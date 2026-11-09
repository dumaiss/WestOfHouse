# Design and existing-extractor audit

## Reuse decision

Import the existing `extract_scenes.py` module through a read-only adapter and
reuse its tokenizer, nested-form parser, definition index, source positions,
property accessors and tree/string utilities. These have existing corpus tests.
The import creates no `SceneExtractor` and calls no scene generation or asset
serialization method. No shared-parser refactor was needed, so the existing
extractor and schema remain byte-for-byte unchanged.

`zil_source.py` is the only parser integration boundary. `conditions.py` owns
observations and the deliberately small matching algebra. `discovery.py` owns
candidate presentation discovery and bounded source-pattern transfer.
`validation.py` is independent of source discovery and YAML serialization.
`compatibility.py` reads optional legacy documents only after discovery.
`extract_room_states.py` performs orchestration and deterministic serialization.

## Findings in the original asset workflow

- `descriptions_and_states()` couples text paths to extensive scene facts and
  ordinal `LOOK-STATE-n` identities. The new tool retains only candidate predicate,
  internal text identity and provenance.
- `expand_text_paths()` and `analyze_execution_paths()` do not exclude earlier
  non-else COND branches from later non-else branches. The new source walk applies
  ordered-CONDITION semantics to every branch.
- `post_condition_from_effects()` can copy an equality's pre-increment value into
  the postcondition, and treats effects separately rather than as ordered writes.
  The new transfer processes assignments in order, forgetting overwritten guards
  and deriving a resulting constant only when justified.
- Action and event execution can generate `ACTION-STATE-n`/`EVENT-STATE-n` simply
  from a mutation. This does not establish a persistent presentation distinction.
  The new tool admits only explicit look variants, indexed presentation text and
  local presentation-attribute mutations accompanied by text. Other mutations
  do not produce states or automatic readiness failures.
- The existing INITIAL condition combines all collected initial variables. The
  new tool does not collect that model. A source-backed single counter's initial
  value can identify the baseline candidate; unrelated flags/globals are omitted.
  No fallback complement is invented for unproven defaults.
- Arbitrary source expressions and dynamic calls require a broader interpreter
  to execute. The new tool retains them as unresolved evidence, not guessed rules.

The existing scalar-property set also makes JSON object-key order vary across
Python hash seeds. Preservation comparisons use `PYTHONHASHSEED=0`; the new
extractor sorts its serialized dependencies explicitly.

These findings are implemented locally in the new tool. They are not changes to
the existing scene extractor's behavior or output.

## Discovery and recognition

Candidate discovery reads room descriptions, direct M-LOOK bodies, object/room
and pseudo-object action handlers, literal indexed text tables, persistent
presentation-flag writes and queued routine identities. Candidate discovery is
unchanged by the readiness correction. Unrelated mutation and downstream asset
audits were removed; they establish no recognition defect in an identified state.

Recognition derives predicates after ordered mutations. An equality before an
increment is advanced with 16-bit wrapping. A comparison after an increment
already constrains the resulting value and is not advanced a second time.
Unknown prior values do not become guessed constants. Overwriting a value drops
its old atomic guards; invalidated compound guards remain unresolved.

Transient VERB?/PRSO/PRSI guards are action triggers, not persistent observations.
Other unsupported guards and opaque calls remain limitations. Source paths that
end the game are not new persistent states. Explicit returns stop processing
later expressions. Same-identity, same-presentation source paths are unioned;
identity collisions with different presentations become unresolved.

Only direct top-level M-LOOK dispatch is accepted without outer-context proof.
Nested look contexts, writes during LOOK, unsupported control forms and helper
calls remain unresolved when attached to an actual candidate. Scheduling, local
event context and asynchronous effects are not simulated. Encountering a queue
or an unrelated action does not by itself invalidate the identified dictionary.

## Validation boundaries

The validator partitions each bounded scalar domain using comparison constants,
their neighbours and endpoints. Boolean domains have two cells. Object-reference
domains include named values, null and an explicit diagnostic-only other-object
representative. This is exact for supported unary scalar comparisons, not a
proof about the game's reachable state space.

It checks contradictions, pairwise overlap and coverage without ordering states.
Witnesses are provided for overlap/gaps. The Cartesian validation budget is
32,768 cells; a larger product produces explicit uncertainty. Discovery is
bounded to 256 paths and depth 20; limitations propagated into a candidate
condition remain unresolved. The dictionary does not certify omitted, unproven
additional states. Readiness does not claim exhaustive source analysis.

Observation minimization is conservative: simplify constants, duplicate terms
and equality implications; remove no condition merely because it is inconvenient
or makes coverage incomplete. Dependency collection then emits only references
actually required by known matching rules. Unknown expressions and provenance
remain available outside the YAML.

## Recognition readiness correction

`validation.validate()` checks supported predicates, symbolic observation kinds
and definitions, contradictions, overlap, coverage and cell-budget uncertainty.
It returns the sole readiness decision, exclusive state classifications and
blocking reasons used by both serializers, summary counts and strict mode.
Discovery warnings and legacy comparisons are not validation inputs.

An identified state with an opaque condition remains unresolved. A mutation with
no established state relationship generates neither a state nor a readiness
failure. Actions, inventory changes and future rendering requirements are outside
the recognition contract. The prior external-effect, object-hotspot and exit-menu
audits were removed without changing candidate IDs or predicates.

Optional legacy comparison runs only on request, after the recognition result
and dependency list are fixed. Its output is separate and never contributes to
readiness. The default report contains only actual recognition diagnostics and
compact source locations. Diagnostic schema v2 explicitly marks the reporting
change; the runtime dictionary representation remains v1.

## Deliberate limitations

`ready` means the retained recognition rules select deterministically over the
validated domain. It is not a proof that every presentation variation was found,
or that assets exist. Complex ZIL, recursive containment, dynamic properties,
reference-to-reference comparisons and general control-flow compilation remain
outside this implementation. No extra analyzer was introduced to improve the
pass count. See CORPUS.md for actual metrics and the remaining recognition failures.
