# KonaOS cache-hit assembly contract

The accepted target path is `tests/story_cache_z80.c`, enabled by
`ZVM_CACHE_ASM=1` (default). It implements only `story_cache_read` hits on
the most recently used page of the selected dynamic/static partition.
The VM itself remains independent of this host adapter.

## SDCC 4.5 sdcccall(1)

| Item | Contract |
| --- | --- |
| Entry | `zvm_error_t story_cache_read(void *user, uint32_t address, uint8_t *out)` |
| Context | HL |
| Logical address | Four little-endian bytes at entry SP+2..5 |
| Output pointer | Entry SP+6..7 |
| Return | A, ZVM_OK on a hit |
| Cleanup | Callee removes six argument bytes |
| Preserved | IX, caller SP after cleanup |
| Scratch | AF, BC, DE, HL, IY |
| Unprofiled code | 161 bytes, zero BSS |

The routine checks null output and all four address bytes against the
immutable source size. It selects dynamic/static with the actual unaligned
dynamic limit, then extracts bits 9–16 for the page tag. It checks the hot
slot, its tag and validity before addressing the resident page bytes.
It never assumes that logical addresses fit a Z80 pointer.

Every fallback restores entry HL, IX and SP before an ordinary direct JP
to `story_cache_read_slow`. That is the existing C bounds/cache/miss path;
it handles associative hits, replacement, dirty writeback, SDK calls and
errors. The SDK automatic placer recognizes JP targets and can generate
its own veneers; this routine supplies no banking mechanism. Current
placement is null and all addresses stay in the primary bank.

With profiling, the successful path saves live BC/IY and calls the small
C `story_cache_hit(context,address)` counter routine. Its observed ABI
takes context in HL, four stack argument bytes, and removes those bytes
on return. IX is preserved. Counts match the C path exactly.

Compile-time checks require the actual target reader size 8, page metadata
size 3, byte-array offset 12 and hot-index offset `12 + page_count*515`.
A layout change fails the build instead of silently changing assembly.
No aperture pointers, writable static initializers or extra allocations
are introduced. The instructions were independently written for this
project; no historical Infocom code was copied.

## Evidence

- `tests/m35_cache_sim.py` links real SDCC code to the existing Z80 simulator.
  Both 1/4-page variants match all eight phase checksums and backend call
  counts. Cold/hot probes check IX=`6a39` and exact SP cleanup. Bounds, null
  output and 256 mixed logical-page accesses are also asserted.
- A four-page hot-read loop falls from 380,747 to 165,067 model T-states over
 128 operations (loop/call/checksum overhead included). A non-hot page can
  pay more on fallback; this is intentionally not a second full cache engine.
- Ten real Zork commands fall from 65.076 to 52.887 emulated seconds compared
  with the C cache, an 18.73% reduction, before the final CRLF presentation fix.
- Profiled MAME execution matches all VM, cache and transfer counters and
  final state/hash. An independent ABI review found no defect in the hit,
  fallback or profile paths.

The smaller packed-character experiment in `src/zchar.asm` remains disabled
by default. Its isolated text improvement did not translate to meaningful
gameplay improvement; see `m35-asm.md` for that historical evaluation.
