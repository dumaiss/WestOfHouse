#!/usr/bin/env python3
"""Profile real Zork turns on the simulator's Z80 core, by function.

Builds libzvm, the story cache and probe.c with SDCC (the game's settings),
runs the probe under turnprof.c (tests/simcore's C core), whose story and
XMEM traps cost nothing, and prints the T-states of each turn and the
functions that spend them.  CPU only: no OS, card or banking time.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ZVM = HERE.parents[1]
OS_ROOT = ZVM.parents[1] / "ZephyrOS"
SDK = OS_ROOT / "build/kos-sdk-0.2.0"
SIMCORE = OS_ROOT / "tests/simcore"
STORY = ZVM.parent / "assets/runtime/story/zork1.z3"
FLAGS = ["-DDYN_CACHE_PAGES=4", "-DSTATIC_CACHE_PAGES=4", "-DZVM_CACHE_ASM=1"]
COMMANDS = ["(start)", "look", "inventory", "north", "east", "open window", "enter house",
            "west", "east", "west", "up", "down", "east", "out"]


def run(argv, cwd):
    r = subprocess.run(list(map(str, argv)), cwd=cwd, text=True, capture_output=True)
    if r.returncode:
        raise SystemExit(f"{' '.join(map(str, argv))}\n{r.stdout}{r.stderr}")
    return r.stdout


def build(out, extra_sources, extra_asm, defines=()):
    out.mkdir(parents=True, exist_ok=True)
    cflags = ["-mz80", "-c", "--nostdinc", "--Werror", "--debug", *FLAGS,
              *("-D" + d for d in defines),
              "-I" + str(SDK / "include/abi"), "-I" + str(SDK / "include"),
              "-I" + str(SDK / "include/kos"),
              "-I" + str(ZVM / "include"), "-I" + str(ZVM / "src"), "-I" + str(ZVM / "tests")]
    sources = sorted((ZVM / "src").glob("*.c")) + [ZVM / "tests/story_cache.c", ZVM / "tests/story_cache_setup.c", ZVM / "tests/story_cache_l2.c",
              ZVM / "tests/story_cache_z80.c", HERE / "probe.c"] + list(extra_sources)
    objects = []
    for path in sources:
        obj = out / (path.stem + ".rel")
        run(["sdcc", *cflags, path, "-o", obj], out)
        objects.append(obj)
    for path in [HERE / "entry.asm", *extra_asm]:
        obj = out / (path.stem + ".rel")
        run(["sdasz80", "-plosgffw", obj, path], out)
        objects.insert(0, obj) if path.name == "entry.asm" else objects.append(obj)
    (out / "probe.lk").write_text("\n".join([
        "-n", "-m", "-w", "-y", "-i " + str(out / "probe.ihx"),
        "-b _CODE = 0x0100", "-b _DATA = 0xB000",
        *map(str, objects), "-k " + str(SDK / "lib"), "-l libkos", "-e", ""]))
    run(["sdldz80", "-f", "probe.lk"], out)
    image = bytearray(65536)
    for line in (out / "probe.ihx").read_text().split():
        n, a, t = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
        if t == 0:
            image[a:a + n] = bytes.fromhex(line[9:9 + 2 * n])
    (out / "probe.bin").write_bytes(image)
    syms = {}
    for line in (out / "probe.map").read_text().splitlines():
        m = re.match(r"\s+([0-9A-F]{8})\s+(\S+)", line)
        if m:
            syms[m.group(2)] = int(m.group(1), 16)
    if syms.get("s__DATA", 0) + syms.get("l__DATA", 0) > 0xD000 or \
       syms.get("s__CODE", 0) + syms.get("l__CODE", 0) > 0xB000:
        raise SystemExit("probe image overlaps data or stack")
    (out / "probe.syms").write_text("".join(f"{k} {v:x}\n" for k, v in syms.items()))
    funcs = {}
    cdb = out / "probe.cdb"
    if cdb.exists():
        for line in cdb.read_text().splitlines():
            m = re.match(r"L:(G|F(\w+))\$(\w+)\$\d+\$\d+:([0-9A-F]+)$", line)
            if m:
                funcs.setdefault(int(m.group(4), 16), (m.group(2) + ":" if m.group(2) else "") + m.group(3))
    for k, v in syms.items():
        if k.startswith("_") and v < syms.get("s__DATA", 0xB000):
            funcs.setdefault(v, k[1:])
    return funcs


def driver(out):
    sys.path.insert(0, str(OS_ROOT / "tests"))
    import z80sim
    def table(d, default=4):
        return ",".join(str(d.get(n, default)) for n in range(256))
    (out / "tables.h").write_text(
        f"static const int ONE_T[256]={{{table(z80sim.ONE_BYTE_T)}}};\n"
        f"static const int ED_T[256]={{{table(z80sim.ED_T)}}};\n"
        f"static const int DD_T[256]={{{table(z80sim.DD_T)}}};\n"
        f"static const int ED_BLOCK[256]={{{','.join('1' if n in z80sim.ED_BLOCK else '0' for n in range(256))}}};\n"
        f"#define INTERRUPT_T {z80sim.INTERRUPT_TSTATES}\n")
    run(["cc", "-O2", "-I" + str(SIMCORE), "-I" + str(out), HERE / "turnprof.c",
         SIMCORE / "z80core.c", "-o", out / "turnprof"], out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, default=ZVM / "build/turnprof")
    p.add_argument("--story", type=Path, default=STORY)
    p.add_argument("--top", type=int, default=30)
    p.add_argument("--source", type=Path, action="append", default=[], help="extra C source")
    p.add_argument("--asm", type=Path, action="append", default=[], help="extra asm source")
    p.add_argument("-D", dest="defines", action="append", default=[], help="extra SDCC define")
    a = p.parse_args()
    out = a.out.resolve()
    funcs = build(out, a.source, a.asm, a.defines)
    driver(out)
    hashes = run([out / "turnprof", out / "probe.bin", a.story, out / "probe.syms", out / "hist.bin"], out)
    (out / "outputs.txt").write_text(hashes)
    data = (out / "hist.bin").read_bytes()
    turns = struct.unpack_from("<i", data)[0]
    totals = struct.unpack_from(f"<{turns}q", data, 4)
    base = 4 + 8 * turns
    starts = sorted(funcs)
    import bisect
    per = {}
    for t in range(turns):
        hist = struct.unpack_from("<65536q", data, base + t * 65536 * 8)
        for pc, v in enumerate(hist):
            if v:
                i = bisect.bisect_right(starts, pc) - 1
                name = funcs[starts[i]] if i >= 0 else "?"
                per.setdefault(name, [0] * turns)[t] += v
    print(f"{'turn':<12} {'T-states':>12} {'ms@10MHz':>9}")
    for t in range(turns):
        print(f"{COMMANDS[t] if t < len(COMMANDS) else t:<12} {totals[t]:12,} {totals[t] / 10000:9.0f}")
    grand = sum(totals)
    print(f"{'all':<12} {grand:12,} {grand / 10000:9.0f}\n")
    execs = struct.unpack_from("<65536I", data, base + turns * 65536 * 8)
    calls = {funcs[a]: execs[a] for a in starts}
    print(f"{'function':<40} {'T-states':>13} {'share':>6} {'calls':>8} {'T/call':>7}")
    for name, v in sorted(per.items(), key=lambda kv: -sum(kv[1]))[:a.top]:
        n = calls.get(name, 0)
        print(f"{name:<40} {sum(v):13,} {100 * sum(v) / grand:5.1f}% {n:8} {sum(v) // n if n else 0:7}")


if __name__ == "__main__":
    main()
