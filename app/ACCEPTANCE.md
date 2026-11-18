# Resource archive acceptance — 2026-11-14

**PASS:** clean target build, host checks, deterministic archive verification,
and full MAME interface/error acceptance, rerun on the YJK+YAE gameplay screen
(below). Deploy only `app/build/WestOfHouse/woh.exe` and `WOH.KPR`.

## Packaged content and migration

- 593 files moved into source/runtime/review areas; 61 moved binary artifacts
  retain their recorded bytes, including every selected illustration, WHT1
  table and the title. Original prompts and generation/reference hashes pass.
- 18 explicitly selected KRES resources: one story, eight YJK+YAE scene
  images, eight WHT1 hotspot/action tables, and the unchanged YJK title.
- Eight original room/state mappings preserved. Attic remains unbound and is
  excluded. Source PNGs, prompts, JSON, overlays and review sheets are excluded.
- The SDK packer verifies the sealed archive. Unpacking confirms the exact
  selected key set and byte-for-byte payload equality. Two package builds are
  deterministic; a second incremental build performs no scene conversions.
- The moved preview builder reproduced a selected YJK payload exactly in
  scratch storage. No illustration was generated and the encoder was not edited.

## Application and target checks

MAME launched WOH without arguments, using the CWD archive. Its card contained
only `WOH.EXE` and `WOH.KPR`, with no loose story to mask integration failures.
The archived title was captured after a visible pause and dismissed by a normal
key. WOH then executed the archived story and accepted typed and mouse commands.
The title and initial/open-window/closed-window scene payloads match the archive
bytes exactly. North of House also loaded during movement. All eight mapped
images were streamed and compared by the host integration executable.

The full interface run checked editor/history navigation, mailbox actions,
smallest-rectangle selection, menu dismissal/restoration, pointer bitmap
preservation, room/state changes, parser rejection, score and quit. Twelve
input boundaries matched the unchanged host VM's narrative byte/hash transcript.
Missing and corrupt archives printed diagnostics and returned to the shell.
The widened menu accommodates the existing eight-character action labels.

## YJK+YAE gameplay screen

Gameplay runs in `GFX_MODE_256x212_YJK_YAE`: the 256×159 scene is YJK
(luminance in bits 7-4, A clear) and the text band, divider and menus are
palette pixels from a seven-entry UI palette. The title stays pure YJK until
the first key. Menu positions and the save-under copy align to four-pixel J/K
groups; the SDK masks every partial-group edge, so the shadow and outline never
disturb the scene's colours. The hardware pointer keeps the fixed sprite
colours (white interior, dark-blue outline).

The full MAME interface and error runs passed on this build: archived scene and
title payloads exact, cursor and menu restoration exact, all twelve narrative
boundaries equal to the host VM, missing/corrupt archives diagnosed. MAME
timings: menu draw 27 ms average (28.7 ms max, 6 calls), menu restoration
13 ms average (13.3 ms max, 4 calls), prompt redraw 18.5 ms average.
The YJK+YAE screens are in `build/interface/run/` after
`python3 app/tools/run_target.py`.

## Placement and memory

| Item | Result |
|---|---:|
| WOH executable | 73,606 bytes |
| KRES archive | 474,624 bytes |
| Primary image | 48,896 bytes |
| Primary high BSS | 7,424 bytes |
| Resident writable data, including low image zero-fill | 11,336 bytes |
| Resident code | 26,633 bytes |
| Reserved stack | 768 bytes |
| SDK-generated veneers | 19 |
| Far-code segments / 8 KiB slots | 2 / 4 |
| Writable story memory / slots | 11,044 bytes / 2 |
| App-owned XMEM objects | 3 (two code segments, one story object) |
| KRES read-path XMEM allocations | 0 |
| Physical resource-file handles | 1 |

The SDK automatic placer and executable packer validate the result. No custom
bank switching or cross-bank calling was added. Story reads use a borrowed
`kos_reader_t` view of KRES, separate from writable VM memory. Buffers, menu
strings and the hotspot table remain resident across aperture-changing calls.
Function-pointer residency is declared for the KRES reader and platform adapter.
Normal calls and placement handle the remaining container/catalog operations.
Stack high-water and system-wide free-bank/object counts were not measured;
the SDK does not expose the latter. Emulator success does not prove all Zork
paths or replace a real-hardware run.

## Checks and evidence

Passed: four host executables plus 3 app Python tests; resource pipeline 6,
hotspots 11, art 6, scene extractor 5, state extractor 26. Focused SDK checks:
KRES 20, TUI 30, placement 16, decoder-only lifecycle 1, source layout 52.

[Summary and measurements](../assets/review/packaging/summary.json),
[catalog](../assets/review/packaging/resource-index.txt),
[deployment/maintenance](../assets/README.md), and
[full review evidence](../assets/review/packaging/) include logs, hashes,
placement, screenshots and target captures. Historical embedded-image G0
acceptance is preserved in `acceptance-data/G0-ACCEPTANCE.md`.

Known art/continuity warnings remain in the pilot review. Attic has no runtime
state. No OS kernel/driver, KRES-format, VM/gameplay, story, encoder or approved
illustration changes were made by this task. The focused SDK changes were
explicitly authorized and are included in the rebuilt installed SDK.


## Approved White House artwork integration

The canonical bundle now packages 20 scene pairs (including Attic/Cellar dark
states), the matching story, title, and existing background music: 43 resources.
The resource builder verified, unpacked and byte-compared every archive payload;
all 20 images and WHT1 payloads match the approved review set. The rebuilt
executable passed SDK pack validation. Resident placement is 25,289 code bytes
and 12,458 data bytes, with the existing 768-byte stack reservation. To fit the
expanded table, the generated lookup retains one image filename per pair and
forms its validated matching `.wht` name in a bounded 32-byte stack buffer.
No music behavior or VM cache settings were changed by this integration.

See `assets/review/packaging/approved-white-house.json` for exact bundle hashes.
No new MAME or real-hardware run was performed for this packaging update.
