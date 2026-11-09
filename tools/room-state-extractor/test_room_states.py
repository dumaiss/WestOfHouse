"""Recognition acceptance fixtures; run with unittest discovery in this directory."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from conditions import (Normalizer, Observation, combine, evaluate, refs, unknowns)
from discovery import Discovery, PathState, State
from extract_room_states import build, yaml_lines
from validation import validate
from zil_source import Source, expression

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(__file__).with_name('extract_room_states.py')

FIXTURE = '''
<GLOBAL COUNT 0>
<GDECL (COUNT) FIX>
<GLOBAL FLAG <>>
<GLOBAL TARGET BOX>
<GLOBAL WORD 65535>
<GLOBAL POINTER <TABLE (PURE) 1 2>>
<ROUTINE POINTER-ARITHMETIC () <+ ,POINTER 1>>
<ROOM PLAIN (LDESC "A bare room.")>
<ROUTINE GENERIC () <MOVE ,BOX ,HERE>>
<ROOM CONDITIONAL (ACTION LOOK-ROOM)>
<ROOM OTHER (LDESC "Other room.")>
<OBJECT BOX (IN CONDITIONAL) (FLAGS OPENBIT) (SIZE 2)>
<ROUTINE LOOK-ROOM (RARG)
 <COND (<EQUAL? .RARG ,M-LOOK>
  <COND (<FSET? ,BOX ,OPENBIT> <TELL "Open.">)
        (<EQUAL? ,COUNT 1> <TELL "One.">)
        (T <TELL "Closed.">)>)>>
<ROOM RELATION (ACTION LOOK-RELATION)>
<ROUTINE LOOK-RELATION (RARG)
 <COND (<EQUAL? .RARG ,M-LOOK>
  <COND (<IN? ,BOX ,RELATION> <TELL "Here.">)
        (T <TELL "Away.">)>)>>
<ROOM UNSUPPORTED (ACTION LOOK-UNKNOWN)>
<ROUTINE LOOK-UNKNOWN (RARG)
 <COND (<EQUAL? .RARG ,M-LOOK>
  <COND (<MAGIC? ,BOX> <TELL "Maybe.">) (T <TELL "Other.">)>)>>
'''


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'fixture.zil').write_text(FIXTURE)
        self.source = Source(self.root)
        self.norm = Normalizer(self.source, 'CONDITIONAL')

    def predicate(self, text):
        return self.norm.condition(expression(text))

    def test_single_state_has_no_observations(self):
        dictionary, report, _ = build(self.source, ['PLAIN'])
        room = dictionary['rooms'][0]
        self.assertEqual([], room['observe'])
        self.assertEqual([{'id': 'INITIAL', 'match': True}], room['states'])
        self.assertEqual('ready', room['status'])

    def test_ordered_cond_and_exhaustive_conditional_room(self):
        discovery = Discovery(self.source, 'CONDITIONAL')
        states = discovery.discover()
        validation = validate(states, discovery.norm.observations)
        self.assertTrue(validation['predicate_partition_complete'])
        self.assertEqual(3, len(states))
        for flag in (False, True):
            for count in (-1, 0, 1, 2):
                self.assertEqual(1, sum(evaluate(s.condition, {'BOX.OPENBIT': flag, 'COUNT': count})
                                        is True for s in states))

    def test_immediate_parent_not_recursive_containment(self):
        dictionary, _, _ = build(self.source, ['RELATION'])
        room = dictionary['rooms'][0]
        self.assertEqual('object_parent', room['observe'][0]['kind'])
        self.assertEqual('object_ref', room['observe'][0]['value_type'])
        d = Discovery(self.source, 'RELATION')
        states = d.discover()
        self.assertEqual(1, sum(evaluate(s.condition, {'BOX.parent': 'RELATION'}) is True for s in states))
        self.assertEqual(1, sum(evaluate(s.condition, {'BOX.parent': 'INNER-BOX'}) is True for s in states))
        self.assertTrue(unknowns(self.predicate('<HELD? ,BOX>')))

    def test_boolean_and_or_not_attributes(self):
        condition = self.predicate('<AND <FSET? ,BOX ,OPENBIT> <NOT <OR ,FLAG <EQUAL? ,COUNT 2>>>>')
        self.assertTrue(evaluate(condition, {'BOX.OPENBIT': True, 'FLAG': False, 'COUNT': 1}))
        self.assertFalse(evaluate(condition, {'BOX.OPENBIT': True, 'FLAG': True, 'COUNT': 1}))
        self.assertFalse(evaluate(self.predicate('<NOT <FSET? ,BOX ,OPENBIT>>'), {'BOX.OPENBIT': True}))

    def test_types_numeric_object_and_property(self):
        self.predicate('<G? ,COUNT -1>')
        self.predicate('<EQUAL? ,TARGET ,BOX>')
        self.predicate('<EQUAL? <GETP ,BOX ,P?SIZE> 2>')
        self.assertEqual('signed16', self.norm.observations['COUNT'].value_type)
        self.assertEqual('object_ref', self.norm.observations['TARGET'].value_type)
        self.assertEqual('unsigned16', self.norm.global_type('WORD'))
        self.assertIsNone(self.norm.global_type('POINTER'))
        self.assertEqual('object_property', self.norm.observations['BOX.P?SIZE'].kind)
        # Unsupported property symbols must not be silently invented.
        self.assertTrue(unknowns(self.predicate('<G? <GETP ,BOX ,P?NONEXISTENT> 2>')))
        self.assertTrue(unknowns(self.predicate('<G? ,TARGET 1>')))

    def test_contradictions_overlaps_and_coverage(self):
        false = self.predicate('<AND <EQUAL? ,COUNT 2> <EQUAL? ,COUNT 3>>')
        a = self.predicate('<FSET? ,BOX ,OPENBIT>')
        b = self.predicate('<EQUAL? ,COUNT 1>')
        states = [State('FALSE', false, '', {}), State('A', a, '', {}), State('B', b, '', {})]
        result = validate(states, self.norm.observations)
        self.assertIn('FALSE', result['contradictory_states'])
        self.assertEqual(['A', 'B'], result['overlaps'][0]['states'])
        self.assertIsNotNone(result['coverage_gap'])
        self.assertFalse(result['predicate_partition_complete'])
        attr = self.predicate('<AND <FSET? ,BOX ,OPENBIT> <NOT <FSET? ,BOX ,OPENBIT>>>')
        self.assertEqual(False, attr)

    def test_unknown_not_erased_by_boolean_reduction(self):
        for text in ('<OR T <MAGIC? ,BOX>>', '<AND <> <MAGIC? ,BOX>>'):
            self.assertIn('<MAGIC? ,BOX>', unknowns(self.predicate(text)))

    def test_unknown_reports_and_strict_fails(self):
        output = self.root / 'states.yaml'
        result = subprocess.run([sys.executable, str(CLI), '--source-root', str(self.root),
                                 '--room', 'UNSUPPORTED', '--output', str(output), '--strict'],
                                capture_output=True, text=True)
        self.assertEqual(1, result.returncode, result.stderr)
        self.assertIn('<MAGIC? ,BOX>', output.with_suffix('.report.json').read_text())
        self.assertNotIn('MAGIC?', output.read_text())
        self.assertIn('status: "unresolved"', output.read_text())

    def test_ordered_postconditions_pre_and_post_increment(self):
        discovery = Discovery(self.source, 'CONDITIONAL')
        before = discovery.norm.condition(expression('<EQUAL? ,COUNT 3>'))
        after = discovery.sequence([expression('<SETG COUNT <+ ,COUNT 1>>')],
                                   [PathState(condition=before)])[0]
        self.assertTrue(evaluate(after.condition, {'COUNT': 4}))
        self.assertFalse(evaluate(after.condition, {'COUNT': 3}))
        after = discovery.sequence([expression('<SETG COUNT 3>'),
                                    expression('<SETG COUNT <+ ,COUNT 1>>'),
                                    expression('<SETG COUNT <- ,COUNT 2>>')])[0]
        self.assertTrue(evaluate(after.condition, {'COUNT': 2}))
        # Comparison after mutation constrains the new value, unlike the above.
        paths = discovery.sequence([expression('<SETG COUNT <+ ,COUNT 1>>'),
                                    expression('<COND (<EQUAL? ,COUNT 3> <FCLEAR ,BOX ,INVISIBLE>)>')])
        clear = next(p for p in paths if 'BOX.INVISIBLE' in refs(p.condition))
        self.assertTrue(evaluate(clear.condition, {'COUNT': 3, 'BOX.INVISIBLE': False}))

    def test_return_stops_mutations(self):
        d = Discovery(self.source, 'CONDITIONAL')
        path = d.sequence([expression('<SETG COUNT 1>'), expression('<RTRUE>'),
                           expression('<SETG COUNT 2>')])[0]
        self.assertTrue(evaluate(path.condition, {'COUNT': 1}))

    def test_unknown_boolean_guard_survives_discovery(self):
        d = Discovery(self.source, 'CONDITIONAL')
        paths = d.sequence([expression('<COND (<AND <> <MAGIC? ,BOX>> <TELL "Unknown.">)>')])
        self.assertTrue(any(unknowns(p.condition) for p in paths))

    def test_counter_mutated_after_text_is_unresolved(self):
        (self.root / 'table.zil').write_text('<GLOBAL TEXTS <TABLE (PURE) "Zero." "One.">>')
        source = Source(self.root)
        d = Discovery(source, 'CONDITIONAL')
        path = d.sequence([expression('<TELL <GET ,TEXTS ,COUNT>>'),
                           expression('<SETG COUNT <+ ,COUNT 1>>')])[0]
        routine = source.index['ROUTINE']['LOOK-ROOM']
        self.assertTrue(d.tables_from_path(path, routine))
        self.assertTrue(all(unknowns(s.condition) for s in d.states))

    def test_opaque_text_interpolation_remains_unresolved(self):
        d = Discovery(self.source, 'CONDITIONAL')
        path = d.sequence([expression('<TELL "Count " <N ,COUNT>>')])[0]
        self.assertIn('<N ,COUNT>', path.limitations[0])

    def test_signed_wrap_and_validation_budget(self):
        d = Discovery(self.source, 'CONDITIONAL')
        path = d.sequence([expression('<SETG COUNT 32767>'),
                           expression('<SETG COUNT <+ ,COUNT 1>>')])[0]
        self.assertTrue(evaluate(path.condition, {'COUNT': -32768}))
        observations = {str(i): Observation('global', str(i), 'boolean') for i in range(16)}
        predicate = {'all': [{'ref': str(i), 'eq': True} for i in range(16)]}
        result = validate([State('BIG', predicate, '', {})], observations)
        self.assertFalse(result['predicate_partition_complete'])
        self.assertTrue(result['uncertainty'])

    def test_yaml_is_parseable_and_deterministic(self):
        import yaml  # Acceptance-test dependencies only; generator uses stdlib.
        import jsonschema
        dictionary, _, _ = build(self.source, sorted(self.source.index['ROOM']))
        text = '\n'.join(yaml_lines(dictionary)) + '\n'
        self.assertEqual(dictionary, yaml.safe_load(text))
        schema = json.loads((ROOT / 'schemas/room-state-dictionary-v1.schema.json').read_text())
        jsonschema.Draft202012Validator(schema).validate(dictionary)
        self.assertEqual(text, '\n'.join(yaml_lines(copy.deepcopy(dictionary))) + '\n')
        self.assertEqual({'schema', 'rooms'}, set(dictionary))
        self.assertNotIn('source_expression', text)

    def test_missing_or_unsupported_observations_are_unresolved(self):
        candidate = State('MISSING', {'ref': 'MISSING', 'eq': 1}, '', {})
        result = validate([candidate], {})
        self.assertFalse(result['ready'])
        self.assertEqual('unresolved', result['state_status']['MISSING'])
        self.assertEqual('unresolved', candidate.output(result)['status'])
        invalid = Observation('unsupported_query', 'MISSING', 'signed16')
        result = validate([candidate], {'MISSING': invalid})
        self.assertFalse(result['ready'])
        self.assertEqual('unsupported_observation_kind', result['observation_issues'][0]['reason'])
        result = validate([candidate], {'MISSING': Observation('global', 'MISSING', 'signed16')}, self.source)
        self.assertEqual('missing_global_symbol', result['observation_issues'][0]['reason'])
        self.assertFalse(validate([], {})['ready'])

    def test_asset_discovery_warning_is_not_a_readiness_input(self):
        from unittest.mock import patch
        original = Discovery.discover

        def discover_with_unrelated_warning(discovery):
            states = original(discovery)
            discovery.issues.append({'reason': 'object_hotspot_menu_mapping_incomplete'})
            return states

        with patch.object(Discovery, 'discover', discover_with_unrelated_warning):
            dictionary, report, _ = build(self.source, ['PLAIN'])
        self.assertEqual('ready', dictionary['rooms'][0]['status'])
        self.assertNotIn('object_hotspot_menu_mapping_incomplete', json.dumps(report))

    def test_mutation_without_candidate_does_not_block_readiness(self):
        (self.root / 'mutation.zil').write_text('''
<ROOM MUTATION (LDESC "Room.") (ACTION MUTATE)>
<ROUTINE MUTATE () <COND (<VERB? WAVE> <SETG COUNT <+ ,COUNT 1>>)>>
''')
        dictionary, report, _ = build(Source(self.root), ['MUTATION'])
        room = dictionary['rooms'][0]
        self.assertEqual('ready', room['status'])
        self.assertEqual([{'id': 'INITIAL', 'match': True}], room['states'])
        self.assertEqual([], room['observe'])
        self.assertEqual([], report['rooms'][0]['validation']['blocking_reasons'])

    def test_default_cli_has_no_compatibility_output_and_strict_passes(self):
        output = self.root / 'plain.yaml'
        result = subprocess.run([sys.executable, str(CLI), '--source-root', str(self.root),
                                 '--room', 'PLAIN', '--strict', '--output', str(output)],
                                capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertFalse(output.with_suffix('.compatibility.json').exists())
        self.assertEqual('room_state_diagnostics_v2', json.loads(output.with_suffix('.report.json').read_text())['schema'])

    def test_list_unknown_and_multiple_rooms(self):
        args = [sys.executable, str(CLI), '--source-root', str(self.root)]
        result = subprocess.run(args + ['--list-rooms'], capture_output=True, text=True)
        self.assertEqual(sorted(self.source.index['ROOM']), result.stdout.splitlines())
        result = subprocess.run(args + ['--room', 'MISSING'], capture_output=True)
        self.assertEqual(2, result.returncode)
        dictionary, _, _ = build(self.source, ['PLAIN', 'OTHER', 'PLAIN'])
        self.assertEqual(['OTHER', 'PLAIN'], [r['id'] for r in dictionary['rooms']])


class CorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Source(ROOT / 'vendor/infocom-zork1')

    def test_sandy_cave_acceptance_and_asset_mapping(self):
        dictionary, report, mapping = build(self.source, ['SANDY-CAVE'], ROOT / 'generated-scenes')
        import yaml
        expected = yaml.safe_load((Path(__file__).parent / 'fixtures/sandy-cave.yaml').read_text())
        self.assertEqual(expected, dictionary)
        room = dictionary['rooms'][0]
        self.assertEqual({'BEACH-DIG', 'SCARAB.INVISIBLE'}, {o['id'] for o in room['observe']})
        states = {s['id']: s for s in room['states']}
        for n in range(3):
            self.assertEqual({'ref': 'BEACH-DIG', 'eq': n}, states[f'BEACH-DIG-STATE-{n}']['match'])
        self.assertEqual({'ref': 'BEACH-DIG', 'eq': -1}, states['INITIAL']['match'])
        post = next(s for s in room['states'] if s['id'].startswith('POST-'))
        self.assertTrue(evaluate(post['match'], {'BEACH-DIG': 3, 'SCARAB.INVISIBLE': False}))
        self.assertFalse(evaluate(post['match'], {'BEACH-DIG': 4, 'SCARAB.INVISIBLE': False}))
        aliases = mapping['rooms'][0]['mappings']
        self.assertEqual('ACTION-STATE-2', next(m['asset_state'] for m in aliases if m['state'] == post['id']))
        self.assertTrue(any(i['kind'] == 'changed_meaning' for i in mapping['rooms'][0]['issues']))
        self.assertIsNotNone(report['rooms'][0]['validation']['coverage_gap'])

    def test_real_single_state_and_conditional_description(self):
        dictionary, report, _ = build(self.source, ['DEAD-END-1', 'DAM-ROOM'], ROOT / 'generated-scenes')
        single = next(r for r in dictionary['rooms'] if r['id'] == 'DEAD-END-1')
        self.assertEqual([], single['observe'])
        self.assertEqual('ready', single['status'])
        dam = next(r for r in report['rooms'] if r['room'] == 'DAM-ROOM')
        self.assertEqual(8, len(dam['states']))
        self.assertTrue(all(s['status'] == 'resolved' for s in dam['states']))
        self.assertTrue(dam['validation']['predicate_partition_complete'])

    def test_required_correction_rooms(self):
        names = ['ATLANTIS-ROOM', 'COLD-PASSAGE', 'WEST-OF-HOUSE', 'EAST-OF-HOUSE',
                 'SANDY-CAVE', 'LIVING-ROOM']
        dictionary, report, _ = build(self.source, names)
        rooms = {r['id']: r for r in dictionary['rooms']}
        checks = {r['room']: r for r in report['rooms']}
        for name in ['ATLANTIS-ROOM', 'COLD-PASSAGE']:
            self.assertEqual('ready', rooms[name]['status'])
            self.assertEqual([], rooms[name]['observe'])
            self.assertEqual([{'id': 'INITIAL', 'match': True}], rooms[name]['states'])
        for name, observation in [('WEST-OF-HOUSE', 'WON-FLAG'),
                                  ('EAST-OF-HOUSE', 'KITCHEN-WINDOW.OPENBIT')]:
            self.assertEqual('ready', rooms[name]['status'])
            self.assertEqual(2, len(rooms[name]['states']))
            self.assertEqual([observation], [o['id'] for o in rooms[name]['observe']])
            self.assertEqual([], checks[name]['validation']['overlaps'])
            self.assertIsNone(checks[name]['validation']['coverage_gap'])
        self.assertEqual('incomplete', rooms['SANDY-CAVE']['status'])
        self.assertIsNotNone(checks['SANDY-CAVE']['validation']['coverage_gap'])
        self.assertEqual('incomplete', rooms['LIVING-ROOM']['status'])
        self.assertTrue(checks['LIVING-ROOM']['validation']['overlaps'])

    def test_optional_legacy_comparison_cannot_affect_recognition(self):
        plain = build(self.source, ['SANDY-CAVE', 'ATLANTIS-ROOM'])
        legacy = build(self.source, ['SANDY-CAVE', 'ATLANTIS-ROOM'], ROOT / 'generated-scenes')
        self.assertEqual(plain[:2], legacy[:2])
        self.assertIsNone(plain[2])
        self.assertTrue(any(r['issues'] for r in legacy[2]['rooms']))
        self.assertNotIn('compatibility_issues', legacy[1]['summary'])
        serialized = json.dumps(legacy[1])
        for excluded in ('object_hotspot_menu_mapping_incomplete', 'exit_menu_state_mapping_incomplete',
                         'external_presentation_effect_unmapped', 'mutation_presentation_mapping_unproven',
                         'no_verified_mapping', 'unmapped_legacy_state', 'changed_meaning'):
            self.assertNotIn(excluded, serialized)

    def test_strict_accepts_ready_rooms_even_with_legacy_issues(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'states.yaml'
            result = subprocess.run([sys.executable, str(CLI), '--room', 'ATLANTIS-ROOM',
                                     '--room', 'WEST-OF-HOUSE', '--room', 'EAST-OF-HOUSE',
                                     '--compare-legacy', '--strict', '--output', str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            comparison = json.loads(output.with_suffix('.compatibility.json').read_text())
            self.assertTrue(any(r['issues'] for r in comparison['rooms']))
            self.assertEqual(0, json.loads(output.with_suffix('.report.json').read_text())['summary']['rooms_not_ready'])

    def test_full_corpus_is_deterministic(self):
        rooms = sorted(self.source.index['ROOM'])
        self.assertEqual(110, len(rooms))
        a = build(self.source, rooms, ROOT / 'generated-scenes')
        b = build(self.source, list(reversed(rooms)), ROOT / 'generated-scenes')
        self.assertEqual(a, b)
        self.assertEqual(110, a[1]['summary']['rooms_processed'])
        self.assertGreater(a[1]['summary']['rooms_not_ready'], 0)
        summary = a[1]['summary']
        total = sum(len(r['states']) for r in a[0]['rooms'])
        self.assertEqual(total, sum(summary[k] for k in ('resolved_states', 'unresolved_states', 'ambiguous_states', 'contradictory_states')))
        self.assertEqual(110, summary['rooms_ready'] + summary['rooms_not_ready'])
        for room, report in zip(a[0]['rooms'], a[1]['rooms']):
            self.assertEqual(room['status'], report['status'])
            self.assertEqual(room['status'] == 'ready', report['validation']['ready'])
            used = set().union(*(refs(s['match']) for s in room['states'] if 'match' in s))
            self.assertEqual(used, {o['id'] for o in room['observe']})



if __name__ == '__main__':
    unittest.main()
