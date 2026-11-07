#!/usr/bin/env python3
"""Measure unprofiled story-cache CPU costs with ZephyrOS's existing Z80sim.

SDK xread/xwrite and source reads are bounded in-process stand-ins, not kernel
services. Inclusive costs contain loops/checksums/assertions and marker calls.
The marked backend body is reported separately, so miss costs cannot be
mistaken for actual OS/IPC/media costs. SP is sampled at instruction boundaries.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OS_ROOT = ROOT.parents[1] / "ZephyrOS"
SDK = OS_ROOT / "build/kos-sdk-0.2.0"
sys.path[:0] = [str(OS_ROOT / "tests"), str(OS_ROOT / "tools")]
import ihx
from z80sim import Z80

NAMES = ["dynamic_hot_read", "dynamic_hot_write", "dynamic_rotate_four",
         "dynamic_rotate_five", "dynamic_dirty_eviction",
         "dynamic_partial_final", "static_partial_final", "static_17bit_rotate"]
ENTRY = """.module cache_entry
.globl _start, _probe_main, _bench_begin, _bench_end, _backend_begin, _backend_end
.area _CODE
_start::
 ld sp, #0xE000
 call _probe_main
 halt
_bench_begin::
 ret
_bench_end::
 ret
_backend_begin::
 ret
_backend_end::
 ret
