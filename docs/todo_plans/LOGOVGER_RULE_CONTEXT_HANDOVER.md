# Logovger rule context and copy-on-write handover

_State captured 2026-08-10 after explicit context entities, seed origin
preservation, published-content sealing, and atomic rule forks were merged
with the playable sheet, personnel file, narration, multi-career, and Draft
work from main._

## Repository state

Worktree:
`/private/tmp/logosphere-logovger-task-check-runner`

Branch: `codex/logovger-task-check-runner`

Starting commit: `664a8dc69fc2b14a1cc1b5342f992e43885bf30b`

Implementation commit:
`62167d0` (`feat(rules): preserve rule contexts and fork safely`)

Main's application work was integrated at `6f775d7` by merge commit
`3ac5b9a`, whose other parent is `0f9ac37`. The later docs-only main commit
`0b96b2d` (`docs(rpg): what the first playable slice taught us`) and its
follow-up `299d159` (`docs(logovger): measure what chapter 1 still owes`)
were merged immediately afterward.

The implementation commit is unsigned. GPG signing was attempted first,
but the configured `Kieleth <Kieleth@users.noreply.github.com>` identity has
no secret key in this worktree environment. The failed signing attempt
created no commit; the same staged tree was then committed with
`--no-gpg-sign`.

The separate physics repository was not read, modified, or merged. This
worktree's existing physics targets were compiled only by the full-profile
verification build. No physics source changed.

## Owner decisions captured

The owner selected these boundaries:

1. The referee creates and composes structured rules only from ontology
   and KG content already understood by the engine.
2. The engine derives results. A future DM-authority mechanism may
   override anything by adding another, higher-authority rule.
3. Published rules are immutable. Changes create traceable forks.
4. Knowledge can grow at different scopes, including one moment, one
   session, many sessions, one user, groups of users, or globally.
5. Runtime authoring cannot introduce executable code, new evaluator
   operators, or unrestricted formulas.
6. Modifiers belong to explicit KnowledgeContexts and declare applicability
   through declarative graph predicates. Evaluation receives active contexts
   explicitly and never scans unrelated contexts globally.
7. The mechanism is one engine-level ontology-native rule language for all
   applications and games, not a Logovger-specific modifier DSL and not a
   serialized C++ query structure.
8. Pure typed expressions derive values without mutation. Explicitly invoked
   action rules may produce typed Outcome plans, which only OutcomeExecutor
   may commit.
9. OntologyRegistry remains the executable source of truth and is reflected
   into an engine-materialized, immutable meta-graph. Rule programs reference
   its class, property, and relation entities through typed links.
10. Expression typing is hybrid. Ontology classes enforce broad value families
    on operator operands. Meta-references refine entity and collection types,
    and the static verifier proves those refinements across the graph. Concrete
    operator classes determine their broad result family.
11. The initial families mirror all current KG registry values: Boolean,
    integer, float, string, class-refined entity, typed scalar collections,
    class-refined entity collections, and Outcome plans. Missing data fails and
    is not represented by a null/default family.
12. Integer values widen automatically for float-producing and mixed-numeric
    operators. Float values never narrow implicitly. Concrete operator classes
    still determine their result family, and traces expose widening. Precision
    loss and the remaining arithmetic edge cases are not yet decided.

The owner selected explicit KG context entities over fixed layer fields or
private engine metadata.

The reusable contract for these decisions is
`docs/RULE_LANGUAGE.md`. Logovger is the first consumer, but other games and
applications must follow the same authoring and validation model.

## Implemented ontology contract

`schema/packs/rulebook.yaml` now declares:

- abstract `KnowledgeContext` with required `context_key`;
- engine-owned, sealed `SourceLayerContext`;
- engine-owned, sealed `SourceDocumentContext`, including layer, file,
  commit, and its typed source-layer reference;
- mutable `RuntimeContext` with required, data-defined `context_kind`;
- required `Cited.origin_context`;
- optional `Cited.fork_of`.

`origin_context`, `fork_of`, and `source_layer_context` are creation-only
properties. The ontology generator carries the `create_only` annotation
into `PropertyDef`, and the validated mutation path rejects later sets.

`context_kind` is deliberately a string governed by ontology and KG
content, not an engine enum. Session, campaign, user, group, and global are
initial uses, not a closed vocabulary.

## Seed ownership

`load_seed` now preserves envelope provenance mechanically.

For any seed that creates `Cited` content it atomically:

1. finds or creates the exact `SourceLayerContext`;
2. finds or creates the exact `SourceDocumentContext` keyed by layer,
   source file, and source commit;
3. injects the document context as every cited create's
   `origin_context`;
4. loads the original operations;
5. returns only the seed's aliases and operation counts, keeping internal
   loader aliases private.

