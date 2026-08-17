# The RPG Module: rulebooks as engine domain

_Design record, started 2026-08-09. Everything here was decided in
owner discussion; sections marked DESIGN are agreed direction not yet
built; OPEN items are undecided. This document is the canonical capture:
if the conversation and this file disagree, fix the file._

## The one-sentence version

The engine learns what a rulebook IS; a game supplies which rulebook it
plays by, as data; a referee reads rules and world from the same knowledge
graph and writes through the same validated grammar as everything else.

## Why this is engine domain

The concepts below (tables you roll on, checks with targets, procedures
with steps, constants buried in prose, referee judgment points) are not
Traveller concepts. Any tabletop-derived ruleset is made of them,
including ones a game invents for itself. By the engine's own boundary
rule, "would it make sense in a farming game and a combat game equally":
it would. First consumer: logovger (issue #48).

## The stack

```
ENGINE   rulebook meta-ontology  what any book of rules is made of
         ingestion pipeline      LLM extracts, machine verifies
         rule executor           rolls checks, rolls tables,
                                 applies outcomes via registry
GAME     cepheus layer           what THIS book says: per-chapter
(logovger)                      schema packs + rule instances in
                                 the KG. Nothing of ours in it.
         voyager layer           what OUR game changes and adds.
                                 Cherry-picks cepheus by import.
                                 Sole home of deviations.
```

Layer separation is 100% by owner decision: the diff between cepheus
and voyager is the game's design document.

## Rule origin, growth contexts, and forks

Built 2026-08-10. Origin and learning scope are graph data, not one
linear `layer` field.

`KnowledgeContext` is the abstract context concept. Seed loading creates
and reuses one `SourceLayerContext` for the envelope layer and one
`SourceDocumentContext` for the exact layer, source file, and source
commit. Every seeded `Cited` entity receives a required
`origin_context` reference to that document context. Seed content cannot
create these engine-owned contexts or supply its own origin.

`RuntimeContext` represents places where rules and knowledge can grow.
Its required `context_kind` is ontology and KG data rather than an
engine enum. Initial intended kinds include session, campaign, user,
group, and global. Context hierarchy, applicability resolution, and
promotion between learning scopes remain later work.

Source contexts and every entity originating in one are immutable on the
validated write path. Origin and lineage properties are creation-only.
`fork_rule` implements copy-on-write: it copies the source entity's
ordinary properties and outgoing relations, applies explicit overrides,
sets the destination RuntimeContext as origin, records `fork_of`, and
commits the complete fork atomically. It never edits the source.

The referee may create rules in a RuntimeContext or request forks using
ontology shapes and existing KG content. It cannot add executable
operators. A future DM-authority mechanism may add a higher-authority
override rule, but that authority model is deliberately not implemented
here.

## Ontology-native rule language (DESIGN)

The general rule mechanism is an engine-level, ontology-native language.
Ontology classes define its grammar and type system; KG entities are programs;
the evaluator validates and executes those programs. Logovger's modifier
algebra is its first consumer, not a separate game DSL.

Rules belong to explicit KnowledgeContexts and declare applicability through
declarative graph predicates. Evaluation receives active contexts explicitly
and never discovers rules by scanning every context in the KG.

Expression evaluation is pure. Explicitly invoked action rules may construct
typed Outcome plans, but only OutcomeExecutor validates and commits them. A
predicate becoming true does not cause an implicit graph mutation.

The runtime ontology registry remains the source of truth. Once all ontology
layers are composed, the engine will materialize an immutable meta-graph with
one canonical entity for each class, property, and relation. Rule programs use
typed references to those meta-entities instead of raw property or relation
names. Runtime authors may compose existing operators but cannot manufacture
new executable semantics.

The reusable application and game contract, validation requirements, open
decisions, and TDD order live in [RULE_LANGUAGE.md](RULE_LANGUAGE.md).

### Shelob is outside this architecture

Shelob is the server deployment library. It is not the referee, the
extractor, the rules engine, or a gameplay component. Rule ingestion and
execution must not assign it any of those responsibilities. The referee
model, engine runtime, Logovger data packs, and Shelob are separate systems.

## The meta-ontology (DESIGN)

Engine schema pack, `schema/packs/rulebook.yaml`, opt-in like earth and
space. Classes earn existence by the RULE OF TWO: no meta-class until
two concrete instances from real text need it. Logic stays in code:
a ProcedureStep names a primitive, it never computes.

Shipped in PR2 as built (2026-08-09); the table below is the pack as
it exists. Contract test: `tests/test_rulebook_pack.cpp`, which
instantiates chapter-1 instances of every class and string-matches
every citation into the vendored SRD.

| Class | Models | First instances (Cepheus ch. 1) |
|---|---|---|
| Cited (mixin) | source file, section, verbatim quote on everything | every row below |
| KnowledgeContext / SourceLayerContext / SourceDocumentContext / RuntimeContext | explicit origin and growth scopes, plus copy-on-write lineage | Cepheus seed layers and documents; session, campaign, user, group, and global runtime scopes |
| DiceExpression | count, sides, modifier, multiplier; mirrors the engine dice struct field for field | 2D6 characteristics, 1D6x10000 medical bill |
| TaskCheck | attribute ref, target, dice ref | the book's "Int 8+" definition, Athlete survival Dex 5+ |
| RollableTable / TableEntry | dice ref; rows as HAS_PART, each claiming a roll band (min/max) | Survival Mishaps 1D6, Aging Table 2D6 |
| LookupTable / LookupEntry | state-keyed tables: a table declares one concrete game row type; the matching row is the typed result | CharacteristicModifierEntry, DifficultyEntry |
| Outcome (abstract) + EnsureSkillLevel, AdvanceSkill, ModifyAttribute, GainFixedMoney, GainRolledMoney, GrantTableRoll, NoEffect, OutcomeSequence / OutcomeStep, OutcomeChoice / OutcomeOption | typed consequence kinds, explicit no-op, ordered composition, and suspending choices; one executor handler per concrete effect class | basic training at Level 0, repeated training, injury -2 Str, fixed or rolled money, commission extra roll, Injury 6, Mishap 3 |
| RuleConstant | the number + the quote that fixes it | age of majority 18, prior-career DM-2 |
| Procedure / ProcedureStep / StepRoute | ordered steps naming code primitives; gotos as route entities (label -> next step ref) | the chargen checklist; survival-fail and natural-12 routes |
| JudgmentPoint | "the Referee may..." spots, prompt text | the 7-term cap, failed-survival optional rule |

## No magic strings (decided 2026-08-09)

Captured data contains POINTERS TO ONTOLOGY ELEMENTS and KG
EXPRESSIONS, never bare names. `Athletics` is a reference that must
resolve to a skill defined in the cepheus skills pack; `+1 Int` is a
typed ModifyAttribute whose attribute ref must resolve against the
target class's declared slots. The verifier rejects any reference that
does not resolve. Verbatim source text rides alongside every typed
value for audit (the Cited mixin):

```yaml
- rank: 0
  bonus:                # an AdvanceSkill entity
    skill: cepheus:skill/athletics
    initial_skill_level: 1
    existing_skill_delta: 1
    source_quote: "[Athletics-1]"
```

Consequence, resolved decision: SKILLS ARE ONTOLOGY ELEMENTS, not
strings and not a bare enum, because rules must point at them.

## Typed table results (decided 2026-08-09, option 3)

Tables have two contracts because the book uses them for two different
jobs.

A lookup table returns information. Its matching row is the result. The
engine-level `LookupEntry` is abstract and carries only its key band. Each
side is either a finite `key_min` or `key_max`, or the matching explicit
`key_min_unbounded=true` or `key_max_unbounded=true` flag. Omission is not
unboundedness, and a finite bound plus its true flag is ambiguous and
invalid. Each game declares concrete row subclasses with the columns its
book prints. Cepheus currently proves two shapes:

- `CharacteristicModifierEntry`: pseudohex lower and upper symbols plus
  characteristic modifier.
- `DifficultyEntry`: difficulty name plus difficulty modifier.

Every `LookupTable` requires `entry_type`, a resolved ontology name. The
verifier rejects unknown types, abstract types, types outside
`LookupEntry`, empty tables, and attached rows that do not conform to the
declared type. `LookupEntry.outcome` was removed. There is no scalar union,
generic result wrapper, or legacy fallback.

`LookupTableSelector` validates the complete live table before returning a
row: table and declared type, every attached row type, every explicit bound,
finite band order, and overlaps. It returns the exact typed row and the key;
it does not interpret game result columns. An uncovered requested key fails
without a default. Seed verification calls the same preflight. A
`band_coverage` invariant may use JSON null for an explicitly unbounded end,
such as `[0, null]`.

A rollable table returns a consequence. Every `TableEntry` requires one
typed root `outcome`. `NoEffect` records that a cited row deliberately
changes no state. Several consequences use one `OutcomeSequence` root with
`OutcomeStep` parts. Each step has an explicit zero-based `step_index` and
a typed child `outcome`; the verifier rejects empty sequences, wrong part
types, duplicate indices, gaps, and indices that do not start at zero.
Relation insertion order is not semantics.

Sequence execution is atomic. Handlers build a complete plan without
mutating the graph. The executor validates and commits its KG operations as
one batch, while dice rolls remain private until the same execution commits.
A failure restores KG state, dice streams, the dice journal, and publishes no
mutation or dice events.

Choices are not sequences. `OutcomeChoice` carries an explicit `player`,
`referee`, or `procedure` authority and ordered `OutcomeOption` parts. An
unresolved choice suspends the complete root before mutation. Execution then
restarts from the root with an explicit selected option.

## Outcomes are KG-ops (the keystone)

The engine already has one validated write grammar: KG-ops
(parse -> validate against ontology -> apply), used by the Logogenesis
materializer and specified for the Director. Decision: the BOOK's
outcomes are parameterized KG-ops, and the referee's writes are KG-ops.
One write path for the book, the referee, and the game. An Outcome the
validator rejects cannot ship, whether a Haiku extracted it from a
table or an Opus improvised it mid-session.

Refined 2026-08-09 (PR2 ruling): outcome DATA is typed, not stringly.
Outcome is an abstract class; each consequence kind is a concrete
subclass with typed slots. Skill consequences distinguish minimum-level
guarantees from advancement, and money consequences distinguish fixed from
rolled amounts while sharing generic per-currency balance state. Instances
are ordinary KG entities validated at creation, and the executor pairs one
registered handler per exact concrete subclass.
The handler is what emits the actual KG-ops; the keystone holds, the
op-template-in-a-string-slot alternative was rejected. Games add
outcome kinds by declaring a subclass in their own pack and
registering a handler.

## Ingestion: LLM extracts, machine verifies (DESIGN)

> **STATE: SUPERSEDED, 2026-08-15 14:51 UTC**, by "Ingestion: the model
> reads, the tool holds the bytes" below. Kept because the five checks
> it names are still the checks, and because the shape it describes
> turned out to be right after a detour: this said the LLM extracts,
> 2026-08-11 replaced that with a mechanical parser, and 2026-08-15 put
> the model back with a tool between it and the text.
>
> The detour was not wasted. What the mechanical parser bought was the
> insistence that citations are COPIED and never retyped, and that
> survives as the tool contract. Read the current section for how the
> two now fit together.

The pipeline, re-runnable at will per chapter, re-runnable when the
SRD source commit bumps:

```
(chapter file, target shapes) -> LLM extraction -> five checks -> data
  1. SCHEMA     output validates against the ontology (refs resolve)
  2. VERBATIM   every extracted value string-matches into the source
  3. VALUE      numeric fields and row bands agree with their citations
  4. SEMANTIC   table row types and outcome composition are complete
  5. INVARIANT  counts the book fixes (24 careers, ranks 0..6, ...)
```

A hallucinated number matches nothing and fails loudly. The verifier is
the only load-bearing code and serves every chapter of every book.

## Execution

`OutcomeExecutor::apply` is built. It dispatches exact concrete outcome
types through a handler registry, traverses sequences and choices, and
commits validated KG operations and dice as one execution. Engine handlers
cover attribute changes, skill minimums and advances, fixed and rolled
money, no-op, and typed pending table-roll requests. Games register their
own concrete handlers and may return typed procedure signals. Unknown
concrete types fail loudly.

`ProcedureRunner` is built in the engine. A game declares exact primitive
contracts, including every route label each primitive may return, then binds
handlers to those declarations. Before the first handler runs, the runner
validates the complete Procedure graph: ordered steps, bound primitives,
route labels, and route targets. It follows seeded routes, falls through to
the next ordered step when no matching route is present, suspends on typed
choices, resumes the same step with explicit input, and stops synchronous
cycles at a transition limit. Unknown primitives, invented route labels,
cross-procedure jumps, malformed suspension data, and handler exceptions fail
loudly.

`RollableTableRunner::select` is built in the engine as the first half of a
two-phase table contract. It validates the table, its referenced
`DiceExpression`, every attached `TableEntry`, all outcome references, band
uniqueness, and coverage of every reachable total before consuming
randomness. It then commits one dice transaction and returns an immutable,
non-fabricable selection value containing the table, row, typed outcome, and
citable `DiceRoll`. It never applies the outcome.

Outcome application is an explicit second call to `OutcomeExecutor`. A failed
or pending outcome retains the original selection and cannot silently trigger
a reroll. Selection and consequence therefore have separate transaction
boundaries. The engine does not recursively execute a table result, and game
code does not interpret row bands or dice fields.

The current Logovger slice registers nine game primitives:
`generate_characteristics`, `choose_career`, `roll_qualification`,
`draft_or_drifter`, `roll_survival`, `roll_training`, `advance_term`,
`choose_term_end`, and `finish_character`. Their order and qualification,
Draft/Drifter, survival, continue, and muster-out routes live in the cited
`basic_chargen` seed. The primitive handlers contain the game behavior, not
the checklist order. Retargeting only a seeded route changes the executed
flow in the acceptance test.

Procedure execution is not one transaction across player choices. Each
primitive owns its own mutation boundary. Outcome execution inside a primitive
keeps the atomic guarantees described above. A handler that changes arbitrary
state before reporting its own failure cannot be rolled back by the generic
runner, so game handlers must validate required input before mutation.

`TaskCheckRunner` is built. It validates the complete check, target
attribute, dice expression, modifier lookup table, selected row, and
integer result column before randomness. It returns an itemized execution
fact with the citable roll, modifier, total, threshold, and pass result.
Gameplay code requests checks and receives facts; it cannot assert a die
result. General contextual modifier composition remains later work.
The cited prior-career qualification penalty is deliberately not applied by
game code. It requires that general composition layer; mutating a temporary
check would create a second, hard-coded modifier path.

## The referee (DESIGN)

A meta-agent in the engine's existing sense: reads the full KG (rules
AND world, same graph, same query shape), writes through KG-ops,
narrates through the chat window. Per turn it receives: rule text for
the current step (cited), a KG snapshot, the journal tail, completed
rolls as facts. It returns narration, ops, and requests. "Create on the
go" is bounded improvisation: procedural content comes from the book's
own tables via requests; schema-bounded content through validated ops;
pure narration is its own.

