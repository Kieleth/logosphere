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

Use `/Users/luis/Projects/logosphere-public`, the repository without
the physics work. Do not use `/Users/luis/Projects/logosphere-public-2`.

The completed R11 implementation baseline is commit `5111d64` on
`codex/logovger-ingestion-ledger`, based on `c1c8791`. At the time of
capture, the four open remote PR heads were unchanged and orthogonal to
this phase except for possible merge-text overlap in `CMakeLists.txt`
and `CHANGELOG.md`:

- PR 137: `a33c9b5`
- PR 134: `9857bf6`
- PR 129: `5670156`
- PR 96: `6f88f7c`

Recheck remote state before integrating. Merge current main into the
working branch. Do not rebase, edit another session's worktree, or
assume a historical branch is idle from this document alone.

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
- [ ] Persist one small real Logovger section through the ledger, then
  derive or validate its existing seed without weakening current seed
  verification.
- [x] Record the observed red tests, implementation commit, focused
  tests, complete registered headless profile, and remaining owner
  decisions in this checklist before integration.

Phase A is complete only when the ledger can distinguish all three
cases without ambiguity: source visited with no rule content, source
visited with one claim, and source visited with several independently
disposed claims.

## Later phases, roadmap only

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
