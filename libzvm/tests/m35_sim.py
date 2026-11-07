#!/usr/bin/env python3
"""Compile real libzvm with SDCC and time synthetic workloads in SDK Z80.

No native implementation is linked. --source permits the same current probe
and compiler settings to measure a preserved baseline source tree. Phase costs
include loop control, assertions, checksum accumulation and the balanced marker
RET/CALL overhead. Per-phase min_SP is sampled at instruction boundaries;
stack_high_water_bytes is measured from initial SP E000. Fixture construction
and final error checks are outside the timed phases.
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

NAMES = ["decode", "arithmetic_branch", "stack_bytecode", "call_return",
         "local_read_write", "global_read_write", "logical_memory",
         "packed_text", "dictionary_hit_miss"]
EXPECTED = [384, 384, 1344, 32, 496, 496, 496, 9408, 35328]
ENTRY = """.module m35_entry
.globl _start, _probe_main, _bench_begin, _bench_end
.area _CODE
_start::
 ld sp, #0xE000
 call _probe_main
 halt
_bench_begin::
 ret
_bench_end::
 ret
"""


def run_command(argv, cwd):
    result = subprocess.run(list(map(str, argv)), cwd=cwd,
                            text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, argv))}\n{result.stdout}{result.stderr}")


def build(source, build_dir, use_asm=False):
    build_dir.mkdir(parents=True, exist_ok=True)
    flags = ["-mz80", "-c", "--nostdinc", "--Werror",
             "-I" + str(SDK / "include/abi"), "-I" + str(SDK / "include"),
             "-I" + str(source / "include"), "-I" + str(source / "src")]
    if use_asm:
        flags.append("-DZVM_ASM=1")
    objects = []
    for path in sorted((source / "src").glob("*.c")) + [ROOT / "tests/zvmperf.c"]:
        obj = build_dir / (path.stem + ".rel")
        run_command(["sdcc"] + flags + [path, "-o", obj], build_dir)
        objects.append(obj)
    if use_asm and (source / "src/zchar.asm").exists():
        obj = build_dir / "zchar.rel"
        run_command(["sdasz80", "-o", obj, source / "src/zchar.asm"], build_dir)
        objects.append(obj)
    (build_dir / "entry.asm").write_text(ENTRY)
    run_command(["sdasz80", "-o", "entry.rel", "entry.asm"], build_dir)
    # An explicit data area leaves room for code growth and zero-filled BSS.
    (build_dir / "probe.lk").write_text("\n".join([
        "-n", "-m", "-w", "-i " + str(build_dir / "probe.ihx"),
        "-b _CODE = 0x015D", "-b _DATA = 0xB000",
        str(build_dir / "entry.rel"), *map(str, objects),
        "-k " + str(SDK / "lib"), "-l libkos", "-e", ""]))
    run_command(["sdldz80", "-f", "probe.lk"], build_dir)
    return ihx.parse_map_symbols((build_dir / "probe.map").read_text())


def measure(build_dir, syms):
    mem = bytearray(65536)
    image = ihx.parse_ihx((build_dir / "probe.ihx").read_text())
    for address, value in image.items():
        mem[address] = value
    if syms["s__CODE"] + syms["l__CODE"] > 0xB000:
        raise RuntimeError("code overlaps benchmark data")
    if syms["s__DATA"] + syms["l__DATA"] > 0xD000:
        raise RuntimeError("benchmark data overlaps stack reserve")
    machine = Z80(bytes(mem), bytes(mem))
    machine.latch = 0x18
    machine.sp = 0xE000
    machine.pc = syms["_start"]
    started = None
    elapsed = {}
    min_sp = {}
    def word(address):
        return machine.rd(address) | (machine.rd(address + 1) << 8)
    for _ in range(20_000_000):
        if machine.rd(machine.pc) == 0x76:
            break
        if machine.pc == syms["_bench_begin"]:
            case = machine.rd(syms["_bench_id"])
            if started is not None or case in elapsed:
                raise RuntimeError("duplicate or nested phase")
            started = (case, machine.tstates)
            min_sp[case] = machine.sp
        if started is not None:
            min_sp[started[0]] = min(min_sp[started[0]], machine.sp)
        if machine.sp < 0xD000:
            raise RuntimeError("benchmark exceeded reserved stack area")
        if machine.pc == syms["_bench_end"]:
            case = machine.rd(syms["_bench_id"])
            if started is None or started[0] != case:
                raise RuntimeError("unmatched phase end")
            elapsed[case] = machine.tstates - started[1]
            started = None
        machine.step()
    else:
        raise RuntimeError("Z80 instruction limit reached before HALT")
    failures = word(syms["_failures"])
    if failures:
        raise RuntimeError(f"Z80 benchmark assertions failed: {failures}")
    if started is not None or set(elapsed) != set(range(len(NAMES))):
        raise RuntimeError("incomplete benchmark phases")
    rows = []
    for case, name in enumerate(NAMES):
        checksum = word(syms["_checksums"] + case * 2)
        if checksum != EXPECTED[case]:
            raise RuntimeError(f"{name}: checksum {checksum}, expected {EXPECTED[case]}")
        rows.append(dict(name=name, iterations=32, Tstates=elapsed[case],
                         checksum=checksum, min_SP=min_sp[case],
                         stack_high_water_bytes=0xE000-min_sp[case]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/m35/synthetic")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--asm", action="store_true",
                        help="define ZVM_ASM=1 and assemble src/zchar.asm when present")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    syms = build(args.source.resolve(), build_dir, args.asm)
    result = dict(source=str(args.source.resolve()), compiler="SDCC -mz80",
                  simulator=str(OS_ROOT / "tests/z80sim.py"),
                  assembly=args.asm, initial_SP=0xE000,
                  failures=0, cases=measure(build_dir, syms))
    output = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
