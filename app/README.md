# West of House graphical shell (G0)

Native KonaOS application embedding the existing libzvm and libgfx. The fixed
scene is `docs/west_of_house_256x144.g7`; it remains identical in every room.
The story supplies the actual numeric HERE / WOH-STATE pair.
The image occupies rows 0–143, the divider is at 144, seven narrative rows
start at 146, and the prompt remains at 203.

Launch with no arguments to load `zork1.z3` from the current working directory
(CWD), as clarified by the user. Enter the game directory before launching.
There is no fixed installation path or executable-location dependency. The OS
is unchanged. Filesystem components are 8.3; `WOH` is a suitable guest directory.

## Build and deploy

From the repository root:

```sh
make -C app
make -C app test
```

Requirements: GNU Make 4.3+, SDCC, sdar, Python 3, and the exported KonaOS SDK
at `../../ZephyrOS/build/kos-sdk-0.2.0` relative to `app/`. Override
`KOS_SDK_ROOT` for another installation. Host tests additionally use the SDK
reader implementation from the OS checkout; override `KOS_READER_FULL_SOURCE`
if necessary.

Deploy **only these two runtime files** from `app/build/WestOfHouse/`:

```text
woh.exe
zork1.z3
```

`SHA256SUMS` identifies the pair. For example, place both in `/fs/WOH`, change
the shell's CWD to `/fs/WOH`, then launch `WOH.EXE` with no arguments. There is
no file selector. An optional directory argument can override the story location
when needed. Intermediate `build/target/user/woh.exe` lacks the embedded image;
use the final bundle path above.

The current instrumented story and its bindings are read from
`../libzvm/build/ZORK1.Z3` and `ZORK1.WOH`; override `STORY` and `BINDINGS`
together for another matching pair. G0 does **not** recompile ZIL or regenerate
room recognition. It verifies the existing `.WOH` header and emits a small C
include with the two global indices and immutable story header. `.WOH` is a
build input, not a graphical application's runtime file.

The 36,864-byte image is uncompressed. Two ordinary SDK DATA segments (32,768
and 4,096 bytes) carry it inside the executable. Startup streams 256-byte
chunks to VRAM, then caches the clean scene on page 1. No software framebuffer
or full-scene main-RAM copy is allocated.

## Controls

- Type a command, then Enter. Left/Right, Home/End, Backspace and Delete edit
  the prompt; long commands scroll horizontally. Capacity is 80 characters.
- PgUp/PgDn browse non-overlapping seven-row pages. Typing jumps to the newest
  page. The bottom prompt stays at y=203. Continuation and arrow indicators
  follow the UI specification, including progressively hidden hints.
- Click the up/down arrow areas for the same paging operations.
- Click the **mailbox** (x=73..103, y=88..136) for the **test-only**
  EXAMINE / OPEN / CLOSE menu. Every item targets MAILBOX, regardless of the
  actual room. This is deliberately a widget fixture, not room semantics.
- Esc or clicking outside closes a menu. Typing closes it and edits the
  prompt. Selecting an item submits through the same path as Enter.
- Ctrl-Q at an input prompt exits without a story turn. After story termination,
  Enter exits; page navigation remains available until then.

An unavailable/disconnected mouse leaves keyboard play available. A stationary
new mouse may remain hidden until the controller receives its first HID report.

## Source map

| Module | Responsibility |
| --- | --- |
| `src/main.c` | Controller, event loop, startup policy and cleanup |
| `src/game.c` | Immutable reader, writable XMEM story, VM lifecycle and observation |
| `src/game_output.c` | Narrative callback and removal of the story's trailing prompt |
| `src/panel.c` | Wrapping, eight-page RAM ring and command editing |
| `src/render.c` | G7 layout, fixed scene boundary, text, menus and timing |
| `src/menu.c` | Three-item menu hit testing and command construction |
| `src/fixture.c` | Replaceable test-only hotspot/noun/verbs |
| `gfx/gfx_border.c` | Additive libgfx two-sprite cursor implementation |
| `tools/prepare.py` | Existing-story bindings include and raw image splitting |
| `tools/extend_gfx.py` | Local archive preparation; upstream member bytes preserved |
| `tools/bundle.py` | Add plain DATA rows to the SDK's placed executable manifest |

