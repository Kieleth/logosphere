# Logovger ingestion ledger roadmap and handover

State captured 2026-08-17 after R11 seed-order enforcement and the
owner's option 4 ledger decision. This is the active checklist for the
ingestion-ledger work and the handover for a parallel Logovger session.
It does not supersede the protocol or decision ledger:

- `docs/REFLECTION_PROTOCOL.md` owns the binding ingestion rules.
- `docs/RPG_MODULE.md` owns the append-only owner decision ledger.
- `docs/CUMULATIVE_INGESTION.md` owns the R11 and R12 rationale.
- this file owns implementation order, completion state, and handoff.

## Repository boundary

The owner corrected the local-repository choice during implementation. The
authoritative repository for this work is
`/Users/luis/Projects/logosphere-public-2`. Work in the isolated
`/private/tmp/logosphere-logovger-ingestion-ledger` worktree so the other local
session can continue. Do not edit physics code.

PR #138 merged the completed ingestion-ledger phase to `main` as
`9db62b8` on 2026-08-18. Continued work uses
`codex/logovger-full-ingestion`, created from that exact commit. The prior
`codex/logovger-ingestion-ledger` remote branch was deleted by the merge.

At the original handover, the four open remote PR heads were unchanged and
orthogonal to the phase except for possible merge-text overlap in
`CMakeLists.txt` and `CHANGELOG.md`:

- PR 137: `a33c9b5`
- PR 134: `9857bf6`
- PR 129: `5670156`
- PR 96: `6f88f7c`

Recheck remote state before every later integration. Do not rebase, edit
another session's worktree, touch physics, or assume a historical branch is
idle from this document alone.

## Binding decisions

- The ledger covers the whole pinned source, not only material already
  judged to contain rules.
- Extraction is cumulative under R11. Claims record what prior concepts
  they resolved against.
- Forward references use R12 typed accommodation. The representation is
  upstream malleus work; nothing provisional executes, and the queue
  closes before play.
- Option 4 is selected: source coverage and semantic claims are separate
  levels.
- Coverage uses atomic source leaves: headings, prose sentences, table
  cells including header and empty cells, and sentences within list
  items.
- Unclassified source content becomes an opaque leaf or fails
  enumeration. It never disappears.
- A pinned-file manifest accounts for every source file, including one
  with no content leaves.
- Paragraphs, table rows, sections, and files may later provide derived
  filtering, batching, display, and roll-ups. They never replace leaf
  records or own a second coverage status.
- Coverage leaves are mechanically enumerable and addressable. Exact
  duplicate and empty leaves use a canonical selector within an exact,
  content-addressed source representation. The current structural
  locator cannot address all of them uniquely and will be replaced.
- Source location and provenance use a standards-shaped local profile:
  W3C target and selector semantics, IIIF image-region and derived-OCR
  semantics, PROV activity and agent semantics, and CID-style typed
  digests. External standards services and serializers are not runtime
  dependencies.
- The representation-plus-primary-selector tuple is canonical leaf
  identity. Its types and constraints come from the reusable LinkML
  rule-language pack through the Malleus-rooted generator. Handwritten
  C++ consumes generated types and never owns the semantic contract.
- Rule entities are scoped to an exact ingestion edition, not to one
  arbitrary source document or only a mutable layer name. The edition
  identity is source layer plus SHA-256 of a canonical, sorted, non-empty
  source-representation manifest. Source targets remain scoped to exact
  representations.
- The canonical manifest includes logical path, media type, digest algorithm,
  source digest, and byte length. It excludes source-system revision, which
  remains provenance. Source order cannot change identity; a path or byte
  change does.
- Corpus membership and byte access are separate engine inputs. The application
  owns an explicit `SourceCorpusDeclaration`; a replaceable `SourceAccess`
  supplies exact bytes. The engine alone derives digest, length, manifest, and
  edition identity. A persisted manifest is generated output, never a private
  Logovger authority.
- Revision provenance is a separate append-only `SourceRevisionObservation`
  linked through typed `identity_context` to one exact representation. A
  representation has no revision property and may be observed at several
  source revisions without changing content or edition identity. Git commit
  remains only on the legacy document context until that path is deleted.
- One source unit may produce zero, one, or many atomic claims.
- One claim may cite more than one source unit when its meaning crosses
  a source boundary.
- Zero claims requires an explicit no-rule-content judgement. Missing
  claims alone never imply it.
- Claim dispositions, resolution dependencies, and invalidation links
  live on claims, not on source-coverage rows.
- Dispositions are append-only decisions, not mutable claim fields.
  `SourceCoverage` and `IngestionClaim` are enduring records;
  `CoverageDecision` and `ClaimDecision` reuse `ArbiterDecision` and
  derive current state from the latest contiguous sequence while
  preserving every earlier judgement.
- Seeds remain the verified load format. The ledger does not replace
  seed verification or weaken byte-exact citations.
- A standalone reusable ingestion module is promoted only after a second
  consumer exists. The mechanism remains generic engine work now; Logovger is
  its first integration consumer, not its definition.

The built schema names are `SourceCoverage`, `IngestionClaim`,
`CoverageDecision`, and `ClaimDecision`. Exact leaf identity is a
`SourceRepresentationContext` plus typed primary selector. Claim
dispositions are `MATERIALIZED`, `PARTIAL`, `RAISED`, `DUPLICATE`, and
`CONTRADICTORY`; incomplete claims distinguish `ONTOLOGY_GAP` from
`RULE_LANGUAGE_GAP`. Provisional claims remain R12/Malleus work.
`IngestionEditionContext` is the selected rule identity scope, with
`EDITION_INCLUDES_REPRESENTATION` owning its exact manifest membership.

## Completed

- [x] Measure current ingestion against five real source journeys.
- [x] Record R7, whole-source coverage with addressed absence.
- [x] Decide whole-book coverage, including explicit no-rule-content
  judgements.