"""


def command(argv, cwd):
    result = subprocess.run(list(map(str, argv)), cwd=cwd, text=True,
                            capture_output=True)
    if result.returncode or "Undefined Global" in result.stdout + result.stderr:
        raise RuntimeError(f"{' '.join(map(str, argv))}\n{result.stdout}{result.stderr}")


def build(directory, pages, fast=False):
    directory.mkdir(parents=True, exist_ok=True)
    flags = ["-mz80", "-c", "--nostdinc", "--Werror", "-DZVM_PROFILE=0",
             f"-DDYN_CACHE_PAGES={pages}", f"-DSTATIC_CACHE_PAGES={pages}",
             "-I" + str(SDK / "include/abi"), "-I" + str(SDK / "include"),
             "-I" + str(ROOT / "include"), "-I" + str(ROOT / "tests")]
    if fast:
        flags.append("-DZVM_CACHE_ASM=1")
    objects = []
    for source in [ROOT / "tests/story_cache.c", ROOT / "tests/story_cache_z80.c", ROOT / "tests/cacheperf.c",
                   OS_ROOT / "user/sdk/src/reader_full.c"]:
        obj = directory / (source.stem + ".rel")
        command(["sdcc", *flags, source, "-o", obj], directory)
        objects.append(obj)
    (directory / "entry.asm").write_text(ENTRY)
    command(["sdasz80", "-o", "entry.rel", "entry.asm"], directory)
    (directory / "probe.lk").write_text("\n".join([
        "-n", "-m", "-w", "-i " + str(directory / "probe.ihx"),
        "-b _CODE = 0x015D", "-b _DATA = 0x7000",
        str(directory / "entry.rel"), *map(str, objects),
        "-k " + str(SDK / "lib"), "-l libkos", "-e", ""]))
    command(["sdldz80", "-f", "probe.lk"], directory)
    return ihx.parse_map_symbols((directory / "probe.map").read_text())


def pattern(address):
    return (address ^ (address >> 9)) & 255


def expected(pages):
    sums = [sum(pattern(i) for i in range(128)),
            sum(i ^ 0xA5 for i in range(128)),
            sum(pattern((i % 4) * 512 + i) for i in range(128)),
            sum(pattern((i % 5) * 512 + i) for i in range(128)),
            sum(i ^ 0xA5 for i in range(128)),
            128 * pattern(11281), 128 * pattern(131069),
            sum(pattern([65535, 65536, 131069][i % 3]) for i in range(128))]
    misses4 = 128 if pages == 1 else 0
    misses_high = 128 if pages == 1 else 0
    # Each tuple: XMEM reads/writes, reader calls, and their byte totals.
    calls = [(0, 0, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0),
             (misses4, 0, 0, misses4 * 512, 0, 0),
             (128, 0, 0, 128 * 512, 0, 0),
             (128, 128-pages, 0, 128 * 512, (128-pages) * 512, 0),
             (1, 0, 0, 18, 0, 0), (0, 0, 1, 0, 0, 510),
             (0, 0, misses_high, 0, 0,
              sum(510 if i % 3 == 2 else 512 for i in range(misses_high)))]
    # Probe aggregates are uint16_t on the target, including byte totals.
    return [value & 65535 for value in sums], [tuple(value & 65535 for value in row) for row in calls]


def measure(directory, symbols, pages):
    memory = bytearray(65536)
    for address, value in ihx.parse_ihx((directory / "probe.ihx").read_text()).items():
        memory[address] = value
    if symbols["s__CODE"] + symbols["l__CODE"] > 0x7000:
        raise RuntimeError("probe code overlaps data")
    if symbols["s__DATA"] + symbols["l__DATA"] > 0xD000:
        raise RuntimeError("probe data overlaps stack reserve")
    cpu = Z80(bytes(memory), bytes(memory))
    cpu.latch = 0x18
    cpu.pc = symbols["_start"]
    cpu.sp = 0xE000
    active = backend = None
    rows = {}
    def word(address):
        return cpu.rd(address) | cpu.rd(address + 1) << 8
    for _ in range(20_000_000):
        if cpu.rd(cpu.pc) == 0x76:
            break
        if cpu.pc == symbols["_bench_begin"]:
            case = cpu.rd(symbols["_bench_id"])
            if active is not None or case in rows:
                raise RuntimeError("nested/duplicate phase")
            active = (case, cpu.tstates)
            rows[case] = dict(min_SP=cpu.sp, backend_stub_body_Tstates=0)
        if active is not None:
            row = rows[active[0]]
            row["min_SP"] = min(row["min_SP"], cpu.sp)
            if cpu.pc == symbols["_backend_begin"]:
                if backend is not None:
                    raise RuntimeError("nested backend marker")
                backend = cpu.tstates
            if cpu.pc == symbols["_backend_end"]:
                if backend is None:
                    raise RuntimeError("unmatched backend marker")
                row["backend_stub_body_Tstates"] += cpu.tstates-backend
                backend = None
            if cpu.pc == symbols["_bench_end"]:
                if backend is not None:
                    raise RuntimeError("backend crosses phase boundary")
                row["Tstates"] = cpu.tstates-active[1]
                active = None
        if cpu.sp < 0xD000:
            raise RuntimeError("stack exceeded reserved area")
        cpu.step()
    else:
        raise RuntimeError("instruction limit reached")
    if active is not None or set(rows) != set(range(len(NAMES))):
        raise RuntimeError("incomplete phases")
    if word(symbols["_failures"]):
        raise RuntimeError(f"probe assertions failed: {word(symbols['_failures'])}")
    sums, calls = expected(pages)
    counter_names = ["read_calls", "write_calls", "source_calls",
                     "read_bytes", "write_bytes", "source_bytes"]
    result = []
    for case, name in enumerate(NAMES):
        checksum = word(symbols["_checksums"] + case * 2)
        observed = tuple(word(symbols["_"+key] + case*2) for key in counter_names)
        if checksum != sums[case] or observed != calls[case]:
            raise RuntimeError(f"{name}: checksum {checksum}/{sums[case]}, "
                               f"calls {observed}/{calls[case]}")
        row = rows[case]
        row.update(name=name, iterations=128, checksum=checksum,
                   stack_high_water_bytes=0xE000-row["min_SP"],
                   excluding_backend_body_Tstates=row["Tstates"]-row["backend_stub_body_Tstates"],
                   backend=dict(zip(counter_names, observed)))
        result.append(row)
    return dict(pages=pages, code_bytes=symbols["l__CODE"],
                fixture_BSS_bytes=symbols["l__DATA"], cases=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/m35/cache-synthetic")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--asm", action="store_true", help="use the cache-hit assembly path")
    args = parser.parse_args()
    variants = []
    for pages in (1, 4):
        directory = args.build_dir.resolve() / str(pages)
        variants.append(measure(directory, build(directory, pages, args.asm), pages))
    result = dict(compiler="SDCC -mz80", profile=False, cache_assembly=args.asm,
                  abi_cases="cold/hot callback: IX=6a39, exact SP cleanup; invalid bounds/null output; 256 mixed pages",
                  simulator=str(OS_ROOT / "tests/z80sim.py"), initial_SP=0xE000,
                  units="CPU T-states; SDK backend services are in-process stand-ins",
                  failures=0, variants=variants)
    output = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
