"""Check the linked Z80 memory paths with a modeled mapped-page callback.

Executes real memory.asm instructions; does not model OS mapping/IRQ timing.
Run after building the Mk2 application.
"""
import os
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
os_root = Path(os.environ.get('ZEPHYROS_ROOT', root.parents[1] / 'ZephyrOS'))
sys.path.insert(0, str(os_root / 'tests'))
from z80sim import Z80

image = root / 'build/target/user/woh/woh'
syms = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r'^\s*([0-9A-F]{8})\s+(\S+)', image.with_suffix('.map').read_text(), re.M)}
mem = bytearray(65536)
for line in image.with_suffix('.ihx').read_text().splitlines():
    n, a, t = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
    if t == 0 and a + n <= 0x8000:
        mem[a:a+n] = bytes.fromhex(line[9:9+n*2])
z = Z80(bytes(mem), bytes(65536))
z.latch = 0x10
z.bank0[:] = mem
state, validity, result, callback = 0xc800, 0xc900, 0xc901, 0xc902
story = bytearray((i * 13 + (i >> 13)) & 255 for i in range(16384))
z.wr16(state + 8, callback)
z.wr16(state + 10, validity)
z.bank0[state+29:state+33] = len(story).to_bytes(4, 'little')
z.bank0[state+37:state+41] = len(story).to_bytes(4, 'little')
z.wr(validity, 255)
lends = switches = 0

def access(kind, address, value=0):
    global lends, switches
    z.sp = 0xf000
    args = address.to_bytes(4, 'little') + (bytes([value]) if kind == 'write' else result.to_bytes(2, 'little'))
    z.bank0[z.sp:z.sp+len(args)] = args
    z.push16(0xface)
    z.pc = syms['_zvm_mem_' + kind]
    z.h, z.l = state >> 8, state & 255
    z.ix = 0x1234
    for _ in range(2000):
        if z.pc == 0xface:
            break
        if z.pc == callback:
            ret = z.pop16()
            at = int.from_bytes(z.bank0[z.sp:z.sp+4], 'little')
            region = at >> 13
            old = z.rd(validity)
            if old != region:
                if old != 255:
                    story[old*8192:(old+1)*8192] = z.bank0[0x8000:0xa000]
                z.bank0[0x8000:0xa000] = story[region*8192:(region+1)*8192]
                z.wr(validity, region)
                switches += 1
            pointer = 0x8000 + (at & 0x1e00)
            z.d, z.e = pointer >> 8, pointer & 255
            z.sp += 5
            z.pc = ret
            lends += 1
        else:
            assert z.pc not in (syms['_zvm_mem_read_slow'], syms['_zvm_mem_write_slow'], syms['_zvm_mem_fetch_slow']), 'unexpected byte fallback'
            z.step()
    assert z.pc == 0xface and z.a == 0 and z.sp == 0xf000 + len(args)
    assert z.ix == 0x1234
    if kind != 'write':
        assert z.rd(result) == story[address]

for _ in range(100):
    access('fetch', 64)
    access('read', 65)
assert lends == 2 and switches == 1
for _ in range(300):
    access('read', 8192)
    access('fetch', 64)
assert switches == 601
access('write', 8192, 0x5a)
story[8192] = 0x5a
access('read', 8192)
# Scope exit invalidates every window, even if it cached the same page tag.
z.wr(validity, 255)
before = lends
access('read', 8192)
assert lends == before + 1
print('linked Z80 mapped memory: PASS (200 hits, 600 region switches, direct write, validity, ABI/stack; no byte fallback)')
