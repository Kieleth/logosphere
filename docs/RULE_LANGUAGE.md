# Ontology-native rule language

_Architecture and implementation contract. Decided 2026-08-10 and implemented
through TDD slice 3. This is engine guidance for every game and application
that authors executable rules in a Logosphere knowledge graph. Logovger is the
first consumer, not the definition of the mechanism._

## Purpose

Logosphere rules are programs represented by ontology-typed KG entities. The
ontology defines the language grammar and type system. The KG stores programs
written in that language. The engine validates and evaluates those programs.

This is not a text DSL embedded in string properties, a serialization of the
C++ query API, or game logic disguised as data. A rule author composes typed
graph nodes whose meaning the engine already implements. Runtime authors may
create new programs from those nodes. They may not create new executable
operators.

The same language must serve Logovger, other games, and non-game applications.
Game ontologies provide domain vocabulary and rule instances. They do not add
private evaluator paths for rules that the shared language can express.

## Three layers, one source of truth

Logosphere already separates the ontology registry from KG instances:

- The TBox is `OntologyRegistry`: classes, inheritance, properties, value
  types, and relation domains and ranges.
- The ABox is the application KG: characters, careers, rules, contexts, and
  other instances.
- The rule language is ABox data whose static types refer back to the TBox.

The registry remains the sole executable source of ontology truth. After the
engine and application ontologies have been composed and their references
validated, the engine materializes an immutable meta-graph containing one
canonical entity for every available ontology class, property, and relation.
These meta-entities are a read-only reflection of the registry. They are not a
second, author-editable ontology.

Rule nodes refer to these meta-entities through typed entity references. The
meta-graph therefore makes ontology elements queryable and composable without
allowing it to disagree with the registry. Runtime operations cannot create,
change, or counterfeit meta-entities.

Each meta-entity has a stable qualified key. Runtime entity IDs are not
portable rule syntax. Loading resolves every qualified reference to exactly
one canonical entity or fails.

### Canonical qualified references

Portable references use one path grammar:

```text
@@meta/class/<class>
@@meta/property/<declaring-class>/<property>
@@meta/relation/<relation>
@@meta/facet/<facet>
@@meta/value-kind/<value-kind>
@@meta/enum/<enum>
@@meta/enum-member/<enum>/<member>
@@entity/<context-key>/<exact-concrete-type>/<entity-key>
```

The registry already composes classes and relations into global,
case-sensitive symbol spaces and properties into a declaring-class plus name
space. Facet names, property value kinds, and enum names are also global
registry vocabulary; member names are scoped to their owning enum. The meta
paths preserve that identity model. Ontology source URIs and pack revisions
remain provenance, not part of symbol identity. Identical
definitions contributed by more than one source resolve to the same canonical
meta-entity; incompatible collisions already fail registry composition.

A property key always names the class that directly declares the property.
Using a subclass through which the property is inherited is not an alias. The
static verifier uses the canonical declaring property and normal inheritance
rules to decide whether it applies to a subtype.

Each variable path segment is the canonical percent encoding of its UTF-8 byte
sequence. ASCII letters, digits, `.`, `_`, `~`, and `-` remain literal. Every
other byte is `%HH` with uppercase hexadecimal. Empty segments, NUL, control
characters, `.` and `..` segments, invalid UTF-8, lowercase hex,
and percent-encoded forms of an otherwise literal byte are rejected. Parsing
then re-encoding must reproduce the input byte for byte. Keys are
case-sensitive and no display-layer Unicode normalization changes identity.
A slash inside a decoded value is represented as `%2F`; raw slashes separate
segments before decoding.

Content entities become portable only by implementing an `Addressable` mixin
with required creation-only `identity_context` and `entity_key` properties.
The context is a typed `KnowledgeContext` reference whose `context_key` is
already globally unique. `entity_key` is immutable machine identity, separate
from a mutable or localized display `name`. The world enforces uniqueness of
the tuple `(identity_context, exact concrete type, entity_key)`.

The entity resolver decodes the context key, exact type, and entity key, then
requires exactly one matching `Addressable` entity. Zero matches fail as a
broken reference. Multiple matches are a violated world invariant and also
fail. Published source content uses its exact ingestion-edition context; its
evidence uses an exact source representation plus selector. The production
Logovger loader materializes its declared corpus and injects the resulting
ingestion edition as rule identity. During the explicit evidence transition,
each source-authored rule uses exactly one evidence path. Unmigrated rules keep
a source-document origin and structural locator. Migrated rules have edition
origin, no legacy locator field, and are materialized by a reconciled
`IngestionClaim` supported by exact representation-scoped `SourceTarget`
coverage. The verifier rejects either path when fields from the other remain.
For `UTF8_TEXT`, every ledger target also carries a `TextQuoteSelector` whose
exact text must equal the bytes selected by its primary `ByteRangeSelector`.
This supporting selector does not own identity; it prevents Unicode character
positions from being accepted as byte positions. `Injury Crisis`,
`Medical Care`, `Medical Debt`, and `Aging` are the first migrated production
sections. Medical Debt proves that one exact coverage leaf can support several
claims with independent dispositions. Aging proves the inverse: one rule may
need several exact leaves, such as the table headers, result key, and effect.
The verifier derives a row band from one unique evidence fragment rather than
assuming evidence order.

An incomplete claim uses the gap kind that names the missing layer.
`ONTOLOGY_GAP` means the graph lacks a concept. `RULE_LANGUAGE_GAP` means the
concepts exist but the executable language lacks the required composition.
`SOURCE_GAP` means the source itself does not fully state the materialized
reading. A printed finite table boundary may be widened at one side only by a
current `PARTIAL/SOURCE_GAP` materializing decision. `Aging` uses this for its
printed `-6` floor; changing that decision to a rule-language gap makes
verification fail. Runtime content uses its explicit runtime context. Forks
receive identity in their new context rather than inheriting the source
entity's portable key accidentally.

Source occurrences and semantic claims have different identity. Repeated text
keeps one `SourceTarget` and `SourceCoverage` per occurrence, but equivalent
meaning may become one claim supported by all of them. When a later generalized
claim replaces an existing narrower claim, the old claim receives an
append-only `SUPERSEDED` decision naming the replacement. It keeps its earlier
decision history, but relinquishes materialized graph results. `DUPLICATE`
remains for the other direction, where a later claim repeats an earlier
canonical claim. `Aging Crisis` is the production proof: five generalized
crisis claims cite both its leaves and the prior Injury Crisis leaves.

