# The Absorption Inventory

_The system design this inventory feeds lives in the engine docs:
[docs/RPG_MODULE.md](../../../docs/RPG_MODULE.md). Read that first._

The contract for the whole track (logosphere#48): every section of the
source texts, mapped to what it becomes here. A rule is absorbed when
its row is DONE — meaning its ontology exists, its procedure is
implemented with the section cited in a comment, its tables load as
data, its referee prompt fragment exists where one is owed, and a test
proves the numbers against the book's own examples.

## The two layers (decided 2026-08-09)

**100% separation.** `schema/cepheus/` is the book: one pack per SRD
chapter file, one to one, nothing of ours in it, every slot cited. The
target for this layer is 100% of the book's data model, as its own
workstream, independent of game slices. `schema/voyager.yaml` is the
game: it cherry-picks cepheus packs by explicit import, whenever the
game first needs that chapter, and it is the ONLY place corrections,
tweaks and expansions may live. The diff between layers is the design
document of the game.

Audit rule: every file under `srd/cepheus/**.md` that carries data
model gets exactly one `schema/cepheus/*.yaml`; coverage is checked by
listing. Procedures, tables and referee material stay slice-driven.

**Sources.** Primary text: Cepheus Engine SRD (OGL 1.0a, Classic-era
2D6 restatement, chapters as published at orffenspace.com/cepheus-srd
and in the SRD PDF). Cross-reference: Mongoose Traveller SRD (ORC, 2022
rules). Where they diverge, the row records the divergence, the choice,
and the reason. License texts ship in `srd/` before any covered text
does.

**Verification rule.** Each chapter's row is fleshed out (sections,
tables enumerated, citations pinned) at transcription time, against the
actual SRD text pulled into `srd/`. Nothing below is absorbed from
memory of the book; this skeleton only fixes the map's shape.

Targets: **O** = ontology (schema pack classes/enums/slots) ·
**P** = engine procedure (deterministic, tested) · **T** = table data
(loaded, rollable) · **R** = referee prompt material (what the LLM is
told, with citations it can repeat).

**Corrected against the vendored text (srd/cepheus, SOURCE_COMMIT
pinned).** The first skeleton guessed 16 flat chapters from memory; the
real SRD is THREE BOOKS (the 1977 shape), plus a Vehicle Design System
and two generator tools the skeleton missed entirely. This table is now
the real map.

| Source file | Becomes | Slice | Status |
|---|---|---|---|
| `introduction.md` — task system (2D6 ≥ 8, DMs, check types §"Types of Checks"), die notation, pseudo-hex, terms | P + O (the pseudo-hex datatype) | 1 | **text pinned** |
| **Book 1** `character-creation.md` — Characteristics (incl. §"Psionic Strength, the Seventh Characteristic"), §UPP, §Background Skills, §Careers, §Qualifying and the Draft, §Terms, §Basic Training, §Survival, §Commission and Advancement, §Skills and Training, §Injuries, §Medical Debt, §Aging, §Mustering Out | O (Character, Career, Term, UPP) + P (the lifepath) + T (career/mustering tables) + R | **1** | **text pinned** |
| **Book 1** `skills.md` — skill list, cascades, levels, checks | O + T + P | 1 | **text pinned** |
| **Book 1** `psionics.md` | O + P + T + R — the spin's engine, absorbed | 3 | text vendored |
| **Book 1** `equipment.md` | O + T | 2 | text vendored |
| **Book 1** `personal-combat.md` | P + R | 2 | text vendored |
| **Book 2** `off-world-travel.md` | O + P | 4 | text vendored |
| **Book 2** `trade-and-commerce.md` | P + T | 4 | text vendored |
| **Book 2** `ship-design-and-construction.md` | O + T | 5 | text vendored |
| **Book 2** `common-vessels.md` | T | 5 | text vendored |
| **Book 2** `space-combat.md` | P | 5 | text vendored |
| **Book 3** `environments-and-hazards.md` | P + T | 4 | text vendored |
| **Book 3** `worlds.md` — UWP generation | **O + P + T — the crown jewel** | **2** | text vendored |
| **Book 3** `planetary-wilderness-encounters.md` — animals | O + T + P (meets the engine NPC layer) | 3 | text vendored |
| **Book 3** `social-encounters.md` — patrons, reactions | T + R | 3 | text vendored |
| **Book 3** `starship-encounters.md` — missed by the skeleton | T + R | 4 | text vendored |
| **Book 3** `refereeing-the-game.md` | R — the referee prompt's spine | every slice | text vendored |
| **Book 3** `adventures.md` — missed by the skeleton | R | 3 | text vendored |
| **VDS** `vds/*.md` — vehicle design, missed by the skeleton | O + T | later, if wanted | text vendored |
| **Tools** `tools/sector-generator.md`, `tools/space-encounter-generator.md` — missed by the skeleton | P (procedures the book itself automates) | 2/4 | text vendored |
| `legal.md` + `LICENSE` — OGL 1.0a + designations | ships verbatim, REQUIRED by the license | 1 | **in repo** |
| Mongoose Traveller SRD (ORC) cross-reference | divergence notes per absorbed chapter | rolling | not yet pulled |

