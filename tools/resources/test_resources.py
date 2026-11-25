"""Manifest validation and deterministic package acceptance."""
import copy
from pathlib import Path
import tempfile
import unittest

import build


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.manifest = build.read(build.ROOT/'assets/manifests/resources.json')

    def rejects(self, mutate):
        value=copy.deepcopy(self.manifest);mutate(value)
        with self.assertRaises((ValueError,KeyError)):
            build.declaration(value)

    def test_mapping_and_explicit_selection(self):
        pairs=build.declaration(self.manifest)
        self.assertEqual(len(pairs['scene']),20)
        self.assertEqual(len(self.manifest['resources']),43)
        self.assertNotIn((201,0),pairs['scene'])
        self.assertIn('WoHTitle.img',[r['id'] for r in self.manifest['resources']])

    def test_duplicates_missing_pairs_and_invented_states(self):
        self.rejects(lambda m:m['resources'].append(copy.deepcopy(m['resources'][1])))
        self.rejects(lambda m:m['resources'].pop(2))
        self.rejects(lambda m:m['resources'][1].update(state=0))
        self.rejects(lambda m:m['resources'][1].update(hotspots='wrong'))
        self.rejects(lambda m:m['resources'][1].update(crop_box=[0,0,100,100]))
        self.rejects(lambda m:m['resources'][1].update(mode='g7'))
        self.rejects(lambda m:m['resources'][1].update(source='assets/source/missing.png'))

    def test_review_selection_and_unexpected_types(self):
        self.rejects(lambda m:m['resources'][1].update(path='assets/review/white-house/contact-sheet-originals.png'))
        self.rejects(lambda m:m['resources'][1].update(type='review'))
        self.rejects(lambda m:m['resources'][1].update(id='../escape'))
        self.rejects(lambda m:m['resources'][1].update(path='assets/runtime/scenes/source.png'))
        path=build.ROOT/'assets/runtime/test-orphan.tmp'
        try:
            path.write_bytes(b'orphan')
            with self.assertRaisesRegex(ValueError,'Orphan'):build.declaration(self.manifest)
        finally:path.unlink(missing_ok=True)

    def test_one_background_music_stream(self):
        music=[r for r in self.manifest['resources'] if r['type']=='music']
        self.assertEqual([r['id'] for r in music],['advent4.pcm'])
        codec,rate,samples=build.pcm_header(build.local(music[0]['path']))
        self.assertEqual((codec,rate),(1,16000))
        self.assertGreater(samples,0)
        # A second stream, or a music row that is not a .pcm, is refused.
        self.rejects(lambda m:m['resources'].append(dict(music[0],id='second.pcm')))
        self.rejects(lambda m:[r.update(path='assets/runtime/story/zork1.z3') for r in m['resources'] if r['type']=='music'])
        with tempfile.TemporaryDirectory() as temp:
            bad=Path(temp)/'bad.pcm'
            data=bytearray(build.local(music[0]['path']).read_bytes()[:4096])
            bad.write_bytes(bytes(data))
            with self.assertRaisesRegex(ValueError,'Invalid PCM'):build.pcm_header(bad)

    def test_preserved_art_and_payloads(self):
        report=build.read(build.ROOT/'assets/review/migration.json')
        for row in report['binary_preservation']:
            self.assertEqual(build.sha(build.ROOT/row['path']),row['sha256'],row['path'])

    def test_incremental_and_deterministic_archive(self):
        sdk=build.ROOT.parent/'ZephyrOS/build-mk2/sys/kos-sdk-0.2.0-mk2'
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);archive=folder/'WOH.KPR'
            pairs,changed=build.build_rows(self.manifest,sdk,folder,False)
            build.pack(self.manifest,sdk,archive,folder)
            digest=build.sha(archive)
            runtime={r['id']:(build.local(r['path']).stat().st_mtime_ns,build.sha(build.local(r['path']))) for r in self.manifest['resources']}
            pairs,changed=build.build_rows(self.manifest,sdk,folder,False)
            self.assertEqual(changed,[])
            build.pack(self.manifest,sdk,archive,folder)
            self.assertEqual(digest,build.sha(archive))
            for row in self.manifest['resources']:
                p=build.local(row['path'])
                self.assertEqual(runtime[row['id']],(p.stat().st_mtime_ns,build.sha(p)))

    def test_bad_kimg_and_hotspot_bounds(self):
        from codec import encode,decode
        scene=build.read(build.ROOT/'assets/source/hotspots/scenes/room-180-state-1.json')
        scene['hotspots'][0]['rect']=[250,150,20,20]
        with self.assertRaises(ValueError):decode(encode(scene))
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'bad.img';path.write_bytes(b'KIMG')
            with self.assertRaises(ValueError):build.kimg_header(path)


if __name__=='__main__':unittest.main()
