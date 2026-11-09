# Symbolic room-state extractor

This build-time Python tool reads original ZIL and generates observations and
recognition rules for the room states currently identified by the extractor.
It answers: **given the current room and observable VM values, which state ID
should be selected?** It does not validate images, resources, hotspots or menus.

## Run

Python 3.10 or later is sufficient. The generator has no external dependencies.
From the WestOfHouse root:

```sh
python3 tools/room-state-extractor/extract_room_states.py \
  --source-root vendor/infocom-zork1 \
  --output generated-room-states/room-states.yaml

python3 tools/room-state-extractor/extract_room_states.py --list-rooms
python3 tools/room-state-extractor/extract_room_states.py --room ATLANTIS-ROOM --strict
```

Repeat `--room` for several rooms; omit it to process all indexed rooms. Unknown
rooms and invalid source directories exit 2. Normal extraction exits 0 and writes
findings even for incomplete rooms. `--strict` exits 1 only if a selected room
fails recognition validation. The outputs remain available for diagnosis.

The **two default outputs** are:

| File | Purpose |
| --- | --- |
| `room-states.yaml` | Symbolic recognition dictionary |
| `room-states.report.json` | Recognition validation, source references, calculated counts and source hashes |

`--report PATH` overrides the JSON path. Output paths must be distinct.
Generation and acceptance require no previous scene-facts output.

## Ready and incomplete

`validation.validate()` is the single readiness authority. Its result supplies
YAML room status, JSON room status, summary totals and strict-mode exit status.
A room is `ready` when:

1. At least one candidate state exists.
2. All retained conditions are supported and their referenced observations have
   valid symbolic definitions, kinds and scalar types.
3. Conditions are mutually exclusive and non-contradictory.
4. Supported conditions cover the validated observation domain.
5. Validation has no unresolved uncertainty, such as an exceeded cell budget.

The domain uses the declared scalar types: both boolean values, the full signed
or unsigned 16-bit interval, and object-reference equivalence classes consisting
of named values, null and other objects. No narrower reachable domain is guessed.
Coverage witnesses show values not covered by the supported recognition rules;
they do not prove those values are reachable during gameplay. This preserves
Sandy Cave's coverage failure without inventing a fallback.

A single unconditional state is valid and needs no observations. Objects, actions,
conditional exits, mutations and possible future graphical variations do not
invalidate that dictionary unless an actual retained candidate has a recognition
problem. An unproven need for another state is different from an unsupported
condition on an identified state. The former does not block readiness; the latter
does. Opaque expressions already attached to candidates are retained unresolved.

`ready` describes internal recognition usability for the identified states and
validated domain. It does **not** certify assets, nor prove discovery of every
presentation variation in the game. `incomplete` means a real recognition issue
prevents deterministic selection; the report lists its blocking reasons.

## Dictionary contract

The [JSON Schema](../../schemas/room-state-dictionary-v1.schema.json) describes
`room_state_dictionary_v1`, whose representation is unchanged by the readiness
correction. Each room contains `id`, `status`, `observe` and `states`. No compiled
numbers, address placeholders, actions, transitions or resource references appear.

A resolved state has `match`. An unresolved or contradictory state has its
corresponding `status` and no match. Overlapping states have `status: ambiguous`
and retain their predicates for inspection. The consumer must require exactly
one valid match; there is no file-order priority, hidden default or source
expression evaluation. This tool supplies no runtime evaluator.

Observations are deduplicated and collected only from emitted predicates.
Unknown-only candidates introduce no speculative reads. Constants, duplicate
terms and equality implications are simplified conservatively; no globally
minimal dependency-set claim is made.

| Kind | Symbolic fields | Meaning |
| --- | --- | --- |
| `global` | `symbol` | One VM global |
| `object_attribute` | `object`, `attribute` | Boolean flag |
| `object_parent` | `object` | Immediate parent, **not recursive containment** |
| `object_property` | `object`, `property` | Source-established numeric property |

