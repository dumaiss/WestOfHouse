#!/usr/bin/env python3
"""Exercise pilot commands against the unchanged host VM using normal play.

No game globals are mutated. A refusal (boarded door, closed window, etc.)
is valid; parser vocabulary/visibility/ambiguity failures require review.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from generate import ROOT, arguments, read, sha, write

sys.path.insert(0,str(ROOT/'tools/state-reporting'))
from host_vm import VM


def main():
    args=arguments();folder=args.output/'acceptance';folder.mkdir(parents=True,exist_ok=True)
    build=tempfile.TemporaryDirectory()
    library=Path(build.name)/'libzvm-host.so'
    command=['cc','-shared','-fPIC','-O2','-DZVM_PROFILE=1','-I'+str(ROOT/'libzvm/include'),
             '-I'+str(ROOT/'libzvm/tests/host/include'),
             *map(str,sorted((ROOT/'libzvm/src').glob('*.c'))),'-o',str(library)]
    subprocess.run(command,check=True)
    story=ROOT/'libzvm/build/ZORK1.Z3'
    inside=['north','east','open window','enter window']
    attic=inside+['west','take lamp','turn on lamp','east','up']
    paths={'WEST-OF-HOUSE':[],'NORTH-OF-HOUSE':['north'],'SOUTH-OF-HOUSE':['south'],
           'EAST-OF-HOUSE':['north','east'],'KITCHEN':inside,'ATTIC':attic}
    report={'schema':'woh_hotspot_parser_checks_v1','story_sha256':sha(story),
            'host_build':command[:-1]+['<temporary host library>'],
            'setup':'Normal parser commands only; no VM/global/object mutation.',
            'checks':[],'warnings':[]}
    bindings=(ROOT/'libzvm/build/ZORK1.WOH').read_bytes();here_index=bindings[4]
    for row in read(args.output/'manifest.json')['scenes']:
        scene=read(args.output/row['manifest']);setup=list(paths[scene['room_name']])
        if scene['room_name']=='EAST-OF-HOUSE' and scene['state']==2:setup+=['open window']
        if scene['room_name']=='KITCHEN' and scene['state']==1:setup+=['close window']
        unique={a['command'] for s in scene['hotspots'] for a in s['actions']}
        for text in sorted(unique):
            vm=VM(library,story);vm.advance()
            for step in setup:vm.advance(step)
            if vm.global_value(here_index)!=scene['room']:
                raise ValueError('Normal-play setup reached wrong room for '+scene['asset_key'])
            output=vm.advance(text).decode('ascii',errors='replace')
            problem=any(term in output.lower() for term in ["i don't know the word",'i do not know the word',
                        "you can't see any",'you cannot see any','which do you mean',
                        'what do you want to','what do you mean','used the word'])
            result={'asset_key':scene['asset_key'],'setup':setup,'command':text,
                    'result':'needs_review' if problem else 'parser_accepted','output':output}
            if scene['room_name']=='WEST-OF-HOUSE' and scene['state']==2:
                result['scope_note']='Same source room in normal initial play; winning flag is not forced. Southwest is expected to be refused before winning.'
            report['checks'].append(result)
            if problem:report['warnings'].append({'asset_key':scene['asset_key'],'command':text,'output':output})
    report['commands_checked']=len(report['checks'])
    report['parser_accepted']=sum(r['result']=='parser_accepted' for r in report['checks'])
    build.cleanup()
    write(folder/'parser-checks.json',report)
    print(json.dumps({k:v for k,v in report.items() if k not in ('checks','host_build')},indent=2))
    raise SystemExit(bool(report['warnings']))


if __name__=='__main__':main()