## The decision ladder (DESIGN, owner vision 2026-08-09)

Most of a session is not the referee. Gameplay decisions route to the
cheapest tier that can honestly make them:

| Tier | Who | Latency | Handles |
|---|---|---|---|
| 0 | engine rules, GOAP, executors | none | movement, combat math, checks, table rolls, routine NPC behavior |
| 1 | pre-prepared content | none | dialogue trees (a-b-c), prepared scenes, pre-created characters with items |
| 2 | fast LLM (Haiku-class) | a speech beat | the ever-present L option: free-form dialogue, immediate NPC reactions |
| 3 | referee (Sonnet-class) | masked by in-game time | scene preparation, plot, the out-of-the-ordinary, judgment the book leaves to the Referee |

Load-bearing properties:

- TIME IS THE MASK. In-game time passing is a property; tier-3 latency
  hides inside travel, scene cuts, nights. Tiers 0-1 keep running
  underneath: the world does not stop because the referee is thinking.
  It is acceptable to make the player wait when it is worth it.
- THE REFEREE WORKS AHEAD. Most tier-3 work is preparation during
  quiet: dialogue trees, scenery, seeded NPCs, before the player
  arrives. The slow tier is rarely on the critical path.
- THE BAKE-BACK IS THE SAFETY. Tier-2 free-form output has
  consequences: it returns narration + validated KG-ops + an NPC
  INTENT (attack, flee, pacify), and an intent is a GOAP goal executed
  by the creature's own in-world brain (the #37 NPC layer). Canon
  cannot corrupt: tier 2 reads only the KG and writes only through the
  validator; its worst case is a refused op.
- Escalation policy lives ON THE CONTENT (leaning, confirm at build):
  an NPC carries its tree, persona brief and escalation marks; a scene
  carries what is prepared and what escalates.
- Dialogue trees: a-b-[c]+L, branches pre-created per character as
  part of story/context; L always present, engages tier 2.
- THE LADDER APPLIES TO CHARGEN TOO (owner, 2026-08-09). The book's
  choice points (career, commission, skill table, reenlist, benefits)
  ARE the baked a-b-c options, cepheus layer. The L slot at marked
  decision beats is VOYAGER-layer: the player goes off-script mid-life
  and the LLM converts it into consequences WITHIN chargen, constrained
  to a small validated consequence vocabulary (a DM on a coming check,
  a Contact/Enemy/Ally relation, a flag on the ServiceTerm). The
  referee picks which legal consequence; the validator decides whether.
  This makes slice 1 the first true showcase moment: a life you can
  steer, with mechanical echoes.
- Slice 1 otherwise needs little of the ladder (tier 3 conversational
  plus tier 0 rolls); the full ladder is built when the ship-interior
  slice brings NPCs. Rule of two applies to its meta-classes
  (DialogueNode, PersonaBrief, PreparedScene).

## What the first playable slice taught us (2026-08-10)

Chargen now runs end to end with a screen you can interrogate. These
are the things that were learned the expensive way, in the order they
cost time.

**A citation that addresses a line cannot prove a cell.** The verifier
passed a deliberately corrupted rule (Scout qualifying on 5 instead of
6) because the quoted line was still present in the source. A number
that lives in a table cell has to be addressed as a cell: file,
heading trail, table, row key, column. This is what `SourceLocator`
exists for, and why the verifier now refuses a citation that quotes a
multi-column row as proof of a single number. See SOURCE_LOCATORS.md.

