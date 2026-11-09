#!/usr/bin/env python3
"""Check captured G0 VRAM and compare real target output with the host VM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/state-reporting'))
from host_vm import VM

class NarrativeVM(VM):
    def _output(self, user, channel, byte):
        if channel == 0:
            return super()._output(user, channel, byte)
        return 0

def check(directory, sdk):
    def pixels(name): return (directory/(name+'.g7')).read_bytes()
    assert pixels('before-cursor') == pixels('after-cursor'), 'Cursor changed bitmap'
    assert pixels('before-menu') == pixels('after-menu'), 'Menu restoration changed bitmap'
    assert pixels('before-menu') == pixels('after-outside-click'), 'Outside-click menu restoration changed bitmap'
    pointer = (directory/'pointer.bin').read_bytes()
    assert len(pointer)==105
    assert pointer[0:2]==pointer[4:6] and pointer[2]==0 and pointer[6]==4 and pointer[8]==0xd8
    assert pointer[9:25]==bytes([15])*16 and pointer[25:41]==bytes([1])*16, 'Cursor layers have wrong colours'
    assert all((byte&15)==0 for byte in pointer[41+48:41+64]), 'Cursor exceeds 12 columns'
    scene = (ROOT/'docs/west_of_house_256x144.g7').read_bytes()
    for name in ('scene-initial','scene-window-open','scene-window-closed'):
        image = pixels(name)
        assert len(image) == len(scene)
        for offset,(a,b) in enumerate(zip(image,scene)):
            if not (2<=offset%256<140 and 2<=offset//256<20):
                assert a==b, f'Scene changed outside overlay: {name}/{offset}'
    host = ROOT/'app/build/host'
    host.mkdir(parents=True,exist_ok=True)
    library = host/'libzvm-reference.so'
    command = ['cc','-shared','-fPIC','-std=c99','-DZVM_PROFILE=1',
               '-I'+str(ROOT/'libzvm/tests/host/include'),'-I'+str(sdk/'include/abi'),
               '-I'+str(ROOT/'libzvm/include'),'-o',str(library)]
    subprocess.run(command+[str(p) for p in sorted((ROOT/'libzvm/src').glob('*.c'))],check=True)
    vm = NarrativeVM(library,ROOT/'app/build/WestOfHouse/zork1.z3')
    sequence = [None,'look','open mailbox','inventory','north','east','open window',
                'close window','xyzzy','score','quit','y']
    rows = [list(map(int,line.split('\t')[:11])) for line in (directory/'interface.tsv').read_text().splitlines()]
    unique = {row[1]:row for row in rows}
    assert len(unique)==len(sequence),'Missing command boundaries'
    total, value, transcript = 0,5381,bytearray()
    results = []
    metadata = (ROOT/'libzvm/build/ZORK1.WOH').read_bytes()
    for epoch,command in enumerate(sequence,1):
        data = vm.advance(command)
        transcript.extend(data)
        total += len(data)
        for byte in data: value = ((value<<5)+value+byte)&0xffffffff
        row = unique[epoch]
        pair = vm.global_value(metadata[4]),vm.global_value(metadata[5])
        assert tuple(row[2:4])==pair, f'Global pair mismatch at {command}'
        assert row[6]==epoch-1, f'Extra input submission at {command}'
        assert row[9:11]==[total,value], f'Narrative mismatch at {command}: target {row[9:11]}, reference {[total,value]}'
        results.append({'command':command,'here':pair[0],'state':pair[1],
                        'output_bytes':total,'output_hash':value})
    assert vm.last_status==5,'Story did not terminate'
    result = {'boundaries':results,'narrative_sha256':hashlib.sha256(transcript).hexdigest(),
              'cursor_bitmap_unchanged':True,'menu_restoration_exact':True,
              'scene_unchanged_outside_overlay':True,
              'two_sprite_cursor_white_and_dark_blue':True,
              'note':'Target observed through read-only telemetry; reference runs the same unchanged VM core.'}
    (directory/'checks.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--sdk',type=Path,default=ROOT.parent/'ZephyrOS/build/kos-sdk-0.2.0')
    args=parser.parse_args()
    check(args.directory,args.sdk)
