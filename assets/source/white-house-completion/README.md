# Complete White House scene set

The selected set contains **20 room/state illustrations** across West, North,
South and East of House, Kitchen, Attic, Living Room and Cellar. Selection is
defined by `manifest.json`; existing selected originals are referenced in their
original source collections rather than duplicated here.

New built-in ImageGen outputs are retained at full resolution in `originals/`:
seven Living Room variants, the corrected Attic, and two pitch-black scenes.
The existing initial Living Room and Cellar images are reused. Exact generation
requests, reference relationships, tool output paths and source/prompt hashes
are preserved in `prompts/`, `metadata/` and the manifest. The Attic correction
removes its previously invented window; stairs, table, knife and rope remain.

## Room/state coverage

| Room | Numeric room | States |
| --- | ---: | --- |
| West of House | 180 | 1, 2 |
| North of House | 81 | 1 |
| South of House | 80 | 1 |
| East / Behind House | 79 | 1, 2 |
| Kitchen | 203 | 1, 2 |
| Attic | 201 | 1 lit, 2 pitch black |
| Living Room | 193 | 1–8 |
| Cellar | 72 | 1 lit, 2 pitch black |

The only image key is still room + state. Attic/Cellar darkness uses the game's
already computed `LIT` global, not ownership or the on/off flag of one particular
lamp. Carrying an unlit lantern still selects darkness. Those two states contain
no visible-object hotspots. Shared darkness hides the room description entirely;
it is not a dim rendering of visible furniture.

Living Room states use one fixed environment and crop. Edits preserve the
trophy case, lantern, sword, east doorway and west door position. The two source
combinations with an open trapdoor but no rug-moved flag use a folded-back corner
of the central rug; the moved-rug combinations keep it displaced beside the
hatch. This is an artistic interpretation of the source's explicit branches,
not an additional state or inventory simulation.

## Previews, native images and hotspots

All derived outputs are in `assets/review/white-house-completion/`:

- `contact-sheet-rgb.png`: complete set at 256 × 159.
- `contact-sheet-yae.png`: decoded YJK+YAE conversion of the complete set.
- `contact-sheet-hotspots.png`: full set with numbered regions and legends.
- `rgb/`, `yae/`, `kimg-yae/`: individual RGB PNGs, conversion previews and KIMG
  `.img` payloads (mode 3, optimized YJK+YAE, 256 × 159).
- `hotspots/scenes/`: reviewable `scene_hotspots_v1` JSON, with source-backed
  menus, exact image hashes, source pointers and explicitly hidden/unseen items.
- `hotspots/binary/`: standalone WHT1 `.wht` payloads for later packaging.
- `hotspots/annotated/`: individual overlays against the exact RGB crops.
- `conversion.json`, `validation.json`, `native-consumer-validation.json` and
  `parser-review.json`: conversion identities and validation evidence.

Hotspots use final-image coordinates and half-open edges. Smaller rectangles
win; equal areas use authored order. Thus the west passage wins over the wooden
door, and the stairs/trapdoor win over the rug. The Cellar's upper stairs are not
visible in the selected illustration, so no invented click region is assigned.
The normal parser still accepts commands outside the illustrated view.

Rebuild derived files without calling ImageGen:

```sh
python3 tools/scene-images/build_white_house_completion.py
```

The command reuses the existing encoder and WHT1 codec. It validates saved image
and prompt hashes, runtime bindings, source nouns/exits, schema, binary limits,
round trips and overlap selection. Changing an image/crop requires reviewing its
visual observations again. YJK+YAE retains the broad scene shapes; minor wood and
rug detail becomes color texture at native resolution. Both darkness conversions
decode to completely black RGB pixels.

## Integration handoff

The approved set is now integrated in `assets/manifests/resources.json` and
`assets/runtime/`. All 20 pairs are packaged in the canonical
`app/build/WestOfHouse/WOH.KPR`, together with the matching darkness-aware story,
the title and existing `advent4.pcm` music resource. The matching `woh.exe` was
rebuilt with the 20-scene lookup. Deploy the executable and archive together.

`integration.json` records the approved image/hotspot pairs and packaged bundle
hashes. Twelve pairs were added to the original eight-scene archive. The largest
WHT1 table remains 456 bytes. Source hotspot JSON used for normal builds is in
`assets/source/hotspots/scenes/`; the package builder checks it against this
collection's saved observations and source grounding. Generation/review outputs
remain separate from the runtime payloads.

Packaging evidence: `assets/review/packaging/approved-white-house.json`. The SDK
packer verified and unpacked all 43 payloads for byte comparison. The executable
passed SDK packing validation. No new target/MAME run is claimed.
