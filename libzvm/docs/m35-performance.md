# M3.5 performance and acceptance

## Outcome

The ten-command Zork workload fell from **359.639 to 52.855 emulated
seconds: 6.80× faster, an 85.30% reduction**. Story preparation fell from
34.878 to 3.829 seconds. Startup after preparation fell from 10.734 to 2.160
seconds. The original 86838-byte Zork story and KonaOS ROM are unchanged.

The accepted implementation combines the existing C VM, a four-page dynamic
cache, a four-page static cache, direct SDK reader transfers, and a bounded
Z80 cache-hit path. Cache misses, dirty writeback, execution semantics,
text processing and host events remain in C. All production changes are in
WestOfHouse/libzvm. No OS, SDK, emulator or original story source was changed.

Commands still take 3.6–6.1 seconds at the emulated 10 MHz target speed. This
is a substantial improvement, but remains slow for a polished graphical
frontend. M4 correctness and persistence work can begin against this state;
frontend responsiveness should remain a separate measured acceptance concern.

## Method and preserved evidence

The timer measures 25.6 µs counts with unsigned subtraction. `load` includes
open, dynamic allocation/staging, original-source checksum and VM setup.
`startup` is VM execution to its first input request. Each input phase starts
before submission/tokenization and ends at the next input request, including
output flushing. Human/keyboard delays and benchmark printing are excluded.
Timer-pair calibration is reported without subtraction (about 46–55 counts).
Timer failures print `unavailable`; no timer call is made per instruction.

Measurements use the same ROM, MAME binary, story and deterministic commands:
`look`, `open mailbox`, `take leaflet`, `read leaflet`, `inventory`, `score`,
`look`, `examine mailbox`, `close mailbox`, `open mailbox`. Normal and
counting/discard modes both execute text decoding and hash raw VM output
bytes before presentation. No native-PC VM or new emulator was introduced.

MAME flags were `-nothrottle -sound none -video none`. These remove host
throttling/rendering; emulated timer counts still measure target execution.
Concurrent host workloads changed MAME speed, so host wall time is not used
for speedup. The final normal+discard run occupied 197.09 host seconds and
returned at 129.584 emulated seconds, including boot/diagnostics. The repeat
interactive run took 65.15 host seconds and returned at 33.293 emulated seconds.
These host figures are observations, not comparable baseline benchmarks.

Preserved under `build/m35/`:

- `baseline-source.tar.gz`: original M3 source; `baseline.exe` and report.
- `baseline-harness-source.tar.gz`: neutral timing/counter harness.
- `baseline-v2/` and `baseline-profile-v2/`: full frozen SDK artifact layouts.
- `environment.sha256`: hashes for story, ROM and MAME.
- `run-baseline-final/console.txt`: complete normal baseline; its following
  discard run was interrupted and is not used for command comparisons.
- `run-baseline-discard/console.txt`: complete discard baseline.
- `run-baseline-profile-final/console.txt`: complete profiled normal baseline.
- `run-cache1-c/`, `run-cache4-c/`, `run-cache4-profile/`: isolated C variants.
- `run-cache4-asm/`: rejected packed-text assembly experiment.
- `run-cache-fast/`, `run-cache-fast-profile/`: accepted cache ASM A/B and
  profiled stack diagnostic; `run-final/`: final presentation/resource build.
- `run-interactive2/` and `run-interactive3/`: actual SDK keyboard input.
- `synthetic-{baseline,current,asm}.json`, `cache-synthetic{,-fast}.json`:
  target CPU probes using the existing ZephyrOS simulator.

Some early MAME invocations failed in the sandbox, had incomplete artifact
layouts, or were interrupted. They are not accepted runs. The existing SDK
session plugin also needs its original command row to remain visible; long
output scrolls it away. `tests/m35_finish.lua` extends that runner using its
existing console API and requires both the application completion marker and
the returned shell prompt. Final runs have SDK `result.txt` status `ran` and
tool exit 0. The original baseline counters/output are retained even where
that old scrolling limitation prevented automatic session exit.

The old adapter checked argv[1] for flags, while the SDK supplies argument
tail argv[0]. This was fixed in the neutral harness before baseline capture;
otherwise -a/-p/-b would silently select the wrong path.

