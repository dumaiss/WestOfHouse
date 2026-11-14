#!/usr/bin/env python3
"""Phase 1: transform scene facts and authoritative runtime IDs into art specs."""
import argparse
import copy
import json
from pathlib import Path
import re
import sys

import yaml
import jsonschema
from mapping import ROOT, Source, resolve, sha

HERE = Path(__file__).resolve().parent


def review_path(output, name):
    if output.resolve() == (ROOT/'assets/source/art-direction').resolve():
        return ROOT/'assets/review/art-direction'/name
    return output/name


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, ensure_ascii=False, indent=2) + '\n'
    if not path.exists() or path.read_text() != data:
        path.write_text(data)


def label(path):
    path = Path(path).resolve()
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def joined(strings):
    return ' '.join(' '.join(t.replace('|', ' ').split()) for t in strings).strip()


def objects_for(scene):
    """Select coarse source room dressing, without simulating object changes.

    Parser globals and concealed contents remain provenance. The existing
    runtime presentation takes precedence over this initial source dressing.
    These categories never participate in the image key or create new states.
    """
    result = []
    by_id = {o['id']: o for o in scene['objects']}
    for i, obj in enumerate(scene['objects']):
        flags = obj['source_flags']
        parent = by_id.get(obj['location'])
        if obj['kind'] != 'object':
            status, reason = 'parser_scope', 'Generic/global handler; visibility requires explicit environmental description.'
        elif parent and 'CONTBIT' in parent['source_flags'] and 'OPENBIT' not in parent['source_flags'] and 'TRANSBIT' not in parent['source_flags']:
            status, reason = 'concealed_initially', 'Initial containment is inside a closed opaque container; do not expose its contents.'
        elif obj['location'] != scene['id']:
            status, reason = 'contained_context', 'Contained object; not independently placed in the room illustration.'
        elif 'INVISIBLE' in flags:
            status, reason = 'hidden_initially', 'Initial source attribute marks this object invisible.'
        elif 'TAKEBIT' in flags or 'ACTORBIT' in flags:
            status, reason = 'scene_prop', 'Canonical room dressing from source placement; no inventory or pose simulation. Runtime presentation text takes precedence.'
        else:
            status, reason = 'fixed_feature', 'Direct room fixture; state presentation text takes precedence over initial attributes.'
        result.append({'id': obj['id'], 'description': obj.get('description'),
                       'kind': obj['kind'], 'initial_location': obj['location'],
                       'source_flags': flags, 'source_properties': obj['source_properties'],
                       'visibility': status, 'reason': reason,
                       'pointer': f'/scene/objects/{i}', 'source_refs': obj.get('source_refs', [])})
    return result


def presentation_for(direction, room, symbolic_state):
    """Editorial treatment for an existing state, never an additional key."""
    return direction.get('presentations', {}).get(room, {}).get(symbolic_state, {
        'visual_treatment': 'Depict the complete room presentation described above.',
        'omit_source_dressing': []})


def visible_dressing(objects, presentation):
    return [o for o in objects
            if o['visibility'] in ('fixed_feature', 'scene_prop')
            and o['id'] not in presentation['omit_source_dressing']]


def prompt_for(scene, resolved, direction, style, objects, continuity, presentation):
    canonical = joined([f['text'] for f in resolved['canonical']])
    fixed = visible_dressing(objects, presentation)
    fixtures = '; '.join(joined([o['source_properties'].get('LDESC') or o['source_properties'].get('FDESC') or o['description'] or o['id'].lower().replace('-', ' ')]).rstrip('.') for o in fixed)
    shared = ' '.join(g['artistic'] for g in continuity)
    parts = [f"Create one complete static illustration of {scene['title']}.",
        'Viewpoint: ' + direction['camera'],
        'The scene establishes these features: ' + canonical,
        'Composition: ' + direction['composition'],
        'Appearance of this illustration: ' + presentation['visual_treatment']]
    if fixtures:
        parts.append('Canonical room dressing: ' + fixtures + '. Preserve this coarse room presentation unless the state description above specifies a different appearance.')
    parts += ['Lighting and color: ' + direction['lighting_palette'],
        'Shared world design: ' + shared,
        'Style: ' + style['style'] + ' ' + style['palette'],
        'Keep this room camera, underlying architecture, terrain layout, light direction and palette consistent across its variants. Depict the complete scene, including the appearance stated above.',
        'Landscape source composition near 4:3. Final scene is 256 by 159 pixels. Allow safe center cropping to 256:159, keeping essential subjects inside the central 82.8 percent of source height and away from the side boundaries. Use clear silhouettes and broad readable shapes at that final size.',
        'Exclude game UI, borders, captions, labels, readable lettering, photographic detail, pixel grids and enlarged pixels. Do not add characters, significant props, openings or events beyond the established scene. Do not depict an action in progress or a before/after collage.']
    return '\n\n'.join(parts) + '\n'


