#!/usr/bin/env python3
"""G0 integration with the existing SDK/MAME, without OS or emulator edits."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
from check_run import check

ROOT = Path(__file__).resolve().parents[1]

def run(name, arguments, story, script, args):
    output = ROOT/'build'/name
    output.mkdir(parents=True,exist_ok=True)
    if story is None:
        # The SDK preserves cards between runs; this case needs an absent file.
        (output/'card/ZORK1.Z3').unlink(missing_ok=True)
    command = [sys.executable,str(args.sdk/'tools/exerun.py'),
        '--exe',str(ROOT/'build/WestOfHouse/woh.exe'),
        '--map',str(ROOT/'build/target/user/woh/woh.map'),
        '--card',str(output/'card'),'--rundir',str(output/'run'),
        '--mame',str(args.mame),'--flash',str(args.flash),'--plugins',str(args.plugins),
        '--args',arguments,'--exit','--timeout','300',
        '--mame-args=-window -sound none -nothrottle -autoboot_script '+str(ROOT/'tests'/script)+' -autoboot_delay 0']
    if story: command += ['--asset',str(story)+'=ZORK1.Z3']
    print('Running '+name,flush=True)
    with (output/'runner.log').open('w') as log:
        process = subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try: code=process.wait(timeout=args.wall_timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGTERM)
            process.wait(timeout=10)
            raise RuntimeError(name+' timed out')
    if code: raise RuntimeError(name+' failed; see '+str(output/'runner.log'))
    print('Completed '+name,flush=True)
    return output/'run'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,default=ROOT.parent.parent/'ZephyrOS/build/kos-sdk-0.2.0')
    parser.add_argument('--mame',type=Path,default=ROOT.parent.parent/'Zephyr-80/Code/MODERN/Emulator/mame/mamepbitz')
    parser.add_argument('--flash',type=Path,default=ROOT.parent.parent/'ZephyrOS/build/konaos.bin')
    parser.add_argument('--plugins',type=Path,default=ROOT.parent.parent/'Zephyr-80/Code/MODERN/Emulator/mame/plugins')
    parser.add_argument('--mode',choices=['all','interface','errors'],default='all')
    parser.add_argument('--wall-timeout',type=int,default=240)
    parser.add_argument('--no-build',action='store_true')
    args=parser.parse_args()
    if not args.no_build: subprocess.run(['make'],cwd=ROOT,check=True)
    result={'startup':{'arguments':'','story_lookup':'CWD/zork1.z3'}}
    if args.mode in {'all','interface'}:
        directory=run('interface','',ROOT/'build/WestOfHouse/zork1.z3','interface.lua',args)
        session=(directory/'session').read_text().splitlines()
        if 'command\t/fs/WOH.EXE' not in session:
            raise RuntimeError('Acceptance must launch without a story-directory argument')
        check(directory,args.sdk)
        result['interface']=json.loads((directory/'checks.json').read_text())
        text=(directory/'console.txt').read_text()
        samples=re.findall(r'woh-perf: op=(\d+) calls=(\d+) total_us=(\d+) max_us=(\d+)',text)
        result['performance']={int(op):{'calls':int(n),'total_us':int(total),'max_us':int(maximum)} for op,n,total,maximum in samples}
        if len(result['performance'])!=7: raise RuntimeError('Incomplete performance capture')
    if args.mode in {'all','errors'}:
        cases=[('missing-story','',None,'cannot open story file','code -1'),
               ('mismatched-story','',ROOT.parent/'vendor/infocom-zork1/COMPILED/zork1.z3',
                'story does not match the compiled global bindings','code -3')]
        result['errors']={}
        for name,path,story,message,code in cases:
            directory=run(name,path,story,'errors.lua',args)
            text=(directory/'console.txt').read_text()
            if message not in text or code not in text: raise RuntimeError(name+' missing expected diagnostic')
            result['errors'][name]={'diagnostic':message,'returned_to_shell':True}
    (ROOT/'build/target-acceptance.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
