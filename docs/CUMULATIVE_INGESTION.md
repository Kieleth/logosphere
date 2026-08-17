# Cumulative ingestion: prior art, and what is ours

`docs/REFLECTION_PROTOCOL.md` states R11: a passage is read against
everything already ingested, or it is not read. This document is the
background R11 stands on. It exists because R11 is not a local
convenience for one RPG book. It is a restatement of something several
fields worked out independently, and knowing which fields lets us take
their results instead of rediscovering them badly.

Owner's framing, 2026-08-16: "the seed-generative effect of written or
any communication, where diffused terms are planted early and revealed
and referenced as narration progresses."

Two claims are kept apart throughout. The **phenomenon** is old and
well described. The **enforcement** is what we are adding, and nothing
below does it for us.

---

## The phenomenon, in one example

> Characters who end their careers receive one benefit per term served
> in which they did not lose benefits.

Five terms are load-bearing: *character*, *end a career*, *benefit*,
*term served*, *lose benefits*. The sentence defines none of them. Each
was planted earlier, some chapters earlier. Read alone, the sentence is
grammatical and meaningless.

A book is not a bag of statements. It is a vocabulary that grows as it
is read, and later rules compose earlier concepts. Extraction that
treats passages as independent extracts sentences that happen to be
printed in a book.

---

## Where this already has a name

### Dynamic semantics: meaning is context change potential

The slogan is literal. In dynamic semantics a sentence does not denote
a truth value; it denotes a **function from an input context to an
output context**. Kamp's Discourse Representation Theory (1981) builds
Discourse Representation Structures, which introduce *discourse
referents* that later sentences pick up. Heim's File Change Semantics
is the closely related account: a discourse is a file, new material
opens file cards, later material updates them.

*What it gives us.* The formal shape of a passage reader. Its input is
the graph so far, its output is the graph after, and a passage whose
input context lacks its referents has no defined output. That is R11 as
a type signature rather than as an intention.

*What it does not give us.* Nothing mechanical. It describes competent
readers, and imposes no obligation on a program.

### Lewis on accommodation: the generative half

Lewis, "Scorekeeping in a Language Game", *Journal of Philosophical
Logic* 8 (1979), 339-359, building on Stalnaker's common ground. The
rule of accommodation: a presupposition required by what is said
**comes into existence**, provided nobody objects. Say "my sister is
visiting" to someone who did not know you have a sister, and they do
not halt; the sister enters the common ground because the sentence
needed her there.

*What it gives us.* This is exactly the owner's stronger proposal: a
concept mentioned before it is defined should be **created**, provisional
and under-specified, so later text has something to attach to. Lewis
supplies both the mechanism and its limit, and the limit is the
important half. Accommodation runs *provided nobody objects*. An
accommodation nobody can object to is not accommodation, it is
invention.

*What it does not give us.* Any notion of an objection that is
mechanically raised, or of a provisional entity that cannot be mistaken
for a settled one.

### Extension by definition: the formal core

The strictest statement of R11 is not linguistic, it is proof theory.
An *extension by definition* introduces a new symbol `R` together with
the axiom `∀x. R(x) ↔ A(x)` where `A` is a formula **in the existing
language**. Such an extension is *conservative*: it proves no new
theorem in the original language, because the new symbol can always be
translated away.

*What it gives us.* The precise reason a passage cannot be read alone.
A definition that references a symbol the theory does not have is not a
definition. It also gives the correctness criterion for our reading
order: each seed must be a conservative extension of everything loaded
before it, which is a checkable property, not a style preference.

*What it does not give us.* Books are not proof scripts. They
forward-reference, define by example, and refine across chapters.

### Terminology science: concept systems

ISO 704 (*Terminology work: principles and methods*) is the standards-
body version. Concepts sit in a **concept system**; a concept's
position is fixed by its *intension* (its set of characteristics) and
*extension*. Intensional definitions are preferred precisely because
they place the concept within the system, and in generic relations a
specific concept **inherits** the characteristics of its superordinate.

*What it gives us.* Vocabulary that survives outside our repo, and
independent confirmation that "define a term against prior terms" is
the recognised discipline rather than our invention. Useful when this
work is described to anyone outside the project.

*What it does not give us.* It governs human terminologists.

### Induction heads: the machine already does this

Olsson et al., "In-context Learning and Induction Heads", Transformer
Circuits Thread, 2022. A previous-token head plus an induction head
implement pattern completion: seeing `[A][B] ... [A]`, attend back to
the earlier `A` and predict `B`. The authors argue this mechanism
accounts for the bulk of in-context learning in large models.

