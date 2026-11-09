# Room-state recognition: corrected corpus results

The correction changes readiness scope, not state discovery. All **110 room
observation lists and 187 state entries** are unchanged, including their IDs and
predicates. Only YAML room readiness changed. Diagnostics now use
`room_state_diagnostics_v2`; runtime dictionary structure remains v1.

## Before and after

| Measure | Before | Corrected |
| --- | ---: | ---: |
| Rooms ready | 38 | 99 |
| Rooms not ready | 72 | 11 |
| Single-state rooms | 83 | 83 |
| Multiple-state rooms | 27 | 27 |
| Resolved states | 139 | 139 |
| Unresolved states | 42 | 42 |
| Ambiguous states | 4 | 4 |
| Contradictory states | 2 | 2 |
| Rooms with overlaps | 2 | 2 |
| Rooms with coverage gaps | 9 | 9 |

**99 dictionaries are internally usable for deterministic recognition; 11 are
not ready.** The 61 newly ready rooms were previously blocked by discovery or
legacy compatibility concerns outside this contract. No states were removed to
improve the totals. No assets are required or validated.

The four state counts are exclusive and sum to 187. Room issue counts below can
overlap. The old `rooms_without_complete_mapping` metric and 389 legacy issues
are no longer acceptance metrics or fields in the recognition report.

## Remaining genuine blockers

- `unresolved_states`: 10 room(s).
- `contradictory_states`: 1 room(s).
- `ambiguous_states`: 2 room(s).
- `recognition_coverage_gap`: 9 room(s).
- `recognition_validation_uncertain`: 0 room(s).

| Room not ready | Recognition blockers |
| --- | --- |
| ATTIC | `unresolved_states`, `recognition_coverage_gap` |
| CYCLOPS-ROOM | `unresolved_states`, `recognition_coverage_gap` |
| DAM-LOBBY | `unresolved_states`, `contradictory_states`, `recognition_coverage_gap` |
| GRATING-ROOM | `unresolved_states` |
| LIVING-ROOM | `unresolved_states`, `ambiguous_states`, `recognition_coverage_gap` |
| MACHINE-ROOM | `unresolved_states`, `ambiguous_states` |
| MAINTENANCE-ROOM | `unresolved_states`, `recognition_coverage_gap` |
| ROUND-ROOM | `unresolved_states`, `recognition_coverage_gap` |
| SANDY-CAVE | `recognition_coverage_gap` |
| SOUTH-TEMPLE | `unresolved_states`, `recognition_coverage_gap` |
| UP-A-TREE | `unresolved_states`, `recognition_coverage_gap` |

Unresolved conditions retain their actual unsupported expressions and compact
file/line provenance in the JSON. Overlaps retain witnesses; no first-state
priority is introduced. Coverage is checked over the declared scalar domains,
not a newly inferred reachable-game subset. Gaps are values the supported rules
cannot select; they do not prove those values can occur at a gameplay prompt.
There are no invalid symbolic observation definitions or cell-limit uncertainties
in this corpus.

## Required acceptance rooms

| Room | Result | Evidence |
| --- | --- | --- |
| ATLANTIS-ROOM | Ready | One unconditional INITIAL state, no observations |
| COLD-PASSAGE | Ready | One unconditional INITIAL state, no observations |
| WEST-OF-HOUSE | Ready | Two complementary conditions; only WON-FLAG |
| EAST-OF-HOUSE | Ready | Two complementary conditions; only KITCHEN-WINDOW.OPENBIT |
| SANDY-CAVE | Incomplete | Five preserved states; real recognition coverage gap |
| LIVING-ROOM | Incomplete | Unresolved conditions, simultaneous matching states and a gap |

Sandy Cave still observes only `BEACH-DIG` and `SCARAB.INVISIBLE`. Its indexed
digging states and [acceptance fixture](fixtures/sandy-cave.yaml) are unchanged.
The source increments before comparing with 3 and then clears invisibility, so
the discovery predicate remains counter **3**, not 4. The generic pre-increment
case is separately tested: a prior equality of 3 followed by increment becomes 4.
No fallback is invented for counter values or visibility combinations outside
the five signatures. Legacy INITIAL meaning differences, if explicitly compared,
do not affect this readiness decision.

## Reproduce

```sh
python3 tools/room-state-extractor/extract_room_states.py \
  --source-root vendor/infocom-zork1 \
  --output generated-room-states/room-states.yaml
python3 -m unittest discover -s tools/room-state-extractor -v
python3 -m unittest discover -s tools/scene-extractor -v
```

The default command writes only YAML and the recognition report. Legacy comparison
is opt-in (`--compare-legacy`) and cannot change readiness or strict mode.
Strict mode succeeds for the four ready acceptance rooms above and fails for
Sandy Cave, Living Room and the full corpus. The JSON records source hashes.