## Diagnosis and selected packages

| Priority | Evidence before implementation | Action and risk |
| --- | --- | --- |
|1: paging/XMEM churn | Startup: 407 instructions, 219 XMEM reads, 74 writes and 44 reader requests; 11.137 profiled seconds. Turn 2 alone caused roughly 545 reads/196 writes/176 reader requests for 1518 instructions. | Four resident pages per partition, associative lookup and bounded round-robin. Risks: dirty coherence, partial fills, 17-bit addresses and shared boundary. |
|2: source CPU/protocol overhead | The stdio adapter uses `fread(dst,1,n)`, with a per-item loop even when lower reads are buffered. Startup staging/checksum issued 193 application fread requests. | Borrowed `kos_reader_from_handle`, descriptor-sized staging/checksum reads, cache storage reused as scratch. No assumption of one syscall per byte. |
|3: cached-read CPU | After paging improvements, 84,420 of 85,547 cache accesses hit. Simulator hot reads still cost roughly 3,000 T each. | Assembly handles bounds-checked current-page hits; every other path restores the ABI and tail-calls C. Whole-game A/B acceptance required. |
|4: packed text | A small shift helper improved the isolated text phase 4.69%. | Evaluated but disabled by default: only 0.023% across the ten commands. |
|5: console | Discarding output saves about 0.6 seconds across ten commands, roughly 1.1% of the final run. | Existing SDK128-byte buffering retained. Additional console buffering was not justified. CRLF conversion stays in the host adapter and fixes diagonal output. |

Decoder, dispatch, operand extraction, stack/variables, frames, dictionary
and logical-memory paths were reviewed. Dispatch already uses ordinary
switch/direct calls; no handler function-pointer table pins the whole engine.
Dictionary work is only 16 lookups/258 byte comparisons here, so no large index
was added. A fetch cursor would require an additional backend contract and
invalidation rules; it is deferred rather than folded into this milestone.

## Correctness and architecture review

- The existing eight-case target core fixture also passes in MAME.
  M1–M3 host regressions pass in C, accepted ASM, and profiling configurations;
  target SDK executables pack and validate with `--Werror` compilation.
- Cache tests cover 50,000 mixed operations against authoritative arrays,
  dirty eviction/flush, failed read/write and partial fills, short source
  reads, last partial pages, the 11282 unaligned split, readonly static data,
  addresses 65535/65536/131069 and source immutability. 1+1, 4+4 and mixed 1+4
  configurations passed. ASan/UBSan passed; leak checking was disabled for
  the ptrace environment and the implementation allocates no heap memory.
- Target cache probes compare C/ASM checksums and backend calls in eight
  phases with 1/4 pages. Cold/hot calls preserve IX=`6a39` and exact SP cleanup;
  null output, out-of-range addresses and 256 mixed pages are checked too.
- All gameplay variants produce 1206 raw bytes with hash 46392838, final
  room 64/score 0/moves 9. Profile variants match all 21 VM counters, including
  14868 instructions, 62518 fetches, 525 calls/493 returns, 4818 branches and 93
  abbreviation expansions. The cache ASM also matches the C cache counters.
- Interactive acceptance sends `look`, `open mailbox`, `quit`, `y` through
  the existing MAME plugin's USB keyboard queue and actual `console_readline`.
  It observes the leaflet, quit confirmation, story finish and shell return.
- No new public VM ABI or TUI/console dependency was added. The optimized
  memory adapter is in `tests/story_cache*`, ready for host integration.
  Readers borrow immutable source handles; dynamic state remains writable
  cache/XMEM data. Checksum computation always reads the original source.
- All copied buffers/pointers are resident primary-bank storage. No mapped
  aperture pointer, physical latch access or custom bank-call mechanism is
  used. Failed dirty writeback preserves the old dirty page; failed fill is
  invalid before publication. Cleanup flush errors now fail the test app.
- New function addresses are host read/write callbacks and SDK reader
  callbacks; ordinary calls and SDK placement handle executable residency.
  Both final placement files are `null`: no placement is needed, with zero
  far segments and veneers. `_INITIALIZED`/`_INITIALIZER` are zero. Supported
  SDK multiplication helpers resolve; an unsupported mixed-width multiply
  discovered in a synthetic fixture was replaced with addition.

