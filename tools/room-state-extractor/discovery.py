"""Conservative presentation discovery directly over the shared ZIL syntax tree.

This is a bounded source-pattern analysis, not a ZIL interpreter. Only explicit
look text, indexed text tables and persistent presentation mutations produce
candidates. Action identity alone never produces a state.
"""
from dataclasses import dataclass, field, replace
from conditions import (Normalizer, Observation, canonical, combine, digest,
                        evaluate, negate, refs, unknowns)
from zil_source import zil, provenance

PATH_LIMIT = 256
MUTATIONS = {'SETG', 'FSET', 'FCLEAR', 'MOVE', 'REMOVE', 'REMOVE-CAREFULLY', 'PUTP'}
# Flags with a direct visible/presentation meaning. Other mutations do not
# establish a candidate state merely because an action changes game memory.
PRESENTATION_FLAGS = {'INVISIBLE', 'OPENBIT', 'ONBIT', 'NDESCBIT', 'TOUCHBIT', 'MUNGBIT'}


@dataclass
class State:
    id: str
    condition: object
    basis: str
    source: dict
    text: tuple = ()

    def output(self, validation):
        status = validation['state_status'][self.id]
        if status in {'unresolved', 'contradictory'}:
            return {'id': self.id, 'status': status}
        result = {'id': self.id, 'match': self.condition}
        if status == 'ambiguous':
            result['status'] = status
        return result



@dataclass
class PathState:
    condition: object = True
    text: tuple = ()
    writes: tuple = ()
    tables: tuple = ()
    limitations: tuple = ()
    terminal: bool = False
    returned: bool = False


def remove_ref(condition, ref):
    """Forget an old value when it is overwritten, without dropping other guards."""
    if not isinstance(condition, dict) or ref not in refs(condition):
        return condition
    if 'ref' in condition:
        return True
    if 'all' in condition:
        return combine('all', [remove_ref(c, ref) for c in condition['all']])
    return {'unknown': 'Mutation invalidates compound guard: ' + canonical(condition)}


def assign(path, observation, value):
    return replace(path, condition=combine('all', [remove_ref(path.condition, observation.id),
                                                   {'ref': observation.id, 'eq': value}]),
                   writes=path.writes + (observation.id,))


def known_value(condition, ref):
    if not isinstance(condition, dict):
        return None
    if condition.get('ref') == ref and 'eq' in condition:
        return condition['eq']
    if 'all' in condition:
        for term in condition['all']:
            value = known_value(term, ref)
            if value is not None:
                return value
    return None


