#!/usr/bin/env python3
"""Extract source-backed scene facts from the Zork I ZIL sources.

The extractor is deliberately conservative.  It parses ZIL forms instead of
matching whole definitions with regular expressions, but it does not attempt
to execute arbitrary ZIL.  Expressions that cannot be normalized safely are
retained as ``source_expression`` conditions or effects.

Typical use::

    python tools/scene-extractor/extract_scenes.py --output-dir generated-scenes
    python tools/scene-extractor/extract_scenes.py --room SANDY-CAVE

The generated files conform to ``schemas/scene-facts-v1.schema.json``.  If the
optional ``jsonschema`` package is installed, pass ``--validate`` to validate
each output file against that schema.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Iterator, Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE_ROOT = PROJECT_ROOT / "vendor" / "infocom-zork1"
DEFAULT_SCHEMA = PROJECT_ROOT / "schemas" / "scene-facts-v1.schema.json"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "generated-scenes"


DIRECTIONS = {
    "NORTH",
    "SOUTH",
    "EAST",
    "WEST",
    "NE",
    "NW",
    "SE",
    "SW",
    "UP",
    "DOWN",
    "IN",
    "OUT",
    "LAND",
}

GENERIC_FLAG_ACTIONS = {
    "TAKEBIT": ("TAKE",),
    "READBIT": ("READ",),
    "TURNBIT": ("TURN",),
    "CLIMBBIT": ("CLIMB",),
    "DOORBIT": ("OPEN", "CLOSE"),
    "VEHBIT": ("ENTER", "EXIT"),
}

SCALAR_PROPERTIES = {
    "CAPACITY",
    "SIZE",
    "VALUE",
    "TVALUE",
    "STRENGTH",
    "VTYPE",
}


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    line: int


@dataclass
class Atom:
    value: str
    file: str
    line_start: int
    line_end: int


@dataclass
class ZString:
    value: str
    file: str
    line_start: int
    line_end: int


@dataclass
class Form:
    opener: str
    items: list[ZValue]
    file: str
    line_start: int
    line_end: int


ZValue = Atom | ZString | Form


@dataclass
class Definition:
    kind: str
    name: str
    form: Form


@dataclass
class ActionAnalysis:
    data: dict[str, Any]
    condition: ZValue
    body: list[ZValue]
    source_ref: str


@dataclass
class PathAnalysis:
    conditions: list[str]
    effects: list[dict[str, Any]]
    output_text: list[str]
    terminal: bool


def tokenize(text: str) -> Iterator[Token]:
    """Tokenize the ZIL delimiters, atoms, strings, and line comments."""

    i = 0
    line = 1
    length = len(text)
    delimiters = "<>()[]"

    while i < length:
        char = text[i]
        if char in " \t\r\f":
            i += 1
            continue
        if char == "\n":
            line += 1
            i += 1
            continue
        if char == ";":
            # In ZIL, semicolon comments the next syntactic value, not the
            # remainder of the physical line.  That value may be a multiline
            # form, as in ;(<COND ...>).
            yield Token("comment", ";", line)
            i += 1
            continue
        # ZIL character literals use forms such as !\".  The quote is part
        # of the atom and must not be mistaken for the start of a string.
        if char == "!" and i + 2 < length and text[i + 1] == "\\":
            yield Token("atom", text[i : i + 3], line)
            i += 3
            continue
        if char == "\\" and i + 1 < length:
            yield Token("atom", text[i : i + 2], line)
            i += 2
            continue
        if char in delimiters:
            yield Token("delimiter", char, line)
            i += 1
            continue
        if char == '"':
            start_line = line
            i += 1
            value: list[str] = []
            while i < length:
                char = text[i]
                if char == '"':
                    i += 1
                    break
                if char == "\\" and i + 1 < length:
                    next_char = text[i + 1]
                    if next_char in {'"', "\\"}:
                        value.append(next_char)
                        i += 2
                        continue
                value.append(char)
                if char == "\n":
                    line += 1
                i += 1
            else:
                raise ValueError(f"Unterminated string beginning on line {start_line}")
            yield Token("string", "".join(value), start_line)
            continue

        start = i
        start_line = line
        while i < length:
            char = text[i]
            if char.isspace() or char in delimiters or char in {'"', ";"}:
                break
            i += 1
        if i == start:
            i += 1
        else:
            yield Token("atom", text[start:i], start_line)


def parse_file(path: Path) -> list[Form]:
    tokens = list(tokenize(path.read_text(encoding="utf-8", errors="replace")))
    index = 0
    close_for = {"<": ">", "(": ")", "[": "]"}

    def parse_value() -> ZValue:
        nonlocal index
        if index >= len(tokens):
            raise ValueError(f"Unexpected end of {path}")
        token = tokens[index]
        index += 1
        if token.kind == "comment":
            if index >= len(tokens):
                raise ValueError(f"Dangling comment marker in {path}:{token.line}")
            parse_value()
            return parse_value()
        if token.kind == "string":
            return ZString(token.value, path.name, token.line, token.line)
        if token.kind == "atom":
            return Atom(token.value, path.name, token.line, token.line)
        if token.value not in close_for:
            raise ValueError(
                f"Unexpected delimiter {token.value!r} in {path}:{token.line}"
            )
        opener = token.value
        closer = close_for[opener]
        items: list[ZValue] = []
        while index < len(tokens) and not (
            tokens[index].kind == "delimiter" and tokens[index].value == closer
        ):
            if tokens[index].kind == "comment":
                index += 1
                if index >= len(tokens):
                    raise ValueError(
                        f"Dangling comment marker in {path}:{tokens[index - 1].line}"
                    )
                parse_value()
                continue
            if tokens[index].kind == "delimiter" and tokens[index].value in {">", ")", "]"}:
                raise ValueError(
                    f"Unexpected delimiter {tokens[index].value!r}; expected {closer!r} "
                    f"for {opener!r} opened in {path}:{token.line} "
                    f"(at line {tokens[index].line})"
                )
            items.append(parse_value())
        if index >= len(tokens):
            raise ValueError(
                f"Unclosed {opener!r} form in {path}:{token.line}"
            )
        end = tokens[index]
        index += 1
        return Form(opener, items, path.name, token.line, end.line)

    forms: list[Form] = []
    while index < len(tokens):
        if tokens[index].kind == "comment":
            index += 1
            if index < len(tokens):
                parse_value()
            continue
        value = parse_value()
        if isinstance(value, Form):
            forms.append(value)
    return forms


def atom(value: ZValue | None) -> str | None:
    return value.value if isinstance(value, Atom) else None


def symbol(value: ZValue | None) -> str | None:
    raw = atom(value)
    if raw is None:
        return None
    return raw.lstrip(",.")


def head(form: ZValue | None) -> str | None:
    if not isinstance(form, Form) or not form.items:
        return None
    return atom(form.items[0])


def render(value: ZValue) -> str:
    if isinstance(value, Atom):
        return value.value
    if isinstance(value, ZString):
        return json.dumps(value.value, ensure_ascii=False)
    closer = {"<": ">", "(": ")", "[": "]"}[value.opener]
    return value.opener + " ".join(render(item) for item in value.items) + closer


def walk(value: ZValue) -> Iterator[ZValue]:
    yield value
    if isinstance(value, Form):
        for item in value.items:
            yield from walk(item)


def strings_in(value: ZValue | Sequence[ZValue]) -> list[str]:
    roots = value if isinstance(value, Sequence) and not isinstance(value, (str, bytes)) else [value]  # type: ignore[list-item]
    result: list[str] = []
    for root in roots:
        for nested in walk(root):
            if isinstance(nested, ZString):
                result.append(nested.value)
    return result


def contains_symbol(value: ZValue, wanted: str) -> bool:
    return any(symbol(nested) == wanted for nested in walk(value))


def scalar(value: ZValue | None) -> Any:
    if value is None:
        return None
    if isinstance(value, ZString):
        return value.value
    if isinstance(value, Form) and not value.items and value.opener == "<":
        return False
    raw = atom(value)
    if raw == "T":
        return True
    if raw in {"<>", "FALSE"}:
        return False
    if raw is not None and re.fullmatch(r"[-+]?\d+", raw):
        return int(raw)
    if raw is not None:
        return raw.lstrip(",")
    return render(value)


def property_forms(definition: Definition) -> list[Form]:
    return [
        item
        for item in definition.form.items[2:]
        if isinstance(item, Form) and item.opener == "("
    ]


def properties(definition: Definition, property_name: str) -> list[Form]:
    return [form for form in property_forms(definition) if head(form) == property_name]


def first_property(definition: Definition, property_name: str) -> Form | None:
    matches = properties(definition, property_name)
    return matches[0] if matches else None


def values_after_head(form: Form | None) -> list[ZValue]:
    return form.items[1:] if form else []


def first_string(form: Form | None) -> str | None:
    for value in values_after_head(form):
        if isinstance(value, ZString):
            return value.value
    return None


def source_ref_id(kind: str, name: str) -> str:
    return f"SRC-{kind}-{name}".upper()


def make_source_reference(definition: Definition) -> dict[str, Any]:
    return {
        "id": source_ref_id(definition.kind, definition.name),
        "file": definition.form.file,
        "line_start": definition.form.line_start,
        "line_end": definition.form.line_end,
        "subject": definition.name,
    }


def true_condition() -> dict[str, Any]:
    return {"op": "true"}


def source_condition(expression: str) -> dict[str, Any]:
    if expression in {"T", "<>", ""}:
        return true_condition() if expression == "T" else {
            "op": "source_expression",
            "expression": expression or "UNSPECIFIED",
        }
    return {"op": "source_expression", "expression": expression}


def combine_condition_strings(parts: Iterable[str]) -> dict[str, Any]:
    unique = [part for part in dict.fromkeys(parts) if part and part != "T"]
    if not unique:
        return true_condition()
    if len(unique) == 1:
        return source_condition(unique[0])
    return source_condition("<AND " + " ".join(unique) + ">")


def post_condition_from_effects(
    effects: Sequence[dict[str, Any]], path_conditions: Sequence[str]
) -> dict[str, Any] | None:
    """Build a concrete post-state condition when mutations make it safe."""

    conditions: list[dict[str, Any]] = []
    for effect in effects:
        operation = effect["operation"]
        target = effect.get("target")
        if operation == "set_variable" and target:
            conditions.append(
                {
                    "op": "eq",
                    "left": {"state_variable": target},
                    "right": {"value": effect.get("value")},
                }
            )
        elif operation == "increment_variable" and target:
            match = None
            for path_condition in path_conditions:
                match = re.fullmatch(
                    rf"<(?:EQUAL\?|==\?)\s+,{re.escape(target)}\s+(-?\d+)>",
                    path_condition,
                )
                if match:
                    break
            if match:
                conditions.append(
                    {
                        "op": "eq",
                        "left": {"state_variable": target},
                        "right": {"value": int(match.group(1))},
                    }
                )
        elif operation in {"set_flag", "clear_flag"} and target and effect.get("flag"):
            conditions.append(
                {
                    "op": "flag_set" if operation == "set_flag" else "flag_clear",
                    "object": target,
                    "flag": effect["flag"],
                }
            )
        elif operation == "move_object" and target and effect.get("destination"):
            conditions.append(
                {
                    "op": "located_in",
                    "object": target,
                    "location": effect["destination"],
                }
            )
    if not conditions:
        return None
    if len(conditions) == 1:
        return conditions[0]
    return {"op": "and", "args": conditions}


def definition_index(forms: Iterable[Form]) -> dict[str, dict[str, Definition]]:
    result: dict[str, dict[str, Definition]] = {
        "ROOM": {},
        "OBJECT": {},
        "ROUTINE": {},
        "GLOBAL": {},
    }
    for form in forms:
        kind = head(form)
        if kind not in result or len(form.items) < 2:
            continue
        name = atom(form.items[1])
        if name:
            result[kind][name] = Definition(kind, name, form)
    return result


def expand_text_paths(expressions: Sequence[ZValue]) -> list[tuple[list[str], list[str]]]:
    """Symbolically expand TELL/COND sequences into conditional text paths."""

    paths: list[tuple[list[str], list[str]]] = [([], [])]
    for expression in expressions:
        expression_head = head(expression)
        if expression_head == "TELL":
            text = strings_in(expression)
            paths = [(conditions, output + text) for conditions, output in paths]
            continue
        if expression_head == "COND" and isinstance(expression, Form):
            branches: list[tuple[list[str], list[str]]] = []
            prior_conditions: list[str] = []
            has_else = False
            for clause in expression.items[1:]:
                if not isinstance(clause, Form) or not clause.items:
                    continue
                raw_condition = render(clause.items[0])
                if symbol(clause.items[0]) == "T":
                    has_else = True
                    if prior_conditions:
                        raw_condition = "<NOT <OR " + " ".join(prior_conditions) + ">>"
                    else:
                        raw_condition = "T"
                else:
                    prior_conditions.append(raw_condition)
                branch_paths = expand_text_paths(clause.items[1:])
                for branch_conditions, branch_text in branch_paths:
                    branches.append(
                        ([raw_condition] + branch_conditions, branch_text)
                    )
            if not has_else and prior_conditions:
                branches.append(
                    (["<NOT <OR " + " ".join(prior_conditions) + ">>"], [])
                )
            if branches:
                paths = [
                    (base_conditions + branch_conditions, base_text + branch_text)
                    for base_conditions, base_text in paths
                    for branch_conditions, branch_text in branches
                ]
            continue
        if isinstance(expression, Form):
            nested_text = [
                value.value
                for value in expression.items
                if isinstance(value, ZString)
            ]
            if nested_text:
                paths = [
                    (conditions, output + nested_text)
                    for conditions, output in paths
                ]
    return paths


def look_description_paths(routine: Definition) -> list[tuple[list[str], list[str]]]:
    paths: list[tuple[list[str], list[str]]] = []
    for nested in walk(routine.form):
        if head(nested) != "COND" or not isinstance(nested, Form):
            continue
        for clause in nested.items[1:]:
            if not isinstance(clause, Form) or not clause.items:
                continue
            if contains_symbol(clause.items[0], "M-LOOK"):
                paths.extend(expand_text_paths(clause.items[1:]))
    deduplicated: list[tuple[list[str], list[str]]] = []
    seen: set[tuple[tuple[str, ...], tuple[str, ...]]] = set()
    for conditions, text in paths:
        key = (tuple(conditions), tuple(text))
        if text and key not in seen:
            seen.add(key)
            deduplicated.append((conditions, text))
    return deduplicated


def extract_effect(value: ZValue, room_id: str) -> dict[str, Any] | None:
    value_head = head(value)
    if not isinstance(value, Form) or value_head is None:
        return None
    args = value.items[1:]

    def target(index: int = 0) -> str | None:
        result = symbol(args[index]) if len(args) > index else None
        return room_id if result == "HERE" else result

    if value_head == "SETG" and len(args) >= 2:
        target_name = target()
        assigned = args[1]
        if isinstance(assigned, Form) and head(assigned) in {"+", "-"}:
            operands = assigned.items[1:]
            plus_increment = (
                len(operands) == 2
                and head(assigned) == "+"
                and (
                    (scalar(operands[0]) == 1 and symbol(operands[1]) == target_name)
                    or (symbol(operands[0]) == target_name and scalar(operands[1]) == 1)
                )
            )
            minus_increment = (
                len(operands) == 2
                and head(assigned) == "-"
                and symbol(operands[0]) == target_name
                and scalar(operands[1]) == 1
            )
            if plus_increment or minus_increment:
                return {
                    "operation": "increment_variable",
                    "target": target_name,
                    "amount": 1 if plus_increment else -1,
                    "source_expression": render(value),
                }
        return {
            "operation": "set_variable",
            "target": target_name,
            "value": scalar(assigned),
            "source_expression": render(value),
        }
    if value_head in {"FSET", "FCLEAR"} and len(args) >= 2:
        return {
            "operation": "set_flag" if value_head == "FSET" else "clear_flag",
            "target": target(),
            "flag": symbol(args[1]),
            "source_expression": render(value),
        }
    if value_head == "MOVE" and len(args) >= 2:
        return {
            "operation": "move_object",
            "target": target(),
            "destination": symbol(args[1]) or render(args[1]),
            "source_expression": render(value),
        }
    if value_head in {"REMOVE", "REMOVE-CAREFULLY"} and args:
        return {
            "operation": "remove_object",
            "target": target(),
            "source_expression": render(value),
        }
    if value_head == "MUNG" and args:
        return {
            "operation": "set_room_munged",
            "target": target(),
            "source_expression": render(value),
        }
    if value_head == "THIS-IS-IT" and args:
        return {
            "operation": "set_parser_referent",
            "target": target(),
            "source_expression": render(value),
        }
    if value_head in {"ENABLE", "DISABLE"}:
        event = None
        for nested in walk(value):
            if head(nested) == "QUEUE" and isinstance(nested, Form) and len(nested.items) > 1:
                event = symbol(nested.items[1])
                break
        return {
            "operation": "enable_event" if value_head == "ENABLE" else "disable_event",
            "target": event or render(value),
            "source_expression": render(value),
        }
    if value_head == "QUEUE" and len(args) >= 2 and scalar(args[1]) == 0:
        return {
            "operation": "disable_event",
            "target": target(),
            "source_expression": render(value),
        }
    return None


def analyze_execution_paths(expressions: Sequence[ZValue], room_id: str) -> list[PathAnalysis]:
    paths = [PathAnalysis([], [], [], False)]
    for expression in expressions:
        expression_head = head(expression)
        if expression_head == "COND" and isinstance(expression, Form):
            branch_paths: list[PathAnalysis] = []
            prior_conditions: list[str] = []
            has_else = False
            for clause in expression.items[1:]:
                if not isinstance(clause, Form) or not clause.items:
                    continue
                raw_condition = render(clause.items[0])
                if symbol(clause.items[0]) == "T":
                    has_else = True
                    raw_condition = (
                        "<NOT <OR " + " ".join(prior_conditions) + ">>"
                        if prior_conditions
                        else "T"
                    )
                else:
                    prior_conditions.append(raw_condition)
                condition_effects = [
                    effect
                    for nested in walk(clause.items[0])
                    if (effect := extract_effect(nested, room_id)) is not None
                ]
                for branch in analyze_execution_paths(clause.items[1:], room_id):
                    branch_paths.append(
                        PathAnalysis(
                            [raw_condition] + branch.conditions,
                            condition_effects + branch.effects,
                            branch.output_text,
                            branch.terminal,
                        )
                    )
            if not has_else and prior_conditions:
                branch_paths.append(
                    PathAnalysis(
                        ["<NOT <OR " + " ".join(prior_conditions) + ">>"],
                        [],
                        [],
                        False,
                    )
                )
            if branch_paths:
                paths = [
                    PathAnalysis(
                        base.conditions + branch.conditions,
                        base.effects + branch.effects,
                        base.output_text + branch.output_text,
                        base.terminal or branch.terminal,
                    )
                    for base in paths
                    for branch in branch_paths
                ]
            continue

        effect = extract_effect(expression, room_id)
        output = strings_in(expression) if expression_head in {"TELL", "JIGS-UP"} else []
        terminal = expression_head == "JIGS-UP"

        nested_effects: list[dict[str, Any]] = []
        if effect is not None:
            nested_effects.append(effect)
        elif isinstance(expression, Form):
            for nested in expression.items[1:]:
                nested_effect = extract_effect(nested, room_id)
                if nested_effect:
                    nested_effects.append(nested_effect)

        if nested_effects or output or terminal:
            paths = [
                PathAnalysis(
                    path.conditions,
                    path.effects + nested_effects,
                    path.output_text + output,
                    path.terminal or terminal,
                )
                for path in paths
            ]
    return paths


def verb_nodes(value: ZValue) -> list[Form]:
    return [
        nested
        for nested in walk(value)
        if isinstance(nested, Form) and head(nested) == "VERB?"
    ]


def indirect_object_from_condition(value: ZValue) -> str | None:
    for nested in walk(value):
        if not isinstance(nested, Form) or head(nested) not in {"EQUAL?", "==?"}:
            continue
        symbols = [symbol(item) for item in nested.items[1:]]
        if "PRSI" in symbols:
            for candidate in symbols:
                if candidate and candidate != "PRSI":
                    return candidate
    return None


def routine_actions(
    routine: Definition,
    direct_object: str,
    room_id: str,
) -> list[ActionAnalysis]:
    result: list[ActionAnalysis] = []
    serial = 0
    ref = source_ref_id("ROUTINE", routine.name)
    for nested in walk(routine.form):
        if not isinstance(nested, Form) or head(nested) != "COND":
            continue
        for clause in nested.items[1:]:
            if not isinstance(clause, Form) or not clause.items:
                continue
            verbs: list[str] = []
            for verb_form in verb_nodes(clause.items[0]):
                verbs.extend(
                    candidate
                    for candidate in (symbol(item) for item in verb_form.items[1:])
                    if candidate
                )
            verbs = list(dict.fromkeys(verbs))
            if not verbs:
                continue
            serial += 1
            action_id = f"{routine.name}-{'-'.join(verbs)}-{serial}"
            action: dict[str, Any] = {
                "id": action_id,
                "verbs": verbs,
                "direct_object": direct_object,
                "condition": source_condition(render(clause.items[0])),
                "handler": routine.name,
                "output_text": strings_in(clause.items[1:]),
                "effects": [],
                "conditional_effects": [],
                "source_refs": [ref],
            }
            indirect_object = indirect_object_from_condition(clause.items[0])
            if indirect_object:
                action["indirect_object"] = indirect_object
            paths = analyze_execution_paths(clause.items[1:], room_id)
            if len(paths) == 1 and not paths[0].conditions:
                action["effects"] = paths[0].effects
            else:
                action["conditional_effects"] = [
                    {
                        "condition": combine_condition_strings(path.conditions),
                        "effects": path.effects,
                        "output_text": path.output_text,
                    }
                    for path in paths
                    if path.effects or path.output_text
                ]
            result.append(
                ActionAnalysis(action, clause.items[0], clause.items[1:], ref)
            )
    return result


def generic_actions(object_id: str, flags: list[str]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for flag, verbs in GENERIC_FLAG_ACTIONS.items():
        if flag not in flags:
            continue
        result.append(
            {
                "id": f"GENERIC-{object_id}-{'-'.join(verbs)}",
                "verbs": list(verbs),
                "direct_object": object_id,
                "condition": true_condition(),
                "handler": "GENERIC-PARSER",
                "output_text": [],
                "effects": [],
                "conditional_effects": [],
                "source_refs": [],
            }
        )
    return result


class SceneExtractor:
    def __init__(self, source_root: Path):
        self.source_root = source_root
        self.forms: list[Form] = []
        for path in sorted(source_root.glob("*.zil")):
            self.forms.extend(parse_file(path))
        self.index = definition_index(self.forms)

    def room_names(self) -> list[str]:
        return list(self.index["ROOM"])

    def object_location(self, definition: Definition) -> str | None:
        prop = first_property(definition, "IN")
        return symbol(prop.items[1]) if prop and len(prop.items) > 1 else None

    def contained_objects(self, room_id: str) -> list[Definition]:
        included: dict[str, Definition] = {}
        changed = True
        while changed:
            changed = False
            for name, definition in self.index["OBJECT"].items():
                location = self.object_location(definition)
                if location == room_id or location in included:
                    if name not in included:
                        included[name] = definition
                        changed = True
        return list(included.values())

    def flags_for(self, definition: Definition) -> list[str]:
        prop = first_property(definition, "FLAGS")
        return [candidate for candidate in map(symbol, values_after_head(prop)) if candidate]

    def object_json(
        self,
        definition: Definition,
        room_id: str,
        kind: str = "object",
    ) -> tuple[dict[str, Any], list[ActionAnalysis], list[Definition]]:
        object_id = definition.name
        flags = self.flags_for(definition)
        action_analyses: list[ActionAnalysis] = []
        refs = [definition]
        action_prop = first_property(definition, "ACTION")
        handler = symbol(action_prop.items[1]) if action_prop and len(action_prop.items) > 1 else None
        if handler and handler in self.index["ROUTINE"]:
            routine = self.index["ROUTINE"][handler]
            refs.append(routine)
            action_analyses = routine_actions(routine, object_id, room_id)

        actions = generic_actions(object_id, flags) + [analysis.data for analysis in action_analyses]
        source_properties: dict[str, Any] = {}
        for property_name in SCALAR_PROPERTIES:
            prop = first_property(definition, property_name)
            if prop and len(prop.items) > 1:
                source_properties[property_name] = scalar(prop.items[1])
        for property_name in ("FDESC", "LDESC"):
            prop = first_property(definition, property_name)
            text = first_string(prop)
            if text is not None:
                source_properties[property_name] = text

        location = self.object_location(definition)
        initial_facts = [
            {
                "subject": object_id,
                "predicate": "location",
                "value": location,
                "basis": "source_location",
                "source_refs": [source_ref_id("OBJECT", object_id)],
            }
        ]
        initial_facts.extend(
            {
                "subject": object_id,
                "predicate": "flag_set",
                "value": flag,
                "basis": "source_flag",
                "source_refs": [source_ref_id("OBJECT", object_id)],
            }
            for flag in flags
        )

        synonyms_prop = first_property(definition, "SYNONYM")
        adjective_prop = first_property(definition, "ADJECTIVE")
        result = {
            "id": object_id,
            "kind": kind,
            "description": first_string(first_property(definition, "DESC")),
            "location": location,
            "synonyms": [
                candidate
                for candidate in map(symbol, values_after_head(synonyms_prop))
                if candidate
            ],
            "adjectives": [
                candidate
                for candidate in map(symbol, values_after_head(adjective_prop))
                if candidate
            ],
            "source_flags": flags,
            "source_properties": source_properties,
            "initial_facts": initial_facts,
            "actions": actions,
            "source_refs": [source_ref_id("OBJECT", object_id)],
        }
        return result, action_analyses, refs

    def pseudo_objects(
        self, room: Definition
    ) -> tuple[list[dict[str, Any]], list[ActionAnalysis], list[Definition]]:
        objects: list[dict[str, Any]] = []
        analyses: list[ActionAnalysis] = []
        refs: list[Definition] = []
        serial = 0
        for prop in properties(room, "PSEUDO"):
            values = prop.items[1:]
            index = 0
            while index + 1 < len(values):
                name_value, routine_value = values[index], values[index + 1]
                index += 2
                if not isinstance(name_value, ZString):
                    continue
                routine_name = symbol(routine_value)
                serial += 1
                object_id = "PSEUDO-" + re.sub(r"[^A-Z0-9]+", "-", name_value.value.upper()).strip("-")
                action_data: list[dict[str, Any]] = []
                object_analyses: list[ActionAnalysis] = []
                source_refs = [source_ref_id("ROOM", room.name)]
                if routine_name and routine_name in self.index["ROUTINE"]:
                    routine = self.index["ROUTINE"][routine_name]
                    refs.append(routine)
                    object_analyses = routine_actions(routine, object_id, room.name)
                    analyses.extend(object_analyses)
                    action_data = [analysis.data for analysis in object_analyses]
                    source_refs.append(source_ref_id("ROUTINE", routine_name))
                objects.append(
                    {
                        "id": object_id,
                        "kind": "pseudo_object",
                        "description": name_value.value.lower(),
                        "location": room.name,
                        "synonyms": [name_value.value.upper()],
                        "adjectives": [],
                        "source_flags": [],
                        "source_properties": {
                            "ACTION": routine_name,
                        },
                        "initial_facts": [
                            {
                                "subject": object_id,
                                "predicate": "location",
                                "value": room.name,
                                "basis": "source_location",
                                "source_refs": [source_ref_id("ROOM", room.name)],
                            }
                        ],
                        "actions": action_data,
                        "source_refs": source_refs,
                    }
                )
        return objects, analyses, refs

    def exit_json(self, room: Definition) -> list[dict[str, Any]]:
        result: list[dict[str, Any]] = []
        room_ref = source_ref_id("ROOM", room.name)
        for prop in property_forms(room):
            direction = head(prop)
            if direction not in DIRECTIONS:
                continue
            values = prop.items[1:]
            destination = None
            condition = true_condition()
            blocked_text = [value.value for value in values if isinstance(value, ZString)]
            effects: list[dict[str, Any]] = []
            symbols = [symbol(value) for value in values]
            if "TO" in symbols:
                to_index = symbols.index("TO")
                if to_index + 1 < len(values):
                    destination = symbol(values[to_index + 1])
            if "IF" in symbols:
                if_index = symbols.index("IF")
                condition_value = values[if_index + 1] if if_index + 1 < len(values) else prop
                condition = source_condition(render(condition_value))
            elif "PER" in symbols or destination is None:
                condition = source_condition(render(prop))
            if "PER" in symbols:
                per_index = symbols.index("PER")
                if per_index + 1 < len(values):
                    routine_name = symbol(values[per_index + 1])
                    effects.append(
                        {
                            "operation": "call_routine",
                            "routine": routine_name,
                            "arguments": [],
                            "source_expression": render(prop),
                        }
                    )
            result.append(
                {
                    "id": f"EXIT-{direction}",
                    "directions": [direction.lower()],
                    "destination": destination,
                    "condition": condition,
                    "blocked_text": blocked_text,
                    "effects": effects,
                    "source_refs": [room_ref],
                }
            )
        return result

    def descriptions_and_states(
        self, room: Definition
    ) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[Definition]]:
        room_ref = source_ref_id("ROOM", room.name)
        descriptions: list[dict[str, Any]] = []
        states: list[dict[str, Any]] = []
        refs: list[Definition] = []

        ldesc = first_string(first_property(room, "LDESC"))
        if ldesc:
            descriptions.append(
                {
                    "id": "LDESC",
                    "trigger": "look",
                    "condition": true_condition(),
                    "text": [ldesc],
                    "source_refs": [room_ref],
                }
            )

        action_prop = first_property(room, "ACTION")
        routine_name = symbol(action_prop.items[1]) if action_prop and len(action_prop.items) > 1 else None
        look_paths: list[tuple[list[str], list[str]]] = []
        routine_ref = None
        if routine_name and routine_name in self.index["ROUTINE"]:
            routine = self.index["ROUTINE"][routine_name]
            refs.append(routine)
            routine_ref = source_ref_id("ROUTINE", routine_name)
            look_paths = look_description_paths(routine)

        for index, (conditions, text) in enumerate(look_paths, 1):
            description_id = f"M-LOOK-{index}"
            description_refs = [room_ref, routine_ref] if routine_ref else [room_ref]
            descriptions.append(
                {
                    "id": description_id,
                    "trigger": "look",
                    "condition": combine_condition_strings(conditions),
                    "text": text,
                    "source_refs": description_refs,
                }
            )
            states.append(
                {
                    "id": f"LOOK-STATE-{index}",
                    "stable": True,
                    "condition": combine_condition_strings(conditions),
                    "facts": [
                        {
                            "subject": room.name,
                            "predicate": "source_description",
                            "value": fragment,
                            "basis": "explicit_text",
                            "source_text": fragment,
                            "source_refs": description_refs,
                        }
                        for fragment in text
                    ],
                    "entry_text": text,
                    "source_refs": description_refs,
                }
            )

        if not descriptions:
            descriptions.append(
                {
                    "id": "NO-EXPLICIT-LOOK-DESCRIPTION",
                    "trigger": "look",
                    "condition": true_condition(),
                    "text": ["No explicit LDESC or mechanically extractable M-LOOK text."],
                    "source_refs": [room_ref],
                }
            )

        if not states:
            facts = []
            for description in descriptions:
                for fragment in description["text"]:
                    facts.append(
                        {
                            "subject": room.name,
                            "predicate": "source_description",
                            "value": fragment,
                            "basis": "explicit_text",
                            "source_text": fragment,
                            "source_refs": description["source_refs"],
                        }
                    )
            states.append(
                {
                    "id": "INITIAL",
                    "stable": True,
                    "condition": true_condition(),
                    "facts": facts,
                    "entry_text": [],
                    "source_refs": [room_ref],
                }
            )
        return descriptions, states, refs

    def transitions_from_actions(
        self,
        room_id: str,
        analyses: Sequence[ActionAnalysis],
        states: list[dict[str, Any]],
    ) -> list[dict[str, Any]]:
        transitions: list[dict[str, Any]] = []
        state_signatures = {
            json.dumps(state["condition"], sort_keys=True): state["id"] for state in states
        }
        serial = 0
        for analysis in analyses:
            paths = analyze_execution_paths(analysis.body, room_id)
            for path in paths:
                if not path.effects and not path.terminal:
                    continue
                serial += 1
                action = analysis.data
                conditions = [render(analysis.condition)] + path.conditions
                transition_id = f"TRANSITION-{serial}-{action['id']}"
                state_effects = [
                    effect
                    for effect in path.effects
                    if effect["operation"]
                    in {
                        "set_variable",
                        "increment_variable",
                        "set_flag",
                        "clear_flag",
                        "move_object",
                        "remove_object",
                        "set_room_munged",
                    }
                ]
                to_state = None
                if state_effects and not path.terminal:
                    post_condition = post_condition_from_effects(
                        state_effects, path.conditions
                    )
                    if post_condition is None:
                        post_expression = "<AFTER " + " ".join(
                            effect.get("source_expression", effect["operation"])
                            for effect in state_effects
                        ) + ">"
                        post_condition = source_condition(post_expression)
                    signature = json.dumps(post_condition, sort_keys=True)
                    to_state = state_signatures.get(signature)
                    if to_state is None:
                        to_state = f"ACTION-STATE-{len(states) + 1}"
                        state_signatures[signature] = to_state
                        states.append(
                            {
                                "id": to_state,
                                "stable": True,
                                "condition": post_condition,
                                "facts": [
                                    {
                                        "subject": room_id,
                                        "predicate": "source_text",
                                        "value": text,
                                        "basis": "explicit_text",
                                        "source_text": text,
                                        "source_refs": [analysis.source_ref],
                                    }
                                    for text in path.output_text
                                ],
                                "entry_text": path.output_text,
                                "source_refs": [analysis.source_ref],
                            }
                        )
                transitions.append(
                    {
                        "id": transition_id,
                        "trigger": {
                            "type": "action",
                            "verb": action["verbs"][0],
                            "direct_object": action.get("direct_object", room_id),
                            **(
                                {"indirect_object": action["indirect_object"]}
                                if "indirect_object" in action
                                else {}
                            ),
                            "routine": action.get("handler"),
                        },
                        "from": combine_condition_strings(conditions),
                        "to_state": to_state,
                        "terminal": path.terminal,
                        "effects": path.effects,
                        "conditional_effects": [],
                        "output_text": path.output_text,
                        "source_refs": [analysis.source_ref],
                    }
                )
        return transitions

    def transitions_from_event(
        self,
        room_id: str,
        routine: Definition,
        states: list[dict[str, Any]],
        serial_offset: int,
    ) -> list[dict[str, Any]]:
        """Extract state-changing paths from a queued/clock routine."""

        transitions: list[dict[str, Any]] = []
        state_signatures = {
            json.dumps(state["condition"], sort_keys=True): state["id"] for state in states
        }
        body = routine.form.items[3:] if len(routine.form.items) > 3 else []
        ref = source_ref_id("ROUTINE", routine.name)
        for path_index, path in enumerate(analyze_execution_paths(body, room_id), 1):
            if not path.effects and not path.terminal and not path.output_text:
                continue
            state_effects = [
                effect
                for effect in path.effects
                if effect["operation"]
                in {
                    "set_variable",
                    "increment_variable",
                    "set_flag",
                    "clear_flag",
                    "move_object",
                    "remove_object",
                    "set_room_munged",
                }
            ]
            to_state = None
            if state_effects and not path.terminal:
                post_condition = post_condition_from_effects(
                    state_effects, path.conditions
                )
                if post_condition is None:
                    post_expression = "<AFTER " + " ".join(
                        effect.get("source_expression", effect["operation"])
                        for effect in state_effects
                    ) + ">"
                    post_condition = source_condition(post_expression)
                signature = json.dumps(post_condition, sort_keys=True)
                to_state = state_signatures.get(signature)
                if to_state is None:
                    to_state = f"EVENT-STATE-{len(states) + 1}"
                    state_signatures[signature] = to_state
                    states.append(
                        {
                            "id": to_state,
                            "stable": True,
                            "condition": post_condition,
                            "facts": [
                                {
                                    "subject": room_id,
                                    "predicate": "source_text",
                                    "value": text,
                                    "basis": "explicit_text",
                                    "source_text": text,
                                    "source_refs": [ref],
                                }
                                for text in path.output_text
                            ],
                            "entry_text": path.output_text,
                            "source_refs": [ref],
                        }
                    )
            transitions.append(
                {
                    "id": f"EVENT-{serial_offset + path_index}-{routine.name}",
                    "trigger": {
                        "type": "clock",
                        "routine": routine.name,
                        "event": routine.name,
                    },
                    "from": combine_condition_strings(path.conditions),
                    "to_state": to_state,
                    "terminal": path.terminal,
                    "effects": path.effects,
                    "conditional_effects": [],
                    "output_text": path.output_text,
                    "source_refs": [ref],
                }
            )
        return transitions

    def expand_counter_table_states(
        self,
        room_id: str,
        analyses: Sequence[ActionAnalysis],
        states: list[dict[str, Any]],
        transitions: list[dict[str, Any]],
    ) -> list[Definition]:
        """Resolve the common ZIL pattern TELL <GET ,TABLE ,COUNTER>.

        This turns source tables such as BDIGS into explicit states without
        interpreting arbitrary routines.
        """

        refs: list[Definition] = []
        existing_state_conditions = {
            json.dumps(state["condition"], sort_keys=True): state["id"] for state in states
        }
        for analysis in analyses:
            for nested in (node for expression in analysis.body for node in walk(expression)):
                if not isinstance(nested, Form) or head(nested) != "GET" or len(nested.items) < 3:
                    continue
                table_name = symbol(nested.items[1])
                counter_name = symbol(nested.items[2])
                table_definition = self.index["GLOBAL"].get(table_name or "")
                counter_definition = self.index["GLOBAL"].get(counter_name or "")
                if not table_definition or not counter_definition or not counter_name:
                    continue
                table_value = (
                    table_definition.form.items[2]
                    if len(table_definition.form.items) > 2
                    else None
                )
                if not isinstance(table_value, Form) or head(table_value) not in {"TABLE", "LTABLE"}:
                    continue
                messages = [
                    item.value
                    for item in table_value.items[1:]
                    if isinstance(item, ZString)
                ]
                if not messages:
                    continue
                initial = (
                    scalar(counter_definition.form.items[2])
                    if len(counter_definition.form.items) > 2
                    else None
                )
                if not isinstance(initial, int):
                    continue
                refs.extend([table_definition, counter_definition])

                replaced_state_ids: set[str] = set()
                retained_transitions: list[dict[str, Any]] = []
                for transition in transitions:
                    matching_increment = any(
                        effect["operation"] == "increment_variable"
                        and effect.get("target") == counter_name
                        for effect in transition["effects"]
                    )
                    same_action = (
                        transition["trigger"].get("routine")
                        == analysis.data.get("handler")
                        and transition["trigger"].get("direct_object")
                        == analysis.data.get("direct_object")
                    )
                    from_expression = transition["from"].get("expression", "")
                    if (
                        matching_increment
                        and same_action
                        and not transition["output_text"]
                        and "<NOT <OR" in from_expression
                    ):
                        if transition.get("to_state"):
                            replaced_state_ids.add(transition["to_state"])
                        continue
                    retained_transitions.append(transition)
                transitions[:] = retained_transitions
                if replaced_state_ids:
                    states[:] = [
                        state for state in states if state["id"] not in replaced_state_ids
                    ]
                    existing_state_conditions = {
                        json.dumps(state["condition"], sort_keys=True): state["id"]
                        for state in states
                    }

                previous_value = initial
                for index, message in enumerate(messages):
                    condition = {
                        "op": "eq",
                        "left": {"state_variable": counter_name},
                        "right": {"value": index},
                    }
                    signature = json.dumps(condition, sort_keys=True)
                    state_id = existing_state_conditions.get(signature)
                    if state_id is None:
                        state_id = f"{counter_name}-STATE-{index}"
                        existing_state_conditions[signature] = state_id
                        states.append(
                            {
                                "id": state_id,
                                "stable": True,
                                "condition": condition,
                                "facts": [
                                    {
                                        "subject": room_id,
                                        "predicate": "source_text",
                                        "value": message,
                                        "basis": "explicit_text",
                                        "source_text": message,
                                        "source_refs": [
                                            source_ref_id("GLOBAL", table_name)
                                        ],
                                    }
                                ],
                                "entry_text": [message],
                                "source_refs": [
                                    source_ref_id("GLOBAL", table_name),
                                    analysis.source_ref,
                                ],
                            }
                        )
                    transitions.append(
                        {
                            "id": f"COUNTER-{counter_name}-{index}-{analysis.data['id']}",
                            "trigger": {
                                "type": "action",
                                "verb": analysis.data["verbs"][0],
                                "direct_object": analysis.data.get("direct_object", room_id),
                                **(
                                    {"indirect_object": analysis.data["indirect_object"]}
                                    if "indirect_object" in analysis.data
                                    else {}
                                ),
                                "routine": analysis.data.get("handler"),
                            },
                            "from": {
                                "op": "eq",
                                "left": {"state_variable": counter_name},
                                "right": {"value": previous_value},
                            },
                            "to_state": state_id,
                            "terminal": False,
                            "effects": [
                                {
                                    "operation": "increment_variable",
                                    "target": counter_name,
                                    "amount": index - previous_value,
                                    "source_expression": render(nested),
                                    "source_refs": [analysis.source_ref],
                                }
                            ],
                            "conditional_effects": [],
                            "output_text": [message],
                            "source_refs": [
                                source_ref_id("GLOBAL", table_name),
                                analysis.source_ref,
                            ],
                        }
                    )
                    previous_value = index
        return refs

    def dependency_json(
        self,
        scene_objects: Sequence[dict[str, Any]],
        analyses: Sequence[ActionAnalysis],
        transitions: Sequence[dict[str, Any]],
        exits: Sequence[dict[str, Any]],
    ) -> tuple[list[dict[str, Any]], list[Definition]]:
        dependencies: dict[tuple[str, str], dict[str, Any]] = {}
        refs: list[Definition] = []
        scene_ids = {obj["id"] for obj in scene_objects}

        for analysis in analyses:
            dependency_id = analysis.data.get("indirect_object")
            definition = self.index["OBJECT"].get(dependency_id)
            if not definition or dependency_id in scene_ids:
                continue
            refs.append(definition)
            dependencies[("object", dependency_id)] = {
                "id": dependency_id,
                "kind": "object",
                "description": first_string(first_property(definition, "DESC")),
                "initial_location": self.object_location(definition),
                "source_flags": self.flags_for(definition),
                "reason": f"Referenced as an indirect object by {analysis.data['id']}.",
                "source_refs": [source_ref_id("OBJECT", dependency_id)],
            }

        for transition in transitions:
            for effect in transition["effects"]:
                if effect["operation"] != "enable_event":
                    continue
                event_id = effect.get("target")
                if not event_id:
                    continue
                definition = self.index["ROUTINE"].get(event_id)
                if definition:
                    refs.append(definition)
                dependencies[("event", event_id)] = {
                    "id": event_id,
                    "kind": "event",
                    "description": None,
                    "initial_location": None,
                    "source_flags": [],
                    "reason": f"Enabled by transition {transition['id']}.",
                    "source_refs": (
                        [source_ref_id("ROUTINE", event_id)] if definition else []
                    ),
                }

        for exit_data in exits:
            for effect in exit_data["effects"]:
                if effect["operation"] != "call_routine":
                    continue
                routine_id = effect.get("routine")
                if not routine_id:
                    continue
                definition = self.index["ROUTINE"].get(routine_id)
                if definition:
                    refs.append(definition)
                dependencies[("routine", routine_id)] = {
                    "id": routine_id,
                    "kind": "routine",
                    "description": None,
                    "initial_location": None,
                    "source_flags": [],
                    "reason": f"Controls {exit_data['id']}.",
                    "source_refs": (
                        [source_ref_id("ROUTINE", routine_id)] if definition else []
                    ),
                }
        return list(dependencies.values()), refs

    def state_variables(
        self,
        room_id: str,
        descriptions: Sequence[dict[str, Any]],
        transitions: Sequence[dict[str, Any]],
        scene_object_ids: set[str],
    ) -> list[dict[str, Any]]:
        variables: dict[str, dict[str, Any]] = {}
        referenced_globals: set[str] = set()
        for description in descriptions:
            expression = description.get("condition", {}).get("expression", "")
            for name in re.findall(r",([A-Z][A-Z0-9-]*)", expression):
                if name in self.index["GLOBAL"]:
                    referenced_globals.add(name)

        effects = [
            effect
            for transition in transitions
            for effect in transition["effects"]
        ]
        for effect in effects:
            operation = effect["operation"]
            target = effect.get("target")
            if operation in {"set_variable", "increment_variable"} and target:
                referenced_globals.add(target)
            if operation in {"set_flag", "clear_flag"} and target:
                variable_id = f"{target}.{effect.get('flag')}"
                definition = self.index["ROOM"].get(target) or self.index["OBJECT"].get(target)
                initial = bool(definition and effect.get("flag") in self.flags_for(definition))
                variables[variable_id] = {
                    "id": variable_id,
                    "kind": "room_flag" if target == room_id else "object_flag",
                    "value_type": "boolean",
                    "initial_value": initial,
                    "source_expression": effect.get("source_expression"),
                    "source_refs": [],
                }
            if operation in {"move_object", "remove_object"} and target in scene_object_ids:
                definition = self.index["OBJECT"].get(target)
                initial = self.object_location(definition) if definition else None
                variables[f"{target}.location"] = {
                    "id": f"{target}.location",
                    "kind": "object_location",
                    "value_type": "object_id",
                    "initial_value": initial,
                    "source_expression": effect.get("source_expression"),
                    "source_refs": (
                        [source_ref_id("OBJECT", target)] if definition else []
                    ),
                }

        for name in sorted(referenced_globals):
            definition = self.index["GLOBAL"].get(name)
            initial_value = scalar(definition.form.items[2]) if definition and len(definition.form.items) > 2 else None
            value_type = (
                "boolean"
                if isinstance(initial_value, bool)
                else "integer"
                if isinstance(initial_value, int)
                else "string"
            )
            variables[name] = {
                "id": name,
                "kind": "global",
                "value_type": value_type,
                "initial_value": initial_value,
                "source_expression": render(definition.form) if definition else None,
                "source_refs": (
                    [source_ref_id("GLOBAL", name)] if definition else []
                ),
            }
        return list(variables.values())

    def extract(self, room_id: str) -> dict[str, Any]:
        if room_id not in self.index["ROOM"]:
            raise KeyError(f"Unknown room: {room_id}")
        room = self.index["ROOM"][room_id]
        referenced_definitions: dict[tuple[str, str], Definition] = {
            (room.kind, room.name): room
        }

        descriptions, states, description_refs = self.descriptions_and_states(room)
        for definition in description_refs:
            referenced_definitions[(definition.kind, definition.name)] = definition

        scene_objects: list[dict[str, Any]] = []
        analyses: list[ActionAnalysis] = []
        for definition in self.contained_objects(room_id):
            object_data, object_analyses, refs = self.object_json(definition, room_id)
            scene_objects.append(object_data)
            analyses.extend(object_analyses)
            for ref in refs:
                referenced_definitions[(ref.kind, ref.name)] = ref

        global_prop = first_property(room, "GLOBAL")
        existing_ids = {obj["id"] for obj in scene_objects}
        for global_id in [candidate for candidate in map(symbol, values_after_head(global_prop)) if candidate]:
            definition = self.index["OBJECT"].get(global_id)
            if not definition or global_id in existing_ids:
                continue
            object_data, object_analyses, refs = self.object_json(
                definition, room_id, "local_global"
            )
            scene_objects.append(object_data)
            existing_ids.add(global_id)
            analyses.extend(object_analyses)
            for ref in refs:
                referenced_definitions[(ref.kind, ref.name)] = ref

        pseudo_data, pseudo_analyses, pseudo_refs = self.pseudo_objects(room)
        scene_objects.extend(pseudo_data)
        analyses.extend(pseudo_analyses)
        for ref in pseudo_refs:
            referenced_definitions[(ref.kind, ref.name)] = ref

        room_action = first_property(room, "ACTION")
        room_handler = symbol(room_action.items[1]) if room_action and len(room_action.items) > 1 else None
        if room_handler and room_handler in self.index["ROUTINE"]:
            routine = self.index["ROUTINE"][room_handler]
            referenced_definitions[(routine.kind, routine.name)] = routine
            analyses.extend(routine_actions(routine, room_id, room_id))

        transitions = self.transitions_from_actions(room_id, analyses, states)
        counter_refs = self.expand_counter_table_states(
            room_id, analyses, states, transitions
        )
        for ref in counter_refs:
            referenced_definitions[(ref.kind, ref.name)] = ref

        # Follow queued routines reached from scene actions.  This captures
        # sequences such as the Maintenance Room flood without attempting to
        # execute the whole game.
        analyzed_events: set[str] = set()
        while True:
            event_names = {
                effect.get("target")
                for transition in transitions
                for effect in transition["effects"]
                if effect["operation"] == "enable_event"
            }
            pending = sorted(
                name
                for name in event_names
                if name and name not in analyzed_events and name in self.index["ROUTINE"]
            )
            if not pending:
                break
            for event_name in pending:
                analyzed_events.add(event_name)
                routine = self.index["ROUTINE"][event_name]
                referenced_definitions[(routine.kind, routine.name)] = routine
                transitions.extend(
                    self.transitions_from_event(
                        room_id, routine, states, len(transitions)
                    )
                )

        exits = self.exit_json(room)
        dependencies, dependency_refs = self.dependency_json(
            scene_objects, analyses, transitions, exits
        )
        for ref in dependency_refs:
            referenced_definitions[(ref.kind, ref.name)] = ref
        object_ids = {obj["id"] for obj in scene_objects}
        state_variables = self.state_variables(
            room_id, descriptions, transitions, object_ids
        )
        initial_state = next(
            (state for state in states if state["id"] == "INITIAL"), None
        )
        if initial_state and state_variables:
            initial_conditions: list[dict[str, Any]] = []
            for variable in state_variables:
                variable_id = variable["id"]
                initial_value = variable["initial_value"]
                if variable["kind"] in {"global", "derived"}:
                    initial_conditions.append(
                        {
                            "op": "eq",
                            "left": {"state_variable": variable_id},
                            "right": {"value": initial_value},
                        }
                    )
                elif variable["kind"] in {"object_flag", "room_flag"}:
                    object_id, flag = variable_id.rsplit(".", 1)
                    initial_conditions.append(
                        {
                            "op": "flag_set" if initial_value else "flag_clear",
                            "object": object_id,
                            "flag": flag,
                        }
                    )
                elif variable["kind"] == "object_location" and initial_value:
                    object_id = variable_id.removesuffix(".location")
                    initial_conditions.append(
                        {
                            "op": "located_in",
                            "object": object_id,
                            "location": initial_value,
                        }
                    )
                initial_state["facts"].append(
                    {
                        "subject": variable_id,
                        "predicate": "initial_value",
                        "value": initial_value,
                        "basis": "source_property",
                        "source_refs": variable.get("source_refs", []),
                    }
                )
            initial_state["condition"] = (
                initial_conditions[0]
                if len(initial_conditions) == 1
                else {"op": "and", "args": initial_conditions}
            )
        for variable in state_variables:
            if variable["kind"] == "global":
                definition = self.index["GLOBAL"].get(variable["id"])
                if definition:
                    referenced_definitions[(definition.kind, definition.name)] = definition

        title = first_string(first_property(room, "DESC")) or room_id.replace("-", " ").title()
        notes = [
            "Generated mechanically from ZIL source; unresolved expressions are preserved as source_expression values."
        ]
        if any(
            state["condition"].get("op") == "source_expression"
            and state["condition"].get("expression", "").startswith("<AFTER ")
            for state in states
        ):
            notes.append(
                "ACTION-STATE conditions describe source mutations and may overlap; arbitrary ZIL was not executed."
            )

        source_references = [
            make_source_reference(definition)
            for _, definition in sorted(referenced_definitions.items())
        ]
        return {
            "schema": "scene_facts_v1",
            "scene": {
                "id": room_id,
                "title": title,
                "descriptions": descriptions,
                "source_flags": self.flags_for(room),
                "exits": exits,
                "objects": scene_objects,
                "dependencies": dependencies,
                "state_variables": state_variables,
                "states": states,
                "transitions": transitions,
                "notes": notes,
                "source_references": source_references,
            },
        }


def _matches_json_type(value: Any, expected: str) -> bool:
    if expected == "null":
        return value is None
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "string":
        return isinstance(value, str)
    if expected == "array":
        return isinstance(value, list)
    if expected == "object":
        return isinstance(value, dict)
    return False


def _validate_schema_subset(
    value: Any,
    schema: dict[str, Any],
    root: dict[str, Any],
    path: str = "$",
) -> None:
    """Validate the JSON-Schema features used by scene-facts-v1.

    This is a dependency-free fallback, not a general JSON Schema engine.
    """

    if "$ref" in schema:
        reference = schema["$ref"]
        if not reference.startswith("#/"):
            raise ValueError(f"{path}: unsupported schema reference {reference}")
        target: Any = root
        for part in reference[2:].split("/"):
            target = target[part.replace("~1", "/").replace("~0", "~")]
        _validate_schema_subset(value, target, root, path)
        return

    if "oneOf" in schema:
        matches = 0
        errors: list[str] = []
        for candidate in schema["oneOf"]:
            try:
                _validate_schema_subset(value, candidate, root, path)
                matches += 1
            except (TypeError, ValueError) as error:
                errors.append(str(error))
        if matches != 1:
            raise ValueError(
                f"{path}: expected exactly one oneOf match, found {matches}; "
                + " | ".join(errors[:3])
            )
        return

    if "const" in schema and value != schema["const"]:
        raise ValueError(f"{path}: expected constant {schema['const']!r}, got {value!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise ValueError(f"{path}: {value!r} is not in {schema['enum']!r}")

    expected_types = schema.get("type")
    if expected_types:
        if isinstance(expected_types, str):
            expected_types = [expected_types]
        if not any(_matches_json_type(value, expected) for expected in expected_types):
            raise TypeError(f"{path}: expected {expected_types}, got {type(value).__name__}")

    if isinstance(value, dict):
        required = schema.get("required", [])
        missing = [name for name in required if name not in value]
        if missing:
            raise ValueError(f"{path}: missing required properties {missing}")
        declared = schema.get("properties", {})
        additional = schema.get("additionalProperties", True)
        for name, child in value.items():
            child_path = f"{path}.{name}"
            if name in declared:
                _validate_schema_subset(child, declared[name], root, child_path)
            elif additional is False:
                raise ValueError(f"{child_path}: additional property is not allowed")
            elif isinstance(additional, dict):
                _validate_schema_subset(child, additional, root, child_path)

    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            raise ValueError(f"{path}: array has fewer than minItems")
        if schema.get("uniqueItems"):
            serialized = [json.dumps(item, sort_keys=True) for item in value]
            if len(serialized) != len(set(serialized)):
                raise ValueError(f"{path}: array items are not unique")
        item_schema = schema.get("items")
        if item_schema:
            for index, child in enumerate(value):
                _validate_schema_subset(child, item_schema, root, f"{path}[{index}]")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            raise ValueError(f"{path}: value is below minimum {schema['minimum']}")


def validate_document(document: dict[str, Any], schema_path: Path) -> None:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    try:
        import jsonschema  # type: ignore[import-not-found]
    except ImportError:
        _validate_schema_subset(document, schema, schema)
    else:
        jsonschema.Draft202012Validator(schema).validate(document)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=DEFAULT_SOURCE_ROOT,
        help="Directory containing the .zil files (default: vendor/infocom-zork1)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory for generated scene JSON files (default: generated-scenes)",
    )
    parser.add_argument(
        "--room",
        action="append",
        dest="rooms",
        help="Extract only this room ID; repeat for multiple rooms",
    )
    parser.add_argument(
        "--schema",
        type=Path,
        default=DEFAULT_SCHEMA,
        help="JSON Schema used by --validate (default: schemas/scene-facts-v1.schema.json)",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Validate outputs against the JSON Schema",
    )
    parser.add_argument(
        "--list-rooms",
        action="store_true",
        help="List discovered room IDs without generating files",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    extractor = SceneExtractor(args.source_root.resolve())
    available = extractor.room_names()
    if args.list_rooms:
        print("\n".join(available))
        return 0

    selected = args.rooms or available
    unknown = sorted(set(selected) - set(available))
    if unknown:
        print("Unknown room ID(s): " + ", ".join(unknown), file=sys.stderr)
        return 2

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for room_id in selected:
        document = extractor.extract(room_id)
        if args.validate:
            validate_document(document, args.schema.resolve())
        output_path = args.output_dir / f"{room_id.lower()}.scene.json"
        output_path.write_text(
            json.dumps(document, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
