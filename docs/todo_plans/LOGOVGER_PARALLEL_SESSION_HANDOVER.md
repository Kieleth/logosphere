# Logovger parallel-session handover

_State captured 2026-08-09 after merging the completed Skills ingestion,
ontology integrity, citation, and dice-verifier phases into local main._

## Read this state first

Repository: `/Users/luis/Projects/logosphere-public`

Current local `main`:

- Branch: `main`
- Commit: `3aa4115c2ff9ca411bafdf8f3e60ed28aeaf02e8`
- Worktree: `/Users/luis/Projects/logosphere-public`

Dedicated hardening work:

- Branch: `codex/logovger-hardening`
- Commit: `3aa4115c2ff9ca411bafdf8f3e60ed28aeaf02e8`
- Worktree: `/Users/luis/Projects/logosphere-logovger-hardening`
- Current `main` was merged into this branch, tested, and main was then
  fast-forwarded to the same commit.
- Do not edit this worktree from another session.

If this file is not present in your worktree, read the committed copy:

```sh
git show codex/logovger-hardening:docs/todo_plans/LOGOVGER_PARALLEL_SESSION_HANDOVER.md
```

Local `main` contains every completed hardening phase through `84525a2`,
the parallel Skills seed-path work from `81be020`, and their tested merge
at `3aa4115`. Start new work from that commit or a descendant.

## What is now integrated and green

Local `main` contains the reviewed skills and seed-verifier work:

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
- Ontology extension now preflights the whole incoming registry. Exact
  entity, relation, property, and ancestor duplicates are idempotent.
  Conflicting definitions throw `OntologyCollision` before any mutation.
- Collision errors identify the definition and both schema sources.
  Generated registries preserve each imported definition's `from_schema`
  provenance.
- Schema generation rejects repeated global classes, enums, or slots in
  an import closure before writing outputs. Class-specific reuse must use
  a class-local attribute.
- This guard exposed and removed three real global-slot shadows:
  Logosphere `relation_type`, Cepheus `strength`, and Logogenesis
  `rock_size`. Imported `Relation.strength` is a float again,
  `Character.strength` remains an integer, Earth `Rock.rock_size` remains
  `RockSize`, and `RelationEvent.relation_type` is required.
- The vendored C++ generator now loads its vendored Jinja templates. The
  generator dependencies are pinned in `environment.yml`, and CI checks
  generator contracts plus committed-output drift.
- Validated `create_entity` operations now reject missing required slots,
  including inherited and entity-reference slots. Duplicate properties
  are rejected instead of applying in vector order.
- LinkML identifier metadata survives generation. KG creation writes the
  numeric `EntityID` into the schema identifier, and validated or trusted
  writes cannot replace or remove it.
- Entity references reject values outside `EntityID` range instead of
  wrapping onto a live `uint32_t` ID.
- Registries validate entity parents, relation endpoints, and property
  reference targets at generation, KG construction, and atomic extension
  boundaries. The guard exposed and corrected a stale test ontology that
  referenced a nonexistent core `Plant` type.
- Cited seed entities require a `source_section`, and that section must be
  the nearest Markdown heading above an exact occurrence of the quote.
- `DiceExpression` fields match one quoted dice expression as a tuple.
  Swapped `6D2` fields no longer pass against separate `2D6` number tokens;
  the book's `1D6×10,000` and `1D6–2` forms are covered.

Verification after required-property and reference-integrity hardening:

- Python generator contracts: 3 of 3 passed.
- Focused registry tests: 40 assertions passed.
- Focused ontology validator tests: 27 tests passed.
- Focused seed-verifier tests: 184 assertions passed.
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

The completed registry, validator, verifier, and Skills seed phases are in
main. No uncommitted work remains in either worktree. The hardening session
is paused at the semantic-completeness decision and owns no new file edits
until the owner selects a row-result representation.

The parallel Skills target is complete. `81be020` introduced
`examples/logovger/seeds/cepheus_book1_skills.json` and moved
`test_skills_pack` onto parse, verify, and load. Merge integration added the
seven required source headings while retaining the registry-provenance and
generated-type guards. Do not rebuild that path through trusted direct
writes.

## Remaining hardening sequence

After registry and required-property integrity, the current ordered review
remains:

1. Complete verifier value, citation, and semantic checks.
2. Resolve the `Character` versus physical `Actor` boundary with the
   owner before changing inheritance.
3. Expand the execution IR beyond the current narrow mutation set.
4. Define persistence and derived-state boundaries.
5. Harden packaging, clean out-of-tree use, performance, and docs.
6. Prove one complete Logovger character-creation slice end to end.

Known risks behind that sequence:

- Requiredness is enforced when declared, but the rulebook schema still
  declares several semantically necessary fields optional. For example,
  `TableEntry.outcome` is optional and the current character-creation
  fixture omits it, so those rows are cited but not executable.
- The current row shape carries one `outcome`, while real rows can combine
  consequences. Mishap row 3 both discharges the character and creates a
  Cr10,000 debt. The current typed outcome vocabulary represents the debt
  but not discharge, and has no composition shape.
- Genuine quotes can contain several numbers, so ordinary values still
  lack field-level provenance.
- `Character` currently inherits the engine's physical living ontology,
  coupling a rules persona to a simulated body.
- The operation IR cannot yet express the full procedure vocabulary.
- Session persistence and derived-value ownership are not defined.

## Binding project decisions

- The book is the rules specification.
- The engine rolls and records dice. A consuming gameplay system receives
  roll facts and cannot invent results. Shelob is the server deployment
  library and has no gameplay or rule-processing role.
- Cepheus book data and Voyager changes remain separate layers.
- Rule instances enter through KG-operation seed files, not a second
  loader grammar.
- Outcomes are typed entities, not executable prose templates.
- Missing or invalid trusted data fails loudly.
- Source drift reports but does not fail verification unless the owner
  changes that decision.
- Do not choose procedure primitives, the referee interruption model,
  persistence format, or `Character` inheritance without the owner.

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