- [x] Record R11 cumulative interpretation and dependency capture.
- [x] Record R12 typed accommodation and reinforcement limits.
- [x] Audit the five local R11 enforcement gaps.
- [x] Keep raw `verify_seed` prerequisites optional because provisional
  representation belongs upstream, not in a local signature patch.
- [x] Add ordered prerequisites to the verifier CLI.
- [x] Give cumulative verify-then-load one engine owner,
  `verify_and_load_seed_sequence`.
- [x] Route the windowed game, headless runner, and chargen test through
  one Logovger seed loader.
- [x] Prove a dependent seed passes with its prerequisite and fails
  without it.
- [x] Prove a reversed generic pair and reversed production manifest
  fail before rule-seed mutation.
- [x] Decide option 4, two-level coverage and atomic claims.
- [x] Choose atomic leaf coverage over block coverage, with block-level
  techniques reserved for derived grouping if volume requires them.
- [x] Research source location and provenance against W3C Web
  Annotation, PROV-O, IIIF, IPFS CID, EPUB CFI, and ALTO. No single
  standard owns identity, fragment selection, OCR evidence, provenance,
  and coverage closure. See `SOURCE_LOCATION_PROVENANCE_SPIKE.md`.
- [x] Reconcile the decision across the protocol, cumulative-ingestion
  rationale, ingestion review, and RPG decision ledger.
- [x] Decide rule identity scope: exact ingestion edition, keyed by source
  layer plus canonical source-manifest digest.
- [x] Define the edition and typed representation membership in the reusable
  LinkML rule-language pack.
- [x] Prove manifest identity ignores relation order and source revision,
  changes for source path or bytes, and refuses empty, duplicate, cross-layer,
  incomplete, or drifted declarations.
- [x] Correct the ownership boundary: explicit application corpus declaration
  plus replaceable source access, with all content identity derived by the
  engine.
- [x] Generalize new provenance from Git-specific `source_commit` to
  source-system `source_revision`.
- [x] Separate revision provenance from content identity through one immutable
  typed observation per representation/revision pair.

Verification for the R11 implementation baseline:

- complete core CTest profile: 92 of 92 effective passes;
- the sandbox-only `test_run_recorder` home-directory denial passed when
  rerun with normal filesystem access;
- changed windowed-app source compiled directly;
- full windowed target remained blocked by the missing Apple Metal
  Toolchain, before application compilation;
- `git diff --check`: clean.

Verification for append-only decision history, implementation commit
`b5f5990`:

- schema contract first ran red at 83 passes / 5 failures because the
  ledger records, decision events, typed values, and relations did not
  exist;
- append-only write-path tests then ran red at 89 passes / 2 failures
  because the facet was declared but rewrite and deletion were not yet
  refused;
- the reconciliation fixture first failed to compile because the
  ledger API did not exist;
- the schema test exposed a generator-class defect: Event subclasses
  had direct parents in generated registries but no ancestor sets, so
  runtime subtype checks contradicted LinkML. The generator now emits
  complete Event ancestry and its unit test prevents recurrence;
- final focused results: `test_rulebook_pack` 91/0,
  `test_ingestion_ledger` 10/0, generator plus schema audits 14/0 with
  15 schemas, 367 classes, 52 enums, and zero findings;
- complete registered headless profile: effective 94/94. The first run
  produced the known sandbox-only `test_run_recorder` home-directory
  denial, which passed with normal filesystem access, and the
  production-world declaration gate correctly found the four new
  concrete types had no production seed instances. Their schema now
  states the exact temporary reason; `test_chargen` passed on rerun.
  The next real-section slice removes those declarations by creating
  production instances;
- `git diff --check`: clean.

Verification for ingestion-edition identity, implementation commit
`ac53d17`:

- the LinkML contract first ran red at 91 passes / 4 failures because the
  edition context, manifest slots, closed format, and membership relation did
  not exist;
- the runtime test target then failed to configure because
  `src/text/source_manifest.cpp` did not exist;
- the first runtime execution exposed one error in the test oracle: the pinned
  length prefix called `a.md` five bytes rather than four. The exact-format
  assertion caught it; correcting the expected literal produced 14/0 without
  changing the implementation;
- final focused results: `test_rulebook_pack` 95/0, `test_source_target` 8/0,
  `test_source_manifest` 14/0, and `test_ingestion_ledger` 10/0;
- generator tests: 14/0. Schema audit: 15 schemas, 369 classes, 53 enums,
  and zero findings;
- complete registered headless profile: effective 95/95. CTest first reported
  94/95 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the unchanged test passed 12/0 with normal
  filesystem access;
- `git diff --check`: clean.

This commit proves edition construction and validation only. Existing rule
entities still use document identity, and the old locator path is still live.
Those two facts are the next atomic migration, not hidden compatibility.

Verification for the generic source-corpus boundary, implementation commit
`c4e1ff4`:

- the new runtime target first failed to compile because
  `logosphere/text/source_corpus.h` did not exist;
- the schema contract then ran red at 94 passes / 1 failure because the
  generic representation still exposed Git-specific `source_commit` instead
  of required source-system `source_revision`;
- the declaration type exposes membership, media type, layer, and revision,
  but no digest or byte length. Two different `SourceAccess`
  implementations and reversed declaration order produce the same identity;
- focused results: `test_source_corpus` 13/0, `test_source_manifest` 14/0,
  `test_source_target` 8/0, `test_ingestion_ledger` 10/0, and
  `test_rulebook_pack` 95/0;
- generator and schema contract tests: 14/0. Schema audit: 15 schemas, 369
  classes, 53 enums, and zero findings;
- complete registered headless profile: effective 96/96. CTest first reported
  95/96 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the unchanged test passed 12/0 with normal
  filesystem access;
- `git diff --check`: clean.

