# State reporting acceptance — 2026-11-08

The installed ZILf/ZAPF compiled the generated resolver for all **99 ready
rooms**. The other **11 rooms remain incomplete** and report zero. No VM core,
OS, SDK or emulator source was changed. The three vendor source changes are
the appended global/include and the two shared normal-input calls.

## Results

| Check | Result |
| --- | --- |
| Initial room | West of House `(180, 1)` |
| Movement | North of House `(81, 1)`, East of House `(79, 1)` |
| Kitchen window | Opening gives `(79, 2)`; closing restores `(79, 1)` |
| Global-dependent state | Controlled `WON-FLAG` false/true/false gives West states 1/2/1 |
| Multiple observations | All eight Dam Room boolean combinations give their mapped IDs 1..8 |
| Incomplete room | Sandy Cave `(126, 0)`, replacing a prior nonzero sentinel |
| Loud Room special input | Shared reader reports state 2, then state 1 after `echo` |
| Confirmation input | `quit`/`y` do not call the resolver; a sentinel remains unchanged |
| Compiler bindings | Actual `.GVAR` allocation parsed; shifted declaration-order fixture passes |
| Reproducibility | Repeated builds in different directories give identical story/sidecar bytes |
| Sidecar validation | Truncated and mismatched metadata rejected; inspection leaves story bytes unchanged |
| Gameplay | 17 boundaries through termination match output, status, globals and all 250 objects' flags/links |

The global and Dam tests establish controlled game states using public logical
memory writes and consistent object links. They do not constitute a complete
winning walkthrough. Gameplay regression uses a baseline compiled with the same
toolchain from staged source with only this instrumentation removed.

The minimal two-room proof compiled and ran in MAME before automatic generation
replaced it. The final generated story also passed the real ZVMTest keyboard
sequence: north, east, open window, close window, quit, yes; it returned to the
shell. The numeric pairs above came from actual compiled object declarations,
not guessed room numbers. Confirmation retains the last normal-input pair.

Verification passed:

- 12 compiled-story/generator/bindings host tests.
- `make -C libzvm test`: core and cache fixtures, SDCC target build and SDK package validation.
- 26 room-state extractor tests and five scene-extractor tests.
- MAME keyboard integration and paired ten-command benchmark, both completed.
- `git diff --check`.

## Measured execution cost

Single paired sample using the existing target timer, accepted optimized
ZVMTest, four dynamic/four static cache pages, sound disabled and unthrottled
MAME. Values are emulated target time, not host wall-clock time or measurements
on real hardware. These commands stay in West of House; other rooms have
different branch positions and predicates.

| Interval | Baseline | Instrumented | Added |
| --- | ---: | ---: | ---: |
| Initial execution to input | 2.111283 s | 2.420889 s | 0.309606 s |
| Ten command intervals combined | 50.617955 s | 54.441289 s | 3.823334 s |
| Total | 52.729238 s | 56.862178 s | 4.132940 s (7.84%) |

Per-command additions range from 0.361446 to 0.451584 seconds. Numeric host
printing is outside the timed interval. Inspection can still influence caches
for the following interval, and story layout changes can influence storage
traffic. These are whole-path overhead figures, not an isolated routine timer.

The host VM instruction counter provides a separate exact execution metric:
initial West input adds 102 instructions; subsequent West commands add 103;
North adds 71; East/window commands add 29. The one-instruction delta before
the quit confirmation is the return from the previous shared reader; the
resolver does not run there. Answering yes adds zero. These counts describe
the tested paths, not all ready rooms.

Both target benchmarks ended at room 180, score 0, moves 9, with 1,204 gameplay
output bytes and hash 1232506913. Host regression output SHA256 is
`295fb411557527fe8b7c48f4a44dd0648051c359e0c71d866713fce17380dd4a`.

## Build and resource audit

| Resource | Final value |
| --- | ---: |
| ZVMTEST.EXE | 43,184 bytes |
| Primary image | 43,148 bytes |
| BSS | 9,638 bytes; reporting adds 233 bytes |
| Reserved native stack | 512 bytes |
| SDK placement | `null`; zero far segments / zero veneers |
| Writable static initializer areas | `_INITIALIZED = 0`, `_INITIALIZER = 0` |
| Instrumented story | 85,522 bytes; baseline 84,312 (+1,210) |
| Story dynamic memory | 11,044 bytes; baseline 11,042 (+2) |
| Story dynamic XMEM | One object, two 8 KiB slots (16,384 allocated) |
| Extra reporting XMEM / code slots | Zero |

The existing automatic executable placer handles the application; this build
fits its primary image and requires no veneers. Reporting uses resident buffers
and ordinary calls through the existing reader/public VM API. It introduces no
mapper operations, function pointers, writable static initializers or native
multiplication. Stack reservation is unchanged; this acceptance does not claim
a new measured stack high-water mark.

The observed application bank is 1, valid-bank mask `0x7F`. This mask is **not**
a free-bank count. The SDK does not report exact remaining system-wide slots
or object capacity; no additional free-capacity figure is inferred. Reporting
does not add an allocation, and the story remains in the same rounded two-slot
allocation as the baseline.

ZVMTEST SHA256:
`cdb232076754b499f23907a2c49a5790d59a0b10db1ae21ccdf8a70afe2e2ddf`.

Instrumented story SHA256:
`cce1fdc54f18a6616e124db2de245aec1da943b458ebdee61bf1c6ff0a4038d4`.

The original 86,838-byte `COMPILED/zork1.z3` is untouched, SHA256
`37084966477dff679282de42974b2077156b1bd68fad92a65d4ea94d8eb64d79`.
Existing compiler warnings (including the original ZSCII tab warnings) remain
in build logs; no new reporting warning was introduced.

## Evidence and reproduction

[acceptance-data](acceptance-data/) preserves host results, target timing
records, keyboard trace, tool/source hashes, compiled bindings, executable
resource report, placement result and test logs. Fresh runs write their full
console captures and compiler assembly/logs under `build/state-reporting/`.

See [README](README.md) for commands and deployment. Deploy the three runtime
files identified by `libzvm/build/STATE-SHA256SUMS` together. The compiler-derived
indices in this build are HERE=0 and WOH-STATE=151; ZVMTest obtains these from
the sidecar, and recompilation regenerates them.