*What it gives us.* The owner's observation, and it is correct: an LLM
reading a book already resolves later terms against earlier ones by
attending back. That is why an LLM reads the mustering-out sentence
correctly and a regular expression cannot.

*Why we build anything at all, then.* Attention resolves a reference
inside one context window, for the duration of one forward pass, and
leaves nothing behind. The resolution is not addressable, not
inspectable, not replayable, and not checkable. Our architecture is not
compensating for a model that fails at this. It is compensating for a
model that **succeeds and does not persist**. The purpose is to turn a
transient, unauditable resolution into a typed, addressable one that
outlives the pass that made it. That is the whole malleus thesis
applied to reading, and it is worth stating in exactly those terms
rather than as a workaround for model weakness.

---

## What is ours

Every field above **describes** cumulative interpretation. None of them
**gates** it. There is no dynamic-semantics compiler that refuses a
discourse whose referents are missing, no accommodation queue whose age
is measured, no terminology tool that fails a build.

Our contribution, and the only part we should claim:

**An ingestion in which cumulative reading is a mechanically checked
contract rather than a property of a careful reader.** Concretely: a
declared dependency order that is checked against actual references, a
refusal when a term resolves to nothing, a record of what each row
resolved against so a corrected definition can find its downstream, and
a test that fails when any of those is bypassed.

That claim is small and defensible. We should not claim to have
discovered cumulative interpretation.

---

## Typed accommodation: DECIDED 2026-08-16

> **STATE: ACTIVE.** Ruled by the owner 2026-08-16: accommodation, not
> a stricter reading order. "B, no C, B." The staged alternative was
> offered and refused, so this is not future work behind a phase gate;
> it is the design. Reinforcement is part of the ruling, see below.

This goes past R11 as written:

> when a concept is defined inside a document it is required to be
> captured in an ontology even if it's not fully fleshed out, and still
> is developing, but we know and can refer later concepts/words to that
> original notion

This is Lewis's accommodation, and it dissolves the hardest problem in
R11 as currently stated. Strict R11 needs a reading order in which
every term is defined before use. Books do not admit such an order:
they forward-reference and refine. Accommodation replaces the
impossible requirement (a perfect order) with an achievable one (a
**closed queue**): a term may be mentioned before it is defined, which
creates a provisional concept, and the ingestion is complete only when
no provisional concept remains unresolved.

**The danger, stated plainly.** A provisional concept is a half-open
gate unless its incompleteness is typed. An under-specified entity that
looks like a settled one is worse than a missing entity, because a
missing entity fails loudly and an under-specified one executes. This
runs straight into malleus doctrine (no half measures) and into the
engine's own rule that missing data is failure rather than a default.

**The form that survives that objection**, and it is the only form that
does:

1. A provisional concept is a **distinct status**, not a normal entity
   with empty slots. Nothing can read it as settled.
2. It carries **why it exists**: the passage that forced it, and the
   term as the book spelled it.
3. Nothing executes against it. A rule depending on a provisional
   concept is not runnable, and says so.
4. The queue is **closed before the world is playable**, and the gate
   is a test, not a report.
5. Its **age is measured**. A queue nobody drains is a backlog wearing
   a protocol's clothes.

Note that malleus already has the machine for this: a staging overlay
for proposed subgraphs, and an assent protocol that binds a content
hash to an actor and a role. A provisional concept is a staged claim.
We should use that rather than build a parallel notion of "sort of in
the graph".

### Reinforcement

Part of the same ruling, and the owner's framing: "provisional that
gets reinforced, inheriting from how brains work and reinforced paths
on neurons."

A provisional concept is created on first mention and **strengthened by
every later reference to it**. The record is therefore not a flat list.
It carries a count and the addresses that produced it, which turns the
accommodation queue into a ranked one: a term the book leans on forty
times and never defines is the most important thing the ingestion has
to say, and it should not be sitting at position 900 of an unsorted
backlog.

Three things reinforcement buys, and they are the argument for it:

1. **Priority that is measured rather than guessed.** The queue drains
   in importance order, and importance is a count of the book's own
   usage.
2. **A signal about the source.** A heavily-referenced never-defined
   term means one of three things: the definition exists and the
   reading missed it (a bug in us), the book assumes it as a primitive
   from outside (a real finding about the source), or the book is
   incoherent (a finding about the book). All three are worth knowing
   and none is visible without the count.
