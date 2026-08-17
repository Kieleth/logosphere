# The book's journey: the vision against what runs

Working document. Iterate on it, extend it, argue with it.

The question: we have a vision for how a rulebook becomes a playable
game, and we have a pipeline that has already absorbed one chapter. Are
they the same thing? Where they differ, does the vision survive contact
with real pages?

Method: take five real pieces of the book, chosen to span the range from
easy to impossible, and walk each one through both pipelines step by
step. No speculation. Every claim below points at a file, a line, or a
number that can be checked.

Measured 2026-08-14 against commit `60b6932`.

---

## What this document got wrong about itself

**Added 2026-08-15 14:51 UTC, after the design record was read
properly.**

This was written as discovery. It was not. Every measurement in it is
new and stands; the DIAGNOSIS was already on file and had been for
days, in `RPG_MODULE.md`, which this document referenced nowhere.

Two things were already decided when it was written:

- **2026-08-09**, the decision ladder: tier 0 engine rules, tier 1
  prepared content, tier 2 a Haiku-class fast model, tier 3 a
  Sonnet-class referee. The model tiering this document reasons its way
  toward was already the owner's design.
- **2026-08-11**, "Ingestion: who reads, who judges, who checks":
  transcription mechanical, meaning judged, judgement audited. That
  `INJURY_ROWS` holds a hand-written reading of the prose is a recorded
  decision with stated reasons, not an unnoticed gap.

So sections 2 and 4 read as findings and are better read as
measurements of a known position. The numbers were worth taking. The
surprise was not.

**And the position they measured has since died.** On 2026-08-15 the
2026-08-11 decision was partly superseded: transcription moved back to
a model reading through a tool that returns exact bytes at an address,
because a deterministic parser leaves 8.5% of the SRD's pipe-prefixed
lines unaccounted for and fails by silent corruption rather than honest
loss. The evidence for that came from section 3 below. So the review
did its job. It just did not know it was arguing with a decision rather
than reporting a discovery.

Read it alongside `docs/REFLECTION_PROTOCOL.md`, where the layers and
their contracts now live, and the ledger in `RPG_MODULE.md`, where a
decision's current state is recorded. Where this document disagrees
with either, they win and this one is stale.

---

## 1. The vision, split into stages

Stated so each stage can be tested separately.

| # | Stage | What it produces |
|---|---|---|
| V1 | Learn the ontology of the book and the game | A schema: the type system for what the book contains |
| V2 | Send pairs of small models, each loaded with the game ontology and the slice of the engine ontology it needs | Two independent readings of the same passage |
| V3 | Extract subject-predicate-object statements against that ontology | Triples, typed, cited |
| V4 | Raise an exception for anything that cannot be ingested | A list of what the ontology cannot yet say |
| V5 | Write a canonical ledger of the book | One transactional record of the whole text, ontology-shaped |
| V6 | Transform the ledger into seeds | Loadable units of knowledge |
| V7 | Load seeds into the KG | A graph the engine can query |
| V8 | Engine runs the graph | Game dynamics |

Two properties matter and are worth naming now, because the review turns
on them:

- **Generality.** A new chapter should cost model calls, not new code.
- **Honesty.** Anything the ontology cannot express should be loud, not
  absent.

---

## 2. What runs today

| # | Vision | Reality | Evidence |
|---|---|---|---|
| V1 | Learn the ontology | **Real.** Hand-authored LinkML, generated to C++ | `examples/logovger/schema/cepheus/*.yaml` |
| V2 | Paired models, ontology-loaded | **Absent.** Hand-written Python parses pipe tables. No model. No ontology loaded: the four extractors import `json`, `re`, `os`, `urllib.parse` and each other, and nothing else | `examples/logovger/tools/extract_*.py` |
| V3 | Ontology-typed triples | **Partly real.** Seed ops are `create_entity` / `set_relation`, which is triple-shaped. But the type names are Python string literals, not read from the schema | `extract_career_tables.py:296` |
| V4 | Exceptions for the un-ingestible | **Absent as a mechanism.** `unmodelled` exists as a slot and is filled by hand for cases a person already noticed. Nothing detects an unnoticed one | `extract_shared_tables.py:123` |
| V5 | Canonical ledger of the book | **Absent.** No intermediate representation exists. `read_tables()` returns in-memory dicts, used and discarded inside one process | `extract_career_tables.py:183` |
| V6 | Ledger to seeds | Not applicable: extraction emits seeds directly | |
| V7 | Seeds to KG | **Real and strong.** Parse, verify against the source text, load atomically | `src/kg/seed_verifier.cpp` |
| V8 | Engine runs the graph | **Real.** Procedure steps, executor, and now a coverage gate | `examples/logovger/chargen/` |

