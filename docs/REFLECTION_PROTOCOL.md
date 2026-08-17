# The Reflection Protocol

**Version 1. 2026-08-15.**

Turning somebody else's rulebook into a playable game touches eight
distinct concerns, and until now they were described in eight places:
a decisions log, three schema headers, a skill, and the signatures of
whatever function happened to implement them. That is why the same
question kept coming back in different clothes. Where does extraction
stop? What may the graph say that the engine does not? Who is allowed
to add a step?

This document answers those once, as a protocol.

A protocol here means three things, and a document missing any of them
is a design note wearing a protocol's name:

1. **Named layers with stated contracts.** What each owns, what it may
   never do, and what crosses its boundary.
2. **A conformance check per rule.** Every rule below names the gate
   that proves it, or admits it has none. A rule nothing can check is
   marked UNGATED and is a liability, not a guarantee.
3. **A way to replace a part.** Each layer says what a replacement must
   honour, so somebody can swap it without reading the layer above.

## How to change this protocol

The protocol is versioned and its history is a ledger, the same
discipline as `RPG_MODULE.md`. Rules are appended and never edited
away; a rule that stops being true is marked SUPERSEDED or INVALIDATED
in place, with date, time, and the evidence.

Extending it is expected. This is version 1 of a thing that has
absorbed one chapter of one book. The parts most likely to move are
named at the bottom under "Known thin ice", which exists so that a
later reader can tell what we were confident about from what we were
guessing about.

---

## The layers

Each layer is written the same way: what it owns, what it may not do,
what crosses the boundary, and what a replacement must honour.

### L0 Source

**Owns** the original text. Bytes at addresses, pinned to a commit.

**May not** be modified. Ever. Corrections to the source are made
upstream and re-pinned; a typo fixed locally is a fork, recorded as
one.

**Crosses out**: `(address) -> exact bytes`, and nothing else. An
address is a file plus a section, sentence, table, row, cell, or, when
images arrive, a region.

**A replacement must honour**: addressability and byte-exactness. A
PDF, an OCR pass, or a transcript replaces markdown here and the layers
above cannot tell, provided it can answer both questions. This is the
extension point for "absorb a scanned book" and it is the only one
needed for it.

### L1 Extraction

**Owns** the reading. Deciding that a three-cell row belongs to an
eight-column table, that a trailing cell is a footnote, that a logical
column wrapped.

**May not** type text. Content is copied from L0 by tool; only
structure is judged. **May not** invent a type: extraction fits the
source to the vocabulary L4 declares, and where it does not fit it
raises rather than approximates.

**May not** read a passage in isolation. See R11: extraction is
cumulative, and a passage is understood against everything already
ingested or it is not understood at all.

**Crosses out**: typed claims, each carrying the address it came from,
plus exceptions for what could not be expressed.

**A replacement must honour**: structure judged, content copied, every
claim addressed, and the cumulative reading. A deterministic parser and
a pair of models with an arbiter are both conformant on the first
three; we have used both, and the ledger records why the second
replaced the first.

### L2 Seed

**Owns** the transactional unit. An envelope (source pin, layer,
invariants) and an ordered list of operations.

**May not** be edited by hand when it declares `generated_by`. **May
not** write onto content another seed created: a seed may point at what
another owns, never modify it, because once a seed has said "this came
from that document at that commit, and here is the quote", nothing may
alter it afterwards or the citation stops being provable. Overrides are
copy-on-write forks, never edits.

**Crosses out**: operations, applied atomically or not at all.

**A replacement must honour**: the envelope's source pin, additive-only
semantics, and declared invariants.

### L3 Verification

**Owns** refusal. Proving L2 against L0 and against L4 before anything
loads.

**May not** repair. A verifier that fixes what it finds is a second
author with no citation. It refuses and says why.

**Crosses out**: a verdict. Nothing else.

**A replacement must honour**: refusal over repair, and byte-exact
comparison rather than normalised comparison.

### L4 Ontology

**Owns** what may exist. Types, slots, ranges, relation endpoints,
enum members.

**May not** contain game policy, and may not know what any rule DOES.
A type says a thing can exist and what shape it has. What happens when
it is applied belongs to L6 and L7.