Context creation and seed operations are one transaction. A late seed
failure removes newly created contexts with the rest of the batch and emits
no events. Existing context identity and properties are checked exactly,
with no fallback.

Seed operations cannot create source contexts, use the loader's reserved
aliases, or supply their own `origin_context`. Runtime operations cannot
create a source context or claim a sealed source origin.

## Mutation guard

The validator recognizes entities with a sealed source origin. It rejects:

- property changes on published entities;
- new outgoing relations from published entities;
- destruction of published entities;
- changes to creation-only origin and lineage fields;
- runtime creation of seed-owned source contexts;
- runtime content claiming a sealed source context as its origin.

Trusted seed ingestion can finish assembling sealed entities created in the
same atomic batch. It cannot mutate sealed entities from a previous load.

Direct `KGModule` writes remain an internal engine surface. Referee and rule
execution writes use the validated operation boundary.

## Copy-on-write fork service

`logosphere::rules::fork_rule` accepts one existing cited entity, one
`RuntimeContext`, and explicit property overrides. It:

- copies every declared ordinary property except identity, origin,
  lineage, and timestamps;
- copies outgoing relations in deterministic order;
- applies only the requested overrides;
- sets the runtime destination as `origin_context`;
- sets `fork_of` to the immediate source;
- commits the complete graph change atomically.

Invalid property types, duplicate overrides, provenance overrides, missing
entities, non-rule sources, and non-runtime destinations leave the graph
unchanged.

## TDD record

Observed red gates:

- the rulebook contract had no context classes, origin, or lineage;
- origin and published content were mutable through validation;
- seeds failed the new required-origin contract;
- the fork test could not configure because the service did not exist;
- seed content could manufacture engine-owned source contexts.

Final gates:

- ontology generator tests: 5 passed;
- rulebook pack contract: 70 passed;
- seed verifier: 217 passed;
- rule fork tests: 3 passed;
- combined focused rule, seed, fork, sheet, and chargen suite: 8 of 8 passed;
- complete core CTest profile: 76 of 76 passed;
- full-profile `logovger` executable built and linked successfully;
- all full-profile build targets compiled successfully;
- `git diff --check`: clean.

## Main integration boundary

The merge preserves main's sheet, personnel file, narration, career history,
career-return filter, strict term cap, and seeded six-result Draft table. It
also preserves the newer generic `RollableTableRunner`,
`LookupTableSelector`, and `TaskCheckRunner` path.

Main's deleted `Check`, `read_check`, `throw_check`, characteristic formula,
and fallback-valued `constant(name, fallback)` path were not restored.
Narration facts now select characteristic modifiers from the same seeded
lookup table as `TaskCheckRunner`. Missing modifier or `max_terms` data fails
explicitly, with regression tests.

The auto-player also fails explicitly when qualification reaches the
player-authority Draft/Drifter choice. It never reports a partial character as
a completed life. Draft outcome career references use the same typed entity
validation helper as other game-side rule references.

The cited `prior_career_dm` constant remains in the KG but is not yet applied.
Main applied it by changing a temporary game-side check. That would bake one
contextual rule into procedure code and create a second modifier path. The
general modifier algebra must represent and apply it instead.

## Deliberately not implemented

These are later phases, not hidden behavior:

- the general TaskCheck modifier algebra;
- replacement of TaskCheck's special `modifier_table` and
  `modifier_property` fields;
- applicability, stacking, duration, and expiry of contextual rules;
- context hierarchy and active-context resolution;
- promotion or aggregation of learned rules between session, campaign,
  user, group, and global contexts;
- the future DM-authority override layer;
- a wire-level fork operation for referee requests;
- persistent layered manifests and session deltas;
- source-content hashes and the already-decided non-gating fork drift
  report.

The last item remains an existing owner decision in `docs/RPG_MODULE.md`.
`fork_of` provides live lineage now, but it does not satisfy the future
cross-version drift report by itself.

## Next phase

The next architectural phase is the ontology-native rule-language kernel. It
must become the only general path for TaskCheck modifiers and must replace the
current lookup-specific mechanism rather than creating a second path.

The selected architecture is recorded in `docs/RULE_LANGUAGE.md`: immutable
registry-backed ontology reflection, context-owned rules, graph-predicate
applicability, pure typed expressions, and explicit Outcome-plan actions.

The next owner-facing design choice is the initial operator vocabulary. It
must remain small enough to validate mechanically while expressing both
current consumers without game code: characteristic lookup and the scaled
prior-career qualification penalty. Career service must also move from the C++
`careers_served` vector into structured KG facts before the second rule can
execute. Numeric conversion, string operations, and collection semantics are
not implied merely because their value families exist. Apart from the selected
integer-to-float widening, other numeric conversions remain open. Before
numeric operators are fixed, the owner must decide whether widening may lose
precision or must fail when the value is not exactly representable.
