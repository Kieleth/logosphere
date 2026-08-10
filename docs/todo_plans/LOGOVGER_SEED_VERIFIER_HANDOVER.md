# Logovger seed verifier handover

_Written 2026-08-09 after the seed-verifier session stopped mid-review.
The work is preserved unchanged in commit `a130779`. This document
records what exists, what was actually tested, and what remains unsafe._

## Repository and branch state

- Repository: `/Users/luis/Projects/logosphere-public`
- Worktree: `.claude/worktrees/seed-verifier`
- Branch: `feat/seed-verifier`
- Preserved head: `a130779` (`wip(kg): preserve interrupted seed verifier work`)
- Parent feature commit: `e0adee7` (`feat(kg): seed files + the three-check ingestion verifier`)
- Common base with the current skills work: `7468bc9`
- Related branch: `feat/cepheus-skills` at `ffb180b`
- Remote status: `feat/seed-verifier` has no remote tracking branch or PR.

The worktree was clean immediately after `a130779`. This handover is
the only later change.

## What the session built

The branch adds the engine-side seed ingestion path intended by
`docs/RPG_MODULE.md` steps 4 and 6:

1. A strict JSON seed envelope containing a source file and commit,
   layer name, invariants, and KG operations.
2. `"as": "@alias"` binders on `create_entity` operations.
3. Alias resolution for operation targets and `entity_ref` property
   values.
4. Loading through `validate_kg_op` followed by `apply_kg_op`.
5. A registry-parameterized verifier with schema, verbatim, value,
   and invariant checks.
6. Source-drift warnings using the vendored `SOURCE_COMMIT` file.
7. A `logosphere-verify` CLI and positive and negative CTest entries.
8. Seed fixtures for a small character-creation rules graph.

The preserved review work in `a130779` tightened the first version:

- Reused load reports are cleared before every load.
- Duplicate alias binders fail before the duplicate operation mutates
  the world.
- A bare `@` reference is rejected.
- Verification reads the loaded world, not only raw create operations.
- Every type declaring `source_quote` must carry a citation.
- Per-entity `source_file` is supported.
- Source paths containing parent traversal are rejected.
- Numeric values must equal complete number tokens, so `5` no longer
  passes merely because a quote contains `15`.
- Thousands separators, negative values, ASCII ranges, en-dash ranges,
  and `through` ranges are handled.
- Table bands outside the asserted range fail.
- Unique-name checks require non-empty names and reject duplicates.
- Source drift reports without changing the verification exit status,
  following the recorded owner decision.

## What was actually tested

Both feature branches were exported from their committed heads into
temporary trees and configured headlessly. The real worktrees were not
modified by configuration or testing.

`feat/cepheus-skills` at `ffb180b`:

- `test_skills_pack`: passed
- `test_rulebook_pack`: passed
- `test_ontology_validator`: passed
- Result: 3 of 3 focused tests passed.

`feat/seed-verifier` at `a130779`:

- `test_kg_ops_parse`: passed
- `logosphere_verify_positive`: passed
- `logosphere_verify_negative`: passed
- `test_seed_verifier`: failed
- Result: 3 of 4 focused CTest targets passed.
- Assertion result inside `test_seed_verifier`: 69 passed, 1 failed.

The failure is understood and reproducible. The test still constructs
its source root with `examples/logoveyer`, while the directory is now
`examples/logovger`. Eight verbatim checks consequently fail to open
the source file, producing the single aggregate assertion failure.

Do not describe this branch as green or repeat the existing `70/0`
claim until the renamed-path test passes from the committed tree.

## Load-bearing defect: loading is not atomic

`load_seed` validates and applies operations one at a time. When
operation N fails, operations 0 through N minus 1 remain in the target
KG. Only the failing operation is guaranteed not to mutate.

This contradicts the header's statement that a partial seed load is a
bug. It also contradicts the intended use of seed files as canonical,
trusted layers. A malformed later relation can leave an apparently
valid but incomplete rules graph behind.

The first behavioral change must be TDD:

1. Construct a seed whose first operations create valid entities and
   whose later operation fails validation.
2. Assert that the target KG, relations, emitted mutation events, and
   caller-visible bindings are unchanged after failure.
3. Observe the test fail against `a130779`.
4. Implement whole-batch validation and transactional commit, or apply
   to an isolated staging world and commit only after success.
5. Keep no sequential partial-application fallback.

Forward aliases are also unsupported. An alias may refer only to an
entity created earlier in file order. Decide explicitly whether seed
files require topological order or whether the loader allocates all
create binders in a first pass. Do not let this emerge accidentally
from implementation order.