Source revision provenance follows the same structured rule. An exact
`SourceRepresentationContext` is content identity and has no revision field.
An append-only `SourceRevisionObservation` is addressable within that
representation and records one exact source-system revision. The same
representation may therefore be observed at several revisions without
changing source targets or edition identity. A revision observation never
owns source bytes, selectors, or manifest membership.

File-local `@alias` bindings remain unchanged. They address entities created
inside one operation batch. The previous cross-seed `@@Type:Name` form is
deleted after migration, not retained as a fallback. Display names no longer
participate in portable resolution. Serialization emits canonical qualified
paths, while the loaded graph stores typed entity links.

### Complete runtime-semantic reflection

The immutable meta-graph reflects every field currently retained by
`OntologyRegistry` that affects runtime behavior or diagnostics. It does not
attempt to mirror schema descriptions, presentation order, or annotations the
registry does not retain.

Each class meta-entity exposes its qualified key, exact name, source
provenance, abstract flag, whether it has a direct parent, a typed link to that
parent when present, and sorted typed links to canonical facet meta-entities.
Direct parent links are the canonical stored structure; ancestor closure
remains derived by traversal.

Each property meta-entity exposes its qualified key, exact name, source
provenance, typed declaring-class and canonical value-kind links, required,
identifier, and creation-only flags, bound-presence flags and numeric bounds,
and a typed reference-target class link when its kind is `entity_ref`. An enum
property additionally links to its exact enum definition. The declaring class
is direct, never an inherited alias. One value-kind meta-entity exists for
every distinct kind admitted by the registry.

Each facet and value-kind meta-entity exposes its qualified key and exact name.
Each enum meta-entity exposes its qualified key, exact name, source provenance,
and sorted typed links to its complete member set. Each member exposes its
qualified key, exact name, source provenance, and typed owning-enum link.
Each relation meta-entity exposes its qualified key, exact name, source
provenance, and sorted typed sets of allowed source and target classes. Empty
sets remain explicit empty collections. Set iteration order from the registry
never leaks into materialized entity order, serialization, or tests.

Absence is explicit. A root class has `has_direct_parent = false`; an unbounded
property has the corresponding bound-presence flag false; a non-reference
property has `has_reference_target = false`. The associated link or scalar is
present only when its flag is true. Consumers do not infer semantic absence
from a failed required read or substitute a default.

Materialization sorts canonical keys, constructs the entire candidate graph,
checks one-to-one coverage and every typed link against the composed registry,
and publishes it atomically into an engine-owned sealed context. Failure
publishes nothing. Meta entities cannot be created or changed by seed,
application, referee, or ordinary runtime mutation authority.

`OntologyRegistry` remains authoritative. A rule-enabled world cannot evaluate
against a registry extension until the engine has atomically rebuilt and
revalidated the complete meta-graph. The reflected source field is diagnostic
provenance and never changes qualified identity. Persistence and migration
across an ontology revision remain a separate decision below.

### Discovered value-kind gap

The earlier value-family inventory was incomplete. Generated registries
currently contain `enum` and `datetime` properties in addition to `string`,
`boolean`, `integer`, `float`, and `entity_ref`. The generic property validator
strictly parses only the four scalar kinds and deliberately passes every
unknown kind through. Consequently `enum`, `datetime`, and a misspelled future
kind currently receive no value validation.

This violates the fail-loud contract and blocks the claim that the rule
language mirrors every registry value category. Complete reflection must expose
the exact current kind so the gap is visible. The validator's unknown-kind
pass-through must be deleted and covered by a regression test.

### Target: first-class enum family

Enum values are a distinct first-class language family. They do not inherit
from `StringExpression`, accept a string operand slot, or convert to or from a
string at a function boundary, property read, comparison, binding, or
collection operation. A textual persistence or interchange encoding does not
change the runtime type of the decoded value.

An enum expression is refined by one exact ontology enum definition. The
registry and generator must therefore retain that definition's identity,
permissible members, and source provenance instead of reducing every named
enum to the undifferentiated `"enum"` marker. A property carries both the
broad enum kind and its exact enum refinement. Enum literals, property reads,
function parameters, applications, local bindings, and collections preserve
that refinement.

Enum compatibility is nominal and closed. A value consists of an exact enum
definition identity and one exact member identity declared by that definition.
The same member spelling in two enum definitions creates no relationship, and
even two definitions with identical member sets remain incompatible. A
cross-enum operand or binding is a static type error, not unequal data and not
a conversion request.

Enum definitions and members materialize as canonical meta entities under
`@@meta/enum/<enum>` and `@@meta/enum-member/<enum>/<member>`. Membership is
fixed by the composed registry and cannot be extended by an application,
caller, session, or content mutation. A vocabulary change requires a new
versioned ontology composition and complete meta-graph rebuild. Adding a member
does not make another enum compatible; removing or renaming a referenced member
causes program revalidation to fail.

Declaration order has no runtime meaning. Enum values have no implicit ordinal,
integer, or string representation, and the initial language defines equality
only between values refined by the same enum. An explicitly open vocabulary is
modeled with identity-bearing KG entities and typed references, not an
extensible enum or opaque text.

### Current implementation boundary

Executable enum values, temporal expressions, and calendar execution are
roadmap work. They do not block the current rule-language implementation and
are not part of its initial ontology, evaluator, static verifier, or operator
set. The accepted future architecture remains recorded below and in
`docs/todo_plans/RULE_LANGUAGE_ROADMAP.md`.

The active enum boundary is registry metadata and KG write validation. The
registry preserves exact enum definitions, members, provenance, and each
property's nominal enum refinement. A validated write must name a member of
that exact definition. The active rule verifier rejects enum properties as an
unsupported executable family. It never exposes an enum as String.

The existing generic `datetime` marker remains a recognized legacy KG kind so
current registries can compose, but it is not an executable rule-language
family. The current static verifier must reject a temporal literal, property
read, parameter, binding, application, or collection with an actionable
unsupported-family error. It must never reinterpret the value as String.

Registry composition rejects every unknown value-kind spelling immediately.
Replacing legacy KG scalar storage with typed enum values, adding
`EnumExpression`, auditing existing datetime writes, defining strict datetime
validation, migrating schema ranges, and adding temporal expression families
are roadmap items.

### Roadmap: full temporal family

When temporal support enters an implementation phase, the abstract
`TemporalExpression` root has four mutually distinct result families:

- `InstantExpression` for one position on the global timeline;
- `CalendarDateExpression` for a date in a declared calendar without a time;
- `LocalDateTimeExpression` for a calendar date and wall-clock time without a
  zone;
- `ZonedDateTimeExpression` for civil date-time meaning under declared zone
  rules.

