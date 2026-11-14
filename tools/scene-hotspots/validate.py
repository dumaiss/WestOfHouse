#!/usr/bin/env python3
"""Schema, source, image-binding, geometry and binary checks for the pilot."""
import json
from pathlib import Path

import jsonschema
from PIL import Image

from codec import decode, encode
from generate import ROOT, Source, actions_for, arguments, candidates, read, sha


def at(data, pointer):
    for token in pointer.strip('/').split('/'):
        data=data[int(token)] if isinstance(data,list) else data[token]
    return data


def overlap(a,b):
    x,y,w,h=a;X,Y,W,H=b
    return max(0,min(x+w,X+W)-max(x,X))*max(0,min(y+h,Y+H)-max(y,Y))


def validate_scene(scene,args):
    try:
        return _validate_scene(scene,args)
    except (OSError,KeyError,IndexError,ValueError) as error:
        return ['Missing or invalid input/provenance: '+str(error)],[]


def _validate_scene(scene,args):
    errors=[];warnings=[]
    schema=read(ROOT/'schemas/scene-hotspots-v1.schema.json')
    errors += ['schema: '+e.message for e in jsonschema.Draft7Validator(schema).iter_errors(scene)]
    if errors:
        return errors,warnings
    record=next((r for r in read(args.pilot/'manifest.json')['records']
                 if r['asset_key']==scene['asset_key']),None)
    if record is None:
        return ['No corresponding pilot image/state'],warnings
    if (scene['room'],scene['state'],scene['room_name'],scene['binding']) != (
            record['runtime_room'],record['runtime_state'],record['room'],record['binding']):
        errors.append('Room/state binding differs from pilot')
    phase1=read(ROOT/'assets/source/art-direction/manifest.json')
    authority=next(r for r in phase1['rooms'] if r['room']==record['room'])
    if scene['state'] is None:
        if scene['binding']!='unbound-concept' or authority['states']:
            errors.append('Unbound concept disagrees with Phase 1')
    elif not any(s['runtime_state']==scene['state'] for s in authority['states']):
        errors.append('Invented runtime state')
    if scene['room']!=authority['runtime_room']:
        errors.append('Runtime room number differs')
    expected_image=args.pilot/record['rgb_preview']
    image=ROOT/scene['image']['path']
    if image.resolve()!=expected_image.resolve() or sha(image)!=scene['image']['sha256']:
        errors.append('Image path/hash differs from selected final preview')
    original=ROOT/scene['image']['original_path']
    if original.resolve()!=(args.pilot/record['original']).resolve() or sha(original)!=scene['image']['original_sha256']:
        errors.append('Selected original image differs')
    with Image.open(image) as preview:
        if preview.size!=(256,159):errors.append('Image dimensions differ')
    if scene['image']['crop_box']!=record['crop_box']:
        errors.append('Crop binding differs')
    original_facts=read(ROOT/scene['provenance']['scene_file']);facts=original_facts['scene']
    if facts['id']!=record['room'] or sha(ROOT/scene['provenance']['scene_file'])!=scene['provenance']['scene_sha256']:
        errors.append('Source facts association/hash differs')
    known=candidates(facts);refs={r['id'] for r in facts['source_references']}
    observations_path=ROOT/scene['localization']['observations']
    if sha(observations_path)!=scene['localization']['observations_sha256']:
        errors.append('Vision observations changed')
    observation=read(observations_path)['scenes'][scene['asset_key']]
    if observation['image_sha256']!=scene['image']['sha256']:
        errors.append('Vision observations describe a different image')
    if scene['unresolved']!=observation['unresolved']:
        errors.append('Unresolved visual observations changed')
    if len(scene['hotspots'])!=len(observation['hotspots']):
        errors.append('Missing vision localization records')
    source=getattr(args,'_source',None)
    if source is None:
        source=args._source=Source(args.source)
    if scene['provenance']['zil_source_sha256']!={p.name:sha(p) for p in args.source.glob('*.zil')}:
        errors.append('ZIL source hashes changed')
    seen=set();localized=set()
    for spot in scene['hotspots']:
        ident=spot['id'];name=spot['object_id'];localized.add(name)
        if ident in seen:errors.append('Duplicate hotspot ID '+ident)
        seen.add(ident)
        x,y,w,h=spot['rect']
        if not w or not h or x+w>256 or y+h>159:errors.append('Out-of-bounds/empty rectangle '+ident)
        if min(w,h)<10:warnings.append({'kind':'small_hit_area','object_id':name,'detail':ident+' is smaller than 10 pixels on one axis.'})
        candidate=known.get(name)
        if candidate is None:
            errors.append('Unknown game object/exit '+name);continue
        try:
            pointed=at(original_facts,spot['provenance']['pointer'])
        except (KeyError,IndexError,TypeError,ValueError):
            pointed={}
        if (spot['provenance']['pointer']!=candidate['pointer']
                or spot['provenance']['source_refs']!=candidate.get('source_refs',[])
                or not isinstance(pointed,dict) or pointed.get('id')!=name
                or not set(spot['provenance']['source_refs'])<=refs):
            errors.append('Broken source pointer/reference '+ident)
        parent=known.get(candidate.get('location'))
        if parent and 'CONTBIT' in parent.get('source_flags',[]) and not {'OPENBIT','TRANSBIT'} & set(parent['source_flags']):
            errors.append('Target inside a source closed opaque container '+name)
        observed=next((s for s in observation['hotspots'] if s['id']==ident),None)
        if observed is None or any(spot[k]!=observed[k] for k in ('object_id','label','rect','confidence','basis','background')):
            errors.append('Localization differs from reviewed vision observation '+ident)
        commands=[a['command'] for a in spot['actions']]
        if len(commands)!=len(set(commands)):errors.append('Duplicate menu command '+ident)
        if spot['actions']!=actions_for(candidate,facts,scene['state'],source):
            errors.append('Commands/source provenance differ from source-backed menu policy '+ident)
        if spot['confidence']!='high':warnings.append({'kind':'visual_uncertainty','object_id':name,'detail':spot['basis']})
        if w*h>256*159*0.85:warnings.append({'kind':'very_large_rectangle','object_id':name,'detail':'Covers over 85% of scene.'})
    for i,a in enumerate(scene['hotspots']):
        for b in scene['hotspots'][i+1:]:
            area_a=a['rect'][2]*a['rect'][3];area_b=b['rect'][2]*b['rect'][3]
            intersection=overlap(a['rect'],b['rect'])
            if intersection and min(area_a,area_b)>0.10*256*159 and intersection/min(area_a,area_b)>0.35:
                warnings.append({'kind':'large_overlap','object_id':a['object_id'],
                                 'detail':a['id']+' overlaps '+b['id']+'; review smallest-area precedence.'})
    unresolved={u['object_id'] for u in scene['unresolved']}
    for obj in facts['objects']:
        if obj['kind']!='object' or 'INVISIBLE' in obj['source_flags']:continue
        parent=known.get(obj['location'])
        expected=obj['location']==facts['id'] or (parent and parent.get('location')==facts['id'] and 'SURFACEBIT' in parent.get('source_flags',[]))
        if expected and obj['id'] not in localized:
            warnings.append({'kind':'missing_expected_object','object_id':obj['id'],
                             'detail':'Source room dressing not localized.'+(' Recorded explicitly as unresolved.' if obj['id'] in unresolved else ' Needs a visibility decision.')})
    for issue in scene['unresolved']:
        name=issue['object_id']
        if name is not None and name not in known:errors.append('Unknown unresolved object/exit '+name)
        if issue['status'] in ('not_visible','uncertain','unsupported_art_feature'):
            warnings.append({'kind':issue['status'],'object_id':name,'detail':issue['detail']})
    if scene['state'] is None:warnings.append({'kind':'unbound_concept','object_id':None,'detail':'No runtime state ID; must not be selected for state 0 or any positive state.'})
    return errors,warnings


