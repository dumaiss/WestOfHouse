# West of House

Build with `make -C app` from the repository root. The deployment directory is:

```
app/build/WestOfHouse/
  woh.exe
  WOH.KPR
  SHA256SUMS
```

Put `woh.exe` in `/bin` and `WOH.KPR` in the game's document directory.
From `/games/kona`, run `woh`; it opens `WOH.KPR` in the inherited CWD.
Or run `woh /games/kona/WOH.KPR`. No loose story/images/hotspot files are needed.
The SDK's copies under `build/target/` are build intermediates.

WOH displays the archived full-screen YJK title until any key is pressed.
It then starts the archived `.z3` story and selects complete 256×159 YJK+YAE scenes
and WHT1 hotspot/menu payloads by the exact numeric `(room,state)` pair.
Twenty approved White House scene states are packaged, including all eight
Living Room variants and lit/dark states for Attic and Cellar. Dark scenes have
no visible-object hotspots. The background music resource is preserved.
Unillustrated pairs have a blank scene and no hotspots; keyboard play continues.
The text panel has five rows, eight pages of history, and the existing editor.
Mouse menus contain one to three authored actions; the normal Zork parser
receives the selected full command. Ctrl-Q exits the application.

See [asset organization and maintenance](../assets/README.md) for the manifest,
source/review separation, conversion, hotspot editing and KRES inspection.
Use the current sibling SDK (`make sdk` in ZephyrOS); no OS rebuild is required.
The SDK includes the focused KRES residency and decoder-only TUI changes.
The optimized libzvm configuration is preserved: four dynamic and four static
cache pages, with the existing assembly cache path.

## Checks

```sh
make -C app test
python3 -m unittest discover -s tools/resources -p 'test_*.py'
python3 -m unittest discover -s tools/scene-hotspots -p 'test_*.py'
python3 tools/scene-images/verify_pilot.py
python3 app/tools/run_target.py --mode all
```

The emulator run opens a window and supplies ordinary USB input for the title,
editor, menus, movement and state changes. Acceptance telemetry reads SRAM;
it does not change VM globals. Target captures stay under `app/build/`.
Historical G0 evidence under `acceptance-data/` describes the earlier embedded
fixture build; current results are documented in [ACCEPTANCE.md](ACCEPTANCE.md).
