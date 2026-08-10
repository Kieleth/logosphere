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
| Outcome (abstract) + GrantSkill, ModifyAttribute, GainMoney, GrantTableRoll, NoEffect, OutcomeSequence / OutcomeStep | typed consequence kinds, explicit no-op, and ordered composition; one executor handler per concrete effect class | basic training at Level 0, injury -2 Str, Cr10000 debt, commission extra roll, Injury 6, Mishap 3 |
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
  bonus:                # a GrantSkill entity
    skill: cepheus:skill/athletics
    skill_level: 1
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

Sequence execution will be atomic: handlers produce their validated KG
operations, the complete batch validates, then the batch commits. This
phase defines and verifies the data contract; the executor remains step 7.

Choices are not sequences. Mishap and Injury rows that offer alternatives
still require a separate owner decision about interruption and choice
authority. This decision does not invent that policy.

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
subclass with typed slots (GrantSkill, ModifyAttribute, GainMoney,
GrantTableRoll), instances are ordinary KG entities validated at
creation, and the executor pairs one registered handler per subclass.
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

## Execution (DESIGN)

Engine executor: roll(TaskCheck), roll_on(RollableTable),
apply(Outcome). Outcomes dispatch through a handler registry: engine
ships generic handlers (grant skill, increment slot, gain credits, all
as validated KG writes), games register exotic ones. Dice are
engine-side, seeded, journaled, cited by roll id; the referee requests
rolls and receives facts. A referee that could assert a die result
would be wrong by construction.

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
| 2026-08-09 | Step-3 rulings, owner: Skill is near-empty (is_cascade only; NO characteristic slot, the book assigns characteristics per task not per skill; no alias slot, rule of two unmet). SkillRating lives in the ENGINE rulebook pack because the generic GrantSkill handler writes it; a character holds ratings via HAS_PART (no new relation until a second consumer needs better semantics). Cascades are SPECIALIZES relations and the book nests them two deep (Winged Aircraft -> Aircraft -> Vehicle) |
| 2026-08-09 | Step-4 rulings, owner: verifier is a C++ CLI tool + library reusing validate_kg_op (first engine CLI); seed files are ENVELOPED (source pin + layer + invariants + ops) and the ops grammar gains "as" alias binders with @ref property values, loader-resolved; verbatim strength is quotes byte-exact + value-digit containment + band derivation for table rows; invariants stay KISS (count_of_type, unique_name_per_type, band_coverage) with values as envelope data; CI verifies all seed files every push and reports (not gates) drift on SOURCE_COMMIT bumps. Verification loads the file into a scratch world, so the verifier's core IS the step-6 loader |
| 2026-08-09 | The project is renamed examples/logovger (logo + V'Ger, Voyager's corrupted name). SRD re-vendored from our fix fork (Kieleth/cepheus-srd fix/book1-skills-typos, upstream PR orffen/cepheus-srd#37): Slug Rifle row, Sciences sentence, and 13 cascade anchors corrected at the source, so the Slug Rifle divergence special-case is retired; re-pin to upstream when the PR merges |
| 2026-08-09 | PR2 rulings, all owner: (1) DiceExpression is a CLASS, no strings-and-parse in rule data; the engine dice grammar grew the xK multiplier so the book's 1D6x10000 is representable and rollable, total = (sum + modifier) * multiplier. (2) LookupTable/LookupEntry added: state-keyed tables (char DM, nobility, rank ladders, retirement pay) are ontology + KG entities exactly like rollable ones; all tables are ingested into the KG. (3) attribute refs (the "Int" in "Int 8+") are verifier-resolved slot references: a string that must resolve against the target class's declared slots, rejected otherwise; characteristics stay slots, not entities. (4) Outcomes are TYPED SUBCLASSES (option B), op-template strings rejected. (5) Table rows are BANDS: roll_min/roll_max and key_min/key_max, single-value row = band of one; buys the coverage invariant (a 2D6 table must cover 2..12, no gaps, no overlaps). StepRoute entities carry the checklist's gotos by the same no-strings principle |
| 2026-08-09 | Table-result option 3: a lookup row is the typed result. LookupEntry is abstract; games declare concrete multi-column row types; LookupTable declares and verifies entry_type. Rollable rows require one typed root Outcome; NoEffect is explicit; OutcomeSequence composes ordered OutcomeSteps and will apply atomically. Choice authority remains open. Shelob is the server deployment library and has no gameplay or rule-processing role. |

## Build state (updated at each compaction point)

_Last updated 2026-08-09, PR2 built._

| Step | What | State |
|---|---|---|
| 1 | DiceService, engine core (seeded streams, citable rolls, DiceRollEvent + dice_rolls() channel) | BUILT, 30/0 (incl. xK multiplier), merged to main (584c3f5) |
| 2 | rulebook.yaml engine meta-pack: 16 classes incl. Cited mixin, LookupTable/LookupEntry, typed Outcome subclasses, StepRoute; SPECIALIZES in core; dice xK multiplier; typed entity refs in the validator | BUILT, 34/0 (test_rulebook_pack verbatim-checks 28 ch1 instances against the vendored SRD; entity refs class-checked on the ops path) |
| 3 | cepheus skills pack (Skill + is_cascade, cascades as nested SPECIALIZES; engine SkillRating; 68 instances come later via ops) | BUILT (test_skills_pack; citations verbatim vs the re-vendored fixed SRD) |
| 4 | The verifier (schema / verbatim / value / semantic / invariant) + seed-file grammar ("as" binders, envelope) + the loader that doubles as step 6 | BUILT; semantic table and outcome checks expanded on codex/logovger-option3 |
| 4b | Typed table-result completeness: concrete lookup row shapes, required roll outcomes, NoEffect, ordered composite outcomes, semantic verifier | BUILT on codex/logovger-option3; executor intentionally not part of this phase |
| 5 | Extraction: careers, skills, ch1 constants -> KG-ops seed files | pending, needs 3+4 |
| 6 | Ops loader: seed files -> KG at game start | pending |
| 7 | Executor + procedure runner (outcome-label routing) + ~12 thin primitives | pending |
| 8 | Chargen session: referee loop (stub for CI, Sonnet behind env key), chat window first consumer, a-b-c choices then the voyager L slot, rule-12 visual (a life as a timeline) | the slice-1 goal |

Working agreements in force: decisions surfaced BEFORE building; gated
merges only (conclusion checked in the same command); background tasks
in the repo may wait and read, never mutate; external communication is
drafted in-chat and sent only after approval.

## OPEN (not yet decided, do not build past them)

_Reconciled 2026-08-09 against the decisions log; former items 3, 5, 6
(rule-text selection, dev transport, instance loading) are decided
above and removed here._

1. The concrete primitive LIST for chargen's Procedure steps. The
   shape is decided (thin, sub-step grain, gotos as routing data);
   the ~12 names surface for approval at step 7.
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
