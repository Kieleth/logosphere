# Ordo Malleus: Inquisition of Logosphere

Date: 2026-08-13. Inquisitor: Ordo Malleus fleet survey (Opus), rubric v1
(`malleus-dev/src/malleus/inquisition/rubric.yaml`).

Logosphere holds the fleet's most advanced engine-executes-the-graph work
(three of its patterns are canonized in `malleus-dev/docs/RECIPES.md`), and
its enforcement layer quietly eroded. The vocabulary held; the gate did not.

## Heresies

### H1. `setProperty` is completely unvalidated  [rubric: gate_integrity]
Where: `src/kg/kg_core.cpp` (no check); documented against itself at
`src/interaction/particle_interaction_system.cpp:397-400` ("KGCore::setProperty
validates nothing (ontology_registry.h:46 claims otherwise and is wrong)").
Full validation already exists on the KGOp path
(`src/kg/ontology_validator.cpp:117-215`) and is used only by the LLM loop.
Fix: route engine-side property writes through the same validator (or a
cheap subset: declared-property + value-type checks).
Done when: a test writes an undeclared property and a wrong-typed value via
`setProperty` and both are rejected; the wrong comment at
`ontology_registry.h:46` is corrected.

### H2. Relation domain/range is vacuous  [rubric: bound_endpoints]
**HEALED 2026-08-16.** Also partly WRONG as written, and the wrong part
is worth keeping: by the time it was read again the generator did NOT
give every relation `({"Entity"}, {"Entity"})`. It harvested endpoints
from `valid_source_types` / `valid_target_types` annotations on enum
permissible values, and thirteen relations were already narrow. The
finding aged badly because it named a symptom in a file that then
changed underneath it.

The real defect was the one it pointed at from the start: the contract
lived on the ENUM instead of on `Relation` subclasses, and
`_is_relation_subtype` stayed dead. Three consequences, all measured
before the fix:

1. `WorldRelation` was a concrete class leaving `relation_type` open
   across an eleven-member enum, so malleus refused to construct the
   root schema. Every schema in the repo inherits that failure, so
   **no schema here had ever been judged past construction** and eight
   rites reported nothing. Their silence was not a pass.
2. The enum path needed an opt-in annotation (`relation_type_enum:
   true`) to see a pack's enum. Eden never set it, so Eden's five
   relation types (`BEARS`, `TEMPTS`, `FOLLOWS`, `FORBIDS`, `DESIRES`)
   reached no registry at all while game code wrote them.
3. It validated only that an endpoint NAME was a class. `LetExpression`
   is a mixin marker and not an Entity subtype, so
   `LET_EXPRESSION_HAS_BINDING` had an endpoint no instance can belong
   to, and nothing said so.

Fixed by declaring one concrete relation class per predicate, each
pinning `relation_type` with `equals_string` and declaring its own
endpoints: 11 in the engine root, 13 in `rule_language`, 5 in Eden. The
generator now reads `Relation` subclasses (the dead
`_is_relation_subtype` revived) and the enum-harvesting path is
deleted rather than kept as a fallback. Enums remain the vocabulary.

Evidence: the 208 previously generated `addRelationType` triples are
byte-identical before and after, so the source of truth moved without
the behaviour moving; the only additions are Eden's five. Eight schemas
now hold a PURITY SEAL; before this, zero did.

Residual, deliberately: `HAS_PART`, `SUPPORTS`, `SPECIALIZES`,
`BONDED_TO` and others keep Entity endpoints and the rite still marks
them SUSPICION. That is the measured truth rather than neglect, and
each class says why in its description. `HAS_PART` spans 81 C++ sites
and 1518 seed ops across twelve distinct type pairs; `SPECIALIZES` is
Skill to Skill in every occurrence, and `Skill` is a rulebook-pack
class the root cannot name without inverting the dependency.

Still open from the original "done when": no test yet rejects an edge
with a wrong-typed endpoint. The generator emits non-Entity pairs, so
half the criterion is met.

### H3. Committed generated code with no reproducible generation  [rubric: dependency_pin]
Where: `scripts/generate_ontology.py:45-52` lists only `logosphere.yaml`,
but `earth`/`space`/game registries are compiled (`CMakeLists.txt:120-121,
1096-1097`) and committed; `make clean` deletes what cannot be regenerated.
Fix: enumerate all schemas in the generation script; regenerate in CI and
fail on dirty diff.
Done when: `make clean && make` succeeds from a fresh checkout.

### H4. The materials property table lives in 9 switch ladders  [rubric: single_source]
Where: `src/materials.h:63-362` (135 case lines); `material_strength = 50e6f`
hardcoded 34 times vs `GetTensileStrength(FLESH)` = 1.0e6f, a 50x
disagreement; the schema declares the identical 15-value `MaterialType` with
zero of the physical properties.
Fix: one MaterialType -> 8-column property lookup (in the KG or generated
from the schema); derive stiffness (`k = E*A/L`) instead of declaring it.
Done when: the 34 magic-number sites read the lookup; the 9 switches are
deleted.

### H5. `ArbiterDecision.event_type` is an open string  [rubric: constrained_tongues]
**FOUND 2026-08-16, NOT FIXED.** Surfaced for the first time by the H2
fix: `schema/packs/rulebook.yaml` could not construct before it, so
this rite had never run against that pack.

Where: `ArbiterDecision` in `schema/packs/rulebook.yaml`. Its
`event_type` is neither constrained to an enum nor pinned with
`equals_string`, so any string validates.

It is the same defect as H2 one primitive over. A concrete Event that
does not pin its type has an unknowable predicate at write time, which
is why the endpoint contract on a relation and the type contract on an
event are the same rite in different clothes.

It matters more here than the general case. `ArbiterDecision` is the
record of who decided what during ingestion, written by exactly one
writer (`ChargenSession::record_decision`). A record whose own type
nothing constrains is a poor foundation for a claim about provenance.

Fix: pin it with `equals_string`, or constrain it to an event enum, and
prove the gate rejects an unpinned value.
Done when: a test writes an `ArbiterDecision` with an arbitrary
`event_type` and the write is rejected.

Left open deliberately. Fixing it here would widen a slice whose claim
was relations, and the doctrine says an open gate found mid-slice is
recorded and surfaced rather than closed silently or deferred silently.

## Suspicions

### S1. Signal has zero adopters  [rubric: root / derived_signals]
No class in 8 schemas extends Signal; capability factors (the natural
Signals) are computed in C++ and never land in the graph. Either adopt
Signal for capability health (bearer = body part, algorithm = aggregation
mode) or record why the engine deliberately keeps derived qualities out of
the KG.

### S2. The joint parameter matrix is branched in four files with
disagreeing numbers  [rubric: single_source]
`joint_types.cpp` vs `humanoid_generator.cpp` (second, different limits) vs
`animation_primitives.h` vs name-gated behavior in locomotion, including a
capital-H `"Head"` inconsistency. Schema-key the 19x7 matrix.

### S3. `materialize_seeds` regressed against `EntityManager`
16 sequential `findByType` blocks + species if-chains
(`logogenesis_app.h:697-1355`), beside the solved registry-dispatch pattern
(`src/entity_manager.cpp:31-56`). Route seeds through activator dispatch.

### S4. Malleus is invisible in the project's own docs
Zero mentions outside `schema/` and the version gate. One paragraph in
`docs/ONTOLOGY_LAYERS.md` naming the root layer prevents the next
contributor from re-deriving the stack.

## Commendations (keep; canonized in malleus RECIPES)

- The ontology slice as the LLM's spec sheet, refusals fed back ("the void
  resisted"), with range-asserting live-model evals.
- `EntityPhysicalState` and the lever discipline (`is_at_rest` deliberately
  has no lever): the fleet's only closed write-loop into a running
  simulation.
- Facet-tagged type selection making prompt leaks unrepresentable (the
  150-phantom-walls lesson).
- TransformationRule: live engine behavior as a 7-line entity, trigger
  parsed through generated code so schema and loader cannot drift.
- The ontology-packs negative test: a generator without its pack fails
  loudly at `createEntity`.

## Consult

`~/Projects/malleus-dev`: `docs/ADOPTION_GUIDE.md`, `docs/RECIPES.md`
(recipes 5 and 6 cite this repo), `.claude/skills/malleus-inquisitor/`.
