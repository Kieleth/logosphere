---
name: game-reflection
description: Operating discipline for turning a body of published rules into executable knowledge. Ingestion, the ledger style, arbiters, coverage gates, and the malleus contract. Load before touching seeds, extractors, the rulebook ontology, chargen procedures, or anything under examples/logovger.
---

# Game reflection: how this project operates

Reflecting a game means taking rules somebody else wrote and making
them executable without inventing anything. A rulebook goes in; a
playable, cited, replayable game comes out. Logovger is consumer one;
the machinery is meant to serve any book.

This skill carries PROCEDURE and POINTERS only. The machine truth lives
in the files below; if this skill ever disagrees with them, the files
win and this skill has a bug. Fix it in the same commit.

"The owner" throughout means the project maintainer.

## Load malleus first. Not optional.

**Any work touching the ontology, the KG, typed writes, schema
packs, or seed vocabulary loads the `malleus-acolyte` skill BEFORE
writing anything.** Not after a rejection, not when something breaks.
First.

The reason is mechanical rather than ceremonial: malleus owns what a
typed claim is, how it is staged, judged and replayed, and it already
implements pieces we would otherwise rebuild badly. It has a ledger, a
staging overlay for proposed subgraphs, an assent protocol whose
records bind content hash to actor and role, and a rubric of rites
earned by other projects' scars. Building any of that here is
duplication the inquisitor will find and be right about.

Run the capability probe in that skill and believe its verdict. A stale
install reports a stale root, and a stale root silently loosens every
constraint the generated headers claim to enforce.

## Owner contract (non-negotiable)

1. **The owner participates in every decision.** Present options with
   evidence and a recommendation when asked; the owner rules. Never
   pick "the obvious option" on their behalf.
2. **Educate before asking for a ruling.** Plain explanation first,
   concrete example, then the options. Name every term that is doing
   work: which file, which entity, what "row" means here. A metaphor
   used without definition is a decision made on the owner's behalf.
3. **No time estimates, ever.** Sequence by dependency only.
4. **Surface architecture decisions BEFORE building**, not embedded in
   a finished deliverable.

## The ledger style

Design records here are LEDGERS, not summaries. This applies to
`docs/RPG_MODULE.md` and to any decision record this module grows.

- **Append, never edit away.** A decision that stops being true is
  marked in place. Deleting it hides that we once believed it, and the
  reasoning that changed our minds is usually worth more than the
  conclusion.
- **States**: `ACTIVE` (default, no marker needed), `SUPERSEDED by
  <date>`, `INVALIDATED <date>` naming the evidence.
- **Date AND time, UTC**, on anything that supersedes or invalidates.
  Two decisions in one day happen here.
- **Mark the section, not just the log.** A reader arriving from a
  search lands in the middle of a DESIGN section and never scrolls to
  the table. The state marker goes at the top of the section too.
- **Say what survived.** A superseded decision is rarely all wrong.
  "Transcription is mechanical" died; the concern that produced it,
  that a model retyping a number is wrong in a way that still reads
  well, survived and became the tool contract. Record the split.

## The layers, and the seams between them

Full statement in `docs/REFLECTION_PROTOCOL.md`, which is the thing to
read before arguing about where something belongs. The short form:

    L0 source      bytes at addresses, pinned, never modified
    L1 extraction  judges STRUCTURE, copies content, may not invent a type
    L2 seed        the transactional unit; additive, sealed, never hand-edited
    L3 verify      refuses, never repairs
    L4 ontology    what MAY exist; gates every write; knows no policy
    L5 graph       what DOES exist; no privileged writer
    L6 runtime     dispatches on TYPE, never on a name; refuses over guessing
    L7 game policy primitives, handlers, procedure; replaceable per game
    L8 arbiter     answers what rules leave open; exactly one; leaves a record

Eight declared seams and nothing else: `extend`, `declare_primitive`,
`register_handler`, `set_attribute_selector`, `set_choice_resolver`,
`register_trigger`, `register_effect` (capability), `register_effect`
(contact). Anything crossing a boundary elsewhere is a bleed.

**Primitives and route contracts.** A primitive is the C++ behind one
procedure step; the procedure itself is data, a step naming
`primitive_ref` which the registry maps to a function. The registry
also declares that step's legal exit labels, and a seed may route on
those and no others: `roll_qualification` declares `passed` and
`failed`, and a route naming anything else is refused at verification.
Name plus exits is the ROUTE CONTRACT. That pairing is what keeps a
procedure data rather than code, which is why the set of primitive
names is a design surface and not an implementation detail. There are
17 today; the design record's OPEN item asks that new ones surface for
approval, and measurement shows the set grew from 8 to 17 without that
happening once, so treat the rule as live but unenforced and ask.