**Crosses out**: a registry that gates every write above it.

**A replacement must honour**: gating. Extension is add-only once
instances exist; retirement is supersession with a deprecation note,
never deletion.

### L5 Graph

**Owns** what does exist. Instances, their properties, their relations.

**May not** accept an undeclared write. Not from a seed, not from the
engine, not from a model.

**Crosses out**: queryable state.

**A replacement must honour**: that every write is validated against
L4, on every path, with no privileged writer.

### L6 Rule runtime

**Owns** turning typed data into effects. Throws, table rolls, lookups,
outcome application, procedure stepping.

**May not** branch on a name. It dispatches on TYPE. A runner that
matches a printed table name has made the book's spelling load-bearing,
and the book will be respelled. **May not** guess: where a rule leaves
something open it stops and asks L8.

**Crosses out**: applied effects, and requests for what it cannot
decide.

**A replacement must honour**: type dispatch, and refusal over
invention.

### L7 Game policy

**Owns** what a step means for this game. Primitives, outcome handlers,
the procedure and its routes.

**May not** live in the engine. If it would make sense in a farming
game and a combat game equally it belongs below; if it only makes sense
here it belongs at this layer.

**Crosses out**: registrations through the declared seams.

**A replacement must honour**: the seams. A different game replaces
this layer entirely and touches nothing below it. That is the test of
whether the boundary is real.

### L8 Arbiter

**Owns** the answers the rules leave open, and only those.

**May not** be plural for a single decision, and may not be silent:
every decision is recorded with the question, the options, the choice,
the reason, and who decided, readable. Readers may be many and may
disagree; exactly one arbiter resolves them.

**Crosses out**: a decision, and a record of it.

**A replacement must honour**: one arbiter per decision, and the
record. A human at a keyboard, a model, and a seeded generator are all
conformant.

---

## The seams, as they exist in code

These are the declared crossing points. Anything else is a bleed.

| Seam | Layer | What a game supplies |
|---|---|---|
| `OntologyRegistry::extend` | L4 | its own types |
| `ProcedurePrimitiveRegistry::declare_primitive` | L7 | a step name and its legal exits |
| `OutcomeExecutor::register_handler` | L7 | what a typed outcome DOES |
| `OutcomeExecutor::set_attribute_selector` | L8 | who chooses which attribute |
| `OutcomeExecutor::set_choice_resolver` | L8 | who chooses which branch |
| `TriggerRegistry::register_trigger` | L7 | when a capability rule fires |
| `EffectRegistry::register_effect` | L7 | what it does |
| `ContactResponse::register_effect` | L7 | what a contact does |

**A step's route contract** is the pair of its name and its declared
exit labels. `roll_qualification` declares `{"passed", "failed"}`, so a
seed may route on those two words and no others. The contract is
checked at verification: a route naming an undeclared label is refused
before anything runs. This is what keeps the procedure data rather than
code, and it is why the set of primitives is a design surface rather
than an implementation detail.

---

## Cross-cutting rules

Numbered so they can be cited. Each names its gate.

**R1. No magic strings.** Captured data holds references to ontology
elements, never bare names. `Athletics` is a reference that must
resolve to a declared skill.
*Gate*: the verifier refuses a reference that does not resolve.

**R2. Byte-exact citation.** Every captured value carries the verbatim
text it came from, copied and never retyped.
*Gate*: the value verifier compares against the source at its address.
Measured: 3,089 quotes, zero misses.

**R3. Structure is judged, content is copied.** A model decides shape;
a tool returns the bytes.
*Gate*: UNGATED today. The current extractors copy by construction, so
nothing yet proves a model did not retype. This becomes gateable when
extraction moves to the tool, and it must, or R2 rests on habit.

**R4. Type dispatch, never name dispatch.** L6 branches on declared
type.
*Gate*: renaming every table in the graph produces identical lives.

**R5. Refusal over invention.** Missing or undecidable data stops the
run with an actionable message. No silent default, no stand-in policy
the book never printed.
*Gate*: partial, by test per site. There is no general check that a new
code path fails closed.

**R6. One arbiter, and it leaves a record.**
*Gate*: `ArbiterDecision` is written by the session, so no driver can
forget it.

