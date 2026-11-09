"""Independent finite-cell validation of unary scalar recognition predicates.

Threshold neighbours partition the bounded scalar domain into truth-equivalent
cells for this algebra. This proves predicate satisfiability/overlap, not game
reachability. Product limits are explicit uncertainty, never success.
"""
from itertools import combinations, product
from math import prod
from conditions import evaluate, refs, unknowns

CELL_LIMIT = 32768


def comparisons(condition):
    if not isinstance(condition, dict):
        return []
    if 'ref' in condition:
        return [condition]
    return [leaf for value in condition.values()
            for child in (value if isinstance(value, list) else [value])
            for leaf in comparisons(child)]


def domains(predicates, observations):
    result = {}
    leaves = [leaf for predicate in predicates for leaf in comparisons(predicate)]
    used = set().union(*(refs(p) for p in predicates))
    for name in sorted(used):
        kind = observations[name].value_type
        values = [v for leaf in leaves if leaf['ref'] == name
                  for key, v in leaf.items() if key != 'ref']
        if kind == 'boolean':
            result[name] = [False, True]
        elif kind == 'object_ref':
            result[name] = sorted({v for v in values if v is not None}) + [None, '<OTHER-OBJECT>']
        else:
            low, high = (-32768, 32767) if kind == 'signed16' else (0, 65535)
            points = {low, high}
            for value in values:
                if type(value) is int:
                    points.update(v for v in (value - 1, value, value + 1) if low <= v <= high)
            result[name] = sorted(points)
    return result


def valuations(predicates, observations):
    domain = domains(predicates, observations)
    if prod(map(len, domain.values())) > CELL_LIMIT:
        return None
    return (dict(zip(domain, values)) for values in product(*domain.values()))


def equivalent(left, right, observations):
    if unknowns(left) or unknowns(right):
        return None
    values = valuations([left, right], observations)
    if values is None:
        return None
    return all(evaluate(left, value) == evaluate(right, value) for value in values)


def observation_issues(states, observations, source=None):
    """Check referenced query kinds/definitions before evaluating predicates.

    Numeric symbol binding is deliberately absent. Source checks establish only
    symbolic globals, objects, flags and properties, not their compiled numbers.
    """
    attributes = set()
    if source is not None:
        from zil_source import zil
        for definition in source.objects.values():
            attributes.update(zil.symbol(v) for v in source.property_values(definition, 'FLAGS'))
        for form in source.forms:
            for node in zil.walk(form):
                if zil.head(node) in {'FSET?', 'FSET', 'FCLEAR'} and len(node.items) == 3:
                    value = node.items[2]
                    if isinstance(value, zil.Atom) and not value.value.startswith('.'):
                        attributes.add(zil.symbol(value))
    issues = []
    for state in states:
        for name in sorted(refs(state.condition)):
            observation = observations.get(name)
            reason = None
            if observation is None:
                reason = 'missing_observation'
            elif observation.kind not in {'global', 'object_attribute', 'object_parent', 'object_property'}:
                reason = 'unsupported_observation_kind'
            elif observation.value_type not in {'boolean', 'signed16', 'unsigned16', 'object_ref'}:
                reason = 'unsupported_value_type'
            elif observation.kind == 'object_attribute' and observation.value_type != 'boolean':
                reason = 'attribute_requires_boolean'
            elif observation.kind == 'object_parent' and observation.value_type != 'object_ref':
                reason = 'parent_requires_object_reference'
            elif source is not None:
                if observation.kind == 'global':
                    if observation.symbol not in source.index['GLOBAL']:
                        reason = 'missing_global_symbol'
                elif observation.symbol not in source.objects:
                    reason = 'missing_object_symbol'
                elif observation.kind == 'object_attribute' and observation.member not in attributes:
                    reason = 'missing_attribute_symbol'
                elif observation.kind == 'object_property':
                    definition = source.objects[observation.symbol]
                    if not source.property_values(definition, observation.member.removeprefix('P?')):
                        reason = 'missing_property_symbol'
            if reason:
                issues.append({'state': state.id, 'ref': name, 'reason': reason})
    return issues


def validate(states, observations, source=None):
    """Single authority for state classification, room readiness and strict mode.

    Discovery warnings and optional legacy comparisons are not inputs. The four
    state classifications are exclusive: contradiction, unresolved, ambiguity,
    then resolved. A domain/cell-limit issue can still make a room incomplete
    even when its individual state predicates are supported.
    """
    invalid_observations = observation_issues(states, observations, source)
    invalid = {issue['state'] for issue in invalid_observations}
    supported = [s for s in states if not unknowns(s.condition) and s.id not in invalid]
    contradictions, overlaps, uncertain = [], [], []
    for state in states:
        if state.id in invalid:
            continue
        values = valuations([state.condition], observations)
        if values is None:
            uncertain.append({'state': state.id, 'reason': 'validation_cell_limit'})
        elif all(evaluate(state.condition, value) is False for value in values):
            contradictions.append(state.id)
    for a, b in combinations(supported, 2):
        values = valuations([a.condition, b.condition], observations)
        if values is None:
            uncertain.append({'states': [a.id, b.id], 'reason': 'validation_cell_limit'})
            continue
        witness = next((v for v in values if evaluate(a.condition, v) is True and
                        evaluate(b.condition, v) is True), None)
        if witness is not None:
            overlaps.append({'states': [a.id, b.id], 'witness': witness})
    values = valuations([s.condition for s in supported], observations)
    gap = None
    if values is None:
        uncertain.append({'reason': 'coverage_cell_limit'})
    else:
        gap = next((v for v in values if not any(evaluate(s.condition, v) is True
                                               for s in supported)), None)
    ambiguous = {name for overlap in overlaps for name in overlap['states']}
    state_status = {
        s.id: 'contradictory' if s.id in contradictions
        else 'unresolved' if s.id in invalid or unknowns(s.condition)
        else 'ambiguous' if s.id in ambiguous else 'resolved'
        for s in states
    }
    blockers = []
    if not states:
        blockers.append('no_candidate_states')
    for status in ('unresolved', 'contradictory', 'ambiguous'):
        if status in state_status.values():
            blockers.append(status + '_states')
    if gap is not None:
        blockers.append('recognition_coverage_gap')
    if uncertain:
        blockers.append('recognition_validation_uncertain')
    ready = not blockers
    return {'ready': ready, 'blocking_reasons': blockers, 'state_status': state_status,
            'observation_issues': invalid_observations,
            'contradictory_states': contradictions, 'overlaps': overlaps,
            'coverage_gap': gap, 'uncertainty': uncertain,
            'predicate_partition_complete': ready}
