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

Slice 3 implementation commit:
`fb12fc8` (`feat(rules): validate typed signatures and bindings`)

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
    still determine their result family, and traces expose widening.
13. Widening succeeds only when converting to the engine float representation
    and back preserves the exact integer. Inexact widening fails during pure
    evaluation before randomness or effects.
14. Integer overflow policy is encoded by the operator type. Checked integer
    operators return Integer and fail on overflow. Promoting operators declare
    a Numeric result and promote an overflowing result only when it is exactly
    representable as float. No operator wraps, clamps, or narrows implicitly.
15. Division accepts Numeric operands and always returns Float. Integer inputs
    must widen exactly first. Zero denominators fail. Integer quotient,
    remainder, and rounding behavior require separate operators.
16. Floats use finite IEEE 754 binary64, round-to-nearest ties-to-even. NaN and
    infinities fail as inputs or results, finite subnormals are legal, negative
    zero is canonicalized, and caller rounding modes do not leak in. The
    current generic float validator does not enforce this yet; implementation
    requires mechanical guards and regression tests.
17. The initial language is higher-order. Typed Function values may be passed,
    returned, and applied. The first core includes typed reads and traversal,
    lookup, comparison, Boolean and numeric operators, reusable functions,
    map, filter, and fold. Functions remain pure graph programs and cannot
    embed code, callbacks, mutation, or new evaluator semantics.
18. Function application uses a bounded concrete class per broad result
    family. The node class fixes that family, while the static verifier checks
    the function signature, parameter bindings, and entity, collection, or
    returned-function refinements. There is no generic executable application
    node and no ontology class generated per signature.
19. Function calls use immutable named signature parameters. Applications own
    unordered bindings that point to the signature's parameter specifications.
    Every parameter must be bound exactly once; missing, duplicate, foreign,
    extra, and incompatible bindings fail static validation. Functions may be
    selected dynamically under one exact shared signature. The initial
    language has no positional meaning, defaults, optional parameters,
    variadics, or new call-boundary coercion.
20. Closures are explicit partial applications. `BindFunctionExpression`
    visibly binds named source parameters, evaluates each bound expression
    once, and returns an immutable function value whose remaining signature is
    exactly the source signature minus those parameters. There is no lexical
    free-variable capture or by-reference environment. Bound values are
    traceable; an entity capture preserves identity rather than copying state.
    Zero bindings fail. Fully bound zero-parameter functions remain open.
21. Zero-parameter signatures are invalid. Partial application must bind at
    least one parameter and leave at least one parameter. Supplying every
    source parameter uses normal result-family-specific application and
    returns the result immediately. Reusable zero-input logic is a typed
    expression root, not a delayed function.
22. Local bindings use eager multi-binding lexical blocks. Bindings have
    unique diagnostic keys, direct typed references, and a statically verified
    dependency DAG. Every binding evaluates exactly once before the body in
    deterministic topological order, including unused bindings. Nested blocks
    may read enclosing bindings; references cannot enter child or sibling
    scopes or escape the owning block. Functions never capture them implicitly.
23. Currying exists only as future authoring sugar. The stored KG and evaluator
    accept complete application or explicit `BindFunctionExpression`, nothing
    between. Partial-call syntax must resolve the exact signature and compile
    into that same typed bind graph before the normal validation path. It
    cannot leave incomplete calls or opaque text for runtime interpretation.
24. Recursion is opt-in through `RecursiveFunctionDefinition` with a required
    positive `max_recursion_depth`. Depth counts active calls per underlying
    source-function identity, including the initial call, and wrappers cannot
    reset it. Visible cycles are checked statically; dynamic higher-order calls
    are guarded before body evaluation. The evaluator uses explicit frames.
    Depth alone does not control total branching work, so invocation budgets
    remain required.
25. Evaluation uses one shared structured budget with required counters for
    expression evaluations, function calls, collection visits, and produced
    values. Engine configuration supplies complete defaults and absolute
    ceilings; app configuration may change defaults and lower ceilings; caller
    overrides must remain within the app ceiling. Invalid overrides fail, not
    clamp. Rule roots may only request lower maxima. The evaluator receives one
    complete resolved budget and has no fallback path. Concrete limits require
    representative and adversarial benchmark fixtures.