This phase establishes the generic declaration/access/identity boundary. It
does not yet write the materialized corpus into the KG and does not migrate the
legacy Logovger loader. Those are explicit subsequent slices below.

Verification for separate revision provenance and atomic KG materialization,
implementation commit `da7ba36`:

- the LinkML contract first ran red at 94 passes / 4 failures because revision
  still lived on `SourceRepresentationContext` and no separate typed
  provenance record existed;
- the runtime target then failed to compile because
  `materialize_source_corpus_into_kg` did not exist;
- an initial grouped `SourceRevisionContext` implementation passed the first
  focused tests, but the incremental-observation regression ran red at 24/1:
  a sealed revision group could not record another independent representation
  without mutating earlier provenance. The grouped context and
  `REVISION_INCLUDES_REPRESENTATION` relation were deleted and replaced by one
  append-only `SourceRevisionObservation` per representation/revision pair;
- a compile-time contract then ran red because
  `SourceRepresentationDeclaration` remained default-constructible and could
  silently default media to UTF-8. Construction now requires path, typed media,
  and revision explicitly;
- the materializer reuses exact layer, representation, edition, and observation
  identities; adds a new observation when identical content appears at a new
  revision; rejects conflicting bytes for one layer/path/revision before KG
  mutation; and commits every missing entity and edition relation through one
  validated atomic KG-op batch;
- final focused results: `test_source_corpus` 25/0,
  `test_source_manifest` 13/0, `test_source_target` 8/0,
  `test_ingestion_ledger` 10/0, and `test_rulebook_pack` 98/0;
- generator and schema contract tests: 14/0. Schema audit: 15 schemas, 370
  classes, 53 enums, and zero findings;
- complete registered headless profile: effective 96/96. CTest first reported
  95/96 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the unchanged test passed 12/0 with normal
  filesystem access;
- `git diff --check`: clean.

The generic engine can now persist a declared corpus and its provenance.

**Scope ruling, 2026-08-17 18:36 UTC.** Owner selected option 2: move rule
identity to the exact edition now, then migrate evidence section by section.
The measured production surface is six seeds, two representations, one source
revision, 1,393 cross-seed qualified references, and 3,082 structural
citations. Identity and evidence are separate graph concerns. Keeping rules
document-scoped until every locator converts would preserve known-wrong
identity and make the 3,082-record evidence conversion a prerequisite for an
independent invariant.

The production loader now derives one edition from application-declared corpus
membership and exact bytes, validates every seed's layer, representation, and
revision observation against it, and injects it as rule identity. All 1,393
production references use the edition context. Source-document contexts and
loose locator fields remain only for existing citation verification. This is a
named transition, not a second evidence grammar: no `SourceTarget` consumer
falls back to a locator, and the locator is deleted when the last evidence
section moves.

Verification for production edition-scoped rule identity, implementation
commit `44ee08d`:

- `test_logovger_rule_seed_identity` first ran red at 2/2 because production
  references still named document contexts and no ingestion edition was
  materialized;
- the engine unit test then failed to compile because
  `load_seed_in_edition` did not exist, and the application test failed to
  compile because `declare_rule_source_corpus` did not exist;
- after the edition-aware loader existed but before the seed migration,
  production loading failed while resolving the old document-scoped dice
  reference in the shared tables seed;
- a late report-contract regression ran red at 270/1 because an empty seed
  falsely reported the supplied edition as materialized. The loader now
  reports an identity context only after loading an addressable rule;
- production now declares six seeds, two representations, one source revision,
  and one exact ingestion edition. All 1,393 cross-seed qualified references
  use
  `ingestion-edition:v1:7:cepheus:sha256:76ecc50c30ab15fccef9e21ff22db79883d916dffe4006ee7c6c1bd51946dd85`;
- focused results: `test_seed_verifier` 271/0,
  `test_logovger_rule_seed_identity` 9/0, and
  `test_logovger_table_results` 91/0. The source corpus, manifest, target,
  ledger, and rulebook targets also pass;
- generator and schema contract tests: 14/0, plus 4/0 for the Logovger
  generators. Schema audit: 15 schemas, 370 classes, 53 enums, and zero
  findings. Regenerating the production seeds reproduced the checked-in
  output;
- complete registered headless profile: effective 97/97. CTest first reported
  96/97 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the rebuilt, unchanged test passed 12/0 with
  normal filesystem access. `test_chargen` passed;
- `git diff --check`: clean.

The remaining 3,082 structural citations and document-scoped citation origins
are intentionally unchanged. They move section by section to exact
representation-scoped `SourceTarget` evidence. There is no compatibility
fallback.

## First production evidence slice, 2026-08-17 19:16 UTC

`Injury Crisis` is the first real section migrated. Four exact byte-range
targets enumerate its heading and three sentences. The heading has one
`NO_RULE_CONTENT` decision. The sentences support six atomic claims: five are
`RAISED` for `RULE_LANGUAGE_GAP`; the medical-cost claim is `PARTIAL` because
it materializes the existing Credits `Currency` entity but cannot yet express
the 1D6 times 10,000 calculation. The entity's old `source_section` and
`source_quote` fields are deleted. Production now has 3,081 legacy structural
citations remaining.

TDD exposed two general defects before the production seed moved:

- the edition-aware loader gave selectors and targets edition identity, while
  source evidence must be representation-scoped. The generic loader now owns
  that distinction and derives byte-range target identity from the canonical
  selector key;
- an old numeric verifier test used `-5` while the pinned Markdown bytes contain
  `\-5`. It therefore failed on verbatim matching before reaching the numeric
  assertion it claimed to test. The fixture now copies the exact bytes and
  proves the wrong numeric value fails through the exact-evidence path.

The verifier now rejects mixed evidence grammars, exact rules without a
materializing claim, unresolved exact targets, and exact selected bytes that
do not support either the stored quote or numeric value. Legacy verification
remains intact for unmigrated rules, but it is not a fallback from exact
evidence.

