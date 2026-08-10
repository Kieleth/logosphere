# Logovger RollableTable runner handover

_State captured 2026-08-10 after the two-phase RollableTable runner and
Logovger training migration passed every verification gate._

## Repository and worktree state

Repository: `/Users/luis/Projects/logosphere-public`

Isolated worktree:
`/private/tmp/logosphere-logovger-rollable-table-runner`

Branch: `codex/logovger-rollable-table-runner`

Base commit:
`ce4312bff087408fcc3921c1b0df0d458ce7026c`

Signed implementation commit:
`c9d2b8f8014b768798f67e8aa01182a064de1391`

The implementation descends directly from the combined ProcedureRunner and
source-locator state on local `main`. No rebase or push occurred.

The primary Logovger worktree is active on `feat/logovger-sheet` at the base
commit and has parallel uncommitted UI work in `examples/logovger`. Do not
switch, reset, merge, or edit that worktree from this session. This phase
changed the shared engine rule runner, `chargen.cpp`, and `test_chargen.cpp`
only in the isolated worktree.

The separate physics repository, `/Users/luis/Projects/logosphere-public-2`,
is active on `fix/physics-surgery` at
`96e26888f0b85bd8abca60fe9e0c2d719367b04d` and is clean. It was inspected
read-only and was not changed.

## Owner decision implemented

The owner selected option 3, a two-phase contract.

Phase one selects. The engine validates the complete `RollableTable`, rolls
once, and returns the exact table, row, typed outcome, and citable
`DiceRoll` as one immutable selection value.

Phase two applies. The caller explicitly sends the selection's outcome to
`OutcomeExecutor`. A failed outcome or a pending choice keeps the original
selection. Retrying outcome application does not reroll the table.

There is no hidden recursive table execution. The table runner does not know
how a consequence changes a character. `OutcomeExecutor` does not select a
table row. Game code does not parse table dice or scan row bands.

Shelob remains the server deployment library. It has no role in selection,
outcome execution, rule ingestion, or gameplay.

## Engine contract

`RollableTableRunner::select` accepts a live KG entity, a required named dice
stream, and a required roll purpose. Before consuming randomness it checks:

- the root exists and is a `RollableTable` or subtype;
- `dice` is an exact entity reference to an existing `DiceExpression`;
- required count and sides parse exactly, optional modifier and multiplier
  parse exactly when present, and the complete expression passes the same
  bounds as `DiceService`;
- the table has at least one `HAS_PART` row;
- every part exists and is a `TableEntry` or subtype;
- every row has exact integer `roll_min` and `roll_max`, with minimum not
  greater than maximum;
- every row has an exact reference to an existing typed `Outcome`;
- no row bands overlap;
- every total reachable by the stored dice expression has exactly one row.

Only after preflight does the runner open and commit its own dice transaction.
The returned `RollableTableSelection` has read-only accessors. Its creation
constructor is private to the runner, so consumer code cannot manufacture a
selection. It remains copy-assignable and move-assignable so a suspended
session can store or replace the value without exposing mutable fields.

The selection transaction publishes one existing `DiceRollEvent` and one
journal entry. No new schema type or event was added. The selected table, row,
and outcome remain in the returned value, tied to the citable roll id.

Calling selection inside an already active `DiceService` transaction remains
a programming error because dice transactions cannot nest. This keeps
selection and outcome application as separate transaction boundaries.

## Logovger migration

The `roll_training` primitive still locates the selected career's training
table because that relationship is game meaning. From that point it uses the
generic runner:

1. call `RollableTableRunner::select`;
2. retain the returned roll id for the life timeline;
3. explicitly call `OutcomeExecutor::apply` on the selected outcome;
4. read the resulting `SkillRating` for display.

The old `roll_for_skill` path was deleted. Logovger no longer parses the
table's `DiceExpression`, rolls it directly, reads row bands, chooses a row,
or parses a row outcome reference.

Two application controls prove the data boundary. Changing only the stored
dice modifier and row bands changes the actual training roll expression and
still completes. Removing the outcome from a row that this seed would not
select rejects the complete table before a training roll or `SkillRating`
mutation.

The current display projection still expects a training outcome root with a
`skill` reference because every current career training row selects
`AdvanceSkill`. If a later data phase changes training roots to sequences,
choices, or another outcome shape, the application needs a generic applied
result projection before that data can drive the current skill label. Do not
hide that projection inside the table runner.

## TDD record

The first engine test build failed because
`logosphere/rules/rollable_table_runner.h` did not exist. The implemented
suite now covers exact selection, one citable roll and event, complete-table
preflight, wrong part types, overlaps, reachable-total gaps, missing context,
dangling dice references, failed outcome reuse, and pending-choice reuse.

The first Logovger controls failed against the handwritten path:

- changed dice data failed with `skills and training roll 3 matches no table
  row` because the old code ignored `dice_modifier`;
- a malformed unselected row did not stop chargen because the old code only
  inspected the row hit by the roll.

Both controls passed only after the handwritten parser and matcher were
deleted and the engine runner became authoritative.

Final API review added compile-time guards. They first failed because the
initial selection type was publicly constructible and not copy-assignable.
The final class is consumer-nonconstructible, read-only, and assignable as a
value.

## Verification

Focused CTest set: 4 of 4 passed:

- `test_rollable_table_runner`
- `test_outcome_executor`
- `test_procedure_runner`
- `test_chargen`

Full core-profile build passed. Full registered run with `CI=1`: 72 of 72
CTest targets passed.

The full-profile `logovger` target compiled and linked successfully,
including the Metal shader build. The first full build attempt was blocked by
the filesystem sandbox at Apple's compiler module cache. The identical
declared build passed when allowed to write that cache. This required no
source or configuration change.

Git whitespace validation passed. No schema changed, so generator output and
generator contracts were not part of this phase.

## Authored files

Engine:

- `include/logosphere/rules/rollable_table_runner.h`
- `src/rules/rollable_table_runner.cpp`
- `tests/test_rollable_table_runner.cpp`
- `CMakeLists.txt`

Logovger:

- `examples/logovger/chargen/chargen.cpp`
- `examples/logovger/test_chargen.cpp`

Documentation:

- `docs/RPG_MODULE.md`
- `CHANGELOG.md`
- this handover

## Next dependency order

The next known engine boundary is generic lookup-table selection, followed by
TaskCheck execution. The current `TaskCheck` path still calls the hardcoded
characteristic modifier function, while the seeded characteristic modifier
table is not yet complete enough to replace it. The dependency order is:

1. build a generic `LookupTable` selector and complete the modifier-table
   data;
2. build `TaskCheck` on the generic lookup and dice mechanisms;
3. migrate qualification and survival off their handwritten check path;
4. continue the broader Cepheus checklist only after its primitive and data
   scope is presented to the owner.

Persistence of selection values across process restarts remains part of the
larger unresolved session-persistence boundary. This phase guarantees
in-memory reuse and citable dice provenance, not durable session storage.

No push is part of this handover.
