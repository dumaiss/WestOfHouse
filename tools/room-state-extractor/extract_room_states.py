#!/usr/bin/env python3
"""Generate symbolic room-state recognition data directly from original ZIL.

The scene-facts tool is imported for its grammar only; no scene JSON is generated.
Explicit legacy comparison is separate from recognition and never gates readiness.
"""
import argparse
import json
from pathlib import Path
import sys

from conditions import refs, unknowns
from discovery import Discovery
from compatibility import compare_assets
from validation import validate
from zil_source import Source

ROOT = Path(__file__).resolve().parents[2]


def yaml_scalar(value):
    # JSON scalar spelling is also valid YAML 1.2; quote all strings, including
    # names that YAML 1.1 parsers might otherwise interpret as booleans.
    return json.dumps(value, ensure_ascii=False)


def yaml_lines(value, indent=0):
    """Deterministic block YAML, with no third-party dependency or custom tags."""
    prefix = ' ' * indent
    if isinstance(value, dict) and value:
        for key, child in value.items():
            if isinstance(child, (dict, list)) and child:
                yield prefix + key + ':'
                yield from yaml_lines(child, indent + 2)
            else:
                yield prefix + key + ': ' + yaml_scalar(child)
    elif isinstance(value, list) and value:
        for child in value:
            if isinstance(child, (dict, list)) and child:
                yield prefix + '-'
                yield from yaml_lines(child, indent + 2)
            else:
                yield prefix + '- ' + yaml_scalar(child)
    else:
        yield prefix + yaml_scalar(value)


def build(source, selected, scene_facts=None):
    rooms, reports, mappings = [], [], []
    for room in sorted(set(selected)):
        discovery = Discovery(source, room)
        states = discovery.discover()
        checks = validate(states, discovery.norm.observations, source)
        status = 'ready' if checks['ready'] else 'incomplete'
        rendered_states = [s.output(checks) for s in states]
        used = set().union(*(refs(s['match']) for s in rendered_states if 'match' in s))
        observe = [discovery.norm.observations[name].output() for name in sorted(used)]
        rooms.append({'id': room, 'status': status,
                      'observe': observe, 'states': rendered_states})
        reports.append({
            'room': room, 'status': status, 'validation': checks,
            'states': [{'id': s.id, 'basis': s.basis,
                        'source': {k: s.source[k] for k in ('file', 'line') if k in s.source},
                        'condition': s.condition, 'status': checks['state_status'][s.id],
                        'unsupported_expressions': unknowns(s.condition)} for s in states],
        })
        # Optional comparison is downstream of the complete recognition result.
        # It has a separate output and cannot change predicates, reads or status.
        if scene_facts is not None:
            mappings.append({'room': room, **compare_assets(room, states, discovery.norm, scene_facts)})
    summary = {
        'rooms_processed': len(rooms),
        'rooms_ready': sum(r['status'] == 'ready' for r in rooms),
        'rooms_not_ready': sum(r['status'] == 'incomplete' for r in rooms),
        'single_state_rooms': sum(len(r['states']) == 1 for r in rooms),
        'multiple_state_rooms': sum(len(r['states']) > 1 for r in rooms),
        'resolved_states': sum(s['status'] == 'resolved' for r in reports for s in r['states']),
        'unresolved_states': sum(s['status'] == 'unresolved' for r in reports for s in r['states']),
        'ambiguous_states': sum(s['status'] == 'ambiguous' for r in reports for s in r['states']),
        'contradictory_states': sum(s['status'] == 'contradictory' for r in reports for s in r['states']),
        'rooms_with_recognition_overlaps': sum(bool(r['validation']['overlaps']) for r in reports),
        'rooms_with_recognition_coverage_gaps': sum(r['validation']['coverage_gap'] is not None for r in reports),
        'rooms_with_recognition_uncertainty': sum(bool(r['validation']['uncertainty']) for r in reports),
        'observation_types': sorted({o['kind'] for r in rooms for o in r['observe']}),
    }
    return ({'schema': 'room_state_dictionary_v1', 'rooms': rooms},
            {'schema': 'room_state_diagnostics_v2', 'source_hashes': source.source_hashes,
             'summary': summary, 'rooms': reports},
            {'schema': 'room_state_asset_mapping_v1', 'rooms': mappings} if scene_facts is not None else None)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT / 'vendor/infocom-zork1')
    parser.add_argument('--output', type=Path, default=ROOT / 'generated-room-states/room-states.yaml')
    parser.add_argument('--report', type=Path, help='Defaults to OUTPUT.report.json')
    parser.add_argument('--compare-legacy', action='store_true',
                        help='Optional legacy comparison; never affects readiness or strict mode')
    parser.add_argument('--compatibility-output', type=Path,
                        help='Enable optional comparison and write it here')
    parser.add_argument('--scene-facts', type=Path,
                        help='Enable optional comparison using this legacy directory')
    parser.add_argument('--room', action='append', dest='rooms')
    parser.add_argument('--list-rooms', action='store_true')
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args(argv)
    if not args.source_root.is_dir() or not list(args.source_root.glob('*.zil')):
        parser.error('source root must contain .zil files')
    source = Source(args.source_root)
    available = sorted(source.index['ROOM'])
    if args.list_rooms:
        print('\n'.join(available))
        return 0
    selected = args.rooms or available
    missing = sorted(set(selected) - set(available))
    if missing:
        parser.error('Unknown room ID(s): ' + ', '.join(missing))
    compare = args.compare_legacy or args.scene_facts is not None or args.compatibility_output is not None
    legacy_directory = (args.scene_facts or ROOT / 'generated-scenes') if compare else None
    dictionary, report, mapping = build(source, selected, legacy_directory)
    outputs = [(args.output, '\n'.join(yaml_lines(dictionary)) + '\n'),
               (args.report or args.output.with_suffix('.report.json'), json.dumps(report, indent=2, ensure_ascii=False) + '\n')]
    if mapping is not None:
        outputs.append((args.compatibility_output or args.output.with_suffix('.compatibility.json'),
                        json.dumps(mapping, indent=2, ensure_ascii=False) + '\n'))
    if len({path.resolve() for path, _ in outputs}) != len(outputs):
        parser.error('output, report and compatibility paths must be distinct')
    for path, text in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    print(json.dumps(report['summary'], sort_keys=True))
    return 1 if args.strict and report['summary']['rooms_not_ready'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
