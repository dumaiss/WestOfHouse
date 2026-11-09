# libzvm

A statically linked Z-machine V3 interpreter for KonaOS/SDCC Z80, with a
host-buildable C core. The original story supplies all game logic. The library
owns no filesystem, terminal, TUI, XMEM mapper or allocation policy.

## Start here

- [Embedding guide](docs/embedding.md): lifecycle, memory ownership, callbacks and execution.
- [Public API](include/zvm.h): types, limits and function contracts.
- [Architecture](docs/architecture.md): module responsibilities and execution flow.
- [Contributor guide](CONTRIBUTING.md): development conventions and verification commands.
- [Milestone status](docs/implementation-plan.md): completed work and remaining scope.

## Build

Run from this directory. Target builds need SDCC, `sdar`, `sdasz80`, Python 3
and the exported KonaOS SDK. The default SDK path is
`../../ZephyrOS/build/kos-sdk-0.2.0`; override it for another installation:

```sh
make                         # Optimized library and ZVMTEST
make release                 # Explicitly force the accepted optimized settings
make KOS_SDK_ROOT=/path/to/kos-sdk
```

**Get the executable from `build/ZVMTEST.EXE`.** The matching library is
`build/libzvm.lib`; `build/SHA256SUMS` identifies both artifacts. A successful
build prints these absolute paths. The production target build needs only the
exported SDK.

Plain `make` and `make zvmtest` use the accepted M3.5 configuration: four dynamic
and four static cache pages, the Z80 cache fast path, a 512-byte native stack,
and no profiling, stack probe or experimental text helper. `make release`
explicitly selects those settings even when configuration overrides are present.

Intermediate optimized files live under `build/optimized/`. Explicit diagnostic,
reference and experimental configurations live under `build/variants/` and never
overwrite the delivery artifacts. For example:

```sh
make zvmtest ZVM_PROFILE=1    # Diagnostic executable; path printed after build
make zvmtest ZVM_CACHE_ASM=0  # C reference cache implementation
make release                 # Refresh the optimized delivery artifacts
make help
```

Older `build/cache-*`, `build/target`, `build/profile*` and `build/m35` directories
may contain historical binaries and measurements. They are not delivery paths;
the current Makefile does not build there. They are preserved until `make clean`.

| Setting | Default | Purpose |
| --- | --- | --- |
| `ZVM_PROFILE` | `0` | Optional VM and host-cache diagnostic counters |
| `ZVM_CACHE_ASM` | `1` | Accepted Z80 current-page read path in the test host adapter; `0` selects C |
| `ZVM_ASM` | `0` | Experimental packed-text helper, separate from the cache fast path |
| `DYN_CACHE_PAGES`, `STATIC_CACHE_PAGES` | `4`, `4` | Host adapter pages per partition; each supports `1` or `4` |
| `ZVM_STACK` | `512` | Target application's native stack reservation, in bytes |
| `ZVM_STACK_PROBE` | `0` | Optional target stack instrumentation; requires a 512-byte stack |

## Existing verification commands

Host checks require a C99 compiler. Cache checks also reuse the platform reader
implementation from `../../ZephyrOS/user/sdk/src/reader_full.c`; override
`KOS_READER_FULL_SOURCE` when that source is elsewhere.

```sh
make test-cache              # Host cache fixtures
make test                    # Host fixtures, target build and package validation
make test ZVM_PROFILE=1      # Same checks with diagnostic counters
make run-zork                # Interactive play through the SDK MAME runner
make test-zork               # Original seven-command story script in MAME
```

MAME commands require the SDK's `KOS_MAME`, `KOS_FLASH` and `KOS_MAME_PLUGINS`
paths. The runner uses the unmodified optional gameplay fixture
`../vendor/infocom-zork1/COMPILED/zork1.z3`. MAME may require a startup key in its
window. `make clean` removes the entire `build/` directory, including local runs.

The test application accepts `-p /fs/ZORK1.Z3` for keyboard play, `-a` for the
seven-command script, `-b` for a ten-command benchmark, `-d` for discard output,
or `-B` for both benchmark sinks. KonaOS `argv[0]` is the first flag, without an
executable-name argument. Benchmark targets are described in the
[M3.5 performance report](docs/m35-performance.md).

## Story state reporting

```sh
make story                   # Optimized ZVMTest + instrumented story bundle
make run-state-zork          # Keyboard play with numeric HERE / WOH-STATE
make test-state-reporting    # Compiled-story host regression and state tests
```

Deploy `build/ZVMTEST.EXE`, `build/ZORK1.Z3` and `build/ZORK1.WOH` together.
`build/STATE-SHA256SUMS` identifies the bundle. The story computes room-local
state IDs from the ready dictionary definitions; incomplete rooms report zero.
The test application's host adapter uses compiler-generated global bindings
and the existing public VM API. See [pipeline documentation](../tools/state-reporting/README.md)
and [acceptance results](../tools/state-reporting/ACCEPTANCE.md).

## Status and evidence

M1–M3 and M3.5 are implemented. Save/restore persistence and complete restart
remain future work. See [M3.5 acceptance](docs/m35-performance.md) for measured
resources and limitations, and [the cache assembly contract](docs/m35-cache-asm.md)
for its private ABI. Historical reports describe the versions measured; their
archived evidence is retained under [docs/m35-data](docs/m35-data/README.md).
