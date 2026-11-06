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

## Runtime direction

The intended runtime host will:

- drive the parser and story state without requiring the Z-machine code to
  access the console or display directly;
- receive the current scene and state from the game engine;
- render the corresponding pre-generated image;
- expose only the objects, exits, and actions valid in that state;
- translate hotspot selections into ordinary parser commands;
- keep typed commands available as the authoritative interaction path.

The native Z80 host engine and parser integration are still under development.

## Licensing boundary

New West of House material is covered by the parent repository's
[MIT License](../LICENSE). The source under `vendor/infocom-zork1` retains its
own repository history, notices, and license. The parent license does not
replace or modify the submodule's licensing terms.
