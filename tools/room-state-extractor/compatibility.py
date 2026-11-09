"""Optional asset-state validation; never an input to source discovery."""
import json
from pathlib import Path
from conditions import Observation, combine, digest
from zil_source import expression
from validation import equivalent


def legacy_condition(value, normalizer):
    op = value.get('op')
    if op == 'true':
        return True
    if op == 'source_expression':
        try:
            return normalizer.condition(expression(value['expression']))
        except ValueError:
            return {'unknown': value['expression']}
    if op in {'and', 'or'}:
        return combine('all' if op == 'and' else 'any',
                       [legacy_condition(v, normalizer) for v in value['args']])
    if op in {'flag_set', 'flag_clear'}:
        if value['object'] not in normalizer.source.objects:
            return {'unknown': 'Legacy attribute target is not a literal object: ' + str(value)}
        obs = normalizer.observe(Observation('object_attribute', value['object'], 'boolean', value['flag']))
        return {'ref': obs.id, 'eq': op == 'flag_set'}
    if op == 'eq' and 'state_variable' in value.get('left', {}):
        name = value['left']['state_variable']
        if name in normalizer.source.index['GLOBAL']:
            kind = normalizer.global_type(name)
            if kind:
                obs = normalizer.observe(Observation('global', name, kind))
                literal = value['right']['value']
                if kind == 'object_ref':
                    if literal is False or literal == 0:
                        literal = None
                    elif literal is not None and literal not in normalizer.source.objects:
                        return {'unknown': 'Unproven legacy object value: ' + str(value)}
                elif kind == 'boolean' and type(literal) is not bool:
                    return {'unknown': 'Unproven legacy boolean value: ' + str(value)}
                elif kind in {'signed16', 'unsigned16'}:
                    if type(literal) not in (int, bool):
                        return {'unknown': 'Unproven legacy numeric value: ' + str(value)}
                    literal = int(literal) & 0xffff
                    if kind == 'signed16' and literal >= 32768:
                        literal -= 65536
                return {'ref': obs.id, 'eq': literal}
    if op == 'located_in':
        # Legacy location semantics are not documented as immediate-parent.
        return {'unknown': 'Legacy located_in semantics require review: ' + str(value)}
    return {'unknown': str(value)}


def compare_assets(room, states, normalizer, directory):
    path = Path(directory) / (room.lower() + '.scene.json') if directory else None
    if path is None or not path.exists():
        return {'status': 'not_checked', 'mappings': [], 'issues': []}
    scene = json.loads(path.read_text())['scene']
    old = scene['states']
    mappings, issues, mapped = [], [], set()
    for state in states:
        same_id = [s for s in old if s['id'] == state.id]
        candidates = same_id or old
        matches = []
        for candidate in candidates:
            predicate = legacy_condition(candidate['condition'], normalizer)
            equal = equivalent(state.condition, predicate, normalizer.observations)
            if equal is True:
                # Distinct packages can share a predicate/image. Never guess an
                # ordinal mapping when source text indicates a different state.
                text = tuple(candidate.get('entry_text', []))
                if not text:
                    text = tuple(f['value'] for f in candidate.get('facts', [])
                                 if f.get('predicate') == 'source_description')
                if same_id and state.text and text and text != state.text:
                    issues.append({'kind': 'changed_presentation_content', 'state': state.id,
                                   'legacy_state': candidate['id']})
                    continue
                if same_id or (state.text and text == state.text):
                    matches.append(candidate)
            elif same_id:
                issues.append({'kind': 'changed_meaning' if equal is False else 'meaning_unproven',
                               'state': state.id, 'legacy_state': candidate['id'],
                               'legacy_condition': candidate['condition'],
                               'new_condition': state.condition})
        if len(matches) == 1:
            legacy = matches[0]['id']
            mapped.add(legacy)
            mappings.append({'state': state.id, 'asset_state': legacy,
                             'status': 'verified_predicate',
                             'meaning_fingerprint': digest([state.condition, state.text])})
        elif len(matches) > 1:
            issues.append({'kind': 'ambiguous_mapping', 'state': state.id,
                           'legacy_states': [s['id'] for s in matches]})
        elif not same_id:
            issues.append({'kind': 'no_verified_mapping', 'state': state.id})
    for state in old:
        if state['id'] not in mapped:
            issues.append({'kind': 'unmapped_legacy_state', 'legacy_state': state['id']})
    return {'status': 'checked', 'mappings': mappings, 'issues': issues}