## All room dictionaries

R/U/A/C are the exclusive resolved, unresolved, ambiguous and contradictory
state classifications. Readiness is the same central validation result in YAML
and JSON. There is no asset or legacy-compatibility gate.

| Room | Recognition | R | U | A | C |
| --- | --- | ---: | ---: | ---: | ---: |
| ARAGAIN-FALLS | ready | 2 | 0 | 0 | 0 |
| ATLANTIS-ROOM | ready | 1 | 0 | 0 | 0 |
| ATTIC | incomplete | 1 | 3 | 0 | 0 |
| BAT-ROOM | ready | 1 | 0 | 0 | 0 |
| CANYON-BOTTOM | ready | 1 | 0 | 0 | 0 |
| CANYON-VIEW | ready | 1 | 0 | 0 | 0 |
| CELLAR | ready | 1 | 0 | 0 | 0 |
| CHASM-ROOM | ready | 1 | 0 | 0 | 0 |
| CLEARING | ready | 1 | 0 | 0 | 0 |
| CLIFF-MIDDLE | ready | 1 | 0 | 0 | 0 |
| COLD-PASSAGE | ready | 1 | 0 | 0 | 0 |
| CYCLOPS-ROOM | incomplete | 2 | 4 | 0 | 0 |
| DAM-BASE | ready | 1 | 0 | 0 | 0 |
| DAM-LOBBY | incomplete | 0 | 5 | 0 | 2 |
| DAM-ROOM | ready | 8 | 0 | 0 | 0 |
| DAMP-CAVE | ready | 1 | 0 | 0 | 0 |
| DEAD-END-1 | ready | 1 | 0 | 0 | 0 |
| DEAD-END-2 | ready | 1 | 0 | 0 | 0 |
| DEAD-END-3 | ready | 1 | 0 | 0 | 0 |
| DEAD-END-4 | ready | 1 | 0 | 0 | 0 |
| DEAD-END-5 | ready | 1 | 0 | 0 | 0 |
| DEEP-CANYON | ready | 3 | 0 | 0 | 0 |
| DOME-ROOM | ready | 2 | 0 | 0 | 0 |
| EAST-OF-CHASM | ready | 1 | 0 | 0 | 0 |
| EAST-OF-HOUSE | ready | 2 | 0 | 0 | 0 |
| EGYPT-ROOM | ready | 1 | 0 | 0 | 0 |
| END-OF-RAINBOW | ready | 1 | 0 | 0 | 0 |
| ENGRAVINGS-CAVE | ready | 1 | 0 | 0 | 0 |
| ENTRANCE-TO-HADES | ready | 2 | 0 | 0 | 0 |
| EW-PASSAGE | ready | 1 | 0 | 0 | 0 |
| FOREST-1 | ready | 1 | 0 | 0 | 0 |
| FOREST-2 | ready | 1 | 0 | 0 | 0 |
| FOREST-3 | ready | 1 | 0 | 0 | 0 |
| GALLERY | ready | 1 | 0 | 0 | 0 |
| GAS-ROOM | ready | 1 | 0 | 0 | 0 |
| GRATING-CLEARING | ready | 3 | 0 | 0 | 0 |
| GRATING-ROOM | incomplete | 3 | 1 | 0 | 0 |
| IN-STREAM | ready | 1 | 0 | 0 | 0 |
| KITCHEN | ready | 2 | 0 | 0 | 0 |
| LADDER-BOTTOM | ready | 1 | 0 | 0 | 0 |
| LADDER-TOP | ready | 1 | 0 | 0 | 0 |
| LAND-OF-LIVING-DEAD | ready | 1 | 0 | 0 | 0 |
| LIVING-ROOM | incomplete | 0 | 8 | 2 | 0 |
| LOUD-ROOM | ready | 2 | 0 | 0 | 0 |
| LOWER-SHAFT | ready | 1 | 0 | 0 | 0 |
| MACHINE-ROOM | incomplete | 1 | 2 | 2 | 0 |
| MAINTENANCE-ROOM | incomplete | 0 | 4 | 0 | 0 |
| MAZE-1 | ready | 1 | 0 | 0 | 0 |
| MAZE-10 | ready | 1 | 0 | 0 | 0 |
| MAZE-11 | ready | 1 | 0 | 0 | 0 |
| MAZE-12 | ready | 1 | 0 | 0 | 0 |
| MAZE-13 | ready | 1 | 0 | 0 | 0 |
| MAZE-14 | ready | 1 | 0 | 0 | 0 |
| MAZE-15 | ready | 1 | 0 | 0 | 0 |
| MAZE-2 | ready | 1 | 0 | 0 | 0 |
| MAZE-3 | ready | 1 | 0 | 0 | 0 |
| MAZE-4 | ready | 1 | 0 | 0 | 0 |
| MAZE-5 | ready | 1 | 0 | 0 | 0 |
| MAZE-6 | ready | 1 | 0 | 0 | 0 |
| MAZE-7 | ready | 1 | 0 | 0 | 0 |
| MAZE-8 | ready | 1 | 0 | 0 | 0 |
| MAZE-9 | ready | 1 | 0 | 0 | 0 |
| MINE-1 | ready | 1 | 0 | 0 | 0 |
| MINE-2 | ready | 1 | 0 | 0 | 0 |
| MINE-3 | ready | 1 | 0 | 0 | 0 |
| MINE-4 | ready | 1 | 0 | 0 | 0 |
| MINE-ENTRANCE | ready | 1 | 0 | 0 | 0 |
| MIRROR-ROOM-1 | ready | 2 | 0 | 0 | 0 |
| MIRROR-ROOM-2 | ready | 2 | 0 | 0 | 0 |
| MOUNTAINS | ready | 1 | 0 | 0 | 0 |
| NARROW-PASSAGE | ready | 1 | 0 | 0 | 0 |
| NORTH-OF-HOUSE | ready | 1 | 0 | 0 | 0 |
| NORTH-TEMPLE | ready | 1 | 0 | 0 | 0 |
| NS-PASSAGE | ready | 1 | 0 | 0 | 0 |
| ON-RAINBOW | ready | 1 | 0 | 0 | 0 |
| PATH | ready | 1 | 0 | 0 | 0 |
| RESERVOIR | ready | 2 | 0 | 0 | 0 |
| RESERVOIR-NORTH | ready | 4 | 0 | 0 | 0 |
| RESERVOIR-SOUTH | ready | 4 | 0 | 0 | 0 |
| RIVER-1 | ready | 1 | 0 | 0 | 0 |
| RIVER-2 | ready | 1 | 0 | 0 | 0 |
| RIVER-3 | ready | 1 | 0 | 0 | 0 |
| RIVER-4 | ready | 1 | 0 | 0 | 0 |
| RIVER-5 | ready | 1 | 0 | 0 | 0 |
| ROUND-ROOM | incomplete | 0 | 9 | 0 | 0 |
| SANDY-BEACH | ready | 1 | 0 | 0 | 0 |
| SANDY-CAVE | incomplete | 5 | 0 | 0 | 0 |
| SHAFT-ROOM | ready | 1 | 0 | 0 | 0 |
| SHORE | ready | 1 | 0 | 0 | 0 |
| SLIDE-ROOM | ready | 1 | 0 | 0 | 0 |
| SMALL-CAVE | ready | 1 | 0 | 0 | 0 |
| SMELLY-ROOM | ready | 1 | 0 | 0 | 0 |
| SOUTH-OF-HOUSE | ready | 1 | 0 | 0 | 0 |
| SOUTH-TEMPLE | incomplete | 0 | 4 | 0 | 0 |
| SQUEEKY-ROOM | ready | 1 | 0 | 0 | 0 |
| STONE-BARROW | ready | 1 | 0 | 0 | 0 |
| STRANGE-PASSAGE | ready | 1 | 0 | 0 | 0 |
| STREAM-VIEW | ready | 1 | 0 | 0 | 0 |
| STUDIO | ready | 1 | 0 | 0 | 0 |
| TIMBER-ROOM | ready | 1 | 0 | 0 | 0 |
| TINY-CAVE | ready | 1 | 0 | 0 | 0 |
| TORCH-ROOM | ready | 2 | 0 | 0 | 0 |
| TREASURE-ROOM | ready | 1 | 0 | 0 | 0 |
| TROLL-ROOM | ready | 1 | 0 | 0 | 0 |
| TWISTING-PASSAGE | ready | 1 | 0 | 0 | 0 |
| UP-A-TREE | incomplete | 0 | 2 | 0 | 0 |
| WEST-OF-HOUSE | ready | 2 | 0 | 0 | 0 |
| WHITE-CLIFFS-NORTH | ready | 1 | 0 | 0 | 0 |
| WHITE-CLIFFS-SOUTH | ready | 1 | 0 | 0 | 0 |
| WINDING-PASSAGE | ready | 1 | 0 | 0 | 0 |

## Preservation and verification

All correction and original extractor tests pass. The original scene-facts
extractor, schema, source ZIL, compiled story, libzvm and OS were not modified.
Existing staged libzvm build/documentation work was preserved. With
`PYTHONHASHSEED=0`, all 110 original scene-facts outputs are byte-identical to the
preserved baseline. The existing extractor has set-dependent JSON key ordering
under random hash seeds; that unrelated behavior was left unchanged.
