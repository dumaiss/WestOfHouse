#!/usr/bin/env python3
"""Evaluate an independent Z80 packed-character candidate; no VM core edits.

Run from libzvm: python3 tests/m35_unpack.py
Uses ZephyrOS's unchanged SdkProbe build helper and Z80 simulator.
"""

import argparse
import collections
import hashlib
import json
from pathlib import Path
import random
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OS = ROOT.parent.parent / "ZephyrOS"

UNPACK_ASM = """
    .module m35_unpack
    .globl _unpack_asm, _unpack_asm_end
    .area _CODE
; uint8_t unpack_asm(uint16_t word, uint16_t slot): HL, DE -> A.
; Valid slots 0..2. IX and SP preserved. Other registers/flags scratch.
_unpack_asm::
    ld a,e
    or a
    jr z,unpack_first
    dec a
    jr nz,unpack_last
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,h
    and #31
    ret
unpack_first:
    ld a,h
    rrca
    rrca
    and #31
    ret
unpack_last:
    ld a,l
    and #31
    ret
_unpack_asm_end::
"""


def wrappers():
    source = """
    .module m35_unpack_checks
    .globl _unpack_c, _unpack_c_end, _unpack_asm
    .globl _checked_c, _checked_asm, _abi_failure
    .globl _bench_c, _bench_asm, _repeat_c, _repeat_asm
    .globl _bench_c_begin, _bench_c_end, _bench_asm_begin, _bench_asm_end
    .globl _checked_c_call, _checked_c_after
    .globl _checked_asm_call, _checked_asm_after
    .area _DATA
saved_sp: .ds 2
saved_a: .ds 1
    .area _CODE
"""
    for kind in ("c", "asm"):
        source += f"""
_checked_{kind}::
    push ix
    ld ix,#0x6a39
    ld (saved_sp),sp
_checked_{kind}_call::
    call _unpack_{kind}
_checked_{kind}_after::
    ld (saved_a),a
    ld hl,#0
    add hl,sp
    ld de,(saved_sp)
    or a
    sbc hl,de
    jr nz,failed_{kind}
    push ix
    pop hl
    ld de,#0x6a39
    or a
    sbc hl,de
    jr z,okay_{kind}
failed_{kind}:
    ld a,#1
    ld (_abi_failure),a
okay_{kind}:
    ld sp,(saved_sp)
    ld a,(saved_a)
    pop ix
    ret
_bench_{kind}::
_bench_{kind}_begin::
    call _repeat_{kind}
_bench_{kind}_end::
    ret
"""
    return source


def probe_source(words):
    count = len(words)
    source = f"""
#include "kos/types.h"
uint8_t unpack_asm(uint16_t word,uint16_t slot);
uint8_t checked_c(uint16_t word,uint16_t slot);
uint8_t checked_asm(uint16_t word,uint16_t slot);
void bench_c(void);
void bench_asm(void);
uint8_t unpack_c(uint16_t word,uint16_t slot)
{{return (uint8_t)((word >> (10u-5u*slot))&31u);}}
void unpack_c_end(void) {{}}
const uint16_t words[{count}]={{{','.join(str(w)+'u' for w in words)}}};
uint8_t results_c[{count * 3}],results_asm[{count * 3}];
uint8_t abi_failure;
volatile uint8_t sink_c,sink_asm;
"""
    for kind in ("c", "asm"):
        source += f"""
void repeat_{kind}(void)
{{
    uint16_t n;
    for(n=0;n<256u;++n){{
        sink_{kind}^=unpack_{kind}(n,0u);
        sink_{kind}^=unpack_{kind}(n,1u);
        sink_{kind}^=unpack_{kind}(n,2u);
    }}
}}
"""
    source += f"""
void probe_main(void)
{{
    uint16_t n,slot,at=0;
    abi_failure=0;sink_c=0;sink_asm=0;
    for(n=0;n<{count}u;++n)for(slot=0;slot<3u;++slot){{
        results_c[at]=checked_c(words[n],slot);
        results_asm[at]=checked_asm(words[n],slot);
        ++at;
    }}
    bench_c();bench_asm();
}}
"""
    return source


