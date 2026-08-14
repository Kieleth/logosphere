# Rules as data: absorbing a rulebook so it checks itself

For anyone putting an existing body of rules into a game — a tabletop
RPG, a board game, a simulation with a published spec. The engine
provides the mechanisms; this is what we learned using them on the
Cepheus Engine SRD, including the parts we got wrong first.

The claim is narrow and testable: **if a rule lives in the graph, the
graph can check that the game obeys it.** If the rule lives in your
procedure code, nothing can check it but a test someone remembered to
write.

---

## The line that matters

The doctrine is one sentence in `schema/packs/rulebook.yaml`: *"Logic
stays in code. A ProcedureStep names a primitive, it never computes."*

That sentence is easy to read the convenient way, and we did for a
while. It licenses **execution machinery** in code. It does not license
the book's **assertions** in code.

| The book asserts | Where it belongs |
|---|---|
| "A natural 2 is always a failure" | data |
| "characters of rank O5 or O6 gain +1" | data |
| "Increase your age by 4 years" | data |
| rolling 2D6 and comparing to a target | code |
| presenting a choice and resuming a step | code |
| looping until benefit rolls are spent | code |

A useful test when you are unsure: **would a referee house-ruling this
have to recompile?** If yes, it is in the wrong place.

A sharper one, which caught four of ours: **is this a number the
procedure TESTS, or a number the book APPLIES?** A threshold the
procedure branches on (a term cap, a roll limit) is honestly a
constant. A number the book does to a character (age +4, restore to 1,
skill at level 0) is an *outcome*, and dressing it as a constant leaves
the rule in code while only the value moves.

---

## What "in the graph" has to mean

Putting a number in the graph is not enough. Three things have to hold
or you have moved the problem rather than solved it.

**1. The rule is reached through the schema, never by name.**

```cpp
find_named(kg, "RollableTable", "Effects of Aging")   // no
step_table(kg, context.step, error)                   // yes
```

The first is a rule about English. Rename the table in your seed and it
breaks silently; translate the book and it cannot work at all. We had
three of those and a `"Cash Benefits"` substring match, and the test
that proved them gone renames every table in the graph and asserts the
same lives come out.

**2. The value cites the text that states it.**

Every rule entity carries the file, the section and the verbatim quote
it came from, and a verifier resolves the quote back into the source.
Numbers are checked against their own citation: a slot holding 8 must
find an 8 in the words that entity quotes.

Books do not always cooperate, so the check has three doors:

- digits — `"End 5+"` proves a target of 5
- number words — `"Reduce three physical characteristics"` proves 3
- `implied_by` — `"An additional benefit is gained"` proves 1, carried
  by the indefinite article and by no token at all

The third is a *reading*, so it names the words it reads, and the
phrase must appear in the quote. A marker invented to smuggle a number
past the check fails exactly as a wrong digit does.

**3. Missing data stops the run.**

Never default. A rule whose constant is absent must fail loudly, not
quietly behave as though the number were zero. We have two mechanisms
worth copying: a hard failure when a `RuleConstant` is missing, and
`miss_is_nothing` for the honest case where a book states a bonus for
three ranks of seven and says nothing about the rest — the silence *is*
the rule, and the table says so rather than a reader guessing.

---

## Generators own seeds

A seed nobody generates rots. We hit this twice, in both directions.

- A generated seed that two PRs hand-edited. The extractor fell behind
  the file it owned, and regenerating would have silently dropped
  fifteen ops. The file's own indentation was the tell: it stopped
  matching what the generator writes.
- A 604-op seed with no generator at all, which could not be re-derived
  when the source changed and sat one typo from being wrong.

The fix is mechanical, not cultural. Each generated seed declares
`generated_by`; one script regenerates all of them; CI runs it and
fails on any diff. Prove the gate works by committing a hand-edit on a
scratch branch and watching the build go red — an unproven gate is a
comment.

Splitting the work is the other half:

- **Transcription is mechanical.** A model that retypes a number
  produces a rule that is wrong while the prose still reads fine. Copy
  bytes; address cells by (table, row, column).
- **Meaning is judgement.** Deciding that `"Low Passage"` is a
  possession and `"+1 Int"` is a characteristic is interpretation, and
  interpretation checked only by the regex that produced it is not
  checked.
