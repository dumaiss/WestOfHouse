# M3 acceptance snapshot

This report records the current KonaOS `ZVMTEST` build and the automated Zork I
scenario. Build measurements include the test application and its M1/M2 fixture
data; they do not describe `libzvm` in isolation.

## Executable and memory placement

The SDK automatic placer ran during the build. The complete program fits in
the primary executable bank, so the placer emitted no far-code segments and no
cross-bank veneers. The generated EXE v2 report records:

| Resource | Result |
| --- | ---: |
| Packed executable | 42,455 bytes |
| Linked image | 42,326 bytes |
| BSS | 8,961 bytes |
| Process stack | 256 bytes |
| EXE data segments | 0 |
| Far-code segments | 0 |
| Generated call veneers | 0 |

`ZVMTEST` opens the original story through a borrowed `kos_reader_t`. It keeps
the immutable story source there and puts only the 11,282-byte writable V3
dynamic region in XMEM. The allocator reports 16,384 bytes allocated (two
8 KiB slots), with no mappable flag; this is below the 16 KiB mappable ceiling
and the 32 KiB object limit. The story adapter uses a 512-byte cache and does
not retain a mapped aperture pointer. The runtime report prints the XMEM
request, rounded allocation, flags, object type, current application bank, and
valid application-bank mask after loading the story.

The diagnostic ABI reports which application banks are valid, while XMEM
intentionally hides object placement and does not report remaining free slots.
The runtime mask therefore describes available banks, not exact free capacity
or the story object's physical bank. No OS or SDK source is changed for this
report.

The automatic-placement callsite declarations cover the modules that invoke
the memory, text-output, and status callbacks (`memory.rel`, `text.rel`, and
`services.rel`). The linked map places SDCC's `__mulint` helper from `kos_mul`
in the primary image; VM multiplication therefore needs no custom bank-call
mechanism. Writable global callback initializers were not added.

## Gameplay and current verification

The automated sequence is `look`, `open mailbox`, `take leaflet`, `read
leaflet`, `inventory`, `score`, and `look`. The user-provided host capture
shows the sequence reaching its completion marker: inventory lists the
leaflet, score reports 24 points in 5 moves, and the final look returns the
West of House description. The target runner's timeout is 300 emulated
seconds because this full story scenario is slow under MAME.

`make test` passes the host VM fixtures, SDCC compilation, automatic placement,
link, EXE packing, and package validation. The local MAME attempt reached the
first prompt and accepted `look`, but did not return within the attempted
180-second run; the user-provided capture confirms a completed run on their
host. A line-for-line YAZM reference transcript and hardware run remain outside
this snapshot.

The text callback emits synchronous single-byte output in ASCII-compatible
ZSCII form. Newline is LF. Unsupported ZSCII values are replaced with `?`;
callbacks own no temporary VM buffer. The VM does not call console or TUI
services.
