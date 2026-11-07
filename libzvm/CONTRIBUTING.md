# Contributing to libzvm

## Find the right module

Read the [embedding guide](docs/embedding.md), public `include/zvm.h` and
[architecture map](docs/architecture.md) first. Follow one instruction through
`decode.c`, `execute.c` and `opcodes.c`, then into its service module. Keep
lifecycle code in `zvm.c`; text, dictionary, objects, frames and memory each have
their own translation unit.

`include/` is the public interface. `src/zvm_internal.h` is private. Files under
`tests/` include host fixtures, the KonaOS test application, its story-memory
adapter and measurement tools. The cache is an example host backend, not a
mandatory dependency of the VM.

## Source conventions

- Use the checked-in `.clang-format` for C and headers: tabs, eight-column
  indentation, Linux function braces and a 100-column limit. Keep inline SDCC
  assembly inside its formatting guards.
- Explain ownership, VM encodings, invariants and target constraints in comments.
  Name numeric opcode cases so the dispatch can be read beside the decoder.
- Use logical integer addresses for story memory. Never expose a native pointer
  into a mapped story page. Route reads and writes through `memory.c`.
- Preserve operand evaluation and callback order. Variable zero pops/pushes for
  ordinary access and peeks/replaces for indirect access. These distinctions
  matter even when a repeated expression appears simpler.
- Keep signed interpretation, widening and 16-bit wrapping explicit. SDCC helper
  availability matters: the dictionary span calculation deliberately uses
  repeated addition rather than a mixed-width multiplication.
- Propagate callback errors. Multi-byte writes and input updates are not
  transactions; earlier writes can remain after a later failure.
- Separate readability changes from semantic changes and performance experiments.
  Keep public API contracts and examples consistent with the implementation.

## KonaOS boundary

Use the platform's authoritative `docs/architecture.md`, `docs/memory.md`,
`docs/sdk.md`, `docs/tui.md` and KRES SDK design documentation when changing the
host adapter. Follow the precedence in the platform architecture document.
The core must stay independent of console/TUI services and resource packaging.
An immutable `kos_reader_t` source and writable dynamic VM memory have different
roles; KRES packaging is optional.

Use the SDK executable placer and generated veneers. Review new function
pointers, writable static initializers and SDCC arithmetic helpers for linker
and packing compatibility. Callback-calling members are declared in the
Makefile's `KOS_POINTER_MEMBERS_zvm`; ordinary opcode calls remain eligible for
automatic placement. Do not add custom cross-bank call machinery.

A backend change must respect the single mapping aperture: do not retain mapped
pointers across calls that can change mapping. XMEM uses 8 KiB slots, has a
32 KiB per-object limit and a 16 KiB mappable limit; object counts and bank
resources are shared with far code. Consult the actual allocator and report
allocations rather than deriving availability from story size alone.

`tests/story_cache_z80.c` depends on structure offsets and the SDCC calling
convention. Its assertions and [assembly contract](docs/m35-cache-asm.md) must
remain aligned with `story_cache.h`. Do not reorder fields casually. The native
C stack reservation and the VM's evaluation stack are separate resources.

## Verification for implementation changes

Use the commands in [README](README.md) to select the affected layer:

1. `make test-cache` exercises cache boundaries, eviction and backend failures.
2. `make test` exercises synthetic VM fixtures, builds the SDCC archive/application
   and validates executable packing.
3. `make test ZVM_PROFILE=1` covers the diagnostic configuration.
4. Target story scripts and keyboard play check actual integration. Use the
   existing SDK MAME runner, allowing for its startup prompt.

For changes to packing, callbacks, assembly or stack usage, inspect the placement
report, veneers, stack/BSS reservation and XMEM/bank resources after loading.
Do not infer hardware acceptance from host checks. Record which configurations
were actually checked and any limits of that evidence.

Keep original story files and historical acceptance evidence unchanged. New
measurements belong in a new report or an explicitly dated update; archived
M3.5 snapshots document the code measured at that milestone. Platform changes
are outside this library's scope.