Verification for the first production evidence slice, implementation commit
`93da42f`:

- the generic exact-evidence fixture first ran red at 280 passes / 6 failures:
  the target lacked loader-owned representation identity, exact byte and quote
  evidence was not verified, mixed locator fields were accepted, and a rule
  could omit its materializing claim;
- after correcting representation ownership, the same target ran 287/5. The
  remaining failures belonged to verifier enforcement, not seed loading;
- the production identity target then ran red at 11/5 before the real seed
  contained targets, coverage, claims, decisions, or a locator-free Credits
  rule;
- after the production records loaded, a schema guard ran red at 16/1 because
  the four ledger classes still claimed `no-instance-declared`. Regeneration
  removed that obsolete facet from every composed registry;
- final focused results: `test_seed_verifier` 292/0,
  `test_logovger_rule_seed_identity` 17/0, and
  `test_logovger_table_results` 91/0;
- generator and schema contract tests: 14/0, plus 4/0 for the Logovger
  generators. Schema audit: 15 schemas, 370 classes, 53 enums, zero findings;
- regenerating the production seeds reproduced the generated files and left
  the hand-authored evidence migration intact;
- a direct table-test run before its executable was rebuilt reported 54/22
  against the previous loader. Rebuilding that target produced 91/0 without a
  source change. Authoritative regression results therefore follow a complete
  build, not an arbitrary existing binary;
- complete registered headless profile: effective 97/97. CTest first reported
  96/97 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the unchanged test passed 12/0 with normal
  filesystem access. `test_chargen` passed;
- the six production seeds contain 3,081 remaining entities with structural
  locator fields, measured after regeneration;
- `git diff --check`: clean.

## Second production evidence slice, 2026-08-17 19:46 UTC

`Medical Care` adds one dependency-ordered, hand-authored production seed.
Although the section appears before career definitions in the source, its
payment table names every Career, so the seed follows `cepheus_careers.json`.
Its 22 leaves are one heading, five sentences, four header cells, and twelve
data cells. They produce 15 claims: fourteen `RAISED` decisions and one
`PARTIAL` decision that materializes the fixed 5,000-credit restoration cost
as a `RuleConstant`. Each of the nine percentage claims cites its career-group
cell, threshold header, and result cell, then resolves against 2D6 and the
exact prior Career entities. The seed contains 83 typed resolution links.

The first integration attempt exposed a general text-address defect. Its
positions were counted as Unicode characters, not UTF-8 bytes. Multibyte
characters before the section shifted every range by 20 bytes. The cost
constant happened to fail numeric verification; the raised claims could have
accepted valid but unintended text because they had no value to contradict it.
The generic verifier now requires a matching `TextQuoteSelector` for every
`UTF8_TEXT` ingestion-ledger target. The earlier `Injury Crisis` targets were
upgraded to the same rule. Non-text `SourceTarget` remains generic and does not
invent a quote contract.

Verification for implementation commit `58a9f83`:

- the production contract first ran red at 14 passes / 7 failures before the
  Medical Care seed existed;
- the first integrated seed ran 12/9 after a complete rebuild. Verification
  exposed the character-versus-byte drift and also corrected seed invariants
  that had been written as cumulative world counts rather than per-seed
  counts;
- the generic missing-quote regression ran red at 293/1 before the verifier
  enforced text convergence, then passed at 294/0;
- final focused results: `test_logovger_rule_seed_identity` 22/0 and
  `test_logovger_table_results` 91/0;
- the complete production ledger has 26 coverage records, 21 claims, three
  no-rule-content leaves, two partial claims, and nineteen raised claims;
- production now has seven seeds and 1,476 qualified cross-seed references.
  The 3,081 legacy structural citations are unchanged because Medical Care had
  no previous seeded rule entities; its new constant starts on exact evidence;
- generator and schema contract tests: 14/0, plus 4/0 for the Logovger
  generators. Schema audit: 15 schemas, 370 classes, 53 enums, zero findings;
- regenerating the production seeds reproduced all generated files and left
  the hand-authored Medical Care seed intact;
- complete registered headless profile: effective 97/97. CTest first reported
  96/97 because `test_run_recorder` could not create its user-session directory
  inside the filesystem sandbox; the unchanged test passed 12/0 with normal
  filesystem access. `test_chargen` passed;
- `git diff --check`: clean.

## Third production evidence slice, 2026-08-17 20:08 UTC

`Medical Debt` adds one dependency-ordered, hand-authored production seed. It
follows the procedure seed because its interpretation uses three prior typed
concepts owned by different seeds: the medical-care restoration-cost
`RuleConstant`, the Benefits `SubjectLookupTable`, and the final
`ProcedureStep`. The new seed adds seven qualified cross-seed references.

The section has two exact leaves: a no-rule heading and one compound sentence.
The sentence supports three independent claims without duplicating its
coverage record. Medical-care debt payment and payment precedence are raised
as `RULE_LANGUAGE_GAP`; the current language cannot debit awarded Benefits or
order that debit before every other finishing-touch action. Anagathic debt is
raised as `ONTOLOGY_GAP`; the current graph contains no anagathic-drug or
corresponding debt concept. The slice creates no executable rule entity and
does not hide any missing mechanism behind prose or a default.

Verification for implementation commit `19a3bab`:

- the production contract ran red at 18 passes / 5 failures before the seed
  existed;
- after integration, the contract ran 25/1 because the test tried to find a
  `ClaimDecision` event by addressable entity key. The corrected assertion
  follows the event's typed `decision_subject` property, then passed 26/0;
- the complete production ledger has 28 coverage records and 24 claims: four
  no-rule-content leaves, two partial claims, and 22 raised claims;
- the compound sentence has one coverage record supporting three claims. The
  anagathic claim is mechanically checked as a raised ontology gap;
