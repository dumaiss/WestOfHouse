"""Schema and semantic checks; does not execute the VM or generate artwork."""
import json
from pathlib import Path
import re
import jsonschema
import yaml
from mapping import ROOT, Source, resolve, sha


def pointer(data, path):
    for token in path.strip('/').split('/'):
        token=token.replace('~1','/').replace('~0','~')
        data=data[int(token)] if isinstance(data,list) else data[token]
    return data


def validate_art(output, args):
    from generate import objects_for, presentation_for, visible_dressing
    output=Path(output)
    load=lambda p:json.loads(Path(p).read_text())
    schema=load(ROOT/'schemas/scene-art-v1.schema.json')
    jsonschema.Draft7Validator.check_schema(schema)
    validator=jsonschema.Draft7Validator(schema)
    errors=[]
    def document(relative):
        relative=Path(relative)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Unsafe artifact path')
        data=load(output/relative)
        for error in validator.iter_errors(data):
            errors.append(str(relative)+': schema '+error.message[:150])
        return data
    manifest=document('manifest.json')
    style=document('world/style-guide.json')
    continuity=document('world/continuity.json')
    direction=load(args.direction)
    bindings=load(args.room_ids)
    authority={r['room']:r for r in load(args.mapping)['rooms']}
    dictionary={r['id']:r for r in yaml.safe_load(args.dictionary.read_text())['rooms']}
    source=Source(args.source)
    if manifest['story_sha256']!=bindings['story_sha256'] or sha(args.story)!=manifest['story_sha256']:
        errors.append('Story association differs')
    if style!=direction['style']:
        errors.append('World style is stale')
    expected_groups={g['id']:g for g in direction['continuity']['groups']}
    for group in continuity['groups']:
        if {k:v for k,v in group.items() if k!='provenance'}!=expected_groups[group['id']]:
            errors.append('Continuity group is stale: '+group['id'])
        for origin in group['provenance']:
            if sha(origin['scene_file'])!=origin['scene_sha256']:
                errors.append('Continuity source hash differs')
    seen_rooms=set(); seen_pairs=set(); seen_assets=set(); total=0; unknown=0; legacy_missing=0; actions=0
    for record in manifest['rooms']:
        name=record['room']
        if name in seen_rooms:
            errors.append('Duplicate room '+name)
        seen_rooms.add(name)
        room=document(record['specification'])
        original=load(room['provenance']['scene_file']); scene=original['scene']
        if sha(room['provenance']['scene_file'])!=room['provenance']['scene_sha256']:
            errors.append('Stale scene '+name)
        if room['room']!=name or room['runtime_room']!=bindings['rooms'][name]:
            errors.append('Room identity differs '+name)
        if room['runtime_status']!=authority[name]['status']:
            errors.append('Runtime room status differs '+name)
        if (room['canonical']['descriptions']!=scene['descriptions']
                or room['canonical']['exits']!=scene['exits']
                or room['canonical']['source_flags']!=scene['source_flags']
                or room['canonical']['objects']!=objects_for(scene)):
            errors.append('Canonical room facts changed '+name)
        if room['artistic']!=direction['rooms'][name]:
            errors.append('Room direction is stale '+name)
        refs={r['id']:r for r in scene['source_references']}
        for ref in refs.values():
            path=args.source/ref['file']
            if not path.exists() or not 1<=ref['line_start']<=ref['line_end']<=len(path.read_text().splitlines()):
                errors.append('Untraceable source location '+name+'/'+ref['id'])
        expected={s['runtime_state']:s for s in resolve(scene,authority[name],dictionary[name],source)}
        if sorted(expected)!=sorted(s['runtime_state'] for s in record['states']):
            errors.append('Authoritative state set differs '+name)
        if not expected:
            unknown+=1
        files=set(); prompts=set(); artistic=None
        common=set.intersection(*(set(f['text'] for f in r['canonical']) for r in expected.values())) if expected else set()
        for entry in record['states']:
            state=document(entry['specification']); number=entry['runtime_state']; total+=1
            pair=(state['runtime_room'],state['runtime_state'])
            if pair in seen_pairs or state['asset_key'] in seen_assets:
                errors.append('Duplicate runtime pair or asset '+str(pair))
            seen_pairs.add(pair);seen_assets.add(state['asset_key'])
            files.add(Path(entry['specification']).name);prompts.add(Path(entry['prompt']).name)
            fresh=expected.get(number)
            if fresh is None:
                errors.append('Invented state '+name+'/'+str(number));continue
            if state['room']!=name or state['runtime_room']!=record['runtime_room'] or state['runtime_state']!=number or state['symbolic_state']!=fresh['symbolic_state']:
                errors.append('State identity differs '+name)
            if state['canonical']!=fresh['canonical'] or state['condition']!=fresh['condition'] or state['extracted_states']!=fresh['extracted_states']:
                errors.append('Canonical state facts/provenance changed '+name+'/'+str(number))
            if state['description_ids']!=fresh['description_ids']:
                errors.append('Description association differs '+name)
            if state['required_environmental_features']!=[f['text'] for f in fresh['canonical']]:
                errors.append('Required features omit canonical text '+name)
            presentation=presentation_for(direction,name,fresh['symbolic_state'])
            if state['state_artistic']!=presentation or presentation['visual_treatment'] not in state['prompt']:
                errors.append('State artistic treatment differs '+name)
            expected_fixed=[o['id'] for o in visible_dressing(objects_for(scene),presentation)]
            if state['required_visible_objects']!=expected_fixed:
                errors.append('Source room dressing differs '+name)
            for fact in state['canonical']:
                if pointer(original,fact['pointer'])!=fact['text'] or not set(fact['source_refs'])<=refs.keys():
                    errors.append('Broken fact provenance '+name)
                normalized=' '.join(fact['text'].replace('|',' ').split())
                if normalized not in state['prompt']:
                    errors.append('Prompt omits canonical fact '+name)
            for obj in room['canonical']['objects']:
                if not set(obj['source_refs'])<=refs.keys():
                    errors.append('Broken object provenance '+name)
            if (output/entry['prompt']).read_text()!=state['prompt']:
                errors.append('Prompt file differs '+name)
            if state['artistic']!=room['artistic'] or state['invariants']['artistic']!=room['artistic']:
                errors.append('Cross-state composition drift '+name)
            if state['invariants']['canonical_text']!=sorted(common) or state['changes']!=[f['text'] for f in fresh['canonical'] if f['text'] not in common]:
                errors.append('State delta/invariant mismatch '+name)
            if state['target']!=style['target'] or manifest['target']!=style['target']:
                errors.append('Target differs '+name)
            for term in ['256 by 159','Viewpoint:','Composition:','Lighting and color:','Style:']:
                if term not in state['prompt']:
                    errors.append('Incomplete standalone prompt '+name)
            # Engine IDs must not leak into the image-generation instruction.
            if re.search(r'LOOK-STATE|ACTION-STATE|RAINBOW-FLAG|M-LOOK|PRSO|PRSI',state['prompt']):
                errors.append('Engine terminology leaked into prompt '+name)
            if state['reference']:
                ref=state['reference']
                if ref['runtime_state'] not in expected or ref['room']!=name or ref['runtime_state']==number:
                    errors.append('Invalid preferred reference '+name)
            if not state['extracted_states']:
                legacy_missing+=1
        for folder,suffix,wanted in [('states','json',files),('prompts','md',prompts)]:
            if {p.name for p in (output/'rooms'/name/folder).glob('*.'+suffix)}!=wanted:
                errors.append('Stale or unlisted artifacts '+name+'/'+folder)
        actions+=sum(s['id'].startswith('ACTION-') for s in scene['states'])
    return {'schema':'scene_art_validation_v1','rooms':len(seen_rooms),'states':total,'prompts':total,
        'runtime_unknown_rooms':unknown,'legacy_relationships_unresolved':legacy_missing,
        'supporting_action_states_excluded':actions,'unique_runtime_pairs':len(seen_pairs),
        'target':[256,159],'errors':errors}


if __name__=='__main__':
    from generate import arguments
    args=arguments()
    result=validate_art(args.output,args)
    print(json.dumps(result,indent=2))
    raise SystemExit(bool(result['errors']))