Scalar types are `boolean`, `signed16`, `unsigned16` and `object_ref`. Object
references remain symbolic; null means no parent/zero reference. Global types
come from source initializers, assignments, arithmetic, comparisons and explicit
`GDECL ... FIX` declarations. Table pointers are not inferred to be counters.
Property selectors such as `P?SIZE` retain their source spelling while being
checked against the object's `SIZE` property. Missing definitions and unsupported
kinds make affected states unresolved before predicate evaluation.

The matching algebra supports `true`; `{ref: ID, eq: VALUE}` / `ne`; numeric
`lt`, `le`, `gt`, `ge`; and `all`, `any`, `not`. Source `G?`/`L?` use signed
ordering. Unsigned ordering is not guessed from a signed primitive. Dynamic
selectors, recursive containment, comparison between two observations and unknown
locals remain unsupported. In room-local source analysis, a location operand
`HERE` denotes the analyzed room; this does not implement VM current-room binding.

Strings use quoted JSON scalar spelling, valid YAML with no boolean-name
ambiguity. Ordering, state identities and output bytes are deterministic.

## Diagnostics and counting semantics

The diagnostic report is now explicitly versioned **`room_state_diagnostics_v2`**.
It replaces the former `complete`, `resolution`, `discovery_issues` and legacy
acceptance fields with room `status`, central `validation`, and exclusive state
`status` classifications. Runtime YAML remains v1 because its shape and matching
language have not changed; its readiness meaning is corrected as defined above.

The four state counters are exclusive and sum to the number of generated states:
contradictory takes precedence, then unresolved, ambiguous, and finally resolved.
For example, an impossible predicate that also contains unknown source is counted
once as contradictory; its unknown expressions are still recorded. Resolved
states can belong to a room with a coverage gap or validation uncertainty.

`rooms_ready + rooms_not_ready == rooms_processed`. Room-level overlap, coverage
and uncertainty counters are issue occurrences by room and can overlap; they
must not be added together to obtain `rooms_not_ready`. `blocking_reasons`,
observation errors, overlap/coverage witnesses and relevant unknown expressions
explain each failure. Source locations are compact file/line references.

There are no asset-readiness metrics or unrelated mutation/hotspot warnings.
The former `rooms_without_complete_mapping` and `compatibility_issues` summary
fields are removed. See [CORPUS](CORPUS.md) for before/after counts and blockers.

## Optional legacy comparison

Legacy state relationships can be inspected explicitly:

```sh
python3 tools/room-state-extractor/extract_room_states.py --compare-legacy
# Or choose a legacy source directory and comparison output:
python3 tools/room-state-extractor/extract_room_states.py \
  --scene-facts generated-scenes \
  --compatibility-output /tmp/legacy-room-comparison.json
```

Any of `--compare-legacy`, `--scene-facts` or `--compatibility-output` enables
comparison. Otherwise no legacy documents are read and no compatibility file is
written. Default comparison paths are `generated-scenes` and
`OUTPUT.compatibility.json`. A missing document is `not_checked`. Changed or
unproven meanings and missing aliases appear **only in this optional output**;
they cannot affect the dictionary, recognition report, totals or strict mode.
Previous compatibility files are not implicitly refreshed by a normal run.

State IDs and source predicates remain unchanged by this correction. Conditional
look/post-mutation IDs use stable content hashes; initial/counter IDs retain their
existing naming. Future downstream build stages may consume these identities
without requiring assets to exist at this milestone.

## Verification

Tests use PyYAML and jsonschema from `requirements-test.txt`; generation uses only
the standard library.

```sh
python3 -m unittest discover -s tools/room-state-extractor -v
python3 -m unittest discover -s tools/scene-extractor -v
```

Fixtures cover Atlantis Room, Cold Passage, West/East of House, Sandy Cave and
Living Room, plus observation kinds, ordered mutations, boolean conditions,
unsupported source, contradictions, overlaps, coverage, strict mode, optional
comparison isolation, valid YAML/schema and full-corpus determinism. See
[DESIGN](DESIGN.md) for source-analysis boundaries.
