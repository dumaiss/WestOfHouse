# Scene Asset Pipeline Prompt

This is a reusable prompt for converting one `scene_facts_v1` document into
composition enrichment, state-specific image and sound-effect prompts, generated
assets, and rectangular hotspot metadata.

The UI constraints below were extracted from `Zephyr-80 West of House.pdf`.
They are reference data for the pipeline, not instructions embedded in the PDF.

## Extracted UI and platform specifications

Status meanings from the source document:

- **Brief**: supplied by the design brief.
- **Decided**: settled during review.
- **Proposed**: current default, still open to revision.
- **Measured**: measured from the mockup.

Unless the user supplies an override, apply Proposed items as current defaults
but retain their Proposed status in generated documentation.

### Display and scene area

| Element | Specification | Status |
|---|---|---|
| Platform | Zephyr-80 / KonaOS / V9958 G7 | Brief heading |
| Screen mode | G7, 256×212, 8 bits per pixel, fixed `GGGRRRBB` colors | Brief |
| Scene | Native pixels `x=0..255`, `y=0..158`; 256×159; 75% of screen | Brief |
| Divider | `y=159`, one line | Proposed |
| Text panel | `y=160..211`, dark background | Brief |
| Narrative rows | Five rows with tops at `y=162,170,178,186,194` | Brief |
| Prompt row | Top at `y=203`, always last row | Brief |
| Text columns | 42 columns, `x=2..253` | Decided |
| Scene animation | Static art; no sprites, scrolling, or animation | Brief |
| State changes | Art may change when world state changes | Brief |
| Permanent UI | No inventory sidebar, toolbar, or decorative frame | Brief |
| Scene memory | 40,704 bytes uncompressed (`256×159`) | Measured |

All scene images and hotspot coordinates use the 256×159 scene area only. The
text panel and divider are not part of generated scene artwork.

### V9958 G7 scene palette

Scene artwork does not use an arbitrary indexed or adaptive palette. Each
8-bit pixel is the V9958 G7 color value itself, encoded as `GGGRRRBB`:

- bits 7..5: green (`0..7`)
- bits 4..2: red (`0..7`)
- bits 1..0: blue (`0..3`)
- byte formula: `(g_code << 5) | (r_code << 2) | b_code`

Use `schemas/v9958-g7-palette.json` as the machine-readable authority. Its canonical
8-bit preview expansion permits only these channel values:

- red and green: `0, 36, 73, 109, 146, 182, 219, 255`
- blue: `0, 85, 170, 255`

The palette contains all 256 combinations. It has no alpha channel. The G7
byte is authoritative; RGB triplets are deterministic previews for modern
tools. The `Mockup RGB` values below are visual references extracted from the
UI mockup and are not replacements for the G7 palette entries.

An image model may work in true color, but the offline deliverable must be
resized to 256×159 and quantized to this exact palette before hotspot
annotation. Quantization must not produce an adaptive palette or colors
outside the listed channel levels. Record whether dithering was `none`,
`ordered`, or `floyd_steinberg`, and use the same choice for all states of a
scene unless an explicit project override says otherwise.

### Font and colors

- Font: 6×8 cells. Lowercase `g j p q y` use the bottom row as a descender.
- CP850 arrows: `0x1E` and `0x1F`.

| Use | G7 byte | Mockup RGB | Status |
|---|---:|---:|---|
| Panel background and highlighted-menu text | `0x01` | `#000049` | Proposed |
| Divider | `0x4D` | `#6D4949` | Proposed |
| Narrative text, menu text, and frame | `0xFE` | `#FFFF92` | Proposed |
| Prompt, echoed command, `[...]`, arrows, menu highlight | `0xBC` | `#FFB600` | `[...]` Decided; otherwise Proposed |
| `(PgUp/PgDn)` hint | `0xB5` | `#B6B649` | Proposed |
| Menu background | `0x25` | `#242449` | Proposed |
| Menu shadow and cursor outline | `0x00` | `#000000` | Proposed |
| Cursor fill | `0xFF` | `#FFFFFF` | Proposed |

