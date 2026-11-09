#!/usr/bin/env python3
"""Add the bordered pointer to a local SDK archive, preserving member bytes.

The exported archive has two gfx_pointer.rel members (C setup and assembly
movement). The automatic placer identifies members by archive/name, so distinct
names are necessary for placed applications. Only names change; the original
member payloads are copied byte-for-byte. The installed SDK is never modified.
"""
import argparse
from collections import Counter
import importlib.util
from pathlib import Path
import subprocess
import tempfile

def extend(sdk, extra, output, sdar):
    spec = importlib.util.spec_from_file_location('sdk_exeplace',sdk/'tools/exeplace.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    members = module.read_archive(str(sdk/'lib/libgfx.lib'))
    counts = Counter(name for name, _ in members)
    seen = Counter()
    output = output.resolve()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        paths = []
        for name, text in members:
            seen[name] += 1
            if counts[name] > 1:
                name = Path(name).stem + '_' + str(seen[name]) + '.rel'
            path = Path(temp)/name
            path.write_bytes(text.encode('latin-1'))
            paths.append(str(path))
        result = Path(temp)/'libgfx.lib'
        subprocess.run([sdar,'-rcD',str(result),*paths,str(extra.resolve())],check=True)
        output.write_bytes(result.read_bytes())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--extra',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sdar',default='sdar')
    args = parser.parse_args()
    extend(args.sdk,args.extra,args.output,args.sdar)
