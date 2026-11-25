#!/usr/bin/env python3
"""Create YJK+YAE scene comparisons with the SDK encoder and approved crops."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT / 'assets/source/white-house'
REVIEW = PROJECT / 'assets/review/white-house'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path,
                        default=PROJECT.parent/'ZephyrOS/build-mk2/sys/kos-sdk-0.2.0-mk2')
    args = parser.parse_args()
    encoder = args.sdk.resolve()/'tools/kimg.py'
    spec = importlib.util.spec_from_file_location('woh_yae_encoder', encoder)
    kimg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(kimg)
    if 'yae' not in kimg.MODES:
        raise SystemExit('Install the SDK with kimg --mode yae support first')
    records = json.loads((SOURCE/'manifest.json').read_text())['records']
    for folder in ('kimg-yae', 'previews/yae', 'metadata'):
        (REVIEW/folder).mkdir(parents=True, exist_ok=True)
    report = {'schema': 'woh_yae_comparison_v1', 'mode': 'YJK+YAE',
              'target': [256, 159], 'quality': 'optimized',
              'encoder': str(encoder), 'encoder_sha256': sha(encoder),
              'pillow_version': Image.__version__,
              'palette': 'No override; picture pixels have A clear, palette slots remain available for UI.',
              'records': []}
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
    sheet = Image.new('RGB', (840, 207*len(records)), '#202328')
    draw = ImageDraw.Draw(sheet)
    for index, record in enumerate(records):
        key = record['asset_key']
        rgb = (SOURCE/record['rgb_preview']).resolve()
        output = REVIEW/'kimg-yae'/(key+'.img')
        preview = REVIEW/'previews/yae'/(key+'.png')
        kimg.convert(rgb, output, 'yae', (256,159), resample='box', yjk_quality='optimized')
        kimg.preview(output, preview)
        g7 = REVIEW/'kimg-g7'/(key+'.img')
        yjk = (SOURCE/record['kimg']).resolve()
        for col, (label, path) in enumerate([('G7',g7),('YJK',yjk),('YJK+YAE',output)]):
            x, y = col*280+12, index*207
            draw.text((x,y+4),key+' / '+label,font=font,fill='white')
            if path.is_file():
                _, w, h, pixels = kimg.decode(path)
                image = Image.new('RGB',(w,h)); image.putdata(pixels)
                sheet.paste(image,(x,y+24))
            else:
                draw.text((x,y+70),'Unbound: no runtime G7',font=font,fill='#e0c88a')
        report['records'].append({'asset_key':key,'room':record['runtime_room'],
                                  'state':record['runtime_state'],'crop_box':record['crop_box'],
                                  'source_rgb':rgb.relative_to(PROJECT).as_posix(),
                                  'source_rgb_sha256':sha(rgb),
                                  'kimg':output.relative_to(PROJECT).as_posix(),
                                  'kimg_sha256':sha(output),'bytes':output.stat().st_size,
                                  'preview':preview.relative_to(PROJECT).as_posix()})
        print(key, output, flush=True)
    sheet.save(REVIEW/'contact-sheet-g7-yjk-yae.png')
    (REVIEW/'metadata/conversion-yae.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    main()
