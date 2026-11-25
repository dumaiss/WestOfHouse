# Compact resident execution validation

## Result

The application uses the compact streaming core through the existing
`zvm.h` interface. KPR loading and resource payloads are retained. Story RAM
remains mapped throughout a bounded execution slice, with remapping on 8 KiB
region changes and restoration of the caller's mapping on every return.
The implementation is hybrid C/Z80: the streaming core and handlers are C,
instruction fetch is Z80 assembly, and the existing object/text/input services
are shared. It is not a complete assembly rewrite.

## Direct mapped views

The native page-access paths point directly into mapped story RAM.
Reads/writes in valid windows execute Z80 pointer loads/stores; view misses
call the page backend, and only changes of 8 KiB region remap SRAM.
The interpreter decodes story bytes without per-byte backend dispatch or
address translation on a window hit.

Host integration checks 200 accesses with no byte callbacks, 600 alternating
region switches, write-through, static write rejection and scope invalidation.
`python3 app/tests/check_mapped_memory_z80.py` also executes the actual linked
Z80 memory paths: the same hits/switches, writes, invalidation and ABI/stack
checks pass without byte fallback. That mapper is modeled and measures no OS
or emulator latency. Preload reuses 256 bytes of the VM object before its
initialization instead of reserving a separate permanent scratch buffer.

## Linked size

| Code | Size |
| --- | ---: |
| Complete linked libzvm | 18,870 bytes |
| Execution path and service helpers required resident | 12,915 bytes |
| Streaming dispatcher/handlers | 5,111 bytes |
| Assembly instruction fetch | 50 bytes |

Lifecycle and input/dictionary code remain outside the resident execution
path and execute through ordinary SDK veneers.

The final application map contains 30,090 resident code bytes (including
veneers), 1,538 resident constant bytes, 597 low BSS bytes and 7,424 high BSS
bytes. The native stack reservation is 768 bytes. The low resident region has
194 bytes free; this layout has little growth margin. Packing and the
residency guard pass. Stack depth during full Z80 gameplay is not measured.

## Implementation checks recorded before consolidation

- Mk2 app build and EXE validation: pass; bundle residency guard verifies
  twelve critical execution/callback modules outside the aperture.
- `make -C libzvm test-compact ...`: reference/compact differential checks pass
  for 320 arithmetic edge cases and 21 commands on the original Zork fixture.
  The real reference and compact cores run in one host process; PC, output,
  story bytes and stack/frame depth are compared after each 50-instruction slice.
- The same differential executable passes 21 commands on `libzvm/build/ZORK1.Z3`:
  595 slices, 3,188 output bytes, hash `1cb0e535`. The original fixture uses
  579 slices, 3,190 bytes, hash `b1a0175e`.
- Existing host VM fixture: pass with streaming execution.
- Actual assembled instruction-fetch routine: 21 PC-boundary/error combinations
  pass on the platform Z80 core, including stack, argument and register checks.
  Its memory-callback return is modeled; this does not execute a full target story.
- `make -C app test ...`: panel/menu, render contracts, actual-story mapped game
  integration and SDK KRES resource checks pass; three Python tests pass.
  Game integration covers opening, preload failures/cleanup, startup, movement,
  VM fault restoration and absence of source reads after preload.
- `make -C app test-mapped-story ...`: pass. The host aperture model checks
  two hundred accesses in two regions require two map selections plus one
  restoration within a scope, and checks yield/input/finish/fault restoration.
- `git diff --check`: pass.

The SDK used is
`/home/kitamura/Documents/ZephyrOS/build-mk2/sys/kos-sdk-0.2.0-mk2`, with
`KOS_BOARD=mk2`. The library and game use the streaming core by default;
there is no production execution-core selector. The older driver and dispatch
are host-only regression oracles under `tests/reference_*.c`.

## Consolidation build (2026-11-22)

The canonical worktree is `/home/kitamura/Documents/WestOfHouse`, on
`woh-mk2-preload-branch`. `make -C app` builds and validates the Mk2 EXE and
passes the twelve-member residency guard. `make -C libzvm` builds the same
production core and the Mk2 ZVMTEST delivery. Tests and MAME are not rerun for
this source/build consolidation. No commits or pushes are made.

## Bundle identity

`app/build/WestOfHouse/woh.exe`: 73,443 bytes,
SHA-256 `2a0dd5602c55251ab485dc3ea9d843da9548ebe88c1691b44fa3a2f4168f6f22`.

`app/build/WestOfHouse/WOH.KPR`: SHA-256
`4ee73143223d673d8d66fe6fc24497bf86a8169af208b91542b70138f1500475`.

## Pending runtime evidence

The user reports markedly improved gameplay responsiveness with the direct
mapped views. No numeric command latency is recorded. MAME/gameplay is not
run by the worker, following the requested build-only workflow.
Command latency, mapping costs and target stack depth need emulator or hardware
measurement. Input tokenization still runs outside the held-mapping scope and
uses per-byte mapping. No runtime speedup or complete V3 conformance is claimed.
