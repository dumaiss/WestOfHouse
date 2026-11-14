#!/usr/bin/env python3
"""Compile source-backed vision observations into review files and WHT1 bytes.

Vision interpretation lives in observations.json, not in keyword/geometry
templates. Regeneration never modifies artwork, Phase 1, ZIL or the application.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys

from PIL import Image, ImageDraw, ImageFont

from codec import encode

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/room-state-extractor'))
from zil_source import Source, zil


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = json.dumps(data, ensure_ascii=False, indent=2)+'\n'
    if not path.exists() or path.read_text() != raw:
        path.write_text(raw)


def relative(path):
    path = Path(path).resolve()
    return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)


def candidates(scene):
    result = {o['id']: {'kind': 'object', 'pointer': f'/scene/objects/{i}', **o}
              for i, o in enumerate(scene['objects'])}
    for i, exit in enumerate(scene['exits']):
        # IN ROOMS is a room property, not a traversable doorway.
        if exit['destination']:
            result[exit['id']] = {'kind': 'exit', 'pointer': f'/scene/exits/{i}', **exit}
    return result


def syntax(source, verb):
    # Use the shared ZIL parser, retaining actual syntax source locations.
    if verb in ('north','south','east','west','southwest','up','down'):
        # Directions are vocabulary tables rather than <SYNTAX> verb forms.
        return {'kind':'room_exit_direction', 'note':'Direction is established by the source room exit, not a new game action.'}
    for form in source.forms:
        if isinstance(form, zil.Form) and zil.head(form) == 'SYNTAX':
            values = zil.values_after_head(form)
            if values and zil.symbol(values[0]) == verb.upper():
                return {'file': form.file, 'line': form.line_start, 'expression': zil.render(form)}
    raise ValueError('No parser syntax for '+verb)


def actions_for(candidate, scene, state, source):
    name = candidate['id']
    if candidate['kind'] == 'exit':
        direction = candidate['directions'][0]
        command = {'sw':'southwest'}.get(direction,direction)
        pairs = [('Go SW' if direction == 'sw' else 'Go '+direction,command)]
    else:
        preferred = {'WHITE-HOUSE':'house','BOARDED-WINDOW':'window','FRONT-DOOR':'door',
                     'KITCHEN-WINDOW':'window','KITCHEN-TABLE':'table','ATTIC-TABLE':'table',
                     'MAILBOX':'mailbox','STAIRS':'stairs','CHIMNEY':'chimney','FOREST':'forest',
                     'KNIFE':'knife','ROPE':'rope'}
        noun = preferred.get(name)
        if noun is None or noun.upper() not in candidate['synonyms']:
            raise ValueError('No reviewed parser noun for '+name)
        pairs = [('Examine','examine '+noun)]
        if name == 'KITCHEN-WINDOW':
            if state not in (1,2):
                raise ValueError('Window menu requires an existing reviewed state')
            pairs += [('Open' if state == 1 else 'Close',('open ' if state == 1 else 'close ')+noun),
                      ('Go out' if scene['id']=='KITCHEN' else 'Enter','enter window')]
        elif name in ('FRONT-DOOR','BOARDED-WINDOW'):
            pairs += [('Open','open '+noun)]
        elif name == 'MAILBOX':
            pairs += [('Open','open mailbox'),('Close','close mailbox')]
        elif name == 'STAIRS':
            direction = 'down' if scene['id']=='ATTIC' else 'up'
            if not any(e['directions']==[direction] and e['destination'] for e in scene['exits']):
                raise ValueError('Stair direction not established')
            pairs += [('Go '+direction,direction)]
        elif name == 'CHIMNEY':
            pairs += [('Climb','climb down chimney')]
        elif 'TAKEBIT' in candidate['source_flags']:
            pairs += [('Take','take '+noun)]
    return [{'label': label, 'command': command,
             'source_refs': candidate.get('source_refs',[]),
             'parser_source': syntax(source,command.split()[0]),
             'authority':'Suggestion only; the original parser validates and executes it.'}
            for label,command in pairs]


def annotate(scene, image_path, output):
    """Diagnostics only: exact final-pixel outlines, then 4x nearest scaling."""
    with Image.open(image_path) as image:
        image = image.convert('RGB')
    draw = ImageDraw.Draw(image)
    colors = ['#ffec33','#ff5555','#38eeff','#ff84ff','#66ff66','#ffa044','#aaccff','#ffffff']
    legend = []
    for i, spot in enumerate(scene['hotspots']):
        x,y,w,h = spot['rect']; color=colors[i%len(colors)]
        draw.rectangle((x,y,x+w-1,y+h-1),outline=color,width=1)
        # Numeric IDs avoid hiding the targets with full text at native size.
        draw.rectangle((x,y,x+10,y+10),fill='#101010')
        draw.text((x+1,y),str(i+1),fill=color,font=ImageFont.load_default())
        legend.append((color,f"{i+1}. {spot['label']} ({spot['object_id']})"))
    enlarged=image.resize((1024,636),Image.Resampling.NEAREST)
    rows=(len(legend)+1)//2
    canvas=Image.new('RGB',(1024,636+48+rows*32),'#202328')
    canvas.paste(enlarged,(0,0));draw=ImageDraw.Draw(canvas)
    font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',24)
    draw.text((8,641),f"{scene['asset_key']} — {scene['room_name']}",fill='white',font=font)
    for i,(color,text) in enumerate(legend):
        draw.text((8+(i%2)*512,680+(i//2)*32),text,fill=color,font=font)
    output.parent.mkdir(parents=True,exist_ok=True);canvas.save(output)


def binary_path(output, key, state):
    if output.resolve() == (ROOT/'assets/source/hotspots').resolve():
        base = ROOT/('assets/review/hotspots/binary' if state is None else 'assets/runtime/hotspots')
        return __import__('os').path.relpath(base/(key+'.wht'), output)
    return 'binary/'+key+'.wht'


def preview_path(output, key):
    if output.resolve() == (ROOT/'assets/source/hotspots').resolve():
        return '../../review/hotspots/annotated/'+key+'.png'
    return 'annotated/'+key+'.png'


def review_path(output, name):
    if output.resolve() == (ROOT/'assets/source/hotspots').resolve():
        return ROOT/'assets/review/hotspots'/name
    return output/name


def generate(args):
    pilot=read(args.pilot/'manifest.json'); observations=read(args.observations)
    keys=[r['asset_key'] for r in pilot['records']]
    if len(keys)!=len(set(keys)) or set(keys)!=set(observations['scenes']):
        raise ValueError('Pilot/vision scene sets differ or contain duplicates')
    if any(not re.fullmatch(r'room-[0-9]+-state-([0-9]+|unbound)',key) for key in keys):
        raise ValueError('Unsafe pilot asset key')
    source=Source(args.source)
    records=[]
    for record in pilot['records']:
        key=record['asset_key']; observation=observations['scenes'][key]
        path=args.pilot/record['rgb_preview']
        if sha(path)!=observation['image_sha256']:
            raise ValueError('Image changed; vision localization must be reviewed again: '+key)
        if not re.fullmatch(r'[A-Z0-9][A-Z0-9-]*',record['room']):
            raise ValueError('Unsafe room identifier')
        scene_path=args.facts/(record['room'].lower()+'.scene.json')
        original=read(scene_path); facts=original['scene']; known=candidates(facts)
        scene={'schema':'scene_hotspots_v1','room':record['runtime_room'],
               'state':record['runtime_state'],'room_name':record['room'],
               'binding':record['binding'],'asset_key':key,'image_size':[256,159],
               'image':{'path':relative(path),'sha256':sha(path),
                        'original_path':relative(args.pilot/record['original']),
                        'original_sha256':sha(args.pilot/record['original']),
                        'crop_box':record['crop_box'],'coordinate_reference':'final RGB preview; no additional scaling transform'},
               'hotspots':[],'unresolved':copy.deepcopy(observation['unresolved']),
               'localization':{'method':observations['method'],'observations':relative(args.observations),
                               'observations_sha256':sha(args.observations)},
               'provenance':{'scene_file':relative(scene_path),'scene_sha256':sha(scene_path),
                             'zil_source_sha256':source.source_hashes,
                             'phase1_specification':record['phase1_specification'],
                             'pilot_manifest':relative(args.pilot/'manifest.json')},
               'overlap_policy':'smallest matching rectangle; authored order breaks equal-area ties; half-open edges'}
        for observed in observation['hotspots']:
            candidate=known[observed['object_id']]
            spot=copy.deepcopy(observed)
            spot.update(actions=actions_for(candidate,facts,record['runtime_state'],source),
                        provenance={'pointer':candidate['pointer'],'source_refs':candidate.get('source_refs',[])})
            scene['hotspots'].append(spot)
        write(args.output/'scenes'/(key+'.json'),scene)
        records.append({'asset_key':key,'room':scene['room'],'state':scene['state'],
                        'manifest':'scenes/'+key+'.json','binary':binary_path(args.output,key,record['runtime_state']),
                        'preview':preview_path(args.output,key)})
    write(args.output/'manifest.json',{'schema':'woh_hotspot_pilot_index_v1','image_size':[256,159],
                                     'pilot_manifest':relative(args.pilot/'manifest.json'),'scenes':records})
    from validate import validate_scene
    report={'schema':'woh_hotspot_validation_v1','scenes':len(records),'hotspots':0,'errors':[],'warnings':[]}
    for row in records:
        scene=read(args.output/row['manifest'])
        errors,warnings=validate_scene(scene,args)
        report['errors'] += [row['asset_key']+': '+e for e in errors]
        report['warnings'] += [dict(asset_key=row['asset_key'],**w) for w in warnings]
        report['hotspots']+=len(scene['hotspots'])
        if errors:
            continue
        binary=args.output/row['binary'];binary.parent.mkdir(parents=True,exist_ok=True)
        binary.write_bytes(encode(scene))
        annotate(scene,ROOT/scene['image']['path'],args.output/row['preview'])
    write(review_path(args.output,'validation.json'),report)
    if report['errors']:
        raise ValueError('Hotspot validation failed: '+str(report['errors'][:3]))
    tile=(512,430);sheet=Image.new('RGB',(tile[0]*3,tile[1]*3),'#202328')
    for i,row in enumerate(records):
        with Image.open(args.output/row['preview']) as image:
            # Annotation pixels stay aligned: downsample the 4x sheet to 2x.
            image=image.resize((512,image.height//2),Image.Resampling.NEAREST)
            sheet.paste(image,((i%3)*tile[0],(i//3)*tile[1]))
    sheet.save(review_path(args.output,'contact-sheet.png'))
    print(json.dumps({'scenes':len(records),'hotspots':report['hotspots'],'warnings':len(report['warnings']),
                      'errors':report['errors'],'output':str(args.output)},indent=2))


def arguments(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pilot',type=Path,default=ROOT/'assets/source/white-house')
    p.add_argument('--observations',type=Path,default=Path(__file__).with_name('observations.json'))
    p.add_argument('--facts',type=Path,default=ROOT/'assets/source/scene-facts')
    p.add_argument('--source',type=Path,default=ROOT/'vendor/infocom-zork1')
    p.add_argument('--output',type=Path,default=ROOT/'assets/source/hotspots')
    return p.parse_args(argv)


if __name__=='__main__':
    try:
        generate(arguments())
    except (ValueError,KeyError,OSError) as error:
        raise SystemExit('scene-hotspots: '+str(error)) from None