### Long text

- Stop a response longer than the panel at its first page. No `[MORE]` line and
  no scroll bar. **Decided**
- Put amber `[...]` one space after the last word of a continued page. Do not
  show it on the final page. **Decided**
- Wrap at 42 columns; on a continued page wrap its final row at 36 columns so
  the marker cannot cover text. **Proposed**
- No page overlap. **Proposed**
- Echo the command in amber on the first narrative row. **Proposed**
- PgUp and PgDn change page. **Decided**
- Show `(PgUp/PgDn)` at prompt-row column 27 for multi-page responses.
  **Decided**
- Put up-arrow at column 39 and down-arrow at column 41; blank an unavailable
  direction. **Decided**
- Proposed arrow hit areas: up `x=232..243`, down `x=244..255`, both
  `y=202..211`.
- Typing during paging jumps to the final page and enters the prompt.
  **Proposed**
- With the hint visible, allow 24 input cells including cursor; hide the hint
  past that point to allow 36, then hide arrows to allow 40. **Proposed**
- After a long response, retain the paging hint until the next command. If that
  response fits, remove the hint and retain only the up-arrow for history.
  **Proposed**
- Store word-wrapped history in a RAM ring buffer and redraw the panel from it.
  **Proposed**

### Context menu

- Trigger only from an obvious object. Hidden objects and secret interactions
  never receive a menu. **Brief**
- Menu items are general uppercase verbs such as `EXAMINE`, `TAKE`, `OPEN`,
  `PUSH`, and `PULL`. **Brief**
- Selecting an item submits the matching parser command and places the result
  in the text panel. **Brief**
- Proposed size: 50 pixels wide; five pixels total padding; nine pixels per
  item; 32 pixels high for three items. Seven-letter labels fit.
- Proposed style: one-pixel frame, two-pixel offset shadow, five-pixel tail
  toward the object, full-width amber cursor highlight.
- Place beside the object on the side obscuring the least important artwork.
  **Proposed**
- Clicking elsewhere or pressing Esc closes without submitting. Typing closes
  the menu and enters the prompt. **Proposed**
- Restore covered art from a clean scene copy on VRAM page 1. **Proposed**

### Cursor and hotspots

- Proposed cursor: 12×16 arrow made from two overlaid 16×16 hardware sprites;
  never modify the scene bitmap.
- Hotspots are never drawn. There is no hover highlight or floating indicator.
  **Brief**
- Hotspots must therefore be emitted only as metadata, never painted into the
  generated image.

### Open design items

- Whether a mailbox flag stays raised.
- Whether the down-arrow and `[...]` are noticeable enough on the LCD.
- Real-machine timing for panel redraw and menu restoration.
- Whether scene images will be compressed.

### Audio constraints supplied by the user

- Environmental effects use 8-bit PCM.
- Each clip is at most one second long.
- Effects must be simple enough for playback by the Z80 target.
- Prefer small environmental cues supported by the facts, such as a short bird
  chirp or a short sample of flowing water.
- Sample rate, channel count, signedness, and container format are not specified
  yet. Do not invent fixed values for them. Leave them `null` until supplied by
  the audio driver specification.

---

## Reusable pipeline prompt

Copy the prompt below and replace its input placeholders.