The model does appear, in one place, and it is worth being precise about
its size. `audit_classifications.py` asks Haiku to classify what a cell
means, independently of the extractor, and disagreement fails the run.
Its scope:

- 7 table families out of the chapter's tables, listed by name in
  `AUDITED_TABLES`. "Ranks and Skills" is excluded on purpose.
- 23 distinct value strings, deduplicated across all cells:
  `checked: 23, agreed: 23` in `seeds/classification_audit.json`.
- Not run by CI. Nothing in `.github/workflows/` references it. CI runs
  a C++ test that checks the shipped audit still matches the shipped
  seed, which is a different thing.

So the model judges 23 strings, once, when a person remembers to run it.

**Headline coverage.** The book is 715,234 bytes across 32 markdown
files. Seeds cite two of them: `book1/character-creation.md` (2,984 ops)
and `book1/skills.md` (78). Inside the one chapter that is absorbed, 16
of its 54 sections are cited, and 1,695 of the 2,984 ops come from a
single section, "Career Tables". Absorption today means tables.

---

## 3. Five journeys

### J1. A plain table cell

**Book**, `character-creation.md:482`:

```
| Cash Benefits | Athlete | Aerospace | Agent | Barbarian | ... |
| 1 | 2000 | 1000 | 1000 | 0 | 1000 | 1000 |
```

**Vision.** A model pair reads the cell, agrees it is a money amount,
emits `(athlete_cash_1, grants_money, 2000)` with the cell address, and
the ledger records it.

**Today.** The extractor finds the pipe table, takes column 1 row 1,
recognises digits as money, emits a `GainFixedMoney` entity carrying
`source_table`, `source_row`, `source_column` and the verbatim cell. The
verifier reopens the book at that address and proves the number appears
in the quoted text. The value string "2000" is one of the 23 the model
audit has classified as MONEY.

**Divergence.** None that matters. Both pipelines get the same answer,
and today's has the stronger proof, because the citation is byte-exact
rather than retyped.

**Verdict.** The vision adds nothing here. Deterministic parsing is
better for this shape and should stay.

---

### J2. A cell holding two different things

**Book**, `character-creation.md:466`:

```
| 0 | \[Athletics-1\] | Airman \[Aircraft-1\] | Agent \[Streetwise-1\] | ... |
```

Each cell is a rank title, or a skill grant, or both. Aerospace gives
the title "Airman" and the skill Aircraft at level 1. Athlete gives a
skill and no title.

**Vision.** The pair reads the cell against an ontology that has both
`ProgressionStep.step_title` and a grant, splits it, and emits two
statements. Where the two models disagree on the split, it escalates.

**Today.** The extractor splits on the bracket convention and emits a
`ProgressionStep` with `step_title` plus a `grants` pointer. It works.
The model audit never sees it: `AUDITED_TABLES` excludes "Ranks and
Skills" with a written reason, that forcing a title-and-grant cell into
one category would answer the question badly.

**Divergence.** The exclusion is honest about the audit's shape but
leaves the compound cells with exactly one reading, the extractor's,
checked by nobody. This is the table where the rank 0 bug lived for the
life of the feature.

Being precise about what would and would not have helped: a second
reading of the cell would **not** have found the rank 0 bug. The cell
was transcribed correctly. What was missing was that no code read the
result, which is a different question, now answered by the coverage gate
in `test_chargen.cpp`. The exclusion is still a real gap, just not the
one that caused that bug.

**Verdict.** Vision wins on this shape. A compound cell is interpretation
and deserves two readings.

---

### J3. A rule whose meaning is a Python literal

**Book**, `character-creation.md:358`:

```
| 1 | Nearly killed. Reduce one physical characteristic by 1D6, reduce
      both other physical characteristics by 2 (or one of them by 4). |
```

**Vision.** The pair parses the sentence into clauses, emits the ones the
ontology can express, and raises an exception naming the clause it
cannot: the second reduction depends on which characteristic the first
one hit, and no outcome can refer to a choice made earlier in its own
sequence.