26. The initial evaluator has no result cache. Reaching a shared expression
    node twice evaluates, charges, and traces it twice. Explicit local binding
    blocks are the only evaluate-once reuse mechanism. No derived result is
    cached across invocations, sessions, users, or KG learning layers. Future
    caching requires profiling and must preserve cold-evaluation budget and
    failure semantics.
27. Portable references use canonical paths for meta classes, declaring-class
    properties, relations, and addressable content. Path segments use strict
    UTF-8 percent encoding. Content identity is the unique tuple of typed
    KnowledgeContext, exact concrete type, and immutable entity key; display
    names are not identity. File-local aliases remain, while `@@Type:Name` is
    deleted after migration and tested dead.
28. The meta-graph reflects all runtime-semantic fields retained by the
    registry: class inheritance, abstractness and facets; property type,
    declaring class, mutability and bounds; relation domains and ranges; and
    diagnostic provenance. Facets and value kinds are canonical typed meta
    entities, not opaque author strings. Absence is explicit. Materialization
    is sorted, complete, atomic, engine-owned, and sealed. Source-schema
    descriptions and presentation metadata remain outside it.
29. Audit finding: generated registries use `enum` and `datetime`, while the
    generic validator passes those and every unknown kind through without
    validation. The earlier registry-complete value-family claim was false.
    Strict remediation and an unknown-kind regression test are required before
    rule-language slice 1.
30. Enum and datetime are distinct first-class scalar and collection families,
    never String refinements. The registry and generator must preserve exact
    enum identity, members, and provenance, use a closed value-kind
    discriminant, and carry typed enum and temporal values through the
    validated KG path. Unknown kinds fail and the permissive legacy path is
    deleted. This family decision did not itself define exact enum
    compatibility or temporal semantics.
31. Enum compatibility is nominal and closed. A value carries exact enum and
    declared member identity. Shared spelling and identical member sets create
    no compatibility. Cross-enum use is a static error. Enum definitions and
    members are canonical meta entities, have no implicit order or scalar
    conversion, and change only through versioned ontology composition. Open
    vocabularies use typed KG entities instead.
32. The temporal language starts with four first-class sibling families:
    instant, calendar date, local datetime, and zoned datetime. Each has
    matching scalar, collection, property-read, binding, signature, and
    application types. There are no implicit cross-family or String
    conversions. The registry uses four closed discriminants, and the generic
    datetime marker is deleted after explicit schema migration. Calendar,
    zone, precision, representation, leap-second, and operator semantics remain
    open.
33. Calendars are nominal, immutable, addressable KG definition graphs under
    an ontology-declared contract, not engine-coded calendar classes. Every
    calendar-bearing value and type refinement names one exact published
    definition. Structural equality creates no compatibility; conversion is
    explicit. Definitions and their reachable rule graphs are sealed, and
    changes create a new version or fork. The engine supplies generic
    validation and evaluation, not name-dispatched calendar logic. This
    decision did not yet define the declarative rule bootstrap.
34. Each calendar combines a recurring phase-zero typed function kernel with
    finite typed exception entities. The kernel reuses the rule language under
    a stricter verifier and cannot depend on temporal nodes, world queries,
    randomness, Outcomes, mutation, external calls, or invocation context.
    Kernel and exceptions are budgeted, validated, and sealed together.
    Signatures, the phase-zero allowlist and recursion, exception precedence,
    and inverse mapping remain open.
35. Scope correction: executable enum values and items 32 through 34 are
    accepted roadmap architecture, not prerequisites for the active
    rule-language phase. The active phase keeps enum and legacy datetime values
    KG-only and rejects them as unsupported rule families. It implements
    unknown-kind rejection, nominal enum registry identity and member
    validation, immutable ontology reflection, canonical references, and the
    rule families required by current Logovger consumers. Typed enum storage,
    temporal and calendar validation, migration, zones, and operators are in
    `docs/todo_plans/RULE_LANGUAGE_ROADMAP.md`.