- **Readings are declared, not inferred.** Where a source says
  `"Aerospace System Defense (Planetary Air Force)"` in one table and
  `"Aerospace Defense"` in another, transcribe both and declare that
  they are one career, in the script, where someone can disagree.

---

## Making the rules test themselves

This is the part that repays everything above.

Every roll records the rule entity that produced it — not a purpose
string like `"survival"`, which reads well and resolves to nothing, but
the id of the check or table being executed. With that link, the dice
journal plus the graph is enough to re-derive **what the rule
permitted** and compare it to what happened:

- a throw's total against its own dice, modifier table and target
- a table roll against the band of the row it selected
- a gate against the character who passed it
- a career's basic training against the skills actually held

No test is written for any individual rule. A rule added to the graph
tomorrow is checked automatically, because the checker reads the graph
rather than a list someone maintained. That is the property a
hand-written checklist can never have.

---

## Where a model helps, and where it does not

We use one in three places and refuse it in a fourth.

**Judging meaning during extraction.** A model classifies each distinct
cell value independently of the extractor, disagreement fails the run,
and agreement is written to an audit file a test compares against the
seed. A value the audit has never seen turns the suite red.

**Sweeping generated lives.** Deterministic tests catch what someone
thought of. A Monte Carlo sweep generates lives nobody wrote and asks
whether the book allows them — the only way to find a rule wrong in a
direction you never considered.

**Answering rules the book leaves open.** A referee choice is a seam
(`ChoiceResolver`, `AttributeSelector`), and a model can sit in it.
There is deliberately no default: a rule with nobody to answer it stops
the run, because picking the first option silently collapses every fork
the book prints.

**Arithmetic.** Do not. Every age finding across two sweeps was the
model double-counting, and arithmetic over a printed timeline is what a
deterministic test does perfectly and for free.

Two rules for the ones you do use:

**Findings are candidates, not verdicts.** Ours have been right about a
class of aborts affecting 8% of runs and wrong about a rule it
confidently misread. Only checking separated them.

**Make checking cheap or nobody does it.** Every generated life is a
seed, so a finding arrives as a number and replays exactly, for
nothing. Verification costs a rerun instead of an argument — which is
what makes it honest to accept a noisy discovery pass at all.

---

## The failure modes, so you can skip them

Every one of these is ours.

**Vocabulary exists and gets bypassed.** Four times we wrote code for
something the pack already expressed: `outcome` and `table` were slots
that simply were not on `ProcedureStep`; `AttributeGroup` existed while
four separate places wrote out six characteristics by hand;
`EnsureSkillLevel` was wired into the executor with a description
reading *"Basic training at Level 0 is the first instance"* and had
none. **Before adding vocabulary, grep for it.** We added a duplicate
`table:` slot to a YAML file that already had one.

**A class per rule.** If a proposed class name contains a noun from
your game — Rank, Benefit, Term — it is policy in the engine pack. Ask
whether it would survive absorbing a second, unrelated book. The rule
of two is the guard: no meta-class without two instances from real
text, quoted.

**Tests that swallow failures.** `if (!run_chargen(...)) continue;` hid
8% of runs dying on rules the book prints plainly, while the sweep
reported the survivors as though nothing were wrong. A life that cannot
be generated is a finding, not a seed to skip.

**Asserting presence instead of movement.** A test that a constant
loads proves nothing. Assert that *changing it changes the run* — ours
caught two constants nothing read, including one that sat cited and
unused for days.

**Refactoring without a contract test first.** Write what the rule owes
the *player* — which skills, at what level, read from the graph —
before touching how it is delivered. Then the implementation can change
underneath and the numbers must not move.

---

## Reading order

- [RPG_MODULE.md](RPG_MODULE.md) — the design record and decision log
- [SOURCE_LOCATORS.md](SOURCE_LOCATORS.md) — how a citation addresses a cell
- [RECORD_AND_REPLAY.md](RECORD_AND_REPLAY.md) — headless runs, tapes, fuzzing
- [GAME_LAYER.md](GAME_LAYER.md) — the game-facing API
- `schema/packs/rulebook.yaml` — the meta-ontology, with every class
  carrying the instance that earned it
- `examples/logovger/` — the whole thing working, extractors included
