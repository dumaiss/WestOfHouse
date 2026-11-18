#!/usr/bin/env python3
"""Rebuild Cellar/Living Room previews from selected originals, without AI calls."""
import hashlib
import importlib.util
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT/'assets/source/house-interiors'
REVIEW = ROOT/'assets/review/house-interiors'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    encoder = ROOT.parent/'ZephyrOS/tools/kimg.py'
    spec = importlib.util.spec_from_file_location('interior_kimg', encoder)
    kimg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(kimg)
    manifest = json.loads((SOURCE/'manifest.json').read_text())
    for folder in ('rgb','yae','kimg-yae'):
        (REVIEW/folder).mkdir(parents=True,exist_ok=True)
    sheet = Image.new('RGB',(560,207*len(manifest['records'])),'#202328')
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',13)
    rows=[]
    for index,record in enumerate(manifest['records']):
        key=record['asset_key'];original=SOURCE/record['original']
        if sha(original)!=record['source_sha256'] or sha(SOURCE/record['prompt'])!=record['prompt_sha256']:
            raise ValueError('Selected source or generation prompt changed: '+key)
        rgb=REVIEW/'rgb'/(key+'.png');output=REVIEW/'kimg-yae'/(key+'.img');preview=REVIEW/'yae'/(key+'.png')
        with Image.open(original) as im:
            im.convert('RGB').crop(record['crop_box']).resize((256,159),Image.Resampling.BOX).save(rgb)
        kimg.convert(rgb,output,'yae',(256,159),resample='box',yjk_quality='optimized')
        kimg.preview(output,preview)
        for col,(label,path) in enumerate([('RGB',rgb),('YJK+YAE',preview)]):
            x,y=col*280+12,index*207
            draw.text((x,y+4),key+' / '+label,font=font,fill='white')
            with Image.open(path) as im:sheet.paste(im,(x,y+24))
        rows.append({'asset_key':key,'runtime_room':record['runtime_room'],'runtime_state':record['runtime_state'],
                     'rgb':rgb.relative_to(ROOT).as_posix(),'kimg':output.relative_to(ROOT).as_posix(),
                     'preview':preview.relative_to(ROOT).as_posix(),'kimg_sha256':sha(output)})
        print(key,output,flush=True)
    sheet.save(REVIEW/'contact-sheet-rgb-yae.png')
    (REVIEW/'conversion.json').write_text(json.dumps({'schema':'woh_scene_conversion_v1','encoder':str(encoder),
        'encoder_sha256':sha(encoder),'quality':'optimized','target':[256,159],'records':rows},indent=2)+'\n')


if __name__=='__main__':
    main()
