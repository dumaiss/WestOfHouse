# Phase 1 scene art

Transforms existing `scene_facts_v1` extractions into `scene_art_v1` art
specifications and standalone English image prompts. It produces no images
and changes no ZIL, VM, renderer or original scene facts.

## Image selection

The existing numeric **(room, state)** pair is the only image key. Each positive
pair gets one complete illustration, even when another state looks similar.
Props are coarse source room dressing. Inventory changes, hidden contents and
character poses do not introduce additional keys or readiness requirements.
An existing state may specify a different appearance; that presentation takes
precedence over initial source dressing.

`prompts/1.md` is a convenient copy of the prompt embedded in `states/1.json`;
these are two representations of one prompt, not two image requests.
`prompts/2.md`, when present, belongs to a different existing runtime state.
Its full room description intentionally repeats so that either prompt can be
used independently by an image generator.

## Run

From the WestOfHouse repository root, using Python 3.10 or newer:

```sh
python3 -m pip install -r tools/scene-art/requirements.txt
python3 tools/scene-art/generate.py --room ARAGAIN-FALLS --output /tmp/woh-art-pilot
python3 tools/scene-art/generate.py
python3 tools/scene-art/validate_art.py
python3 -m unittest discover -s tools/scene-art -p test_scene_art.py -v
```

Regenerate one room in the existing collection:

```sh
python3 tools/scene-art/generate.py --room ARAGAIN-FALLS
```

`--room` can be repeated. Unselected room artifacts and manifest records are
preserved. Identical JSON and prompt files are not rewritten. Validation checks
the whole output collection, so changes to shared direction or schema should
be followed by a full generation. `review.md` is refreshed on every run.

All paths have repository-relative defaults, including `--input`, `--output`,
`--source`, `--mapping`, `--dictionary`, `--room-ids`, `--story` and `--direction`.
Use `--help` for the complete interface.

## Inputs and authority

- `assets/source/scene-facts/*.scene.json`: original facts and source references.
- `generated-zil/woh-state-ids.json`: authoritative existing local runtime IDs.
- `generated-room-states/room-states.yaml`: corresponding recognition predicates.
- `vendor/infocom-zork1`: read-only source used to verify predicates and text.
- `runtime-room-ids.json`: numeric room bindings for the compiled story, pinned
  to its SHA-256. Room numbers follow the existing binding tool's one-based
  `.OBJECT` declaration order in
  `build/state-reporting/instrumented/zork1_data.zap`. Regenerate this proof
  deliberately with the state-reporting build when the story changes; a hash
  mismatch stops generation rather than guessing room numbers.
- `direction.json`: model-authored creative interpretation of the collection.
  `style` and `continuity` describe the shared world; `rooms` contains individual
  camera, composition, palette and assumptions; `presentations` records visual
  treatment of existing multi-state variants, keyed by symbolic runtime IDs.

Regeneration is deterministic. It does not call a model or interpret creative
decisions through keyword templates. New creative work is reviewed in
`direction.json`, with canonical source facts kept separately in the output.
Shared continuity summaries cite the input scenes from which they were drawn.

The authority bridge reuses the existing static discovery and predicate
equivalence helpers. It does not execute ZIL. `ACTION-*` entries and
`stable: true` do not establish image states. Missing legacy relationships are
reported without withholding an authoritative runtime image. State 0 remains
an unsupported recognition result, with no assigned image.

## Output

```text
assets/source/art-direction/
  world/style-guide.json
  world/continuity.json
  rooms/<ROOM>/room.json
  rooms/<ROOM>/states/<runtime-state>.json
  rooms/<ROOM>/prompts/<runtime-state>.md
  manifest.json
  validation.json
  review.md
```

The manifest associates each existing pair with its specification, prompt and
asset key. Asset keys are identifiers for future illustrations; no image files
exist yet. The first numeric state is a preferred future reference for other
variants, not an assertion about the game's initial state.

Room files separate canonical facts, inferences and artistic choices. State
files include complete canonical text, JSON pointers and source references,
conditions, legacy associations, invariants, differences and full prompts.
Parser globals and concealed initial contents remain source context rather
than automatically becoming visible standalone props.

The target is **256×159**, following the user's correction to the original
package. Prompts request painterly adventure illustration, roughly 256
significant colors, no text/UI/pixel grid, and a 4:3 source composition protected
for a center crop to 256:159. Protect the central 82.8% of height and 90% of width.
YJK conversion belongs to a later phase.

## Validation and maintenance

`schemas/scene-art-v1.schema.json` defines the artifact contract.
`validate_art.py` additionally checks uniqueness, existing runtime IDs,
story/source hashes, static predicates, canonical text and pointers, source
line ranges, coarse dressing, cross-state invariants, editorial treatment,
prompt copies and unlisted generated files. These checks establish structural
and source consistency; the creative direction remains reviewable editorial
work.

The acceptance tests cover the Aragain Falls mapping (including its reversed
legacy numbering), exclusion of action states, deterministic regeneration,
single-room preservation, state 0, duplicate pairs, broken provenance, wrong
story bindings, and whole-scene appearance controlled only by existing states.

Generated scene facts must already exist. See
[`docs/SCENE-EXTRACTOR.md`](../../docs/SCENE-EXTRACTOR.md) for the upstream process.
