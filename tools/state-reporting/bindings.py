"""Read ZILf's emitted ZAP declarations; produce story-specific host bindings.

.GVAR declarations allocate variables 16..255 in declaration order. The public
libzvm API uses zero-based indices, so no global number is guessed here. Only
HERE and WOH-STATE are shipped to the target; other symbols aid build-time tests.
"""
import hashlib
import json
from pathlib import Path
import re

MAGIC = b'WOH1'
METADATA_SIZE = 70


def compiled_symbols(data_path):
    text = Path(data_path).read_text()
    names = re.findall(r'^\s*\.GVAR\s+([^=\s]+)=', text, re.M)
    objects = re.findall(r'^\s*\.OBJECT\s+([^,\s]+),', text, re.M)
    if len(names) != len(set(names)) or len(names) > 240:
        raise ValueError('Invalid/duplicate compiled global declarations')
    constants = dict((name, int(value)) for name, value in re.findall(
        r'^\s*([A-Z][A-Z0-9?\-]*)=(-?\d+)\s*$', text, re.M))
    return {'globals': {name: index for index, name in enumerate(names)},
            'objects': {name: index for index, name in enumerate(objects, 1)},
            'constants': constants}


def write_bindings(story_path, data_path, output_path):
    story = Path(story_path).read_bytes()
    symbols = compiled_symbols(data_path)
    if len(story) < 64 or story[0] != 3:
        raise ValueError('Bindings require a V3 story')
    globals_base = int.from_bytes(story[12:14], 'big')
    indices = [symbols['globals'][name] for name in ('HERE', 'WOH-STATE')]
    if indices[0] == indices[1] or globals_base + 2 * (max(indices) + 1) > len(story):
        raise ValueError('Invalid compiled reporting globals')
    payload = MAGIC + bytes(indices) + story[:64]
    assert len(payload) == METADATA_SIZE
    Path(output_path).write_bytes(payload)
    manifest = {'schema': 'woh_story_bindings_v1',
                'story_sha256': hashlib.sha256(story).hexdigest(),
                'metadata_sha256': hashlib.sha256(payload).hexdigest(),
                'globals': dict(zip(('HERE', 'WOH-STATE'), indices)),
                'source': 'ZILf ZAP .GVAR declaration order; zero-based indices'}
    Path(output_path).with_suffix('.bindings.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest
