# House-room state extraction

Attic and Living Room now have complete runtime recognition. All eight house-area
rooms checked by the acceptance fixture are ready: West, North, South and East
of House, Kitchen, Attic, Living Room and Cellar. Other room definitions and all
previously assigned numeric state IDs are unchanged.

## What was fixed

- Resolve simple local scalar aliases in description paths. Living Room's
  `.RUG?` reads the value copied from `RUG-MOVED`; it is not a new observation.
  Bindings are per path and invalidated by writes or unknown calls.
- Keep the existing ordered `COND` semantics when expanding the description.
  The three boolean observations produce eight exclusive, exhaustive conditions.
- Use statically proven room guards when walking shared object handlers.
  Attic's rope handler immediately selects its non-Dome branch, so later
  Dome-only tying/untying branches cannot establish Attic states.
- Reconcile supported action replies with an already validated LOOK partition
  for the same fixtures. Rug/trapdoor replies describe transitions within the
  Living Room partition. Handlers with unresolved candidate paths are retained
  conservatively, rather than silently discarded.

## Current runtime bindings

The authoritative IDs are generated in `generated-zil/woh-state-ids.json`.
Attic is **room 201**: state **1** is lit and state **2** is pitch black.
Cellar is **room 72** with the same lit/dark state numbering. Both observe the
source-computed `LIT` VM global: nonzero selects state 1, zero selects state 2.
This uses any available game light source; carrying an unlit lantern is still
dark. Moving the portable rope does not create another room image.

The shared `DESCRIBE-ROOM` darkness branch runs before a room-specific
description. The earlier extraction missed that gate. The extractor now
includes it for these two explicitly reviewed rooms; other underground rooms
still need a separate visibility review. The native resolver only reads the
cached global and never calls `LIT?`, which has parser side effects.

Living Room is **room 193**, observing only `MAGIC-FLAG`, `RUG-MOVED` and
`TRAP-DOOR.OPENBIT`:

| State | West door | Rug moved | Trapdoor |
| --- | --- | --- | --- |
| 1 | Nailed shut | No | Covered, closed |
| 2 | Nailed shut | Yes | Open |
| 3 | Cyclops-shaped opening | No | Covered, closed |
| 4 | Cyclops-shaped opening | No | Open |
| 5 | Cyclops-shaped opening | Yes | Open |
| 6 | Cyclops-shaped opening | Yes | Exposed, closed |
| 7 | Nailed shut | Yes | Exposed, closed |
| 8 | Nailed shut | No | Open |

Gothic lettering remains in both door conditions. States 4 and 8 preserve an
explicit description branch for an open trapdoor without the rug-moved flag;
full-domain validation does not claim every combination is reachable in play.
State 1 is the initial Living Room presentation. Future changes to the source
definitions can change these generated IDs; always deploy matching resources.

## Outputs and verification

The symbolic dictionary, native ZIL resolver, compiled story/sidecar and Phase 1
specifications/prompts have been regenerated. The story bundle is in
`libzvm/build/`; the canonical application bundle is `app/build/WestOfHouse/`. The latest
darkness-aware story is staged in `libzvm/build/` for the next combined build.
The image-completion work does not repack that archive or rebuild the executable
while music integration is in progress.
The global collection now has 101 ready rooms, 9 incomplete rooms and 138
assigned image pairs. The completed White House art set has 20 pairs, including
the two darkness presentations, with reviewable hotspot tables and WHT1 bytes.
See `assets/source/white-house-completion/README.md`.

Checks passed: 29 room-state tests, 15 state-reporting tests, 5 scene-extractor
tests and 6 scene-art tests. The compiled story checks all eight Living Room
combinations and a house walkthrough (lamp, lit Attic, rug, open/close trapdoor), plus actual
Attic/Cellar light-on/off transitions and arrival in the Attic without a lamp.
Walkthrough text matches the same-toolchain baseline byte for byte. Full Living
Room combinations use controlled VM writes, including the Cyclops flag; this is
not a complete winning walkthrough. No new MAME acceptance run is claimed.

Current machine-readable evidence is in
[`state-acceptance.json`](../../assets/review/white-house-completion/state-acceptance.json).
The earlier lit-only milestone is preserved in
[`house-extraction.json`](../state-reporting/acceptance-data/house-extraction.json).
Earlier target acceptance reports retain their historical story hashes.