def room_document(path, scene, direction, runtime, numeric, resolved):
    objects = objects_for(scene)
    unresolved = [{'kind':'runtime_unknown', 'detail':'The resolver returns state 0; no positive runtime state or image is assigned.'}] if not runtime['states'] else []
    return {'schema':'scene_art_v1','kind':'room','room':scene['id'],'title':scene['title'],
            'runtime_room':numeric, 'runtime_status':runtime['status'],
            'canonical':{'descriptions':scene['descriptions'],'objects':objects,
                         'exits':scene['exits'],'source_flags':scene['source_flags']},
            'inferred':[{'text':'Connected rooms share design language; adjacency alone does not establish a visible sightline.',
                         'basis':'Source exit destinations, preserved without adding routes.'}],
            'artistic':direction,
            'provenance':{'scene_file':label(path),'scene_sha256':sha(path),
                          'source_references':scene['source_references']},
            'supporting_context':{'state_variables':scene['state_variables'],
                'extracted_states':[{'id':s['id'],'condition':s['condition'],'stable':s.get('stable',False),
                                     'source_refs':s.get('source_refs',[])} for s in scene['states']],
                'transitions':[{'id':t['id'],'source_refs':t.get('source_refs',[])} for t in scene['transitions']],
                'notes':scene['notes']},
            'unresolved':unresolved}


