"""Read-only access to the asset extractor's existing ZIL grammar.

No SceneExtractor instance or scene-facts document is constructed here.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
from pathlib import Path
import sys

PARSER_PATH = Path(__file__).resolve().parents[1] / 'scene-extractor' / 'extract_scenes.py'
_spec = importlib.util.spec_from_file_location('_room_states_zil_parser', PARSER_PATH)
zil = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = zil
_spec.loader.exec_module(zil)


@dataclass
class TextSource:
    text: str
    name: str = '<diagnostic-expression>'

    def read_text(self, **kwargs):
        return self.text


def expression(text):
    """Parse a diagnostic expression with exactly the shared parser grammar."""
    if text == 'T':
        return zil.Atom('T', '<expression>', 1, 1)
    forms = zil.parse_file(TextSource(text))
    if len(forms) != 1:
        raise ValueError('Expected one ZIL form')
    return forms[0]


class Source:
    def __init__(self, root):
        paths = sorted(Path(root).glob('*.zil'))
        self.source_hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        self.forms = [form for path in paths
                      for form in zil.parse_file(path)]
        self.index = zil.definition_index(self.forms)
        self.objects = {**self.index['ROOM'], **self.index['OBJECT']}

    def property_values(self, definition, name):
        return zil.values_after_head(zil.first_property(definition, name))

    def location(self, definition):
        values = self.property_values(definition, 'IN')
        return zil.symbol(values[0]) if values else None

    def room_objects(self, room):
        included = set()
        while True:
            found = {name for name, obj in self.index['OBJECT'].items()
                     if self.location(obj) in included | {room}}
            if found <= included:
                break
            included |= found
        return sorted(included)

    def handlers(self, room):
        definition = self.index['ROOM'][room]
        names = self.room_objects(room)
        names += [zil.symbol(v) for v in self.property_values(definition, 'GLOBAL')]
        result = set()
        for obj in [definition] + [self.objects[n] for n in names if n in self.objects]:
            for prop in ('ACTION', 'PSEUDO'):
                for value in self.property_values(obj, prop):
                    if zil.symbol(value) in self.index['ROUTINE']:
                        result.add(zil.symbol(value))
        return sorted(result)


def provenance(value):
    return {'file': value.file, 'line': value.line_start, 'expression': zil.render(value)}
