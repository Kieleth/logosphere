# Logovger parallel-session handover

_State captured 2026-08-09 after synchronizing the dedicated hardening
worktree with local `main`._

## Read this state first

Repository: `/Users/luis/Projects/logosphere-public`

Current local `main`:

- Branch: `main`
- Commit: `792c934086299865600d5f3e73ac7540ac1adcf0`
- Worktree: `/Users/luis/Projects/logosphere-public`

Dedicated hardening work:

- Branch: `codex/logovger-hardening`
- Commit: `7ec8212a7f5fa40755f3af16504f947c1844b811`
- Worktree: `/Users/luis/Projects/logosphere-logovger-hardening`
- `main` was merged into this branch at `7ec8212`.
- Do not edit this worktree from another session.

If this file is not present in your worktree, read the committed copy:

```sh
git show codex/logovger-hardening:docs/todo_plans/LOGOVGER_PARALLEL_SESSION_HANDOVER.md
```

Do not assume `main` contains the hardening commits. At the state above,
it does not. Work on a separate branch and worktree, then report the
commit for deliberate integration.

## What is now integrated and green

Local `main` brought in the reviewed skills and seed-verifier work:

- The corrected Cepheus source pin and stronger Skills assertions.
- A 20-operation character-creation seed fixture.
- Verifier checks over loaded state, including later property writes.
- Per-entity source files and source-path traversal rejection.
- Required citations for types carrying the citation slot.
- Exact numeric-token checks, comma handling, signed values, and the
  table-band shapes printed by the source.
- Stronger invariant diagnostics and source-commit drift reporting.
- CTest execution in the Linux headless gate.

The hardening branch adds:

- `60d38ca`: whole-seed atomic rollback. Failed loads restore created
  entities, existing properties, new properties, and relations. They
  expose no partial report state and emit no mutation events.
- Successful seed events publish only after the complete graph commits.
- `destroy_entity` and `play_cinematic` are rejected in additive seed
  layers.
- `29ed226`: every headless test executable must be registered with
  CTest. The Linux gate runs the complete registered suite. Seven fake
  green `CI` exits were removed.
- `85543f0`: directly constructed dice expressions use the same bounds
  as parsed expressions before touching RNG, IDs, journal, or events.
- Cepheus EHex and characteristic modifiers now follow the cited
  33-or-higher `Z` and `DM+9` row.
- `7ec8212`: merged current `main`, retained its expanded verifier
  coverage, and reconciled its loader assumptions with atomic loading.

Verification at `7ec8212`:

- Focused Logovger suite: 6 of 6 CTest targets passed.
- Full headless build: passed.
- Full run with `CI=1`: 65 of 65 CTest targets passed.

The focused targets were:

- `test_seed_verifier`
- `logosphere_verify_positive`
- `logosphere_verify_negative`
- `test_skills_pack`
- `test_dice_service`
- `test_logovger_rules_book1`

## Ownership boundary between sessions

The hardening session currently owns ontology registry composition. Do
not modify these files until that step lands or ownership is explicitly
transferred:

- `include/logosphere/kg/ontology_registry.h`
- `src/kg/kg_module.cpp` and its extension API
- `scripts/generate_registry.py`
- generated `*_ontology_registry.cpp` files
- registry collision and provenance tests

The next defect under review is factual: `OntologyRegistry::extend`
silently overwrites entity and relation definitions, appends duplicate
properties, and records no source provenance. Generated pack registries
contain their imported schema closure, so identical duplicates are
normal while conflicting duplicates must not be silent.

An owner decision is still required between:

1. Atomic extension that treats exact duplicates as idempotent and
   throws on a conflicting definition, with both sources in the error.
2. The same collision policy through a mandatory result object rather
   than an exception.

Do not implement either policy without that decision.

## Best orthogonal implementation target

If your current assignment does not already name a narrower target,
move the Skills test data onto the production ingestion path.

The present `examples/logovger/test_skills_pack.cpp` still constructs
the seven cited Skill instances with direct `createEntity`,
`setProperty`, and `createRelation` calls. That bypasses the seed
envelope, alias resolver, validator, atomic loader, and verifier.

Use TDD to:

1. Add a Cepheus Skills seed fixture containing the seven existing
   cited instances and their `SPECIALIZES` tree.
2. Pin it to the current vendored `SOURCE_COMMIT`.
3. Load and verify it through `parse_seed_envelope`, `load_seed`, and
   `verify_seed`.
4. Make `test_skills_pack` read the resulting KG instead of rebuilding
   the same facts through trusted direct writes.
5. Preserve the existing checks for the Slug Rifle correction, Sciences
   text, fixed cascade links, typed `SkillRating.skill`, and citation
   round trips from the KG.
6. Add a negative test proving a late invalid Skill relation leaves no
   partial Skills graph.

Keep this work out of registry and schema generation files. If the
current ontology cannot express a required fact, stop and report the
specific missing type, property, relation, and source citation. Do not
invent a schema extension as a workaround.

Likely owned files for this parallel slice:

- A new fixture under `tests/fixtures/seed/`
- `examples/logovger/test_skills_pack.cpp`
- `CMakeLists.txt` only if a new test target is truly needed. Every
  `test_*` or `at_*` target in the headless profile must have a matching
  CTest registration.

## Remaining hardening sequence

After registry collisions, the current ordered review remains:

1. Enforce required ontology properties and reference validity.
2. Complete verifier value, citation, and semantic checks.
3. Resolve the `Character` versus physical `Actor` boundary with the
   owner before changing inheritance.
4. Expand the execution IR beyond the current narrow mutation set.
5. Define persistence and derived-state boundaries.
6. Harden packaging, clean out-of-tree use, performance, and docs.
7. Prove one complete Logovger character-creation slice end to end.

Known risks behind that sequence:

- `required` schema properties are not generally enforced at entity
  creation.
- Genuine quotes can contain several numbers, so ordinary values still
  lack field-level provenance.
- `Character` currently inherits the engine's physical living ontology,
  coupling a rules persona to a simulated body.
- The operation IR cannot yet express the full procedure vocabulary.
- Session persistence and derived-value ownership are not defined.

## Binding project decisions

- The book is the rules specification.
- The engine rolls and records dice. Shelob receives roll facts and
  cannot invent results.
- Cepheus book data and Voyager changes remain separate layers.
- Rule instances enter through KG-operation seed files, not a second
  loader grammar.
- Outcomes are typed entities, not executable prose templates.
- Missing or invalid trusted data fails loudly.
- Source drift reports but does not fail verification unless the owner
  changes that decision.
- Do not choose procedure primitives, the referee interruption model,
  persistence format, registry collision API, or `Character` inheritance
  without the owner.

## Handoff requirements

Before returning parallel work, provide:

- Branch, worktree, and commit hash.
- Exact files changed.
- The red test observed before implementation.
- Focused test output after implementation.
- Full registered headless CTest result if CMake, KG, loader, verifier,
  schema, or generated code changed.
- Any owner decision still needed.

Do not merge, rebase, push, or edit another session's worktree unless
the owner explicitly asks.
