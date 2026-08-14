# Logovger procedure-runner handover

_State captured 2026-08-10 after the generic ProcedureRunner and current
chargen migration passed every verification gate._

## Repository state

Repository: `/Users/luis/Projects/logosphere-public`

Isolated worktree: `/private/tmp/logosphere-logovger-option3`

Branch: `codex/logovger-procedure-runner`

Base commit:
`1905115bd7479c4a7c48fc6800909e409e82f849`

Implementation commit:
`ac005f6813efb6b91c547a8a50fe4b20c3faffe8`

The implementation is a direct descendant of local `main` as it stood at
the start of the phase. During this work, the primary worktree advanced on
the orthogonal `feat/source-locators` branch to
`3870755f3ca2f3ec04bcc4685a15e4315c2110cc`. That worktree and its branch
were not changed here. Local `main` can be fast-forwarded by updating its
branch ref after this handover commit without switching or disturbing the
active primary worktree.

This file supersedes the procedure next-step section in
`LOGOVGER_OUTCOME_EXECUTOR_HANDOVER.md`. The earlier handover remains the
accurate record for outcome execution.

## Owner decision implemented

The selected scope was option 2: build the generic engine runner and migrate
the current playable chargen slice. This phase does not absorb the complete
Cepheus character-creation checklist.

The current game registry declares exactly eight primitives:

- `generate_characteristics`
- `choose_career`
- `roll_qualification`, with `passed` and `failed`
- `roll_survival`, with `passed` and `failed`
- `roll_training`
- `advance_term`
- `choose_term_end`, with `continue` and `muster_out`
- `finish_character`

The list, step order, and route labels are explicit contracts. Procedure
order and gotos live in cited KG data. Primitive behavior remains game code.
Shelob remains the server deployment library and has no role in procedure
execution, rule ingestion, or gameplay.

## Engine contract

`ProcedurePrimitiveRegistry` separates declarations from handlers. A game
first declares each primitive and its complete route-label vocabulary, then
binds one exact handler. Duplicate declarations, replacement handlers, empty
names, empty labels, and handlers for undeclared primitives fail loudly.

`ProcedureRunner` validates the complete graph before invoking the first
handler:

- the root exists and is a `Procedure`;
- it contains only `ProcedureStep` parts;
- `step_index` values are contiguous and start at zero;
- every step names a declared primitive with a bound handler;
- every step part is a `StepRoute`;
- route labels belong to the step primitive's declared vocabulary;
- one step cannot define the same route label twice;
- every route target is a step inside the same Procedure.

A primitive can advance with an optional route label, suspend with a prompt
and typed choices, complete, or fail. A matching seeded route redirects
execution. Otherwise flow falls through to the next ordered step. Resume
invokes the suspended step again with explicit input. The graph is rebuilt
and revalidated on resume, so runtime data changes are observed. A transition
limit stops synchronous route cycles. Handler exceptions become actionable
runner failures.

`ProcedureStep.primitive_ref`, `StepRoute.route_label`, and
`StepRoute.next_step` are now required schema fields. The seed verifier checks
procedure order, primitive contracts, duplicate route labels, and
cross-procedure targets against a game-supplied primitive catalog. A seed
containing a Procedure cannot pass semantic verification without that catalog.

## Logovger migration

`cepheus_basic_chargen_procedure.json` contains one cited Procedure, eight
ordered steps, and four routes:

- qualification failure to finish;
- survival failure to finish;
- continue to the survival step for another term;
- muster out to finish.

The app and test world load and verify both the career seed and procedure
seed. `ChargenSession` binds the eight game handlers and delegates all step
ordering, routing, suspension, and resume to the engine runner. The old
`run_term` control chain and branching inside `choose` were deleted.

The acceptance control retargets only the seeded `continue` route to the
finish step. The same request then ends after one term, proving the loaded
Procedure controls runtime flow. A second control replaces a later
`primitive_ref` with an unknown name and proves the complete graph fails
before a Character or dice roll is created.

