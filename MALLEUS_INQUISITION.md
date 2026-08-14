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
Where: `scripts/generate_registry.py:100-127`: every relation type gets
`({"Entity"}, {"Entity"})`; `_is_relation_subtype` (line 44) is dead code;
`WorldRelation`/`EdenRelation` reach no generated registry.
Fix: revive the dead path: generate relation types with domain/range from
`Relation` subclasses in the schema, not from the enum.
Done when: an edge with a wrong-typed endpoint is rejected in a test, and
the generator emits at least one non-Entity source/target pair.

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
