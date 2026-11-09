"""Compile and execute the actual Zork I source using the existing toolchain."""
import copy
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from bindings import compiled_symbols, write_bindings
from build_story import ROOT, build
from generate_states import Emitter, generate, yaml
from host_vm import VM, Context
from conditions import evaluate

OUT = ROOT/'build/state-reporting'


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.dictionary = yaml.safe_load((ROOT/'generated-room-states/room-states.yaml').read_text())

    def test_all_ready_rooms_and_stable_local_ids(self):
        text, mapping = generate(self.dictionary)
        self.assertEqual(99, sum(r['status'] == 'ready' for r in mapping['rooms']))
        self.assertEqual(11, sum(r['status'] == 'incomplete' for r in mapping['rooms']))
        self.assertEqual(99, text.count('(<EQUAL? ,HERE '))
        self.assertIn('<SETG WOH-STATE 0>', text)
        self.assertNotIn('<TELL', text)
        reversed_input = copy.deepcopy(self.dictionary)
        reversed_input['rooms'].reverse()
        for room in reversed_input['rooms']:
            room['states'].reverse()
        self.assertEqual((text, mapping), generate(reversed_input))
        for room in mapping['rooms']:
            self.assertEqual(list(range(1, len(room['states']) + 1)), [s['value'] for s in room['states']])

    def test_invalid_ready_dictionary_rejected(self):
        room = next(r for r in self.dictionary['rooms'] if r['id'] == 'WEST-OF-HOUSE')
        room['states'][1]['match'] = room['states'][0]['match']
        with self.assertRaises(ValueError):
            generate(self.dictionary)
        room['states'].pop()
        with self.assertRaises(ValueError):
            generate(self.dictionary)

    def test_signed_unsigned_parent_and_property_operators(self):
        emitter = Emitter([
            {'id': 'word', 'kind': 'global', 'symbol': 'WORD', 'value_type': 'unsigned16'},
            {'id': 'signed', 'kind': 'global', 'symbol': 'COUNT', 'value_type': 'signed16'},
            {'id': 'parent', 'kind': 'object_parent', 'object': 'BOX', 'value_type': 'object_ref'},
            {'id': 'property', 'kind': 'object_property', 'object': 'BOX', 'property': 'P?SIZE', 'value_type': 'signed16'},
        ])
        self.assertEqual('<NOT <L? <+ ,WORD -32768> 0>>', emitter.condition({'ref': 'word', 'ge': 32768}))
        self.assertEqual('<L? ,COUNT 0>', emitter.condition({'ref': 'signed', 'lt': 0}))
        self.assertEqual('<EQUAL? <LOC ,BOX> ,ROOM>', emitter.condition({'ref': 'parent', 'eq': 'ROOM'}))
        self.assertEqual('<NOT <EQUAL? <GETP ,BOX ,P?SIZE> 2>>', emitter.condition({'ref': 'property', 'ne': 2}))
        with self.assertRaises(ValueError):
            emitter.condition({'ref': 'parent', 'lt': 'ROOM'})

    def test_bindings_follow_compiler_global_order(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            data = folder/'symbols.zap'
            story = folder/'story.z3'
            raw = bytearray(1024)
            raw[0] = 3
            raw[12:14] = (64).to_bytes(2, 'big')
            story.write_bytes(raw)
            data.write_text('.GVAR PREFIX=0\n.GVAR HERE=0\n.GVAR WOH-STATE=0\n')
            manifest = write_bindings(story, data, folder/'story.WOH')
            self.assertEqual({'HERE': 1, 'WOH-STATE': 2}, manifest['globals'])
            self.assertEqual(70, len((folder/'story.WOH').read_bytes()))


class StoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = build(OUT/'baseline', baseline=True)
        cls.instrumented = build(OUT/'instrumented')
        cls.library = OUT/'libzvm-host.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-DZVM_PROFILE=1',
                        '-I'+str(ROOT/'libzvm/tests/host/include'), '-I'+str(ROOT/'libzvm/include'),
                        '-I'+str(ROOT/'libzvm/src'), '-I'+str(ROOT/'libzvm/tests'),
                        *map(str, sorted((ROOT/'libzvm/src').glob('*.c'))),
                        str(ROOT/'libzvm/tests/state_report.c'), '-o', str(cls.library)], check=True)
        cls.symbols = compiled_symbols(cls.instrumented/'zork1_data.zap')
        cls.mapping = json.loads((ROOT/'generated-zil/woh-state-ids.json').read_text())
        cls.dictionary = yaml.safe_load((ROOT/'generated-room-states/room-states.yaml').read_text())
        cls.results = {}

    @classmethod
    def tearDownClass(cls):
        (OUT/'host-acceptance.json').write_text(json.dumps(cls.results, indent=2) + '\n')

    def machine(self):
        vm = VM(self.library, self.instrumented/'ZORK1.Z3')
        vm.advance()
        return vm

    def observed(self, vm):
        # Exercise exactly the C adapter used by target ZVMTest, not a second
        # symbolic selector or hard-coded compiled-global location.
        report = (C.c_uint8 * 3)()
        payload = (C.c_uint8 * 70).from_buffer_copy((self.instrumented/'ZORK1.WOH').read_bytes())
        header = (C.c_uint8 * 64).from_buffer_copy(vm.original_header)
        init = vm.lib.woh_state_report_init
        init.argtypes = [C.c_void_p, C.c_void_p, C.c_uint16, C.c_void_p]
        self.assertEqual(1, init(report, payload, 70, header))
        read = vm.lib.woh_state_report_read
        read.argtypes = [C.POINTER(Context), C.c_void_p, C.POINTER(C.c_uint16), C.POINTER(C.c_uint16)]
        here, state = C.c_uint16(), C.c_uint16()
        before = bytes(vm.story)
        self.assertEqual(0, read(C.byref(vm.context), report, C.byref(here), C.byref(state)))
        self.assertEqual(before, bytes(vm.story))
        return here.value, state.value

    def place(self, vm, name):
        target = self.symbols['objects'][name]
        vm.move_object(vm.global_value(self.symbols['globals']['PLAYER']), target)
        vm.set_global(self.symbols['globals']['HERE'], target)

    def test_initial_transition_and_window(self):
        vm = self.machine()
        pairs = [self.observed(vm)]
        for command in ['north', 'east', 'open window', 'close window']:
            vm.advance(command)
            pairs.append(self.observed(vm))
        objects = self.symbols['objects']
        self.assertEqual([(objects['WEST-OF-HOUSE'], 1), (objects['NORTH-OF-HOUSE'], 1),
                          (objects['EAST-OF-HOUSE'], 1), (objects['EAST-OF-HOUSE'], 2),
                          (objects['EAST-OF-HOUSE'], 1)], pairs)
        self.results['normal_transitions_and_window'] = pairs

    def test_global_dependent_state(self):
        vm = self.machine()
        seen = []
        for won in (0, 1, 0):
            vm.set_global(self.symbols['globals']['WON-FLAG'], won)
            vm.advance('look')
            seen.append(self.observed(vm)[1])
        self.assertEqual([1, 2, 1], seen)
        self.results['won_flag_fixture'] = seen

    def test_dam_multiple_observations(self):
        vm = self.machine()
        self.place(vm, 'DAM-ROOM')
        room = next(r for r in self.dictionary['rooms'] if r['id'] == 'DAM-ROOM')
        mapping = next(r for r in self.mapping['rooms'] if r['room'] == 'DAM-ROOM')
        numeric = {s['id']: s['value'] for s in mapping['states']}
        results = []
        for values in itertools.product((False, True), repeat=3):
            readings = dict(zip(('GATE-FLAG', 'GATES-OPEN', 'LOW-TIDE'), values))
            for key, value in readings.items():
                vm.set_global(self.symbols['globals'][key], int(value))
            vm.advance('look')
            actual = self.observed(vm)
            matches = [s for s in room['states'] if evaluate(s['match'], readings)]
            self.assertEqual(1, len(matches))
            self.assertEqual((self.symbols['objects']['DAM-ROOM'], numeric[matches[0]['id']]), actual)
            results.append({'values': readings, 'state': actual[1]})
        self.assertEqual(set(range(1, 9)), {r['state'] for r in results})
        self.results['dam_room_fixture'] = results

    def test_incomplete_room_and_reset(self):
        vm = self.machine()
        self.place(vm, 'SANDY-CAVE')
        vm.set_global(self.symbols['globals']['WOH-STATE'], 99)
        vm.advance('look')
        self.assertEqual((self.symbols['objects']['SANDY-CAVE'], 0), self.observed(vm))
        self.results['incomplete_sandy_cave'] = self.observed(vm)

    def test_loud_room_shared_input_path(self):
        vm = self.machine()
        self.place(vm, 'ROUND-ROOM')
        for key in ('GATES-OPEN', 'LOW-TIDE', 'LOUD-FLAG'):
            vm.set_global(self.symbols['globals'][key], 0)
        vm.advance('east')
        self.assertEqual((self.symbols['objects']['LOUD-ROOM'], 2), self.observed(vm))
        vm.advance('echo')
        self.assertEqual((self.symbols['objects']['LOUD-ROOM'], 1), self.observed(vm))
        self.results['loud_room_shared_read'] = self.observed(vm)

    def test_metadata_rejection(self):
        vm = self.machine()
        report = (C.c_uint8 * 3)()
        data = bytearray((self.instrumented/'ZORK1.WOH').read_bytes())
        header = (C.c_uint8 * 64).from_buffer_copy(vm.original_header)
        init = vm.lib.woh_state_report_init
        init.argtypes = [C.c_void_p, C.c_void_p, C.c_uint16, C.c_void_p]
        for index in (0, 6, 20, 69):
            corrupted = data.copy()
            corrupted[index] ^= 1
            payload = (C.c_uint8 * 70).from_buffer_copy(corrupted)
            self.assertEqual(0, init(report, payload, 70, header))
            self.assertEqual(0, report[2])
        payload = (C.c_uint8 * 70).from_buffer_copy(data)
        self.assertEqual(0, init(report, payload, 69, header))
        self.assertEqual(1, init(report, payload, 70, header))

    def test_gameplay_regression_and_instruction_cost(self):
        baseline = VM(self.library, self.baseline/'ZORK1.Z3')
        instrumented = VM(self.library, self.instrumented/'ZORK1.Z3')
        b_symbols = compiled_symbols(self.baseline/'zork1_data.zap')
        self.assertEqual(b_symbols['objects'], self.symbols['objects'])
        commands = [None, 'look', 'open mailbox', 'take leaflet', 'read leaflet', 'inventory',
                    'score', 'look', 'examine mailbox', 'close mailbox', 'open mailbox',
                    'north', 'east', 'open window', 'close window', 'quit', 'y']
        results = []
        b_before = i_before = 0
        digest = hashlib.sha256()
        for command in commands:
            if command == 'quit':
                instrumented.set_global(self.symbols['globals']['WOH-STATE'], 4242)
            b_text = baseline.advance(command)
            i_text = instrumented.advance(command)
            self.assertEqual(b_text, i_text, command)
            self.assertEqual(baseline.last_status, instrumented.last_status)
            if command in ('quit', 'y'):
                self.assertEqual(4242, instrumented.global_value(self.symbols['globals']['WOH-STATE']))
            self.assertEqual(baseline.status_events, instrumented.status_events)
            for name in ('HERE', 'SCORE', 'MOVES', 'PLAYER', 'WINNER', 'WON-FLAG'):
                self.assertEqual(baseline.global_value(b_symbols['globals'][name]),
                                 instrumented.global_value(self.symbols['globals'][name]), (command, name))
            for object_id in self.symbols['objects'].values():
                b = baseline.object_entry(object_id)
                i = instrumented.object_entry(object_id)
                self.assertEqual(baseline.story[b:b + 7], instrumented.story[i:i + 7])
            digest.update(b_text)
            b_count, i_count = baseline.instructions - b_before, instrumented.instructions - i_before
            results.append({'command': command, 'baseline_instructions': b_count,
                            'instrumented_instructions': i_count, 'added_instructions': i_count - b_count})
            b_before, i_before = baseline.instructions, instrumented.instructions
        self.assertEqual(5, instrumented.last_status)
        # Confirmation is a separate native READ: resolver is not called again.
        self.assertEqual(0, results[-1]['added_instructions'])
        self.assertLessEqual(results[-2]['added_instructions'], 1)  # Return from preceding WOH-READ only.
        self.results['regression'] = {'turns': results, 'identical_output_sha256': digest.hexdigest(),
                                      'termination': 'ZVM_FINISHED',
                                      'fixture_note': 'Global/Dam fixtures use public logical writes; no full win walkthrough claimed.'}

    def test_reproducible_story(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            repeated = build(directory)
            self.assertEqual((self.instrumented/'ZORK1.Z3').read_bytes(), (repeated/'ZORK1.Z3').read_bytes())
            self.assertEqual((self.instrumented/'ZORK1.WOH').read_bytes(), (repeated/'ZORK1.WOH').read_bytes())


if __name__ == '__main__':
    unittest.main()