**R7. Absence has an address.** Un-ingested content leaves a row saying
so, with a reason. Metrics reconcile: parsed, ingested, raised,
duplicated, and the totals add up.
*Gate*: UNGATED. This is the largest hole in the protocol and the
reason the ledger work exists.

**R8. Reached, not merely present.** A rule absorbed and executed by
nothing is a defect, not an inventory item.
*Gate*: coverage gate (no table reached by nothing) and instantiation
gate (no declared type silently uninstantiated, or it says why).

**R9. Sealed provenance.** A seed may point at another's content and
never write onto it. Overrides fork.
*Gate*: `create_only` slots and the additive-batch refusal of
`destroy_entity`.

**R10. Add-only evolution.** Once instances exist, a type is extended
and never narrowed in place; retirement is supersession.
*Gate*: partial. The ontology collision check catches redefinition, not
narrowing.

**R11. Extraction is cumulative, never parallel and blind.** A passage
is read against everything already ingested. Every term it uses either
resolves to something the graph already holds, or is a new concept this
passage defines, or is unresolvable, in which case it is raised and
accommodated under R12. There is no fourth option, and in particular
there is no "understood well enough from the sentence alone".
*Gate*: **AMENDED 2026-08-17 00:41 UTC.** Still partial, but seed loading
is no longer accidental. `verify_and_load_seed_sequence` owns ordered
prerequisite accumulation; the game, headless runner, chargen gate and
verifier CLI all enter through it. A two-seed fixture passes only with its
prerequisite, and both a reversed fixture and the reversed production
manifest fail before the dependent seed mutates the world. Raw extraction
output and R12 accommodation remain ungated.

This is the rule the whole module turns on, so it gets its own section
below.

---

## R11 in full: a book teaches itself, in order

Take one sentence:

> Characters who end their careers receive one benefit per term served
> in which they did not lose benefits.

To capture that you must already know what a **character** is, what it
means to **end a career**, what a **benefit** is, what a **term served**
is, and what **losing benefits** means. Not one of those is defined in
this sentence. Every one is defined earlier, in another section, often
in another chapter, and the sentence is unreadable without them.

A book is not a bag of independent statements. It is a vocabulary that
grows as you read, where later rules compose earlier concepts, and the
same words carry meanings the text established pages ago. Extraction
that treats passages as independent units is not extracting the book.
It is extracting sentences that happen to be printed in it.

**What follows from that, concretely.**

*Order is a dependency order, not a page order.* A passage cannot be
read before the passages that define its terms. The book's own
ordering is usually close to this and is not identical to it, because
books forward-reference.

*Parallelism is bounded by that order.* Passages with no unresolved
dependency on each other can be read at once. A naive fan-out across
the whole book cannot work, and the limit is real rather than a
performance choice.

*Every extraction carries the graph so far.* Not the whole graph
necessarily, but the slice its terms resolve into. This is the same
discipline R1 already applies to values, raised to concepts: no magic
strings becomes no magic CONCEPTS.

*An unresolvable term is a finding, not a guess.* "Benefit" appearing
before anything defines it means either the reading order is wrong or
the book forward-references, and both are worth knowing. Silently
inventing a meaning is how a rule ends up subtly wrong while reading
perfectly. **AMENDED 2026-08-16 06:48 UTC**: it is a finding *and* it
creates something. See R12.

*The ledger records the dependency.* A row says which prior concepts it
resolved against, so re-ingesting a changed passage can find everything
downstream of it. Without that, a corrected definition leaves every
rule built on the old one quietly stale.

**The practice already exists in embryo, unnamed.** Three places:

- `extract_career_tables.py` loads the skills seed to resolve
  `Athletics` to a canonical skill reference rather than a string.
- `verify_seed` takes `loaded_before` and checks each seed against the
  growing world rather than against nothing.
- `rule_seeds.h` lists the seeds in a hand-maintained dependency order,
  with the reasons in comments: "After the dice and currency it
  references", "it references the Skills the vocabulary owns".

Three hand-wired instances of one principle nobody had stated. Naming
it is what turns a habit into a contract, and what makes the paired
readers and the arbiter buildable: a reader cannot be handed a passage
and a schema alone, it must be handed a passage, a schema, and what the
book has said so far.