def validate_collection(args):
    index=read(args.output/'manifest.json');report={'scenes':len(index['scenes']),'errors':[],'warnings':[]}
    expected={r['asset_key'] for r in read(args.pilot/'manifest.json')['records']}
    if len(index['scenes'])!=len(expected) or {r['asset_key'] for r in index['scenes']}!=expected:
        report['errors'].append('Pilot scene set differs or duplicates exist')
    # Source/review/runtime directories intentionally differ. Restrict every
    # index path to the repository (or the explicit scratch output for tests).
    for row in index['scenes']:
        paths=[(args.output/row[k]).resolve() for k in ('manifest','binary','preview')]
        if any(not (p.is_relative_to(ROOT) or p.is_relative_to(args.output.resolve())) for p in paths):
            report['errors'].append('Unsafe artifact path '+row['asset_key']);continue
        scene=read(args.output/row['manifest']);errors,warnings=validate_scene(scene,args)
        try:
            data=(args.output/row['binary']).read_bytes()
            decode(data)
            if data!=encode(scene):errors.append('Binary differs from manifest')
        except (OSError,ValueError) as error:errors.append('Binary: '+str(error))
        if not (args.output/row['preview']).is_file():errors.append('Missing annotated preview')
        report['errors'] += [row['asset_key']+': '+e for e in errors]
        report['warnings'] += [dict(asset_key=row['asset_key'],**w) for w in warnings]
    return report


if __name__=='__main__':
    result=validate_collection(arguments());print(json.dumps(result,indent=2))
    raise SystemExit(bool(result['errors']))
