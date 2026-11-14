# White House hotspot pilot review

## Result

All nine illustrations have reviewable `scene_hotspots_v1` JSON files,
standalone WHT1 binary tables and annotated previews. There are **44 hotspots**.
The nine binaries total **2,578 bytes** (225–456 bytes per scene).

Start with the [annotated contact sheet](contact-sheet.png), then use
[manifest.json](manifest.json) to locate individual JSON, binary and preview
files. Individual previews enlarge the exact 256×159 coordinate grid by 4×
using nearest-neighbor sampling. Rectangles and labels are diagnostics only.

No artwork, ZIL, Phase 1 generator or WOH application code was changed. Per the
user's clarification, **no KRES archive was created** and no resources were
installed into the executable. These payloads are for later incorporation.

## Coverage

| Scene | Runtime pair | Hotspots |
| --- | --- | ---: |
| West of House | 180 / 1 | 4: house, door, mailbox, forest |
| West of House | 180 / 2 | 5: same targets plus the revealed southwest path |
| North of House | 81 / 1 | 7: house, four boarded windows, forest, north path |
| South of House | 80 / 1 | 8: house, six boarded windows, forest |
| Behind House | 79 / 1 | 4: house, window, forest, east path |
| Behind House | 79 / 2 | 4: matching corrected image and window frame |
| Kitchen | 203 / 1 | 4: table, window, stairs, chimney |
| Kitchen | 203 / 2 | 4: same identifiable targets |
| Attic concept | 201 / **UNBOUND** | 4: stairs, table, knife, rope |

The corrected Behind House state 2 image is selected from the Phase 2 manifest.
Its window hotspot remains on the same wall as state 1. Menus offer Open for
the ajar state and Close for the open state, plus Examine and Enter/Go out.
No new states are introduced for object contents, poses or other details.

## Successful localization and overlap

The revised classic mailbox and its post have a comfortable 20×34 hit area.
The door and windows include their frames. Separate boarded-window rectangles
refer to the game's single boarded-window parser object. The small attic knife
is localized independently of its table. House/door and table/knife overlaps
are intentional: the supplied reader chooses the smaller matching rectangle,
with authored order for equal-area ties.

There are no suspicious large-overlap warnings in this set. The chimney's
lower edge is close to the kitchen table; its rectangle is restricted to the
visible hearth opening, and confidence remains medium because the artwork
interprets the source's downward chimney as an ordinary fireplace.

## Unresolved and excluded targets

The validator reports **14 warning records**, including repeated observations
across the two kitchen states and separate coverage/visibility notices:

- The kitchen sack and glass bottle are not reliably identifiable. Painted
  onions and a brown jug are not substituted for source objects.
- The kitchen's west passage is not independently visible, so no coordinates
  are invented for it.
- The chimney identification remains visually uncertain.
- The Attic illustration's additional window has no source-supported parser
  object/exit in that room; it receives no hotspot.
- Attic remains unbound. Its review table exists, but cannot match any runtime
  state, including state 0, through the supplied reader.

The closed mailbox's leaflet and concealed sack contents are recorded as
excluded, without clickable rectangles. Decorative preparation objects on the
kitchen table do not gain invented game interactions.

See [validation.json](validation.json) for the full machine-readable warnings.
These concern annotation coverage only and do not request new game states.

## Parser commands and acceptance

Menus use established synonyms and source-backed syntax, with one to three
ordinary commands each. Boarded-door/window refusals and attempts to close an
already closed mailbox are legitimate original-game responses.

The host VM accepted **59 distinct scene/command checks** with no vocabulary,
visibility or ambiguity warnings. Normal play reached each room, including a
lit Attic, without altering VM globals or object locations. West of House's
state-2 commands were checked in the ordinary initial room without forcing the
winning flag; the southwest command therefore receives the expected refusal
before winning. This checks parser acceptance, not a complete winning playthrough.
Transcripts are in [parser-checks.json](acceptance/parser-checks.json).

The acceptance tests cover exact scene sets, immutable artwork, deterministic
regeneration, image-change rejection, geometry, identifiers, commands, empty
and unbound tables, corrupt/truncated data and source provenance. Every pixel
of all nine scenes is compared between Python and the portable C hit tester.
The C reader also compiles independently with SDCC. See the
[test log](acceptance/unit-tests.txt) and [acceptance summary](acceptance/summary.json).

## What later integration needs

The existing mouse convention is suitable: G7 uses x=0–255, y=0–211 with the
pointer hot point at its top-left dot; scene rectangles start at screen `(0,0)`.
No graphics or interaction architecture redesign is needed. However, current
G0 has only a fixed mailbox fixture, a 256×144 scene and a fixed three-item
verb-plus-noun menu. It does not yet load these tables.

A later small adapter must select the matching 256×159 image/table, support
one to three stored full commands and submit through the existing parser path.
Missing metadata should disable clicking only. The supplied binary reader is
available for that step; JSON parsing is entirely on the host. Keyboard play is
unchanged because this pilot does not modify the application.

Phase 1 could eventually make source-backed parser nouns and traversable
features easier to distinguish from decorative objects. Missing kitchen
objects and the unsupported Attic window remain illustration-review issues.
No Phase 1 changes or artwork corrections were made here.

Regeneration instructions and the complete binary format are in
[`tools/scene-hotspots/README.md`](../../../tools/scene-hotspots/README.md).
