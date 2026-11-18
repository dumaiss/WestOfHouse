#!/usr/bin/env python3
"""Build the complete White House review set without touching the game bundle.

Selected originals and visual observations are authored inputs. This command
does no AI generation, changes no runtime IDs and writes only its own review
directory. It reuses the existing KIMG converter, ZIL parser and WHT1 codec.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

import jsonschema
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/source/white-house-completion'
REVIEW = ROOT / 'assets/review/white-house-completion'
sys.path.insert(0, str(ROOT / 'tools/scene-hotspots'))
from codec import decode, encode, hit
from generate import Source, candidates, syntax


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def relative(path):
    return path.resolve().relative_to(ROOT).as_posix()


def sheet(records, folder, destination, annotated=False):
    width, height = (512, 430) if annotated else (256, 183)
    canvas = Image.new('RGB', (width * 3, height * ((len(records) + 2) // 3)), '#202328')
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12)
    for index, record in enumerate(records):
        x, y = (index % 3) * width, (index // 3) * height
        with Image.open(folder / (record['asset_key'] + '.png')) as image:
            if annotated:
                image = image.resize((512, image.height // 2), Image.Resampling.BOX)
                canvas.paste(image, (x, y))
            else:
                draw.text((x + 4, y + 3), record['asset_key'], font=font, fill='white')
                canvas.paste(image, (x, y + 22))
    canvas.save(destination)


def annotate(scene, image_path, destination):
    """Draw native-pixel hit regions and enlarge by an exact integer factor."""
    with Image.open(image_path) as original:
        image = original.convert('RGB')
    draw = ImageDraw.Draw(image)
    colors = ['#ffec33', '#ff5555', '#38eeff', '#ff84ff', '#66ff66', '#ffa044', '#aaccff', '#ffffff']
    for index, spot in enumerate(scene['hotspots']):
        x, y, w, h = spot['rect']
        color = colors[index % len(colors)]
        draw.rectangle((x, y, x + w - 1, y + h - 1), outline=color, width=1)
        draw.text((x + 1, y), str(index + 1), fill=color)
    # BOX at integer 4x replicates native pixels without interpolating edges.
    # This also avoids a NEAREST-upscale failure in the installed Pillow build.
    enlarged = image.resize((1024, 636), Image.Resampling.BOX)
    canvas = Image.new('RGB', (1024, 684 + 32 * ((len(scene['hotspots']) + 1) // 2)), '#202328')
    canvas.paste(enlarged, (0, 0))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 22)
    draw.text((8, 640), scene['asset_key'], font=font, fill='white')
    for index, spot in enumerate(scene['hotspots']):
        draw.text((8 + 512 * (index % 2), 678 + 32 * (index // 2)),
                  f"{index + 1}. {spot['label']} ({spot['object_id']})",
                  font=font, fill=colors[index % len(colors)])
    canvas.save(destination)


def hotspot_scene(record, observed, source, observation_path):
    key = record['asset_key']
    image = REVIEW / 'rgb' / (key + '.png')
    facts_path = ROOT / 'assets/source/scene-facts' / (record['room'].lower() + '.scene.json')
    facts = read(facts_path)['scene']
    known = candidates(facts)
    scene = {
        'schema': 'scene_hotspots_v1', 'room': record['runtime_room'],
        'state': record['runtime_state'], 'room_name': record['room'],
        'binding': 'mapped', 'asset_key': key, 'image_size': [256, 159],
        'image': {'path': relative(image), 'sha256': sha(image),
                  'original_path': record['original'],
                  'original_sha256': record['original_sha256'],
                  'crop_box': record['crop_box'],
                  'coordinate_reference': 'final RGB preview; no additional scaling transform'},
        'hotspots': [], 'unresolved': observed['unresolved'],
        'localization': {'method': 'Assistant visual inspection of final RGB crops',
                         'observations': relative(observation_path),
                         'observations_sha256': sha(observation_path)},
        'provenance': {'scene_file': relative(facts_path), 'scene_sha256': sha(facts_path),
                       'zil_source_sha256': source.source_hashes,
                       'phase1_specification': record['phase1_specification'],
                       'pilot_manifest': relative(SOURCE / 'manifest.json')},
        'overlap_policy': 'smallest matching rectangle; authored order breaks equal-area ties; half-open edges',
    }
    if sha(image) != observed['image_sha256']:
        raise ValueError('Crop changed: visually review observations again for ' + key)
    for ordinal, observation in enumerate(observed['hotspots'], 1):
        spot = copy.deepcopy(observation)
        candidate = known[spot['object_id']]
        spot['id'] = 'h' + str(ordinal)
        spot['provenance'] = {'pointer': candidate['pointer'],
                              'source_refs': candidate.get('source_refs', [])}
        # Menus are explicitly reviewed observations, grounded in actual source
        # candidates and parser syntax, not guessed from an object's name.
        for action in spot['actions']:
            verb = action['command'].split()[0]
            words = action['command'].upper().split()
            directions = {d.upper() for exit in facts['exits'] for d in exit['directions']}
            if candidate['kind'] == 'exit':
                if verb not in candidate['directions'] and not (verb == 'southwest' and 'sw' in candidate['directions']):
                    raise ValueError('Command does not name the source exit: ' + action['command'])
            elif not (set(words) & set(candidate['synonyms'])) and not (len(words) == 1 and words[0] in directions):
                raise ValueError('Command lacks a source synonym or room direction: ' + action['command'])
            action.update(source_refs=candidate.get('source_refs', []),
                          parser_source=syntax(source, verb),
                          authority='Suggestion only; the original parser validates and executes it.')
        scene['hotspots'].append(spot)
    for issue in scene['unresolved']:
        if issue['object_id'] is not None and issue['object_id'] not in known:
            raise ValueError('Unknown excluded object: ' + str(issue))
    return scene


def build():
    manifest = read(SOURCE / 'manifest.json')
    observations_path = SOURCE / 'observations.json'
    observations = read(observations_path)
    mapping_path = ROOT / manifest['runtime_mapping']
    if sha(mapping_path) != manifest['runtime_mapping_sha256']:
        raise ValueError('Runtime state mapping changed; review the selected scene bindings')
    mapping = {r['room']: r for r in read(mapping_path)['rooms']}
    records = manifest['records']
    keys = [r['asset_key'] for r in records]
    if len(keys) != len(set(keys)) or set(keys) != set(observations['scenes']):
        raise ValueError('Selected scene and observation sets differ')
    for folder in ('rgb', 'yae', 'kimg-yae', 'hotspots/scenes', 'hotspots/binary', 'hotspots/annotated'):
        (REVIEW / folder).mkdir(parents=True, exist_ok=True)
    encoder_path = ROOT.parent / 'ZephyrOS/tools/kimg.py'
    spec = importlib.util.spec_from_file_location('completion_kimg', encoder_path)
    encoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(encoder)
    cache_path = REVIEW / 'conversion.json'
    previous = read(cache_path) if cache_path.exists() else {}
    cached = {r['asset_key']: r for r in previous.get('records', [])}
    source = Source(ROOT / 'vendor/infocom-zork1')
    schema = read(ROOT / 'schemas/scene-hotspots-v1.schema.json')
    results = []
    for record in records:
        key = record['asset_key']
        room = mapping[record['room']]
        if room['status'] != 'ready' or record['runtime_state'] not in [s['value'] for s in room['states']]:
            raise ValueError('Unassigned room/state: ' + key)
        state_spec = read(ROOT / record['phase1_specification'])
        if (state_spec['runtime_room'], state_spec['runtime_state']) != (record['runtime_room'], record['runtime_state']):
            raise ValueError('Phase 1 room/state binding differs: ' + key)
        original = ROOT / record['original']
        if sha(original) != record['original_sha256']:
            raise ValueError('Selected original changed: ' + key)
        if record.get('generation_prompt') and sha(ROOT / record['generation_prompt']) != record['generation_prompt_sha256']:
            raise ValueError('Saved generation prompt changed: ' + key)
        rgb, native, preview = [REVIEW / folder / (key + suffix)
                                for folder, suffix in [('rgb', '.png'), ('kimg-yae', '.img'), ('yae', '.png')]]
        with Image.open(original) as image:
            crop = record['crop_box']
            if not (0 <= crop[0] < crop[2] <= image.width and 0 <= crop[1] < crop[3] <= image.height):
                raise ValueError('Invalid crop: ' + key)
            image.convert('RGB').crop(crop).resize((256, 159), Image.Resampling.BOX).save(rgb)
        fingerprint = {'rgb_sha256': sha(rgb), 'encoder_sha256': sha(encoder_path),
                       'mode': 'yae', 'quality': 'optimized'}
        old = cached.get(key, {})
        if old.get('inputs') != fingerprint or not native.exists() or old.get('kimg_sha256') != sha(native):
            encoder.convert(rgb, native, 'yae', (256, 159), resample='box', yjk_quality='optimized')
        encoder.preview(native, preview)
        scene = hotspot_scene(record, observations['scenes'][key], source, observations_path)
        jsonschema.validate(scene, schema)
        payload = encode(scene)
        decoded = decode(payload)
        if (decoded['room'], decoded['state'], decoded['image_sha256']) != (scene['room'], scene['state'], sha(rgb)):
            raise ValueError('Binary identity differs: ' + key)
        # Decode validates bounds/string limits. Check that a real object region
        # wins at every pixel, including overlaps, using the shared consumer rule.
        for y in range(159):
            for x in range(256):
                containing = [(s['rect'][2] * s['rect'][3], i, s) for i, s in enumerate(scene['hotspots'])
                              if s['rect'][0] <= x < s['rect'][0] + s['rect'][2]
                              and s['rect'][1] <= y < s['rect'][1] + s['rect'][3]]
                expected = min(containing, default=None, key=lambda row: row[:2])
                actual = hit(decoded, x, y)
                if (actual['id'] if actual else None) != (expected[1] + 1 if expected else None):
                    raise ValueError('Overlap precedence differs: ' + key)
        json_path = REVIEW / 'hotspots/scenes' / (key + '.json')
        binary_path = REVIEW / 'hotspots/binary' / (key + '.wht')
        write(json_path, scene)
        binary_path.write_bytes(payload)
        annotate(scene, rgb, REVIEW / 'hotspots/annotated' / (key + '.png'))
        result = {'asset_key': key, 'room': scene['room'], 'state': scene['state'],
                  'inputs': fingerprint, 'kimg': relative(native), 'kimg_sha256': sha(native),
                  'rgb': relative(rgb), 'yae_preview': relative(preview),
                  'hotspot_json': relative(json_path), 'hotspot_binary': relative(binary_path),
                  'hotspot_bytes': len(payload), 'hotspots': len(scene['hotspots']),
                  'unresolved': scene['unresolved']}
        results.append(result)
        print(key, len(scene['hotspots']), 'hotspots,', len(payload), 'WHT1 bytes', flush=True)
    write(cache_path, {'schema': 'woh_completion_conversion_v1', 'records': results})
    write(REVIEW / 'hotspots/manifest.json', {'schema': 'woh_completion_hotspots_v1', 'scenes': results})
    sheet(records, REVIEW / 'rgb', REVIEW / 'contact-sheet-rgb.png')
    sheet(records, REVIEW / 'yae', REVIEW / 'contact-sheet-yae.png')
    sheet(records, REVIEW / 'hotspots/annotated', REVIEW / 'contact-sheet-hotspots.png', annotated=True)
    write(REVIEW / 'validation.json', {'scenes': len(records), 'hotspots': sum(r['hotspots'] for r in results),
                                     'runtime_pairs': len(set((r['room'], r['state']) for r in results)),
                                     'binary_roundtrips': len(results), 'overlap_pixels_checked': len(results) * 256 * 159,
                                     'archive_modified': False, 'application_modified': False, 'errors': []})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    build()
