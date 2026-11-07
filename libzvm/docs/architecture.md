# libzvm architecture

`libzvm` is a statically linked Z-machine V3 interpreter for SDCC/Z80. The original story remains authoritative for game logic. The library provides no graphics, terminal UI, filesystem, or game-specific interaction loop. It emits characters and status events only through caller-owned callbacks. The caller owns `zvm_t`, the story allocation or memory backend, and any host-service boundary.

## Module layout

The implementation keeps responsibilities split across focused translation units:

- `zvm.c`: public lifecycle, header validation, and accessors.
- `memory.c`: flat and callback-backed logical story memory.
- `decode.c`: bounded V3 instruction parsing, store/branch metadata, and inline-text boundaries.
- `execute.c`: budgeted fetch/decode/operand evaluation loop and VM fault state.
- `variables.c`: evaluation stack, locals, globals, and direct/indirect variable access.
- `frames.c`: bounded Z-machine call frames and return handling.
- `opcodes.c`: arithmetic, branching, calls, stack/object dispatch, text/input services, random, verification and explicit unsupported results.
- `objects.c`: V3 object links, attributes, and properties.
- `text.c`: streaming packed-text decoder, abbreviation expansion, and output callback.
- `dictionary.c`: V3 dictionary lookup and bounded tokenization into story memory.
- `input.c`: input submission, text-buffer update, and parse-table generation.
- `services.c`: status-line event construction for the host.

The public context is opaque fixed storage. Logical story addresses and PCs are 32-bit values; native pointers are confined to caller-owned backend storage. The callback backend lets callers expose larger or paged stories without changing VM logic. The core allocates nothing and calls no KonaOS services directly; host callbacks may use SDK services. The core has no dependency on the test application's cache or console adapter.

## M1-M3 implementation

Initialization validates V3 version, initial PC, globals table, object defaults, static-memory base, and declared story extent. Reads are big-endian and bounds checked. Both backends reject writes at or above static memory before modifying the buffer or invoking the write callback. Backend errors propagate to the caller. The interpreter never constructs a duplicate object tree.

The decoder handles long 2OP, short 0OP/1OP, VAR 2OP and VAR forms, large and small constants, variable operands, omitted operands, store and branch bytes, and inline print-string boundaries. `0xBE` is not treated as an extended-opcode prefix in V3. Operands are evaluated from left to right, with variable-number operands retained for `load`, `store`, `inc`, `dec`, checked increment/decrement, and `pull`. Variable zero ordinary reads pop; indirect reads peek; indirect writes replace the stack top. Stack and frame storage are bounded and faults are explicit.

Implemented opcode families include:

- 0OP: `rtrue`, `rfalse`, `nop`, `print`, `print_ret`, `ret_popped`, `pop`, `quit`, `new_line`, `show_status`, `verify`, and defined false branches for V3 save/restore.
- 1OP: `jz`, object sibling/child/parent access, property length, `inc`, `dec`, `call_1s`, `remove_obj`, `print_addr`, `print_obj`, `ret`, `jump`, `print_paddr`, `load`, and `not`.
- 2OP: equality and signed comparisons, checked increment/decrement, `jin`, `test`, logical operations, attributes, indirect `store`, object insertion, word/byte loads, property access, arithmetic, and `call_2s`. `call_2n` returns unsupported.
- VAR: `call`, word/byte stores, `put_prop`, cooperative `sread`, `print_char`, `print_num`, `random`, `push`, and `pull`.

Packed text is decoded incrementally through logical memory reads, including bounded abbreviation expansion. Output is synchronous ASCII-compatible bytes with LF newlines; unsupported ZSCII characters become `?`. Input is submitted only after `ZVM_NEED_INPUT`, then written to the V3 text and parse buffers. The VM never blocks for input or calls console/TUI services. Output streams, optional sound, and other unimplemented opcodes fail explicitly as unsupported. The caller can resume after each deterministic instruction budget. `quit` reports `ZVM_FINISHED`.

The host fixture checks packed text output and input/tokenization. Target fixtures execute real bytecode through the public API and inspect globals, properties, attributes, object relationships, PC, yield/resume, nested calls, stack evaluation, and execution above 64 KiB. They also check invalid-opcode, stack-overflow, frame-overflow, out-of-range-read faults, and deterministic repetition. `ZVMTEST` can load the original Zork I story through `kos_reader_t`, allocate only dynamic story memory in XMEM, and run a deterministic command sequence. The historical [M3 acceptance](m3-acceptance.md) records that milestone's placement and XMEM details. Later [M3.5 acceptance](m35-performance.md) records profiling, the accepted cache implementation and its resource measurements. These measurements include the test application and fixtures, not just the library, and apply to the versions/configurations measured.

## Host adapter and target placement

`tests/zvmtest.c` implements the KonaOS console adapter and application loop.
`tests/story_cache.c` separates the immutable `kos_reader_t` story source from
writable dynamic memory in XMEM, with separate dynamic/static cache partitions.
The accepted current-page read path lives in `tests/story_cache_z80.c`; the
[private assembly contract](m35-cache-asm.md) describes layout and residency
requirements. This backend is an application concern, not part of the VM API.

The Makefile declares callback-calling archive members to the SDK placer.
Opcode dispatch uses ordinary calls; automatic placement and SDK veneers handle
executable zones. No custom bank-call mechanism is introduced. A host backend
must obey the single-aperture rule and may not retain mapped pointers across
operations that change the mapping.

See [README](../README.md) for build/test commands and SDK path overrides, and
[CONTRIBUTING](../CONTRIBUTING.md) for target constraints and verification layers.
No ZephyrOS source or SDK files are changed by this project. The original story
at `../vendor/infocom-zork1/COMPILED/zork1.z3` is the optional gameplay fixture.

## Boundaries and remaining work

Save/restore persistence and complete story restart are not implemented.
`zvm_reset` resets execution bookkeeping without restoring original story bytes;
see [embedding](embedding.md) before using it. See the
[milestone status](implementation-plan.md) for completed and deferred work.

The game-specific `zvm_zork_room` convention remains isolated from standard VM
semantics. Any later YAZM-derived algorithm must retain MIT attribution and
follow the Z-machine specification when the two differ.