- generic verifier and table semantics remain 294/0 and 91/0;
- production now has eight seeds and 1,483 qualified cross-seed references.
  The 3,081 legacy structural citations remain unchanged because this slice
  materializes no rule entity and replaces no legacy citation;
- generator and schema contract tests pass 14/0, plus 4/0 for the Logovger
  generators. Schema audit: 15 schemas, 370 classes, 53 enums, zero findings;
- regenerating all four generated production seeds reproduced them exactly
  and left the hand-authored Medical Debt seed intact;
- complete registered headless profile: effective 97/97. CTest reported 96/97
  inside the filesystem sandbox because `test_run_recorder` could not create
  its user-session directory; the unchanged test passed 12/0 with normal
  filesystem access. `test_chargen` passed in the registered profile;
- `git diff --check`: clean.

## Fourth production evidence slice, 2026-08-17 21:08 UTC

`Aging` migrates the heading-based block ending before `Aging Crisis`. Its 22
atomic leaves are one heading, three prose sentences, two table header cells,
and sixteen data cells. They support 12 claims: nine `MATERIALIZED`, two
`RAISED/RULE_LANGUAGE_GAP`, and one `PARTIAL/SOURCE_GAP`.

The start-age constant moves from `extract_careers.py` into
`extract_shared_tables.py`. The latter already owned the table and now owns
the section's exact evidence too. All 30 Aging rules lose their structural
locators and gain materializing claims. The `roll_aging` procedure step remains
legacy because its citation belongs to the separate character-creation
checklist leaf.

The table's bottom row is printed as plain `-6`, but the existing runtime row
is open below that value. The graph keeps the executable reading and records
the source omission as a partial `SOURCE_GAP`. It does not rewrite the source
claim as complete.

TDD exposed and closed two generic verifier defects before integration:

- exact evidence from several leaves was joined in support order, while band
  verification assumed the first fragment was the row key;
- `read_row_band` honored the open top flag but ignored
  `roll_min_unbounded`, despite the table runner already honoring it.

The generic verifier now scans all evidence fragments and requires one unique
matching band. A one-sided expansion from a finite printed band is accepted
only when the rule's current materializing decision is
`PARTIAL/SOURCE_GAP`. A negative regression proves `RULE_LANGUAGE_GAP` cannot
authorize the same widening. The generic changes are commits `3788fd7` and
`c5886f5`; the Aging migration is `dbda4c8`.

Verification:

- the new generic verifier fixture first ran red at 296 passes / 2 failures,
  rejecting both the later evidence fragment and the open-bottom reading;
- `test_seed_verifier` then passed 298/0;
- `test_logovger_rule_seed_identity` passed 29/0 and
  `test_logovger_table_results` passed;
- ingestion-ledger, rulebook-pack, seed-verifier, production table, and
  production identity CTest targets passed 5/5;
- schema contracts passed with 15 schemas, 370 classes, 53 enums, and zero
  findings. Logovger extractor contracts passed 5/0;
- regenerating all generated seeds reproduced the checked-in outputs;
- complete registered headless profile: effective 97/97. CTest reported 96/97
  inside the filesystem sandbox because `test_run_recorder` could not create
  its user-session directory; the unchanged binary passed 12/0 with normal
  filesystem access. The slow `test_chargen` target passed in the registered
  profile;
- production totals are 50 coverage records and 36 claims: five no-rule
  leaves, three partial claims, 24 raised claims, and nine materialized claims;
- the six legacy-bearing production seeds now contain 3,051 entities with
  structural locator fields;
- `git diff --check`: clean.

## Fifth production evidence slice, 2026-08-17 21:38 UTC

`Aging Crisis` adds four exact leaves: one heading and three sentences. The
trigger is aging-specific, while the remaining five semantic consequences
repeat `Injury Crisis`. The selected abstraction keeps both source occurrences
but generalizes shared meaning.

Five new canonical claims each cite the matching Injury Crisis and Aging
Crisis coverage records. The five old occurrence-specific Injury Crisis claims
remain addressable and retain their sequence-zero decisions. Each receives a
sequence-one `SUPERSEDED` decision pointing to its generalized replacement.
`DUPLICATE` was not reused backwards.

That integration exposed a missing generic lifecycle state. The rulebook
LinkML schema now declares `SUPERSEDED`; generated projections carry it; and
reconciliation requires a replacement claim while forbidding retained gaps or
materialized results. Positive and negative ledger tests cover the contract.
The generic phase is commit `d47e7f6`.

The generalized cost claim takes over the Credits materialization from the old
claim. The generalized restoration claim materializes `crisis_restore_value`,
which moves from the generated career seed to the earlier hand-authored crisis
seed. A separate partial claim in the procedure seed materializes
`aging_crisis_unpaid`; it states that the route covers the death projection but
not the complete payment gate. Both rules lose their structural locators. The
production phase is commit `f8f1ab6`.

Verification before the documentation phase:

- the generic tests first failed through the enum property gate because
  `SUPERSEDED` did not exist; final ingestion reconciliation passed 14/0 and
  rulebook schema passed 100/0;
- the production contract first passed 25 assertions and failed eight before
  Aging Crisis coverage, generalized claims, supersession histories, moved
  materializations, and current totals existed; final production identity
  passed 33/0;
- ingestion, rulebook, seed-verifier, production table, and production
  identity CTest targets passed 5/5;
- all six Logovger extractor and ownership tests passed. Schema contracts read
  15 schemas, 370 classes, and 53 enums with zero findings;
- all generated seeds reproduced the checked-in output;
- the full `test_chargen` behavior target passed after 156 seconds;
- complete registered headless profile: effective 97/97. The filesystem
  sandbox prevents `test_run_recorder` from creating its user-session
  directory; the unchanged binary passed 12/0 with normal filesystem access;
- production now has 54 coverage records, 43 claims, and 48 claim decisions.
  Current state is six no-rule leaves, five partial claims, 24 raised claims,
  nine materialized claims, and five superseded claims;