def run_profile(probe, ihx, Z80):
    image = ihx.parse_ihx(Path(probe.path("probe.ihx")).read_text())
    memory = bytearray(0x10000)
    for address, byte in image.items():
        memory[address] = byte
    machine = Z80(bytes(memory), bytes(memory))
    machine.latch = 0x18
    machine.sp = 0xE000
    machine.pc = probe.syms["_start"]
    calls = {kind: collections.defaultdict(list) for kind in ("c", "asm")}
    batches = {}
    open_calls = {}
    open_batches = {}
    for steps in range(10_000_000):
        for kind in ("c", "asm"):
            if machine.pc == probe.syms[f"_checked_{kind}_call"]:
                open_calls[kind] = (machine.e, machine.tstates)
            if machine.pc == probe.syms[f"_checked_{kind}_after"]:
                slot, start = open_calls.pop(kind)
                calls[kind][slot].append(machine.tstates - start)
            if machine.pc == probe.syms[f"_bench_{kind}_begin"]:
                open_batches[kind] = machine.tstates
            if machine.pc == probe.syms[f"_bench_{kind}_end"]:
                batches[kind] = machine.tstates - open_batches.pop(kind)
        if machine.rd(machine.pc) == 0x76:
            probe.machine = machine
            return calls, batches, steps
        machine.step()
    raise AssertionError("probe exceeded instruction budget")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--os-root", type=Path, default=DEFAULT_OS)
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build/m35/unpack-baseline.json")
    args = parser.parse_args()
    sys.path[:0] = [str(args.os_root / "tests"), str(args.os_root / "tools")]
    import test_stdio as sdk
    import ihx
    from z80sim import Z80

    rng = random.Random(0x3505)
    words = [0, 1, 31, 32, 33, 1023, 1024, 1025, 0x7fff,
             0x8000, 0x8001, 0xfffe, 0xffff, 0x5555, 0xaaaa]
    for bit in range(16):
        words.extend([1 << bit, 0xffff ^ (1 << bit)])
    words.extend(rng.randrange(65536) for _ in range(512))
    scratch = ROOT / "build/m35/unpack"
    scratch.mkdir(parents=True, exist_ok=True)
    probe = sdk.SdkProbe(str(scratch), [], probe_source(words),
                         [UNPACK_ASM, wrappers()]).build()
    calls, batches, steps = run_profile(probe, ihx, Z80)
    expected = bytes((word >> (10 - 5 * slot)) & 31
                     for word in words for slot in range(3))
    for kind in ("c", "asm"):
        base = probe.syms[f"_results_{kind}"]
        actual = bytes(probe.machine.rd(base + index)
                       for index in range(len(expected)))
        assert actual == expected, f"{kind} differs from reference"
        for slot in range(3):
            assert len(calls[kind][slot]) == len(words)
    assert probe.byte("_abi_failure") == 0, "SP or IX preservation failed"
    assert probe.machine.sp == 0xE000, "final stack pointer differs"
    assert probe.byte("_sink_c") == probe.byte("_sink_asm")
    report = {
        "scope": "isolated candidate evaluation; no production integration",
        "compiler": sdk._run([sdk.SDCC, "--version"]).stdout.splitlines()[0],
        "simulator": str(args.os_root / "tests/z80sim.py"),
        "simulator_sha256": hashlib.sha256(
            (args.os_root / "tests/z80sim.py").read_bytes()).hexdigest(),
        "timing_limits": "Existing simulator timing estimates; conditional branches use taken costs and unlisted opcodes have a 4-T-state floor.",
        "words": len(words), "random_words": 512,
        "differential_cases": len(expected),
        "abi": {"arguments": "HL=word, DE=slot (uint16_t)",
                "return": "A=uint8_t", "valid_slots": [0, 1, 2],
                "preserved": ["IX", "SP"], "sentinel": "0x6a39",
                "checks": "SP and IX checked after every C and ASM call"},
        "code_bytes": {kind: probe.syms[f"_unpack_{kind}_end"] -
                       probe.syms[f"_unpack_{kind}"] for kind in ("c", "asm")},
        "caller_tstates_by_slot": {
            kind: {str(slot): {"min": min(values), "max": max(values),
                               "mean": sum(values) / len(values),
                               "calls": len(values)}
                   for slot, values in sorted(calls[kind].items())}
            for kind in ("c", "asm")},
        "repeated_three_character_loop": {
            "iterations": 256, "helper_calls_each": 768,
            "includes": "CALL repeat, loop, input setup, volatile XOR sink, three helper calls, RET repeat",
            "tstates": batches,
            "saved_tstates": batches["c"] - batches["asm"],
            "speedup": batches["c"] / batches["asm"]},
        "pass": True, "simulated_instructions": steps,
        "asm_sha256": hashlib.sha256(UNPACK_ASM.encode()).hexdigest(),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