## Read first (in this order)

- `docs/REFLECTION_PROTOCOL.md`: the layers, the seams, the ten
  cross-cutting rules and the gate that proves each. It names its own
  ungated rules; those are intentions, not guarantees.
- `docs/RPG_MODULE.md`: the design record and decisions ledger. Read
  the log tail AND the state markers before proposing anything. It has
  an OPEN section listing what must not be built past.
- `docs/RULES_AS_DATA.md`: what absorbing a book cost us to learn, and
  the failure modes, written for other developers.
- `examples/logovger/docs/ABSORPTION_INVENTORY.md`: the contract, every
  book section mapped to ontology, procedure, table data or prompt.
- `docs/SOURCE_LOCATORS.md`: the addressing scheme.
- `docs/INGESTION_REVIEW.md`: five real pieces of the book traced
  through the pipeline, with what is measured rather than assumed.

## Operating rules

- **Structure is judged, content is copied.** A model decides which
  rows belong to which table, which cell is a footnote, where a column
  wrapped. It NEVER types the text. Bytes come from a tool that
  returns the exact bytes at an address, which is what keeps citation
  byte-exact. Superseding a decision here does not relax this.
- **One arbiter.** Readers may be many and may disagree; exactly one
  arbiter resolves them and its decision is recorded with its
  reasoning. Two authorities over one record leaves no way to say
  which wrote it.
- **Absence must have an address.** Un-ingested content leaves a row
  saying so, with a reason. Silence is the failure mode this whole
  module exists to break: an unabsorbed rule and a nonexistent rule
  look identical from inside the repo. Report `% parsed`, `% ingested`,
  `% raised`, `% duplicated`, and make the totals reconcile.
- **Three claims, kept apart.** FIDELITY (the data matches the source),
  COHERENCE (the graph hangs together), CONSEQUENCE (the engine acts on
  it). Green on the first two reads as assurance about the third and is
  not. Rank 0 grants were cited, verified, counted and executed by
  nothing for the life of the feature.
- **A seam that exists is not a seam that is wired.** Three separate
  bugs this shape: the headless driver never wired the choice resolver
  (one life in ten died at a fork), relation endpoints shipped as
  `(Entity, Entity)` while the machinery to narrow them was complete,
  and the coverage sweep supplied no attribute selector so no life ever
  got past the aging question. When you add a seam, grep every driver.
- **Generated seeds are the extractor's.** Never hand-edit a seed
  carrying `generated_by`. Teach the extractor and regenerate.
- **No quiet fallbacks.** Where a rule defers a judgment, it goes to
  the model and there is no deterministic stand-in. No key means the
  game refuses to run, not that it decides for itself.
- **Verify agent and tool claims.** Subagent reports are hypotheses.
  Grep them before building on them, and check whether a finding is
  stale against the branch you are on rather than the one it inspected.
- **Evidence unfiltered first.** Read raw output with eyes before any
  grep; a filter encodes a hypothesis. "No output" is verified wide
  before it becomes a finding.
- **Failed experiments are evidence.** Commit them honestly or park
  them behind a lever. Never revert or discard without the owner asking.

## Ruling protocol

1. Bring the question with its education and the options with
   evidence. One at a time when the owner says so.
2. Owner rules, and the ruling is appended to the ledger in
   `docs/RPG_MODULE.md` with date and time, in the owner's words where
   those carry the reasoning.
3. If it supersedes something, mark the old entry AND the old section.
4. If it changes PROCEDURE, update this skill in the same commit.
   Stale procedure loaded with authority is worse than no procedure.

## Ship checklist

Full suite green, with pre-existing failures named and shown to fail
identically on clean `origin/main` → coverage gates green (no absorbed
table reached by nothing, no declared type silently uninstantiated) →
headless sweep with no new aborts → seeds regenerate with no drift
(`git status --porcelain -uall`, not just `git diff`) → CHANGELOG entry
for anything user-facing → `Signed-off-by` on every commit → CI
conclusion read in the SAME command as the merge, never chained
blindly → owner says merge.

## Gates that already exist, so you do not rebuild them

- Seed verifier: citations resolve, numbers appear in the quoted text,
  `implied_by` for a number the text asserts without writing.
- `test_classification_audit`: the shipped seed against the shipped
  audit; a value the audit never saw turns the suite red.
- Coverage gate: every absorbed table reached by some life.
- Instantiation gate: every declared type has an instance or declares
  why not, by facet, with the reason in the schema.
- Root currency gate: the vendored malleus root must BE the installed
  root, byte for byte.
- Drift gate: `git diff` plus `git status --porcelain -uall`, because
  `git diff` cannot see a file that did not exist before it ran.
