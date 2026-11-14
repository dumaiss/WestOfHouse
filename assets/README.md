# West of House assets

`manifests/resources.json` is the authoritative package selection. KRES uses
flat, case-sensitive, opaque names, not directories. The build stages only
these declared names before calling the SDK's `kres pack`.

```
assets/
  manifests/resources.json       Package names, types, pairs and dependencies
  source/
    scene-facts/                 Rebuildable scene_facts_v1 extraction
    art-direction/               Phase 1 world/room/state specs and prompts
    white-house/                 Nine approved selections, requests, provenance
    hotspots/scenes/             Editable Phase 2B rectangular annotations
    ui/WoHTitle.png              Original title source
  runtime/
    story/zork1.z3               Exact instrumented story bytes
    scenes/room-*-state-*.img     Eight complete 256×159 YJK+YAE KIMG scenes
    hotspots/room-*-state-*.wht   Eight WHT1 action/hit tables
    ui/WoHTitle.img              Existing 256×212 YJK title (unchanged)
  review/
    art-direction/               Phase 1 validation and readiness review
    white-house/                 RGB/YJK previews, sheets, conversion reports
    hotspots/                    Overlays, sheets and acceptance evidence
    migration.json               Move inventory and binary preservation hashes
```

The nine source illustrations and generation prompts are retained. Attic room
201 is an **unbound concept**, with its conversions/hotspot binary in review;
it is never assigned state 0 or packaged as a runtime pair. Existing visual
quality/continuity warnings remain in review; packaging is not art approval.
The earlier `docs/west_of_house_256x144.g7` is a historical G0 fixture, retained
for its earlier acceptance records; the current build does not consume it.

## Build, inspect and deploy

From the repository root:

```sh
make -C app
../ZephyrOS/build/kos-sdk-0.2.0/tools/kres list app/build/WestOfHouse/WOH.KPR
../ZephyrOS/build/kos-sdk-0.2.0/tools/kres verify app/build/WestOfHouse/WOH.KPR
make -C app test
python3 -m unittest discover -s tools/resources -p 'test_*.py'
```

Use only `app/build/WestOfHouse/woh.exe` and `WOH.KPR` for deployment.
Put the executable in `/bin`; put the archive in the game/document directory.
`cd /games/kona` then `woh` opens `WOH.KPR` in that CWD. Alternatively pass
an archive pathname: `woh /games/kona/WOH.KPR`. No loose `.z3`, `.WOH`, scene
or hotspot files are needed on the target. Names inside KRES are not 8.3 paths;
the physical archive name is 8.3.

The title waits for a key, then WOH starts the archived story. Exact room/state
pairs select images and their tables. Unsupported pairs show a blank scene
and have no hotspots. Missing/bad hotspot tables disable mouse actions but
leave the image and keyboard usable. No state fallback or invented mapping
is applied. Smallest containing rectangle wins, with authored order breaking
ties; menu actions submit their full command to the normal story parser.

## Rebuild without generating artwork

```sh
python3 tools/resources/build.py --rebuild-images
python3 tools/scene-images/build_previews.py
python3 tools/scene-images/verify_pilot.py
python3 tools/scene-hotspots/validate.py
```

The normal build hashes each source, recipe, Pillow version and SDK encoder.
Only missing/stale scene conversions are rebuilt. Hotspot binaries are
compiled from the editable source manifests, not regenerated annotations.
The recorded crop and BOX resize are reused; the encoder is unchanged.
Review previews/contact sheets are explicit developer commands, not game
build outputs. `--rebuild-images` rebuilds YJK+YAE payloads, not illustrations.
The YJK review command uses the sibling encoder/decoder retained from Phase 2.

Generated C names in `app/build/assets/resource_names.h` come from the same
manifest; there is no second manually maintained ID registry. Reports and
catalog listings are in `app/build/assets`. Package verification unpacks the
archive with the SDK tool and compares every payload to its declared file.

## Add or update a scene/state

1. Establish the existing runtime room/state mapping first; do not make image
   states for movable props or micro-details. Rebuild source facts with
   `python3 tools/scene-extractor/extract_scenes.py --validate`, then Phase 1
   with `python3 tools/scene-art/generate.py` when its inputs change.
2. Keep source artwork, original prompt/request and generation/reference hashes
   under `source/white-house` (or another named collection). Never overwrite
   a source PNG during conversion. Update the selected image manifest and crop.
3. Review final 256×159 geometry. Edit `source/hotspots/scenes/<key>.json` and
   the corresponding reviewed vision observations in
   `tools/scene-hotspots/observations.json`. Image/provenance hashes must match;
   changed artwork requires a new localization review. Rectangles are integer
   `[x,y,width,height]`, half-open, one rectangle per object. Empty tables work.
4. `python3 tools/scene-hotspots/generate.py` explicitly regenerates the pilot
   annotations/actions from source facts and reviewed observations, plus
   overlays. It overwrites editable annotations; use normal `make -C app`
   to compile edits without replacing them. Validate with `validate.py`.
5. Add the scene and hotspot entries to `manifests/resources.json`, preserving
   `room-N-state-S.img` / `.wht` keys and mutual associations. Include source
   paths, the Phase 1 specification and approved crop. Run build and tests.

The title IMG is preencoded: it is copied byte for byte, not automatically
rebuilt from its PNG. Replace it deliberately with a valid full-screen YJK
KIMG when changing the title, then rebuild the package.

## Version control and rebuildable files

Commit source PNGs, prompts, provenance, art/state specs, editable hotspots,
the resource manifest and preencoded title IMG. Runtime story/YJK+YAE/WHT files
are deterministic generated payloads retained here for inspection; they can
be rebuilt by the resource builder (story input comes from the existing ZIL
build). Review sheets, previews and existing reports are retained for review,
not deployment. `scene-facts/`, Python caches and `app/build/` are ignored;
rebuild facts before a clean asset build. Historical acceptance reports retain
their generation-time hashes; `review/migration.json` explains moved paths.

## SDK requirement

Use the installed SDK with `tools/kres`, split KRES streaming/state members,
`kres_reader` declared resident, the data-only placement fix, and the
keyboard key decoder `kos/key.h` (in libkos), which the graphics app reads keys
through without linking libtui. They change
no OS kernel, driver, KRES format or VM behavior. Rebuild that SDK with
`make sdk` in the sibling ZephyrOS repository if needed.

## YJK+YAE gameplay graphics

The runtime scene recipe is now `mode: yae`, `yjk_quality: optimized`.
The application uses `GFX_MODE_256x212_YJK_YAE` and a seven-entry UI palette.
Palette drawing uses indices 0–6; the hardware sprite pointer retains its fixed
colours. Menu positions and save-under copies align to four-pixel J/K groups.
The story, room/state keys, crop geometry and hotspot/action payloads are unchanged.

Earlier G7 IMG comparisons are in `review/white-house/kimg-g7/`; YJK and
YJK+YAE comparisons remain in `kimg/` and `kimg-yae/`. The title stays pure YJK
until the first key, then WOH switches to YJK+YAE for gameplay. Rebuild the SDK
with its YAE drawing support before building the application.
