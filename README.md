# West of House

**Illustrated Interactive Fiction for the Zephyr-80**

An official Kita's Retro Brew project.

You are standing in an open field west of a white house, with a boarded front
door.

*(There is a small mailbox here.)*

The Great Underground Empire has been extensively renovated. The puzzles
remain fiendish, the prose remains dry, and the grues remain uninterested in
your welfare. We have, however, taken the liberty of providing pictures.

*West of House* preserves the original parser-driven story while adding
state-specific static illustrations, mouse hotspots, compact context menus, and
short environmental sound effects. Text remains authoritative, and typing
remains a first-class way to play.

## Target platform

- [Zephyr-80](https://github.com/dumaiss/Zephyr-80)
- [KonaOS](https://github.com/dumaiss/ZephyrOS)
- Z80 CPU
- Yamaha V9958 G7 graphics at 256×212
- 256×159 static scene artwork using direct 8-bit `GGGRRRBB` pixels
- Offline-generated artwork and audio; no generative model runs during play

## Repository layout

```text
docs/                         Design and asset-pipeline documentation
schemas/                      Scene-fact and V9958 palette contracts
tools/scene-extractor/        New West of House extraction tooling
vendor/infocom-zork1/         Original Infocom source as a Git submodule
generated-scenes/             Local generated output; not committed
```

The original source is deliberately isolated from the new implementation.
`vendor/infocom-zork1` points to
[`dumaiss/zork1`](https://github.com/dumaiss/zork1), a fork of
[`historicalsource/zork1`](https://github.com/historicalsource/zork1).
West of House code, schemas, documentation, and generated-asset tooling live
outside that submodule.

## Clone

Clone recursively so the original source module is populated:

```powershell
git clone --recurse-submodules https://github.com/dumaiss/WestOfHouse.git
cd WestOfHouse
```

For an existing non-recursive clone:

```powershell
git submodule update --init --recursive
```

The submodule is pinned to a reviewed source revision. Updating it should be an
intentional parent-repository commit, not an unrecorded checkout change.

## Scene extraction

The extractor mechanically walks the vendored ZIL rooms and emits source-backed
JSON. It does not invent visual composition, audio, hotspots, or story facts.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe tools\scene-extractor\extract_scenes.py `
  --room SANDY-CAVE `
  --validate
```

Run the complete test suite:

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tools\scene-extractor -v
```

See [the extractor guide](docs/SCENE-EXTRACTOR.md) for commands and output
semantics. See [the asset pipeline prompt](docs/scene-asset-pipeline-prompt.md)
for state enrichment, image generation, V9958 palette quantization, audio, and
hotspot contracts.

## Status

The scene-facts extractor and downstream asset contracts are working. The
native Z80 host engine and parser integration remain under development.

## Licensing and provenance

New West of House material is licensed under this repository's
[MIT License](LICENSE). The source in `vendor/infocom-zork1` is a separate
repository with its own history, notices, and license; the parent repository's
license does not replace them.

*Inspired by the work of Infocom. All trespassers will be eaten.*
