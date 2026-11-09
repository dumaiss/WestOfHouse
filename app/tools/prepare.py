#!/usr/bin/env python3
"""Prepare G0's existing story bindings and raw embedded scene; never compile ZIL."""
import argparse
import hashlib
import json
from pathlib import Path

def prepare(story, metadata, image, output):
    data, binding, pixels = story.read_bytes(), metadata.read_bytes(), image.read_bytes()
    if len(binding) != 70 or binding[:4] != b'WOH1' or binding[6:] != data[:64]:
        raise ValueError('Story and WOH bindings must be a matching built pair')
    if data[0] != 3 or binding[4] >= 240 or binding[5] >= 240 or binding[4] == binding[5]:
        raise ValueError('Invalid V3 global bindings')
    if len(pixels) != 256 * 144:
        raise ValueError('Scene must be raw 256x144 G7 (36864 bytes)')
    output.mkdir(parents=True, exist_ok=True)
    header = ('/* Generated from the existing compiled story; do not edit. */\n'
              '#ifndef WOH_BINDINGS_H\n#define WOH_BINDINGS_H\n'
              f'#define WOH_HERE_INDEX {binding[4]}u\n'
              f'#define WOH_STATE_INDEX {binding[5]}u\n'
              'static const uint8_t woh_story_header[64] = {\n' +
              ','.join(f'0x{x:02x}' for x in data[:64]) + '\n};\n#endif\n')
    (output/'woh_bindings.h').write_text(header)
    (output/'scene-a.bin').write_bytes(pixels[:32768])
    (output/'scene-b.bin').write_bytes(pixels[32768:])
    (output/'inputs.json').write_text(json.dumps({
        'story_sha256': hashlib.sha256(data).hexdigest(),
        'bindings_sha256': hashlib.sha256(binding).hexdigest(),
        'scene_sha256': hashlib.sha256(pixels).hexdigest()}, indent=2)+'\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--story', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.story, args.metadata, args.image, args.output)