**Ambiguity must fail, and failure must name what is there.** A
locator that resolves to two cells is a broken citation, not a
fifty-fifty guess. When resolution fails the error lists the headings,
tables and columns that DO exist at that address, because the common
case is a citation written against a slightly different edition.

**Extraction is a retrieval problem, not a parsing problem.** Regexes
over the SRD were fragile and quietly wrong on the career blocks,
where a run of pipe rows holds several sub-tables that restate their
headers. The shape that works: bound a region of the source, hand it
to a fast model to fill, then verify every value against the source
mechanically. The verifier is what makes the model's output
trustworthy; without it, extraction is just plausible text.

**Enforce a rule where the book conditions it.** The seven-term cap
was missing, so a character could serve thirteen terms and die of old
age. Putting the check at "term ends" was still wrong: the book
conditions the PERMISSION TO CHOOSE A NEW CAREER on having terms left.
Enforced there, the behaviour falls out correctly and the citation
sits on the sentence that actually says it.

**Narration must not name the machinery.** The narrator wrote "snake
eyes, nothing Dex 9 could buy back" to a player looking at a sheet
that already says Dex 9. Prose earns its place only by saying what the
numbers cannot: where they were, who else was there, what it cost.
The prompt forbids naming a die, target, modifier or characteristic,
and asks for invented concrete detail inside the setting.

**One call can carry two products.** Each beat returns the scene AND
one clipped clause for the character's file. The file is therefore
written a line at a time as the life happens, at no extra latency and
no extra call.

**A prose cache must be keyed by the whole prompt.** Keying on the
seed and the lore alone meant a change to the narrator's rules served
prose written under the old rules. The key now covers the exact
prompt, so changing the voice rewrites every story.

**Nothing on screen may be cut.** Ellipsis is a lie about what the
book says. Wrapping belongs in the widget, not in its callers: a
caller that has to pre-trim will eventually pass something too long.
The provenance panel matters most, since quoting the book is the
entire point of it.

**Layout is arithmetic, so test it.** A window widened to 1820 points
on a 1728-point display does not appear at all, and no screenshot can
catch a window that was never drawn. Geometry is now a pure function
with a headless test: the window fits a display measured in points,
panels do not overlap, and every absolutely-positioned root widget
lands inside the panel it visually belongs to.

**Reproducibility is a tool, not a default.** Seeding lives from a
counter meant every launch played the same lives. Play draws from real
entropy and prints its seed; `--seed N` replays a specific life.

**Generated code must be ordered by every dependency.** The ontology
generator sorted classes by inheritance only, so a class embedding
another by value could be emitted first and fail to compile. Slot
ranges are dependencies too.

**A gate that runs a hardcoded list is not a gate.** CI listed 21
tests by hand while the suite had grown past 50. It runs `ctest` now.

## Finishing chapter 1 (decided 2026-08-10)

**The referee is the LLM, including when the book delegates to it.**
Cepheus gates the survival-mishap table on "the Referee's approval".
That approval is a RULING, not narration, and the referee agent makes
it: handed the cited rule, the character, and the failed roll as a
fact, it approves or refuses and says why. This is the first time the
LLM decides something rather than describing it.

The guardrail is the same one that governs everything else here: the
referee may rule only where the text delegates to a Referee, and every
ruling is recorded citing the sentence that granted the authority. No
delegating sentence, no ruling. A referee that can approve anything is
not a referee, it is a random number generator with opinions.

**Scope: everything except the world-dependent parts.** Basic
training, commission and advancement with ranks, the second training
roll, aging, the re-enlistment throw, mustering-out benefits,
injuries and medical debt, psionic strength, noble titles and final
details. Homeworld background skills wait for Slice 2, because the
first two of them are chosen from a world's trade codes and law level.

**Commission and Advancement are offered each term**, as the book
writes them, not rolled automatically. Reaching for rank is the
player's call and declining it is a fact about the character worth
recording.

**Material benefits land typed where they map and as entities where
they do not.** Characteristic increases, cash and skill levels use the
typed outcomes that already exist. Passages, ship shares, weapons,
society membership and the two named vessels become Possession
entities carrying the book's own prose and its citation.

### The sequence (dependencies, not estimates)

Extraction first, because every rule below needs its table.

- **A1** Six table types across four career blocks: Personal
  Development, Specialist, Adv Education, Ranks and Skills, Material
  Benefits, Cash Benefits. One retriever pass per type per block,
  verified against the source by locator.
- **A2** The shared tables: Survival Mishaps, Aging, Injury.
- **A3** The remaining checks: Commission and Advancement for the 17
  careers that offer them, Re-enlistment for all 24.

Then the rules, each blocked only on its own data.

- **B1** Basic training (A1 not required; service tables exist).
- **B2** Commission, Advancement, rank state, rank bonus skills, the
  extra training roll (A1, A3).
- **B3** Second training roll for careers without a commission check.
- **B4** Aging from age 34, terms as a negative DM (A2).
- **B5** Re-enlistment throw, natural 12 forces another term (A3).
- **B6** Mustering out: benefit count by terms and rank, the
  three-roll cash cap, the Gambling and rank modifiers (A1).
- **B7** The mishap path, injuries and medical debt (A2, C1).

The referee's new authority, which B7 needs.

- **C1** Rulings: a delegated-decision point, the referee agent that
  answers it, and the verifier rule that a ruling must cite a sentence
  delegating to a Referee.

Then the parts that finish a character.

- **D1** Final details into the graph: name, gender, appearance,
  personal goals. The narrator already invents a name; it lives in the
  UI and nowhere else.
- **D2** Psionic strength and noble titles.
- **D3** Surface: rank, cash and possessions on the sheet, benefits in
  the file.

### Open before building

1. **Rank cells carry two things.** "Squadron Leader \[Leadership-1\]"
   is a title and a bonus skill in one cell. Proposed: a LookupTable
   keyed by rank, each entry carrying a title and an optional
   AdvanceSkill outcome. Needs sign-off.
2. **Re-enlistment has no characteristic.** Every check absorbed so
   far is "attribute target+". Re-enlistment is a flat 2D6 against a
   number. Either TaskCheck permits an empty attribute or a second
   check type exists. Needs sign-off.
3. **The aging table has negative band keys** (-6 through 1+). Band
   derivation has only been exercised on positive totals.
4. **Psionics contradicts the setting draft.** `lore/voyager.md` says
   a character's psionic strength is not rolled at creation; the SRD
   has a chargen step for it. The rules and the setting disagree, and
   the setting is the owner's. Needs a call.

## Ingestion: who reads, who judges, who checks (decided 2026-08-11)

> **STATE: PARTLY SUPERSEDED, 2026-08-15 14:51 UTC.** The first of its
> three claims is dead. "Transcription is mechanical" is replaced by
> LLM-driven transcription through tools that hold the bytes; see the
> section below and the decisions log. The other two claims, meaning is
> judged and judgement is audited, are ACTIVE and were always right.
>
> What killed it, measured across the whole SRD on 2026-08-14: a
> deterministic parser leaves 8.5% of pipe-prefixed lines unaccounted
> for, cuts 14 tables at a width mismatch, and fails by silent
> CORRUPTION rather than honest loss. It promoted a data row into a
> column header and manufactured a phantom table without a word.
>
> The paragraph below about copying bytes rather than retyping them is
> NOT superseded and never will be. It moved: the tool copies the
> bytes, the model decides the structure.

**Transcription is mechanical. Meaning is judged. Judgement is
audited.** All three, or the pipeline is not what it claims to be.

The extractor reads the source by splitting tables and copying bytes,
because a model that retypes a number produces a rule that is wrong
while the prose still reads fine, and byte-exact citations only work
if the bytes were copied. It classifies each cell by form, and refuses
anything that does not resolve rather than guessing. That refusal is
what found four defects in the SRD.

But a classification checked only by the regexes that produced it is
not checked. So `tools/audit_classifications.py` has a model classify
the same values independently, without seeing the extractor's answer,
and disagreement fails the run. A disagreement may be settled by a
person, in writing, with the reason recorded in the tool; it may not
be waved away, because the point is that a difference of reading
reaches a human once.

**And the audit cannot be forgotten**, which is the part that matters.
A tool nobody runs is worse than no tool, because its existence reads
as a guarantee. `test_classification_audit` compares the shipped seed
against the shipped audit and fails when a cell value has never been
audited, when a value is classified two ways, or when the audit was
taken against a different revision of the source. It needs no network.
Change extraction, skip the audit, and the suite goes red.

The first run found one disagreement: the Barbarian's first Cash
Benefits row prints `0`, which the audit reads as nothing and the
extractor as money. Kept as money, because the cell is a cash result
whose amount is zero and a benefit roll was spent to get it. That
reasoning is in the tool, and any new disagreement still fails.

## Ingestion: the model reads, the tool holds the bytes (decided 2026-08-15)

> **STATE: ACTIVE, 2026-08-15 14:51 UTC.** Supersedes the first claim
> of the 2026-08-11 section above.

A model addresses the source through a tool and decides STRUCTURE. The
tool answers two questions and nothing else: what are the addressable
units of this source, and what are the exact bytes at this address. The
model never types the text.