3. **A convergence measure.** Reinforcement rising while resolutions
   stay flat means the ingestion is accumulating debt, not progress.
   That is a number you can put on a dashboard and act on.

**What reinforcement must NOT do, and this is the whole risk.**
Reinforcement measures **importance, never resolution**. Forty
references do not define a term. Any rule of the form "a provisional
concept referenced N times is promoted to settled" rebuilds exactly the
half-open gate the five conditions above exist to prevent, and it does
it in the most dangerous possible way, because the promoted concept
will be one of the most load-bearing in the whole graph. Promotion
happens on a definition being found and judged, and on nothing else.

The neural analogy is worth keeping for intuition and worth abandoning
at this exact point: a reinforced path in a brain does become the
answer through repetition alone, and that is precisely the property we
are refusing.

---

## The module question

The owner's framing: this is not logosphere-specific, and could be a
module malleus or anyone else uses. The analogy offered was TCP inside
the ISO seven-layer model, meaning a protocol occupying a defined layer
of a larger stack rather than a whole system.

The analogy holds and is worth keeping. `REFLECTION_PROTOCOL.md`
already defines layers L0 to L8. Cumulative ingestion is the contract
across **L1 (extraction) and L2 (seed)**, and nothing above L2 should
know how it was satisfied. A different implementation may replace it if
it honours the same contract: every reference resolves against a
declared prior state, unresolved terms are raised or accommodated
explicitly, and the resolution set is recorded.

What that module would own, if extracted:

- the address grammar for source units (L0 already reserves this)
- the declared dependency order, and its check against actual
  references
- source-coverage records and the atomic claims linked to them
- claim-level resolution records and the downstream-invalidation query
  they enable
- the accommodation queue, while consuming malleus's provisional status
  type

What it must NOT own: the ontology (malleus), the graph store, the rule
runtime, and anything about games.

This stays a design note. Extracting a module before the second
consumer exists violates the promotion rule this project already
follows, and logovger is consumer one.

One correction to that, from the 2026-08-16 rulings: the **provisional
status type** in the list above is not ours to own. It is a
representation capability, so it belongs to malleus with the rest of
the ontology, and this module would consume it. See gap 1 below, which
makes the same point from the other end.

**Engine-boundary amendment, 2026-08-17 16:03 UTC.** The second-consumer rule
above delays packaging this protocol as a standalone reusable module. It does
not make Logovger the owner of source-corpus identity. The engine now accepts
two distinct inputs: an application-owned `SourceCorpusDeclaration` naming
the complete membership, layer, typed media, and revision provenance; and a
replaceable `SourceAccess` that returns exact bytes for each declaration. The
engine derives representation digests and lengths, the canonical manifest,
and edition identity. Logovger now supplies the first declaration and exact
filesystem adapter. Its six production seeds declare two source
representations and resolve all cross-seed references inside the derived
edition. The application does not own a private manifest grammar.

---

## The state of the gate today, measured 2026-08-16

R11's gate is recorded in the protocol as "partial and accidental".
Here is what that means, exactly, so the enforcement work has real
targets. Each carries the owner's verdict from 2026-08-16.

**ENFORCEMENT UPDATE, 2026-08-17 00:41 UTC.** Defects 2, 3 and 4 below
are fixed. The CLI accepts ordered prerequisite seeds and has paired
passing-with-prerequisite and failing-without-prerequisite fixtures.
`verify_and_load_seed_sequence` is the one owner of the growing prior
world, and the windowed game, headless runner and chargen tests all call
the same Logovger loader that delegates to it. A generic reversed pair
and the real six-seed manifest with its final procedure moved first both
fail at seed zero before loading anything. Item 1 remains upstream
representation work and item 5 remains accepted. R12 is not part of this
enforcement slice.

1. **The prior world is an optional argument.**
   `include/logosphere/kg/seed_verifier.h:120` declares
   `verify_seed(..., const std::vector<const SeedEnvelope*>& prerequisites = {})`.
   Blind verification is the DEFAULT and compiles silently. The header
   comment explains at length why prerequisites are needed, and the
   signature makes them optional.
   **Verdict: WRONG LAYER, not a bug here.** The owner's correction:
   the starting ontology must be able to capture any concept relevant
   to the domain being captured, provisional ones included, and that is
   malleus-dev work rather than logosphere work. Deleting a default
   argument treats a representation gap as a call-signature defect.
   Tightening the signature is worth doing and does not close anything;
   an ontology that cannot hold a half-known concept forces the caller
   to choose between a lie and a refusal no matter what the signature
   says. Goes upstream with the accommodation design.

