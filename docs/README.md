# West of House Technical Guide

This document describes the repository organization, development setup, scene
extraction workflow, and target-platform constraints. The project introduction
and acknowledgements remain in the [root README](../README.md).

## Target platform

West of House targets:

- [Zephyr-80](https://github.com/dumaiss/Zephyr-80)
- [KonaOS](https://github.com/dumaiss/ZephyrOS)
- Z80 CPU
- Yamaha V9958 G7 graphics at 256×212 pixels
- A 256×159 static scene area above the text interface
- Direct 8-bit `GGGRRRBB` scene pixels
- Short, pre-generated 8-bit PCM environmental effects

Artwork, audio, scene metadata, and hotspots are generated offline. No
generative model runs during gameplay.

## Repository layout

```text
docs/
  README.md                       This technical guide
  SCENE-EXTRACTOR.md              Extractor usage and output semantics
  scene-asset-pipeline-prompt.md  Enrichment and asset-generation pipeline
schemas/
  scene-facts-v1.schema.json      Mechanical ZIL extraction contract
  v9958-g7-palette.json           Exact 256-entry V9958 color table
tools/
  scene-extractor/
    extract_scenes.py             ZIL parser and static scene analyzer
    test_extract_scenes.py        Extractor and schema tests
vendor/
  infocom-zork1/                  Original source Git submodule
generated-scenes/                 Local generated output; not committed
```

The original Infocom source is isolated from new West of House code.
`vendor/infocom-zork1` is a Git submodule pointing to
[`dumaiss/zork1`](https://github.com/dumaiss/zork1), a fork of
[`historicalsource/zork1`](https://github.com/historicalsource/zork1).

The parent repository records an exact submodule commit. Updating that commit
should be an intentional, reviewed change. New implementation code,
documentation, schemas, or generated assets must not be added inside the
submodule.

## Clone and initialize

Clone recursively so the original source is available immediately:

```powershell
git clone --recurse-submodules https://github.com/dumaiss/WestOfHouse.git
cd WestOfHouse
```

For an existing clone that does not yet contain the submodule:

```powershell
git submodule update --init --recursive
```

## Python environment

The extractor requires Python 3.10 or newer and otherwise uses only the Python
standard library. The optional `jsonschema` package provides full Draft
2020-12 validation; a dependency-free validator covers the schema features
used by this project.

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
```

The following examples invoke the environment directly, so activation is
optional.

## Scene extraction

List every discovered room:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py --list-rooms
```

Extract and validate one scene:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --room SANDY-CAVE `
  --validate
```

Extract and validate all scenes:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py --validate
```

The defaults are:

- source: `vendor/infocom-zork1`
- schema: `schemas/scene-facts-v1.schema.json`
- output: `generated-scenes/`

Generated scene files are ignored by Git. The extractor overwrites selected
output files but does not delete unrelated output.

For the complete command reference and static-analysis policy, see
[SCENE-EXTRACTOR.md](SCENE-EXTRACTOR.md).

## Tests

Run the complete suite from the repository root:

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tools\scene-extractor -v
```

The suite verifies room discovery, representative state and event extraction,
and schema conformance for all 110 rooms in the pinned source snapshot.

## Offline scene-asset pipeline

The asset pipeline proceeds in four broad stages:

1. Mechanically extract source-backed room, object, exit, action, state, and
   transition facts from ZIL.
2. Enrich every stable state with visual composition and small environmental
   sound ideas while preserving provenance.
3. Generate state-specific images and audio offline, then quantize images to
   the exact V9958 G7 color space.
4. Inspect each final image and emit rectangular hotspot metadata using only
   source-backed target IDs and legal parser actions.

Each distinct visible room state receives a complete image rather than a
runtime overlay. Hotspots are metadata only and are never painted into the
scene.

See [scene-asset-pipeline-prompt.md](scene-asset-pipeline-prompt.md) for the
reusable prompt, enrichment schema, prompt-bundle schema, hotspot schema, and
cross-document invariants.

## V9958 image constraints

Final scene artwork is exactly 256×159 pixels. In G7 mode, each pixel byte is
the color value itself:

```text
GGGRRRBB
```

- Green: 3 bits
- Red: 3 bits
- Blue: 2 bits
- Alpha: none

True-color model output is only an intermediate. Before hotspot annotation,
every scene must be resized and quantized to
[`schemas/v9958-g7-palette.json`](../schemas/v9958-g7-palette.json). The
palette file contains every byte value, its component codes, and a canonical
RGB preview for modern tools.

## Host-side runtime design

### Authority and responsibilities

The Z-machine interpreter and the existing Zork story already contain the game
engine, parser, world model, and rules. They remain authoritative. The host is
a presentation layer: it renders the world described by the current
Z-machine state and submits commands back to the existing parser.

The host must not:

- reproduce ZIL rules or decide whether a command is valid;
- invent state, objects, transitions, or semantic events that the story does
  not contain;
- require fictional engine messages such as `PLAY_SOUND`;
- alter the result of a parser command.

The parser validates every submitted command, including commands suggested by
a hotspot.

### Observing existing Z-machine state

A presentation adapter must expose the subset of existing VM state required by
the asset catalog. Candidate inputs include:

- the current room;
- the object tree and object locations;
- object attribute bits such as visibility, open/closed, or lighting flags;
- relevant globals, flags, and counters;
- the narrative text already emitted by the story.

These are observations of data the Z-machine already owns, not new game facts.
The exact observation mechanism is still to be designed. For an unmodified Z3
story, it may involve reading the object table and selected globals or
observing the corresponding interpreter operations.

The adapter must not interpret those values as new game rules. Build-time
tables map the observed values to stable numeric scene, state, and object IDs.

### Complete image and hotspot set per state

Every renderable state of a scene has:

- one complete 256×159 image;
- one hotspot set authored against that exact image.

There are no partial scene images and no runtime image overlays. When the
selected state changes, the host replaces the complete scene bitmap and uses
the hotspot set belonging to the new image.

```text
observed Z-machine state
        ↓
scene-state lookup
        ↓
complete image + matching hotspot set
```

Objects that are inactive, absent, removed, or invisible in the selected state
must not be hit-testable. Existing Z-machine object state supplies that
information; the runtime representation can apply it as active/inactive bits
over the object catalog. Build-time `not_visible` records may be retained for
validation but need not be stored in the final runtime data.

### Object catalog and suggested commands

The mechanically extracted scene facts may contain a set of possible action
verbs for an object. That complete set is useful for provenance and asset
generation, but it does not need to become a complete context menu.

Each generated hotspot normally selects one preferred action for display:

```text
target: BLUE-BUTTON
suggested verb: READ
suggested command: READ BLUE BUTTON
```

The suggestion is not an exhaustive declaration of what the parser accepts.
Clicking the hotspot places the suggested command in the input line. The user
may edit it before submitting it—for example, changing `READ BLUE BUTTON` to
`PUSH BLUE BUTTON`.

The resulting separation is:

```text
scene facts       all mechanically extracted possible actions
state hotspots    visible targets, rectangles, and one suggested command
Zork parser       final authority over command validity and outcome
```

Unless the source proves otherwise, the design assumes that an object's
displayed suggestion can remain fixed while the parser produces
state-dependent results. If a visible object's suggested verb must itself
change by state, that choice belongs in each state's independently generated
hotspot set.

### Turn flow

```text
typed or hotspot-suggested command
        ↓
existing Zork parser and story execute one turn
        ↓
existing VM state and narrative output change
        ↓
presentation adapter observes the new state
        ↓
host selects the complete image and hotspot set
```

The host does not need a high-level transition notification. It can render the
state observed after the turn. Whether the adapter supplies a complete
snapshot or a compact delta is an implementation and storage decision, not a
change to the game model.

Environmental audio may later be associated with a complete generated scene
state when justified by the extracted facts. It must not depend on an event
invented for the Z-machine.

### Build-time and runtime representations

JSON is an offline extraction, enrichment, generation, and validation format.
The Z80 will not parse it. A later build step will compile the required data
into compact binary structures using numeric IDs.

The binary design is intentionally deferred until the required runtime data
and Z-machine observation boundary are proven. Work still required includes:

1. Identify the minimum existing VM values that distinguish every rendered
   scene state.
2. Determine how the interpreter or adapter exposes those values without
   changing story semantics.
3. Define the deterministic mapping from observed VM state to a complete image
   and hotspot set.
4. Verify whether any visible object's preferred hotspot suggestion genuinely
   changes independently of the scene state.
5. Design the compact Z80 binary catalog, state records, object-active bits,
   and asset indexing.

The native Z80 host and Z-machine presentation adapter are still under
development.

## Licensing boundary

New West of House material is covered by the parent repository's
[MIT License](../LICENSE). The source under `vendor/infocom-zork1` retains its
own repository history, notices, and license. The parent license does not
replace or modify the submodule's licensing terms.
