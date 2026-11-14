"""WHT1: bounded, little-endian hotspot payloads, independent of containers."""
import struct
import re

HEADER = struct.Struct('<4sBBHHHHH')
REGION = struct.Struct('<BBBHBBB')
MAX_HOTSPOTS = 12
MAX_ACTIONS = 3
LABEL_MAX = 8
COMMAND_MAX = 80
UNBOUND = 1
NULL_STATE = 65535
MAX_BYTES = 4096


def encode(scene):
    body = bytearray(bytes.fromhex(scene['image']['sha256']))
    for ordinal, spot in enumerate(scene['hotspots'], 1):
        label = spot['label'].encode('ascii')
        x, y, width, height = spot['rect']
        body += REGION.pack(ordinal, x, y, width, height, len(spot['actions']), len(label))
        body += label
        for action in spot['actions']:
            name, command = action['label'].encode('ascii'), action['command'].encode('ascii')
            body += bytes([len(name), len(command)]) + name + command
    if len(body)+HEADER.size > MAX_BYTES:
        raise ValueError('WHT1 exceeds bounded consumer size')
    return HEADER.pack(b'WHT1', UNBOUND if scene['state'] is None else 0,
                       len(scene['hotspots']), scene['room'],
                       NULL_STATE if scene['state'] is None else scene['state'],
                       256, 159, len(body)) + body


def decode(data):
    if len(data) < HEADER.size + 32 or len(data) > MAX_BYTES:
        raise ValueError('Truncated WHT1 header/hash')
    magic, flags, count, room, state, width, height, length = HEADER.unpack_from(data)
    if (magic != b'WHT1' or flags not in (0, UNBOUND) or count > MAX_HOTSPOTS
            or not room or (width, height) != (256, 159)
            or length != len(data) - HEADER.size
            or (flags == UNBOUND and state != NULL_STATE)
            or (not flags and not 0 < state < NULL_STATE)):
        raise ValueError('Invalid WHT1 header')
    offset = HEADER.size + 32
    result = {'room': room, 'state': None if flags else state,
              'image_sha256': data[HEADER.size:offset].hex(), 'hotspots': []}
    def string(size):
        nonlocal offset
        value = data[offset:offset+size]
        if len(value) != size or any(c < 32 or c > 126 for c in value):
            raise ValueError('Truncated/non-ASCII WHT1 string')
        offset += size
        return value.decode('ascii')
    seen = set()
    for index in range(count):
        if offset + REGION.size > len(data):
            raise ValueError('Truncated WHT1 region')
        ordinal, x, y, w, h, actions, label_size = REGION.unpack_from(data, offset)
        offset += REGION.size
        if (ordinal != index+1 or ordinal in seen or not w or not h or x+w > width
                or y+h > height or not 1 <= actions <= MAX_ACTIONS or not 1 <= label_size <= 31):
            raise ValueError('Invalid WHT1 region')
        seen.add(ordinal)
        spot = {'id': ordinal, 'rect': [x,y,w,h], 'label': string(label_size), 'actions': []}
        commands = set()
        for _ in range(actions):
            if offset + 2 > len(data):
                raise ValueError('Truncated WHT1 action')
            a, b = data[offset:offset+2]; offset += 2
            if not 1 <= a <= LABEL_MAX or not 1 <= b <= COMMAND_MAX:
                raise ValueError('Invalid WHT1 action length')
            label, command = string(a), string(b)
            if command in commands or not re.fullmatch(r'[a-z]+(?: [a-z]+)*', command):
                raise ValueError('Invalid/duplicate WHT1 command')
            commands.add(command)
            spot['actions'].append({'label': label, 'command': command})
        result['hotspots'].append(spot)
    if offset != len(data):
        raise ValueError('Trailing WHT1 data')
    return result


def hit(scene, x, y):
    """Half-open rectangles; smaller regions win, then authored order."""
    matches = [s for s in scene['hotspots']
               if s['rect'][0] <= x < s['rect'][0]+s['rect'][2]
               and s['rect'][1] <= y < s['rect'][1]+s['rect'][3]]
    return min(matches, key=lambda s: s['rect'][2]*s['rect'][3], default=None)