2. **The shipped CLI omits it.** `tools/logosphere_verify.cpp:76` calls
   `verify_seed(parsed.seed, source_root, registry)` with no
   prerequisites. Every seed it checks is checked in isolation. Its two
   ctest cases (`logosphere_verify_positive`, `_negative`) use
   self-contained fixtures, so the omission has never shown up.
   **Verdict: BUG.** The tool whose job is verification cannot verify
   the seeds the game actually loads. Fix it and give it a fixture that
   only passes with prerequisites, so the fix is gated rather than
   asserted.

3. **Accumulation is hand-rolled per call site, twice.**
   `examples/logovger/test_chargen.cpp:91` and
   `examples/logovger/src/logovger_app.h:348` each build their own
   `loaded_before` vector in their own loop:

   ```cpp
   std::vector<const kg::SeedEnvelope*> loaded_before;
   for (seed : kRuleSeeds) {
       verify_seed(parsed, root, registry, &procedures, loaded_before);
       kept.push_back(parsed);
       loaded_before.push_back(&kept.back());   // the growing world
   }
   ```

   The R11 discipline lives inside that loop, and the loop is copied
   rather than owned. A third loader (a tool, a new example, an editor)
   must rebuild it correctly from memory, and nothing catches a version
   that forgets the last line. The comment above `kRuleSeeds` records
   that duplicating the seed LIST already caused a real failure; this
   is the same hazard one level down.
   **Verdict: one owner.** The accumulator becomes a function, and the
   loaders call it.