The eight-page history model occupies 2,617 bytes, including the editor and
pending wrap line. It retains the newest 56 wrapped rows; older whole pages are
evicted. A single response longer than that retains its newest eight pages.
This is bounded history, not persistent transcript storage. Explicit newlines,
blank lines and indentation are retained. Continued seventh rows wrap at 36
cells; the final seventh row can use all 42. Echoes are amber, narrative is pale
yellow, and the prompt uses its own fixed row.

The SDK's pinned 6x8 font is used, preserving its lowercase descenders. Its
placeholder glyphs at CP850 0x1E/0x1F are replaced with up/down triangles through
the public glyph API. No bitmap palette changes are made.

## SDK integration details

The production cache adapter is compiled directly from the existing
`libzvm/tests/story_cache.c` and `story_cache_z80.c`, with the accepted 4+4-page
configuration and assembly fast path. The VM core is unchanged. The immutable
story reader and writable XMEM memory remain separate; mapping pointers never
escape a service call.

Only the narrative output channel enters the text panel. Status-channel room
names are omitted from the narrative; actual globals populate the scene overlay.
Every NEED_INPUT is supported, including yes/no and parser questions. The story
updates WOH-STATE at its normal readers; confirmation reads retain its cached
value. No host inference about normal versus confirmation prompts is used.

The SDK TUI library is initialized before graphics only to use its existing
key decoder. There is no graphical-terminal emulator and no TUI drawing during
play. This reuse costs about 4 KiB of resident TUI model storage.

The installed gfx pointer API has a single colour layer. The local additive
member uses its existing shape/show/hide and backend streaming primitives to
add a second sprite: white fill (15), dark-blue outline (1), 12x16 visible
pixels. Bitmap VRAM remains untouched. The extension depends on the documented
V9958 pointer band at page-1 lines 496..511; the scene cache ends at line 414.
No TP change, fixed machine I/O port, bank switching or alternate graphics
engine is introduced.

Two installed SDK build limitations require local packaging steps:

1. The exported gfx archive has C and assembly members both named
   `gfx_pointer.rel`; the automatic placer identifies members by archive/name.
   `extend_gfx.py` gives duplicates unique archive names without changing their
   contents, then adds the bordered-pointer member. The installed archive is
   untouched; a host test verifies preservation of every member payload.
2. `exemanifest` rejects simultaneous placement and explicit blob declarations.
   `bundle.py` retains its complete automatic placement manifest, appends two
   ordinary DATA rows, and invokes the existing `exepack` pack/validate tools.
   It calculates no code placement, veneers or bank addresses.

Callbacks and the cache are marked resident as required by their actual
function-pointer use. The normal SDK placer handles the rest. A mode-setup
resident hint and home placement for the tiny fixture avoid the observed
oversized gfx group and low-region packing failure. There is no hand-declared
far-code calling scheme. See the generated placement report after each build;
this application uses more bank resources than ZVMTest.

## Verification and evidence

```sh
python3 app/tools/run_target.py
```

The runner uses the existing SDK, MAME binary and plugin in the sibling trees;
see `--help` for path overrides. It launches without arguments from the SDK's
`/fs` working directory, where staging places the two files. It exercises keyboard editing, both paging inputs,
hardware cursor movement, menu dispatch/closing, room/window changes, inventory,
score, unsuccessful commands, quit/yes, and error cleanup.

The read-only `woh_probe` symbol allows the existing bank-aware MAME plugin to
observe input boundaries, output hashes and UI state. It offers no VM mutation
or shortcut command path. Captured G7 bytes verify scene preservation and exact
menu restoration; captured output is compared with the unchanged host VM.

See [acceptance report](ACCEPTANCE.md) for measured performance, memory, results
and the CWD startup acceptance.