def generate(args):
    direction = read(args.direction)
    runtime = read(args.mapping)
    dictionary = yaml.safe_load(args.dictionary.read_text())
    bindings = read(args.room_ids)
    if runtime['schema'] != 'woh_local_state_ids_v1' or runtime['unknown_value'] != 0:
        raise ValueError('Unsupported runtime mapping')
    if dictionary['schema'] != 'room_state_dictionary_v1':
        raise ValueError('Unsupported runtime dictionary')
    if sha(args.story) != bindings['story_sha256']:
        raise ValueError('Compiled numeric room IDs do not match the selected story')
    rooms = {r['room']:r for r in runtime['rooms']}
    if len(rooms) != len(runtime['rooms']):
        raise ValueError('Duplicate runtime room')
    definitions = {r['id']:r for r in dictionary['rooms']}
    scenes = {}
    source_schema=read(ROOT/'schemas/scene-facts-v1.schema.json')
    source_validator=jsonschema.validators.validator_for(source_schema)(source_schema)
    for path in sorted(args.input.glob('*.scene.json')):
        data = read(path)
        source_validator.validate(data)
        name = data['scene']['id']
        if not re.fullmatch(r'[A-Z0-9][A-Z0-9-]*', name):
            raise ValueError('Unsafe room identifier '+name)
        if name in scenes:
            raise ValueError('Duplicate input room '+name)
        scenes[name] = (path, data['scene'])
    selected = sorted(set(args.room) if args.room else scenes)
    if not set(selected) <= scenes.keys():
        raise ValueError('Requested room has no scene facts')
    source = Source(args.source)
    manifest_path = args.output/'manifest.json'
    manifest = read(manifest_path) if manifest_path.exists() else {'schema':'scene_art_v1','kind':'manifest','rooms':[]}
    records = {r['room']:r for r in manifest['rooms']}
    for name in selected:
        path, scene = scenes[name]
        if name not in rooms or name not in direction['rooms'] or name not in bindings['rooms']:
            raise ValueError('Missing authoritative mapping or model-authored direction for '+name)
        runtime_room = rooms[name]
        resolved = resolve(scene, runtime_room, definitions[name], source)
        treatments = direction.get('presentations', {}).get(name, {})
        state_ids = {item['symbolic_state'] for item in resolved}
        if not set(treatments) <= state_ids or (len(resolved)>1 and set(treatments)!=state_ids):
            raise ValueError('Artistic presentation IDs differ from runtime states for '+name)
        numeric = bindings['rooms'][name]
        document = room_document(path,scene,direction['rooms'][name],runtime_room,numeric,resolved)
        write(args.output/'rooms'/name/'room.json',document)
        group_ids = direction['rooms'][name]['continuity']
        groups = [g for g in direction['continuity']['groups'] if g['id'] in group_ids]
        records[name] = {'room':name,'runtime_room':numeric,'runtime_status':runtime_room['status'],
                         'specification':f'rooms/{name}/room.json','states':[],
                         'diagnostics':copy.deepcopy(document['unresolved'])}
        common = set.intersection(*(set(f['text'] for f in r['canonical']) for r in resolved)) if resolved else set()
        base = min((r['runtime_state'] for r in resolved),default=None)
        used_legacy = set()
        for item in resolved:
            number = item['runtime_state']
            used_legacy.update(s['id'] for s in item['extracted_states'])
            changed = [f['text'] for f in item['canonical'] if f['text'] not in common]
            presentation = presentation_for(direction,name,item['symbolic_state'])
            object_ids = {o['id'] for o in document['canonical']['objects']}
            if not set(presentation['omit_source_dressing']) <= object_ids:
                raise ValueError('Unknown source dressing omission for '+name)
            prompt = prompt_for(scene,item,direction['rooms'][name],direction['style'],document['canonical']['objects'],groups,presentation)
            reference = None if number == base else {'room':name,'runtime_room':numeric,'runtime_state':base,
                'asset_key':f'room-{numeric}-state-{base}',
                'status':'preferred future Phase 2 reference; no illustration exists yet'}
            state = {'schema':'scene_art_v1','kind':'state','room':name,'title':scene['title'],
                'runtime_room':numeric,'runtime_state':number,'symbolic_state':item['symbolic_state'],
                'condition':item['condition'],'extracted_states':item['extracted_states'],
                'description_ids':item['description_ids'],'canonical':item['canonical'],
                'required_environmental_features':[f['text'] for f in item['canonical']],
                'required_visible_objects':[o['id'] for o in visible_dressing(document['canonical']['objects'],presentation)],
                'artistic':direction['rooms'][name],
                'state_artistic':presentation,
                'invariants':{'canonical_text':sorted(common),'artistic':direction['rooms'][name]},
                'changes':changed,
                'reference':reference,'asset_key':f'room-{numeric}-state-{number}',
                'target':direction['style']['target'],
                'prompt':prompt,'negative_prompt':'No UI, borders, readable text, photorealism, pixel grid, invented props, extra exits or transient action events.',
                'provenance':{'scene_file':label(path),'scene_sha256':sha(path),
                              'runtime_source':item['runtime_source'],
                              'mapping_file':label(args.mapping),'mapping_sha256':sha(args.mapping),
                              'dictionary_file':label(args.dictionary),'dictionary_sha256':sha(args.dictionary),
                              'story_sha256':bindings['story_sha256']},
                'unresolved':copy.deepcopy(document['unresolved'])}
            if not item['extracted_states']:
                issue={'kind':'legacy_state_relationship_unresolved','state':item['symbolic_state'],
                       'detail':'Runtime ID and presentation text are verified; no equivalent legacy state condition was proven. Source descriptions remain traced directly.'}
                state['unresolved'].append(issue); records[name]['diagnostics'].append(issue)
            if len(resolved)>1 and not changed:
                state['unresolved'].append({'kind':'no_visible_text_delta','detail':'Keep the separate runtime state and asset; predicate difference may be audible or otherwise nonvisual.'})
            write(args.output/'rooms'/name/'states'/f'{number}.json',state)
            prompt_path=args.output/'rooms'/name/'prompts'/f'{number}.md'
            prompt_path.parent.mkdir(parents=True,exist_ok=True)
            if not prompt_path.exists() or prompt_path.read_text()!=prompt:
                prompt_path.write_text(prompt)
            records[name]['states'].append({'runtime_state':number,'symbolic_state':item['symbolic_state'],
                'asset_key':state['asset_key'],'specification':f'rooms/{name}/states/{number}.json',
                'prompt':f'rooms/{name}/prompts/{number}.md'})
        records[name]['unmapped_extracted_states']=[{'id':s['id'],'condition':s['condition'],
            'reason':'Supporting action/transition context, not an image state.' if s['id'].startswith('ACTION-')
                     else 'No authoritative equivalent legacy relationship established; no additional asset assigned.'}
            for s in scene['states'] if s['id'] not in used_legacy]
        # Only remove stale artifacts owned by this generator in selected rooms.
        for directory,suffix in [('states','json'),('prompts','md')]:
            wanted={f"{r['runtime_state']}.{suffix}" for r in resolved}
            folder=args.output/'rooms'/name/directory
            for old in folder.glob('*.'+suffix):
                if old.name not in wanted:
                    old.unlink()
    write(args.output/'world/style-guide.json',direction['style'])
    continuity=copy.deepcopy(direction['continuity'])
    for group in continuity['groups']:
        group['provenance']=[{'scene_file':label(scenes[name][0]),'scene_sha256':sha(scenes[name][0])}
                             for name in group['rooms'] if name in scenes]
    write(args.output/'world/continuity.json',continuity)
    manifest.update({'target':direction['style']['target'],'story_sha256':bindings['story_sha256'],
                     'rooms':[records[n] for n in sorted(records)]})
    write(manifest_path,manifest)
    from validate_art import validate_art
    report=validate_art(args.output,args)
    write(review_path(args.output,'validation.json'),report)
    lines=['# Phase 1 review','',f"Rooms: {report['rooms']}. Runtime states / prompts: {report['states']}. Unresolved runtime rooms: {report['runtime_unknown_rooms']}.",'',
           '## Validation','',f"Schema and semantic checks: {'PASS' if not report['errors'] else 'FAIL'}.",'',
           '## Decisions and limitations','',
           '- Target is 256×159, following the latest user clarification. Source compositions use 4:3 with safe top/bottom crop margins.',
           '- Creative direction is stored explicitly in tools/scene-art/direction.json. It was authored through model interpretation of the collection and is not inferred by keyword templates at regeneration time.',
           '- State 0 is an unsupported recognition result, not a new art state. No image is assigned to it.',
           '- The existing (room, state) pair is the only illustration key. Props are coarse canonical room dressing; inventory, poses and micro-details are not separately tracked or turned into extra art states.',
           '- Canonical inscriptions remain in provenance, while prompts request blank/unreadable lettering to satisfy the no-text illustration brief.',
           '- Prior composition_enrichment_v1 and asset_prompt_bundle_v1 files are historical inputs only; their pixel-grid style and render flags are not authorities for this phase.',
           '- Each mapped pair has a separate full-scene prompt and asset key. The first numeric state is a preferred future reference, not a claim that it is the initial or default state.',
           '- No images, conversion, audio, hotspots or runtime integration are performed.','', '## Room diagnostics','']
    for record in manifest['rooms']:
        if record['diagnostics']:
            lines.append(f"### {record['room']}")
            for issue in record['diagnostics']:
                lines.append('- '+issue['kind']+': '+issue.get('detail',''))
            lines.append('')
    if report['errors']:
        lines+=['## Errors','']+['- '+e for e in report['errors']]
    (review_path(args.output,'review.md')).write_text('\n'.join(lines)+'\n')
    if report['errors']:
        raise ValueError('Art validation failed: '+'; '.join(report['errors'][:3]))
    print(json.dumps({k:v for k,v in report.items() if k!='errors'},indent=2))


def arguments(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=ROOT/'assets/source/scene-facts')
    parser.add_argument('--output',type=Path,default=ROOT/'assets/source/art-direction')
    parser.add_argument('--mapping',type=Path,default=ROOT/'generated-zil/woh-state-ids.json')
    parser.add_argument('--dictionary',type=Path,default=ROOT/'generated-room-states/room-states.yaml')
    parser.add_argument('--room-ids',type=Path,default=HERE/'runtime-room-ids.json')
    parser.add_argument('--story',type=Path,default=ROOT/'libzvm/build/ZORK1.Z3')
    parser.add_argument('--source',type=Path,default=ROOT/'vendor/infocom-zork1')
    parser.add_argument('--direction',type=Path,default=HERE/'direction.json')
    parser.add_argument('--room',action='append',default=[])
    return parser.parse_args(argv)


if __name__=='__main__':
    try:
        generate(arguments())
    except (ValueError,KeyError,OSError,jsonschema.ValidationError) as error:
        raise SystemExit('scene-art: '+str(error)) from None