- the six legacy-bearing production seeds contain 3,049 entities with
  structural locator fields;
- `git diff --check`: clean.

## Cross-platform exact-byte guard, 2026-08-17 23:46 UTC

PR #138 exposed a repository-boundary defect on Windows. Git checkout
converted the vendored Cepheus Markdown from LF to CRLF because the repository
had no `.gitattributes`. `RuleSourceAccess` then did the correct thing and read
the checked-out file in binary mode. Its bytes no longer matched the
LF-authored `ByteRangeSelector` offsets and `TextQuoteSelector` values, so seed
verification rejected the exact targets. The later table, character creation,
and rule-identity failures were cascades from that refusal.

The fix does not normalize source content in the engine. It declares
`text eol=lf` for `corpora/**/*.md`, preserving the Git
object bytes on every checkout. A new source-checkout contract enumerates all
32 vendored Markdown files and verifies both Git's effective `eol` attribute
and the raw absence of CRLF bytes.

TDD evidence:

- red: all 32 source-file subtests failed because `eol` was unspecified;
- green: the focused checkout contract passed 1/0 and all seven Logovger
  Python tool suites passed;
- `git check-attr eol` reports `lf` for both production corpus members;
- PR #138 CI rerun: `headless-windows`, `headless-linux`,
  `physics-linux`, `ontology-generation`, and `merge-policy` passed;
- the separate DCO failure is the known repository-policy defect tracked by
  PR #137. The final fix commit was signed. The full branch was not rewritten;
- `full-macos` failed only in `test_humanoid_terrain_scenarios`: 118 passed,
  three failed, and 17 were known-red. Current `main` run 31962521058 had the
  exact same three `litter underfoot` failures and totals. This branch changed
  no physics;
- the owner approved an audited admin squash merge. The PR comment records
  both overrides, and squash commit `9db62b8` carries a `Signed-off-by`
  trailer.

## PR #138 integration closure, 2026-08-18 00:07 UTC

The first five production evidence slices and the generic ingestion-ledger
mechanism are now on `main`. The Windows gate proves that repository checkout
preserves the exact bytes addressed by source targets. Linux headless also
passed its complete suite, 60-life run, installation, and external-consumer
check. The unrelated macOS physics failure and the known DCO-policy defect are
recorded on the PR before the owner-authorized admin merge.

The next completion defect was narrower than adding more claims: production
verification passed `world.findByType("SourceTarget")` directly to ledger
reconciliation. That proved every declared target had coverage, but it could
not detect source bytes that were never declared. The exact-partition phase
below closes that defect without making an independently derived Markdown
parse the semantic authority.

## Exact source partition gate, 2026-08-18 00:33 UTC

Owner selected option 1: one explicit exact-byte partition assertion per
representation. `CompleteSourcePartition` activates the gate. Its exact UTF-8
representation must then be covered once, with no gaps or overlaps, by
`SourceTarget` primary byte ranges plus typed `SourceExclusion` ranges.
Exclusions are only `SYNTAX` or `LAYOUT`; unclassified content remains an
opaque target. Without the assertion, partial migrations remain valid.

TDD evidence and integration findings:

- initial ledger red: 16 passes and 3 failures because a one-byte gap, an
  overlap, and a range past the representation all reconciled;
- first shipped-verifier red: 298 passes and 2 failures because the new
  partition records were incorrectly marked loader-owned;
- second integration red exposed the existing loader's type-name scoping.
  Moving scoping to the LinkML `identity_context` range then exposed that the
  runtime registry chose an alphabetically earlier base property instead of
  the nearest ancestor's refinement;
- the registry now resolves inherited property refinements by specificity,
  and a generic ontology test prevents that class of regression;
- the seed loader refuses representation-scoped content without an exact
  ingestion edition before any mutation. It derives scope from the schema,
  with no list of source type names;
- a later red proved that a completeness assertion with no semantic targets
  skipped reconciliation. The shipped verifier now gates on the assertion
  itself, so an all-exclusion partition is still checked;
- focused `test_ontology_extension`, `test_rulebook_pack`,
  `test_ingestion_ledger`, and `test_seed_verifier` pass. Negative coverage
  includes gaps, overlaps, out-of-bounds ranges, duplicate assertions,
  cross-representation selectors and target identities, empty exclusions,
  absent byte length, no-target assertions, and one omitted byte through the
  shipped verifier;
- schema and generator audits pass 14/0, regeneration is byte-stable, and the
  complete registered headless suite passes 97/97. The sandboxed run first
  passed 96 tests and denied `test_run_recorder` access to its normal telemetry
  directory; that unchanged test passed 1/1 with the required filesystem
  access.

The next production step is to classify the remaining bytes of one complete
source representation into semantic or opaque targets and syntax/layout
exclusions, then add its single completeness assertion. If that integration
finds a general representation defect, pause the production migration, repair
the engine contract under a red test, and resume.

## PR #139 exact-fixture checkout guard, 2026-08-18 01:05 UTC

The first Windows run passed DCO, merge policy, ontology generation, and the
Linux physics lane, but `test_seed_verifier` passed 303 cases and failed the two
new complete-partition fixture cases. Windows had converted
`tests/fixtures/source_partition/partition.md` from LF to CRLF because the new
fixture was outside the repository's source checkout policy. The verifier then
correctly rejected the authored byte ranges and quote selector against the
different checked-out bytes.

The repair extends the existing source-integrity boundary instead of changing
the verifier. `.gitattributes` now fixes the exact-byte fixture path to LF. The
source-checkout contract enumerates production source Markdown and
repository-backed exact-byte fixtures together, currently 33 files, and checks
both effective `eol=lf` and raw absence of CRLF.

TDD and integration state:

- red: the generalized checkout contract reported `eol: unspecified` for the
  partition fixture before the attribute was added;
