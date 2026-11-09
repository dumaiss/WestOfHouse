"""Small, typed recognition algebra. Unknown source is never a boolean.

All references describe a read-only scalar VM query. No recursive containment
query, parser state or executable ZIL expression is emitted to the dictionary.
"""
from dataclasses import dataclass
import hashlib
import json
from zil_source import zil


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()[:12].upper()


def refs(condition):
    if not isinstance(condition, dict):
        return set()
    if 'ref' in condition:
        return {condition['ref']}
    return set().union(*(refs(v) for v in condition.values() if isinstance(v, dict)),
                       *(refs(v) for values in condition.values() if isinstance(values, list)
                         for v in values))


def unknowns(condition):
    if not isinstance(condition, dict):
        return []
    if 'unknown' in condition:
        return [condition['unknown']]
    return [u for v in condition.values() for child in (v if isinstance(v, list) else [v])
            for u in unknowns(child)]


def evaluate(condition, values):
    """Three-valued evaluation for build-time validation, never a runtime engine."""
    if isinstance(condition, bool):
        return condition
    if 'unknown' in condition:
        return None
    if 'ref' in condition:
        if condition['ref'] not in values:
            return None
        actual = values[condition['ref']]
        op = next(k for k in condition if k != 'ref')
        expected = condition[op]
        if op == 'eq':
            return actual == expected
        if op == 'ne':
            return actual != expected
        if not isinstance(actual, int) or not isinstance(expected, int):
            return None
        return {'lt': actual < expected, 'le': actual <= expected,
                'gt': actual > expected, 'ge': actual >= expected}[op]
    if 'not' in condition:
        v = evaluate(condition['not'], values)
        return None if v is None else not v
    key = 'all' if 'all' in condition else 'any'
    results = [evaluate(c, values) for c in condition[key]]
    if key == 'all':
        return False if False in results else None if None in results else True
    return True if True in results else None if None in results else False


def negate(condition):
    if isinstance(condition, bool):
        return not condition
    if 'not' in condition:
        return condition['not']
    if 'ref' in condition:
        inverse = {'eq': 'ne', 'ne': 'eq', 'lt': 'ge', 'ge': 'lt', 'gt': 'le', 'le': 'gt'}
        op = next(k for k in condition if k != 'ref')
        return {'ref': condition['ref'], inverse[op]: condition[op]}
    return {'not': condition}


def combine(key, parts):
    flat = []
    identity = key == 'all'
    decisive = None
    for part in parts:
        if isinstance(part, bool):
            if part != identity:
                decisive = part
            continue
        flat.extend(part[key] if key in part else [part])
    # A nested reduction may retain a decisive boolean alongside an unknown.
    # Keep that evidence without treating a boolean as a predicate mapping.
    for part in flat:
        if isinstance(part, bool) and part != identity:
            decisive = part
    flat = [p for p in flat if not isinstance(p, bool)]
    flat = list({canonical(p): p for p in flat}.values())
    unsupported = [p for p in flat if unknowns(p)]
    if decisive is not None:
        return {key: [decisive, *flat]} if unsupported else decisive
    if key == 'all':
        equalities = {}
        for part in flat:
            if 'ref' in part and 'eq' in part:
                ref = part['ref']
                if ref in equalities and equalities[ref] != part['eq']:
                    return {'all': [False, *unsupported]} if unsupported else False
                equalities[ref] = part['eq']
        reduced = []
        for part in flat:
            result = evaluate(part, equalities)
            if result is False:
                return {'all': [False, *unsupported]} if unsupported else False
            # Retain each equality that justified eliminating another condition.
            if result is not True or ('ref' in part and 'eq' in part):
                reduced.append(part)
        flat = reduced
    flat.sort(key=canonical)
    if not flat:
        return identity
    return flat[0] if len(flat) == 1 else {key: flat}


@dataclass(frozen=True)
class Observation:
    kind: str
    symbol: str
    value_type: str
    member: str = ''

    @property
    def id(self):
        return (self.symbol if self.kind == 'global' else
                f'{self.symbol}.{self.member or "parent"}')

    def output(self):
        result = {'id': self.id, 'kind': self.kind}
        result['symbol' if self.kind == 'global' else 'object'] = self.symbol
        if self.kind == 'object_attribute':
            result['attribute'] = self.member
        if self.kind == 'object_property':
            result['property'] = self.member
        result['value_type'] = self.value_type
        return result


