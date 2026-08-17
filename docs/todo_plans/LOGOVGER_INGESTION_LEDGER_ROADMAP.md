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
- One source unit may produce zero, one, or many atomic claims.
- One claim may cite more than one source unit when its meaning crosses
  a source boundary.
- Zero claims requires an explicit no-rule-content judgement. Missing
  claims alone never imply it.
- Claim dispositions, resolution dependencies, and invalidation links
  live on claims, not on source-coverage rows.
- Seeds remain the verified load format. The ledger does not replace
  seed verification or weaken byte-exact citations.
- A future reusable ingestion module is promoted only after a second
  consumer exists. Logovger is consumer one.

`SourceCoverageUnit` and `IngestionClaim` are useful working labels in
discussion only. The owner has not selected schema type names, the exact
leaf-identity grammar, or the closed claim-disposition vocabulary.

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

Verification for the R11 implementation baseline:

- complete core CTest profile: 92 of 92 effective passes;
- the sandbox-only `test_run_recorder` home-directory denial passed when
  rerun with normal filesystem access;
- changed windowed-app source compiled directly;
- full windowed target remained blocked by the missing Apple Metal
  Toolchain, before application compilation;
- `git diff --check`: clean.

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
- [ ] Present and decide the minimum closed claim dispositions. Keep
  ontology-gap and rule-language-gap reasons distinct if both enter the
  first slice.
- [ ] Write a fixture containing a zero-claim unit, a one-claim unit,
  and a compound unit with multiple claims. Include one claim supported
  by multiple units if the selected address grammar can express it.
- [ ] Add red schema or parser tests for coverage-to-claim linkage and
  exact source identity before adding production types.
- [ ] Implement only the minimum ledger representation required by those
  tests. No compatibility fallback and no second ingestion grammar.
- [ ] Add a reconciliation gate: every enumerated unit has exactly one
  coverage judgement; zero claims requires explicit no-rule-content;
  every claim cites existing coverage; totals close.
- [ ] Prove a missing unit, an unlinked claim, a duplicate coverage row,
  and silent zero-claim coverage each fail mechanically.
- [ ] Persist one small real Logovger section through the ledger, then
  derive or validate its existing seed without weakening current seed
  verification.
- [ ] Record the observed red tests, implementation commit, focused
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