**Today**, `extract_shared_tables.py:122`:

```python
INJURY_ROWS = {
    "1": ([("group_dice", "physical", 1, "1D6")],
          "The second clause, 'reduce both other physical "
          "characteristics by 2 (or one of them by 4)', depends on "
          "which characteristic the first clause reduced. ..."),
```

The prose is never parsed. A person read it, decided what it means, and
typed both the clause list and the note into the extractor. The verifier
then proves the numbers in the emitted entity appear in the quoted
sentence, which is a real check on transcription and no check at all on
interpretation.

**Divergence.** This is the deepest one. The load-bearing step, turning
English into ontology, is a human writing Python constants. Every table
family has its own dict of them.

The honest counterpoint: the outcome here is *correct*, and the gap it
could not express is *recorded* in `unmodelled` rather than hidden. That
is better than most pipelines manage. But it is correct because a careful
person did it by hand, and it does not scale past the chapters that
person has read.

**Verdict.** Vision wins decisively. This is the shape that makes the
vision worth building.

---

### J4. A sentence carrying a number it never writes

**Book**, `character-creation.md:434`:

> Characters who end their careers receive one benefit per term served
> in which they did not lose benefits. An additional benefit is gained
> if the character held rank O4, and two for rank O5. A character with
> rank O6 gains three extra benefits.

**Vision.** The pair extracts a lookup keyed by rank, notices that the
first sentence states a rate rather than a value, and raises it as
un-ingestible.

**Today**, `extract_careers.py:126`:

```python
RANK_BONUS_ROWS = [(4, 1, "An additional benefit is gained"), (5, 2, None), (6, 3, None)]
RANK_BONUS_UNMODELLED = (
    "Only the rank half of this sentence is in the graph. The base "
    "count ... is a rate over a filtered count, which needs arithmetic "
    "and a count the rule language does not yet have, so the procedure "
    "still does it.")
```

The rank half becomes a table. The rate half stays in C++, and the entity
says so. The number 1 for rank O4 is never written in the book, so the
seed proves it with `implied_by`: the phrase "An additional benefit is
gained" must appear in the quote, which makes an invented justification
fail the same way a wrong digit does.

**Divergence.** Same as J3 in kind. The split between "modelled" and
"the procedure still does it" was decided by a person, and it is the
right split. Nothing would notice if a future sentence got the split
wrong, because nothing else reads the sentence.

`implied_by` is genuinely good and the vision should keep it.

**Verdict.** Vision wins on extraction. Today wins on proof. Both are
needed and they are not in tension.

---

### J5. A section that never entered

**Book**, `character-creation.md:404`:

> ### Anagathics
>
> While using anagathic drugs, the character effectively does not age
> ... add the number of terms since the character started taking
> anagathics as a positive Dice Modifier to rolls on the aging table.
> ... the character must make a second Survival check if he passes his
> first Survival check in a term. ... The drugs cost 1D6x2,500 Credits
> for each term.

Three mechanics: a modifier on an existing table, an extra check, a
recurring cost.

**Vision.** The pair reads it, finds no ontology term for "a modifier
that grows with the number of terms since an event", and raises an
exception. It appears in the ledger as an un-ingestible passage with a
reason. Somebody sees a list of them.

**Today.** Nothing reads this section. No extractor targets it, no seed
cites it, no test mentions it. The `README.md` lists anagathics under
"not yet absorbed", which is a person keeping a list by hand in prose.

Same for the other 37 uncited sections in this chapter, and for the 30
uncited files in the rest of the book.

**Divergence.** Total. The vision produces a queue of work with reasons.
Today produces silence, plus a hand-maintained sentence in a README.

**Verdict.** Vision wins, and this is the crack with the worst
consequences, because it is the one that hides its own size. An absent
rule and an unabsorbed rule look identical from inside the repo.

---

## 4. The cracks, worst first

1. **Silence is the failure mode.** Un-ingested content leaves no trace.
   There is no queue, no count, no exception. 16 of 54 sections in the
   one absorbed chapter, 2 of 32 files overall, and the only record of
   what is missing is English in a README. (J5)

2. **Interpretation is hand-written code.** `INJURY_ROWS`,
   `RANK_BONUS_ROWS`, `DRAFT_CAREERS` and their siblings are a person's
   reading of the prose, typed as Python literals. The verifier proves
   the transcription and never the interpretation. (J3, J4)

