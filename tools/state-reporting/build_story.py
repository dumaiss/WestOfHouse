#!/usr/bin/env python3
"""Reproducible wrapper around the installed ZILf/ZAPF tools and GNU make.

Baseline staging removes only this package's reporting declarations/read calls.
The original Infocom COMPILED/zork1.z3 is never overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from bindings import write_bindings
from generate_states import generate, yaml

ROOT = Path(__file__).resolve().parents[2]


def run(command, cwd, log):
    result = subprocess.run([str(x) for x in command], cwd=cwd, capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'Build failed; see {log}')


def build(output, baseline=False, dictionary=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    original = ROOT/'vendor/infocom-zork1'
    source = output/'source'
    source.mkdir(exist_ok=True)
    if baseline:
        for stale in (source/'woh-states.zil', output/'ZORK1.WOH', output/'ZORK1.bindings.json'):
            if stale.exists():
                stale.unlink()
    for path in sorted(original.glob('*.zil')):
        text = path.read_text()
        if baseline:
            if path.name == 'zork1.zil':
                text = text.replace('<GLOBAL WOH-STATE 0>\n', '').replace('<INSERT-FILE "WOH-STATES" T>\n', '')
            if path.name in {'gparser.zil', '1actions.zil'}:
                text = text.replace('<WOH-READ>', '<READ ,P-INBUF ,P-LEXV>')
        (source/path.name).write_text(text)
    if not baseline:
        dictionary = Path(dictionary or ROOT/'generated-room-states/room-states.yaml')
        text, mapping = generate(yaml.safe_load(dictionary.read_text()))
        generated = ROOT/'generated-zil'
        generated.mkdir(exist_ok=True)
        (generated/'woh-states.zil').write_text(text)
        (generated/'woh-state-ids.json').write_text(json.dumps(mapping, indent=2) + '\n')
        (source/'woh-states.zil').write_text(text)
    run([ROOT/'tools/zilf', 'build', '-S', '-I', source, source/'zork1.zil', output/'zork1.zap'],
        source, output/'zilf.log')
    # Fix the serial instead of letting wall-clock date change the story bytes.
    run([ROOT/'tools/zapf', output/'zork1.zap', output/'ZORK1.Z3', '-s', '000001'],
        source, output/'zapf.log')
    if not baseline:
        write_bindings(output/'ZORK1.Z3', output/'zork1_data.zap', output/'ZORK1.WOH')
    manifest = {'schema': 'woh_story_build_v1', 'baseline': baseline,
                'story_sha256': hashlib.sha256((output/'ZORK1.Z3').read_bytes()).hexdigest(),
                'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.glob('*.zil'))},
                'tools_sha256': {name: hashlib.sha256((ROOT/'tools'/name).read_bytes()).hexdigest() for name in ('zilf','zapf')}}
    (output/'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(output/'ZORK1.Z3')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--dictionary', type=Path)
    args = parser.parse_args()
    build(args.output_dir or ROOT/'build/state-reporting'/('baseline' if args.baseline else 'instrumented'),
          args.baseline, args.dictionary)


if __name__ == '__main__':
    main()