## Slices

1. **Chargen as a session** — Introduction task system + Chapter 1 +
   Chapter 2. The referee runs your career term by term; the engine
   rolls survival, commission, skills, aging; the KG accumulates a
   life. Self-contained, famously a game in itself, watchable.
2. **Worlds** — Chapter 12. UWP generation into KG entities; a
   subsector map is a query. The referee describes worlds it reads
   from the KG, never invents.
3. **Encounters and the spin** — Chapters 3, 13, 14. Psionics
   (the future-magic sensibility on absorbed rules), animals (meets
   the engine's NPC layer), patrons (story seeds).
4. **Travel, trade, hazards** — Chapters 6, 7, 11. The campaign loop:
   jump, haul, survive.
5. **Ships in full** — Chapters 8, 9, 10.

## Chapter 1: what remains (measured against the source 2026-08-10)

Slice 1 is playable but the chapter is not finished. This is what the
book has and the graph does not, counted from
`srd/cepheus/book1/character-creation.md`, not from memory.

**Seeded today:** 24 Careers, 48 TaskChecks (qualification and
survival), 24 Service Skills tables, the Draft table, 29 Skills, 144
AdvanceSkill outcomes, 2 RuleConstants.

**Tables still to extract.** The career tables are laid out in four
blocks of six careers, each block restating its column headers per
sub-table, so the natural unit of work is one table type across one
block: six types times four blocks.

| Table | Count | Shape |
|---|---|---|
| Personal Development | 24 x 6 rows | characteristic increases and skills |
| Specialist | 24 x 6 rows | skills |
| Adv Education | 24 x 6 rows | skills, gated on Edu 8+ |
| Ranks and Skills | 24 x 7 rows | rank TITLE plus optional bonus skill, in one cell |
| Material Benefits | 24 x 7 rows | mixed: characteristics, passages, ship shares, weapons, society |
| Cash Benefits | 24 x 7 rows | credits |
| Survival Mishaps | 1 x 6 rows | discharge outcomes, debts, injury references |
| Aging | 1 x 8 bands | NEGATIVE band keys (-6 up to 1+), terms applied as a negative DM |
| Injury | 1 | referenced by mishaps 1 and 6 |

**Checks still to extract:** Commission and Advancement for the 17
careers that offer them (the book names the seven that do not:
Athlete, Barbarian, Belter, Drifter, Entertainer, Hunter, Scout), and
Re-enlistment for all 24. Re-enlistment is a flat 2D6 target with no
characteristic, unlike every check absorbed so far.

**Checklist steps not implemented.** Numbered as the book numbers them:

- **4. Basic training.** First term of a first career grants EVERY
  service skill at level 0; a first term in a later career grants one
  chosen service skill at level 0.
- **5. Mishap.** Failing survival is death; the mishap table is an
  optional rule requiring the Referee's approval, forces you out after
  half a term, and costs the current term's benefit roll.
- **6. Commission and Advancement.** Rank 0 to 6, an extra Skills and
  Training roll on success, rank bonus skills, once per term each, and
  a draftee may not attempt Commission in a first term.
- **7.2. Second training roll** for careers with no Commission or
  Advancement check.
- **8.2. Aging** from age 34: at the end of the fourth term and every
  term after, 2D6 with total terms as a negative DM.
- **9. Re-enlistment throw.** Today the player simply chooses. The
  book makes it a roll, and a natural 12 forces another term.
- **10. Mustering out benefits.** One roll per term served without
  loss, plus one at rank O4, two at O5, three at O6. At most three
  rolls may be taken as cash. Gambling skill or retirement gives +1 on
  cash; O5 or O6 gives +1 on material.
- **12. Final details.** Name, gender, appearance, personal goals. The
  narrator already invents a name; it lives in the UI and not in the
  graph.

**Deliberately out of this slice:** homeworld and background skills
(marked Optional by the book, and the first two skills depend on a
world's trade codes and law level, which is Slice 2 data), alien
species, psionic strength, noble titles beyond the Soc mapping, and
equipment purchase (Chapter 2).

## The referee's contract (applies to every R row)

The LLM is handed: the relevant rule text (cited), the current KG
state, and completed die rolls as facts. It returns narration plus
proposed KG ops, validated against the ontology. It is never asked to
produce a number a table or die owns. Rolls are engine-side, seeded,
logged, and replayable.
