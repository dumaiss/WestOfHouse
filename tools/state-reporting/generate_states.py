#!/usr/bin/env python3
"""Compile symbolic room recognition YAML to native ZIL, not a runtime evaluator."""
import argparse
import json
from pathlib import Path
import re
import sys

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/room-state-extractor'))
from conditions import Observation
from discovery import State
from validation import validate

SYMBOL = re.compile(r'[A-Z][A-Z0-9?\-]*\Z')


def symbol(value):
    if not isinstance(value, str) or not SYMBOL.fullmatch(value):
        raise ValueError(f'Invalid literal ZIL symbol: {value!r}')
    return ',' + value


class Emitter:
    def __init__(self, observations):
        self.observations = {}
        self.models = {}
        for observation in observations:
            name = observation['id']
            if name in self.observations:
                raise ValueError('Duplicate observation: ' + name)
            self.observations[name] = observation
            kind = observation['kind']
            target = observation.get('symbol') if kind == 'global' else observation.get('object')
            symbol(target)
            member = observation.get('attribute', observation.get('property', ''))
            if member:
                symbol(member)
            self.models[name] = Observation(kind, target, observation['value_type'], member)

    def read(self, observation):
        kind = observation['kind']
        if kind == 'global':
            return symbol(observation['symbol'])
        target = symbol(observation['object'])
        if kind == 'object_attribute':
            return f'<FSET? {target} {symbol(observation["attribute"])}>'
        if kind == 'object_parent':
            return f'<LOC {target}>'
        if kind == 'object_property':
            return f'<GETP {target} {symbol(observation["property"])}>'
        raise ValueError('Unsupported observation kind: ' + kind)

    def condition(self, condition):
        if type(condition) is bool:
            return 'T' if condition else '<>'
        if not isinstance(condition, dict):
            raise ValueError('Invalid recognition condition')
        if 'ref' not in condition:
            if set(condition) == {'not'}:
                return '<NOT ' + self.condition(condition['not']) + '>'
            for key, op in [('all', 'AND'), ('any', 'OR')]:
                if set(condition) == {key} and condition[key]:
                    return '<' + op + ' ' + ' '.join(self.condition(c) for c in condition[key]) + '>'
            raise ValueError('Unsupported logical condition: ' + str(condition))
        if len(condition) != 2 or condition['ref'] not in self.observations:
            raise ValueError('Invalid/missing observation reference')
        observation = self.observations[condition['ref']]
        op = next(k for k in condition if k != 'ref')
        if op not in {'eq', 'ne', 'lt', 'le', 'gt', 'ge'}:
            raise ValueError('Unsupported comparison: ' + op)
        value = condition[op]
        kind = observation['value_type']
        read = self.read(observation)
        if kind == 'boolean':
            if type(value) is not bool or op not in {'eq', 'ne'}:
                raise ValueError('Invalid boolean comparison')
            positive = value if op == 'eq' else not value
            return read if positive else '<NOT ' + read + '>'
        if kind == 'object_ref':
            if op not in {'eq', 'ne'}:
                raise ValueError('Object ordering is unsupported')
            literal = '<>' if value is None else symbol(value)
        elif kind in {'signed16', 'unsigned16'}:
            low, high = (-32768, 32767) if kind == 'signed16' else (0, 65535)
            if type(value) is not int or not low <= value <= high:
                raise ValueError('Scalar outside its declared type')
            if kind == 'unsigned16' and op not in {'eq', 'ne'}:
                # Adding 8000h rotates unsigned ordering into signed ordering.
                # Native VM ADD wraps at 16 bits; no additional global is used.
                read, literal = f'<+ {read} -32768>', str(value - 32768)
            else:
                literal = str(value if value < 32768 else value - 65536)
        else:
            raise ValueError('Unsupported scalar type: ' + kind)
        zil_op = {'eq': 'EQUAL?', 'ne': 'EQUAL?', 'gt': 'G?', 'le': 'G?',
                  'lt': 'L?', 'ge': 'L?'}[op]
        result = f'<{zil_op} {read} {literal}>'
        return '<NOT ' + result + '>' if op in {'ne', 'le', 'ge'} else result


def generate(dictionary):
    if dictionary.get('schema') != 'room_state_dictionary_v1':
        raise ValueError('Unsupported dictionary schema')
    lines = [';"Generated from room-states.yaml. Do not edit; no gameplay side effects."',
             '<ROUTINE WOH-UPDATE-STATE ()', ' <SETG WOH-STATE 0>', ' <COND']
    mapping, seen = [], set()
    for room in sorted(dictionary['rooms'], key=lambda r: r['id']):
        name = room['id']
        symbol(name)
        if name in seen:
            raise ValueError('Duplicate room: ' + name)
        seen.add(name)
        if room['status'] == 'incomplete':
            mapping.append({'room': name, 'status': 'incomplete', 'states': []})
            continue
        if room['status'] != 'ready':
            raise ValueError('Unsupported room status')
        emitter = Emitter(room['observe'])
        states = sorted(room['states'], key=lambda s: (s['id'] != 'INITIAL', s['id']))
        if len({s['id'] for s in states}) != len(states) or len(states) > 32767:
            raise ValueError('Invalid state identities/count')
        if any('match' not in s or 'status' in s for s in states):
            raise ValueError('Ready room contains an unsupported state')
        predicates = [emitter.condition(s['match']) for s in states]
        models = [State(s['id'], s['match'], '', {}) for s in states]
        checks = validate(models, emitter.models)
        if not checks['ready']:
            raise ValueError(f'Ready room {name} fails recognition validation: {checks["blocking_reasons"]}')
        lines += [f'  (<EQUAL? ,HERE {symbol(name)}>', '   <COND']
        assigned = []
        for number, (state, predicate) in enumerate(zip(states, predicates), 1):
            lines.append(f'    ({predicate} <SETG WOH-STATE {number}>)')
            assigned.append({'value': number, 'id': state['id']})
        lines.append('   >)')
        mapping.append({'room': name, 'status': 'ready', 'states': assigned})
    lines += [' >', ' <RTRUE>>', '',
              ';"Only normal command readers use this wrapper; YES?/FINISH keep native READ."',
              '<ROUTINE WOH-READ ()', ' <WOH-UPDATE-STATE>', ' <READ ,P-INBUF ,P-LEXV>>', '']
    return '\n'.join(lines), {'schema': 'woh_local_state_ids_v1', 'unknown_value': 0, 'rooms': mapping}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dictionary', type=Path, default=ROOT/'generated-room-states/room-states.yaml')
    parser.add_argument('--output', type=Path, default=ROOT/'generated-zil/woh-states.zil')
    parser.add_argument('--mapping', type=Path, default=ROOT/'generated-zil/woh-state-ids.json')
    args = parser.parse_args(argv)
    text, mapping = generate(yaml.safe_load(args.dictionary.read_text()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.mapping.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    args.mapping.write_text(json.dumps(mapping, indent=2) + '\n')


if __name__ == '__main__':
    main()