---

**R12. A term used before it is defined creates a provisional concept.**
Accommodation, not refusal, and not a guess. The mention itself is the
act of creation, so later passages have something to attach to. A
provisional concept is a distinct status that nothing may execute
against, it names the passage that forced it and the term as the source
spelled it, and every later reference reinforces it. Ingestion is
complete only when nothing provisional remains unresolved.
*Gate*: NONE. Decided 2026-08-16, not yet built.

## R12 in full: what to do when the order cannot exist

R11 needs a reading order in which every term is defined before it is
used. No real document admits one. Books forward-reference, define by
example, and refine across chapters, and a protocol that demands
otherwise will be honoured by nobody and quietly bypassed by everybody.

R12 replaces the impossible requirement with an achievable one. Not a
perfect order: a **closed queue**.

The formal ancestor is David Lewis, "Scorekeeping in a Language Game"
(1979), and his rule of accommodation: a presupposition required by
what is said comes into existence, **provided nobody objects**. Say "my
sister is visiting" to someone who did not know you have a sister and
they do not halt. The sister enters the common ground because the
sentence needed her there.

The second clause is the half we implement. An accommodation nobody can
object to is not accommodation, it is invention. So:

1. **Provisional is a status, never an entity with empty slots.**
   Nothing may read it as settled. An under-specified thing that looks
   settled is worse than a missing thing, because the missing thing
   fails loudly and the under-specified one executes.
2. **It carries why it exists.** The passage that forced it, and the
   term as the source spelled it.
3. **Nothing executes against it.** A rule depending on a provisional
   concept is not runnable and says so.
4. **The queue closes before the world is playable**, and the gate is a
   test, not a report.
5. **The queue's age is measured.** A queue nobody drains is a backlog
   wearing a protocol's clothes.

**Reinforcement.** Every later reference strengthens the provisional
concept, so the queue is ranked by the source's own usage rather than
by arrival order. A term the book leans on forty times and never
defines is the most important thing the ingestion has to report, and it
must not sit at position 900 of an unsorted list. Reinforcement rising
while resolutions stay flat is an ingestion accumulating debt, which is
a number worth watching.

**And the limit, which is the whole risk.** Reinforcement measures
importance, **never resolution**. Forty references do not define a
term. Any rule that promotes a provisional concept to settled on count
alone rebuilds the half-open gate, and does it to the most load-bearing
concepts in the graph. Promotion happens when a definition is found and
judged, and on nothing else. The neural analogy that motivates
reinforcement is kept for intuition and abandoned exactly here: a
reinforced path in a brain does become the answer through repetition,
and that is precisely the property this refuses.

A replacement for this layer **must honour**: mention creates, status
is distinct, nothing executes against provisional, the queue closes
before play, and count never promotes.

---

## Decisions this protocol records

Taken 2026-08-16 04:57 UTC, except where marked OPEN. R11 above is one
of them and is written up separately because it governs the others.

**The ledger covers the whole book.** Not only rule-bearing passages.
Every passage gets a row, and "no rule content" is a judged status like
any other. Measured, the book is 51.3% table rows, 35.6% prose, 12.8%
headings, and the genuinely non-rule files (legal, about, introduction,
README, contents) are 47,407 bytes of 715,234, so 6.6%. Restricting the
ledger to rule-bearing passages would have made `% ingested` read
better and would have put the act of exclusion outside the record,
which is exactly where a rule goes missing. The cost is real: something
judges "this is flavour" a few thousand times, and every one of those
is a judgement that can be wrong. It is a judgement that will at least
be visible.

**The ledger has two levels, DECIDED 2026-08-17 01:41 UTC.** The owner
selected option 4, "belt and suspenders": mechanically enumerable
source-coverage rows plus atomic semantic claims linked to them. This
supersedes the single-row-grain question. A coverage row proves that an
addressed part of the pinned source was visited. A claim records one
meaning judged from that source, what it resolved against, and what
happened to it. One source unit may produce zero, one, or many claims;
one claim may cite more than one source unit when its meaning crosses an
address boundary. The mustering-out paragraph therefore gets one
coverage judgement and separate claims for its separate rules. It no
longer has to lie about a mixed outcome.

