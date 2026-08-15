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

**Crosses out**: typed claims, each carrying the address it came from,
plus exceptions for what could not be expressed.

**A replacement must honour**: structure judged, content copied, and
every claim addressed. A deterministic parser and a pair of models with
an arbiter are both conformant here; we have used both, and the ledger
records why the second replaced the first.

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