`ChargenSession` is explicitly non-copyable and non-movable. Its registered
callbacks capture the session address, so copying would leave callbacks
pointing at the original object. Compile-time assertions prevent that entire
lifetime defect class.

## Transaction and scope boundaries

The runner is an orchestrator, not a transaction spanning the whole
Procedure. A player choice can suspend for an arbitrary duration, so
whole-procedure rollback is neither implemented nor claimed. Each primitive
owns its mutation boundary. `OutcomeExecutor` remains atomic when a primitive
uses it.

The generic runner cannot roll back arbitrary state changed by a handler
before that handler reports failure. Current game handlers validate required
input before mutation where practical, but this is a handler responsibility.

Only order and routing moved to Procedure data. The current game primitives
still contain inherited basic-slice behavior, including characteristic dice,
check execution, skill-table row selection, term aging, and display text.
TaskCheck execution and RollableTable selection are not yet generic engine
runners. The current procedure also retains the previously approved basic
scope: no draft, commission, mishaps, benefits, retirement, or aging crisis.

## TDD record

The first runner test build failed because
`logosphere/rules/procedure_runner.h` did not exist. The implemented suite
then established exact route dispatch, suspension and same-step resume,
complete-graph preflight, registry exactness, undeclared runtime routes,
cross-procedure rejection, cycle limits, and valid pending prompts and
choices.

The first verifier tests failed because `verify_seed` had no primitive
catalog and no Procedure semantic pass. Required-field tests then failed
until the schema made primitive and route fields mandatory. Negative controls
now cover a missing catalog, unknown primitive, undeclared label, step-index
gap, duplicate route label, and cross-procedure jump.

The first chargen test after adding the procedure seed still failed its
data-control assertion: retargeting the seeded route had no effect because the
old handwritten flow remained authoritative. That test passed only after the
old path was deleted and the session ran through `ProcedureRunner`.

Final review found the callback-copy lifetime defect described above. The
compile-time non-copyable and non-movable assertions are its mechanical
guard.

## Verification

Schema generation completed using the repository's declared environment.
Generator regression: 4 of 4 passed.

Focused CTest set: 10 of 10 passed:

- `test_outcome_executor`
- `test_procedure_runner`
- `test_rulebook_pack`
- `test_seed_verifier`
- `logosphere_verify_positive`
- `logosphere_verify_negative`
- `test_logovger_rules_book1`
- `test_skills_pack`
- `test_logovger_table_results`
- `test_chargen`

Full core-profile build passed. Full registered run with `CI=1`: 70 of 70
CTest targets passed. The full-profile `logovger` application target also
compiled and linked successfully, including the Metal shader build. Git
whitespace validation passed.

## Main authored files

Engine:

- `include/logosphere/rules/procedure_runner.h`
- `src/rules/procedure_runner.cpp`
- `include/logosphere/kg/seed_verifier.h`
- `src/kg/seed_verifier.cpp`
- `schema/packs/rulebook.yaml`
- `CMakeLists.txt`
- `tests/test_procedure_runner.cpp`
- `tests/test_seed_verifier.cpp`
- `tests/test_rulebook_pack.cpp`

Logovger:

- `examples/logovger/chargen/procedure_catalog.h`
- `examples/logovger/seeds/cepheus_basic_chargen_procedure.json`
- `examples/logovger/chargen/chargen.h`
- `examples/logovger/chargen/chargen.cpp`
- `examples/logovger/src/logovger_app.h`
- `examples/logovger/test_chargen.cpp`

Documentation:

- `docs/RPG_MODULE.md`
- `CHANGELOG.md`

Generated rulebook, Cepheus character-creation, Cepheus skills, and Voyager
headers and registries were regenerated from the schema and committed.

## Next boundary

Do not invent the remaining full-checklist primitive list. The current open
engineering boundaries are the generic TaskCheck and RollableTable runners,
and the next increment of the Cepheus chargen checklist. Their dependency
order and concrete scope must be presented to the owner before code changes.

No push is part of this handover. The primary worktree's active
`feat/source-locators` branch remains untouched.
