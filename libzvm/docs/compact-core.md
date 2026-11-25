# Compact streaming interpreter

The library uses a streaming V3 execution core behind the existing
`zvm.h` API. Caller-owned contexts, memory callbacks, output/status callbacks,
instruction budgets, input submission and faults retain their library roles.
The core has no KRES, filesystem, console or KonaOS mapping dependency.

`compact.c` fetches and evaluates operands in order and dispatches immediately.
Handlers consume store/branch bytes as needed. It does not construct the
reference decoder's `zvm_instruction_t`. Signed arithmetic operates on 16-bit
magnitudes; logical PCs and public addresses retain their 32-bit API encoding.
`compact_fetch.asm` performs Z80 instruction fetch and increments the PC in
place, with a compile-time context-offset assertion. Host and diagnostic builds
use the equivalent C fetch. Object, text, input, frame and variable services
are also used by the test-only regression oracle.

This follows ZIP80's streaming execution approach. No ZIP80 source is copied
into the library. It is a hybrid C/Z80 implementation, not a full assembly
port. The initial 10 KiB size target is not an achieved guarantee. The application
placement report is authoritative for the resident execution footprint.

## Build

```
make library KOS_SDK_ROOT=/path/to/mk2-sdk
make test-compact KOS_SDK_ROOT=/path/to/mk2-sdk
```

`library` builds the production archive; ordinary `make` also builds and
publishes ZVMTEST. There is no execution-core selector. `ZVM_BUILD` can select
an explicit output directory. `test-compact` links the test-only regression
oracle and production core in one host process and compares them after each
50-instruction slice, including
PC, output, story bytes and stack/frame depths. It includes signed edge cases
and a separate instruction-fetch assembly check. `ZORK_STORY` selects a story.
These comparisons establish coverage of the exercised paths, not exhaustive
V3 conformance or target gameplay acceptance.

## Mapped hosts

A host may retain a story mapping while running the core only if the entire
execution path, callback code, state, buffers and native stack remain visible.
The host restores the caller's mapping before returning to code in an aperture.
`zvm_set_page_access` can lend views into mapped story RAM. Its validity byte
identifies the current 8 KiB region, so a cached view is usable only while
that region is mapped. Restoring the same region makes its views valid again;
there is no wrapping generation counter. An invalid sentinel disables the
views before returning from the scope. Fast assembly reads/writes access
these pointers directly, calling the backend only on a view miss. The WoH
`mapped_story_run` scope implements this boundary using the public VM API.