That split is the whole design. **Structure is judged, content is
copied.** Byte-exact citation, the property everything else here rests
on, survives untouched, because the bytes still come from a copy. What
moves to the model is the part a regular expression was never able to
do: deciding that a three-cell row belongs to an eight-column table,
that a trailing cell is a footnote marker, that a logical column
wrapped across two physical ones.

Declared table shapes are the instrument, not the parser. A shape says
"this is an encounter table keyed by 2D6 with these columns"; the model
scans a table against it and reports rows that do not fit. A deviation
becomes a finding with a reason, where a width check produced a
`break`.

**Why the mechanical parser lost the argument.** Measured across the
whole SRD on 2026-08-14: 8.5% of pipe-prefixed lines unaccounted for,
14 tables cut at a width mismatch. And it does not fail honestly. At
`book3/planetary-wilderness-encounters.md:326` the encounter table has
eight columns and roll 10 is a legal three-cell Event row. The parser
ends the table there, then treats data row 11 as the HEADER of a new
one:

```
table at line 316:  8 rows, header = ['2D6', '#App', 'Size', 'Subtype']
table at line 327:  1 row,  header = ['11', '2D6', '800kg', 'Chaser (C)']
```

No error, no warning, a phantom table in the output. A human reads that
page without pausing. Parsing is the hard part, and it belongs where
the judgement is rather than in a correction pass that runs afterwards
and cannot know what was lost.

**Read in pairs, arbitrated once.** Two readers, one positive and one
adversarial, prompted differently rather than sampled twice: the second
exists to find what the first missed or over-claimed. A single arbiter
above them decides what enters, and records the decision with its
reasoning, so a disagreement is resolved visibly instead of averaged
away. There is exactly one arbiter; two would mean two authorities and
no way to say which one wrote a given record.

**Each reader carries the book so far.** Added 2026-08-16 04:57 UTC by
the R11 ruling, and it changes what a reader IS. A reader is handed a
passage, a schema, and the slice of graph the passage's terms resolve
into, never the first two alone. That makes reading order a dependency
order, bounds how much of the book can be read at once, and turns an
unresolvable term into a raised finding rather than a guess. The full
statement is R11 in `docs/REFLECTION_PROTOCOL.md`.

**What does not change.** Meaning is still judged and judgement is
still audited, both from 2026-08-11. Byte-exact citation is still
absolute. The value verifier still proves numbers against the sentence
that states them, and it stays deterministic: a string comparison
proves that absolutely, and spending a model on it would be worse in
every way.

## No LLM, no game, and no quiet fallbacks

Some rules hand a judgment to a person rather than to the dice.
Cepheus aging reduces "two physical characteristics by 2" without
saying which two, and the mishap table turns on the referee's
approval. Logovger sends those to the model, reading the character's
history so the choice means something.

There is no fallback. If no API key is set and no local server is
reachable, the game refuses to start. A deterministic stand-in like
"take the highest first" would be policy the book never printed,
invented by us, applied silently, and indistinguishable in the finished
character from a judgment someone actually made. A missing model is a
missing referee, and the honest response is to stop.

This is the same discipline as refusing to invent a number the source
never gave. It costs a hard dependency and it is worth it.

Tests are not an exception to it, they are how it stays true: a test
installs its own stub selector and asserts against that, so the
production path never grows a fallback to make CI pass.

## Absorb faithfully, tune later, and say which is which

Some rules will be wrong for Voyager and are absorbed anyway. Aging is
the first: decline from 34, with a 58% chance of permanent loss per
term by age 46, is a 1977 assumption rather than a fact about a
setting with jump drives.

Absorbing it faithfully is still right. The Cepheus layer is a true
reading of the book, and a layer that quietly "improved" the numbers
would be neither the book nor an honest divergence. The tuning goes in
Voyager's own layer, and the reasoning goes in
examples/logovger/docs/VOYAGER_EXPANSION.md at the moment it is
noticed, not later when the surprise has to be reconstructed.

The useful discovery there: the book usually contains its own dial.
Aging has anagathics, whose modifier cancels the term count exactly,
so the setting can move the curve without a single rule being
rewritten. Look for that before reaching for a divergence.

## Decisions log

**This log is a ledger, not a summary. Entries are appended, never
edited away.** A decision that stops being true is marked SUPERSEDED or
INVALIDATED in place, with the date and time it died and what replaced
it. Deleting it would hide that we once believed it, and the reasoning
that changed our minds is usually worth more than the conclusion.

States:

- **ACTIVE**: in force. The default; an entry with no marker is ACTIVE.
- **SUPERSEDED by \<date\>**: a later decision replaced it. The old one
  was right for what was known then.
- **INVALIDATED \<date\>**: it was wrong, and evidence says so. Names
  the evidence.

The same marking applies to the DESIGN sections above this table. A
section describing a superseded design carries its state at the top,
because a reader who lands there from a search will never scroll down
to this log.