The four families are siblings. None is a subtype of another, and no implicit
conversion exists between them. Future projection or resolution operations
must be explicit typed expression nodes. A textual persistence or interchange
encoding is decoded into one exact temporal family or fails; it never becomes
a string value that operators reinterpret.

The registry uses separate closed value-kind discriminants for `instant`,
`calendar_date`, `local_datetime`, and `zoned_datetime`. The generic
`datetime` marker is not an executable fifth family. The current generator
collapses LinkML `date` and `datetime` into that marker, so existing schema
ranges must migrate explicitly after their intended temporal meaning is
selected. The generator cannot guess, default, or retain the legacy marker as
a fallback.

Each future temporal family has its matching literal, property-read, function
parameter, application, local-binding, and collection expression types. Zone
rule authority and versioning, precision, canonical representation,
leap-second behavior, comparison, projection, and arithmetic remain owner
decisions.

### Roadmap: nominal data-defined calendars

Calendars are ontology-shaped KG data, not a closed list of engine classes.
The engine ontology declares the abstract calendar-definition contract and its
typed component vocabulary. An application or game defines a concrete calendar
by publishing an addressable `CalendarDefinition` graph using only that
contract. The engine supplies generic validation and evaluation mechanisms; it
does not dispatch calendar names to hardcoded calendar logic.

Every calendar date, local datetime, and zoned datetime value carries an exact
reference to one published calendar definition. Instant values do not. The
reference uses the normal canonical addressable-content identity, including
the owning KnowledgeContext, exact concrete calendar-definition type, and
immutable entity key. Calendar refinement propagates through literals,
property reads, collections, function signatures, applications, and local
bindings.

Calendar compatibility is nominal. Two definitions remain incompatible even
when their current structures or generated dates are identical. A value from
one calendar cannot bind to, compare with, or enter a collection refined by
another calendar. Cross-calendar conversion requires an explicit typed
conversion expression whose rule proves both source and target definitions.

A calendar definition and every reachable structural or rule entity become
sealed before any temporal value or executable program may reference them.
Changing a published calendar creates a new version or fork with a new
addressable identity. Mutation cannot retroactively change the meaning of an
existing date. Loading a referenced definition requires complete graph
validation; missing components, dangling links, cycles outside the selected
rule model, or unsupported constructs fail before evaluation.

Calendar rules remain declarative KG programs and cannot contain host-language
callbacks, source text, hidden state, or mutation. The exact rule execution
model is the next owner decision because unrestricted temporal operators inside
calendar definitions would create a circular dependency: temporal evaluation
would need the calendar before the calendar itself could be evaluated.

### Roadmap: hybrid phase-zero calendar kernel

Each calendar definition combines one recurring base kernel with a finite set
of exception entries. The kernel handles regular cycles, months or other
segments, eras, and leap rules. Exceptions represent irregular historical or
narrative dates and cutovers that should not be distorted into a recurring
formula.

The base kernel consists of normal typed KG function graphs evaluated under a
calendar phase-zero verifier profile. It reuses the language's named
signatures, bindings, integer, Boolean, enum, finite-collection, and pure
function mechanisms. It cannot read or construct any temporal family, query
world entities or relations, consume randomness, produce Outcome plans, mutate
state, call external code, or inspect invocation context. Calendar evaluation
therefore terminates entirely below the temporal layer it defines.

Phase-zero calls use the same structured invocation meter as every other rule
program and may request only lower limits. The exact kernel input and output
signatures, operator allowlist, and recursion policy remain owner decisions.
An unsupported node anywhere in the reachable kernel graph invalidates the
calendar before publication.

Exception entries are typed immutable entities owned by the same published
calendar definition. They are not source snippets or loosely typed patches.
The verifier loads the base kernel and the complete exception set together,
rejects duplicate or conflicting exception identities, and seals both in one
publication boundary. The exact exception key, precedence, range behavior, and
inverse-mapping rules remain owner decisions. A kernel or exception validation
failure fails the calendar; neither path is a permissive fallback for the
other.

`PropertyDef` can no longer treat an open string as its executable value-kind
discriminant. Registry composition must use a closed engine-owned kind and fail
on every unsupported schema range. The current validated KG path must carry
exact nominal enum refinement and reject undeclared members while retaining its
existing scalar persistence representation. Typed enum and temporal values
replace that legacy storage only in their roadmap phases. The existing
unknown-kind permissive path is deleted, not retained as a fallback.

## The graph is the program

Language syntax consists of ontology classes and typed links, not operator
names stored in strings. The intended abstract families are:

- rules;
- variables and bindings;
- value expressions;
- collection expressions;
- predicates;
- outcome-plan expressions.

Concrete subclasses provide the executable vocabulary. An integer addition
node, for example, takes integer expressions. A property-read node takes an
entity expression plus an immutable ontology-property reference. A relation
traversal takes an entity expression plus an ontology-relation reference and
returns an entity collection with a provable range.

Normal non-call operators store each semantic operand in a direct, required,
typed entity-reference slot. A binary numeric operator, for example, exposes
distinct left and right slots whose ranges are the exact broad expression
families accepted by that operator. It does not own generic operand-binding
entities and does not recover operand meaning from relation order.

Binding entities remain limited to the cases where the binding itself has
language meaning: named function invocation, explicit partial application,
and eager lexical binding blocks. This is not a generic operand mechanism for
arithmetic, comparison, property access, traversal, lookup, or Boolean
composition. Any future genuinely variadic non-call operator needs its own
approved typed shape; this decision does not silently encode order in an
unordered relation set.

Concrete operator names and their exact initial vocabulary remain to be
selected. Their type contract is decided: generic stringly nodes with fields
such as `operator = "multiply"` or `property = "intelligence"` do not satisfy
this architecture.

### Hybrid expression typing

Expression typing uses ontology inheritance for broad value families and
immutable meta-references for domain refinement.

Broad families such as Boolean, integer, entity, entity collection, and
outcome-plan expressions are ontology classes. A concrete operator's operand
slots range over the required family, so the normal validated KG write path
rejects an integer expression in a Boolean slot or an entity expression where
a collection is required.

Entity-valued expressions additionally carry or infer an immutable ontology
class reference. Collection expressions do the same for their element type.
The static rule verifier uses those refinements to prove that a property read
accepts its subject, a relation traversal accepts its source, and the produced
entity type is compatible with the next operator.

An operator's broad result family follows from its concrete ontology class. It
is not an author-supplied `result_type` that can contradict the operator.
Refinements are inferred where possible and explicit only where inference
would be ambiguous. A generic untyped `Expression` operand remains an abstract
root, not an escape hatch for executable programs.