4. **The dependency order is a comment.**
   `examples/logovger/chargen/rule_seeds.h` orders six seeds and
   explains the reasons in prose ("it references the Skills the
   vocabulary owns, the Currency and dice the earlier seeds create").
   Nothing machine-checks that the order matches the actual references.
   The header asserts that a wrong order "fails loudly, which is the
   intended behaviour"; **no test loads them out of order**, so that
   claim is untested.
   **Verdict: BUG.** A documented guarantee with nothing behind it. The
   test loads them out of order and asserts the failure, which either
   proves the guarantee or discovers it was never true. Both outcomes
   are worth more than the comment.

5. **Extractors load prior seeds individually.**
   `examples/logovger/tools/extract_career_tables.py` has
   `load_skill_references()` and `load_career_seed_references()`. Each
   extractor decides for itself what prior knowledge it needs.
   **Verdict: ACCEPTED, not a defect.** The owner's ruling: this is
   what the model is for. Which prior knowledge a passage needs is an
   ambiguous judgement, and a judgement is exactly what should not be
   frozen into a static manifest. The gate belongs on the OUTPUT (does
   every reference resolve, is every unresolved term accommodated and
   recorded), never on the reader's choice of what to consult. Note the
   consistency with the module's oldest rule: structure is judged,
   content is copied, and what to read next is structure.

At the time of measurement, three of the five were defects: 2 and 4
were bugs and 3 was a duplicated owner. They are now closed by the
enforcement update above. Item 1 remains upstream in malleus. Item 5
remains correct as it stands.

---

## The ingestion ledger topology, decided 2026-08-17

The ledger is not one row forced to serve two incompatible purposes.
It has two levels:

1. A mechanically enumerated **source-coverage record** proves that an
   addressed unit of the pinned source was visited and judged.
2. An **atomic claim record** captures one semantic claim produced from
   one or more of those units, including its disposition and the prior
   concepts against which it resolved.

The labels above describe roles, not final schema type names. The owner
selected this topology as option 4, "belt and suspenders". It resolves
the paragraph-versus-sentence-versus-cell dead end without pretending
that source layout and semantic meaning have the same grain. A source
unit may yield zero, one, or many claims. A claim may cross source-unit
boundaries. Mixed results therefore stay truthful: two rules in one
paragraph can have different dispositions, while the source paragraph
still has one visible coverage judgement.

Four reconciliation rules are binding:

1. Every unit produced by the selected mechanical source enumerator has
   exactly one coverage judgement.
2. A unit with zero claims explicitly says "no rule content". Absence
   of claims is not accepted as evidence of that judgement.
3. Every claim cites at least one coverage unit and owns its own
   disposition, resolution dependencies, and invalidation identity.
4. Whole-source totals reconcile from enumeration through coverage to
   claims, including raised, duplicate, contradictory, and provisional
   claims. No filtered content disappears before the ledger.

This decision does not choose the schema class names, exact source-unit
identity grammar, or closed claim-disposition vocabulary. Those are the
first owner-facing boundaries in Phase A. The TDD sequence and current
handoff are recorded in
`todo_plans/LOGOVGER_INGESTION_LEDGER_ROADMAP.md`.

**Implementation amendment, 2026-08-17 15:25 UTC.** Those boundaries
are now selected. Exact leaf identity is the typed source
representation-plus-primary-selector tuple. `SourceCoverage` and
`IngestionClaim` are the enduring records. Disposition is never a
mutable property on either record: `CoverageDecision` and
`ClaimDecision` reuse `ArbiterDecision` and form contiguous append-only
histories per subject. The latest decision is current, and previous
readings remain queryable. Coverage decisions choose `CLAIMS_PRESENT`
or `NO_RULE_CONTENT`. Claim decisions choose `MATERIALIZED`, `PARTIAL`,
`RAISED`, `DUPLICATE`, or `CONTRADICTORY`; incomplete claims distinguish
ontology gaps from rule-language gaps. Provisional remains an R12
staged-claim concern owned by Malleus rather than a sixth local status.

**Identity-context amendment, 2026-08-17 15:42 UTC.** A captured rule is
scoped to the exact ingestion edition against which it was interpreted. The
edition is not a single document and is not only the layer name: it is a sealed
context whose identity combines the source layer with the SHA-256 digest of a
canonical manifest of exact source representations. This lets one claim cite
several files without assigning the resulting rule to one arbitrary file. The
manifest is sorted by logical path and length-prefixes every field, so relation
order and delimiter characters cannot change or collide in its identity.
Paths and represented bytes participate. Source-system revision is kept in a
separate typed observation linked to the exact representation and excluded
from identity, so the same exact corpus remains the same edition across an
unrelated source revision. The schema and mechanical resolver are built;
existing document-scoped rule loading has not yet migrated.

**Evidence-migration amendment, 2026-08-17 19:16 UTC.** Production rule
identity has since moved to the exact edition, and the first real section now
uses the ledger. The migration exposed a general ownership error before more
content moved: selectors and targets were being scoped like rules, to the
edition. They identify bytes in one representation, so the loader now scopes
them to `SourceRepresentationContext` and derives byte-range target identity
from the canonical selector key. Rules and claims remain edition-scoped.

The verifier enforces one evidence grammar per rule. Exact evidence requires a
reconciled claim and coverage path to a resolvable `SourceTarget`, edition
origin, and no structural locator fields. Legacy evidence requires its quoted
document path and cannot coexist with exact claims. The `Injury Crisis`
heading and three sentences prove zero-claim and multi-claim coverage against
production bytes: four coverage records, six claims, five raised decisions,
and one partial decision materializing the existing Credits entity. This is a
section-by-section replacement. It does not weaken verification while the
remaining 3,081 legacy citations move.

**Second-slice amendment, 2026-08-17 19:46 UTC.** `Medical Care` makes the
dependency order visible. The section appears before the career definitions in
the book, but its payment table names all 24 careers. Its production seed is
therefore loaded after the Career seed, and its nine percentage claims record
typed `CLAIM_RESOLVED_AGAINST` links to the exact prior Career entities and
2D6 expression. This is R11 as graph data rather than a comment about reading
order. The seed adds 83 resolution links in total.

The slice also found a source-address failure class. Unicode character offsets
are not UTF-8 byte offsets, and a range may resolve successfully while pointing
at the wrong bytes. Every UTF-8 ledger target now requires an exact supporting
quote that must converge with its primary byte range. `Medical Care` persists
22 leaves and 15 claims; one partial claim materializes the 5,000-credit
restoration-cost constant and fourteen remain raised. The earlier `Injury
Crisis` targets were upgraded to the same convergence gate.

**Third-slice amendment, 2026-08-17 20:08 UTC.** `Medical Debt` proves the
production ledger's compound-leaf case. One exact sentence supports three
independent claims: medical-care debt must be paid from Benefits, anagathic
drug debt must be paid from Benefits, and both obligations precede every other
finishing-touch action. The claims share one coverage record rather than
duplicating the source target.

None of the three claims is forced into executable data. The medical-debt and
precedence claims are raised as rule-language gaps because the graph cannot
debit Benefits or order the payment before every other finishing action. The
anagathic claim is raised as an ontology gap because the graph has no
anagathic-drug or corresponding debt concept. Interpretation still records
typed dependencies on the prior medical-care cost, Benefits lookup, and final
procedure step. The seed therefore follows all three owners in dependency
order. Production now has 28 coverage records and 24 claims: four
no-rule-content leaves, two partial claims, and 22 raised claims.

**Fourth-slice amendment, 2026-08-17 21:08 UTC.** `Aging` migrates the section
ending before `Aging Crisis`: one heading, three prose sentences, two table
header cells, and sixteen data cells. These 22 exact leaves support 12 claims.
Nine are materialized, two recurring or computed operations are raised as
rule-language gaps, and the bottom table row is partial because the source
prints `-6` while the executable table must also decide results below `-6`.
That difference is recorded as `SOURCE_GAP`, not hidden in an ordinary
materialized decision.

The slice replaces structural locators on all 30 Aging rule entities. The
start-age constant moves from the career extractor to the shared-table
extractor, which now owns both its exact evidence and every existing Aging
table consequence. The checklist-owned `roll_aging` procedure step cites a
different source leaf and remains on the legacy path. Production now has 50
coverage records and 36 claims: five no-rule-content leaves, three partial
claims, 24 raised claims, and nine materialized claims. There are 3,051 legacy
structural citations left.

Integration exposed two generic verifier assumptions. Exact evidence may
combine several leaves, so a table band cannot be assumed to occupy the first
fragment. The verifier now inspects every fragment and requires one unique
band match. It also now reads `roll_min_unbounded`, matching the existing
table runner. A finite printed band may widen at one boundary only when the
current materializing decision is `PARTIAL` with `SOURCE_GAP`; another gap kind
cannot authorize the change.

### Coverage grain: atomic leaves, decided 2026-08-17

Coverage starts at the smallest source leaves the document model must
expose: headings, prose sentences, table cells including header and
empty cells, and sentences inside list items. Source content that cannot
be classified becomes an opaque leaf or makes enumeration fail. It is
never dropped. A file manifest proves that every file in the pinned
source entered enumeration, including files with no leaves.

This is deliberately more granular than the minimum needed for many
pages. Paragraphs, rows, sections, and files may be derived later as
grouping, filtering, batching, or display views if leaf volume becomes
unmanageable. They do not become another coverage authority and cannot
hide an atomic leaf. Claims remain a separate level and may link one or
many leaves.

The decision does not make the present locator complete. The corpus has
duplicate source text, empty table cells, and rows without unique keys.
The exact identity and disambiguation grammar for those leaves remains
the next Phase A decision.

---

## Citations

Verified against sources 2026-08-16:

- Dynamic semantics, "meaning is context change potential"; Kamp 1981
  (DRT), Heim (File Change Semantics). Stanford Encyclopedia of
  Philosophy, https://plato.stanford.edu/entries/dynamic-semantics/
- Lewis, D. "Scorekeeping in a Language Game", *Journal of
  Philosophical Logic* 8 (1979), 339-359.
  https://link.springer.com/article/10.1007/BF00258436
- Extension by definitions; conservative extension.
  https://en.wikipedia.org/wiki/Extension_by_definitions
- ISO 704, *Terminology work: principles and methods* (2009, 2022).
  https://www.iso.org/standard/79077.html
- Olsson, C. et al. "In-context Learning and Induction Heads",
  Transformer Circuits Thread, 2022. https://arxiv.org/pdf/2209.11895
- Ying, C. et al. "Do Transformers Really Perform Bad for Graph
  Representation?" (Graphormer), NeurIPS 2021. Structural encodings
  (centrality, spatial, edge) added as a bias inside attention.
  https://proceedings.neurips.cc/paper/2021/file/f1c1592588411002af340cbaedd6fc33-Paper.pdf

Named from memory and NOT verified here. Check before citing any of
these in anything that leaves the repo:

- Halliday and Hasan, *Cohesion in English* (lexical cohesion, lexical
  chains): the textual mechanism by which a term is planted and later
  picked up.
- Grosz, Joshi and Weinstein, centering theory (local coherence and
  attentional state).
- Mann and Thompson, Rhetorical Structure Theory (discourse structure
  above the sentence).
- Buitelaar and Cimiano, ontology learning from text (the "ontology
  learning layer cake").
- Bengio et al., curriculum learning (presentation order affects what
  is learned).
