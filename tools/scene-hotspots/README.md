# Phase 2B hotspot pilot

Current packaging and app integration: [assets/README.md](../../assets/README.md).
This tool retains the independent Phase 2B review/compiler workflow.

This workflow produces **reviewable JSON, annotated diagnostics and standalone
WHT1 binary payloads** for the nine existing White House illustrations.
It does not pack KRES resources, change WOH.EXE, alter artwork, regenerate
Phase 1, or modify ZIL. Resource packaging and application integration are
deferred by the user's instruction.

## Regenerate and check

From the WestOfHouse repository root, with Python 3.10+, Pillow and jsonschema:

```sh
python3 -m pip install -r tools/scene-hotspots/requirements.txt
python3 tools/scene-hotspots/generate.py
python3 tools/scene-hotspots/validate.py
python3 -m unittest discover -s tools/scene-hotspots -p test_hotspots.py -v
python3 tools/scene-hotspots/check_parser.py
```

Outputs are in [`assets/source/hotspots`](../../assets/source/hotspots/).
Start with its [contact sheet](../../assets/review/hotspots/contact-sheet.png)
and [review](../../assets/review/hotspots/review.md).

`--pilot`, `--observations`, `--facts`, `--source` and `--output` override the
inputs and output directory. Run with `--help` for defaults. The parser checker
also requires the existing compiled story/bindings, a C compiler and the
repository's host VM adapter. It builds the unchanged libzvm in temporary
storage and submits normal text commands; it does not recompile or mutate ZIL.

## What is automatic, and what uses vision

The model's visual decisions are saved explicitly in `observations.json`.
For this pilot, the assistant inspected the final RGB previews, source images
and annotated diagnostics. These are real visual estimates, not object-name
coordinate templates. The observations retain image hashes, rectangles,
confidence, rationale and exclusions.

The compiler deterministically grounds those observations in `scene_facts_v1`,
derives concise source-backed parser menus, validates the results, serializes
them and produces diagnostic previews. It does not call a vision service by
itself. The reusable AI-assisted step is described in [WORKFLOW.md](WORKFLOW.md).
No API key or paid model call is needed for unchanged-input regeneration.

This is a deliberately scoped pilot. Its reviewed menu policy covers these
rooms' objects, parser synonyms and exits. Unsupported new nouns fail rather
than acquiring invented commands. Adding another room requires new visual
observations and review of its menu vocabulary.

## Coordinates, binding and overlap

All rectangles are `[x, y, width, height]` in the **final 256×159 RGB preview**.
Top-left is `(0,0)`; hit tests use `x <= pointer_x < x+width` and the same rule
for y. Diagnostics outline through `x+width-1`, `y+height-1`, then enlarge with
nearest-neighbor sampling. There is no further source-to-preview transform.

JSON retains the selected original PNG hash, final preview hash and exact
Phase 2 crop box. Localization fails if the final preview changes until the
vision step is reviewed again. The hash identifies the annotated PNG, not a
future native G7/YJK payload. A later packer must keep the chosen image and
matching table together.

The existing `(room, state)` pair remains the only runtime key. Attic stays
`state: null`, `binding: unbound-concept`; its binary explicitly encodes an
unbound marker, and the example consumer refuses runtime matches/hits for it.
Nothing is assigned to state 0.

Small regions take precedence over larger ones, with authored order breaking
equal-area ties. Thus the mailbox beats the house, and the knife beats its
table. Background house regions occupy the visible building, not the whole
scene. Repeated boarded windows have separate hotspot IDs but intentionally
share the source's single parser object and menu.

## Files

- `schemas/scene-hotspots-v1.schema.json`: the `scene_hotspots_v1` contract.
- `observations.json`: model-authored visual input, tied to exact previews.
- `generate.py`: source lookup, menu derivation, manifests and diagnostics.
- `validate.py`: schema, runtime identity, image, source, geometry, command,
  overlap, uncertainty and missing-object checks.
- `codec.py`: binary encoder, independent decoder and reference hit testing.
- `consumer.h`, `consumer.c`: optional bounded C reader for later integration.
- `test_hotspots.py`: corpus, malformed data, per-pixel hit tests and C/Python
  agreement. Host tests compile the C reader with warnings treated as errors.
- `check_parser.py`: ordinary-play parser-command checks and transcripts.

The output index maps each asset to its JSON, `.wht` payload and annotated PNG.
Uncertain/missing candidates remain in `unresolved` and `validation.json`.
They never acquire fabricated rectangles or additional game states.

## WHT1 binary format

All integers are little-endian. Bytes are serialized explicitly; C structure
padding is never on disk. A payload is at most 4,096 bytes; this pilot's tables
are 225–456 bytes each. The extension `.wht` denotes one table, not a container.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `WHT1` magic/version |
| 4 | u8 | Flags: bit 0 = unbound concept; all other bits zero |
| 5 | u8 | Hotspot count, 0–12 |
| 6 | u16 | Existing numeric room |
| 8 | u16 | Existing state; `65535` only when the unbound flag is set |
| 10 | u16 | Width, 256 |
| 12 | u16 | Height, 159 |
| 14 | u16 | Body length, excluding the 16-byte header |
| 16 | 32 bytes | SHA-256 of the exact annotated final RGB PNG |
| 48 | variable | Hotspot entries in authored order |

Each entry contains:

| Field | Encoding |
| --- | --- |
| Runtime hotspot ordinal | u8, sequential from 1, corresponding to JSON array order |
| x, y | u8 each |
| width | u16 (allows 256) |
| height | u8 |
| action count | u8, 1–3 |
| object label length | u8, 1–31 |
| object label | ASCII bytes, no terminator |
| each action | u8 label length (1–8), u8 command length (1–80), then label bytes and full command bytes |

All strings are printable ASCII; commands are lowercase words separated by
single spaces, with no terminators, control characters or embedded newlines.
The reader supplies NUL-terminated copies for the host UI. Object identifiers,
source references, confidence and visual rationale stay in JSON, since the
runtime needs only targets, rectangles, menu labels and ordinary commands.

## Later WOH integration

This pilot installs nothing. Current G0 still uses its fixed mailbox fixture,
256×144 image area and three-item verb-plus-noun menu. These 256×159 tables
must **not** be placed over that old image. Later integration will need:

1. Matching image/table selection for the existing room/state pair.
2. The intended 256×159 scene geometry, with pixel coordinates from libGFX's
   top-left mouse hot point (G7 x=0–255, display y=0–211).
3. One to three menu items using the stored **complete** command string.
   `climb down chimney`, for example, cannot be formed with a single common
   noun and the present verb-only widget labels.
4. Optional loading: missing tables simply disable clicking; image display and
   keyboard play continue. Suggested commands still use the existing submit
   path and original parser. No gameplay logic belongs in the adapter.

The supplied reader has no filesystem, KRES, graphics, VM, function-pointer
table, writable static initializer or allocation dependency. It validates
the whole payload before exposing it, supports empty tables and selects the
smallest hit region. Its only multiplication computes a bounded 16-bit area;
SDCC emits the ordinary `__mulint` helper. No banking or placement mechanism is
introduced. If later linked, the SDK's executable placer owns its placement.

For later KRES packaging, each `.wht` can be one opaque resource, alongside its
corresponding image. Resource naming belongs to that later application step;
the host filenames are not a new filesystem convention or resource hierarchy.