36. Slice 0 is implemented. The registry now uses a closed
    `PropertyValueKind`, preserves nominal enum definitions, exact members,
    source provenance, and property refinements, and composes identical enum
    definitions idempotently while rejecting conflicts atomically. The schema
    generator rejects unknown ranges instead of silently emitting String.
    Validated create and set operations accept only members of the property's
    exact enum. KG scalar persistence remains unchanged, and executable enum
    values remain deferred.
37. Slice 1 is implemented on `codex/logovger-task-check-runner`. The generic
    `rule_language` pack now owns contexts, portable `Addressable` identity,
    and the immutable ontology meta vocabulary. Seeds derive identity from
    their source-document context and required create alias. Rule forks
    require an explicit new key in the destination context. Cross-seed paths
    use strict canonical `@@entity/<context>/<exact-type>/<entity-key>`
    references. The old name and `source_aliases` resolver was deleted, and
    all 935 production references in `cepheus_careers.json` and
    `cepheus_book1_career_tables.json` were migrated. The career-table
    extractor emits canonical paths, so regeneration cannot restore the old
    grammar. `materialize_ontology_meta_graph` now publishes complete sorted
    class, direct-property, relation, facet, value-kind, enum, and enum-member
    reflection under `@@meta/...`. The graph is engine-owned and immutable;
    registry extension invalidates it until an atomic rebuild succeeds. The
    full registered headless profile passes 78 of 78 tests.
38. Slice 2 is implemented at the approved abstract-family boundary. The
    ontology declares Boolean, Numeric, Integer, Float, String, Entity,
    scalar-collection, EntityCollection, Function, and OutcomePlan expression
    families. They are all abstract; no literal, read, or operator shape was
    invented. The read-only static service classifies concrete application
    subclasses, validates required root families, and infers property-read
    types from canonical property meta entities. Typed references preserve
    exact class refinements. Enum and legacy datetime properties fail as
    unsupported executable families and never become String. Validated float
    writes reject NaN, infinities, and overflow, parse under binary64
    round-to-nearest ties-to-even regardless of caller mode, restore that mode,
    and canonicalize signed zero before storage and events.
39. Normal non-call operators use direct required typed operand slots. Generic
    operand-binding entities are not used for arithmetic, comparison,
    property access, traversal, lookup, or Boolean composition. Binding
    entities remain reserved for named function calls, explicit partial
    application, and eager lexical blocks, where the binding has independent
    language meaning. Future genuinely variadic non-call operators require an
    explicit typed shape and cannot infer order from an unordered relation
    set.
40. Stored function types use one reusable `ValueTypeDescriptor` hierarchy.
    Concrete descriptor subclasses fix the active broad family. Entity and
    EntityCollection descriptors require canonical ontology-class
    refinements, and Function descriptors require an exact signature. Both
    parameter specifications and signature results reference this same type
    grammar. Open family strings and duplicated parameter/result type
    hierarchies are excluded.
41. Value-type descriptors are addressable content owned by an explicit
    `KnowledgeContext`, with no global interning. Compatibility is structural:
    concrete descriptor class, plus exact ontology-class refinement for
    Entity and EntityCollection, or exact signature identity for Function.
    Descriptor entity identity does not distinguish equivalent non-function
    types. Cyclic higher-order comparisons must fail boundedly. Sealed source
    contexts already make their descriptors immutable. Runtime-context
    descriptors remain drafts until an atomic publication mechanism seals
    them with a signature or executable program; static validity alone does
    not publish them. The runtime publication mechanism remains undecided.
42. Runtime publication is deferred to the context-owned discovery phase.
    Slice 3 may build and statically inspect runtime signatures and programs as
    drafts, but they cannot execute. Sealed source-context programs are the
    only executable path until publication validates and seals a complete
    runtime program atomically with activation. Static validity alone never
    publishes a draft.
43. Mutable drafts use layered validity. Every write enforces required direct
    properties, exact entity-reference ranges, closed value kinds, immutable
    metadata, and no unsupported-family fallback. Explicit static validation
    enforces cross-entity completeness, unique keys, type compatibility,
    complete calls, lexical scope, and acyclic dependencies before execution
    or publication. Drafts may be temporarily incomplete across entities, but
    validation fails loudly and supplies no defaults.
