# Logovger outcome-executor handover

_State captured 2026-08-10 after the typed outcome executor phase passed the
complete registered test suite._

## Repository state

Repository: `/Users/luis/Projects/logosphere-public`

Isolated worktree: `/private/tmp/logosphere-logovger-option3`

Branch: `codex/logovger-outcome-executor`

Base commit: `2821d434d4ce56c3316f42928632c7f86dbeb785`

Implementation commit:
`4ffbe71a64345df5d27d13f71dfd9bd4e07abed6`

The base was also local `main` when this implementation commit was created.
The previous `feat/chargen-slice` branch no longer existed locally because
its work was already integrated. The separate
`codex/logovger-hardening` worktree remained untouched at `df6662c`.

This file supersedes the next-step guidance in
`LOGOVGER_OPTION3_HANDOVER.md`. That earlier document remains an accurate
historical record of the table-result phase, but its open choice and executor
items are now resolved.

## Contract implemented

Outcome handlers read a const KG view and append an `OutcomePlan`. They do
not receive a mutable KG. One `OutcomeExecutor` traverses the complete root,
validates and applies the plan, and only then exposes its results.

The implemented contract is:

- Exact concrete outcome-type dispatch. Unknown types fail loudly. A
  handler cannot silently replace another handler.
- `NoEffect`, `ModifyAttribute`, `EnsureSkillLevel`, `AdvanceSkill`,
  `GainFixedMoney`, `GainRolledMoney`, and `GrantTableRoll` are engine
  handlers.
- `EnsureSkillLevel` guarantees a minimum. `AdvanceSkill` carries separate
  `initial_skill_level` and `existing_skill_delta` values. The removed
  `GrantSkill` type has no compatibility path.
- Money writes generic `CurrencyBalance` state. Currency stays an entity
  reference supplied by the game. Fixed and rolled amounts are distinct
  concrete outcome types under abstract `GainMoney`.
- `OutcomeSequence` composes against planned state, not stale pre-plan
  state. Repeated attribute, skill, and currency changes therefore compose
  correctly in one root.
- `OutcomeChoice` has explicit `player`, `referee`, or `procedure` authority
  and ordered `OutcomeOption` parts. An unresolved choice returns structured
  options and suspends before mutation. The caller reruns the complete root
  with an explicit selection.
- `GrantTableRoll` returns a typed pending request. It does not choose or
  roll the table implicitly.
- A game-specific exact handler may return a typed `ProcedureSignal`.
- KG state, KG mutation events, dice streams, roll IDs, the dice journal,
  and dice events are covered by the execution transaction. Planning or KG
  validation failure leaves no partial result.

Shelob remains the server deployment library. It has no gameplay,
ingestion, choice, or outcome-execution responsibility.

## Implementation boundaries

`include/logosphere/rules/outcome_executor.h` is the public engine API.
`src/rules/outcome_executor.cpp` contains structural traversal and the
generic handlers.

`include/logosphere/kg/kg_ops_transaction.h` and
`src/kg/kg_ops_transaction.cpp` contain the reusable atomic KG-operation
batch. `src/kg/seed_loader.cpp` now delegates to this batch. The old private
seed rollback implementation was deleted.

`DiceService::begin_transaction` checkpoints every stream, journal length,
and next roll ID. Rolls remain private until commit. Destruction without
commit restores the checkpoint. Nested transactions fail loudly.

`examples/logovger/chargen/chargen.cpp` still owns game procedure, including
choosing the matching skill-table row. It no longer interprets or mutates the
row consequence. It sends the typed outcome to `OutcomeExecutor` and reads
back the committed `SkillRating` for display. The old implicit level-1
default and handwritten repeat-skill increment path were deleted.

## Schema and seed changes

`schema/packs/rulebook.yaml` adds or changes:

- `EnsureSkillLevel`
- `AdvanceSkill`
- abstract `GainMoney`
- `GainFixedMoney`
- `GainRolledMoney`
- `OutcomeChoice`
- `OutcomeOption`
- `CurrencyBalance`
- required operands on executable outcomes and runtime rating or balance
  state

Generated rulebook, Cepheus, and Voyager headers and registries were
regenerated from the schema.