| Date | Decision |
|---|---|
| 2026-08-17 15:25 UTC | **Ingestion disposition is full append-only decision-event history.** Owner selected option 3. `SourceCoverage` and `IngestionClaim` are enduring Addressable records; neither stores a mutable final status. `CoverageDecision` and `ClaimDecision` reuse the existing `ArbiterDecision` event contract, identify their enduring subject, and carry a contiguous zero-based sequence. Current state is derived from the latest decision while every earlier reading remains queryable. Coverage decisions use the closed `CLAIMS_PRESENT` / `NO_RULE_CONTENT` vocabulary. Claim decisions use `MATERIALIZED`, `PARTIAL`, `RAISED`, `DUPLICATE`, and `CONTRADICTORY`; partial and raised decisions distinguish `ONTOLOGY_GAP` from `RULE_LANGUAGE_GAP`, while duplicate and contradictory decisions name the related claim. Typed `CLAIM_SUPPORTED_BY`, `CLAIM_MATERIALIZES`, and `CLAIM_RESOLVED_AGAINST` relations connect claims to evidence, graph output, and prior knowledge. The `append-only` facet is enforced by the validated write path against both rewrite and deletion. Reconciliation closes mechanical enumeration to exactly one coverage record per source target, coverage to claims, and every claim to a valid current decision without deleting decision history. Provisional claims remain R12/Malleus work and are not smuggled into this local vocabulary. |
| 2026-08-17 04:39 UTC | **The structured source identity is declared in LinkML and generated through the Malleus-rooted ontology pipeline.** Owner selected tuple option 1 and clarified "out of malleus linkml schema, remember." No handwritten C++ struct becomes the semantic authority. The reusable rule-language pack declares source representations, abstract and concrete selectors, targets, closed media and digest vocabularies, required typed references, and numeric bounds. Generated C++ and runtime registry data are projections of that schema. The vendored `schema/malleus.yaml` remains byte-identical to the installed Malleus root and is not locally extended; Logosphere-specific provenance belongs in the importing `schema/packs/rule_language.yaml`. The canonical identity is the structured representation-plus-primary-selector tuple. Any compact key is a mechanical projection, never a second identity. |
| 2026-08-17 04:30 UTC | **Source location and provenance use standards-shaped semantics implemented locally, and replace the parser-centric locator contract.** Owner approved the combined path after asking why the research options were not both: the distinction is semantic contract versus implementation vehicle, so they are deliberately combined. W3C Web Annotation supplies source, target, selector, and state concepts; IIIF supplies page-region and derived-OCR semantics; PROV supplies entity, activity, agent, quotation, and derivation semantics; CID supplies the typed content-digest pattern. Logosphere expresses only the required subset as strict ontology and engine types. It does not require IPFS, RDF, JSON-LD, IIIF services, or ALTO files. The model reader remains the authority for structure. For digital text, the source tool copies the selected bytes. For scans, the pinned image region is the source evidence and the model-generated transcript is a derived reading with activity and reader provenance. The implementation replaces the existing parser-centric `SourceLocator` path; there is no parallel legacy fallback. External serializers wait for a real second consumer. Research and rejected boundaries: `docs/todo_plans/SOURCE_LOCATION_PROVENANCE_SPIKE.md`. |
| 2026-08-17 03:12 UTC | **Coverage starts from atomic source leaves; coarser units are derived views only.** Owner selected option 1 as the more foundational path: headings, prose sentences, every table cell including headers and empty cells, and sentences within list items each receive their own coverage record. Content the normalizer cannot classify becomes an opaque leaf or fails enumeration; it never disappears. A pinned-file manifest also accounts for files with no leaves. If transaction or review volume becomes unmanageable, paragraph, row, section, and file techniques from option 2 may be added on top for filtering, batching, display, and roll-ups. They never replace atomic records or create a second coverage truth. Claims remain separate and may cite one or many leaves. The locator identity for duplicate and empty leaves remains open because the present structural keys do not address all of them uniquely. |
| 2026-08-17 01:41 UTC | **The ingestion ledger has two levels: mechanically enumerable source coverage plus atomic semantic claims.** Owner selected option 4, "belt and suspenders". Every addressed unit of the pinned source gets exactly one visible coverage judgement. A source unit may produce zero, one, or many claims; zero is legal only with an explicit no-rule-content judgement. Every claim cites at least one coverage unit and owns its disposition, prior-resolution dependencies, and downstream invalidation identity. A claim may cite multiple units when meaning crosses an address boundary. This closes the false choice between paragraph, sentence, and table-cell rows: source layout and semantic meaning do not have to share a grain, and the mustering-out paragraph can record both rules separately without losing whole-book coverage. Exact schema names, the source-unit enumeration grammar, and the closed disposition vocabulary remain Phase A decisions. Roadmap and handoff: `docs/todo_plans/LOGOVGER_INGESTION_LEDGER_ROADMAP.md`. |
| 2026-08-17 00:41 UTC | **R11 seed-order enforcement landed.** The three local defects from the 2026-08-16 gate audit are closed without entering R12. `verify_and_load_seed_sequence` now owns cumulative prerequisite construction and per-seed verify-then-load. The windowed game, headless runner and chargen test use one Logovger loader over that operation. The verifier CLI accepts ordered prerequisite seeds. Its dependent fixture passes with the prerequisite and fails without it; a reversed generic pair and the real manifest with its final procedure moved first both fail at seed zero before any rule seed mutates the world. The optional raw `verify_seed` argument remains by the owner's prior ruling, and provisional concepts remain malleus work. |
| 2026-08-16 06:48 UTC | **Accommodation, with reinforcement. A term mentioned before it is defined CREATES a provisional concept.** Owner's ruling, and the staged alternative was offered and refused in the same breath: "so B, no C, B." R11 as written needs a reading order in which every term is defined before use, and no real book admits one, because books forward-reference and refine. Accommodation replaces the impossible requirement (a perfect order) with an achievable one (a CLOSED QUEUE): a term may be mentioned before it is defined, that mention creates a provisional concept, and the ingestion is complete only when nothing provisional remains unresolved. This is Lewis 1979, the rule of accommodation, where a presupposition required by what is said comes into existence *provided nobody objects*, and the second clause is the half we implement: an accommodation nobody can object to is invention. Five conditions, all binding: a provisional concept is a DISTINCT STATUS and never a normal entity with empty slots; it carries the passage that forced it and the term as the book spelled it; nothing executes against it, and a rule depending on one is not runnable and says so; the queue is closed before the world is playable, and the gate is a test rather than a report; the queue's age is measured, because a queue nobody drains is a backlog wearing a protocol's clothes. REINFORCEMENT is part of the ruling, owner's framing "inheriting from how brains work and reinforced paths on neurons": every later reference strengthens the provisional concept, so the queue is ranked by the book's own usage rather than by arrival. That buys measured priority, a signal about the source (a heavily-used never-defined term means the reading missed the definition, or the book assumes an outside primitive, or the book is incoherent), and a convergence measure, since reinforcement rising while resolutions stay flat means the ingestion is accumulating debt. THE LIMIT, and it is the whole risk: reinforcement measures IMPORTANCE, never resolution. Forty references do not define a term. Any rule promoting a provisional concept to settled on count alone rebuilds the half-open gate, and does it to the most load-bearing concepts in the graph. Promotion happens on a definition being found and judged, on nothing else. The neural analogy is kept for intuition and abandoned exactly here: a reinforced path in a brain does become the answer through repetition, and that is the property we refuse. Malleus already owns the machine (staging overlay for proposed subgraphs, assent protocol binding content hash to actor and role); a provisional concept is a staged claim and must not become a parallel notion of "sort of in the graph". Full design in `docs/CUMULATIVE_INGESTION.md`. |
| 2026-08-16 06:48 UTC | **R11's five enforcement gaps, judged one at a time.** Owner's verdicts on the measured gate. (1) `verify_seed`'s optional `prerequisites` argument is the WRONG LAYER, not a local bug: the starting ontology must be able to capture any concept relevant to the domain, provisional ones included, and that is malleus-dev work. Deleting a default argument treats a representation gap as a call-signature defect; an ontology that cannot hold a half-known concept forces the caller to choose between a lie and a refusal whatever the signature says. Goes upstream with the accommodation design. (2) The shipped verifier CLI omitting prerequisites is a BUG: the tool whose job is verification cannot verify the seeds the game loads, and its two fixtures are self-contained so it never showed. (3) The accumulation loop copied into two loaders gets ONE OWNER; the R11 discipline lives inside that loop and a third loader must currently rebuild it from memory with nothing catching a version that drops the last line. (4) The dependency order documented in `rule_seeds.h` prose with no test is a BUG: the header asserts a wrong order "fails loudly, which is the intended behaviour" and nothing has ever loaded them out of order, so the test either proves the guarantee or discovers it was never true. (5) Extractors each deciding what prior knowledge to load is ACCEPTED and not a defect: which prior knowledge a passage needs is an ambiguous judgement, which is what the model is for, and freezing it into a static manifest would be the error. The gate belongs on the OUTPUT (every reference resolves, every unresolved term is accommodated and recorded), never on the reader's choice of what to consult. Consistent with the module's oldest rule, since what to read next is structure, and structure is judged. |
| 2026-08-16 04:57 UTC | **Extraction is cumulative. A passage is read against everything already ingested, or it is not read.** Owner's ruling, and the most load-bearing entry in this log since the module started. The trigger: one sentence, "Characters who end their careers receive one benefit per term served in which they did not lose benefits." To capture it you must already hold *character*, *end a career*, *benefit*, *term served*, and *lose benefits*, and the sentence defines none of them. A book is a vocabulary that grows as it is read, where later rules compose earlier concepts; extraction that treats passages as independent units extracts sentences that happen to be printed in a book, not the book. Consequences, all binding: reading order is a DEPENDENCY order and only approximately page order, since books forward-reference; parallelism is bounded by that order, which is a correctness limit and not a throughput choice; every extraction is handed the slice of graph its terms resolve into, so "no magic strings" becomes "no magic CONCEPTS"; an unresolvable term is a raised finding, never a guessed meaning; and the ledger records what each row resolved against, so a corrected definition can find everything downstream of it. Written up as R11 in `docs/REFLECTION_PROTOCOL.md` with its own section. The practice already existed unnamed in three places: `extract_career_tables.py` loading the skills seed to resolve `Athletics`, `verify_seed(..., loaded_before)` checking against a growing world, and the hand-ordered `kRuleSeeds[]` whose comments carry the dependency reasons. Naming it is what makes it a contract: the paired readers cannot be handed a passage and a schema alone, they must be handed a passage, a schema, and what the book has said so far. |
| 2026-08-16 04:57 UTC | **The extraction ledger covers the whole book, not the rule-bearing parts.** Owner chose option A. Every passage gets a row, and "no rule content" is a judged status like any other rather than a filter applied before the record exists. Measured: 51.3% of the SRD's lines are table rows, 35.6% prose, 12.8% headings, and the plainly non-rule files (legal, about, introduction, README, contents) are 47,407 of 715,234 bytes, so 6.6%. Excluding them would improve the `% ingested` number and would move the act of exclusion outside the record, which is exactly where a rule goes missing. Accepted cost: a few thousand "this is flavour" judgements, every one of which can be wrong, and every one of which is now visible. |
| 2026-08-16 04:57 UTC | **A derived value that contradicts a stated one is kept, both of them, with the divergence recorded.** Owner: "of course, we track all, and use what we want." Not refused, not silently reconciled. The concrete case: the Cutlass states 1250g and a blade of 600 to 900mm, and at the ends of its own stated range the derived mass is 1000g and 1500g, so the book's two facts cannot both hold across the book's own range. Any policy that keeps one number silently picks which of the book's statements to discard. Keeping both, with the divergence as data, turns a contradiction into an inventory that can be queried and decided per use. |
| 2026-08-16 04:57 UTC | **Tests that nothing runs get run, and the resulting red is accepted.** Owner: "tests break and break CI, I want them broken and handled by the responsible, not us, so broken CI." Two engine tests were built by every profile and executed by no lane. The `full-macos` lane now runs both. `test_determinism_guards` passes; `test_humanoid_terrain_scenarios` fails three locomotion cases and stays red until the physics session lands the fix. The lane is advisory, so the failure is loud and blocks no merge. This is the opposite of the usual reflex and it is right: a failure hidden until someone gets to it is how it stayed hidden for as long as it did. |
| 2026-08-16 04:23 UTC | **The primitive-approval rule stays, and gets a gate.** OPEN item 1 said primitive names and route contracts surface for the owner's approval. Measured: the set grew from 8 to 17 and not one of the nine additions was ever surfaced, #59 adding five in a single PR. The owner's ruling on that evidence: nine unenforced violations are evidence the rule was never enforced, NOT evidence the rule is wrong, and the correct reading is the same one this month has produced everywhere else, that a rule with no gate is an intention. So `examples/logovger/chargen/APPROVED_PRIMITIVES` now lists the approved seventeen with their route contracts, and `test_no_primitive_enters_the_procedure_unapproved` fails when the registry and the list disagree in either direction. It cannot stop an addition and does not pretend to; it makes one impossible to add QUIETLY, because the author must edit the list in the same commit and the diff says in one line that the procedure's vocabulary changed. All seventeen are hereby approved retroactively, `grant_rank_zero` included: the rank 0 grant must reach all 24 careers, only the rank-track table covers them, so it cannot ride the promotion step which sees 17. |
| 2026-08-15 14:51 UTC | **Transcription is LLM-driven, through tools that hold the bytes.** SUPERSEDES the 2026-08-11 "transcription is mechanical" entry. A model addresses the source through a coordinate tool that returns the exact bytes at an address, and decides STRUCTURE: which rows belong to which table, which cell is a footnote, where a logical column wrapped. It never retypes text, so byte-exact citation survives untouched: structure is judged, content is copied. Declared table shapes are the instrument it scans against, so a row that does not fit a shape is a reported finding rather than a `break`. Read in pairs, one positive and one adversarial, with a separate arbiter deciding what enters. INVALIDATING EVIDENCE for the old position, measured 2026-08-14 across the whole SRD: the deterministic parser leaves 8.5% of pipe-prefixed lines unaccounted for and cuts 14 tables at a width mismatch, and the failure is silent CORRUPTION rather than honest loss. `book3/planetary-wilderness-encounters.md:326` is a legal 3-cell Event row inside an 8-column encounter table; the parser ends the table there and promotes data row 11 into a column HEADER, manufacturing a phantom table, with no error anywhere. A human reads that page without pausing. Parsing is the hard part and belongs where the judgement is, at the edge, not in a correction pass afterwards. |
| 2026-08-11 | **Transcription is mechanical. Meaning is judged. Judgement is audited.** **SUPERSEDED IN PART by 2026-08-15 14:51 UTC**: the first claim is dead, the other two are ACTIVE. A deterministic parser was chosen because a model that retypes a number produces a rule that is wrong while the prose still reads fine. That concern was correct and is now met by a tool that returns the bytes; what was wrong was concluding the model should be kept away from the reading. Measured 2026-08-14: 8.5% of pipe lines unaccounted, 14 tables cut, and one data row silently promoted to a column header. |
| 2026-08-11 | **No LLM, no game.** Logovger requires a model, remote or local. Where a rule defers a judgment the book leaves open, that judgment goes to the model and there is NO deterministic fallback: no key and no local server means the game refuses to run, not that it quietly decides for itself. Tests inject their own stub selector; production never carries one. Applies to every decision of this shape from here on. |
| 2026-08-08 | Absorb Traveller; the book is the spec; referee cannot roll; everything watchable (rule 12) |
| 2026-08-08 | Spin: psionics-forward frontier sensibility; EA's Sentinel Worlds is tone inspiration only, no names/text/assets |
| 2026-08-09 | Bundled in-repo as examples/logovger, the engine's LLM-runs-a-game showcase |
| 2026-08-09 | Pure Cepheus (Mongoose dropped); source is orffen/cepheus-srd mdBook, commit-pinned; credit in README, owner sends personal note |
| 2026-08-09 | Two game layers, 100% separated; cepheus targets 100% of the book's data model; per-chapter packs 1:1 with SRD files |
| 2026-08-09 | Character is the book's class; Voyager is_a Character, game layer, holds all deviations (incl. learn-by-doing growth if ever adopted) |
| 2026-08-09 | All 24 careers adopted as data now; procedure stays basic (no aging/injuries/draft yet); Drifter mandatory (the book's fallback) |
| 2026-08-09 | Rulebook meta-ontology + ingestion + executor are ENGINE domain (this module); rule of two; logic stays in code |
| 2026-08-09 | No magic strings: ontology refs + KG expressions everywhere; skills are ontology elements; outcomes are KG-ops |
| 2026-08-09 | Ingestion is LLM-extraction with the three-check verifier, not per-format parsers. **SUPERSEDED by 2026-08-11, then RESTORED in amended form by 2026-08-15**: the model extracts again, but through a tool that returns the bytes, so it decides structure and never retypes content. The round trip was worth it; the mechanical detour is what established that citations must be copied. |
| 2026-08-09 | OPEN-1 decided: override = COPY-ON-WRITE FORK (option C). Voyager copies a book rule entity, edits the copy; runtime loads ONLY voyager's set. Guardrails: forked_from + content hash on every fork; CI drift check reports (not gates) when the cepheus original changes after a fork |
| 2026-08-09 | Procedure primitives: THIN, sub-step grain, the book's own gotos transcribed as routing data (outcome label -> step id, nothing else in data; conditions live in primitives). The more granularity the better |
| 2026-08-09 | Rule text per turn: curated fragments, LEARNABLE. Referee may request find_rule(query); the engine searches the vendored SRD (plain text first); results are BAKED BACK as curation-link entities in the KG, so curation grows by play |
| 2026-08-09 | Orchestration: the DECISION LADDER (see section below). Time passing masks LLM latency; the referee works AHEAD of play; free-form output bakes back as validated ops + NPC intents that become GOAP goals |
| 2026-08-09 | Models: Sonnet-class referee, Haiku-class fast tier and extraction, KISS; Opus-class noted for heavy world creation later. Model is config, never a constant |
| 2026-08-09 | Skills modeling: `Skill` is a CLASS, the ~50 skills are KG-seeded INSTANCES (ops file), cascades are relations. Decisive evidence is the book's own line: "Referees may add other skills as needed" — the open vocabulary is the book's design, so voyager extends by ops, never by editing the cepheus pack |
| 2026-08-09 | First divergence-from-source recorded: the SRD's Available Skills List prints "Slug Pistol" twice; the Gun Combat cascade and the description sections prove the second entry is "Slug Rifle". We transcribe from the descriptions with a divergence note; reported upstream as orffen/cepheus-srd#36 |
| 2026-08-09 | Rule instances load as KG-OPS FILES applied at game start (ingestion emits ops; one write grammar for book, referee, game). World persistence direction: LAYERED MANIFEST, never a monolith: named+versioned seed layers (engine, cepheus@commit, voyager@version, worldgen@seed) plus a session delta (ops or snapshot+journal). Snapshot is a cache, not a format. KG save/load is future ENGINE work |
| 2026-08-09 | Post-review rulings, owner: (1) typed entity refs land NOW on the validated write path (registry carries the target class per class-ranged slot; validator rejects non-ids, dangling ids, and wrong-class ids), not deferred to the step-4 verifier. (2) Money is generic: GainCredits renamed GainMoney; the currency is an entity reference into the game's monetary vocabulary exactly as a skill is, credits being one currency of one game |
| 2026-08-09 | Step-3 rulings, owner: Skill is near-empty (is_cascade only; NO characteristic slot, the book assigns characteristics per task not per skill; no alias slot, rule of two unmet). SkillRating lives in the ENGINE rulebook pack because generic skill outcome handlers write it; a character holds ratings via HAS_PART (no new relation until a second consumer needs better semantics). Cascades are SPECIALIZES relations and the book nests them two deep (Winged Aircraft -> Aircraft -> Vehicle) |
| 2026-08-09 | Step-4 rulings, owner: verifier is a C++ CLI tool + library reusing validate_kg_op (first engine CLI); seed files are ENVELOPED (source pin + layer + invariants + ops) and the ops grammar gains "as" alias binders with @ref property values, loader-resolved; verbatim strength is quotes byte-exact + value-digit containment + band derivation for table rows; invariants stay KISS (count_of_type, unique_name_per_type, band_coverage) with values as envelope data; CI verifies all seed files every push and reports (not gates) drift on SOURCE_COMMIT bumps. Verification loads the file into a scratch world, so the verifier's core IS the step-6 loader |
| 2026-08-09 | The project is renamed examples/logovger (logo + V'Ger, Voyager's corrupted name). SRD re-vendored from our fix fork (Kieleth/cepheus-srd fix/book1-skills-typos, upstream PR orffen/cepheus-srd#37): Slug Rifle row, Sciences sentence, and 13 cascade anchors corrected at the source, so the Slug Rifle divergence special-case is retired; re-pin to upstream when the PR merges |
| 2026-08-09 | PR2 rulings, all owner: (1) DiceExpression is a CLASS, no strings-and-parse in rule data; the engine dice grammar grew the xK multiplier so the book's 1D6x10000 is representable and rollable, total = (sum + modifier) * multiplier. (2) LookupTable/LookupEntry added: state-keyed tables (char DM, nobility, rank ladders, retirement pay) are ontology + KG entities exactly like rollable ones; all tables are ingested into the KG. (3) attribute refs (the "Int" in "Int 8+") are verifier-resolved slot references: a string that must resolve against the target class's declared slots, rejected otherwise; characteristics stay slots, not entities. (4) Outcomes are TYPED SUBCLASSES (option B), op-template strings rejected. (5) Table rows are BANDS: roll_min/roll_max and key_min/key_max, single-value row = band of one; buys the coverage invariant (a 2D6 table must cover 2..12, no gaps, no overlaps). StepRoute entities carry the checklist's gotos by the same no-strings principle |
| 2026-08-09 | Table-result option 3: a lookup row is the typed result. LookupEntry is abstract; games declare concrete multi-column row types; LookupTable declares and verifies entry_type. Rollable rows require one typed root Outcome; NoEffect is explicit; OutcomeSequence composes ordered OutcomeSteps and will apply atomically. Choice authority remains open. Shelob is the server deployment library and has no gameplay or rule-processing role. |
| 2026-08-10 | Outcome executor contract, owner: handlers produce plans and never mutate the KG directly; one central executor validates and commits. Exact concrete types dispatch or fail loudly. Choices are typed OutcomeChoice data with player, referee, or procedure authority and suspend before mutation. Skill acquisition separates EnsureSkillLevel from AdvanceSkill so initial and existing-skill behavior remain data. Money uses generic per-currency balance state and distinct fixed versus rolled outcomes. Sequence atomicity includes KG state, mutation events, dice streams, the dice journal, and dice events. GrantTableRoll produces a typed pending request; game-specific outcomes may produce typed procedure signals. |
| 2026-08-10 | Procedure runner scope, owner selected option 2: build the generic engine runner and migrate the current playable chargen slice, without absorbing the full Cepheus checklist in the same phase. The current eight primitive names and their exact route labels are fixed by the game registry. Procedure order and gotos are cited KG data; primitive behavior remains game code. Complete graph validation happens before execution, choices suspend and resume the same step, and synchronous route cycles fail at a transition limit. |
| 2026-08-10 | RollableTable execution, owner selected option 3: selection and consequence application are explicit phases. The engine validates the complete table before randomness, commits one immutable selection fact containing table, row, typed outcome, and citable roll, then stops. The caller applies the selected outcome separately through OutcomeExecutor. Failure or suspension reuses the same selection without rerolling. No hidden recursive execution and no rule consequences baked into table-runner code. |
| 2026-08-10 | LookupTable bounds and selection, owner selected option 1: a missing key bound is legal only with the matching explicit unbounded flag; a finite bound plus a true flag is rejected. The generic engine selector validates the complete table and returns its game-typed row without interpreting result columns. Cepheus characteristic modifiers are all twelve cited rows, with `33 or higher` represented as `key_min=33` and `key_max_unbounded=true`. Qualification and survival read the selected modifier data; the old formula implementation was deleted. |
| 2026-08-10 | General rule authoring boundary, owner: the referee may create and compose rules only from ontology and KG content the engine already understands. The engine derives results. Published rules are immutable and changes create forks. Origin and growth scope are explicit KnowledgeContext entities, supporting session, campaign, user, group, and global learning without a fixed enum ladder. A future DM-authority layer may override by adding a higher-authority rule. New executable operators remain engine work. |
| 2026-08-10 | Main integration boundary: preserve the sheet, personnel file, narration, career history, Draft, and strict term cap while keeping all checks on the generic runners. The deleted characteristic formula and temporary-check modifier path stay deleted. Narration facts read characteristic DMs from the seeded LookupTable. The prior-career penalty remains cited data until general modifier composition can apply it without baking the rule into chargen code. |
| 2026-08-10 | General rule applicability: modifiers belong to explicit KnowledgeContexts and use declarative graph predicates. The evaluator receives active contexts explicitly and never scans unrelated contexts globally. |
| 2026-08-10 | Rule-language boundary: build one engine-level ontology-native language, not a Logovger modifier DSL or a serialized C++ Query. KG entities are typed programs. Pure expressions derive values; explicitly invoked action rules may produce typed Outcome plans through OutcomeExecutor. |
| 2026-08-10 | Ontology reflection: OntologyRegistry remains the sole executable TBox. After registry composition, the engine materializes an immutable, uniquely keyed meta-graph for classes, properties, and relations. Rule nodes use typed entity references to this graph. Author-created meta-entities and unchecked symbolic strings are rejected. |
| 2026-08-10 | Expression typing: use a hybrid model. Ontology inheritance enforces broad value families on operator slots. Immutable class, property, and relation meta-references refine entity and collection types, which the static verifier proves across the expression graph. An operator's broad result type comes from its concrete ontology class, not a writable result-type field. |
| 2026-08-10 | Initial value families: mirror the complete current KG registry with Boolean, integer, float, string, entity, typed scalar collection, class-refined entity collection, and Outcome-plan expression families. Missing data is failure, not a null/default value. Coercion and operator semantics remain separate decisions. |
| 2026-08-10 | Numeric compatibility: integer operands widen automatically when used by a float-producing or mixed-numeric operator. Float values never narrow implicitly. Concrete operator types still fix their broad result family, and evaluation traces expose every widening. Exactness is decided separately below. |
| 2026-08-10 | Widening precision: integer-to-float widening succeeds only when conversion to the engine float representation and back preserves the exact integer. Inexact widening fails during pure evaluation before dice, events, outcomes, or mutation. Overflow is decided separately below. |
| 2026-08-10 | Integer overflow: use explicit operator families. Checked integer operators return Integer and fail on overflow. Promoting integer operators declare a Numeric result, return Integer while the result fits, and promote an overflowing result only when it is exactly representable as float. Otherwise they fail. No wraparound, saturation, hidden narrowing, or runtime change to an operator's declared broad family. |
| 2026-08-10 | Division: one numeric division operator always returns Float. Integer operands must widen exactly before division, and zero denominators fail. Integer quotient, remainder, floor, ceiling, and truncation are not implicit. Float behavior is decided separately below. |
| 2026-08-10 | Float semantics: finite IEEE 754 binary64 only, round-to-nearest ties-to-even. Reject NaN and infinities as inputs or results, allow finite subnormals, canonicalize negative zero, and never inherit a caller-modified rounding mode. Approximate comparison requires a future explicit tolerance operator. The current std::stod validator path does not enforce all of this and needs mechanical guards and tests during implementation. |
| 2026-08-10 | Initial language scope: higher-order from the first version. Functions are typed KG values that may be passed, returned, and applied. The initial core includes typed literals and reads, entity selection, relation traversal, lookup, comparison, Boolean composition, arithmetic, reusable functions, map, filter, and fold. Function bodies remain pure ontology-declared expression graphs; they cannot embed text code, callbacks, mutations, or new evaluator operators. Parameters, capture, recursion, and budgets remain open; application typing is decided separately below. |
| 2026-08-10 | Function application typing: use one concrete application class per broad result family, including function and Outcome-plan results. The node class fixes the broad family; the static verifier proves the applied function signature, argument compatibility, and entity, collection, or returned-function refinements. No generic executable application node and no generated class per signature. Parameter and binding representation remains open. |
| 2026-08-10 | Function parameters and calls: an immutable FunctionSignature declares uniquely named typed parameter specifications. Applications bind argument expressions directly to those specifications as an unordered set. Every parameter is required exactly once; missing, duplicate, foreign, extra, or incompatible bindings fail static validation. Functions may be selected dynamically only under the exact shared signature. No defaults, optional or variadic parameters, positional meaning, or implicit call-boundary coercion. Closures, partial application, currying, and local bindings remain open. |
| 2026-08-10 | Function capture: no implicit lexical closures. BindFunctionExpression explicitly binds named source parameters, evaluates those argument expressions once, and returns an immutable function value under a statically verified remaining signature. That signature reuses exactly the unbound parameter specifications and the source result descriptor. Captures and evaluated values are traceable; entity capture preserves identity rather than deep-copying state. Zero-binding is invalid. Fully bound zero-parameter functions, additional currying, and local bindings remain open. |
| 2026-08-10 | Function arity: zero-parameter signatures are invalid. Partial application must bind at least one source parameter and leave at least one unbound parameter. Binding every parameter uses normal result-family-specific application and returns the result immediately. Reusable zero-input logic remains a typed expression root rather than a delayed function. |
| 2026-08-10 | Local bindings: use eager multi-binding lexical blocks. Let nodes are result-family-specific; bindings have unique diagnostic keys, typed value expressions, and direct entity references. Same-block dependencies form a statically verified DAG. Every binding evaluates exactly once before the body in deterministic topological order, including unused bindings. Nested blocks may read enclosing bindings, but references cannot enter child or sibling scopes or escape their owning block. No string-name lookup or implicit function capture. |
| 2026-08-10 | Currying: no automatic currying in the stored KG or evaluator. Every incomplete call is an explicit BindFunctionExpression. A future human-readable syntax may offer partial-call sugar only by resolving the exact signature and compiling to that same typed bind graph before validation. Complete calls compile to result-family-specific application nodes. No incomplete application or opaque call text reaches runtime. |
| 2026-08-10 | Recursion: opt in with RecursiveFunctionDefinition and a required positive max_recursion_depth; ordinary functions cannot be re-entered. Depth counts active invocations per underlying source-function identity, including the initial call, and bound or returned wrappers cannot reset it. Visible cycles are checked statically; the evaluator guards dynamically selected calls before body evaluation and uses explicit frames rather than the C++ stack. Depth does not bound branching work, so total invocation budgets remain required. |
| 2026-08-10 | Evaluation budgets: use required structured deterministic counters for expression evaluations, function calls, collection visits, and produced values. One meter is shared by the complete nested invocation. Engine configuration supplies complete defaults and absolute ceilings; versioned app configuration may change defaults and lower ceilings; callers may override within the app ceiling. Invalid overrides fail rather than clamp. A rule root may only request lower maxima. The evaluator receives one complete resolved budget and has no null, partial, or fallback path. Concrete numbers come from benchmark fixtures, not guesses. |
| 2026-08-10 | Evaluation caching: no result cache in the initial kernel. Reaching a shared expression node twice evaluates, charges, and traces it twice. Explicit local binding blocks are the only evaluate-once reuse mechanism. There is no per-invocation or cross-invocation derived-result cache, and KG learning layers do not imply one. Future caching requires profiling and must preserve cold-evaluation budgets, trace meaning, deterministic failures, and results. |
| 2026-08-10 | Qualified references: use canonical paths over the current globally composed registry: `@@meta/class/<class>`, `@@meta/property/<declaring-class>/<property>`, `@@meta/relation/<relation>`, `@@meta/facet/<facet>`, `@@meta/value-kind/<value-kind>`, `@@meta/enum/<enum>`, `@@meta/enum-member/<enum>/<member>`, and `@@entity/<context-key>/<exact-type>/<entity-key>`. Segments use strict canonical UTF-8 percent encoding. Portable content implements Addressable with creation-only identity_context and entity_key; display names are not identity. The world enforces tuple uniqueness. File-local aliases remain. The old `@@Type:Name` resolver is deleted after migration and tested dead. |
| 2026-08-10 | Meta-graph breadth: reflect every runtime-semantic field retained by OntologyRegistry, not source-schema documentation. Class meta entities expose parent, abstract, typed facet links, and provenance; properties expose typed declaring-class and value-kind links, required, identifier, creation-only, bounds, reference target, and provenance; relations expose source and target class sets and provenance. Facets and value kinds are canonical engine-owned meta entities under the same qualified grammar. Absence uses explicit flags or empty collections. Materialization is sorted, complete, atomic, engine-owned, and sealed; the registry remains authoritative. |
| 2026-08-10 | Value-kind audit finding: generated registries use enum and datetime, but the generic validator strictly parses only string, Boolean, integer, and float, then accepts every unknown kind unchanged. The earlier registry-complete family claim was therefore false. Unknown-kind rejection and exact enum remediation are active work; datetime validation and migration are tracked separately on the roadmap. |
| 2026-08-10 | Enum and datetime typing target: both are distinct first-class scalar and collection families, never string refinements. The registry and generator must use a closed value-kind discriminant, preserve exact enum definition identity, members, and provenance, and carry typed enum and temporal values through validated KG operations. Unknown kinds fail and the permissive path is deleted. Temporal implementation is now roadmap work. |
| 2026-08-10 | Enum compatibility: nominal and closed. Values carry exact enum and declared member identities. Shared spelling or identical member sets never creates compatibility; cross-enum use fails static validation. Definitions and members are canonical meta entities, declaration order has no runtime meaning, and there is no implicit ordinal, integer, or string conversion. Vocabulary changes require versioned ontology composition. Open vocabularies use typed KG entities. |
| 2026-08-10 | Temporal roadmap target: use four first-class sibling families: instant, calendar date, local datetime, and zoned datetime, with matching collection and result-family-specific application types. There is no implicit conversion or String path. The registry eventually uses four closed discriminants and deletes the generic datetime marker after explicit schema migration. |
| 2026-08-10 | Calendar roadmap target: calendar definitions are nominal, immutable, addressable KG graphs under an ontology-declared contract. Calendar-bearing values and expression types refine to one exact published definition; structural equality creates no compatibility. Cross-calendar conversion is explicit. Published definitions are sealed and changes create a new version or fork. The engine provides generic validation and evaluation, never name-dispatched calendar logic. |
| 2026-08-10 | Calendar execution roadmap target: combine a recurring phase-zero function kernel with finite typed exception entities. The kernel reuses the typed rule language under a stricter verifier profile and cannot use temporal nodes, world queries, randomness, Outcomes, mutation, external calls, or invocation context. Kernel and exceptions share one budgeted, atomic validation and publication boundary. |
| 2026-08-10 | Scope correction: executable enum values, temporal expressions, calendars, zones, temporal operators, and legacy scalar-storage migration move to `docs/todo_plans/RULE_LANGUAGE_ROADMAP.md`. They do not gate the active rule-language phase. The active phase preserves nominal enum metadata and validates members, rejects enum and datetime as executable rule families, and implements unknown-kind rejection, ontology reflection, canonical references, and the families required by current Logovger consumers. |

## Build state (updated at each compaction point)

_Last updated 2026-08-10, the first playable slice, generic rule runners,
explicit rule contexts, and copy-on-write forks integrated._

| Step | What | State |
|---|---|---|
| 1 | DiceService, engine core (seeded streams, citable rolls, DiceRollEvent + dice_rolls() channel) | BUILT, 30/0 (incl. xK multiplier), merged to main (584c3f5) |
| 2 | rulebook.yaml engine meta-pack: 31 classes incl. Cited mixin, explicit KnowledgeContexts, LookupTable/LookupEntry, typed Outcome subclasses, and StepRoute; SPECIALIZES in core; dice xK multiplier; typed entity refs in the validator | BUILT, 70/0 (test_rulebook_pack verbatim-checks ch1 instances against the vendored SRD; entity refs class-checked on the ops path) |
| 3 | cepheus skills pack (Skill + is_cascade, cascades as nested SPECIALIZES; engine SkillRating; 68 instances come later via ops) | BUILT (test_skills_pack; citations verbatim vs the re-vendored fixed SRD) |
| 4 | The verifier (schema / verbatim / value / semantic / invariant) + seed-file grammar ("as" binders, envelope) + the loader that doubles as step 6 | BUILT; semantic checks cover table and outcome structure plus procedure primitive and route contracts |
| 4b | Typed table-result completeness: concrete lookup row shapes, required roll outcomes, NoEffect, ordered sequences and choices, semantic verifier | BUILT on codex/logovger-outcome-executor |
| 5 | Extraction: careers, skills, ch1 constants -> KG-ops seed files | PARTIAL: skills, careers, and selected book-1 tables are production seeds |
| 6 | Ops loader: seed files -> KG at game start | BUILT; now delegates to the reusable atomic KG-op batch |
| 7a | Outcome executor | BUILT with exact dispatch, atomic KG and dice execution, typed choices, generic skill and currency state, typed pending table rolls, and game procedure signals |
| 7b | Procedure runner (outcome-label routing) + thin game primitives | BUILT for the current slice: generic engine runner plus nine registered Logovger primitives, including the Draft/Drifter decision; full-checklist primitives remain part of later absorption |
| 7c | RollableTable runner | BUILT with complete preflight, one committed citable selection, and explicit separate outcome application |
| 7d | LookupTable selector | BUILT with explicit finite or unbounded bounds, complete preflight shared by ingestion, and exact typed-row selection |
| 7e | Rule origin and copy-on-write | BUILT: explicit source and runtime KnowledgeContexts, seed-stamped origin, sealed published content, creation-only lineage, and atomic rule forks |
| 7f | Ontology-native rule language | ACTIVE IMPLEMENTATION: closed registry kinds, canonical references, immutable ontology reflection, abstract expression families, finite binary64 validation, reusable structural type descriptors, named non-empty signatures, unordered typed argument bindings, typed parameter and local reads, and deterministic eager lexical blocks are built and statically validated. Normal non-call operators use direct typed slots. Executable enum, temporal, and calendar families remain deferred; the exact initial literal, read, traversal, comparison, Boolean, lookup, and arithmetic vocabulary is the next active boundary. |
| 8 | Chargen session and playable screen | PLAYABLE END TO END: seeded `basic_chargen` Procedure, sheet, live personnel file with provenance, narration recorded as Narration entities, multi-career history, Draft/Drifter choice, and strict term cap; skill training uses rollable selection plus outcome execution, and qualification and survival use TaskCheckRunner; mishaps, ageing, mustering-out benefits, general contextual modifiers, and referee integration remain |

Working agreements in force: decisions surfaced BEFORE building; gated
merges only (conclusion checked in the same command); background tasks
in the repo may wait and read, never mutate; external communication is
drafted in-chat and sent only after approval.

## OPEN (not yet decided, do not build past them)

_Reconciled 2026-08-09 against the decisions log; former items 3, 5, 6
(rule-text selection, dev transport, instance loading) are decided
above and removed here._

1. ~~Primitive names and route contracts beyond the current nine-step
   `basic_chargen` slice. They surface for approval with each later
   checklist expansion, not as speculative engine vocabulary.~~
   **CLOSED 2026-08-16 04:23 UTC.** The rule stands and is now gated:
   `chargen/APPROVED_PRIMITIVES` holds the approved set with its route
   contracts, and a test fails when the registry disagrees with it in
   either direction. It went nine additions without being honoured
   while it lived only here, which is the argument for the gate rather
   than against the rule. See the decisions log.
2. Turn cadence and interruption model for the referee loop.
3. Escalation policy living ON the content (NPC carries tree + persona
   brief + escalation marks): leaning yes, confirm at build.
4. PersonaBrief shape: what the tier-2 fast LLM gets per NPC.
5. Layered-manifest persistence: direction decided (named+versioned
   seed layers + session delta, never a monolith), the concrete
   manifest format is engine work to design before the worlds slice.
6. find_rule retrieval: plain-text search over the vendored SRD first;
   upgrade only on evidence it falls short.
7. ~~The grain of an extraction-ledger row.~~ **CLOSED 2026-08-17
   01:41 UTC.** Option 4 separates the grains: mechanically enumerable
   source-coverage rows plus atomic semantic claims linked to them. One
   source unit may yield zero, one, or many claims, and a claim may cite
   multiple units. **AMENDED 2026-08-17 03:12 UTC:** enumeration uses
   atomic source leaves; coarser grouping is derived only. Exact leaf
   identity and schema names remain Phase A decisions, but the two-level
   topology and leaf grain are binding. See the decisions log and
   `docs/todo_plans/LOGOVGER_INGESTION_LEDGER_ROADMAP.md`.

## Pointers

- Voyager divergences (deliberate, not corrections):
  examples/logovger/docs/VOYAGER_EXPANSION.md

- Reusable ontology-native rule-language contract: docs/RULE_LANGUAGE.md
- Absorption contract: examples/logovger/docs/ABSORPTION_INVENTORY.md
- The mission and binding rules: examples/logovger/README.md, CLAUDE.md
- Tracking issue: #48