44. Slice 3 is implemented through static validation. The ontology contains
    the closed addressable value-type descriptor hierarchy, named non-empty
    signatures and parameter specs, unordered argument bindings,
    result-family-specific parameter and local reads, and eager typed let
    blocks. Static validation rejects duplicate and cyclic signatures,
    incomplete, duplicate, foreign, or incompatible calls, multiple local
    ownership, duplicate keys, invalid scope, and dependency cycles. Valid let
    blocks expose deterministic eager topological order including unused
    bindings. Nested blocks may read enclosing bindings. Validation is
    read-only. The mutation guard now follows addressable `identity_context` as
    well as cited `origin_context`, mechanically sealing source-owned type
    graphs and rejecting runtime attempts to claim source identity. Runtime
    drafts remain non-executable. Generator tests pass 9 of 9 and the complete
    registered headless profile passes 81 of 81 tests.
45. Main commit `8c7bc78` was integrated after slice 3. Its per-career table
    ownership and optional TaskCheck modifier behavior are preserved. Every
    cross-seed career, service-skill table, procedure lookup table, and dice
    link now uses canonical addressable identity. The career-table extractor
    reads canonical Career and RollableTable identities from their owning
    seed, refuses missing identities, and rejects obsolete qualified-reference
    syntax before writing. A repository-level regression test scans every
    Logovger seed for the same obsolete-reference class. Chargen's missing-data
    test now identifies the exact canonical table key.

## Slice 1 integration rules for parallel work

Parallel Logovger work must preserve these new contracts:

- Every seed-created `Addressable` entity needs a non-empty `as` alias. The
  loader owns `identity_context` and `entity_key`; seed JSON must not set them.
- A cross-seed link must use the owning seed's source-document context, exact
  concrete type, and create alias. Display `name` and `source_aliases` do not
  resolve and must not be restored as fallbacks.
- `RuleForkRequest` now requires `entity_key`. A fork receives both
  `identity_context` and that key in its destination `RuntimeContext`; it must
  not copy either identity field from the source.
- Ontology composition must finish before meta materialization. Any later
  `extendOntology` call makes `@@meta/...` resolution fail until
  `materialize_ontology_meta_graph` rebuilds the full graph.
- Meta entities and `OntologyMetaContext` are internal. Seed ingestion and
  ordinary runtime mutation cannot create or alter them.

The implementation red gates were the absent qualified-reference API, the
old transaction resolver accepting `@@Parent:Admin`, missing portable seed
identity, the old fork request shape, the absent meta-graph API, and a
generator that ignored pack-owned relation vocabularies. Final verification:
9 of 9 Python generator tests and 78 of 78 registered headless CTest targets.

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
- the registry generator erased enum identity and members to the generic
  `enum` marker;
- unknown LinkML ranges silently became String;
- the registry had no nominal enum API, and the validator still depended on
  the deleted open `value_type` string path.

Final gates:

- ontology generator tests: 7 passed;
- ontology extension contract: 53 passed;
- ontology validator contract: 31 passed;
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

Registry value kinds, nominal enum metadata and validation, canonical
references, the immutable ontology meta-graph, abstract expression-family
typing, descriptors, signatures, named bindings, and eager lexical blocks are
complete through slice 3.
Non-call operator storage is decided: normal operators use direct typed slots,
while language-level binding entities remain limited to calls, partial
application, and lexical blocks. The exact initial operator vocabulary and any
genuinely variadic shape remain owner-facing boundaries. Temporal and calendar
work is explicitly deferred. The enum
compatibility evidence and owner decision are recorded in
`docs/todo_plans/ENUM_COMPATIBILITY_SPIKE.md`; deferred work is in
`docs/todo_plans/RULE_LANGUAGE_ROADMAP.md`.

The operator core must express both current consumers without game code:
characteristic lookup and the scaled prior-career qualification penalty.
Career service must also move from the C++ `careers_served` vector into
structured KG facts before the second rule can execute.
