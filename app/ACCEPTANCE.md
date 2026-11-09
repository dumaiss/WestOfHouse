> Historical acceptance snapshot: the measurements and captures below describe
> the previous 256×159 scene / five-row layout. The current build uses
> `west_of_house_256x144.g7` and seven narrative rows; new runtime acceptance
> has not been performed for this layout revision.

# G0 acceptance — 2026-11-08

## Status

G0 is implemented and playable with automatic story loading from the current
working directory. The user clarified that CWD lookup is the intended startup
contract, superseding the original executable-relative wording. No fixed
installation directory or OS change is needed. An optional directory argument
remains available as an override.

The tested launch is `/fs/WOH.EXE` with no arguments and CWD `/fs`, with the
executable and story staged together by the existing SDK runner. The runner
asserts the session's exact no-argument command. The two-file delivery bundle is
`app/build/WestOfHouse/{woh.exe,zork1.z3}`. The image and compiled bindings are
embedded in `woh.exe`; no image or `.WOH` sidecar is needed at runtime.

## Acceptance results

| Test | Evidence |
| --- | --- |
| Automatic startup | Pass: no arguments; `zork1.z3` loaded from CWD; no selector or external VM process |
| Initial execution | Native embedded VM reaches West of House `(180,1)` |
| Room transition | North `(81,1)`, East `(79,1)`; source bitmap unchanged outside overlay |
| Window transition | East remains 79; state changes 1 → 2 → 1 on open/close |
| Unsupported state | Renderer unit test prints literal zero; the unchanged story's Sandy Cave zero recognition is covered by the prior state-reporting fixture for this same story SHA256 |
| Long output | Initial response spans three pages; first page shown, non-overlapping PgUp/PgDn and both mouse arrow hit areas pass |
| Keyboard | Insertion in the middle, Home/End-compatible navigation, Backspace, 40-character prompt scrolling and progressive hint hiding pass |
| Typing during paging | Jumps to newest page and edits without submitting a turn |
| Mouse/menu | Real USB-field motion, two sprites, hover highlight, OPEN MAILBOX submission, Esc, outside click and typing-to-close pass |
| Cursor preservation | Exact bitmap byte comparison before/after pointer movement passes |
| Menu restoration | Exact bitmap byte comparison after Esc and outside-click restoration passes |
| Gameplay regression | All 12 input/termination boundaries match host VM narrative hashes and actual room/state values |
| Missing story | Useful path/error, graphics released, console restored and shell reached |
| Mismatched story | Compiled-header mismatch rejected, graphics released and shell reached |

The full MAME sequence is `look`, menu-selected `open mailbox`, `inventory`,
`north`, `east`, `open window`, `close window`, `xyzzy`, `score`, `quit`, `y`.
UI paging, cursor motion, menu cancellation and prompt editing do not increment
the submitted-command counter. Termination is followed by the ordinary Enter
exit. Narrative output is compared separately from the VM's status channel.
The cumulative 1,300 narrative bytes have SHA256
`fa2269c2b4c0bbfc2ceaf16a7da61bfefe7d9c3d0ccd4b30fe68c853337ab42d`.

Host verification passes:

- Panel/menu C fixtures: wrapping, final 42-cell row versus continued 36-cell
  row, blank lines/indentation, bounded history eviction and editing bounds.
- Renderer contract fixtures: exact image bytes outside the overlay, zero
  state, geometry, colours, hint suppression, prompt and menu restoration.
- Game fixtures with the actual story, unchanged core and production cache:
  startup, movement, missing/read/mismatch/allocation/staging/backend faults
  and resource cleanup; immutable source remains unchanged.
- Three Python preparation/archive tests, including byte-for-byte preservation
  of installed SDK library members.
- AddressSanitizer/UndefinedBehaviorSanitizer panel/menu fixtures. Leak checking
  was disabled because the execution sandbox uses ptrace; these fixtures use
  static storage and no allocation.
- Existing `make -C libzvm test`, including core/cache fixtures and target
  package validation.
- SDK pack/validate for the final application and `git diff --check`.

## Rendering observations

Single successful MAME session, 10 MHz emulated target, sound disabled,
unthrottled. These are target TIMER readings, not host wall-clock or real
hardware measurements. Sprites are active during normal interaction.

