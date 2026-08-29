# Voyager: game design document

**The one place Voyager's design lives.** Intent and pillars belong
here; the rules the game runs belong in the book at
`corpora/voyager-book/`; what the graph holds is what actually plays.
When this document and the book disagree, whichever the owner ruled
on later wins, and the other has a bug.

This is a ledger. Entries are dated, appended, and superseded in
place, never deleted.

---

## 1. The pitch (2026-08-29)

A character generator, and later a game, where the rulebook is ours:
written by the makers in their own words, ingested by the same
machinery that absorbed a published SRD, and played from the graph.
The loop is the design: write narrative, ingest it, play it, and let
play teach us what to write next. The published book this grew from
remains vendored as inspiration and quarry; its structures (terms,
fixed career tables as the whole of life) are explicitly not ours.

## 2. Pillars (2026-08-29)

**We create based on our narrative.** The book is the source. No
rule enters the game except through the book and its ingestion.
Nothing the code needs may exist only in code.

**Equal opportunity, tilted luck.** Different lives get different
kinds of doors, never different amounts of fairness. Luck is worked
for; an option that arrives free is charity, and charity is boring.

**Risk you cannot see is not a decision.** An open moment states
what each door risks and reaches for. Blindness exists in this game,
but it is stage, a property of the character, never a property of
the UI.

**The engine rolls, the model proposes.** The referee authors
options and narrates; every die is the engine's; the model never
decides an outcome it could roll for.

**A life is seasons broken by moments.** Quiet accumulation,
punctuated by rare situations that demand action and reveal
character. Chapter One of the book is the canonical statement.

**Stage is typed.** Experience is the shape of what was lived, per
kind of moment, never a scalar. You are a veteran of some kinds of
trouble and a rookie before the rest, simultaneously.

## 3. Mechanics settled so far (2026-08-29)

- Careers have no terms. A career is a tilt: a weighting over which
  kinds of moments life sends.
- Five kinds of moments: Violence, Judgment, Discovery, Standing,
  Loss. Canon as a starter, owned by the book, and a closed
  vocabulary: on ingestion this becomes a schema enum.
- Moment resolution: below your stage in its kind, a moment arrives
  as pre-rolled fate; at or above it, as doors with visible odds.
  Beside every door, the open one: free-form input the referee
  translates into a throw the rules already hold. The translation
  invents nothing and always carries a cost.
- Seasons raise what you bring; moments raise what you see.
- All tuning numbers (how hard fate tilts by stage, door counts) are
  graph data, never code.

## 4. Open questions

Numbered, dated, answered in place.

1. (2026-08-29) Can one moment carry two kinds at once, or does the
   fiercest kind claim it? Mirrored in the book's Unsettled section.
2. (2026-08-29) Can the open door drag a moment from one kind to
   another? Mirrored in the book's Unsettled section.
3. (2026-08-29) What exactly is a stage rating per kind: a count of
   moments lived, a graded rating, or edges to named thresholds?
   Blocks the chargen slice that needs perception checks.
4. (2026-08-29) What does a season cost and yield, in numbers the
   book must fix?
5. (2026-08-29) Foresight: stage made trainable as a skill, or left
   emergent? Parked from the earlier design notes.