This hybrid keeps common type failures out of the KG while avoiding one
ontology class for every combination such as a collection of characters or a
collection of careers.

### Reusable value-type descriptors

Stored function signatures use reusable typed KG entities, not open family
strings and not separate parallel parameter-type and result-type grammars.
`ValueTypeDescriptor` is abstract. Its concrete subclasses cover the active
broad families: Boolean, Numeric, Integer, Float, String, Entity, each scalar
collection, Entity collection, Function, and Outcome plan. The descriptor's
ontology class fixes its broad family.

An Entity descriptor requires one canonical ontology-class meta reference. An
Entity-collection descriptor requires one canonical element-class meta
reference. A Function descriptor requires one exact `FunctionSignature`
reference. The remaining active descriptors have no refinement field. Enum
and temporal descriptor families remain absent with their expression families.

`FunctionParameterSpec` references one descriptor, and `FunctionSignature`
references one descriptor for its result. The same descriptor grammar can be
used wherever the language must store a type, including higher-order function
signatures. Invalid combinations such as an Integer descriptor carrying an
entity-class refinement are structurally impossible rather than deferred to
a family string plus optional fields.

Descriptors are addressable content owned by one explicit `KnowledgeContext`.
They may be shared by signatures and programs without entering a global type
registry. Equivalent descriptors may exist in different contexts or under
different entity identities; no interning or canonicalization service is
required.

Type compatibility is structural. Unrefined descriptors compare by exact
concrete descriptor class. Entity and Entity-collection descriptors also
compare their exact canonical ontology-class reference. Function descriptors
also compare the exact referenced `FunctionSignature` identity. Descriptor
entity identity by itself never makes two otherwise equivalent non-function
types incompatible. Structural comparison must guard cyclic higher-order
signature graphs and fail with the involved descriptors and signatures rather
than recurse without a bound.

Descriptors in sealed source contexts inherit the existing immutable-origin
guard. A descriptor in a mutable runtime context is a draft. It must become
immutable atomically with the signature or executable program that publishes
it, but the engine does not yet have a runtime publication transition.

The initial signature and binding slice does not invent that transition.
Sealed source-context programs may execute. Runtime-context programs may be
created and statically inspected as drafts but cannot execute, even when they
are structurally valid. Runtime publication moves to the context-owned rule
discovery phase, where publication, activation, and sealing can share one
coherent boundary. Static validity alone never publishes a draft.

Draft validity is layered. Every KG write still enforces the local ontology
shape: required direct properties, exact entity-reference ranges, closed value
kinds, immutable meta references, and absence of unsupported-family fallbacks.
There are no missing-data defaults. Cross-entity program invariants are checked
by explicit static validation and must pass before source execution or future
runtime publication. These include non-empty parameter and local-binding sets,
unique keys, descriptor compatibility, complete invocation bindings, lexical
scope, and acyclic dependencies.

A mutable draft may therefore be temporarily incomplete across entities while
it is assembled. Asking to validate that draft fails loudly with the exact
missing, duplicate, foreign, incompatible, cyclic, or out-of-scope entity. It
never fabricates a parameter, binding, value, or default. Requiring every
individual edit to carry a complete program and allowing arbitrary locally
ill-typed draft data are both excluded.

### Initial value families

The active implementation target includes the value categories required by
current rule consumers, plus language-native function and Outcome-plan
families. Enum and temporal expression families are listed in the roadmap, not
this initial tree:

```text
Expression
  ScalarExpression
    BooleanExpression
    NumericExpression
      IntegerExpression
      FloatExpression
    StringExpression
  EntityExpression                 refined by ontology class
  CollectionExpression
    BooleanCollectionExpression
    IntegerCollectionExpression
    FloatCollectionExpression
    StringCollectionExpression
    EntityCollectionExpression     refined by element ontology class
  FunctionExpression               refined by parameter and result signature
  OutcomePlanExpression
```

The abstract roots are not executable. Concrete literals, reads, operators,
and aggregations inherit from the family they return. Collection operators
preserve their scalar element family. Entity expressions and entity
collections additionally carry or infer their class refinement through the
immutable ontology meta-graph.

Missing data is not a `null` value family and does not become a default. A
required read that finds no value fails. Optional values, unions, and absence
tests require separate approved semantics before they enter the language.

The enum and temporal roadmaps preserve the no-String boundary. Selecting the
active families did not by itself select string operators, floating-point
comparison rules, or collection behavior. Integer-to-float widening is decided
below. The others remain explicit language decisions.

### Initial higher-order operator scope

The first language version includes the first-order typed core and higher-order
composition. Functions are values represented by typed KG entities. They may
be bound to variables, passed as arguments, returned from other functions, and
applied by expression nodes. A function's parameter and result signature is
part of its static type and may refine entity and collection types through the
immutable ontology meta-graph.

The first-order core covers:

- scalar and entity literals;
- invocation and local bindings;
- typed scalar, entity-reference, and meta-property reads;
- entity selection by ontology type;
- forward and reverse relation traversal;
- exact-one collection selection and counting;
- typed equality, numeric ordering, and Boolean composition;
- lookup-table selection;
- the checked, promoting, widening, division, and float contracts specified
  below.

The higher-order core adds function definition, function reference,
application, map, filter, and fold. Reusable named expressions are normal KG
program entities, not copied subgraphs. Map may change a collection's element
type. Fold carries an explicitly typed accumulator. Filter consumes a
predicate function instead of embedding an untyped condition string.

This does not permit textual code, runtime operator definitions, hidden
mutation, or an unrestricted host-language callback. Every function body is a
pure expression graph using ontology-declared operators. An action rule may
apply functions to construct an Outcome plan, but only `OutcomeExecutor`
commits it.

Application typing, parameter representation, explicit capture through partial
application, and local binding blocks are decided below. Function signatures
must be non-empty. Currying is syntax-only and compiles to explicit partial
application. Recursion is explicit and depth-bounded. Total evaluation budgets
use structured deterministic counters with configurable defaults and ceilings.
The initial evaluator has no result cache.

### Function application typing

Function application uses result-family-specific expression classes. There is
no executable generic `ApplyExpression`, and the engine does not generate a
new ontology class for each function signature.

The initial application family contains concrete nodes for Boolean, integer,
numeric, float, string, entity, each supported scalar collection, entity
collection, function, and Outcome-plan results. For example,
`ApplyIntegerExpression` inherits `IntegerExpression`,
`ApplyEntityExpression` inherits `EntityExpression`, and
`ApplyFunctionExpression` inherits `FunctionExpression`. The concrete
application class therefore fixes the broad result family before evaluation,
just as every other concrete operator does.