| Operation | Samples | Mean | Maximum |
| --- | ---: | ---: | ---: |
| Graphics initialization/mode/clear | 1 | 130.790 ms | 130.790 ms |
| First scene upload and page-1 cache copy | 1 | 815.180 ms | 815.180 ms |
| Narrative panel redraw, up to five rows | 13 | 53.701 ms | 75.136 ms |
| Page-change narrative redraw | 8 | 49.450 ms | 63.001 ms |
| Prompt redraw | 170 | 18.227 ms | 23.859 ms |
| Menu open/highlight redraw | 5 | 20.357 ms | 22.963 ms |
| Menu close/VRAM restoration | 4 | 12.269 ms | 12.416 ms |

Panel timings include both full five-row and shorter pages. Page changes also
redraw the prompt separately; the two rows above show those steps individually.
The scene-upload timer stops before drawing the numeric overlay. No scene
reload occurs on movement or state changes: its upload count is exactly one.
Timers include the existing service/VDP waits; no speculative optimization was
introduced to alter these results. VM execution uses 50-instruction cooperative
slices so mouse movement can be serviced during a slow turn.

## Memory and executable audit

| Item | Value |
| --- | ---: |
| Final executable, including fixed image | 107,956 bytes |
| Primary image | 48,896 bytes |
| Resident writable statics | 13,512 bytes total; 7,324 high BSS, remainder zero-fill in primary image |
| Reserved native stack | 768 bytes |
| History/editor model | 1,913 bytes, eight pages / 40 rows |
| Story cache | Four dynamic + four static 512-byte pages |
| Scene streaming buffer | 256 bytes |
| Far segment 1 | 9,024 image + 5,290 BSS bytes |
| Far segment 2 | 9,231 image bytes, no BSS |
| SDK veneers | 22 |
| Embedded scene DATA rows | 32,768 and 7,936 bytes |
| Loader segment allocation | Nine 8 KiB slots total |
| Writable story allocation | 11,044 bytes, rounded to two 8 KiB slots |
| `_INITIALIZED` / `_INITIALIZER` | 0 / 0 |

The two far-code segments, two scene blobs and one runtime story object compete
for the existing allocator. No object exceeds 32 KiB, and neither mapped code
segment exceeds 16 KiB. The image blobs use copied reads, not aperture pointers.
The application consumes eleven auxiliary slots including writable story
memory, in addition to its primary application bank. Exact remaining system
objects/free slots are not exposed by the SDK and are not inferred here.

The SDK's automatic placer produces every segment and veneer. VM callbacks and
their actual callees remain resident. Local placement hints address observed
link failures; no custom bank switching or cross-bank calling was added.
All newly compiled application/extension objects were audited for multiplication
helper references; none introduce `__mulint`/`__mulong`. Constant style/pattern
tables are read-only. The stack size is a reservation, not a measured high-water
mark; successful emulator play is not a proof for every Zork path.

## Limits and evidence

- No physical hardware acceptance or full winning walkthrough is claimed.
- Zero recognition is validated by the unchanged-story host fixture and G0
  renderer test; this run does not walk to Sandy Cave in MAME.
- History is bounded. Whole old pages are evicted; a response exceeding eight
  pages retains its newest eight pages rather than an unbounded transcript.
- The menu is a three-item generic widget with a deliberately artificial
  MAILBOX target on the cave's foreground rock, not actual scene semantics.
- A simulated hot-unplug check did not produce a disconnect through the current
  emulator/controller mouse API; hot-unplug/reconnect acceptance remains
  unverified. Missing/error mouse services leave keyboard play available by
  implementation; the earlier stationary-mouse keyboard run also succeeded.
- The local gfx archive normalization and blob-manifest append are documented
  in [README](README.md); installed SDK files remain unchanged.
- No OS, SDK, emulator, VM core, ZIL source or story bytes were changed for G0.

[acceptance-data](acceptance-data/) preserves the successful target results,
trace, checksums, executable/placement reports, representative screenshots and
test logs. Full VRAM dumps and console captures are under `app/build/` and can
be regenerated with `python3 app/tools/run_target.py`.

Executable SHA256:
`2b9f1ad007451d541b83ba1e261720b6335e104a006eb904ddf5591ce5062939`.

Story SHA256:
`cce1fdc54f18a6616e124db2de245aec1da943b458ebdee61bf1c6ff0a4038d4`.