- local green: the checkout contract passes 1/0, `git check-attr eol` reports
  `lf` for the fixture, and the shipped seed verifier passes 305/0;
- the fresh Windows run is a merge gate. A failure pauses integration and
  returns to the general source-integrity contract;
- the initial macOS failure is the unchanged three-case
  `test_humanoid_terrain_scenarios` baseline already present on `main`. This
  branch changes no physics files.

## First complete production representation, 2026-08-18 02:51 UTC

`book1/skills.md` is the first production source representation to assert a
complete exact partition. The existing `extract_skills.py` remains its sole
generated-seed owner. It now refuses any source whose SHA-256 differs from the
reviewed 31,878-byte chapter, mechanically addresses atomic headings, prose
sentences, and lexical table cells, and emits the reviewed semantic decisions
into the same production seed. It is source-specific extraction, not a general
Markdown-to-rules authority.

Current representation totals:

- 437 non-empty `SourceTarget` leaves with converging UTF-8 byte ranges and
  exact quotes;
- 844 non-empty `SourceExclusion` records, closed to `SYNTAX` or `LAYOUT`;
- one `CompleteSourcePartition`, so every one of the 31,878 bytes is gated;
- 437 coverage decisions: 405 `CLAIMS_PRESENT`, 32 `NO_RULE_CONTENT`;
- 365 claims: 68 materialized skill-definition claims, 68 duplicate
  available-skill-list claims, and 229 raised claims;
- the listed but locally undefined `Airship` skill remains a raised
  `SOURCE_GAP`. `Perception` and `Prospecting` are not owned by this
  representation because they occur only in `character-creation.md`;
- the 68 chapter-defined `Skill` entities no longer carry the legacy locator
  fields. Production now has 491 coverage records, 408 claims, 413 claim
  decisions, and 2,981 legacy structural citations.

TDD and integration findings:

- initial extractor red: four contract failures for absent completeness,
  vacuous empty coverage, surviving skill locators, and acceptance of changed
  source bytes;
- shipped-verifier red: the real production seed had no completeness
  assertion or semantic targets. It now passes the engine's schema, ledger,
  exact-byte, identity, value, semantic, and invariant checks;
- regeneration exposed that the career loader treated every vocabulary
  `create_entity` as a `Skill`. A mixed-operation regression test now requires
  exact type filtering, and required skill name or alias data fails loudly;
- the production identity gate was scoped too broadly: representation-owned
  source-partition records were incorrectly tested as edition-owned rules. It
  now derives that distinction from the `source-partition` ontology facet and
  resolves each target against its own representation;
- all 14 extractor contract tests pass. The effective registered headless
  result is 97/97: the initial run passed 95, the identity totals were updated
  and passed, and the unchanged telemetry test passed with its normal session
  directory. A real headless seed-28 life also completed correctly.

The volume exposed an integration-cost blocker before merge. One real
headless process took 6.22 seconds, and `test_chargen` took 313.94 seconds.
The root cause was generic sequence verification: it rematerialized the corpus
for every seed and replayed the full prerequisite prefix before checking each
later seed.

The owner selected the foundational engine fix on 2026-08-18. Sequence
verification now owns one scratch verified-prefix world. It materializes an
edition corpus there once, then leaves each verified seed loaded for the next
dependency. The caller world still receives each seed through the existing
atomic loader only after that seed passes every check. Standalone verification
still creates a fresh world, and a failed later seed leaves earlier caller
state intact. The TDD guardrail requires a two-seed edition sequence to read
the corpus exactly twice, once for the caller world and once for the scratch
prefix; the old implementation read it three times. The focused verifier is
320/320, the production identity target passes, the deterministic headless
life fell to 2.70 seconds, and `test_chargen` remains 253/253 at 134.42
seconds. Both measured paths are about 57 percent faster.

Grouped and filtered ledger projections stay on the later roadmap if review
or query volume becomes unmanageable. They are not needed to make this
production partition viable and cannot replace atomic coverage.

## Career Tables production evidence slice, 2026-08-18 04:30 UTC

The complete `Career Tables` source section now uses exact claims across its
two generated owners. The reviewed `character-creation.md` bytes are pinned by
SHA-256. Each migrated cell claim converges on the exact table-title, column,
row-key, and value cells. Repeated context cells create one coverage record;
the later career-tables seed references coverage already owned by the careers
seed instead of duplicating representation-scoped identity.

The migration replaces 1,695 legacy-cited materializations: 217 in
`cepheus_careers.json` and 1,478 in
`cepheus_book1_career_tables.json`. The two source defects move to their real
evidence owners. The careers seed owns the first `Prospecting` occurrence as a
`PARTIAL/SOURCE_GAP` Skill claim. The career-tables seed owns `Perception` the
same way and records the later `Prospecting` occurrence as a distinct
`DUPLICATE` claim linked to the first. The skills representation no longer
creates either term.

TDD and integration findings:

- initial extractor contracts failed four ways: no exact claims existed, all
  1,695 entities retained loose locators, the two undefined Skills belonged to
  the wrong representation, and changed source bytes were accepted;
- the first engine integration rejected name-uniqueness invariants applied to
  nameless ledger nodes. Generated seeds now keep count invariants for every
  type but exclude ledger records from name invariants, with a regression
  contract;
- the classification audit had coupled itself to `source_table` and
  `source_quote`. It now follows `CLAIM_MATERIALIZES` and the exact supporting
  value coverage. Python and C++ gates compare that projection with the
  shipped independent audit;
- the production identity gate now asserts 2,057 exact targets, 1,554 claims,
  and 1,559 append-only claim decisions. All targets resolve against their
  own pinned representation;
- current decisions are 38 `NO_RULE_CONTENT`, 2,019 `CLAIMS_PRESENT`, seven
  `PARTIAL`, 253 `RAISED`, 1,220 `MATERIALIZED`, five `SUPERSEDED`, and 69
  `DUPLICATE`;