## Verifier limitations that remain

The value check is stronger than substring matching, but it is not yet
value-level provenance.

- If a quote contains several numeric tokens, a stored value can match
  the wrong token and pass.
- Non-numeric extracted values receive no semantic comparison.
- Source byte spans and content hashes are not stored per extracted
  value.
- A quote can be genuine while a nearby stored fact is still wrong.
- Required schema properties are not generally enforced by
  `validate_create`; that is an engine validator defect outside this
  branch.

Table-row bands are tied to the leading source cell and are therefore
stronger than ordinary numeric properties. General rule values still
need structured evidence linking each stored value to a source span or
field-specific extraction result.

## Relationship to `feat/cepheus-skills`

The branches are not orthogonal.

`feat/cepheus-skills` supplies:

- The current corrected Cepheus source at commit
  `efb8f9d8fa0c53119b9d18bee6f580191ddd0107`.
- The `Skill` game class.
- The engine `SkillRating` class.
- Cascade relations and focused rule tests.

`feat/seed-verifier` supplies the loader and verifier that those skill
instances are meant to use.

Their only textual merge conflict is in `CMakeLists.txt`, where both
branches add adjacent test targets. The resolution must retain both
target sets and use the `Logovger` spelling.

The seed fixture currently pins source commit
`0839018902355215fb8148f0b4ce1b1f8e011080`, so combining the branches
will produce the expected source-drift warning. Do not blindly replace
that pin. Run every citation and value check against the corrected
source first, then update the pin in the same green change.

The Skills test currently creates most entities through direct
`createEntity` and `setProperty` calls. After integration, convert its
fixture to the seed loader so the test proves the production ingestion
path rather than a trusted internal bypass.

## Recommended integration path

Do not restart this work from `main`. That would duplicate the alias,
loader, verifier, SkillRating, and skills-pack changes and force a
larger reconciliation later.

The evaluated path is:

1. Create the hardening branch from `feat/cepheus-skills`.
2. Merge `feat/seed-verifier`, preserving both source branches.
3. Resolve the single CMake conflict by retaining both feature target
   sets and the corrected project spelling.
4. Reproduce the current renamed-path failure as the red baseline.
5. Fix only that path and restore the merged baseline to green.
6. Verify the corrected source and update the seed source pin.
7. Add the whole-batch atomicity regression before changing the loader.
8. Make seed loading atomic and decide forward-reference semantics.
9. Convert the Skills fixture to the validated seed path.
10. Make all seed, skills, rulebook, dice, ontology, and KG-op tests
    actual headless CI gates.

## CI and build facts

- CTest registers the new Skills and seed-verifier targets.
- The headless GitHub workflow manually enumerates an older test list,
  so neither feature is currently a CI gate.
- `test_ontology_validator` still contains CI-specific skip behavior on
  the reviewed base.
- CMake configuration writes `include/logosphere/build_info.h.tmp` into
  the source tree. This prevents configuration against a read-only
  source export and violates clean out-of-tree build expectations.

These belong at the front of the general hardening sequence once the
two feature branches are integrated and green.

## Key files

- `include/logosphere/kg/seed_loader.h`
- `src/kg/seed_loader.cpp`
- `include/logosphere/kg/seed_verifier.h`
- `src/kg/seed_verifier.cpp`
- `include/logosphere/kg/kg_ops.h`
- `src/kg/kg_ops_parse.cpp`
- `tests/test_seed_verifier.cpp`
- `tests/fixtures/seed/chargen_ch1.json`
- `tests/fixtures/seed/negative_misquote.json`
- `tools/logosphere_verify.cpp`
- `examples/logovger/test_skills_pack.cpp` on `feat/cepheus-skills`
- `schema/packs/rulebook.yaml` on `feat/cepheus-skills`
- `docs/RPG_MODULE.md` on `feat/cepheus-skills`

## Decisions already binding

- The book is the rules specification.
- The engine rolls and records dice; Shelob cannot invent a roll.
- Cepheus book data and Voyager deviations remain separate layers.
- Rule instances enter through KG-ops seed files, not a second loader
  grammar.
- Outcomes are typed entities, not executable text templates.
- Missing or invalid trusted data fails loudly.
- Source drift reports but does not gate until the owner changes that
  ruling.
- The concrete procedure primitive list, referee interruption model,
  and persistence manifest format remain owner decisions. Do not build
  past them without approval.

## Definition of a safe handoff

The interrupted implementation is safely resumed when a new branch
contains both feature histories, all focused tests are green from a
clean export, seed failure is atomic, the corrected source pin is
verified rather than merely replaced, and CI executes the same public
paths used by Logovger.