```text
Create the complete graphical and environmental-audio asset package for the
supplied scene-facts document.

INPUTS

SCENE FACTS (authoritative game facts):
{{SCENE_FACTS_JSON}}

OPTIONAL PROJECT OVERRIDES:
{{PROJECT_OVERRIDES_OR_NULL}}

AUTHORITY AND PROVENANCE

1. The scene-facts JSON is authoritative for scene identity, descriptions,
   objects, exits, legal actions, state variables, states, transitions, emitted
   story text, and visibility flags.
2. The UI and platform specifications included with this prompt are
   authoritative according to their stated Brief, Decided, Proposed, or
   Measured status. Apply Proposed items as defaults unless an override is
   supplied.
3. Creative enrichment may make the facts drawable or audible, but must never
   introduce new game objects, exits, actions, state transitions, secrets, or
   story events.
4. Preserve provenance. Keep source-backed facts separate from creative
   composition and environmental-audio additions.
5. Do not expose hidden objects or secret interactions in images, sound cues,
   prompts, menus, or hotspots.

GLOBAL ASSET RULES

- Process every stable source-defined state. Keep each source state ID exactly.
- Treat terminal transitions as separate display assets only when the supplied
  facts identify a displayable state. Otherwise record them as non-rendered.
- Use one shared base viewpoint, geometry, spatial arrangement, and visual style
  across a scene's states. Change only what the facts or state delta require.
- Prefer editing a shared base image when generating state variants.
- Final scene artwork is exactly 256×159 native pixels and contains no text
  panel, divider, cursor, context menu, hotspot outline, annotation, or other UI.
- Art is static: no animation, scrolling, or runtime sprites in scene artwork.
- The final deliverable must use the exact `V9958-G7-GGGRRRBB` palette in
  `schemas/v9958-g7-palette.json`; "G7-compatible" or merely reducible true-color art
  is not sufficient. Quantize offline before hotspot annotation.
- Every final red and green channel value must be one of
  `0,36,73,109,146,182,219,255`; every blue value must be one of
  `0,85,170,255`; no alpha channel is allowed.
- Invented scenery remains non-interactive.

PHASE 1 — TEXT ENRICHMENT FOR EVERY STATE

For every stable state, create one composition_enrichment_v1 entry.

Copy or normalize the applicable source facts into `source_facts` and
`state_facts`. Put all invented visual detail only in `creative_composition`.

The enrichment must establish:

- Architecture and materials where the source leaves them unspecified.
- Lighting needed to make source-visible targets readable.
- A fixed viewpoint and scene geometry shared across states.
- Spatial placement for every source-visible target.
- Explicit visual bindings between source IDs and depicted objects or exits.
- Which targets are visible, hidden by game state, absent, or not renderable.
- The exact visual delta from the base state.
- Non-interactive background details, clearly identified as creative.

Also propose small environmental sound effects in `environmental_sfx`.

Environmental-sound rules:

- Each sound must be justified by a source fact or by a conservative creative
  interpretation of the established environment.
- A sound must not reveal a hidden object, secret exit, puzzle solution, or
  off-screen event.
- Do not treat music, speech, narration, or UI feedback as environmental sound.
- Prefer one short, recognizable event over a dense soundscape.
- Every clip must be 8-bit PCM and no longer than 1000 ms.
- Use `one_shot` for occasional cues and `loop` only when a seamless short loop
  is justified, such as continuously flowing water.
- Set `sample_rate_hz`, `channels`, `signedness`, and `container` to null unless
  project overrides define them.
- If no environmental effect is justified, return an empty array. Never add a
  sound merely to fill the field.

Exit-direction bindings are authored presentation decisions. If a direction is
assigned to a doorway, record it explicitly in `visual_bindings`. Do not infer
that mapping later from pixels alone.

Validate Phase 1 against composition-enrichment-v1.schema.json.

PHASE 2 — IMAGE AND SOUND-EFFECT PROMPTS FOR EVERY STATE

For every Phase 1 state entry, create one asset_prompt_bundle_v1 entry.

The image prompt must:

- Include every applicable source and state fact.
- Include the approved creative composition.
- Require exact continuity with the base state.
- Identify every required-visible and required-hidden source target.
- Preserve every visual binding.
- Keep distinct interactive targets visually separable.
- Keep required targets unobscured enough for later rectangular annotation.
- State that invented background details are non-interactive.
- Request static artwork without UI, cursor, menus, labels, hotspot drawings,
  or annotations.
- Target a final 256×159 image using exactly the 256 colors in
  `schemas/v9958-g7-palette.json`, with pixels encoded as `GGGRRRBB` and no alpha.
- State that any true-color model output is only an intermediate and must be
  quantized offline. Gradients and shading must resolve to valid G7 colors,
  using only the recorded dithering method when dithering is enabled.

For every Phase 1 environmental effect, create a separate sound-effect prompt.
Each sound prompt must:

- Preserve the effect ID from Phase 1.
- Describe only one isolated, recognizable sound.
- Specify 8-bit PCM and a duration no greater than 1000 ms.
- Avoid speech, music, UI tones, modern cinematic design, long reverberation,
  layered ambience, or content not justified by the state.
- Describe a clean start and end. For a loop, request a seamless boundary.
- Preserve unspecified technical fields as null rather than guessing them.
- Be suitable for offline generation and later conversion to the target format;
  no generative model runs during gameplay.

Validate Phase 2 against asset-prompt-bundle-v1.schema.json.

PHASE 3 — GENERATE OFFLINE ASSETS

For each Phase 2 state bundle:

1. Generate one scene image.
2. Convert or resize the intermediate image to exactly 256×159 pixels.
3. Quantize it to `schemas/v9958-g7-palette.json`. Map red and green to their nearest
   3-bit levels and blue to its nearest 2-bit level, applying only the recorded
   dithering method. Do not use an adaptive palette or alpha.
4. Validate that every pixel is one of the 256 palette entries, then encode or
   export it in the runtime asset format without changing pixel values.
5. Preserve the established visual bindings and state continuity.
6. Record the exact image asset ID, dimensions, palette ID, and dithering
   method.
7. Generate each environmental sound effect offline from its prompt when audio
   generation is available.
8. Limit every generated sound to 8-bit PCM and at most 1000 ms after target
   conversion.
9. Record actual audio duration and any finalized format values. If audio cannot
   be generated, retain the prompt and mark its asset status `prompt_only`.

PHASE 4 — RECTANGULAR HOTSPOTS FOR EVERY IMAGE

Inspect the exact final 256×159 image for a state and create one
hotspot_metadata_v2 document.

Hotspot rules:

- The coordinate origin is the top-left of the 256×159 scene image.
- Coordinates are integer native pixels.
- Rectangles must remain inside x=0..255 and y=0..158.
- Use only source target IDs supplied by the scene facts.
- Use only legal actions associated with that source target.
- Use general uppercase context-menu verbs. Labels should be at most seven
  characters where possible because seven-letter labels fit the specified menu.
- The submitted command must match the parser action and object, such as
  `OPEN MAILBOX`.
- Do not create hotspots for invented scenery such as pipes, signs, boxes,
  furniture, plants, decorative machinery, or background architecture.
- Do not turn creative composition details into game objects.
- Do not omit a required target silently.
- If a required target is absent or cannot be identified confidently, emit it
  with `status: not_visible` and a reason.
- Hidden source objects and secret interactions must not receive a visible
  hotspot or menu.
- Do not guess which doorway is west, south, or another direction. Bind a
  directional exit only when Phase 1 established it in `visual_bindings` and
  Phase 3 preserved it.
- Use `rects` for a logical target depicted in multiple disconnected regions.
- Cover the visible target closely without unnecessarily including background.
- `primary_action`, when present, must also appear in `menu_actions`.
- Never draw hotspots or hover indicators into the image.

Validate Phase 4 against hotspot-metadata-v2.schema.json.

FINAL PACKAGE

Return:

1. One composition-enrichment document containing every state.
2. One asset-prompt-bundle document containing every state.
3. One 256×159 image for every rendered state.
4. Generated environmental audio assets, or prompt-only records when audio
   generation is unavailable.
5. One hotspot document for every generated scene image.
6. A manifest associating scene ID, state ID, image asset, sound assets,
   hotspot document, visible targets, and not-visible targets.

Do not skip a stable state. Do not silently omit a required target or sound
record. Do not present creative enrichment as a ZIL source fact.
```

