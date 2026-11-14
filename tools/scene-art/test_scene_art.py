"""Phase 1 acceptance, first exercised on Aragain Falls."""
import copy
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from generate import arguments, generate, read, write
from mapping import ROOT, sha
from validate_art import validate_art


class SceneArtTests(unittest.TestCase):
    def args(self, directory, *rooms):
        args=arguments(['--output',str(directory)])
        args.room=list(rooms)
        return args

    def generate(self,args):
        with contextlib.redirect_stdout(io.StringIO()):
            generate(args)

    def test_aragain_runtime_identity_provenance_and_complete_prompts(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);args=self.args(output,'ARAGAIN-FALLS')
            original=sha(ROOT/'assets/source/scene-facts/aragain-falls.scene.json')
            self.generate(args)
            report=read(output/'validation.json')
            self.assertEqual(report['errors'],[])
            self.assertEqual((report['rooms'],report['states'],report['prompts']),(1,2,2))
            self.assertEqual(report['supporting_action_states_excluded'],8)
            one=read(output/'rooms/ARAGAIN-FALLS/states/1.json')
            two=read(output/'rooms/ARAGAIN-FALLS/states/2.json')
            self.assertEqual((one['runtime_room'],one['runtime_state']),(29,1))
            self.assertEqual(one['extracted_states'][0]['id'],'LOOK-STATE-2')
            self.assertEqual(two['extracted_states'][0]['id'],'LOOK-STATE-1')
            for state in [one,two]:
                self.assertIn('450 feet',state['prompt'])
                self.assertIn('north end',state['prompt'])
                self.assertIn('rainbow',state['prompt'])
                self.assertEqual(state['required_visible_objects'],[])
                self.assertEqual(state['target']['height'],159)
                self.assertNotIn('GLOBAL-WATER',state['prompt'])
            self.assertIn('solid rainbow',two['prompt'])
            self.assertEqual(one['artistic'],two['artistic'])
            self.assertEqual(two['reference']['runtime_state'],1)
            self.assertEqual(original,sha(ROOT/'assets/source/scene-facts/aragain-falls.scene.json'))

    def test_idempotent_and_single_room_regeneration(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);args=self.args(output,'ARAGAIN-FALLS','ATLANTIS-ROOM')
            self.generate(args)
            files={p:p.read_bytes() for p in output.rglob('*') if p.is_file()}
            self.generate(args)
            self.assertEqual(files,{p:p.read_bytes() for p in output.rglob('*') if p.is_file()})
            untouched=output/'rooms/ATLANTIS-ROOM/room.json'
            modified=untouched.stat().st_mtime_ns
            self.generate(self.args(output,'ARAGAIN-FALLS'))
            self.assertEqual(untouched.stat().st_mtime_ns,modified)
            self.assertEqual(len(read(output/'manifest.json')['rooms']),2)

    def test_unknown_runtime_room_does_not_invent_zero_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);args=self.args(output,'SANDY-CAVE')
            self.generate(args)
            record=read(output/'manifest.json')['rooms'][0]
            self.assertEqual(record['states'],[])
            self.assertEqual(record['runtime_status'],'incomplete')
            self.assertFalse((output/'rooms/SANDY-CAVE/states/0.json').exists())

    def test_only_existing_states_control_complete_scene_appearance(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);args=self.args(output,'ENTRANCE-TO-HADES')
            self.generate(args)
            record=read(output/'manifest.json')['rooms'][0]
            self.assertEqual([s['runtime_state'] for s in record['states']],[1,2])
            clear=read(output/'rooms/ENTRANCE-TO-HADES/states/1.json')
            blocked=read(output/'rooms/ENTRANCE-TO-HADES/states/2.json')
            self.assertNotIn('GHOSTS',clear['required_visible_objects'])
            self.assertIn('GHOSTS',blocked['required_visible_objects'])
            self.assertIn('no spirits blocking',clear['prompt'])
            self.assertIn('evil spirits barring',blocked['prompt'])
            for state in [clear,blocked]:
                self.assertFalse(any(i['kind'] in (
                    'object_presence_not_observed','lighting_not_observed')
                    for i in state['unresolved']))

    def test_reject_duplicate_pair_or_changed_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);args=self.args(output,'ARAGAIN-FALLS')
            self.generate(args)
            manifest=read(output/'manifest.json')
            manifest['rooms'][0]['states'].append(copy.deepcopy(manifest['rooms'][0]['states'][0]))
            write(output/'manifest.json',manifest)
            self.assertTrue(validate_art(output,args)['errors'])
            self.generate(args)
            path=output/'rooms/ARAGAIN-FALLS/states/1.json';state=read(path)
            state['canonical'][0]['pointer']='/scene/id'
            write(path,state)
            self.assertTrue(validate_art(output,args)['errors'])

    def test_mismatched_story_is_rejected_before_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'assets/source/art-direction';args=self.args(output,'ARAGAIN-FALLS')
            args.story=Path(directory)/'wrong.z3';args.story.write_bytes(b'wrong story')
            with self.assertRaisesRegex(ValueError,'room IDs'):
                self.generate(args)
            self.assertFalse(output.exists())


if __name__=='__main__':
    unittest.main()
