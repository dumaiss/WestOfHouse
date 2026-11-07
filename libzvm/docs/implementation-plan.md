# Milestone status

This page describes the current implementation. Acceptance reports retain the
measurements and limitations of their respective milestone snapshots.

| Milestone | Status | Scope |
| --- | --- | --- |
| M1 | Implemented | Public opaque context, V3 header validation, bounded storage, memory API, SDK archive and initial fixtures |
| M2 | Implemented | Decoder, operand evaluation, variables/frames, arithmetic/control flow, objects and cooperative execution |
| M3 | Implemented | Packed text/ZSCII, abbreviations, dictionary/tokenization, host input/output and playable Zork I adapter |
| M3.5 | Implemented | Profiling, cache/host-call improvements, accepted Z80 cache path and target acceptance |
| Later milestones | Pending | Save/restore persistence, complete restart and application integration |

The M3.5 resource, performance and regression evidence is recorded in
[m35-performance.md](m35-performance.md), with the measured source snapshot and
logs under [m35-data](m35-data/README.md). The earlier
[M3 report](m3-acceptance.md) is historical; its stack and executable sizes are
not current resource declarations.

## Remaining boundaries

- V3 save and restore currently branch false. Reserved public request statuses
  do not constitute a persistence implementation.
- The restart opcode is unsupported. The public execution reset does not restore
  original story memory or reset all state.
- The implemented opcode subset and bounded stack/frame capacities must be
  considered when embedding other stories. Existing Zork I runs do not prove
  whole-story or all-story coverage.
- West of House frontend integration and any KRES packaging remain application
  work. The VM stays independent of TUI/console and packaging choices.

Keep the module split described in [architecture.md](architecture.md). Future
work should extend the responsible modules rather than consolidate the engine
into `zvm.c`. Platform changes remain outside this library's scope.
