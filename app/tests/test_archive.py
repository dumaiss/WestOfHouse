import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from collections import Counter

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('WOH_SDK_ROOT',ROOT.parent.parent/'ZephyrOS/build/kos-sdk-0.2.0'))
spec = importlib.util.spec_from_file_location('woh_extend',ROOT/'tools/extend_gfx.py')
extension = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extension)

class ArchiveTests(unittest.TestCase):
    def test_sdk_member_payloads_are_preserved(self):
        spec = importlib.util.spec_from_file_location('sdk_archive_test',SDK/'tools/exeplace.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        original = module.read_archive(str(SDK/'lib/libgfx.lib'))
        before = (SDK/'lib/libgfx.lib').read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            extra = folder/'test_extra.rel'
            extra.write_bytes(original[0][1].encode('latin-1'))
            output = folder/'libgfx.lib'
            extension.extend(SDK,extra,output,'sdar')
            modified = module.read_archive(str(output))
            names = [name for name,_ in modified]
            self.assertEqual(len(names),len(set(names)))
            self.assertEqual(Counter(text for _,text in original)+Counter([original[0][1]]),
                             Counter(text for _,text in modified))
        self.assertEqual(before,(SDK/'lib/libgfx.lib').read_bytes())