The two levels have separate invariants. Every mechanically enumerated
source unit has exactly one coverage judgement. Zero claims is legal
only when the coverage row explicitly judges that the unit has no rule
content; silence is never that judgement. Every claim cites at least one
coverage row and carries its own disposition, dependency set, and
downstream invalidation identity. Ingested, raised, duplicate,
contradictory, provisional, and any later claim outcomes belong to the
claim, not to the coverage row. Exact type names, leaf identity grammar,
and the closed outcome vocabulary remain implementation decisions and
must be settled by the Phase A tests. The required topology does not.

**Phase A contract, 2026-08-17 15:25 UTC.** The selected names are
`SourceCoverage`, `IngestionClaim`, `CoverageDecision`, and
`ClaimDecision`. Outcomes are append-only `ArbiterDecision` histories,
ordered contiguously per subject, never mutable status fields. Current
state is the latest decision. The closed coverage vocabulary is
`CLAIMS_PRESENT` / `NO_RULE_CONTENT`; the closed claim vocabulary is
`MATERIALIZED`, `PARTIAL`, `RAISED`, `DUPLICATE`, and
`CONTRADICTORY`. Partial and raised decisions distinguish
`ONTOLOGY_GAP` from `RULE_LANGUAGE_GAP`. The reconciliation gate
accounts for every enumerated source target, every coverage record,
every claim-to-coverage link, and every current decision while retaining
the prior events. R12 provisional state remains upstream Malleus work.

**Coverage uses atomic source leaves, DECIDED 2026-08-17 03:12 UTC.**
The canonical coverage units are headings, prose sentences, table cells
including header and empty cells, sentences within list items, and an
opaque leaf for source content the normalizer cannot classify more
precisely. A file manifest still records every pinned file, including a
file that produces no content leaves. Markdown separators, blank layout
lines, and fence delimiters are syntax protected by the source pin, not
independent semantic judgements. Content inside a fence remains a leaf.
Nothing unrecognised may disappear: it becomes opaque or enumeration
fails.

Paragraphs, table rows, sections, and files may later be used for
filtering, batching, display, and derived roll-ups when leaf volume
becomes inconvenient. They do not replace the atomic records and do not
carry a second coverage truth. The owner's reason is foundational: start
with the smallest addressable evidence and add grouping on top if scale
requires it. The exact identity grammar for duplicate and empty leaves
remains open; the current locator cannot uniquely address every such
leaf, so Phase A may not pretend the existing grammar is sufficient.

The implementation checklist and parallel-session handover live in
`todo_plans/LOGOVGER_INGESTION_LEDGER_ROADMAP.md`.

**A derived value that contradicts a stated one is kept, both of them,
with the divergence recorded.** Not refused, not silently resolved.
This is not hypothetical: the Cutlass states 1250g and a blade of 600
to 900mm, and at the ends of its own range the derived mass is 1000g
and 1500g. The book's two facts cannot both hold across the book's own
range, so any policy that keeps one number is choosing which of the
book's statements to discard. Keeping both makes every such
contradiction queryable, which turns a problem into an inventory.

**Tests that nothing runs get run, and the red is accepted.** The
`full-macos` lane now executes the two engine tests no lane covered.
One passes; the other fails three locomotion cases and will stay red
until the responsible work lands. The lane is advisory, so the red is
loud and blocks nobody. Hiding a failure until someone gets to it is
how it stayed hidden.

---

## Known thin ice

Written down because a protocol that hides its own weak points is
marketing.

- **R3 and R7 are ungated.** They are the two rules the next phase of
  work exists to make checkable, and until then they are intentions.
- **L1 is one implementation deep.** It has absorbed one chapter of one
  book. Every claim about what generalises is a prediction.
- **The L7 boundary is asserted, not proved.** No second game has
  replaced that layer, so "a different game touches nothing below it"
  has never been tested by anyone doing it.
- **R5 has no general gate.** Fail-closed is checked per site, so a new
  path that quietly defaults would pass everything.
- **Images are absent from L0.** The address grammar reserves room for
  a region; nothing implements it.
