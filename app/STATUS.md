# G0 status

G0 is implemented. The user clarified that `zork1.z3` should be loaded from
the current working directory, rather than the executable's directory.
No-argument startup uses that convention; an explicit directory remains an
optional override. There is no fixed installation path and no OS change.

Build the two-file bundle with `make -C app`; deploy
`app/build/WestOfHouse/woh.exe` and `zork1.z3`, enter their directory, and launch
`woh.exe` with no arguments.

See [README](README.md) for development/deployment and [ACCEPTANCE](ACCEPTANCE.md)
for host/MAME evidence, resource usage, timing and known limits.

No VM core, ZIL, OS, emulator or installed SDK source changes were made for G0.
Existing work in the repository was preserved. No files have been staged or
committed for G0.

## 256×144 layout revision

The default scene is now `docs/west_of_house_256x144.g7`, embedded in the
final bundle. Seven narrative rows replace five; eight pages retain up to
56 wrapped lines. The mailbox fixture follows the new artwork. The updated
executable builds and passes SDK packing validation. Earlier host/MAME
evidence in `acceptance-data/` describes the previous layout; runtime tests
have not been rerun for this revision.