---

## `composition-enrichment-v1.schema.json`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://westofhouse.local/schemas/composition-enrichment-v1.schema.json",
  "title": "Scene Composition and Environmental Audio Enrichment",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema", "scene_id", "base_state_id", "continuity", "states"],
  "properties": {
    "schema": { "const": "composition_enrichment_v1" },
    "scene_id": { "type": "string" },
    "base_state_id": { "type": "string" },
    "continuity": {
      "type": "object",
      "additionalProperties": false,
      "required": ["viewpoint", "geometry", "style", "shared_bindings"],
      "properties": {
        "viewpoint": { "type": "string" },
        "geometry": { "type": "string" },
        "style": { "type": "string" },
        "shared_bindings": {
          "type": "object",
          "additionalProperties": { "type": "string" }
        }
      }
    },
    "states": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/$defs/state" }
    }
  },
  "$defs": {
    "fact": {
      "type": "object",
      "additionalProperties": false,
      "required": ["subject", "predicate", "value", "provenance"],
      "properties": {
        "subject": { "type": "string" },
        "predicate": { "type": "string" },
        "value": { "type": ["string", "number", "integer", "boolean", "null"] },
        "provenance": {
          "enum": ["scene_facts", "state_facts", "creative_enrichment"]
        },
        "source_ref": { "type": ["string", "null"] }
      }
    },
    "target": {
      "type": "object",
      "additionalProperties": false,
      "required": ["id", "status", "reason"],
      "properties": {
        "id": { "type": "string" },
        "status": {
          "enum": ["visible", "hidden_by_state", "absent_by_state", "not_renderable"]
        },
        "reason": { "type": "string" }
      }
    },
    "sfx": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "id", "description", "category", "trigger", "source_basis",
        "creative_addition", "playback_mode", "max_duration_ms",
        "bit_depth", "sample_rate_hz", "channels", "signedness",
        "container", "priority"
      ],
      "properties": {
        "id": { "type": "string", "pattern": "^SFX-[A-Z0-9-]+$" },
        "description": { "type": "string" },
        "category": {
          "enum": ["environment", "state", "action", "transition"]
        },
        "trigger": {
          "enum": ["on_enter", "ambient_interval", "on_state_entry", "on_action"]
        },
        "source_basis": {
          "type": "array",
          "minItems": 1,
          "items": { "type": "string" }
        },
        "creative_addition": { "type": "boolean" },
        "playback_mode": { "enum": ["one_shot", "loop"] },
        "max_duration_ms": { "type": "integer", "minimum": 1, "maximum": 1000 },
        "bit_depth": { "const": 8 },
        "sample_rate_hz": { "type": ["integer", "null"], "minimum": 1 },
        "channels": { "type": ["integer", "null"], "minimum": 1 },
        "signedness": { "enum": ["signed", "unsigned", null] },
        "container": { "type": ["string", "null"] },
        "priority": { "type": "integer", "minimum": 0 }
      }
    },
    "state": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "state_id", "render", "terminal", "source_facts", "state_facts",
        "creative_composition", "visual_bindings", "target_manifest",
        "state_delta", "environmental_sfx"
      ],
      "properties": {
        "state_id": { "type": "string" },
        "render": { "type": "boolean" },
        "terminal": { "type": "boolean" },
        "source_facts": {
          "type": "array",
          "items": { "$ref": "#/$defs/fact" }
        },
        "state_facts": {
          "type": "array",
          "items": { "$ref": "#/$defs/fact" }
        },
        "creative_composition": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "architecture", "materials", "lighting", "viewpoint",
            "spatial_arrangement", "noninteractive_background"
          ],
          "properties": {
            "architecture": { "type": "array", "items": { "type": "string" } },
            "materials": { "type": "array", "items": { "type": "string" } },
            "lighting": { "type": "array", "items": { "type": "string" } },
            "viewpoint": { "type": "array", "items": { "type": "string" } },
            "spatial_arrangement": { "type": "array", "items": { "type": "string" } },
            "noninteractive_background": { "type": "array", "items": { "type": "string" } }
          }
        },
        "visual_bindings": {
          "type": "object",
          "additionalProperties": { "type": "string" }
        },
        "target_manifest": {
          "type": "array",
          "items": { "$ref": "#/$defs/target" }
        },
        "state_delta": { "type": "array", "items": { "type": "string" } },
        "environmental_sfx": {
          "type": "array",
          "items": { "$ref": "#/$defs/sfx" }
        }
      }
    }
  }
}
```

## `asset-prompt-bundle-v1.schema.json`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://westofhouse.local/schemas/asset-prompt-bundle-v1.schema.json",
  "title": "State Image and Environmental Sound Prompt Bundle",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema", "scene_id", "states"],
  "properties": {
    "schema": { "const": "asset_prompt_bundle_v1" },
    "scene_id": { "type": "string" },
    "states": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/$defs/statePrompt" }
    }
  },
  "$defs": {
    "audioTechnicalConstraints": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "bit_depth", "max_duration_ms", "sample_rate_hz", "channels",
        "signedness", "container"
      ],
      "properties": {
        "bit_depth": { "const": 8 },
        "max_duration_ms": { "type": "integer", "minimum": 1, "maximum": 1000 },
        "sample_rate_hz": { "type": ["integer", "null"], "minimum": 1 },
        "channels": { "type": ["integer", "null"], "minimum": 1 },
        "signedness": { "enum": ["signed", "unsigned", null] },
        "container": { "type": ["string", "null"] }
      }
    },
    "soundPrompt": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "sfx_id", "prompt", "negative_constraints", "playback_mode",
        "technical_constraints", "asset_status", "asset_id"
      ],
      "properties": {
        "sfx_id": { "type": "string" },
        "prompt": { "type": "string" },
        "negative_constraints": {
          "type": "array",
          "items": { "type": "string" }
        },
        "playback_mode": { "enum": ["one_shot", "loop"] },
        "technical_constraints": {
          "$ref": "#/$defs/audioTechnicalConstraints"
        },
        "asset_status": { "enum": ["prompt_only", "generated", "converted"] },
        "asset_id": { "type": ["string", "null"] }
      }
    },
    "statePrompt": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "state_id", "render", "reference_state_id", "image_prompt",
        "image_negative_constraints", "required_visible_targets",
        "required_hidden_targets", "visual_bindings",
        "image_technical_constraints", "image_asset_id",
        "sound_effect_prompts"
      ],
      "properties": {
        "state_id": { "type": "string" },
        "render": { "type": "boolean" },
        "reference_state_id": { "type": ["string", "null"] },
        "image_prompt": { "type": "string" },
        "image_negative_constraints": {
          "type": "array",
          "items": { "type": "string" }
        },
        "required_visible_targets": {
          "type": "array",
          "items": { "type": "string" },
          "uniqueItems": true
        },
        "required_hidden_targets": {
          "type": "array",
          "items": { "type": "string" },
          "uniqueItems": true
        },
        "visual_bindings": {
          "type": "object",
          "additionalProperties": { "type": "string" }
        },
        "image_technical_constraints": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "width", "height", "bits_per_pixel", "pixel_encoding",
            "palette_id", "palette_file", "alpha",
            "quantization_required", "dithering"
          ],
          "properties": {
            "width": { "const": 256 },
            "height": { "const": 159 },
            "bits_per_pixel": { "const": 8 },
            "pixel_encoding": { "const": "GGGRRRBB" },
            "palette_id": { "const": "V9958-G7-GGGRRRBB" },
            "palette_file": { "const": "schemas/v9958-g7-palette.json" },
            "alpha": { "const": false },
            "quantization_required": { "const": true },
            "dithering": {
              "enum": ["none", "ordered", "floyd_steinberg"]
            }
          }
        },
        "image_asset_id": { "type": ["string", "null"] },
        "sound_effect_prompts": {
          "type": "array",
          "items": { "$ref": "#/$defs/soundPrompt" }
        }
      }
    }
  }
}
```

