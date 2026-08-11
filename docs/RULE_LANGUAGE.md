# Ontology-native rule language

_Architecture contract. Decided 2026-08-10, not yet implemented. This is
engine guidance for every game and application that authors executable rules
in a Logosphere knowledge graph. Logovger is the first consumer, not the
definition of the mechanism._

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

Each meta-entity requires a stable qualified key. Runtime entity IDs are not
portable rule syntax. The concrete key grammar and seed reference syntax are
still open, but loading must resolve a stable key to exactly one meta-entity or
fail.

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

The names and exact shapes of these classes are not decided yet. Their type
contract is decided: generic stringly nodes with fields such as
`operator = "multiply"` or `property = "intelligence"` do not satisfy this
architecture.

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

### Initial value families

The first language version mirrors every value category currently supported
by the KG registry:

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

Selecting the complete registry family did not by itself select conversions,
string operators, floating-point comparison rules, or collection behavior.
Integer-to-float widening is decided below. The others remain explicit
language decisions.

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
Boolean-to-number conversion. It also does not decide what happens when a KG
integer cannot be represented exactly as the engine's floating-point type.

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
- the root produces the result type required by its caller;
- the reachable expression graph is complete and acyclic;
- every selected rule belongs to an explicitly active context;
- published nodes have not been mutated;
- numeric evaluation cannot overflow silently;
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
not the persistence format or source of truth. Round-tripping such syntax must
preserve the exact typed graph.

## Decisions still required

Do not implement past these boundaries without owner decisions:

1. The initial operator vocabulary.
2. Variable scope, binding sources, and whether reusable functions exist.
3. The stable qualified-key grammar for meta-entities and seed references.
4. Static handling of inheritance, unions, and entity collections.
5. Predicate composition, including whether negation exists under an
   open-world KG.
6. Numeric precision loss, overflow, division, and non-finite float semantics.
7. Rule result identity, modifier stacking, precedence, suppression, and
   future higher-authority overrides.
8. Context activation, hierarchy, duration, and expiry.
9. Program sharing, recursion policy, evaluation budgets, and caching.
10. Persistence and migration when an ontology revision changes a referenced
    meta-entity.

## TDD implementation order

Each slice starts with a failing contract test and removes any mechanism it
replaces.

1. Immutable registry-backed meta-graph and strict meta-reference resolution.
2. Registry-complete hybrid-typed pure expression families with static
   validation and no side effects.
3. Typed variables and invocation bindings.
4. Typed property access, relation traversal, filtering, aggregation, and
   arithmetic, limited to the approved initial vocabulary.
5. Context-owned rule discovery from explicit active contexts.
6. Itemized modifier derivation as the first language consumer.
7. KG-backed career-service history and the cited prior-career rule.
8. Characteristic lookup expressed through the same language.
9. Replacement and deletion of `TaskCheck.modifier_table` and
   `modifier_property`, with a regression test proving the old path is dead.
10. Explicit action-rule production of typed outcome plans through the
    existing executor.