Each application node references a `FunctionExpression` and supplies argument
expressions using the binding model selected below. The static verifier
resolves the function's signature, checks every binding against its parameter,
and proves that the application's concrete result family matches the signature
result.
Entity classes, entity-collection element classes, and returned function
signatures are refinements propagated from that resolved signature through the
immutable ontology meta-graph. An application node cannot claim a narrower
refinement than the function guarantees.

Applying a function whose result family disagrees with the node is an invalid
program, not a conversion request. It fails static validation before any
evaluation, randomness, Outcome-plan construction, event, or mutation. A
function-valued expression whose signature cannot be proved statically also
cannot be applied.

This adds a bounded set of ontology classes, one per broad result family. It
keeps runtime-created function values composable without mutating the
ontology, while preserving broad slot validation and refinement checking. The
argument-binding representation is defined below.

### Named parameters and invocation bindings

Function calls use named signature parameters, not positional arguments. An
immutable `FunctionSignature` declares a set of typed `FunctionParameterSpec`
entities. Every parameter specification has a non-empty `parameter_key` that
is unique within that signature and references one `ValueTypeDescriptor`.
That descriptor supplies its broad family and any entity, collection, or
function-signature refinement. The signature references a descriptor for its
result through the same type grammar.

Every valid signature declares at least one parameter. A mutable draft may
temporarily lack the cross-entity parameter relation, but explicit static
validation fails and it cannot become an executable function type. Reusable
zero-input logic is represented by its typed expression root, not wrapped in a
delayed function.

A function value references exactly one signature. An application owns a set
of `ArgumentBinding` entities. Each binding references one parameter
specification from that signature and one argument expression. Bindings have
no semantic order. This makes adding or inspecting graph relations unable to
change a call merely by reordering them.

Static validation requires every parameter exactly once. A missing binding,
duplicate binding, parameter from another signature, unexpected binding, or
incompatible argument type invalidates the complete program. There are no
defaults, optional parameters, variadic parameters, or ignored extra bindings
in the initial language. Failure identifies the application, function
signature, parameter key, expected type, and actual type.

Parameter reads in a function body also reference the signature's parameter
specification. Their concrete expression class fixes the broad result family,
and the verifier proves that it matches the specification and propagates its
refinements. A dynamically selected function is applicable only when its
`FunctionExpression` is statically refined to the exact signature used by the
bindings. Different functions can therefore be passed and returned under one
shared signature without callers knowing the concrete function entity.

This decision does not add a new coercion, default, or runtime currying at
function boundaries. Explicit partial application, syntax-only currying, and
local bindings are defined below. A newly authored signature may enter the KG
only through the validated write path and must become immutable no later than
the atomic publication that makes a referencing program executable.

### Explicit partial application

The language has no implicit lexical closure capture. A function body cannot
resolve a free variable from the graph that happens to surround its definition.
Closure-like behavior uses an explicit `BindFunctionExpression`, which
inherits `FunctionExpression` and binds named source parameters visibly in the
program graph.

The bind node references a source `FunctionExpression`, a non-empty set of
normal `ArgumentBinding` entities targeting that source signature, and an
immutable remaining signature. The remaining signature reuses the unbound
`FunctionParameterSpec` entities from the source signature and preserves the
source result descriptor. Static validation proves that its parameter set is
exactly the source set minus the bound parameters. A bound parameter cannot
also remain in the returned signature, and no parameter may be captured more
than once.

When the bind node is evaluated, each bound argument expression is evaluated
once in the current pure invocation. The resulting typed values form an
immutable bound-function value together with the source function. Later
applications supply only the remaining named parameters. An entity capture is
the entity identity, not a deep copy of all its mutable properties. The
captured values and their source expression nodes appear in the evaluation
trace.

Partial application composes: its source may itself be a bound-function value,
provided the verifier can prove the exact source and remaining signatures.
Binding zero parameters is invalid because it adds no meaning. Binding every
source parameter is also invalid because the remaining signature would be
empty. To supply every parameter and obtain the result, an author uses the
appropriate result-family-specific application node instead.

The stored bind node and its argument expressions are KG data. The evaluator
does not mutate the KG when it creates the immutable function value. This
decision does not introduce hidden environment references, by-reference
captures, defaults, a second invocation grammar, or automatic currying.

### Syntax-only currying

The stored graph and evaluator have no automatic currying. Every application
binds its complete signature, and every partial application is an explicit
`BindFunctionExpression`.

A future human-readable projection may accept partial-call notation as author
convenience. Its compiler must resolve the exact source signature, require at
least one supplied and one remaining parameter, and emit the same bind node,
argument bindings, and immutable remaining signature that a graph author would
create directly. A complete call emits the appropriate result-family-specific
application node instead.

The emitted graph passes through the same validated write path and static
verifier. The compiler cannot leave an incomplete application for evaluation,
invent a second function type, or encode the partial call as opaque text. A
graph projected to friendly syntax and parsed again must preserve the exact
typed graph, regardless of whether the printer chooses the shorter notation.

### Local binding blocks

Repeated subexpressions use explicit multi-binding lexical blocks. There is one
concrete `Let...Expression` class per broad result family. Its class fixes the
block result family, while its body supplies that value and any entity,
collection, or function-signature refinement.

A let block owns a non-empty set of `LocalBinding` entities and one body
expression. Every binding has a required non-empty `binding_key`, unique within
that block, and one value expression. A local-reference expression points
directly to the binding entity. Its result-family-specific concrete class must
match the binding value, and the verifier propagates refinements from that
value. Keys exist for inspection and deterministic diagnostics; they are never
resolved as variable-name strings.

Bindings in one block may reference each other. The verifier derives a
dependency edge whenever a binding value reaches a local reference to a peer,
then requires the complete same-block graph to be acyclic. Every binding is
evaluated exactly once before the body in topological order. When several
bindings are ready, ascending bytewise `binding_key` order breaks the tie so
traces and the first reported runtime failure are reproducible. Unused
bindings are still evaluated and may fail; the block is eager, not a hidden
lazy cache.

A reference may target a binding in its own block or a lexically enclosing
block. References into a child or sibling block, and references reachable
outside the owning block's lexical subtree of binding values and body,
invalidate the program. Nested blocks provide additional scopes. Reusing a key
in a nested block does not shadow anything, because identity comes only from
the direct entity reference.

The block and all binding expressions remain pure. Re-evaluating the block in
a later function call recomputes each binding once for that invocation. A
function that needs to retain a local value must do so through the explicit
partial-application mechanism above; returning a function never captures the
surrounding block implicitly.