3. **There is no canonical ledger.** Seeds are the only representation
   of the book, and they are already the loadable form. There is nowhere
   to put a statement that is true about the book but not yet loadable,
   which is why point 1 has nowhere to write to. These two cracks are
   the same crack seen from two sides.

4. **The extractor does not know the ontology.** Entity type names are
   string literals in Python. Renaming a class in the schema does not
   break extraction; it produces seeds that fail at load, later, with a
   worse message. The schema is the glossary, and the tool that writes
   the data has never read it.

5. **No pairing, and the single reading is narrow.** One model, 23
   value strings, 7 table families, "Ranks and Skills" excluded, and no
   CI runs it. What exists is good and is roughly one percent of what
   the vision describes.

6. **Every new table family costs new Python.** `extract_careers.py`
   alone carries `SECTION`, `OWNED_CHECKS`, `CAREER_CITATION_ROW`,
   `SKILL_LEVEL_SECTION`, `DRAFT_TABLE`, `DRAFT_COLUMN`,
   `RANK_BONUS_ROWS`, `DM_TABLE` and more, all naming specific headings
   in one chapter. Book 2 and book 3 would each need their own. This is
   the generality property, and it currently fails.

---

## 5. What today does better, and must survive

Do not rebuild these. A model-first pipeline that loses them is worse
than what we have.

- **Byte-exact citation.** Copy, never retype. A model that retypes a
  number produces a rule that is wrong while the prose still reads fine.
- **The value verifier.** Numbers are proved against the sentence that
  states them, with `implied_by` for the ones the text asserts without
  writing.
- **Seeds as transactions**, with invariants and a source commit pin.
- **The load path.** Verify, then load atomically, refusing on any
  violation.
- **The consequence gate.** Absorbed rules that no life ever receives
  now fail the build.

---

## 6. Does the vision survive the five journeys?

Yes, with one correction and one addition.

- **J1 says the vision must not be total.** A pipe table of integers
  does not need two models. Deterministic parsing plus verification is
  cheaper, faster and stronger. The model belongs where judgement is
  required, and a plain cell requires none. Route by shape, not by
  policy.
- **J3 and J4 say the vision is necessary.** Meaning as a hand-written
  Python literal is the real ceiling on absorbing the rest of the book.
- **J5 says the ledger is the keystone, not a stage.** The reason to
  build a canonical intermediate is not that seeds are wrong. It is that
  there is currently nowhere to record a true statement about the book
  that cannot yet be loaded, and so nothing knows what is missing.
- **J2 says the pairing has to cover compound cells**, which are exactly
  the ones the current audit excludes.

The correction to the vision as stated: the ledger is not a step between
extraction and seeds. It is the thing that makes the pipeline honest,
because it can hold what the seeds cannot.

---

## 7. Open questions

This review is historical. Later decisions are marked here so it does
not keep reopening settled questions. The canonical decision record is
`RPG_MODULE.md`; the active implementation checklist is
`todo_plans/LOGOVGER_INGESTION_LEDGER_ROADMAP.md`.

1. **DECIDED 2026-08-16:** the ledger holds the whole book. "No rule
   content" is a visible judgement, never a pre-ledger filter.
   **AMENDED 2026-08-17:** option 4 gives that judgement a mechanically
   enumerable source-coverage row and records each distinct meaning as
   an atomic child claim. One source unit may yield zero, one, or many
   claims, so compound passages no longer force one misleading status.
   **GRAIN DECIDED 2026-08-17:** coverage units are atomic source leaves:
   headings, prose sentences, table cells, and sentences within list
   items, with opaque leaves for unclassified source content. Paragraph,
   row, section, and file grouping may be derived later but never replace
   or hide the leaf record.
2. What does an un-ingestible exception look like when the reason is
   "the ontology cannot say this yet" versus "the rule language has no
   operator for this"? J4 is the second kind and today they are the same
   slot.
3. Do paired models disagree by construction (different prompts, one
   adversarial) or by sampling the same prompt twice?
4. When extraction becomes model-driven, what keeps the citation
   byte-exact? The current guarantee comes from the extractor copying
   bytes it already has.
5. Does the ledger get versioned against the source commit, so
   re-pinning the SRD shows what changed underneath it?
