# Embedding libzvm

## Storage and initialization

Allocate a `zvm_t` and keep it alive while using the VM. It is caller-owned opaque
storage with bounded space for 128 evaluation-stack words and 16 frames, including
the root frame. These limits are distinct from the application's native C stack.
Do not inspect its bytes or depend on `src/zvm_internal.h` from application code.

Choose one memory model:

| Initializer | Caller supplies | Checksum for `verify` |
| --- | --- | --- |
| `zvm_init` | Complete writable story buffer and byte length | Computed during initialization |
| `zvm_init_memory` | Read/write byte callbacks, user storage and logical length | Caller supplies original-source checksum with `zvm_set_story_checksum` |

Both validate the V3 header. Both reject writes at or above the static-memory
boundary. All word accesses are big-endian and logical addresses are 32-bit byte
addresses. A callback backend must read the complete declared memory range;
it may return errors for underlying I/O failures. Both callbacks are required.

Keep the story buffer or backend, callback code and callback user storage alive
until release or replacement. The library allocates nothing. `zvm_release()`
clears the VM context and closes/frees no host resources.

For a paged backend, preserve an immutable original source separately from
writable dynamic memory. The checksum is the modulo-65536 sum of original bytes
from offset 64 to the declared story end (the V3 header length word multiplied
by two), excluding the end. Compute it before dynamic memory changes.
`tests/story_cache.c` demonstrates a KonaOS reader/XMEM backend; it is owned by
the test application and does not require KRES packaging.

## Cooperative execution

Initialize memory, check the result, install callbacks and check each setter's
result. Repeatedly call `zvm_run(vm, budget)` with a nonzero instruction budget.

| Result | Host action |
| --- | --- |
| `ZVM_YIELDED` | Service host work, then run again |
| `ZVM_NEED_INPUT` | Obtain a line; call `zvm_submit_input`, check its error, then resume |
| `ZVM_FINISHED` | Stop; the story executed `quit` |
| `ZVM_FAULT` | Stop and inspect `zvm_error`, fault address and opcode |
| `ZVM_READY` | Initialized or successful input submission; ready to run |
| `ZVM_NEED_SAVE`, `ZVM_NEED_RESTORE` | Reserved states; current V3 save/restore branches report failure |

Budget zero yields without executing an instruction. Calling `zvm_run` while
waiting for input, finished or faulted returns that state without progress.
An instruction budget is not a time limit: memory and output callbacks can take
time. Input acquisition belongs in the host, outside the execution call.

A host loop, with application-defined I/O and error handling, can follow this
outline after successful initialization and callback registration:

```c
for (;;) {
        zvm_status_t status = zvm_run(&vm, 1000);
        if (status == ZVM_YIELDED) {
                service_host_events();
        } else if (status == ZVM_NEED_INPUT) {
                if (!read_host_line(line, sizeof(line)))
                        break;
                if (zvm_submit_input(&vm, line) != ZVM_OK)
                        break;
        } else {
                /* FINISHED, FAULT, or a state this host does not support. */
                break;
        }
}
zvm_release(&vm);
```

The host owns `vm`, `line` and the illustrative host functions above. Inspect
fault diagnostics before release, which clears them.

## Output, status and input

Callbacks are synchronous and return `zvm_error_t`. Serialize access to a VM;
do not recursively run or submit input to the same VM from its callbacks.
No terminal service is called by the core, though a host callback may call SDK
services or buffer output to batch them.

The output callback receives a channel and byte. Narrative and status text use
separate channel values. CR becomes LF and unsupported extended ZSCII becomes
`?`; byte zero is ignored. A null output callback discards output.

Status generation emits the location name through the status output channel,
then invokes the optional status callback with the location and either
score/moves or hours/minutes. Installing/removing the status callback also
updates header byte 1. A failed header access can occur after the callback has
been stored; callback registration is not transactional.

Submit NUL-terminated text only for a pending input request. The implementation
truncates to the V3 text buffer capacity minus one, lowercases ASCII, replaces
unsupported input with `?`, NUL-terminates and tokenizes into the parse buffer.
The request becomes ready only after success. Backend errors can leave partial
text or parse-buffer updates; do not assume rollback.

## Reset, inspection and diagnostics

`zvm_reset()` resets the PC, evaluation stack, root frame, status and error.
It does **not** restore dynamic memory or reset every field. Callbacks, random
state and checksum state are retained. It is not a complete story restart.
For a fresh game, recreate the original writable memory/backend and initialize
again, then register callbacks and checksum as appropriate.

Public inspection functions copy values out; they return no mapped story
pointers. `zvm_global` takes a zero-based index 0..239, whereas VM variable
numbers for globals are 16..255. `zvm_zork_room` is an isolated Zork I convention
for global index zero. Fault opcodes are normalized after a successful decode;
a failed decode attempts to record the raw opcode byte instead.

With `ZVM_PROFILE=1`, `zvm_profile.h` exposes counters owned by the VM. The getter
returns a borrowed pointer; reset clears counters only. Counters wrap modulo
2^32, and word operations also count their constituent byte accesses. Build the
application and library with the same profiling setting.