### Explicitly bounded recursion

Recursion is opt-in through `RecursiveFunctionDefinition`, a distinct subtype
with a required positive integer `max_recursion_depth`. An ordinary function
definition has no implicit depth allowance and cannot be re-entered while an
invocation of the same source definition is active. There is no default depth.

Depth is counted per underlying source-function entity, including the initial
invocation. A maximum of one therefore permits the initial call and rejects
the first recursive re-entry. A `BindFunctionExpression`, returned function,
local binding, or higher-order parameter preserves the source-function
identity and cannot reset its counter by wrapping or rebinding the value.

For statically resolved calls, the verifier builds the known function call
graph. Every function that a visible cycle re-enters must be a recursive
definition with a valid bound. Higher-order dispatch can leave the concrete
source unknown until evaluation, so the evaluator maintains active depth by
source identity for every call. On entry it computes the next depth and fails
before evaluating the body when an ordinary function would be re-entered or a
recursive function would exceed its declared maximum.

The failure reports the application node, source function, declared maximum,
attempted depth, and active invocation chain. Recursive evaluation uses
explicit evaluator frames rather than the native C++ call stack, so an
author-provided bound cannot turn into uncontrolled host-stack recursion.

Per-function depth does not bound total work. Mutual or branching recursion
can perform many calls while each source remains within its own active-depth
limit. A separate invocation budget must cap total expression steps, function
calls, collection visits, and produced values. The effective recursion limit
will also be constrained by that invocation budget; this section does not
invent an unbounded engine override or memoization policy.

### Structured invocation budgets

Every evaluation runs with one complete `EvaluationBudget` and one shared
meter. The required positive limits are:

- `max_expression_evaluations`;
- `max_function_calls`;
- `max_collection_visits`;
- `max_values_produced`.

Expression evaluations count execution occurrences, not distinct graph nodes.
A function call is charged before its frame is created. A collection visit is
charged before inspecting an element or traversed relation target, including a
candidate later rejected by a filter. Produced values count each scalar,
entity, function, Outcome-plan item, and collection container, plus each
element added to a collection. Existing collection values returned from KG
reads still charge their elements; a large stored collection cannot bypass the
meter. This counter does not bound bytes inside one scalar. Validated ingestion
and any future size-growing string or blob operator need separate byte limits
before such content is executable.

The engine ships a complete baseline profile and absolute implementation
ceilings. An application's versioned configuration may replace the defaults
and set lower application ceilings. A caller may provide partial per-invocation
overrides above or below the app default but never above the app ceiling. An
override outside the permitted range fails before validation or evaluation; it
is never silently clamped. Omitting an override means explicit inheritance
from the configured profile, not a missing required evaluator field.

A rule root may declare an optional lower budget profile. It is an upper bound,
not an allocation request: the effective limit for each declared dimension is
the minimum of that profile and the resolved caller limit. It cannot increase
the budget or reset counters in nested functions. The application layer
resolves engine, app, caller, and rule policy into one complete immutable
budget before calling the evaluator. The evaluator API does not accept a null
or partial budget and contains no fallback values.

All nested functions, bound functions, recursive branches, local blocks, and
collection operators share the same meter. Each operation reserves its charge
before doing the work. If the charge would exceed a limit, evaluation fails
without performing that operation. The error reports the exhausted dimension,
limit, amount already used, requested charge, expression node, and invocation
chain. No Outcome plan is returned and no dice, event, or KG mutation occurs.

Concrete default numbers are not guessed in this design. They must be chosen
from representative and adversarial benchmark fixtures and recorded in
reproducible engine and app configuration. Static program-validation size
limits remain a separate ingestion guard; an invocation budget does not make
an arbitrarily large input graph safe to validate.

### No initial evaluation cache

The initial evaluator does not memoize expression results. Reaching the same
expression entity twice evaluates it twice, consumes budget twice, and records
two trace occurrences. Sharing a graph node therefore means shared program
structure, not an implicit evaluate-once instruction.

An explicit local binding block remains the only language mechanism that
evaluates a subexpression once and reuses its value inside one invocation.
Function parameters and captured partial-application values are already values,
not cached expression executions.

There is no per-invocation or cross-invocation result cache, cache persistence,
or invalidation protocol in the initial kernel. This is separate from learning
or retaining KG content across sessions and users. Those knowledge layers do
not imply that derived evaluation results are reusable.

Caching may be reconsidered only after representative profiles show a concrete
bottleneck. Any future cache must preserve uncached budget charges, trace
meaning, deterministic failure order, and results. Cache warmth cannot make a
rule pass that would exhaust its budget when evaluated cold.

### Numeric widening

Integer-to-float widening is implicit when a float-producing or mixed-numeric
operator accepts an integer operand. Float-to-integer narrowing is never
implicit.

The ontology makes this visible in the operator contract. Integer-only
operators accept `IntegerExpression`. Float-producing operators may accept the
abstract `NumericExpression` family and always return `FloatExpression`.
Numeric comparisons may also accept that shared family and widen integer
operands before comparison. This preserves the earlier rule that a concrete
operator determines its broad result family.

The evaluator does not add a conversion node to the stored program. Its
evaluation trace must still record the operand's original integer type and the
float value used by the operator. Widening is therefore automatic but not
invisible during inspection.

This decision does not permit implicit narrowing, string-to-number parsing, or
Boolean-to-number conversion.

Widening is exact or it fails. Before a numeric operator uses the converted
value, the evaluator must prove that converting the integer to the engine's
floating-point representation and back preserves the original integer. A value
that cannot make that round trip fails evaluation with the operand node, the
integer value, and the required float type in the error. The evaluator never
rounds it to a nearby float.

This is a value-dependent runtime check, not a reason to defer all validation.
The program's types and structure are still validated before evaluation. The
exactness check runs during pure evaluation and therefore still occurs before
dice, events, outcome construction, or KG mutation.

### Integer overflow operators

Integer overflow behavior is explicit in the operator type. The language
provides two arithmetic families rather than one operator whose static result
changes unexpectedly.

A checked integer operator accepts integer operands, returns
`IntegerExpression`, and fails when the mathematical result lies outside the
declared integer range. It never wraps or clamps.

A promoting integer operator also accepts integer operands, but its declared
result family is `NumericExpression`. It returns an integer when the result
fits. On overflow, it returns a float only when that exact mathematical integer
is representable by the engine's float type. Otherwise it fails. The evaluator
must detect overflow without first performing overflowing native arithmetic.

