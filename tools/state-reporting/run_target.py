#!/usr/bin/env python3
"""Run reporting/overhead acceptance through the existing KonaOS SDK runner.

Uses the installed MAME binary and plugin; never edits the emulator, OS or SDK.
Each case has a separate card directory so baseline runs cannot inherit sidecars.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

from bindings import compiled_symbols
from build_story import ROOT, build

OUT = ROOT/'build/state-reporting'


def run_case(name, story, bindings, arguments, script, args):
    directory = OUT/name
    directory.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(args.sdk/'tools/exerun.py'),
               '--exe', str(ROOT/'libzvm/build/ZVMTEST.EXE'),
               '--map', str(ROOT/'libzvm/build/optimized/target/user/zvmtest/zvmtest.map'),
               '--card', str(directory/'card'), '--rundir', str(directory/'run'),
               '--mame', str(args.mame), '--flash', str(args.flash), '--plugins', str(args.plugins),
               '--asset', str(story)+'=ZORK1.Z3', '--args', arguments, '--exit', '--timeout', '300',
               '--mame-args=-window -sound none -nothrottle -autoboot_script '+str(script)+' -autoboot_delay 0']
    if bindings is not None:
        command += ['--asset', str(bindings)+'=ZORK1.WOH']
    print('Running '+name, flush=True)
    with (directory/'runner.log').open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT/'libzvm', stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=args.wall_timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=10)
            raise RuntimeError(name+' exceeded its wall-clock timeout')
    if code:
        raise RuntimeError(name+' failed; see '+str(directory/'runner.log'))
    print('Completed '+name, flush=True)
    return directory/'run'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['all','keyboard','benchmark'], default='all')
    parser.add_argument('--sdk', type=Path, default=ROOT.parent/'ZephyrOS/build/kos-sdk-0.2.0')
    parser.add_argument('--mame', type=Path, default=ROOT.parent/'Zephyr-80/Code/MODERN/Emulator/mame/mamepbitz')
    parser.add_argument('--flash', type=Path, default=ROOT.parent/'ZephyrOS/build/konaos.bin')
    parser.add_argument('--plugins', type=Path, default=ROOT.parent/'Zephyr-80/Code/MODERN/Emulator/mame/plugins')
    parser.add_argument('--wall-timeout', type=int, default=240)
    parser.add_argument('--no-build', action='store_true', help='Use the already built matching acceptance bundle')
    args = parser.parse_args()
    if not args.no_build:
        subprocess.run(['make','-C',str(ROOT/'libzvm'),'story'],check=True)
        build(OUT/'baseline',baseline=True)
    story, bindings = OUT/'instrumented/ZORK1.Z3', OUT/'instrumented/ZORK1.WOH'
    result = {}
    if args.mode in {'all','keyboard'}:
        folder = run_case('keyboard',story,bindings,'-p /fs/ZORK1.Z3',ROOT/'libzvm/tests/state_play.lua',args)
        entries = [line.split('\t') for line in (folder/'state-input.tsv').read_text().splitlines()]
        symbols = compiled_symbols(OUT/'instrumented/zork1_data.zap')
        objects = symbols['objects']
        expected = [(objects['WEST-OF-HOUSE'],1), (objects['NORTH-OF-HOUSE'],1),
                    (objects['EAST-OF-HOUSE'],1), (objects['EAST-OF-HOUSE'],2), (objects['EAST-OF-HOUSE'],1)]
        actual = [(int(e[1]),int(e[2])) for e in entries[:5]]
        if actual != expected or len(entries)!=6 or entries[-1][0]!='confirmation':
            raise RuntimeError('Keyboard numeric trace differs: '+repr(entries))
        result['keyboard'] = {'pairs': actual, 'confirmation': entries[-1], 'returned_to_shell': True}
    if args.mode in {'all','benchmark'}:
        before = run_case('benchmark-baseline',OUT/'baseline/ZORK1.Z3',None,'-b /fs/ZORK1.Z3',
                          ROOT/'libzvm/tests/m35_finish.lua',args)
        after = run_case('benchmark-instrumented',story,bindings,'-b /fs/ZORK1.Z3',
                         ROOT/'libzvm/tests/m35_finish.lua',args)
        spec = importlib.util.spec_from_file_location('woh_benchmark_report', ROOT/'libzvm/tests/m35_report.py')
        report = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(report)
        records = [report.collect(p/'console.txt') for p in (before,after)]
        indexes = [{(r['phase'],r['turn']):r for r in run['records'] if r['mode']=='normal'} for run in records]
        expected = [('startup',0)]+[('input',i) for i in range(1,11)]
        turns = []
        for key in expected:
            a,b = [i.get(key,{}) for i in indexes]
            if 'us' not in a or 'us' not in b:
                raise RuntimeError('Incomplete timer capture: '+str(key))
            turns.append({'phase':key[0],'turn':key[1], 'baseline_us':a['us'],
                          'instrumented_us':b['us'],'added_us':b['us']-a['us']})
        keys = ('room','score','moves','output_bytes','hash')
        final = [tuple(index[('final',0)][key] for key in keys) for index in indexes]
        if final[0] != final[1]:
            raise RuntimeError('Benchmark final gameplay state/output mismatch')
        result['benchmark'] = {'turns':turns, 'final':dict(zip(keys,final[0])),
                               'baseline_us':sum(r['baseline_us'] for r in turns),
                               'instrumented_us':sum(r['instrumented_us'] for r in turns),
                               'note':'Single paired target-timer sample, includes cache effects; host print time excluded by existing harness.'}
        (OUT/'target-benchmark-raw.json').write_text(json.dumps(records,indent=2)+'\n')
    (OUT/'target-acceptance.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
