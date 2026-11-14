"""Preserve a built-in imagegen result and its request provenance for this pilot."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2] / 'assets/source/white-house'


def record(key, source, tool_metadata):
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    item = next(r for r in manifest['records'] if r['asset_key'] == key)
    stem = key
    target = ROOT / 'originals' / (stem + '.png')
    # User policy: retain only the selected generation under canonical names.
    references = [{'asset_key': key, 'path': ref['original'],
                   'sha256': hashlib.sha256((ROOT / ref['original']).read_bytes()).hexdigest(),
                   'retained': True}
                  for key in item['reference_assets']
                  for ref in manifest['records'] if ref['asset_key'] == key]
    shutil.copy2(source, target)
    from PIL import Image
    with Image.open(target) as image:
        size = list(image.size)
    metadata = {
        'tool': 'built-in image_gen.imagegen',
        'recorded_at_utc': datetime.now(timezone.utc).isoformat(),
        'original_tool_path': source,
        'tool_result': tool_metadata,
        'request': item['request'],
        'request_sha256': hashlib.sha256((ROOT / item['request']).read_bytes()).hexdigest(),
        'references': references,
        'source_size': size,
        'source_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'model_and_seed': 'Not exposed by the built-in tool.',
    }
    (ROOT / 'metadata' / (stem + '.json')).write_text(json.dumps(metadata, indent=2) + '\n')
    item.pop('previous_candidates', None)
    item.update(status='generated', original='originals/' + stem + '.png',
                metadata='metadata/' + stem + '.json', source_size=size)
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # References to a replaced image remain truthful hashes, not misleading
    # links to the new image that now occupies the same canonical filename.
    for path in (ROOT / 'metadata').glob('room-*.json'):
        existing = json.loads(path.read_text())
        for ref in existing['references']:
            if ref.get('path') and hashlib.sha256((ROOT / ref['path']).read_bytes()).hexdigest() != ref['sha256']:
                ref.update(path=None, retained=False,
                           note='Superseded reference removed; generation hash retained for provenance.')
        path.write_text(json.dumps(existing, indent=2) + '\n')
    print(key, size, target)


if __name__ == '__main__':
    record(**json.loads(sys.argv[1]))