The operator choice is a typed node in the KG, so promotion policy is visible
before execution. Evaluation traces record whether the result remained integer
or promoted. A `NumericExpression` result cannot enter an integer-only operand
slot or property through implicit narrowing.

The concrete addition, subtraction, multiplication, and aggregation operator
names remain part of the initial-vocabulary decision. This section fixes the
checked-versus-promoting contract they must follow.

### Division

Division always returns `FloatExpression`. Its concrete operator accepts two
`NumericExpression` operands and therefore has one stable result family whether
the inputs are integers, floats, or one of each.

Integer operands pass through the exact widening check before division. An
integer that the engine float type cannot represent exactly makes evaluation
fail even if a later quotient might be representable. The evaluator does not
introduce a hidden rational intermediate or reorder conversion after division.

A zero denominator fails before performing the operation. This includes an
integer zero and either floating-point signed zero. There is no implicit
integer quotient, remainder, floor, ceiling, or truncation behavior. Those
would require separate, explicitly typed operators.

An always-float result can require representational rounding, such as one
divided by three. It follows the finite binary64 contract below.

### Float semantics

`FloatExpression` uses the finite subset of IEEE 754 binary64. Operations round
to nearest with ties to even. Subnormal finite values are legal. Overflow to
infinity and any NaN result fail evaluation rather than entering the rule
graph's value stream.

Float literals and property reads must also be finite. Positive and negative
infinity and every NaN representation are invalid inputs. Negative zero is
canonicalized to positive zero before storage in an evaluation value, trace,
or result. Division treats either signed source representation as zero.

The evaluator must not inherit a caller-modified floating-point rounding mode.
Its platform guard must prove that the implementation supplies the required
binary64 semantics. There is no implicit epsilon comparison; approximate
comparison would be a separate typed operator with an explicit tolerance.

The validated KG write path now uses the shared finite-binary64 parser. It
rejects non-finite and overflowing inputs, forces ties-to-even independently
of the caller rounding mode, restores that mode, and canonicalizes every signed
zero spelling before storage and event publication. Literal, property-read,
intermediate-result, and serialization coverage remains required as those
concrete expression and evaluator slices are added.

## Pure derivation and explicit effects

The language has two execution layers.

The expression layer is pure. Evaluation reads one immutable KG snapshot and
returns a Boolean, number, entity, collection, or other typed value. Expression
evaluation never changes the KG, consumes randomness, or publishes events.
The same graph and bindings must produce the same value from the same snapshot.

The action layer is explicit. An invoked action rule may use pure expressions
to construct a typed `Outcome` plan. The existing `OutcomeExecutor` remains the
only component that validates and commits that plan. A matched predicate does
not fire hidden mutations merely because it exists in the graph.

This boundary avoids an implicit production-rule loop. Automatic firing,
retraction, conflict sets, and mutation ordering are not part of the selected
language model.

## Context ownership and rule discovery

Rules and modifiers belong to explicit `KnowledgeContext` entities. Published
rules originate in immutable source contexts. Runtime-authored rules originate
in mutable runtime contexts such as a moment, session, campaign, user, group,
or global scope. Changes to published rules create traceable forks.

Evaluation receives the active contexts explicitly. The evaluator does not
scan every context in the KG. This keeps rule discovery bounded and prevents an
unrelated global or user rule from entering an execution accidentally.

A context-owned rule declares applicability through a rule-language predicate,
not through a hardcoded list of checks or unvalidated tags. The predicate may
inspect the bound invocation, the target, and related KG facts using the
language's typed graph operators.

Context hierarchy, precedence, expiry, stacking, and future higher-authority
overrides remain separate semantics. Context ownership alone does not imply an
ordering rule.

## Example: prior-career qualification modifier

Cepheus states that each previous career contributes a negative qualification
modifier. The rule must not be implemented in chargen code or by editing a
temporary `TaskCheck`.

Conceptually, the source context owns one rule program:

```text
when:
  the bound check is a qualification check

derive:
  count CareerService entities related to the bound character
  multiplied by the cited prior-career constant
```

The exact program is a graph:

- the check and character are typed variables supplied by the invocation;
- qualification is tested through ontology-aware graph expressions;
- career history is structured KG data, not a C++ string vector;
- relation and property access point to immutable meta-entities;
- count and multiplication are typed expression nodes;
- the result is one itemized modifier contribution with origin and rule
  identity.

This example establishes two implementation dependencies. Career service must
become structured KG state, and the current lookup-specific
`TaskCheck.modifier_table` plus `modifier_property` mechanism must be replaced,
not retained as a fallback.

## Application and game contract

An application that uses the rule language follows this sequence:

1. Import the engine rule-language schema pack.
2. Define domain classes, properties, and relations in the application
   ontology.
3. Compose and validate the complete registry.
4. Let the engine materialize its immutable ontology meta-graph.
5. Load cited source rules as typed KG programs in source contexts.
6. Create runtime rules or forks only through validated KG operations.
7. Invoke evaluation with explicit typed bindings and explicit active
   contexts.
8. Validate the complete reachable program before consuming randomness or
   constructing an effect.
9. Send any resulting typed outcome plan through `OutcomeExecutor`.

Applications may add new domain types and new rule programs without changing
the engine. A genuinely new executable operator is engine work: define its
ontology type, static validation, evaluator semantics, failure behavior, and
tests before applications may use it.

## Mechanical validity requirements

No invalid program may partially execute. Before evaluation, the validator
must prove at least:

- every referenced entity exists and has the required ontology type;
- every meta-reference resolves to the registry definition it reflects;
- variables are bound once and within their declared types;
- property reads accept the possible subject type and produce the property's
  actual value type;
- relation traversals accept the possible source type and constrain their
  result to the declared range;
- every operator receives compatible operand types;
- every function application satisfies its declared parameter and result
  signature;
- the root produces the result type required by its caller;
- the reachable expression graph is complete and acyclic;
- every selected rule belongs to an explicitly active context;
- published nodes have not been mutated;
- numeric evaluation cannot overflow silently;
- every float input and result is finite and negative zero is canonicalized;
- failure occurs before dice, events, or KG mutation.

Validation must report the failing rule node, expected type, actual type, and
the relevant ontology symbol. Missing required data has no default.

## Authoring and inspection

Because programs and ontology metadata are graph entities, the same KG tools
can expose the language to a referee, editor, debugger, or test. An author can
ask which operators accept an integer collection, which properties exist on a
character subtype, or why a program failed static validation without parsing
C++ declarations.

Human-readable syntax may be added later as a projection of the graph. It is
not the persistence format or source of truth. Its optional partial-call sugar
must follow the compilation contract above, and round-tripping must preserve
the exact typed graph.

