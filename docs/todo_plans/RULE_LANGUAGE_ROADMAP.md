# Rule-language roadmap

Date: 2026-08-10

Status: roadmap only. None of these items blocks the active rule-language TDD
plan.

## Active boundary

The active phase implements nominal enum registry metadata and member
validation, closed registry value-kind discriminants, immutable ontology
reflection, canonical references, and the typed rule families required by
current Logovger modifiers and task checks.

Enum and legacy `datetime` properties are KG-only during this phase. Rule
programs that attempt to use either fail static validation as unsupported
families. Neither is treated as String.

## E1: First-class enum values

1. Replace legacy scalar KG storage for enum properties with a typed value that
   carries exact enum and member identities.
2. Add EnumExpression and EnumCollectionExpression families with nominal
   static refinement.
3. Add matching literals, reads, signatures, bindings, applications, equality,
   map, filter, and fold behavior.
4. Reject every cross-enum operation even when member spelling or complete
   member sets match.
5. Migrate persisted enum values without exposing a String conversion path.

## T1: Existing datetime containment

1. Audit every runtime write, seed value, persisted value, and consumer of the
   current `date` and `datetime` schema ranges.
2. Select strict KG validation compatible with the intended semantics of those
   actual fields.
3. Delete datetime pass-through from the generic validator.
4. Add regression tests for malformed values and every migrated field.

## T2: Full temporal type family

1. Add mutually distinct Instant, CalendarDate, LocalDateTime, and
   ZonedDateTime scalar families.
2. Add matching literals, reads, collections, signatures, bindings,
   applications, and static-verifier rules.
3. Replace the generic `datetime` marker with explicit closed registry kinds.
4. Prove no temporal path converts to or from String.

## T3: Nominal data-defined calendars

1. Define the calendar ontology and exact CalendarDefinition refinement.
2. Publish calendars as immutable addressable KG graphs.
3. Reject structural compatibility between distinct calendars.
4. Define explicit cross-calendar conversion programs.
5. Preserve versioned meaning by forking rather than mutating published
   definitions.

## T4: Hybrid calendar execution

1. Define required phase-zero kernel signatures and result records.
2. Select the phase-zero operator allowlist and recursion policy.
3. Define exception keys, range semantics, precedence, and inverse mapping.
4. Define totality, uniqueness, and invertibility validation boundaries.
5. Validate and seal the kernel and complete exception set atomically.
6. Reject temporal nodes, world queries, randomness, Outcomes, mutation,
   external calls, and invocation-context reads from calendar kernels.

## T5: Zones and canonical temporal values

1. Select zone-rule authority, data source, version identity, and update
   policy.
2. Define deterministic ambiguous-time and nonexistent-time handling.
3. Select precision, canonical persistence representation, and leap-second
   behavior.
4. Define comparison, projection, conversion, and arithmetic operators.

## T6: Schema migration and integration

1. Migrate each legacy schema range explicitly from generic `datetime` to its
   intended temporal family.
2. Regenerate registries without the legacy marker.
3. Revalidate persisted data and published programs against selected temporal
   version identities.
4. Add application and game fixtures only when a real temporal rule consumer
   exists.
