# M3.5 packed-character assembly evaluation

This is the historical packed-text experiment. It remains disabled by
default: full gameplay improved only 0.023% across ten commands. The accepted
cache-hit assembly path is documented in [m35-cache-asm.md](m35-cache-asm.md).
Current build directories include cache sizes and stack/ASM variants; the
directory table below describes the earlier isolated experiment.

`ZVM_ASM=1` selects one Z80 helper in `src/zchar.asm`. The default remains
`ZVM_ASM=0` while gameplay A/B evaluation is pending. `src/text.c` calls it
only when both SDCC and the assembly build flag are present; host builds
retain the original C expression.

The helper replaces only `(*word >> (10u - 5u * *slot)) & 31u` inside
`get_z`. C still handles story reads, callback errors, end bits, cursor
updates, slot advancement, abbreviations and output. No VM state layout,
decoder behavior or public API changes are involved.

## ABI

The internal signature is `uint8_t zvm_unpack(uint16_t word, uint16_t slot)`.
It uses the observed SDCC 4.5.0 Z80 `sdcccall(1)` convention:

| Item | Contract |
| --- | --- |
| Symbol | `_zvm_unpack` |
| Word | `HL` |
| Slot | `DE`, valid values 0, 1 or 2 |
| Result | `A`, value 0 through 31 |
| Preserved | `IX`, `SP` |
| Scratch | Other registers and flags |
| Stack arguments | None |
| Code/data | 24 bytes of `_CODE`; no data or BSS |

The 16-bit slot deliberately keeps both arguments in registers. `get_z`
provides only valid slots after its word-reload check. The helper does not
define behavior for invalid slots. The mask excludes the packed word's
end bit in every slot.

For slot 0, rotating the high byte right twice and masking selects bits
10–14. For slot 1, shifting `HL` left three times and masking its high byte
selects bits 5–9. Slot 2 masks the low byte. These instructions were written
independently for this project; no Infocom source was copied or translated.

The historical architectural reference was
[Infocom ZIP80](https://github.com/erkyrath/infocom-zcode-terps/blob/master/cpm80/zorkcpm.asm),
blob `507a267af5ce8994036746b3b4291bca0089579c`. Its cursor/page structure
informed the audit, not this implementation. Its existing copyright and
distribution notice supplies no permissive reuse grant.

## Reproducing the comparison

```sh
python3 tests/m35_unpack.py
python3 tests/m35_sim.py --asm --out build/m35/synthetic-asm.json --build-dir build/m35/synthetic-asm
make test ZVM_ASM=1
make zvmtest ZVM_ASM=1 ZVM_PROFILE=1
```

The build directories prevent stale compiler flags from crossing variants:

| `ZVM_PROFILE` | `ZVM_ASM` | Directory |
| --- | --- | --- |
| 0 | 0 | `build` |
| 1 | 0 | `build/profile` |
| 0 | 1 | `build/asm` |
| 1 | 1 | `build/profile-asm` |

The Makefile assembles the member with `sdasz80` and conditionally adds it
to `libzvm.lib`. The existing SDK placer handles its ordinary direct call;
the implementation uses no generic SDCC bank-call convention.

## Measured results

The isolated test passed 559 words × three slots: 1,677 comparisons against
the original expression and the host reference. Inputs include 512
deterministic random words, every single-bit position and complement,
bit 15 and field boundaries. An assembly wrapper checks `IX=6a39h` and the
saved `SP` after each C and assembly call. Final SP was `E000h`.
Results are recorded in `build/m35/unpack-baseline.json`.

| Caller cost with arguments ready, including CALL/RET | C | Assembly |
| --- | ---: | ---: |
| Slot 0 | 450 T | 66 T |
| Slot 1 | 290 T | 107 T |
| Slot 2 | 130 T | 74 T |
| Helper code | 24 bytes | 24 bytes |

A 256-iteration loop extracting all three characters includes argument
setup, 768 helper calls, loop control and a volatile XOR sink. It cost
280,101 T with C versus 120,613 T with assembly, a 2.32× isolated speedup.

The integrated `tests/zvmperf.c` packed-text phase prints `abc` 32 times
through the real VM text path and output callback. Comparing
`build/m35/synthetic-baseline.json` with `build/m35/synthetic-asm.json`:

| Integrated phase | Baseline C | Assembly |
| --- | ---: | ---: |
| Packed text | 330,918 T | 315,398 T |
| Output checksum | 9,408 | 9,408 |
| Stack high-water mark | 137 bytes | 137 bytes |

That phase saves 15,520 T, or 4.69%. All other eight synthetic phases have
identical timings, checksums and stack high-water marks. The host suite
passed with `make test ZVM_ASM=1`, and the target executable packed and
validated successfully.

The unprofiled target comparison has 41,767 versus 41,776 bytes of `_CODE`
and 8,984 bytes of BSS in both variants. The executable grows from 44,289
to 44,298 bytes. The 24-byte helper replaces inline extraction code, so
total code grows by nine bytes. Placement is direct: `placement.json` is
null, there are no auxiliary segments or generated veneers, and the helper
appears as `_zvm_unpack` in the link map.

The profile build also packs and validates: its executable grows from
47,609 to 47,618 bytes, `_CODE` from 44,558 to 44,567 bytes, with BSS
unchanged at 9,042 bytes. It likewise has null placement, zero auxiliary
segments and no generated veneers. The declared pointer-member closure
continues to include `text.rel`; no additional placement pin was needed.

These are estimates from the unchanged ZephyrOS `tests/z80sim.py` timing
model. Conditional branches receive their taken costs and missing timing
entries receive a four-T-state floor. They establish a local text-path
benefit; gameplay speedup is unmeasured here. The remaining acceptance
work is the supervisor's identical-workload gameplay comparison, including
output modes and profile counters. More complex packed-text behavior,
placement changes under future size pressure, and added call overhead
must continue to be checked during integration.
