# Scene Facts Extractor

`tools/scene-extractor/extract_scenes.py` walks the Zork I ZIL source in the
`vendor/infocom-zork1` submodule and writes one source-backed JSON document
per game room. In the graphical pipeline, a ZIL room is called a **scene**.

The offline asset pipeline is:

1. Extract ZIL scene facts and states.
2. Add visual composition and environmental-sound enrichment.
3. Create image and sound-effect prompts.
4. Generate state-specific images and short audio assets.
5. Annotate each final image with rectangular hotspots and menu commands.

The extractor performs only step 1. It does not generate creative details,
images, prompts, audio, or hotspots.

## Project files

- `vendor/infocom-zork1/`: pinned Git submodule containing the original source.
- `tools/scene-extractor/extract_scenes.py`: ZIL parser, static analyzer, and
  command-line program.
- `schemas/scene-facts-v1.schema.json`: fixed schema for extracted facts.
- `tools/scene-extractor/test_extract_scenes.py`: extraction and validation tests.
- `docs/scene-asset-pipeline-prompt.md`: downstream image, audio, and hotspot prompt,
  including the Zephyr-80 UI specifications.
- `schemas/v9958-g7-palette.json`: exact 256-entry V9958 G7 `GGGRRRBB` palette,
  including byte codes and canonical RGB previews for offline image tools.
- `generated-scenes/`: default output directory; ignored by Git.

## Python setup

Create a project-local virtual environment with Python 3.10 or newer:

```powershell
python -m venv .venv
```

Optional activation:

```powershell
.\.venv\Scripts\Activate.ps1
```

All examples below call the environment directly, so activation is not
required. The extractor uses only the Python standard library.

## Commands

List all rooms:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py --list-rooms
```

Extract and validate every room:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --output-dir generated-scenes `
  --validate
```

Extract one room:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --room SANDY-CAVE `
  --output-dir generated-scenes `
  --validate
```

Extract several selected rooms by repeating `--room`:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --room SANDY-CAVE `
  --room MAINTENANCE-ROOM `
  --room DAM-ROOM `
  --output-dir generated-scenes `
  --validate
```

Override source and schema paths:

```powershell
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --source-root vendor\infocom-zork1 `
  --schema schemas\scene-facts-v1.schema.json `
  --output-dir generated-scenes `
  --validate
```

Room IDs are exact ZIL identifiers such as `WEST-OF-HOUSE` and
`MAINTENANCE-ROOM`. Output filenames are lowercase:

```text
generated-scenes/maintenance-room.scene.json
```

The command overwrites selected output files. It does not delete unrelated
files from the output directory.

## Tests

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tools\scene-extractor -v
```

The suite checks:

- discovery of all 110 rooms in this source snapshot;
- Sandy Cave counter states and its external shovel dependency;
- Maintenance Room flood-event discovery;
- expansion of the Dam's conditional descriptions;
- conformance of every extracted room to `schemas/scene-facts-v1.schema.json`.

## Output structure

Every document has this fixed top-level shape:

```json
{
  "schema": "scene_facts_v1",
  "scene": {
    "id": "SANDY-CAVE",
    "title": "Sandy Cave",
    "descriptions": [],
    "source_flags": [],
    "exits": [],
    "objects": [],
    "dependencies": [],
    "state_variables": [],
    "states": [],
    "transitions": [],
    "notes": [],
    "source_references": []
  }
}
```

### Descriptions

`descriptions` contains static `LDESC` text and conditional text mechanically
expanded from room `M-LOOK` routines. Text remains in source fragments; it is
not rewritten into an image prompt.

### Exits

`exits` records direction, destination when statically known, conditions,
blocked text, and routines used by dynamic exits. Unresolved logic is retained
as `source_expression` instead of being guessed.

### Objects

`objects` includes:

- objects directly in the room;
- objects nested in containers initially in the room;
- room globals and local globals;
- pseudo-objects;
- parser synonyms and adjectives;
- flags and scalar properties;
- mechanically identifiable actions and handlers;
- initial location and flag facts.

Generic actions are conservatively derived from flags including `TAKEBIT`,
`READBIT`, `TURNBIT`, `CLIMBBIT`, `DOORBIT`, and `VEHBIT`.

### Dependencies

`dependencies` records relevant definitions initially outside the scene, such
as the shovel needed in Sandy Cave, a queued routine controlling a sequence, or
a routine controlling a dynamic exit.

### State variables

`state_variables` identifies globals, object flags, room flags, and object
locations read or changed by mechanically reachable scene behavior. Initial
values come from ZIL definitions.

### States

`states` contains source-backed stable conditions derived from conditional
descriptions, recognizable action mutations, table-driven counters, flags, and
locally enabled clock routines. Entries contain their conditions, exact source
facts, entry text, and source references.

State IDs are extractor identifiers; conditions and source text are the
authoritative content. Some states describe independent axes and can overlap.
The extractor does not enumerate every possible product of room state, globals,
and arbitrary movable-object locations.

### Transitions

`transitions` records the action or clock trigger, source condition, recognized
effects, emitted text, destination state when one can be formed safely, and
whether the path is terminal.

Recognized effects include global assignments and increments, flag changes,
object movement and removal, clock-event changes, room munging, and parser
referent changes.

### Source references

`source_references` records the original file and line range for every room,
object, routine, or global used by the extraction.

## Static-analysis policy

ZIL does not declare graphical states directly. They emerge from routines,
globals, flags, object locations, events, and conditional descriptions. The
extractor therefore follows a conservative policy:

- Parse balanced ZIL forms instead of matching definitions with broad regexes.
- Never execute the game or arbitrary ZIL.
- Normalize only operations whose meaning is mechanically clear.
- Preserve unresolved logic as `source_expression`.
- Follow clock routines enabled by local scene actions.
- Resolve the common `TELL <GET ,TABLE ,COUNTER>` pattern into counter states.
- Never add camera, layout, materials, mood, invented objects, image prompts,
  audio, or hotspots.

This output is a build-time starting point, not a replacement for testing the
original game. Review complex scenes before committing generated assets.

## Validation

`--validate` checks each document before writing it. If the third-party
`jsonschema` package is installed, its Draft 2020-12 validator is used.
Otherwise the extractor uses a dependency-free validator for the schema
features used by this project.

Validation confirms shape and types. It cannot prove complete interpretation
of arbitrary ZIL.

## Downstream generation

After extraction, use `docs/scene-asset-pipeline-prompt.md`. It contains:

- UI constraints extracted from the supplied Zephyr-80 PDF;
- the exact 256-color V9958 G7 `GGGRRRBB` lookup table and quantization rules;
- the reusable multi-state pipeline prompt;
- the composition and environmental-sound enrichment schema;
- the image and sound-prompt schema;
- the rectangular hotspot schema;
- cross-document validation rules.

The pipeline is offline. Runtime play uses pre-generated 256×159 images whose
pixels are exact V9958 G7 bytes, pre-generated 8-bit PCM environmental clips no
longer than one second, and hotspot metadata. No generative model runs on the
Z80.

## Extending the extractor

When adding support for another ZIL pattern:

1. Preserve its original expression and source reference.
2. Normalize only behavior deterministic from the source.
3. Keep output compatible with `schemas/scene-facts-v1.schema.json`.
4. Add a focused test under `tools/scene-extractor/`.
5. Run the entire test suite and all-room validation.

Creative or compositional fields do not belong in the scene-facts schema. Add
them only in the downstream enrichment documents.
