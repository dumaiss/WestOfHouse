"""Read-only bridge between shipped runtime IDs and scene-facts provenance."""
import copy
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/room-state-extractor'))
from zil_source import Source, expression
from discovery import Discovery
from compatibility import legacy_condition
from validation import equivalent


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalize_legacy(value, normalizer):
    # The shared expression() helper expects a form, not a bare global.
    # OR is a truth-preserving wrapper for the static normalizer; no ZIL runs.
    value = copy.deepcopy(value)
    def wrap(node):
        if isinstance(node, dict):
            if node.get('op') == 'source_expression':
                node['expression'] = '<OR ' + node['expression'] + '>'
            else:
                for v in node.values():
                    wrap(v)
        elif isinstance(node, list):
            for v in node:
                wrap(v)
    wrap(value)
    return legacy_condition(value, normalizer)


def resolve(scene, runtime, dictionary, source):
    """Never assign IDs: consume the resolver's existing local values."""
    discovery = Discovery(source, scene['id'])
    found = {s.id: s for s in discovery.discover()}
    dictionary_states = {s['id']: s for s in dictionary['states']}
    descriptions = scene['descriptions']
    result = []
    if runtime['status'] != 'ready':
        if runtime['states']:
            raise ValueError('Incomplete runtime room has assigned states')
        return []
    if dictionary['status'] != 'ready':
        raise ValueError('Runtime/dictionary readiness differs for ' + scene['id'])
    if {s['id'] for s in runtime['states']} != set(dictionary_states):
        raise ValueError('Runtime/dictionary state identities differ')
    for assigned in runtime['states']:
        state = found.get(assigned['id'])
        condition = dictionary_states[assigned['id']]['match']
        if state is None or state.condition != condition:
            raise ValueError('Runtime predicate is stale: ' + scene['id'] + '/' + assigned['id'])
        facts, description_ids = [], []
        for text in state.text:
            matches = [(i, d, j) for i, d in enumerate(descriptions)
                       for j, t in enumerate(d['text']) if t == text]
            if not matches:
                raise ValueError('Runtime presentation text absent from scene facts')
            # Prefer the description for this complete presentation when a
            # common sentence also occurs in a different state variant.
            matches.sort(key=lambda match: tuple(match[1]['text']) != state.text)
            i, d, j = matches[0]
            description_ids.append(d['id'])
            facts.append({'text': text, 'pointer': f'/scene/descriptions/{i}/text/{j}',
                          'source_refs': d.get('source_refs', [])})
        legacy = []
        for candidate in scene['states']:
            # A stable action mutation is never promoted to a presentation.
            if not (candidate['id'].startswith('LOOK-') or candidate['id'] == 'INITIAL'):
                continue
            candidate_text = candidate.get('entry_text', [])
            if candidate['id'] != 'INITIAL' and tuple(candidate_text) != state.text:
                continue
            try:
                predicate = normalize_legacy(candidate['condition'], discovery.norm)
                equal = equivalent(condition, predicate, discovery.norm.observations)
            except (ValueError, KeyError, TypeError):
                equal = None
            if equal is True:
                legacy.append({'id': candidate['id'], 'condition': candidate['condition'],
                               'source_refs': candidate.get('source_refs', []),
                               'method': 'equivalent_static_predicate_and_matching_presentation_text'})
        result.append({'runtime_state': assigned['value'], 'symbolic_state': assigned['id'],
                       'condition': condition, 'extracted_states': legacy,
                       'description_ids': sorted(set(description_ids)), 'canonical': facts,
                       'runtime_source': state.source})
    return result
