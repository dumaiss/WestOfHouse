# ZIL state reporting

The story computes its own `(HERE, WOH-STATE)` pair before normal command input.
ZVMTest reads the two globals with the existing public `zvm_global()` API and
prints numbers. The VM core has no dictionary, symbol lookup or reporting hook.

## Build and play

From the repository root, using Python 3 with PyYAML, the installed `tools/zilf`
and `tools/zapf`, and the existing exported KonaOS SDK:

```sh
make -C libzvm story
make -C libzvm run-state-zork
```

The delivery bundle is:

| File under `libzvm/build/` | Purpose |
| --- | --- |
| `ZVMTEST.EXE` | Accepted optimized test application |
| `ZORK1.Z3` | Instrumented V3 story |
| `ZORK1.WOH` | Matching compiled global bindings; deploy beside the story |
| `ZORK1.bindings.json` | Build-time indices and SHA256 identities |
| `STATE-SHA256SUMS` | Checksums for the three runtime files |

`story` forces the accepted optimized application settings. Normal `make`
still builds the library/application; `run-zork` still selects the original
Infocom compiled fixture. Reporting and original runs use separate persistent
SDK card directories so a reporting sidecar cannot leak into an original run.

`build_story.py` stages source copies under `build/state-reporting/`, runs the
existing ZILf/ZAPF tools, and fixes the assembler serial for reproducibility.
It leaves `vendor/infocom-zork1/COMPILED/zork1.z3` untouched. Compiler logs,
emitted assembly and source/tool hashes remain beside each built story.

```sh
python3 tools/state-reporting/build_story.py
python3 tools/state-reporting/build_story.py --baseline
```

The baseline removes only this package's global/include and input-call changes
from staged copies, then uses the same compiler. It is the regression and cost
comparison story; the original historical binary is preserved separately.

## Generation and input boundary

`generate_states.py` consumes `generated-room-states/room-states.yaml`. It emits
`generated-zil/woh-states.zil` and `woh-state-ids.json`. IDs are room-local:
zero means unknown/incomplete; ready states receive 1..N, with INITIAL first,
then symbolic state ID order. Ordering the input YAML differently does not
change the result. IDs may change when the room's definitions change; deploy
future presentation assets with their matching generated mapping.

The resolver supports all 99 currently ready rooms. It resets `WOH-STATE` to
zero on every invocation and uses native ZIL conditions. The 11 incomplete
rooms have no recognition branch. Ready predicates are validated for coverage,
overlap and supported observations before code generation. No YAML is loaded
by the target application.

`WOH-READ` calls `WOH-UPDATE-STATE`, then the original `READ`. Both PARSER's
normal command read in `gparser.zil` and Loud Room's separate command loop in
`1actions.zil` call this shared routine. The latter change was explicitly
approved. Ordinary turns finish gameplay and scheduled events before reaching
the next parser read. Clarification reads that return through PARSER use the
same path. Separate yes/no and end-of-game reads are unchanged: ZVMTest may
print the cached pair there, but no resolver runs at those confirmation reads.

`WOH-STATE` is appended after the game's globals, preserving the first three
V3 status globals. The generated resolver writes only `WOH-STATE`, produces
no text and invokes no gameplay actions.

## Compiled bindings

`bindings.py` reads ZILf's actual `zork1_data.zap` `.GVAR` declaration order.
ZAP allocates machine variables from 16; the public VM API takes their zero-based
global-table indices. Neither index is guessed or embedded in ZVMTest.
Recompiling regenerates bindings, including when global allocation changes.

The 70-byte sidecar contains `WOH1`, one byte for each index, and the exact
64-byte immutable story header. ZVMTest checks this header before VM startup
can modify it, rejects invalid/truncated/oversized metadata, and reads both
values only through the public API. Missing metadata permits ordinary stories;
a present mismatched sidecar is a load error. Header matching detects mismatched
bundles; it is not a full-story cryptographic check. The JSON/SHA256 manifests
identify the complete files for deployment.

The host adapter uses the existing immutable `kos_reader_t` source and resident
buffers. It adds no function pointers, writable static initializers, native
multiplication, mapper operations, XMEM objects or banking mechanism. Numeric
printing occurs after the existing benchmark timer stops; reading the globals
can affect caches used by the following command.

## Verification

```sh
make -C libzvm test-state-reporting
make -C libzvm test
python3 tools/state-reporting/run_target.py
```

The target runner uses the existing SDK/MAME executable and plugin. Its default
paths match the sibling repositories; see `--help` for overrides. It runs a
keyboard window-state sequence and paired baseline/instrumented ten-command
benchmarks, recording traces under `build/state-reporting/`. `--no-build` reuses
an already matching acceptance bundle. MAME may require its startup key.

The host tests compile real stories and run the unchanged C VM. Global-dependent
and Dam combinations use controlled fixtures via public memory writes, including
consistent object links; these are not a complete winning walkthrough. See
[acceptance results](ACCEPTANCE.md) for measured evidence and limits.
