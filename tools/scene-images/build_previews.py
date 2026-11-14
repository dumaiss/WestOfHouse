"""Rebuild this pilot's previews and sheets; never invokes image generation.

Pillow performs only crop/resize and contact-sheet assembly. The sibling
ZephyrOS KIMG encoder and decoder are imported unchanged.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2] / 'assets/source/white-house'
PROJECT = ROOT.parents[2]
REVIEW = PROJECT / 'assets/review/white-house'
SDK_TOOLS = PROJECT.parent / 'ZephyrOS/tools'
sys.path.insert(0, str(SDK_TOOLS))
import kimg
from kimg_compare import decode_payload


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(image, exterior):
    width, height = image.size
    ratio = 256 / 159
    if width / height > ratio:
        cropped_width = int(height * ratio)
        left = (width - cropped_width) // 2
        box = (left, 0, left + cropped_width, height)
    else:
        cropped_height = int(width / ratio)
        # The generated exteriors place chimneys near the top. Sacrifice
        # foreground grass rather than cutting their roofs with a center crop.
        top = 0 if exterior else (height - cropped_height) // 2
        box = (0, top, width, top + cropped_height)
    return image.crop(box).resize((256, 159), Image.Resampling.BOX), list(box)


def sheet(records, field, filename, original=False):
    thumb = (432, 324) if original else (256, 159)
    margin, caption, columns = 12, 48, 3
    cell_width, cell_height = thumb[0] + margin * 2, thumb[1] + caption
    canvas = Image.new('RGB', (columns * cell_width,
                              ((len(records) + columns - 1) // columns) * cell_height), '#202328')
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
    for index, record in enumerate(records):
        x, y = (index % columns) * cell_width + margin, (index // columns) * cell_height
        with Image.open(ROOT / record[field]) as image:
            image = image.convert('RGB')
            if original:
                image.thumbnail(thumb, Image.Resampling.LANCZOS)
            canvas.paste(image, (x + (thumb[0] - image.width) // 2,
                                 y + (thumb[1] - image.height) // 2))
        state = record['runtime_state'] if record['runtime_state'] is not None else 'UNBOUND'
        draw.text((x, y + thumb[1] + 4), record['room'], font=font, fill='white')
        draw.text((x, y + thumb[1] + 22), f"Room {record['runtime_room']} / state {state}",
                  font=font, fill='#e0c88a')
    canvas.save(REVIEW / filename)


def main():
    (REVIEW / 'metadata').mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    metrics = []
    for record in manifest['records']:
        assert record['status'] == 'generated'
        key = record['asset_key']
        with Image.open(ROOT / record['original']) as source:
            reference, crop = prepare(source.convert('RGB'), record['room'].endswith('OF-HOUSE'))
        rgb_path = ROOT / record['rgb_preview']
        rgb_path.parent.mkdir(parents=True, exist_ok=True)
        reference.save(rgb_path)
        output = ROOT / record['kimg']
        output.parent.mkdir(parents=True, exist_ok=True)
        started = time.perf_counter()
        kimg.convert(rgb_path, output, 'yjk', (256, 159), resample='box', yjk_quality='optimized')
        elapsed = time.perf_counter() - started
        packed = output.read_bytes()
        assert packed[:4] == b'KIMG' and len(packed) == 16 + 256 * 159
        assert struct.unpack_from('<I', packed, 12)[0] == 256 * 159
        decoded = decode_payload(packed[16:])
        preview = Image.new('RGB', (256, 159))
        preview.putdata([tuple((c * 255 + 15) // 31 for c in pixel) for pixel in decoded])
        yjk_path = ROOT / record['yjk_preview']
        yjk_path.parent.mkdir(parents=True, exist_ok=True)
        preview.save(yjk_path)
        channels = iter(reference.tobytes())
        expected = [tuple((c * 31 + 127) // 255 for c in pixel)
                    for pixel in zip(channels, channels, channels)]
        mse = sum((a-b)**2 for p, q in zip(expected, decoded) for a, b in zip(p, q)) / (256*159*3)
        record.update(rgb_preview=os.path.relpath(rgb_path, ROOT),
                      yjk_preview=os.path.relpath(yjk_path, ROOT),
                      kimg=os.path.relpath(output, ROOT), crop_box=crop)
        metrics.append({'asset_key': key, 'crop_box': crop,
                        'rgb555_mse_per_component': mse,
                        'conversion_seconds': elapsed, 'kimg_bytes': len(packed),
                        'kimg_sha256': digest(output)})
        print(key, f'{elapsed:.2f}s', f'RGB555 MSE {mse:.4f}', flush=True)
    sheet(manifest['records'], 'original', 'contact-sheet-originals.png', original=True)
    sheet(manifest['records'], 'rgb_preview', 'contact-sheet-rgb-256x159.png')
    sheet(manifest['records'], 'yjk_preview', 'contact-sheet-yjk-256x159.png')
    report = {'schema': 'woh_phase2_conversion_v1', 'target': [256, 159],
              'resampling': 'Pillow BOX, crop without distortion, no sharpening or forced palette',
              'encoder': str(SDK_TOOLS / 'kimg.py'), 'encoder_sha256': digest(SDK_TOOLS / 'kimg.py'),
              'quality': 'optimized', 'pillow_version': Image.__version__,
              'decode': 'Existing kimg_compare.decode_payload; RGB555 expanded to RGB888',
              'preview_scope': 'Host decode, not an emulator or real-hardware display test',
              'records': metrics}
    (REVIEW / 'metadata/conversion.json').write_text(json.dumps(report, indent=2) + '\n')
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