class Discovery:
    def __init__(self, source, room):
        self.source, self.room = source, room
        self.norm = Normalizer(source, room)
        self.issues = []
        self.objects = set(source.room_objects(room)) | {room}
        self.states = []
        self.events = set()
        self.counter_states = {}

    def issue(self, reason, value, **details):
        record = {'reason': reason, 'source': provenance(value), **details}
        if record not in self.issues:
            self.issues.append(record)

    def mutation(self, path, node):
        op, args = zil.head(node), node.items[1:]
        if op == 'SETG' and len(args) == 2:
            name = zil.symbol(args[0])
            definition = self.source.index['GLOBAL'].get(name)
            kind = self.norm.global_type(name) if definition else None
            if not kind:
                return replace(path, limitations=path.limitations + (zil.render(node),))
            obs = self.norm.observe(Observation('global', name, kind))
            ok, value = self.norm.literal(args[1], kind)
            if ok:
                return assign(path, obs, value)
            assigned = args[1]
            if zil.head(assigned) in {'+', '-'} and len(assigned.items) == 3:
                a, b = assigned.items[1:]
                old = known_value(path.condition, obs.id)
                amount = None
                if zil.symbol(a) == name and type(zil.scalar(b)) is int:
                    amount = zil.scalar(b) * (1 if zil.head(assigned) == '+' else -1)
                elif zil.head(assigned) == '+' and zil.symbol(b) == name and type(zil.scalar(a)) is int:
                    amount = zil.scalar(a)
                if amount is not None:
                    if type(old) is int:
                        value = (old + amount) & 0xffff
                        if kind == 'signed16' and value >= 32768:
                            value -= 65536
                        return assign(path, obs, value)
                    # A later comparison constrains the resulting value, not the
                    # pre-increment value. Do not manufacture an equality here.
                    return replace(path, condition=remove_ref(path.condition, obs.id),
                                   writes=path.writes + (obs.id,))
            return replace(path, condition=combine('all', [remove_ref(path.condition, obs.id),
                                                          {'unknown': zil.render(node)}]),
                           writes=path.writes + (obs.id,))
        if op in {'FSET', 'FCLEAR'} and len(args) == 2:
            obj, flag = self.norm.object_symbol(args[0]), zil.symbol(args[1])
            if obj and flag:
                obs = self.norm.observe(Observation('object_attribute', obj, 'boolean', flag))
                return assign(path, obs, op == 'FSET')
        if op in {'MOVE', 'REMOVE', 'REMOVE-CAREFULLY'} and args:
            obj = self.norm.object_symbol(args[0])
            dest = self.norm.object_symbol(args[1]) if len(args) == 2 else None
            if obj and (dest or op != 'MOVE'):
                obs = self.norm.observe(Observation('object_parent', obj, 'object_ref'))
                return assign(path, obs, dest)
        return replace(path, limitations=path.limitations + (zil.render(node),))

    def sequence(self, expressions, paths=None, depth=0):
        paths = paths if paths is not None else [PathState()]
        if not expressions:
            return paths
        if depth > 20:
            self.issue('control_flow_depth_limit', expressions[0])
            return [replace(p, limitations=p.limitations + ('control flow depth limit',)) for p in paths]
        for node in expressions:
            op = zil.head(node)
            if op == 'COND':
                expanded = []
                for path in paths:
                    if path.terminal or path.returned:
                        expanded.append(path)
                        continue
                    prior = []
                    has_else = False
                    for clause in node.items[1:]:
                        if not isinstance(clause, zil.Form) or not clause.items:
                            continue
                        condition = self.norm.condition(clause.items[0])
                        branch = combine('all', [path.condition, negate(combine('any', prior)), condition])
                        has_else |= condition is True
                        prior.append(condition)
                        if branch is not False:
                            expanded.extend(self.sequence(clause.items[1:],
                                                          [replace(path, condition=branch)], depth + 1))
                    if not has_else:
                        branch = combine('all', [path.condition, negate(combine('any', prior))])
                        if branch is not False:
                            expanded.append(replace(path, condition=branch))
                if len(expanded) > PATH_LIMIT:
                    self.issue('path_limit', node, count=len(expanded))
                    return [replace(p, limitations=p.limitations + ('path limit',)) for p in paths]
                paths = expanded
                continue
            updated = []
            for path in paths:
                if path.terminal or path.returned:
                    updated.append(path)
                    continue
                if op in MUTATIONS:
                    path = self.mutation(path, node)
                elif op == 'TELL':
                    tables = tuple((n, len(path.writes)) for n in zil.walk(node) if zil.head(n) == 'GET')
                    opaque = tuple('Unsupported TELL interpolation: ' + zil.render(n)
                                   for n in node.items[1:]
                                   if isinstance(n, zil.Form) and zil.head(n) != 'GET')
                    path = replace(path, text=path.text + tuple(zil.strings_in(node)),
                                   tables=path.tables + tables,
                                   limitations=path.limitations + opaque)
                elif op in {'JIGS-UP', 'QUIT', 'FINISH'}:
                    path = replace(path, terminal=True)
                elif op == 'QUEUE':
                    event = zil.symbol(node.items[1]) if len(node.items) > 1 else None
                    if event:
                        self.events.add(event)
                    self.issue('queued_event_requires_context', node)
                elif op in {'RTRUE', 'RFALSE', 'RETURN'}:
                    # Stop this source path; it is not a terminal game state.
                    # A return from an action may still establish its post-state.
                    updated.append(replace(path, returned=True))
                    continue
                elif op in {'THIS-IS-IT', 'CRLF'} or isinstance(node, (zil.Atom, zil.ZString)):
                    pass
                elif op:
                    # Retain unsupported calls/control forms. Their text or
                    # mutation side effects must not be silently ignored.
                    path = replace(path, limitations=path.limitations + (zil.render(node),))
                    for nested in zil.walk(node):
                        if zil.head(nested) == 'QUEUE' and len(nested.items) > 1:
                            event = zil.symbol(nested.items[1])
                            if event:
                                self.events.add(event)
                updated.append(path)
            paths = updated
        return paths

    def add(self, state):
        old = next((s for s in self.states if s.id == state.id), None)
        if old:
            if old.basis == state.basis and old.text == state.text:
                # Same asset identity and source presentation: alternate paths
                # are a union, not competing runtime states.
                old.condition = combine('any', [old.condition, state.condition])
            else:
                self.issue('state_id_collision', self.source.index['ROOM'][self.room].form,
                           state=state.id, conditions=[old.condition, state.condition])
                old.condition = {'unknown': 'State identity collision: ' + state.id}
            return
        self.states.append(state)

    def look_states(self, routine):
        found = False
        # Only a direct M-LOOK dispatch is admitted without extra outer guards.
        for node in zil.walk(routine.form):
            if zil.head(node) != 'COND':
                continue
            for clause in node.items[1:]:
                if not isinstance(clause, zil.Form) or not clause.items:
                    continue
                guard = clause.items[0]
                if not zil.contains_symbol(guard, 'M-LOOK'):
                    continue
                simple = (zil.head(guard) in {'EQUAL?', '==?'} and len(guard.items) == 3
                          and node in routine.form.items[3:])
                for path in self.sequence(clause.items[1:]):
                    if not path.text and not path.tables:
                        self.issue('look_path_without_static_text', clause)
                        continue
                    if path.tables and simple and not path.writes and self.tables_from_path(path, routine):
                        found = True
                        continue
                    condition = path.condition
                    if path.tables:
                        condition = combine('all', [condition, {'unknown': 'Unsupported indexed LOOK text'}])
                    if not simple:
                        condition = combine('all', [condition, {'unknown': zil.render(guard)}])
                    if path.writes or path.limitations:
                        condition = combine('all', [condition, {'unknown': '; '.join(path.limitations)
                                                               or 'M-LOOK mutates observed state'}])
                    identity = digest([routine.name, path.text, condition])
                    self.add(State('LOOK-' + identity, condition, 'conditional_room_description',
                                   provenance(clause), path.text))
                    found = True
        return found

    def tables_from_path(self, path, routine):
        discovered = False
        for node, write_count in path.tables:
            if len(node.items) != 3:
                continue
            table, counter = map(zil.symbol, node.items[1:])
            definition = self.source.index['GLOBAL'].get(table)
            if not definition or counter not in self.norm.numeric:
                continue
            if self.norm.global_type(counter) != 'signed16':
                self.issue('unproven_indexed_counter_type', node)
                continue
            value = definition.form.items[2] if len(definition.form.items) > 2 else None
            if zil.head(value) != 'TABLE':
                self.issue('unsupported_indexed_table_layout', node)
                continue
            messages = [n.value for n in value.items[1:] if isinstance(n, zil.ZString)]
            if not messages or any(not isinstance(n, zil.ZString) and zil.head(n) != 'PURE'
                                   for n in value.items[1:]):
                self.issue('unsupported_indexed_table_layout', node)
                continue
            obs = self.norm.observe(Observation('global', counter, 'signed16'))
            self.counter_states[counter] = len(messages)
            for index, message in enumerate(messages):
                condition = combine('all', [path.condition, {'ref': obs.id, 'eq': index}])
                limitations = path.limitations
                if counter in path.writes[write_count:]:
                    limitations += ('Counter mutated after indexed presentation text: ' + zil.render(node),)
                if limitations:
                    condition = combine('all', [condition, {'unknown': '; '.join(limitations)}])
                if condition is not False:
                    self.add(State(f'{counter}-STATE-{index}', condition, 'indexed_presentation_text',
                                   provenance(node), (message,)))
            discovered = True
        return discovered

    def action_states(self, routine):
        # Discover action bodies from syntax alone. Trigger operands are not
        # persistent observations; other guards remain part of the proof.
        for node in zil.walk(routine.form):
            if zil.head(node) != 'COND':
                continue
            for clause in node.items[1:]:
                if not isinstance(clause, zil.Form) or not clause.items:
                    continue
                guard = clause.items[0]
                if not zil.contains_symbol(guard, 'VERB?'):
                    continue
                self.action_body(routine, guard, clause.items[1:])

    def trigger_guard(self, guard):
        op = zil.head(guard)
        if op == 'VERB?':
            return True
        if op == 'AND':
            return combine('all', [self.trigger_guard(g) for g in guard.items[1:]])
        if op in {'EQUAL?', '==?'} and any(zil.symbol(v) in {'PRSO', 'PRSI'}
                                         for v in guard.items[1:]):
            return True
        return self.norm.condition(guard)

    def action_body(self, routine, guard, body):
        for path in self.sequence(body, [PathState(condition=self.trigger_guard(guard))]):
            if path.terminal:
                continue
            self.tables_from_path(path, routine)
            relevant = [ref for ref in path.writes if ref in self.norm.observations and
                        self.norm.observations[ref].kind == 'object_attribute' and
                        self.norm.observations[ref].symbol in self.objects and
                        self.norm.observations[ref].member in PRESENTATION_FLAGS]
            if relevant and path.text:
                condition = path.condition
                if path.limitations:
                    condition = combine('all', [condition, {'unknown': '; '.join(path.limitations)}])
                identity = digest([routine.name, condition, path.text])
                self.add(State('POST-' + routine.name + '-' + identity, condition,
                               'explicit_presentation_mutation', provenance(routine.form), path.text))

    def discover(self):
        room = self.source.index['ROOM'][self.room]
        action = self.source.property_values(room, 'ACTION')
        handler = self.source.index['ROUTINE'].get(zil.symbol(action[0])) if action else None
        has_look = self.look_states(handler) if handler else False
        for name in self.source.handlers(self.room):
            self.action_states(self.source.index['ROUTINE'][name])
        if not has_look:
            if len(self.counter_states) == 1:
                counter = next(iter(self.counter_states))
                definition = self.source.index['GLOBAL'][counter]
                ok, initial = self.norm.literal(definition.form.items[2], 'signed16')
                condition = {'ref': counter, 'eq': initial} if ok else {'unknown': zil.render(definition.form)}
            elif self.states or self.counter_states:
                condition = {'unknown': 'No source-proven default complement for presentation variants'}
            else:
                condition = True
            base_text = tuple(zil.strings_in(self.source.property_values(room, 'LDESC')))
            self.add(State('INITIAL', condition, 'base_room_description', provenance(room.form), base_text))
        self.states.sort(key=lambda s: (s.id != 'INITIAL', s.id))
        return self.states
