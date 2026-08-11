# Enum compatibility micro-spike

Date: 2026-08-10

Status: complete. The owner selected option 1, nominal and closed.

## Question

Should first-class enum values use nominal closed types, structural closed
types, or nominal types whose member set may be extended?

## Method

The spike audited every ontology YAML under `schema/` and
`examples/*/schema/`, inspected the generator and validator, and compiled a
C++ probe against the generated ontology headers. The audit is reproducible
with the dependencies already pinned in `environment.yml`:

```text
conda run -n logosphere_env python scripts/spike_enum_compatibility.py
```

The script fails with an actionable dependency error outside the declared
environment. It also self-checks the open-vocabulary description matcher that
initially produced a false positive on `FloorType`.

## Observations

The 12 schema files define 36 uniquely named enums. They contain 31 direct
enum-ranged fields. Generated registry snapshots contain 139 enum property
registrations because imported definitions appear in multiple composed
registries, but every registration retains only the generic `"enum"` marker.

Only one pair of definitions has an identical member set: `LogType` and
`DeadwoodKind`, both containing `BRANCH`, `LOG`, `TRUNK`, and `TWIG`. Nine
different pairs overlap without being identical. Examples include
`MaterialType.STONE` and `FloorType.STONE`, `EntityStatus.DESTROYED` and
`EntityModification.DESTROYED`, and several unrelated `NONE` members.

The generated C++ surface is already nominal. A compile-time probe verified
that `earth::ontology::LogType` and
`logogenesis::ontology::DeadwoodKind` are different, non-convertible types even
though their current member sets are identical.

The schema already distinguishes closed and open vocabularies. For example,
`TransformationTrigger` is an enum because the engine owns a closed set of
event sources. `TransformationEffect` is explicitly described as a vocabulary
floor and its property is not enum-ranged because applications may register
more effects. That current open representation is textual and therefore does
not satisfy the new first-class type contract, but the semantic distinction is
deliberate.

## Option consequences

### 1. Nominal and closed

An enum value carries its exact enum identity and exact declared member.
Spelling overlap never creates compatibility. Adding a member requires a new
composed ontology revision, but does not change compatibility between existing
enum types.

This matches the generated C++ API, the schemas' closed-range meaning, the
immutable meta-graph, and fail-loud validation. It requires the generator and
registry to preserve enum definitions and members instead of erasing them.

### 2. Structural and closed

Compatibility depends on complete member-set equality. It makes exactly one
current pair compatible. Adding a member to either side then makes previously
compatible types incompatible, even though neither definition changed its
identity. It also equates independently named concepts solely because their
current vocabularies happen to match.

The current schemas provide no consumer that needs this behavior. Reuse is
better expressed by referencing one shared enum definition.

### 3. Nominal and extensible

The enum identity remains exact, but the accepted member set depends on active
extensions. This supports vocabulary growth without changing the base enum,
but validation and rule meaning then depend on extension provenance, ordering,
conflict rules, activation, persistence, and migration. A sealed published
program could change meaning when a new member extension becomes active.

If extensions are allowed only through a new versioned ontology composition,
the effective runtime contract becomes option 1 for each composed registry.

## Additional path for open vocabularies

An open vocabulary does not need to be an extensible enum. It can be modeled as
identity-bearing KG entities under an ontology class, with properties and
relations carrying meaning. Rules then refer to typed entities already present
in the active content layers. This preserves runtime extensibility and graph
composition without weakening closed enum validation or reverting to text.

## Spike result

The evidence favors option 1, nominal and closed. Structural compatibility has
one current beneficiary and unstable evolution semantics. Runtime-extensible
members conflict with the sealed registry unless extension occurs by versioned
ontology composition. Truly open vocabularies fit typed KG entities better than
enums.

The owner accepted this recommendation on 2026-08-10.