`examples/logovger/seeds/cepheus_careers.json` now encodes each training
result as `AdvanceSkill` with both state branches in data.
`examples/logovger/seeds/cepheus_book1_tables.json` and the seed fixture use
`GainFixedMoney` for fixed debt.

The seed verifier now validates choice authority, option type, labels, and
contiguous zero-based option order. `option_index`, like `step_index`, is
structural data and is excluded from quote-number matching.

## TDD record

The first schema test failed because the generated types and required slots
did not exist. The first choice semantic controls failed before choice
verification existed.

The first atomic KG batch tests failed before the reusable transaction API
existed. The first dice tests failed before stream and journal checkpoints
existed.

The first executor build failed because
`logosphere/rules/outcome_executor.h` did not exist. After the first
implementation, behavior tests exposed a real composition defect: each
outcome calculated from pre-plan state, so two updates to the same attribute
did not compose. The planner now reads its own pending property and part
state. A regression covers repeated attribute changes, a newly created skill
rating, and a newly created currency balance in one sequence.

The chargen integration controls initially failed because procedure code
ignored `existing_skill_delta` and silently defaulted missing level data.
Chargen now follows the executor result. Changing the data to delta 7 changes
the repeated skill to level 8 or higher, and deleting the required delta
stops chargen with an actionable error.

Additional controls cover exact dispatch, duplicate handlers, reserved
structural types, cycles, invalid choice authority, duplicate selections,
missing required outcome data, late planning failure, late KG-validation
failure after a rolled outcome, and rollback of all state and events.

## Verification

Generation completed from the declared environment. Generator regression:
4 of 4 passed.

Focused CTest set: 10 of 10 passed:

- `test_kg_ops_transaction`
- `test_outcome_executor`
- `test_rulebook_pack`
- `test_seed_verifier`
- `logosphere_verify_positive`
- `logosphere_verify_negative`
- `test_skills_pack`
- `test_logovger_table_results`
- `test_chargen`
- `test_dice_service`

Full core-profile build passed. Full registered run with `CI=1`: 69 of 69
CTest targets passed. Git whitespace validation passed.

## Files changed by the implementation commit

Authored engine files:

- `include/logosphere/core/dice_service.h`
- `include/logosphere/kg/kg_ops_transaction.h`
- `include/logosphere/rules/outcome_executor.h`
- `src/core/dice_service.cpp`
- `src/kg/kg_ops_transaction.cpp`
- `src/kg/seed_loader.cpp`
- `src/kg/seed_verifier.cpp`
- `src/rules/outcome_executor.cpp`
- `schema/packs/rulebook.yaml`
- `CMakeLists.txt`

Authored Logovger and test files:

- `examples/logovger/chargen/chargen.h`
- `examples/logovger/chargen/chargen.cpp`
- `examples/logovger/seeds/cepheus_careers.json`
- `examples/logovger/seeds/cepheus_book1_tables.json`
- `examples/logovger/test_chargen.cpp`
- `examples/logovger/test_skills_pack.cpp`
- `examples/logovger/test_table_results.cpp`
- `tests/fixtures/seed/chargen_ch1.json`
- `tests/test_dice_service.cpp`
- `tests/test_kg_ops_transaction.cpp`
- `tests/test_outcome_executor.cpp`
- `tests/test_rulebook_pack.cpp`
- `tests/test_seed_verifier.cpp`
- `docs/RPG_MODULE.md`
- `CHANGELOG.md`

Generated files:

- `src/generated/rulebook_ontology.h`
- `src/generated/rulebook_ontology_registry.cpp`
- `examples/logovger/src/generated/cepheus_book1_character_creation_ontology.h`
- `examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp`
- `examples/logovger/src/generated/cepheus_book1_skills_ontology.h`
- `examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp`
- `examples/logovger/src/generated/voyager_ontology.h`
- `examples/logovger/src/generated/voyager_ontology_registry.cpp`

## Next boundary

Do not invent the procedure primitive list. The next planned phase is the
general Procedure runner and outcome-label routing, but the concrete thin
primitive names remain an owner decision recorded in `docs/RPG_MODULE.md`.

No push is part of this handover. Check current branch and main state before
integrating because another task may have advanced them after this capture.