- 1,286 rule entities still use legacy structural citations outside this
  source section. The final locator path remains transitional until those
  separately owned sections migrate.

Verification:

- all 20 Logovger extractor and ownership contracts pass;
- `test_seed_verifier` passes 320/320;
- deterministic regeneration reproduces all four generated seeds;
- the complete registered headless profile is effectively 97/97. The
  filesystem sandbox denied the unchanged recorder its normal session
  directory; the same binary passed 12/0 with normal access;
- `test_chargen` passes 253/253, including the new assertion that a Career's
  claim resolves its exact table-title, column, row-key, and value supports;
- deterministic headless seed 28 completes, but now takes 8.99 seconds versus
  the prior 2.70-second baseline. Standalone `test_chargen` takes about 336
  seconds versus 134.42 seconds. Correctness is closed; the larger exact
  ledger has exposed a new production-verification scaling cost.

## Immediate Phase A, minimum honest ledger

Do not start with model pairing, dashboards, learning layers, a reusable
module, or R12 implementation. The immediate blocker is simpler: the
repository still has no durable place to record that every source unit
was visited and what claims came from it.

Follow this TDD order:

- [x] Decide source-unit grain: atomic headings, prose sentences, table
  cells, list-item sentences, and opaque leaves, without a semantic
  pre-filter.
- [x] Decide the standards adoption boundary: standards-shaped
  semantics implemented through a local replacement for the current
  locator, with no external standards runtime or legacy fallback.
- [x] Decide the exact leaf-identity grammar: a typed source
  representation plus a typed primary selector, declared in LinkML.
  Supporting quote and structural selectors do not own identity. It
  must address
  duplicate sentences, repeated headings, empty cells, duplicate or
  blank row keys, and repeated table labels at one pinned source commit.
- [x] Present and decide the minimum closed claim dispositions. Keep
  ontology-gap and rule-language-gap reasons distinct if both enter the
  first slice.
- [x] Write a fixture containing a zero-claim unit, a one-claim unit,
  and a compound unit with multiple claims. Include one claim supported
  by multiple units if the selected address grammar can express it.
- [x] Add red schema or parser tests for coverage-to-claim linkage and
  exact source identity before adding production types.
- [x] Implement only the minimum ledger representation required by those
  tests. No compatibility fallback and no second ingestion grammar.
- [x] Add a reconciliation gate: every enumerated unit has exactly one
  coverage judgement; zero claims requires explicit no-rule-content;
  every claim cites existing coverage; totals close.
- [x] Add an opt-in exact-byte partition gate so a representation can prove
  that no source byte was omitted, without imposing parser-authored semantic
  boundaries on partial migrations.
- [x] Prove a missing unit, an unlinked claim, a duplicate coverage row,
  and silent zero-claim coverage each fail mechanically.
- [x] Establish the sealed ingestion-edition context and canonical manifest
  resolver before changing rule identity.
- [x] Establish the engine-level corpus declaration and exact-byte access seam
  before introducing a Logovger adapter.
- [x] Materialize a declared corpus as `SourceLayerContext`,
  `SourceRepresentationContext`, append-only `SourceRevisionObservation`, and
  `IngestionEditionContext` in one validated engine operation.
- [x] Migrate all production rule identity and cross-seed references from
  document context to the exact ingestion edition. Keep the existing locator
  only as the explicit transitional citation path.
- [ ] Migrate citations section by section from source-document origin and
  loose locator fields to exact representation-scoped `SourceTarget` evidence.
  Delete the replaced locator path when the final section moves; do not add a
  `SourceTarget`-to-locator fallback.
- [x] Persist one small real Logovger section through the ledger, then
  derive or validate its existing seed without weakening current seed
  verification.
- [x] Complete one production representation with opaque leaves and typed
  syntax/layout exclusions, then add its `CompleteSourcePartition` assertion.
- [x] Record the observed red tests, implementation commit, focused
  tests, complete registered headless profile, and remaining owner
  decisions in this checklist before integration.

Phase A is complete only when the ledger can distinguish all three
cases without ambiguity: source visited with no rule content, source
visited with one claim, and source visited with several independently
disposed claims.

## Later phases, roadmap only

- [ ] Profile and reduce exact-ledger production verification cost at the
  2,057-target scale. Preserve atomic evidence and the verified-prefix
  boundary; grouping or indexes may be derived accelerators, never alternate
  coverage authority.
- [ ] Claim-level downstream invalidation when a definition or source
  pin changes.
- [ ] R12 provisional-claim integration after malleus exposes the
  required distinct representation and non-execution gate.
- [ ] Reinforcement counts and accommodation-queue ordering. Count
  ranks importance and never promotes a claim.
- [ ] Byte-exact model-assisted extraction through source-address tools.
- [ ] Independent or adversarial reading policy for ambiguous passages.
- [ ] Corpus-scale whole-book reconciliation and drift reporting.
- [ ] Learning and promotion across session, campaign, user, group, and
  global contexts.
- [ ] Reusable ingestion-module extraction, only after a second consumer
  proves the boundary.
- [ ] Dashboards and throughput optimization, only after the ledger's
  correctness gates exist.

These items do not block Phase A. Moving them forward requires the owner
decision named by the relevant boundary and a red test for the failure
being removed.

## Handoff contract

A session returning this work records:

- repository, worktree, branch, base, and resulting commit;
- remote main and relevant PR heads checked before integration;
- files changed and ownership boundaries with other sessions;
- the red test observed before each behavior change;
- focused tests and complete registered headless CTest result;
- any environment-only blocker, kept separate from product failures;
- every owner decision still open;
- documentation updates to this checklist and the append-only decision
  ledger.

Do not mark a phase complete from prose or a green happy-path fixture.
Its negative reconciliation cases must fail for the intended reason.