## `hotspot-metadata-v2.schema.json`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://westofhouse.local/schemas/hotspot-metadata-v2.schema.json",
  "title": "Scene Hotspot Metadata",
  "type": "object",
  "additionalProperties": false,
  "required": [
    "schema", "scene_id", "state_id", "image", "coordinate_space", "hotspots"
  ],
  "properties": {
    "schema": { "const": "hotspot_metadata_v2" },
    "scene_id": { "type": "string" },
    "state_id": { "type": "string" },
    "image": { "type": "string" },
    "coordinate_space": {
      "type": "object",
      "additionalProperties": false,
      "required": ["width", "height", "origin"],
      "properties": {
        "width": { "const": 256 },
        "height": { "const": 159 },
        "origin": { "const": "top-left" }
      }
    },
    "hotspots": {
      "type": "array",
      "items": { "$ref": "#/$defs/hotspot" }
    }
  },
  "$defs": {
    "rect": {
      "type": "object",
      "additionalProperties": false,
      "required": ["x", "y", "w", "h"],
      "properties": {
        "x": { "type": "integer", "minimum": 0, "maximum": 255 },
        "y": { "type": "integer", "minimum": 0, "maximum": 158 },
        "w": { "type": "integer", "minimum": 1, "maximum": 256 },
        "h": { "type": "integer", "minimum": 1, "maximum": 159 }
      }
    },
    "menuAction": {
      "type": "object",
      "additionalProperties": false,
      "required": ["id", "label", "command"],
      "properties": {
        "id": { "type": "string" },
        "label": {
          "type": "string",
          "pattern": "^[A-Z0-9-]+$",
          "maxLength": 7
        },
        "command": { "type": "string" }
      }
    },
    "visibleHotspot": {
      "type": "object",
      "additionalProperties": false,
      "required": ["id", "status", "rects", "menu_actions"],
      "properties": {
        "id": { "type": "string" },
        "status": { "const": "visible" },
        "rects": {
          "type": "array",
          "minItems": 1,
          "items": { "$ref": "#/$defs/rect" }
        },
        "menu_actions": {
          "type": "array",
          "minItems": 1,
          "items": { "$ref": "#/$defs/menuAction" }
        },
        "primary_action": { "type": "string" },
        "confidence": { "type": "number", "minimum": 0, "maximum": 1 }
      }
    },
    "notVisibleHotspot": {
      "type": "object",
      "additionalProperties": false,
      "required": ["id", "status", "reason", "menu_actions"],
      "properties": {
        "id": { "type": "string" },
        "status": { "const": "not_visible" },
        "reason": { "type": "string" },
        "menu_actions": {
          "type": "array",
          "items": { "$ref": "#/$defs/menuAction" }
        },
        "confidence": { "type": "number", "minimum": 0, "maximum": 1 }
      }
    },
    "hotspot": {
      "oneOf": [
        { "$ref": "#/$defs/visibleHotspot" },
        { "$ref": "#/$defs/notVisibleHotspot" }
      ]
    }
  }
}
```

## Cross-document invariants

These rules are not fully expressible in the individual JSON Schemas and must
be checked by the pipeline validator:

1. Every enrichment `scene_id` and `state_id` must exist in the input scene
   facts.
2. Every hotspot ID must exist in that state's target manifest.
3. Every menu action must be legal for its target in the scene facts.
4. A hotspot `primary_action` must identify one of its `menu_actions`.
5. For every rectangle, `x + w <= 256` and `y + h <= 159`.
6. A directional exit hotspot requires the same binding in Phase 1 and the
   generated image.
7. A hidden or absent target cannot have a visible hotspot.
8. Creative background elements cannot appear in the target manifest.
9. Every sound prompt ID must exist in the corresponding Phase 1
   `environmental_sfx` array.
10. Every generated sound must be 8-bit PCM and no longer than 1000 ms.
11. Unspecified audio format values remain null until the playback driver fixes
    them.
12. Every rendered state has exactly one image prompt, image asset record, and
    hotspot document.
13. Every final image is exactly 256×159 with no alpha, and every pixel belongs
    to the `V9958-G7-GGGRRRBB` palette in `schemas/v9958-g7-palette.json`.
14. Red and green channel values are limited to
    `0,36,73,109,146,182,219,255`; blue values are limited to
    `0,85,170,255`.
15. Every rendered state records the same palette ID and, unless overridden,
    the same dithering method. Hotspots are measured only after final palette
    quantization.
