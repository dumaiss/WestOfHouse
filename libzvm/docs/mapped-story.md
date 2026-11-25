# Mk2 mapped story memory

The application preloads the same KPR-packaged story used by the application. The
KRES resource reader, story/header bindings and UI remain the host boundary.

## Execution and memory

The application links the streaming interpreter library. Its execution
path and output callbacks are resident outside 8000h–BFFFh. At load time it
copies the story into 8 KiB mappable XMEM objects and computes its checksum.
The story size determines the object count; the last object can be partial.

`mapped_story_run` opens a resident execution scope around each bounded
`zvm_run`. The backend lends native 512-byte page views into the selected
8 KiB story object through `zvm_set_page_access`. The existing Z80 memory hit
paths load/store directly through those pointers without a byte callback. A
view miss calls the page backend; a remap occurs only if the requested 8 KiB
region differs from the one held. Writes update RAM immediately. A 512-byte
view boundary can require a new view without requiring a remap.

The validity byte is the current region identity, not a wrapping counter. Each
identity always denotes the same backing object and native address range, so
returning to that region safely revalidates its cached views. Sentinel 255
invalidates all views before restoring the caller mapping and outside a scope. The scope restores the caller's mapping on yield, input,
finish and fault. Calls outside the scope restore the mapping per byte, so
initialization, inspection and input submission retain their ordinary API.
No story blocks are copied or read from the archive during execution. Preload
uses 256 bytes of the not-yet-initialized VM object as scratch; VM initialization
then replaces that storage. It adds neither a stack buffer nor permanent BSS.

The scope permits only resident callbacks and helpers. `game_output.c` and
the panel output routines stay resident; panel navigation/editing lives in
`panel_edit.c`. The SDK placer keeps the execution call closure resident, and
`app/tools/check_vm_residency.py` checks the critical members during bundling.
KRES retains the SDK's resident callback declarations, and graphics mode setup
retains the application's resident placement.

## Build and checks

Build the application with `KOS_BOARD=mk2` and the Mk2 `KOS_SDK_ROOT`. Its
library sub-build uses the same streaming core as the default libzvm build.
The resulting bundle is `app/build/WestOfHouse/woh.exe` plus `WOH.KPR`.

`make -C libzvm test-compact` compares the compact and reference interpreters
on a story, runs the existing VM fixture, and executes the instruction-fetch
assembly on the platform Z80 core. `make -C app test` checks the actual story
integration, resource bytes and UI; `test-mapped-story` checks mapping reuse
and restoration with a host aperture model. Pass `KOS_SDK_ROOT` as for builds.
`ZEPHYROS_ROOT` selects the platform checkout for the Z80 fetch check.
After a target build, `python3 app/tests/check_mapped_memory_z80.py` executes the
linked Z80 memory paths with a modeled mapper, checking direct reads/writes,
600 region switches, invalidation and ABI/stack cleanup.

## Measurement limits

`WOH_PROFILE=1` records the application's existing per-turn data. In
`PROF.BIN`, `xreads` counts story aperture selections and `io_static` counts
8 KiB region transitions observed by the backend. `io_dynamic`/`xwrites` count
only fallback byte-write callbacks: direct mapped stores bypass these counters. These are not
copy counts. A scope restore is not included in `xreads`. Far code outside the
scope can cause additional mapping operations.

Instruction and data accesses can still alternate between story regions;
this requires remapping. Input tokenization outside the execution scope uses
per-byte mapping. Native host comparisons do not measure Z80 speed, OS mapping
costs, graphics or interrupt timing. Emulator command-time measurements remain
necessary before claiming a runtime speedup. The compact core is a hybrid of
streaming C handlers and Z80 fetch assembly, not a complete assembly rewrite.
