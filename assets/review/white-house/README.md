# White House pilot artifacts

Start with [the evaluation](EVALUATION.md) and the
[original contact sheet](contact-sheet-originals.png).

- `../../source/white-house/manifest.json`: selected original, numeric room/state pair, prompt,
  references and preview paths for each scene.
- `../../source/white-house/originals/`: the nine selected high-resolution PNGs, one per scene, using
  canonical room/state filenames. Superseded generations have been removed.
- `previews/rgb/`: cropped and reduced 256×159 RGB PNGs.
- `previews/yjk/`: 256×159 PNGs decoded from the converted KIMG payloads.
- `kimg/`: optimized YJK `.img` files for review. The unbound Attic file is a
  concept conversion, not a registered runtime resource.
- `../../source/white-house/prompts/phase1/`: unchanged Phase 1 prompt copies; the Attic file is a room-
  derived concept brief because Phase 1 has no mapped Attic state prompt.
- `../../source/white-house/prompts/requests/`: the generation/edit request for each selected image.
  Fixed crop percentages were replaced with the pilot's natural
  composition and sensible-cropping instruction. Phase 1 files were not edited.
- `../../source/white-house/metadata/`: generation provenance and reference hashes.
- `metadata/`: historical conversion measurements and acceptance evidence.

## Reference relationships

The first West of House image established the visual vocabulary for North,
South, Behind House and Kitchen. Kitchen also used the original rear-window
image. Each initial alternate state used its own room's base image as an edit
target. The mailbox revision edited West of House state 1, then state 2 was
regenerated from that revised base. Attic used the revised house and Kitchen.
Behind House state 2 was subsequently corrected using a localized window-only
edit of its original state 1, preserving the window wall and chimney position.
Metadata records hashes of the images actually referenced. When an earlier
reference was superseded and removed, it has `retained: false` and `path: null`;
its hash remains as provenance rather than pointing to a different image now
using the same filename. Only the selected generations are kept.

Built-in image generation does not expose a model identifier, seed or complete
reproducibility controls. Metadata records that limitation; regeneration of
images is not promised to be deterministic.

## Rebuild previews

From the WestOfHouse repository root:

```sh
python3 tools/scene-images/build_previews.py
python3 tools/scene-images/verify_pilot.py
```

Requires Pillow and the sibling `ZephyrOS/tools/kimg.py` and
`kimg_compare.py`. This runs no model calls. It uses the existing encoder's
`optimized` YJK mode and does not change encoder code. Sheet labels are not
included in scene assets. Generation-time protected hashes are retained as historical evidence;
current verification checks selected originals, requests and reference hashes.

`record_generation.py` was used to preserve built-in imagegen outputs after
each call. It follows the user's selected-only policy: new approved generations
replace the canonical asset, and superseded image files are not archived.

## YJK+YAE comparisons

- `kimg-g7/`: earlier G7 runtime images retained for comparison.
- `kimg-yae/`: nine 256×159 YJK+YAE KIMG files (mode 3), with the same
  room/state filenames and approved crops as the other comparisons.
- `previews/yae/`: PNGs decoded by the SDK converter.
- `contact-sheet-g7-yjk-yae.png`: native-resolution G7/YJK/YJK+YAE comparison.
- `metadata/conversion-yae.json`: input/output hashes and conversion settings.

Rebuild with `python3 tools/scene-images/build_yae.py`. This uses the installed
SDK's optimized `--mode yae` encoder. Scene pixels have the A flag clear and
use YAE-compatible luminance; palette entries remain available for application
UI drawing. The unbound Attic concept has no G7 runtime counterpart.
