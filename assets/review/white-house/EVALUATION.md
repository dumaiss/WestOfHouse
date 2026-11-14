# White House Phase 2 pilot

## Result

Generated eight mapped illustrations and one explicitly **unbound Attic
concept** using the built-in image-generation tool. All high-resolution PNGs
of the selected generations are retained. Superseded candidates were removed
at the user's request. The pilot demonstrates attractive individual scenes and a
consistent painting vocabulary, but **the complete set is not approved for
production**: architectural continuity and the window-state changes need work.

The only runtime image key remains `(room, state)`. No state mappings were
changed and no states were added for props, poses or other scene details.
The user approved the Attic concept without assigning it a runtime state.

## Review sheets

- [Original illustrations](contact-sheet-originals.png)
- [RGB previews, 256×159](contact-sheet-rgb-256x159.png)
- [Decoded YJK previews, 256×159](contact-sheet-yjk-256x159.png)

Labels and sheet backgrounds belong only to these review sheets. Individual
illustrations and previews contain no UI, captions or borders.

## Scene results

| Runtime pair | Scene | Observations |
| --- | --- | --- |
| 180 / 1 | West of House | Clear white house, boarded front door and mailbox. Revised following the user's request to a classic rounded mailbox on a post with a small red side flag. |
| 180 / 2 | West of House | Revealed forest path is visible; the revised mailbox and overall house identity remain consistent. Minor repainting occurs, so the edit is not pixel-identical outside the path. |
| 81 / 1 | North of House | Boarded windows, no door and winding forest path are present. Siding, trim and palette match, but the elevation and roof interpretation are not convincingly the north side of the first building. |
| 80 / 1 | South of House | Boarded windows and no door are clear. Window count and elevation proportions differ from the north view; roof orientation across the exterior set is unresolved. |
| 79 / 1 | Behind House | Small ajar corner window and forest path are readable. The rear looks lower than the two-storey front; it does not establish a convincing single building volume. |
| 79 / 2 | Behind House | The selected correction opens the lower sash fully on the same broad wall as state 1, with the chimney retained at the right roof end. Camera, house and landscape remain visually consistent. The rejected candidate was removed. |
| 203 / 1 | Kitchen | Attractive timber/plaster room, preparation table, ascending stairs and small east window. The downward chimney is interpreted as a conventional fireplace, and the western passage is not clearly presented. |
| 203 / 2 | Kitchen | Room composition is largely preserved, but lighting changes and the window is still only partly open. The open-versus-ajar difference is not reliable at the target size. |
| 201 / unbound | Attic concept | Roof timbers, downward stair, table, knife and rope form a readable attic. The generator adds a lit, unboarded gable window that is not established by the source or approved exterior. Requires architectural review; not a deployable runtime asset. |

## What Phase 1 supplied successfully

The room titles, canonical descriptions, source dressing, exterior boarding,
forest paths and kitchen-window conditions were sufficient to establish each
scene's subject. Shared white siding, dark roof, timber/plaster vocabulary,
muted daylight and painterly treatment produced a recognizable visual family.
The ordinary approach and revealed-path versions of West of House are a useful
successful example of complete-image state variants.

## Ambiguity and consistency

The briefs leave storey arrangement, roof orientation, chimney identity and
window construction largely unspecified. The image reference transferred
materials and style more reliably than building identity across new views.
The mailbox initially became a small wall box; the user's later direction
resolved that choice, and both West of House variants were revised together.

Following the user's continuity correction, Behind House state 2 was edited
again directly from state 1 with a short, localized window-only request. The
selected `room-79-state-2.png` now clearly opens the window without moving
it to the gable wall or relocating the chimney. The failed attempt was removed;
this correction does not resolve the separate cross-view house
proportion issues.

The attic had room facts but no mapped state or state prompt. Its concept
request was derived from the existing room specification, with source-listed
table, knife and rope. Its additional window demonstrates how a sparse brief
can produce an unsupported architectural feature. This is an editorial issue;
it does not call for additional runtime states.

The kitchen's chimney and westward passage also show that preserving source
words in a prompt does not guarantee their correct visual interpretation.

## Scaling and YJK

Exteriors and Attic were returned at 1448×1086 (4:3). Kitchen originals were
returned at 1586×992 and 1585×992, despite the approximately-4:3 request. These
source-aspect exceptions are retained and reported rather than stretched.

Previews crop without distortion and use BOX reduction to 256×159, without
sharpening or forced palette quantization. Exterior crops retain the upper
roof/chimney area and sacrifice foreground grass. Other scenes use a centered
crop. Actual crop boxes are recorded in the pilot manifest and conversion
metadata; they were not introduced as geometric image-generation instructions.

The existing, unchanged KIMG encoder produced optimized YJK files. The decoded
previews remain legible, with reduced color nuance and some horizontal chroma
smearing around small edges. Broad building masses and interior light/shadow
survive well. Roof/siding texture becomes busy, the mailbox flag is marginal,
and small window-opening differences become harder to read. Scaling already
loses much of that detail before YJK encoding.

RGB555 mean squared error per color component ranges from approximately
0.35 to 0.60 for this set. Each KIMG file contains a 40,704-byte pixel payload
plus its 16-byte header. Measurements and hashes are in
[conversion.json](metadata/conversion.json). These are host-decoded previews;
the pilot does not claim an emulator or real-hardware display check.

## Proposed follow-up, not implemented

1. Approve a short house-identity description and a small set of mutually
   consistent reference views before expanding the area. Establish storeys,
   roof/chimney identity and window construction in words without prescribing
   coordinates or rigid compositions.
2. Carry the approved mailbox choice into future briefs as an artistic design
   decision, separately from canonical source facts.
3. Review instructions for the kitchen's traversable downward chimney and
   western passage; avoid the ordinary-fireplace interpretation.
4. Make the already-existing open/ajar window contrast unmistakable, then
   review reference-guided edits at 256×159 before accepting variants.
5. Keep the Attic concept unbound until its existing runtime recognition issue
   is resolved in a separately authorized task. Do not invent a state here.

No Phase 1 generator, runtime mapping, ZIL source or KIMG encoder was modified
by this pilot. Preservation checks are recorded in
[acceptance.json](metadata/acceptance.json).
