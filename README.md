# West of House

**Illustrated Interactive Fiction**

A classic text adventure, newly illustrated.

You are standing in an open field west of a white house, with a boarded front door.

(There is a small mailbox here.)

---

The Great Underground Empire has been extensively renovated. The puzzles remain fiendish, the prose remains dry, and the grues remain uninterested in your welfare. We have, however, taken the liberty of providing pictures.

*West of House* is the classic text adventure, still driven by its parser and still perfectly willing to let you do something inadvisable with a sharp object. The mouse is present as a convenience, not a requirement. Clicking an object will offer a few sensible actions and type them for you. Typing remains the superior (and more dangerous) option.

The illustrations are static, deliberate, and free of unnecessary animation. They exist to show you the world the text has always described, not to replace it.

Graphics are atmospheric.  
Text is authoritative.  
The parser is still in charge, and it has not gotten any more forgiving.

---

Written for the Zephyr-80 and KonaOS.

The leaflet has been updated. The rest of the empire is under construction.

*Inspired by the work of Infocom. All trespassers will be eaten.*

---

## Origins and Acknowledgments

West of House is an independent, illustrated adaptation of Zork I, originally created by Marc Blank, Dave Lebling, Bruce Daniels, and Tim Anderson, and published by Infocom.

The original Zork I source code was made available under the MIT License by Microsoft, Xbox, and Activision in November 2025. This project builds upon that work with original illustrations, graphical interaction, audio, and integration with KonaOS.

Our thanks to the original Infocom team for creating a world that remains worth exploring more than four decades later.

West of House is an independent project and is not affiliated with or endorsed by Microsoft, Activision, or the original creators.

The original Infocom source is preserved
in the [`dumaiss/zork1`](https://github.com/dumaiss/zork1) submodule, forked
from [`historicalsource/zork1`](https://github.com/historicalsource/zork1).

## Graphical shell (G0)

The native [G0 graphical shell](app/README.md) embeds libzvm and libgfx, with
the fixed test image, live numeric state, narrative paging and mouse menus.
Build its two-file bundle with `make -C app`. Enter the game directory and
launch `woh.exe` with no arguments; it loads `zork1.z3` from the CWD.

## Build-time room-state dictionary

The [ZIL state-reporting pipeline](tools/state-reporting/README.md) compiles the
ready dictionary definitions into the story. Run `make -C libzvm story` for the
optimized ZVMTest, instrumented story and matching bindings in `libzvm/build/`.
ZVMTest displays the numeric `(HERE, WOH-STATE)` pair at input.

The independent [room-state extractor](tools/room-state-extractor/README.md)
reads the original ZIL and emits symbolic observations and recognition predicates
for a future generic graphical host. Its diagnostics report unresolved recognition conditions, overlaps and coverage
gaps. Legacy comparison is optional and never affects room readiness. See the
[corpus results](tools/room-state-extractor/CORPUS.md) for current coverage.