## Decisions still required for the active plan

Do not implement past these boundaries without owner decisions:

1. Exact initial non-call operator vocabulary and any genuinely variadic
   operand shape.
2. Static handling of inheritance, unions, and entity collections.
3. Predicate composition, including whether negation exists under an
   open-world KG.
4. Rule result identity, modifier stacking, precedence, suppression, and
   future higher-authority overrides.
5. Context activation, hierarchy, duration, and expiry.
6. Program sharing and ownership across reusable expression graphs.
7. Persistence and migration when an ontology revision changes a referenced
   meta-entity.

Temporal and calendar decisions are explicitly deferred. Their accepted
architecture, unresolved decisions, dependencies, and migration work are in
`docs/todo_plans/RULE_LANGUAGE_ROADMAP.md`.

## TDD implementation order

Each slice starts with a failing contract test and removes any mechanism it
replaces.

Slice 0 is implemented. `PropertyDef` now uses the closed
`PropertyValueKind` discriminant. The registry retains nominal enum
definitions, members, source provenance, and each property's exact enum
refinement. Generated registries reject unknown LinkML ranges instead of
turning them into String. Validated create and set operations reject values
outside the property's exact enum. KG persistence still stores enum members as
strings, and executable enum expressions remain roadmap work.

Slice 1 is implemented. The reusable `rule_language` pack owns
`KnowledgeContext`, `Addressable`, and the ontology-reflection vocabulary.
Seed ingestion derives portable identity from the exact source-document
context and required create alias; forks require a new caller-supplied key in
their destination context. Canonical references use strict percent encoding
and resolve only exact context, concrete type, and machine-key tuples. All
production Logovger cross-seed references were migrated, and the previous
name and source-alias resolver was deleted and tested dead.

The later ingestion-edition decision supersedes source-document context as the
target identity scope without changing the canonical reference grammar. The
generic source-corpus materializer now derives representation and edition
identity from an explicit declaration plus replaceable exact-byte access. The
loader migration remains incomplete, so document-scoped loading above is a
historical implementation state rather than the final contract.

`materialize_ontology_meta_graph` reflects the complete composed registry into
sorted canonical class, direct-property, relation, facet, value-kind, enum,
and enum-member entities. Typed relations preserve parents, facets, declaring
classes, value kinds, reference and enum refinements, relation endpoints, and
enum membership. External authorities cannot create or mutate the graph.
Registry extension invalidates meta resolution until a complete atomic
rebuild succeeds, then the superseded graph is removed.

Slice 2 is implemented at the approved family boundary, without inventing
concrete literals or operator operand shapes. The rule-language ontology now
contains the abstract Boolean, numeric, integer, float, string, entity,
typed-collection, function, and Outcome-plan expression hierarchy. Enum and
temporal expression families remain absent.

The read-only static type service classifies concrete expression subclasses by
that ontology hierarchy, checks a caller-required root family, and derives
property-read families from canonical direct-property meta entities. Entity
references retain their exact target-class refinement. Enum properties fail
with their nominal enum identity, and legacy datetime properties fail as an
unsupported family; neither can become String. Entity-expression refinement,
outside the implemented descriptor-backed parameter and local shapes, and
concrete operators remain blocked on their later scheduled decisions.

Validated float writes now accept only finite IEEE 754 binary64 values. Parsing
forces round-to-nearest ties-to-even and restores the caller's rounding mode.
Every signed-zero spelling is stored and published as canonical positive zero.
Static validation is const, emits no KG events, and performs no mutation or
randomness.

Slice 3 is implemented through static program validation. The ontology now
contains the closed addressable `ValueTypeDescriptor` hierarchy, non-empty
named `FunctionSignature` and `FunctionParameterSpec` graphs, unordered
`ArgumentBinding` entities, result-family-specific parameter and local reads,
and result-family-specific eager let blocks. Entity and EntityCollection
descriptors preserve exact canonical class refinements. Function descriptors
preserve exact signature identity. Equivalent non-function descriptors compare
structurally without global interning; Numeric accepts Integer and Float while
all still-undecided entity inheritance compatibility remains exact-only.

The validator rejects empty and duplicate-key signatures, malformed or cyclic
higher-order type graphs, missing, duplicate, foreign, and incompatible named
arguments, empty or duplicate-key let blocks, multiple binding ownership,
out-of-scope references, and local dependency cycles. Valid let blocks expose
one deterministic eager topological order, including unused bindings, with
bytewise keys breaking ties. Nested blocks may read enclosing bindings but not
siblings or children. All validation reads one KG snapshot and emits no events.
The mutation guard follows both cited `origin_context` and addressable
`identity_context`, so source-owned descriptors and signatures inherit the
same seal as existing published rules and runtime writes cannot counterfeit a
source identity. Runtime programs remain non-executable drafts until slice 6
adds publication.

0. Closed registry value-kind discriminants and nominal enum definitions,
   members, provenance, generated property refinement, and validated-write
   checks. The first regression proves an unknown kind fails instead of
   passing through. Further tests prove a property accepts only members of its
   exact enum definition and that distinct definitions remain distinct even
   when member spellings or complete member sets match.
1. Immutable registry-backed meta-graph, canonical qualified references,
   addressable content identities, migration from `@@Type:Name`, and a
   regression test proving the old resolver is dead. Enum reflection tests
   prove exact definition and member identity survive materialization.
2. **Implemented.** Active-phase hybrid-typed pure expression families with static
   validation, finite binary64 guards, and no side effects. The verifier
   rejects enum and legacy datetime properties with actionable
   unsupported-family errors and no String fallback.
3. **Implemented.** Non-empty typed function signatures, descriptor-backed
   parameters, unordered named invocation bindings, typed parameter and local
   reads, and deterministic eager multi-binding lexical blocks.
4. Typed literals, property access, relation traversal, comparison, Boolean
   composition, lookup, and arithmetic.
5. Result-family-specific function application, explicit partial application,
   bounded recursion, structured invocation budgets, no implicit result cache,
   reusable functions, map, filter, and fold.
6. Runtime publication and context-owned rule discovery from explicit active
   contexts. Publication validates and seals the complete executable graph
   atomically before it may be activated.
7. Itemized modifier derivation as the first language consumer.
8. KG-backed career-service history and the cited prior-career rule.
9. Characteristic lookup expressed through the same language.
10. Replacement and deletion of `TaskCheck.modifier_table` and
    `modifier_property`, with a regression test proving the old path is dead.
11. Explicit action-rule production of typed outcome plans through the
    existing executor.
