# Logovger TaskCheck runner handover

_State captured 2026-08-10 after declarative TaskCheck execution, seed
colocation, Logovger migration, and all verification gates passed._

## Repository and worktree state

Repository: `/Users/luis/Projects/logosphere-public`

Isolated worktree:
`/private/tmp/logosphere-logovger-task-check-runner`

Branch: `codex/logovger-task-check-runner`

Base commit:
`92a829618a86d8f583b36a2d3160337adba4ea0d`

Signed implementation commit:
`fb6bbcd1531137d70a58dcdb860c0744633f94c8`

The implementation descends directly from local `main` after the
LookupTable selector phase. No rebase or push occurred.

The primary Logovger worktree remains active on `feat/logovger-sheet` at
`828ee512cd993db2264f8c9020de1ad150f3cde7`, with live uncommitted sheet,
multi-career, Draft, narration, schema, procedure, and careers-seed work.
This phase did not edit that worktree.

The separate physics repository was not read, modified, built, or merged.

## Owner decisions implemented

The owner selected a structured, engine-executable TaskCheck contract. A
TaskCheck declares:

- the target entity property in `attribute_ref`;
- the success threshold in `target_number`;
- the complete `DiceExpression` reference in `dice`;
- the `LookupTable` used to map the attribute value to a row;
- the integer result column read from that row.

The engine interprets this generic shape. It contains no Cepheus property,
table, characteristic, or career names.

The owner then selected option A for seed-local references: move the
characteristic modifier table into `cepheus_careers.json`. No cross-seed
alias mechanism and no string name lookup were added.

## Engine contract

`TaskCheckRunner::run(check, target, stream, purpose)` validates every input
before randomness:

- non-empty stream and purpose;
- an existing TaskCheck and target;
- a declared integer target property named by `attribute_ref`;
- a present, exact integer target value and threshold;
- a typed DiceExpression reference and complete valid expression, including
  modifier and multiplier;
- a typed LookupTable reference;
- the complete lookup table through `LookupTableSelector`;
- an integer `modifier_property` on the table's declared entry type;
- a present, exact integer value on the selected row.

Only then does it open a dice transaction and roll. Failure before commit
leaves no dice fact. Success returns the exact check, target, attribute,
attribute value, selected table and row, modifier column and value, threshold,
dice fact, total, and pass result.

`rule_entity_reader.h` contains the shared strict entity-reference, integer,
and DiceExpression readers. `RollableTableRunner` now uses the same readers;
its previous duplicate implementation was deleted.

The seed verifier adds a TaskCheck semantic check. It rejects a
`modifier_property` absent from the lookup table's declared entry type or
declared with any type other than integer.

## Schema and data

The rulebook schema makes all five TaskCheck inputs required. Generated
ontology headers and registries were regenerated.

All 48 career TaskChecks now set:

```json
"modifier_table": "@dm_table",
"modifier_property": "characteristic_modifier"
```

The complete 12-row characteristic modifier LookupTable moved unchanged
from `cepheus_book1_tables.json` into `cepheus_careers.json`. Its type counts,
unique-name checks, and `[0, null]` band-coverage invariant moved with it.
The original table seed no longer contains or counts that table or its rows.

A repository-wide JSON audit found 49 TaskChecks, including the generic test
fixture. All 49 carry the complete five-field contract.

## Logovger migration

Qualification and survival now resolve the Career's typed TaskCheck reference
and pass it with the Character entity to `TaskCheckRunner`. Game code formats
the returned execution for the life log, but does not decide the mechanic.

The following handwritten mechanism was deleted completely:

- `property_int`;
- `characteristic_of` and its six-name switch;
- the local `Check` and `Throw` structs;
- `read_check`;
- `throw_check`;
- the hardcoded lookup of a table named `characteristic_modifiers`.

Career menu rendering still reads the TaskCheck's attribute and threshold for
display. It resolves typed references exactly and now fails loudly on an
invalid Career instead of silently hiding it.

## TDD and verification

Observed red gates before implementation:

- `test_task_check_runner` could not compile because the API did not exist;
- the rulebook test rejected the incomplete TaskCheck schema;
- the colocated-table test failed because the table was still in the other
  seed and TaskChecks lacked their new required fields;
- semantic mutation tests showed that unknown and string modifier columns
  passed ingestion;
- the Logovger integration test showed that the old evaluator ignored
  `modifier_property`.

Final gates:

- TaskCheck runner unit tests: 4 passed;
- Logovger table-result tests: 87 passed;
- Logovger chargen tests: 49 passed;
- complete headless CTest profile: 74 of 74 targets passed;
- full-profile `logovger` executable: built and linked successfully,
  including Metal shaders;
- production seeds parse as JSON;
- all TaskCheck seed instances audited as complete;
- `git diff --check`: clean.

The first full-profile build could not write Apple's Metal module cache under
the filesystem sandbox. The identical build passed after granting that cache
access. This was an environment restriction, not a code failure.

## Parallel sheet session integration

Protect the live work before integrating local `main`. The direct overlap is
substantial in:

- `examples/logovger/chargen/chargen.cpp`;
- `examples/logovger/chargen/chargen.h`;
- `examples/logovger/seeds/cepheus_careers.json`;
- generated character-creation and Voyager ontology files.

The semantic merge for `cepheus_careers.json` must keep both sets of data:

1. retain the sheet session's RuleConstants, Draft table, six EnterCareer
   outcomes, rows, relations, counts, and coverage;
2. retain this phase's colocated `@dm_table`, twelve modifier rows and
   relations, counts, uniqueness declarations, and `[0, null]` coverage;
3. retain `modifier_table` and `modifier_property` on every one of the 48
   TaskChecks;
4. define `@dm_table` before the first TaskCheck because aliases are
   seed-local and resolved in operation order.

The semantic merge for chargen must keep the sheet session's multi-career,
Draft, presentation, and narration behavior while accepting this phase's
TaskCheck execution. Do not restore `Check`, `read_check`, `throw_check`,
`characteristic_of`, or the named modifier-table lookup. Combine the
no-return-career filter with this phase's loud `offer_careers` validation.

### Unresolved prior-career penalty

The live sheet code currently implements the prior-career qualification
penalty by copying the deleted local `Check` and changing its `modifier`.
That code cannot survive this phase unchanged.

This TaskCheck contract represents the base modifier derived from the target
attribute. It does not yet define situational or contextual modifiers. Adding
such a contract is an owner decision and was not invented during this phase.
The parallel session must stop at that boundary and ask before adding a
runtime modifier argument, a structured modifier list, or another data shape.

The live sheet helper `constant(name, fallback)` also supplies `-2` when the
required rule data is absent. That fallback violates the repository rule that
required missing data fails loudly. Preserve the cited RuleConstant, but do
not preserve the fallback behavior.

Shelob is the server deployment library. It has no gameplay or TaskCheck role.

No push is part of this handover.
