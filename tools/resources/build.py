#!/usr/bin/env python3
"""Validate/build explicitly declared WOH resources using the installed SDK.

No image generation. Runtime names are KRES opaque keys from resources.json.
Conversions are content-addressed by their inputs, settings and SDK encoder.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/scene-hotspots'))
from codec import encode, decode
from validate import validate_scene
from generate import arguments as hotspot_arguments


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def local(path, category=None):
    result = (ROOT / path).resolve()
    base = ROOT / category if category else ROOT
    if not result.is_relative_to(base.resolve()):
        raise ValueError('Path outside ' + str(base) + ': ' + path)
    return result


def kimg_header(path):
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError('Truncated KIMG: ' + str(path))
    magic, version, mode, flags, palette, w, h, n = struct.unpack_from('<4sBBBBHHI', data)
    if (magic != b'KIMG' or version != 1 or mode not in (0, 1, 2, 3) or flags or
            palette > 16 or not w or not h or (mode not in (0, 3) and palette) or
            (mode == 0 and w % 2) or (mode in (2, 3) and w % 4) or
            n != w * h // (2 if mode == 0 else 1) or len(data) != 16 + 3 * palette + n):
        raise ValueError('Invalid KIMG: ' + str(path))
    return mode, w, h


def declaration(manifest):
    """Validate selection before generating or staging anything."""
    if manifest.get('schema') != 'woh_resources_v1':
        raise ValueError('Unsupported resource manifest')
    names, paths, pairs = set(), set(), {'scene': {}, 'hotspots': {}}
    for row in manifest['resources']:
        name, kind = row['id'], row['type']
        if (not name or len(name.encode('ascii')) > 48 or '/' in name or '\\' in name or
                name in ('.', '..') or name in names):
            raise ValueError('Invalid/duplicate KRES name: ' + name)
        names.add(name)
        if kind not in ('story', 'scene', 'hotspots', 'ui'):
            raise ValueError('Unsupported resource type: ' + kind)
        path = local(row['path'], 'assets/runtime')
        suffix = '.z3' if kind == 'story' else '.wht' if kind == 'hotspots' else '.img'
        if path.suffix.lower() != suffix or path in paths:
            raise ValueError('Duplicate/unexpected runtime file: ' + row['path'])
        paths.add(path)
        local(row['source'], 'assets/source' if kind != 'story' else None)
        if not local(row['source']).is_file():
            raise ValueError('Missing source: ' + row['source'])
        if kind in pairs:
            pair = row['room'], row['state']
            if (not all(isinstance(n, int) and 0 < n < 65535 for n in pair) or pair in pairs[kind]):
                raise ValueError('Invalid/duplicate room-state mapping: ' + str(pair))
            pairs[kind][pair] = row
    if len([r for r in manifest['resources'] if r['type']=='ui' and r.get('role')=='title']) != 1:
        raise ValueError('Exactly one title resource is required')
    if len([r for r in manifest['resources'] if r['type'] == 'story']) != 1:
        raise ValueError('Exactly one story resource is required')
    if pairs['scene'].keys() != pairs['hotspots'].keys():
        raise ValueError('Scene/hotspot pair sets differ')
    authority = read(ROOT / 'assets/source/art-direction/manifest.json')
    known = {(r['runtime_room'], s['runtime_state']) for r in authority['rooms'] for s in r['states']}
    for pair, scene in pairs['scene'].items():
        hot = pairs['hotspots'][pair]
        key = 'room-%d-state-%d' % pair
        if (pair not in known or scene['id'] != key + '.img' or hot['id'] != key + '.wht' or
                scene['hotspots'] != hot['id'] or hot['image'] != scene['id']):
            raise ValueError('Scene/hotspot lookup association differs: ' + key)
        spec = read(local(scene['specification'], 'assets/source/art-direction'))
        if (spec['runtime_room'], spec['runtime_state']) != pair:
            raise ValueError('State specification differs: ' + key)
    pilot = read(ROOT / 'assets/source/white-house/manifest.json')
    expected = {(r['runtime_room'], r['runtime_state']) for r in pilot['records'] if r['binding'] == 'mapped'}
    selected = {r['asset_key']:r for r in pilot['records']}
    for row in pairs['scene'].values():
        record=selected.get(row['id'][:-4])
        if row.get('size') != [256,159] or row.get('mode') != 'yae' or row.get('yjk_quality') != 'optimized' or row.get('resample') != 'box':
            raise ValueError('Unsupported scene conversion recipe: '+row['id'])
        if record:
            approved=(ROOT/'assets/source/white-house'/record['original']).resolve()
            if local(row['source'])!=approved or row['crop_box']!=record['crop_box']:
                raise ValueError('Scene crop/source differs from hotspot coordinate reference: '+row['id'])
    if not expected <= pairs['scene'].keys():
        raise ValueError('Missing mapped pilot scene')
    for p in manifest['unbound_concepts']:
        if read(local(p))['state'] is not None:
            raise ValueError('Concept unexpectedly acquired a runtime state')
    extras = {p.resolve() for p in (ROOT / 'assets/runtime').rglob('*') if p.is_file()} - paths
    if extras:
        raise ValueError('Orphan/unexpected runtime files: ' + ', '.join(str(p.relative_to(ROOT)) for p in extras))
    return pairs


def build_rows(manifest, sdk, build, rebuild):
    pairs = declaration(manifest)
    fingerprint_file = build / 'resource-inputs.json'
    previous = read(fingerprint_file) if fingerprint_file.exists() else {}
    fingerprints, changed = {}, []
    encoder = sdk / 'tools/kimg.py'
    spec = importlib.util.spec_from_file_location('woh_kimg', encoder)
    kimg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(kimg)
    from PIL import Image
    args = hotspot_arguments([])
    for row in manifest['resources']:
        source, output = local(row['source']), local(row['path'])
        kind = row['type']
        dependencies = {'source': sha(source), 'recipe': row}
        if kind == 'scene':
            dependencies.update(encoder=sha(encoder), pillow=Image.__version__)
        fingerprint = hashlib.sha256(json.dumps(dependencies, sort_keys=True).encode()).hexdigest()
        if kind == 'hotspots':
            scene = read(source)
            errors, _ = validate_scene(scene, args)
            if errors:
                raise ValueError(row['id'] + ': ' + '; '.join(errors))
            if (scene['room'], scene['state']) != (row['room'], row['state']):
                raise ValueError('Hotspot binding differs')
            data = encode(scene)
            if not output.exists() or output.read_bytes() != data:
                write_changed(output, data); changed.append(row['id'])
        elif kind == 'story':
            binding = local(row['bindings']).read_bytes()
            data = source.read_bytes()
            if len(binding) != 70 or binding[:4] != b'WOH1' or binding[6:] != data[:64] or data[0] != 3:
                raise ValueError('Story/bindings mismatch')
            if not output.exists() or output.read_bytes() != data:
                write_changed(output, data); changed.append(row['id'])
        elif kind == 'scene' and (rebuild or not output.exists() or previous.get(row['id']) != {'input': fingerprint, 'output': sha(output)}):
            # Use the recorded approved crop, never the converter's default crop.
            with Image.open(source) as image:
                box = row['crop_box']
                if not (0 <= box[0] < box[2] <= image.width and 0 <= box[1] < box[3] <= image.height):
                    raise ValueError('Invalid approved crop')
                image = image.convert('RGB').crop(box).resize((256, 159), Image.Resampling.BOX)
                with tempfile.TemporaryDirectory() as temp:
                    png, result = Path(temp)/'crop.png', Path(temp)/'scene.img'
                    image.save(png)
                    kimg.convert(png, result, 'yae', (256, 159), resample='box', yjk_quality='optimized')
                    write_changed(output, result.read_bytes())
            changed.append(row['id'])
        if not output.is_file():
            raise ValueError('Missing runtime resource: ' + row['path'])
        if kind in ('scene', 'ui'):
            mode, w, h = kimg_header(output)
            if kind == 'ui' and row.get('role')=='title' and (mode,w,h)!=(2,256,212):
                raise ValueError('Title must retain full-screen YJK format')
            if kind == 'scene' and (mode, w, h) != (3, 256, 159):
                raise ValueError('Scene must be YJK+YAE 256x159: ' + row['id'])
        if kind == 'hotspots':
            decode(output.read_bytes())
        fingerprints[row['id']] = {'input': fingerprint, 'output': sha(output)}
    write_changed(fingerprint_file, (json.dumps(fingerprints, indent=2) + '\n').encode())
    return pairs, changed


def generated_header(manifest, pairs, path):
    rows = sorted(pairs['scene'].items())
    story = next(r['id'] for r in manifest['resources'] if r['type'] == 'story')
    title = next(r['id'] for r in manifest['resources'] if r.get('role') == 'title')
    bound = max(local(r['path']).stat().st_size for r in pairs['hotspots'].values())
    text = ('/* Generated from assets/manifests/resources.json; do not edit. */\n'
            '#ifndef WOH_RESOURCE_NAMES_H\n#define WOH_RESOURCE_NAMES_H\n'
            f'#define WOH_TITLE_RESOURCE "{title}"\n'
            f'#define WOH_STORY_RESOURCE "{story}"\n#define WOH_HOTSPOT_BYTES {bound}u\n'
            'typedef struct { uint16_t room, state; const char *image, *hotspots; } scene_key_t;\n'
            'static const scene_key_t scene_keys[] = {\n')
    for (room, state), row in rows:
        text += f'    {{{room}u,{state}u,"{row["id"]}","{row["hotspots"]}"}},\n'
    text += '};\n#define WOH_SCENE_COUNT %du\n#endif\n' % len(rows)
    write_changed(path, text.encode())


def pack(manifest, sdk, archive, build):
    tool = sdk / 'tools/kres'
    if not tool.is_file():
        raise ValueError('SDK tools/kres is missing; install the current SDK')
    # The packer walks its directory. This isolated staging dir contains only
    # the explicit manifest selection, with flat opaque names as filenames.
    with tempfile.TemporaryDirectory(dir=build) as temp:
        stage = Path(temp)/'stage'; stage.mkdir()
        for row in sorted(manifest['resources'], key=lambda r: r['id'].encode('ascii')):
            shutil.copyfile(local(row['path']), stage/row['id'])
        result = Path(temp)/'WOH.KPR'
        subprocess.run([str(tool), 'pack', str(stage), str(result)], check=True)
        subprocess.run([str(tool), 'verify', str(result)], check=True)
        listing = subprocess.check_output([str(tool), 'list', str(result)], text=True)
        # Check catalog selection and exact extracted payloads using the SDK.
        extracted = Path(temp)/'unpacked'
        subprocess.run([str(tool), 'unpack', str(result), str(extracted)], check=True)
        actual = {p.name for p in extracted.iterdir()}
        if actual != {r['id'] for r in manifest['resources']}:
            raise ValueError('KRES catalog differs from explicit selection')
        for row in manifest['resources']:
            if sha(extracted/row['id']) != sha(local(row['path'])):
                raise ValueError('KRES payload differs: ' + row['id'])
        write_changed(archive, result.read_bytes())
        write_changed(build/'resource-index.txt', listing.encode())
    return listing


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=ROOT.parent/'ZephyrOS/build/kos-sdk-0.2.0')
    p.add_argument('--manifest', type=Path, default=ROOT/'assets/manifests/resources.json')
    p.add_argument('--build', type=Path, default=ROOT/'app/build/assets')
    p.add_argument('--archive', type=Path, default=ROOT/'app/build/WestOfHouse/WOH.KPR')
    p.add_argument('--rebuild-images', action='store_true')
    args = p.parse_args()
    args.sdk = args.sdk.resolve(); args.build.mkdir(parents=True, exist_ok=True)
    manifest = read(args.manifest)
    pairs, changed = build_rows(manifest, args.sdk, args.build, args.rebuild_images)
    generated_header(manifest, pairs, args.build/'resource_names.h')
    pack(manifest, args.sdk, args.archive, args.build)
    report = {'schema': 'woh_resource_build_v1', 'resources': len(manifest['resources']),
              'mapped_scenes': len(pairs['scene']), 'unbound_packaged': 0,
              'archive_sha256': sha(args.archive), 'archive_bytes': args.archive.stat().st_size,
              'rebuilt': changed, 'verified_payloads': [r['id'] for r in manifest['resources']]}
    write_changed(args.build/'resource-validation.json', (json.dumps(report, indent=2)+'\n').encode())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit('resources: ' + str(error)) from None
