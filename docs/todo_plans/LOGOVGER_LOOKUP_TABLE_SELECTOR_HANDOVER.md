# Logovger LookupTable selector handover

_State captured 2026-08-10 after generic lookup selection, complete
characteristic-modifier ingestion, and Logovger check migration passed every
verification gate._

## Repository and worktree state

Repository: `/Users/luis/Projects/logosphere-public`

Isolated worktree:
`/private/tmp/logosphere-logovger-lookup-table-selector`

Branch: `codex/logovger-lookup-table-selector`

Base commit:
`ff53de1546aa5b891d21a7bd4bb17ddce85a1a66`

Signed implementation commit:
`e73419aee3cb5f870f9505697f31f4b89028b6ff`

The implementation descends directly from local `main` after the
RollableTable phase. No rebase or push occurred.

The primary Logovger worktree is active on `feat/logovger-sheet` at
`828ee512cd993db2264f8c9020de1ad150f3cde7`. It has live uncommitted sheet and
chargen work. This phase did not edit that worktree. Both branches change
`examples/logovger/chargen/chargen.cpp`, so the sheet session must preserve
its UI changes while accepting the modifier lookup in `throw_check` when it
later integrates `main`.

The separate physics repository is outside this work. It was not read,
modified, built, or merged during this phase.

## Owner decision implemented

The owner selected explicit unbounded lookup bounds, option 1.

Each side of a `LookupEntry` band now has exactly one valid form:

- a finite `key_min` or `key_max` value;
- the matching `key_min_unbounded=true` or `key_max_unbounded=true` flag.

Omitting a finite bound without a true flag fails. Supplying both the finite
bound and its true flag also fails. There is no sentinel integer and no
implicit interpretation of a missing property.

## Engine contract

`LookupTableSelector` accepts a live `LookupTable` entity and an integer key.
Before returning any row it validates the complete table:

- the table exists and is a `LookupTable` or subtype;
- `entry_type` names a known, concrete `LookupEntry` subtype;
- the table has `HAS_PART` rows;
- every row exists and conforms to the declared entry type;
- both sides of every band use exactly one finite or explicit-unbounded form;
- every finite lower bound is no greater than its finite upper bound;
- no bands overlap, including unbounded bands.

An uncovered requested key fails. The result contains the exact table, typed
row, and key. The selector does not read or interpret game-declared result
columns.

The selection value has read-only accessors and a private constructor. It is
copy-assignable and move-assignable for session storage, but consumer code
cannot manufacture one.

The seed verifier calls the selector's same table preflight. Runtime and
ingestion therefore cannot disagree about lookup structure. The
`band_coverage` invariant now accepts an explicit JSON null endpoint, so
`[0, null]` proves complete coverage from zero upward without a maximum
sentinel. Value verification understands cited `N or higher` and `N or
lower` row labels.

## Cepheus data and Logovger migration

`cepheus_book1_tables.json` now contains all twelve cited characteristic
modifier rows from 0 through 2 up to 33 or higher. The last row stores
`key_min=33` and `key_max_unbounded=true`. Its coverage invariant is
`[0, null]`.

Logovger loads that table seed before careers and the chargen procedure.
Qualification and survival select the modifier row for the named
characteristic, then read the required `characteristic_modifier` result
column. Changing only those values changes the same seeded qualification
throw. Missing selected modifier data fails before the qualification roll.

The old `examples/logovger/rules/characteristics.h` formula was deleted. No
`characteristic_dm` symbol or include remains.

## TDD and verification

The implementation started from two observed failures:

- `test_lookup_table_selector` could not compile because the engine API did
  not exist;
- `test_rulebook_pack` rejected the new optional-bound schema contract.

The Logovger data-control test then failed because changing
`characteristic_modifier` had no effect while chargen still called the
hardcoded formula.

Final gates:

- ontology generator tests: 4 passed;
- affected lookup, rulebook, verifier, table-result, and chargen tests: 5
  targets passed;
- complete core profile: 73 of 73 CTest targets passed;
- full-profile `logovger` executable: built successfully, including Metal
  shaders and application linkage;
- generated ontology files regenerated from source schemas;
- production table seed parsed as JSON;
- `git diff --check`: clean.

The first full-profile attempt could not write Apple's Metal module cache
under the filesystem sandbox. The identical build completed after granting
that cache access. This was an execution-environment restriction, not a code
failure.

## Parallel sheet session integration

The sheet branch already includes `main` through the RollableTable handover.
Before it integrates this phase, it should commit or otherwise protect its
live work. The expected semantic combination in `chargen.cpp` is:

1. retain the sheet session's presentation and state changes;
2. retain this phase's `LookupTableSelector` include;
3. retain the modifier-table lookup and loud error path in `throw_check`;
4. do not restore `rules/characteristics.h` or `characteristic_dm`.

Its edits to `chargen.h`, `procedure_catalog.h`, the procedure seed, careers
seed, and sheet screen do not overlap this phase except where the resulting
chargen behavior meets `chargen.cpp`.

## Next boundary

The next known boundary is a generic engine `TaskCheck` runner. The current
Logovger `read_check` and `throw_check` still resolve entity references, parse
the `DiceExpression`, roll, add the selected modifier, compare the target,
and format the result in game code. The lookup formula is gone, but the
complete check operation is not yet an engine service.

The `TaskCheck` runner contract still needs to be presented to the owner
before implementation, especially how generic engine code receives the
attribute value and game-specific modifier result column without learning
Cepheus schema.

Shelob remains the server deployment library. It has no gameplay,
rule-ingestion, lookup, or check-execution role.

No push is part of this handover.
