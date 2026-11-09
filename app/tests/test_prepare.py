import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('woh_prepare',ROOT/'tools/prepare.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PrepareTests(unittest.TestCase):
    def test_existing_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            story = ROOT.parent/'libzvm/build/ZORK1.Z3'
            metadata = ROOT.parent/'libzvm/build/ZORK1.WOH'
            scene = ROOT.parent/'docs/west_of_house_256x144.g7'
            module.prepare(story,metadata,scene,output)
            self.assertEqual((output/'scene-a.bin').read_bytes()+(output/'scene-b.bin').read_bytes(),scene.read_bytes())
            self.assertIn(f'WOH_STATE_INDEX {metadata.read_bytes()[5]}u',(output/'woh_bindings.h').read_text())
            before = (output/'woh_bindings.h').read_bytes()
            module.prepare(story,metadata,scene,output)
            self.assertEqual(before,(output/'woh_bindings.h').read_bytes())

    def test_mismatch_and_image_dimensions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            story = root/'story'; metadata = root/'metadata'; scene = root/'scene'
            data = b'\x03'+b'\x00'*63
            story.write_bytes(data)
            metadata.write_bytes(b'WOH1'+bytes([0,151])+data)
            scene.write_bytes(bytes(36863))
            with self.assertRaises(ValueError): module.prepare(story,metadata,scene,root/'out')
            scene.write_bytes(bytes(36864))
            metadata.write_bytes(b'WOH1'+bytes([0,151])+bytes(64))
            with self.assertRaises(ValueError): module.prepare(story,metadata,scene,root/'out')

if __name__ == '__main__': unittest.main()
