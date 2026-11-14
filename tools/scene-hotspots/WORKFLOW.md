# Refresh vision observations after an image changes

Current packaging and app integration: [assets/README.md](../../assets/README.md).
This tool retains the independent Phase 2B review/compiler workflow.

1. Read the selected Phase 2 manifest and the relevant Phase 1 room/state
   specification. Use the selected original and final RGB preview, including
   revisions. Never choose an older candidate merely because its filename is
   shorter. Do not invent a state for the unbound Attic.
2. Inspect the **actual final 256×159 preview** with a vision-capable model.
   An enlarged nearest-neighbor diagnostic is useful. The high-resolution
   source may help identify objects, but rectangle estimates must refer to the
   final preview. Do not transplant high-resolution coordinates by scaling
   without the recorded crop.
3. Read the scene facts and ZIL object synonyms, flags, local globals, exits and
   action handlers. Identify only visible game-relevant targets. A painted
   kitchen knife is not automatically the knife that the source places in the
   attic. Leaves or containers do not expose hidden contents merely because
   the parser knows about them.
4. Update the corresponding entry in `observations.json`. Set `image_sha256`
   to the final preview's SHA-256. Give each accepted target an `h1`, `h2`, ...
   identifier, known `object_id` (or source-backed `EXIT-*`), short label,
   generous `[x,y,width,height]`, confidence, rationale and `background` flag.
   Preserve uncertainty; use `unresolved` instead of coordinates when the
   target is absent or cannot be identified. Repeated windows may share an
   object ID while retaining distinct hotspot IDs.
5. Run generation and validation. If a new object lacks a reviewed command
   policy, examine its parser vocabulary and add a concise source-backed menu
   to `actions_for`; do not offer incomplete two-object actions. Ordinary
   parser refusals remain valid outcomes.
6. Inspect the generated annotated preview. Check small objects, edges and
   nested regions; move/expand approximate rectangles where the visual estimate
   misses the target. Regenerate after editing observations. The observations
   are the source of truth, so manual edits to generated manifests will be
   rejected as drift.
7. Run the tests and parser checker. Keep the JSON, binaries, diagnostic sheet
   and warnings together for human review. Verify that source artwork hashes
   remain unchanged. Do not pack a KRES or install tables into WOH at this stage.

The coding assistant's built-in vision was used for this pilot. The Python
compiler consumes the resulting model observations; it is not an independent
object detector, and it does not silently invoke an external service.
