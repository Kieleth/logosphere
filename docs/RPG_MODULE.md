# The RPG Module: rulebooks as engine domain

_Design record, started 2026-08-09. Everything here was decided in
owner discussion; sections marked DESIGN are agreed direction not yet
built; OPEN items are undecided. This document is the canonical capture:
if the conversation and this file disagree, fix the file._

## The one-sentence version

The engine learns what a rulebook IS; a game supplies which rulebook it
plays by, as data; an LLM referees by reading rules and world from the
same knowledge graph, writing through the same validated grammar as
everything else.

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
engine-level `LookupEntry` is abstract and carries only `key_min` and
`key_max`. Each game declares concrete row subclasses with the columns its
book prints. Cepheus currently proves two shapes:

- `CharacteristicModifierEntry`: pseudohex lower and upper symbols plus
  characteristic modifier.
- `DifficultyEntry`: difficulty name plus difficulty modifier.

Every `LookupTable` requires `entry_type`, a resolved ontology name. The
verifier rejects unknown types, abstract types, types outside
`LookupEntry`, empty tables, and attached rows that do not conform to the
declared type. `LookupEntry.outcome` was removed. There is no scalar union,
generic result wrapper, or legacy fallback.

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

The current Logovger slice registers eight game primitives:
`generate_characteristics`, `choose_career`, `roll_qualification`,
`roll_survival`, `roll_training`, `advance_term`, `choose_term_end`, and
`finish_character`. Their order and qualification, survival, continue, and
muster-out routes live in the cited `basic_chargen` seed. The primitive
handlers contain the game behavior, not the checklist order. Retargeting only
a seeded route changes the executed flow in the acceptance test.

Procedure execution is not one transaction across player choices. Each
primitive owns its own mutation boundary. Outcome execution inside a primitive
keeps the atomic guarantees described above. A handler that changes arbitrary
state before reporting its own failure cannot be rolled back by the generic
runner, so game handlers must validate required input before mutation.

The TaskCheck runner remains design work. Dice are engine-side, seeded,
journaled, and cited by roll id. Gameplay code requests rolls and receives
facts; it cannot assert a die result.

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

## Decisions log

| Date | Decision |
|---|---|
| 2026-08-08 | Absorb Traveller; the book is the spec; referee cannot roll; everything watchable (rule 12) |
| 2026-08-08 | Spin: psionics-forward frontier sensibility; EA's Sentinel Worlds is tone inspiration only, no names/text/assets |
| 2026-08-09 | Bundled in-repo as examples/logovger, the engine's LLM-runs-a-game showcase |
| 2026-08-09 | Pure Cepheus (Mongoose dropped); source is orffen/cepheus-srd mdBook, commit-pinned; credit in README, owner sends personal note |
| 2026-08-09 | Two game layers, 100% separated; cepheus targets 100% of the book's data model; per-chapter packs 1:1 with SRD files |
| 2026-08-09 | Character is the book's class; Voyager is_a Character, game layer, holds all deviations (incl. learn-by-doing growth if ever adopted) |
| 2026-08-09 | All 24 careers adopted as data now; procedure stays basic (no aging/injuries/draft yet); Drifter mandatory (the book's fallback) |
| 2026-08-09 | Rulebook meta-ontology + ingestion + executor are ENGINE domain (this module); rule of two; logic stays in code |
| 2026-08-09 | No magic strings: ontology refs + KG expressions everywhere; skills are ontology elements; outcomes are KG-ops |
| 2026-08-09 | Ingestion is LLM-extraction with the three-check verifier, not per-format parsers |
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

## Build state (updated at each compaction point)

_Last updated 2026-08-10, first playable slice merged; see "What the
first playable slice taught us"._

| Step | What | State |
|---|---|---|
| 1 | DiceService, engine core (seeded streams, citable rolls, DiceRollEvent + dice_rolls() channel) | BUILT, 30/0 (incl. xK multiplier), merged to main (584c3f5) |
| 2 | rulebook.yaml engine meta-pack: 16 classes incl. Cited mixin, LookupTable/LookupEntry, typed Outcome subclasses, StepRoute; SPECIALIZES in core; dice xK multiplier; typed entity refs in the validator | BUILT, 34/0 (test_rulebook_pack verbatim-checks 28 ch1 instances against the vendored SRD; entity refs class-checked on the ops path) |
| 3 | cepheus skills pack (Skill + is_cascade, cascades as nested SPECIALIZES; engine SkillRating; 68 instances come later via ops) | BUILT (test_skills_pack; citations verbatim vs the re-vendored fixed SRD) |
| 4 | The verifier (schema / verbatim / value / semantic / invariant) + seed-file grammar ("as" binders, envelope) + the loader that doubles as step 6 | BUILT; semantic checks cover table and outcome structure plus procedure primitive and route contracts |
| 4b | Typed table-result completeness: concrete lookup row shapes, required roll outcomes, NoEffect, ordered sequences and choices, semantic verifier | BUILT on codex/logovger-outcome-executor |
| 5 | Extraction: careers, skills, ch1 constants -> KG-ops seed files | PARTIAL: skills, careers, and selected book-1 tables are production seeds |
| 6 | Ops loader: seed files -> KG at game start | BUILT; now delegates to the reusable atomic KG-op batch |
| 7a | Outcome executor | BUILT with exact dispatch, atomic KG and dice execution, typed choices, generic skill and currency state, typed pending table rolls, and game procedure signals |
| 7b | Procedure runner (outcome-label routing) + thin game primitives | BUILT for the current slice: generic engine runner plus eight registered Logovger primitives; full-checklist primitives remain part of later absorption |
| 7c | RollableTable runner | BUILT with complete preflight, one committed citable selection, and explicit separate outcome application |
| 8 | Chargen session, playable screen (sheet + live personnel file + provenance), multi-career, refusal to Draft/Drifter, seven-term cap, LLM narration recorded as Narration entities | PLAYABLE END TO END, merged (6f775d7); mishap table, ageing and mustering-out benefits not yet absorbed |
| 8-prev | Chargen session and rule-12 life timeline | BASIC SLICE BUILT on the seeded `basic_chargen` Procedure; skill training uses engine table selection followed explicitly by the engine outcome executor; broader checklist and referee integration remain |

Working agreements in force: decisions surfaced BEFORE building; gated
merges only (conclusion checked in the same command); background tasks
in the repo may wait and read, never mutate; external communication is
drafted in-chat and sent only after approval.

## OPEN (not yet decided, do not build past them)

_Reconciled 2026-08-09 against the decisions log; former items 3, 5, 6
(rule-text selection, dev transport, instance loading) are decided
above and removed here._

1. Primitive names and route contracts beyond the current eight-step
   `basic_chargen` slice. They surface for approval with each later
   checklist expansion, not as speculative engine vocabulary.
2. Turn cadence and interruption model for the referee loop.
3. Escalation policy living ON the content (NPC carries tree + persona
   brief + escalation marks): leaning yes, confirm at build.
4. PersonaBrief shape: what the tier-2 fast LLM gets per NPC.
5. Layered-manifest persistence: direction decided (named+versioned
   seed layers + session delta, never a monolith), the concrete
   manifest format is engine work to design before the worlds slice.
6. find_rule retrieval: plain-text search over the vendored SRD first;
   upgrade only on evidence it falls short.

## Pointers

- Absorption contract: examples/logovger/docs/ABSORPTION_INVENTORY.md
- The mission and binding rules: examples/logovger/README.md, CLAUDE.md
- Tracking issue: #48