## Stack and bank resources

The old 256-byte C stack was insufficient: generated frames/arguments already
exceed 266 bytes before the reader's SDK calls. The EXE reservation is now 512
bytes, with the same E000 stack top. The separate target canary build observed
420 written bytes on the C-cache path and 444 on the profiled ASM path; the low
16-byte guard stayed intact. This is a **write-watermark lower bound**, not a
continuous minimum-SP proof: untouched allocations and A5 collisions can hide
usage. The profile run leaves 68 bytes of observed headroom. Synthetic VM
phases reached 214 bytes, but exclude this full host call chain.

Runtime INFO reports one plain XMEM object: 11282 requested, 16384 allocated,
flags 0/type 1. It consumes two 8 KiB slots. The four dynamic and four static
resident pages use 4096 bytes; the unprofiled adapter struct is 4136 bytes
(4177 profiled). The VM's public opaque storage remains 1536 bytes; target
state uses 960 bytes (1044 profiled), including 128 evaluation words (256 bytes)
and 16 frames (640 bytes). The story source stays on disk; no full-story XMEM
copy is required.

The observed process bank is 1 and valid-bank mask 127. **That mask is not a
free-bank count.** The SDK does not expose exact remaining system objects or
free slots, and XMEM placement is intentionally opaque. Allocator banks 0–5
and their seven slots compete with other processes; this executable adds
one primary bank, one auxiliary object/two slots, and no far-code/mappable
segments. The 32 KiB plain-object limit and the 32 systemwide-object cap apply;
16 KiB mappable limits would apply to future aperture objects. No exact
systemwide free capacity or fast-slot residency is claimed.

## Reproduction

```sh
make test
make test ZVM_PROFILE=1
make test ZVM_CACHE_ASM=0
python3 tests/m35_sim.py --out build/m35/synthetic-current.json
python3 tests/m35_cache_sim.py --out build/m35/cache-synthetic.json
python3 tests/m35_cache_sim.py --asm --build-dir build/m35/cache-synthetic-fast --out build/m35/cache-synthetic-fast.json
make bench-both ZVM_MAME_ARGS='-nothrottle -sound none -video none -autoboot_script /home/kitamura/Documents/WestOfHouse/libzvm/tests/m35_finish.lua -autoboot_delay 0'
python3 tests/m35_report.py build/m35/run-baseline-final/console.txt build/m35/run-final/console.txt --require-complete --mode normal --out build/m35/final-comparison.json
python3 tests/m35_report.py build/m35/run-baseline-discard/console.txt build/m35/run-final/console.txt --require-complete --mode discard --out build/m35/final-discard-comparison.json
```

Set the SDK's KOS_MAME/KOS_FLASH/KOS_MAME_PLUGINS paths for make run targets.
MAME needed host execution outside this session's sandbox; no OS changes
were required. Build variants have separate directories for cache sizes,
profiling, ASM and stack diagnostics. `ZVM_CACHE_ASM=0` selects the C
reference; `ZVM_ASM=1` is only the rejected packed-text experiment.

## Remaining work worth measuring

Sequential instruction fetch still crosses the logical-memory callback and
bounds layer 62518 times. A resident fetch cursor/optional block-reader
contract is the next CPU candidate, with explicit page, branch/call/return
and write-coherence rules. First measure it against this accepted backend.
Remaining XMEM service/copy costs should also be isolated before considering
two smaller dynamic objects: contiguous plain allocations and documented
fast-slot preferences do not guarantee physical placement. Avoid adding
more caches, a dictionary hash or console buffers without evidence.

The core nine synthetic phases remain unchanged because accepted changes
are in the platform memory adapter, rather than opcode semantics. This is
consistent with prioritizing measured memory/IPC costs. Do not extrapolate
the simulator's isolated speedups to whole-game throughput.

## Multi-agent execution

The supervisor retained architecture, cache policy, acceptance and final
integration. Worker settings below are explicit spawn configurations; the
supervisor's underlying model/effort were not independently verified.