class Normalizer:
    def __init__(self, source, room):
        self.source, self.room = source, room
        self.observations = {}
        # Signed ZIL arithmetic/comparisons are evidence independent of traversal.
        self.numeric = getattr(source, '_numeric_globals', None)
        if self.numeric is not None:
            return
        self.numeric = set()
        for form in source.forms:
            for node in zil.walk(form):
                if zil.head(node) in {'+', '-', '*', '/', 'G?', 'L?', 'IGRTR?', 'DLESS?'}:
                    self.numeric.update(zil.symbol(v) for v in node.items[1:]
                                        if zil.symbol(v) in source.index['GLOBAL'])
        for form in source.forms:
            if zil.head(form) == 'GDECL':
                for names, kind in zip(form.items[1::2], form.items[2::2]):
                    if zil.symbol(kind) == 'FIX' and isinstance(names, zil.Form):
                        self.numeric.update(zil.symbol(v) for v in names.items
                                            if zil.symbol(v) in source.index['GLOBAL'])
        source._numeric_globals = self.numeric

    def global_type(self, name):
        cache = getattr(self.source, '_global_types', {})
        self.source._global_types = cache
        if name not in cache:
            cache[name] = self._global_type(name)
        return cache[name]

    def _global_type(self, name):
        definition = self.source.index['GLOBAL'][name]
        initial = zil.scalar(definition.form.items[2]) if len(definition.form.items) > 2 else None
        # Initial values alone cannot prove a global's type: inspect assignments.
        assigned = [initial]
        for form in self.source.forms:
            for node in zil.walk(form):
                if zil.head(node) == 'SETG' and len(node.items) == 3 and zil.symbol(node.items[1]) == name:
                    assigned.append(zil.scalar(node.items[2]))
        # A table initializer is an encoded source pointer, not a scalar
        # counter merely because address arithmetic happens to mention it.
        initial_form = definition.form.items[2] if len(definition.form.items) > 2 else None
        if zil.head(initial_form) in {'TABLE', 'LTABLE', 'ITABLE', 'PLTABLE'}:
            return None
        if name in self.numeric and not (isinstance(initial, str) and initial in self.source.objects):
            return 'signed16'
        if all(type(v) is bool for v in assigned):
            return 'boolean'
        if all((isinstance(v, str) and v in self.source.objects) or v is False or v == 0
               for v in assigned) and any(isinstance(v, str) for v in assigned):
            return 'object_ref'
        if all(type(v) in (int, bool) for v in assigned):
            return 'signed16' if any(type(v) is int and v < 0 for v in assigned) else 'unsigned16'
        return None

    def observe(self, observation):
        old = self.observations.get(observation.id)
        if old and old != observation:
            raise ValueError('Conflicting observation type: ' + observation.id)
        self.observations[observation.id] = observation
        return observation

    def object_symbol(self, value):
        name = zil.symbol(value)
        if name == 'HERE':
            return self.room
        return name if name in self.source.objects else None

    def operand(self, value):
        name = zil.symbol(value)
        if isinstance(value, zil.Atom) and value.value.startswith(',') and name in self.source.index['GLOBAL']:
            kind = self.global_type(name)
            if kind:
                return self.observe(Observation('global', name, kind))
        if zil.head(value) == 'LOC' and len(value.items) == 2:
            obj = self.object_symbol(value.items[1])
            if obj:
                return self.observe(Observation('object_parent', obj, 'object_ref'))
        if zil.head(value) == 'GETP' and len(value.items) == 3:
            obj, prop = self.object_symbol(value.items[1]), zil.symbol(value.items[2])
            if obj and prop:
                vals = self.source.property_values(self.source.objects[obj], prop.removeprefix('P?'))
                if vals and type(zil.scalar(vals[0])) is int:
                    return self.observe(Observation('object_property', obj, 'signed16', prop))
        return None

    def literal(self, value, kind):
        raw = zil.scalar(value)
        if kind == 'object_ref':
            obj = self.object_symbol(value)
            return (True, obj) if obj else (True, None) if raw is False or raw == 0 else (False, None)
        if kind == 'boolean':
            return (True, raw) if type(raw) is bool else (False, None)
        if type(raw) not in (int, bool):
            return False, None
        raw = int(raw) & 0xffff
        return True, raw - 65536 if kind == 'signed16' and raw >= 32768 else raw

    def condition(self, value):
        raw, op = zil.scalar(value), zil.head(value)
        if raw is True or raw is False:
            return raw
        args = value.items[1:] if isinstance(value, zil.Form) else []
        if op in {'AND', 'OR'}:
            return combine('all' if op == 'AND' else 'any', [self.condition(v) for v in args])
        if op == 'NOT' and len(args) == 1:
            return negate(self.condition(args[0]))
        if op == 'FSET?' and len(args) == 2:
            obj, flag = self.object_symbol(args[0]), zil.symbol(args[1])
            if obj and flag:
                obs = self.observe(Observation('object_attribute', obj, 'boolean', flag))
                return {'ref': obs.id, 'eq': True}
        if op == 'IN?' and len(args) == 2:
            obj, parent = self.object_symbol(args[0]), self.object_symbol(args[1])
            if obj and parent:
                obs = self.observe(Observation('object_parent', obj, 'object_ref'))
                return {'ref': obs.id, 'eq': parent}
        if op in {'EQUAL?', '==?', 'G?', 'L?'} and len(args) >= 2:
            obs = self.operand(args[0])
            rest = args[1:]
            reverse = False
            if obs is None and len(args) == 2:
                obs, rest, reverse = self.operand(args[1]), [args[0]], True
            if obs and (op in {'EQUAL?', '==?'} or (len(rest) == 1 and obs.value_type == 'signed16')):
                terms = []
                for val in rest:
                    ok, literal = self.literal(val, obs.value_type)
                    if not ok:
                        break
                    comparison = {'EQUAL?': 'eq', '==?': 'eq', 'G?': 'gt', 'L?': 'lt'}[op]
                    if reverse:
                        comparison = {'gt': 'lt', 'lt': 'gt'}.get(comparison, comparison)
                    terms.append({'ref': obs.id, comparison: literal})
                else:
                    return combine('any', terms)
        obs = self.operand(value)
        if obs:
            empty = False if obs.value_type == 'boolean' else None if obs.value_type == 'object_ref' else 0
            return {'ref': obs.id, 'ne': empty}
        return {'unknown': zil.render(value)}
