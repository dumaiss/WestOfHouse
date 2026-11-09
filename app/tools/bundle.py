#!/usr/bin/env python3
"""Append plain scene DATA rows to the SDK's automatically placed EXE manifest.

SDK exemanifest currently refuses --segments together with --placement. The
SDK packer supports the resulting combination, so retain its complete placed
manifest and append only the two ordinary blob rows here. No code placement,
veneer, address, executable size or bank mapping is calculated by this tool.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

def bundle(sdk, manifest_path, assets, output):
    manifest = json.loads(manifest_path.read_text())
    segments = manifest.setdefault('segments',[])
    next_id = max((row['id'] for row in segments),default=0)+1
    for ordinal, name in enumerate(('scene-a.bin','scene-b.bin')):
        shutil.copyfile(assets/name,manifest_path.parent/name)
        segments.append({'id':next_id+ordinal,'kind':'DATA','input':name,'bss_size':0})
    path = manifest_path.with_name('woh-bundle.json')
    path.write_text(json.dumps(manifest,indent=2)+'\n')
    subprocess.run([sys.executable,str(sdk/'tools/exepack.py'),'pack',str(path),str(output),
                    '--objdir',str(manifest_path.parent),'--report',str(output.with_suffix('.report.json'))],check=True)
    subprocess.run([sys.executable,str(sdk/'tools/exepack.py'),'validate',str(output)],check=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','manifest','assets','output'): parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    bundle(args.sdk,args.manifest,args.assets,args.output)