| Worker | Actual model / effort | Ownership and result |
| --- | --- | --- |
|profiling |gpt-6.1-sol / medium | Optional counters, target harness, Makefile variants and argv correction; completed before backend changes. |
|cpu_audit |gpt-6.1-sol / high | Read-only ABI/dispatch/ZIP80 audit, packed-text candidate/tests; later independent cache ASM and stack review. |
|io_audit |gpt-6.1-sol / high | SDK I/O audit, bounded C cache/reader package and focused tests; later cache simulator fixture and interactive contract review. |
|synthetics |gpt-6-luna / medium | Initial fixture attempt failed without deliverable; escalated. |
|synthetics_sol |gpt-6.1-sol / medium | Real-SDCC nine-phase VM fixture using the existing simulator; expected state/checksums and SP accounting. |

Cache work was authorized after baseline measurements, with ownership of
tests/zvmtest.c and new story_cache files; the supervisor integrated
Makefile changes after profiler/CPU handoff. Root then implemented and
accepted the cache-hit ASM after its measured C cost, with independent CPU
review. Root repaired fixture counter wrap/unsupported multiply and added
target ABI cases. Usage limits interrupted two worker followups; completed
work was preserved and resumed. No two workers edited the same active
execution path. ZIP80 was an architectural reference only; no restricted
Infocom interpreter source was copied into these routines.

## Measured tables

The following tables are generated from the preserved captures and SDK maps.

Durable parsed records are in [m35-data/results.json](m35-data/results.json).
[m35-data/evidence.tar.gz](m35-data/evidence.tar.gz) preserves raw captures,
baseline source/executable artifacts, final sources, maps and test logs so
the evidence survives `make clean`. It contains no copy of the Zork story.

<!-- BEGIN GENERATED RESULTS -->

### Unprofiled target time

| Phase | Baseline seconds | Final seconds | Speedup | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Story preparation | 34.878 | 3.829 | 9.11× | 89.02% |
| Startup to input | 10.734 | 2.160 | 4.97× | 79.88% |
| look | 31.841 | 5.204 | 6.12× | 83.66% |
| open mailbox | 40.325 | 5.606 | 7.19× | 86.10% |
| take leaflet | 46.793 | 6.141 | 7.62× | 86.88% |
| read leaflet | 41.429 | 5.828 | 7.11× | 85.93% |
| inventory | 26.479 | 4.538 | 5.83× | 82.86% |
| score | 20.232 | 3.607 | 5.61× | 82.17% |
| look | 31.786 | 5.150 | 6.17× | 83.80% |
| examine mailbox | 41.650 | 5.705 | 7.30× | 86.30% |
| close mailbox | 39.188 | 5.534 | 7.08× | 85.88% |
| open mailbox | 39.917 | 5.542 | 7.20× | 86.12% |
| **Ten commands** | **359.639** | **52.855** | **6.80×** | **85.30%** |

### Separating changes

| Unprofiled ten commands | Normal seconds | Discard seconds |
| --- | ---: | ---: |
| Baseline stdio / one page each | 359.639 | 359.013 |
| Direct handle / one page each | 121.756 | 121.113 |
| Direct handle / four pages each, C | 65.076 | 64.450 |
| Four pages / cache-hit ASM | 52.887 | 52.257 |
| Final with CRLF / resource diagnostics | 52.855 | 52.259 |

Direct I/O with one page reduced normal command time by 66.14%. Four pages then reduced it by 46.55%. Cache-hit assembly reduced the C-cache command time by 18.73% (1.230×). These stages are measured separately; they are not additive percentages.

Normal versus discard on the final build differs by 0.596 s over ten commands. Discard also skips status presentation, so this bounds the combined host-output contribution; it is not an isolated renderer timing. Final VM profiling adds 27.07% to the normal workload and 27.38% in discard mode. Baseline profiling adds 2.68%. Profiling changes code/layout too; unprofiled runs determine the reported speedup.

### Paging and requested SDK transfers

| Aggregate through startup + ten commands | Baseline | Final |
| --- | ---: | ---: |
| dyn_hits | 11,071 | 15,949 |
| dyn_misses | 5,492 | 614 |
| dyn_evictions | 5,491 | 610 |
| dyn hit rate | 66.84% | 96.29% |
| static_hits | 67,404 | 68,471 |
| static_misses | 1,580 | 513 |
| static_evictions | 1,579 | 509 |
| static hit rate | 97.71% | 99.26% |
| xread_calls | 5,492 | 614 |
| xread_bytes | 2,798,566 | 305,476 |
| xwrite_calls | 1,945 | 217 |
| xwrite_bytes | 995,346 | 120,850 |
| reader_calls | 1,582 | 540 |
| reader_bytes | 808,964 | 360,716 |

Cache stats count logical accesses. Transfer snapshots are cumulative and include preparation; final cleanup writeback is outside the command timings. SDK xread/xwrite request counts are exact adapter calls. `reader_calls` counts read-at requests, not physical SD faults or all IPC: baseline stdio has hidden refills; direct handle reads use seek + read. Physical SD fault counts are unavailable and are not invented. During preparation, application fread requests fell from 193 to 0, direct read-at requests were 27, and staging XMEM writes fell from 23 to 3. The immutable bytes/checksum are the same.

| Command | Baseline XMEM calls | Final XMEM calls | Baseline read-at calls | Final read-at calls |
| --- | ---: | ---: | ---: | ---: |
| look | 724 | 87 | 131 | 44 |
| open mailbox | 741 | 89 | 176 | 54 |
| take leaflet | 799 | 100 | 208 | 59 |
| read leaflet | 739 | 86 | 181 | 56 |
| inventory | 640 | 60 | 107 | 45 |
| score | 573 | 20 | 77 | 30 |
| look | 715 | 79 | 131 | 44 |
| examine mailbox | 742 | 83 | 183 | 58 |
| close mailbox | 734 | 87 | 170 | 53 |
| open mailbox | 737 | 84 | 174 | 52 |

### Code and primary-bank footprint

| Measurement | Baseline harness | Final | Final profile |
| --- | ---: | ---: | ---: |
| EXE file bytes | 44,289 | 42,057 | 45,557 |
| Whole primary CODE bytes | 41,767 | 39,425 | 42,396 |
| VM archive CODE bytes | 21,029 | 21,029 | 21,693 |
| BSS bytes | 8,984 | 9,405 | 9,464 |
| C stack reservation | 256 | 512 | 512 |
| Primary free gap below reserved stack | 3,595 | 5,150 | 1,591 |
| Far segments / veneers | 0 / 0 | 0 / 0 | 0 / 0 |

The primary gap is image/BSS end to declared stack base, not a malloc heap or systemwide free-memory query. The profile build has less headroom and is diagnostic only. The 161-byte cache-hit helper adds no BSS. Public VM storage is unchanged.

### CPU probes

VM probes use 32 iterations, real encoded instructions/state assertions, and the existing simulator. C baseline/final core results are identical.

| VM phase | Baseline/final T-states | Observed stack bytes |
| --- | ---: | ---: |
| decode | 307,630 | 116 |
| arithmetic_branch | 1,327,790 | 178 |
| stack_bytecode | 1,353,186 | 180 |
| call_return | 1,388,482 | 214 |
| local_read_write | 61,154 | 62 |
| global_read_write | 173,826 | 84 |
| logical_memory | 68,834 | 64 |
| packed_text | 330,918 | 137 |
| dictionary_hit_miss | 1,363,074 | 127 |

| Cache probe, 4 + 4 pages, 128 operations | C T-states | ASM T-states |
| --- | ---: | ---: |
| dynamic_hot_read | 380,747 | 165,067 |
| dynamic_hot_write | 373,875 | 373,875 |
| dynamic_rotate_four | 490,119 | 577,415 |
| dynamic_rotate_five | 1,093,245 | 1,180,384 |
| dynamic_dirty_eviction | 1,589,845 | 1,374,165 |
| dynamic_partial_final | 380,874 | 167,404 |
| static_partial_final | 378,798 | 156,719 |
| static_17 bit_rotate | 469,190 | 547,470 |

Cache figures exclude the marked in-process backend body but retain call/lookup/loop/checksum overhead. They do not measure OS, banking, IPC or SD service time. Only the current hot page is accelerated; rotating non-hot pages pay the checked fallback overhead and can regress in isolation. Whole-game timing justified acceptance. The unchanged simulator estimates taken conditional branches and applies a four-T-state floor to unlisted opcodes; these are model estimates, not hardware cycle measurements.

<!-- END GENERATED RESULTS -->
