# Logovger

Traveller, absorbed whole. An LLM referees; the engine rolls the dice.
The first real showcase of an LLM running a game: fully open, from
character generation to the first session.

A Logosphere game: the classic 2D6 science-fiction RPG adapted
faithfully from its open rule texts, merged with a psionics-forward
frontier sensibility. No invented mechanics. The book is the spec.

## What plays today

Character creation, end to end, from rules that were read out of the
book rather than written into the program.

```bash
cmake --build build --target logovger
export ANTHROPIC_API_KEY=...          # the narrator; no key, no play
./build/logovger/logovger             # a random life each launch
./build/logovger/logovger --seed 28   # or replay a particular one
```

You pick a career and throw to qualify. Refused, you take the Draft or
become a Drifter. On joining you already know the trade: a first
career grants every skill on its service table at level 0, a later one
grants a chosen one. Each term you survive, decide whether to reach
for rank, and choose which of four tables you train on. At the end of
a term the book throws for re-enlistment rather than asking you, and a
natural 12 keeps you in even past the seven-term limit. Leaving pays:
one benefit roll per term served in that career, more for rank, at
most three taken as cash.

**Every value on the sheet is a button.** Click one and the panel
below answers with the address in the book, the text at that address,
and the line number, resolved against the vendored source at that
moment rather than trusted from a stored quote. Options are readable
before they are taken: a training table shows all six results, a
promotion throw shows its characteristic, target and dice.

A local model can narrate instead of a hosted one:

```bash
LOGOVGER_LLM=mlx LOGOVGER_LLM_URL=http://localhost:8081 \
LOGOVGER_LLM_MODEL=mlx-community/Qwen2.5-14B-Instruct-4bit \
    ./build/logovger/logovger
```

`logovger-bench-narrator` measures what that choice costs: the pause
between a decision and being allowed the next one.

It also plays with nobody at the keyboard, which is where the rules get
tested at scale:

```bash
# One life, no window, no model. The number seeds the dice AND every
# answer, so it reproduces exactly, forever.
./build/logovger-headless --random 28

# Let the model play a character, and tape every answer it gives.
./build/logovger-headless --record /tmp/life.tape

# Replay that tape without calling the model again.
./build/logovger-headless --replay /tmp/life.tape

# Sweep lives and ask a model which of them the book does not allow.
# Findings carry their seed, so checking one costs a replay.
python3 examples/logovger/tools/audit_lives.py \
    ./build/logovger-headless 1 40 examples/logovger/audits/lives.json
```

Aging, the survival-mishap table and injuries all play: a failed
survival roll is death unless the Referee allows the mishap table, a
mishap can end a career and cost years, and a characteristic driven to
zero raises a crisis that is paid for or fatal.

Medical care and medical debt are now absorbed as exact coverage and claims,
including the Cr5,000 per-point constant, every career-payment cell, and the
requirement to pay outstanding costs from Benefits first. Their restoration,
percentage-payment, debt, and precedence operations are not yet executable.
The medical-debt claim also records the missing anagathic concept as an
ontology gap. Not yet absorbed, and each knowingly so: the retirement pension,
noble titles, and cascade skill specialisation. Rules whose numbers are in the
graph but whose derivation is not, including the per-term benefit count and
the prior-career qualification penalty, say so on themselves in `unmodelled`
and wait on a rule language that today ships a type system and no operators.

## What this demonstrates about the engine

It is the ingestion pipeline end to end, on a real published book
rather than a toy:

1. **Corpus identity is exact.** The application declares both source files,
   and the engine derives one content-addressed ingestion edition from their
   bytes. Every production rule and cross-seed reference uses that edition.
   `Injury Crisis`, `Medical Care`, and `Medical Debt` are the first production
   sections using exact `SourceTarget` coverage and atomic claims. Medical Debt
   proves that one compound source leaf can support several independently
   disposed claims. Every UTF-8 target has a supporting quote that must match
   its byte range. Credits and the medical restoration-cost constant have no
   legacy locator. The remaining 3,081
   structural citations retain the explicit transitional path until each
   section converts; exact evidence never falls back to that path.
2. **Ingestion is verified, not trusted.** Seeds are refused unless
   every citation resolves in the source and every number appears in
   the text it quotes. A line citation cannot prove a table cell.
3. **Structure is judged, content is copied, and judgement is audited.**
   A model reader decides the structure of irregular source material.
   For digital text, the source tool returns exact bytes, so the reader
   never retypes a number. What a cell means is classified, and an
   independent reader audits that classification; a test fails if the
   audit has never seen a value the seed ships. The current mechanical
   extractors predate this accepted target design.
4. **Rules are data all the way down.** Procedures, steps, routes,
   throws, tables and outcomes are entities. Changing what a career
   teaches is editing a seed, not a rebuild of game code.
5. **Dice belong to the engine.** Seeded streams, journalled rolls,
   every result citable by id, so a life replays exactly.
6. **The LLM cannot cheat.** It receives resolved facts and returns
   prose. It has no path to a die, a skill or a characteristic.

Absorbing one chapter this way surfaced four defects in the published
book: a skill granted by two tables and defined nowhere, another that
occurs once in the whole SRD, and two misspellings the book's own text
proves. All were reported upstream rather than silently corrected.

## The discipline

- **Ontology = the book's data model.** A UWP world profile is a KG
  entity class. A career is an entity. A skill is a slot.
- **Engine = the book's procedures.** Chargen terms, task rolls, world
  generation: deterministic, testable, cited to their section.
- **LLM = the referee.** It applies rules it can cite and narrates
  outcomes. It cannot invent a roll: dice are engine-side, and every
  roll is a recorded fact the referee is handed, never asked for.
- **Zero invention, minimum translation.** If the book says 2D6, we
  roll 2D6. Divergences between source texts are recorded per section
  with the choice and the reason.

Absorbing a different book? **[Rules as
Data](../../docs/RULES_AS_DATA.md)** is what this cost us to learn:
where the line between data and code actually falls, why a rule must
never be found by its printed name, how a value proves itself against
the text that states it, how the rules come to check themselves, and
the failure modes we hit so you can skip them.

## Sources and licenses

| Source | License | Role |
|---|---|---|
| Cepheus Engine SRD | OGL 1.0a | Classic-flavor base text |
| Mongoose Traveller SRD | ORC | Current-rules cross-reference |

Both license texts ship in `srd/` alongside the material they cover.

**Credit where it is due.** The Cepheus Engine SRD is by Jason "Flynn"
Kemp, Samardan Press, 2016. Our vendored copy comes from the mdBook
conversion maintained by Steve Simenic ([orffen/cepheus-srd on
GitHub](https://github.com/orffen/cepheus-srd), rendered at
[orffenspace.com/cepheus-srd](https://www.orffenspace.com/cepheus-srd/)),
pinned by commit in `srd/cepheus/SOURCE_COMMIT`. The markdown-source
conversion is what makes citation-grade absorption possible, and this
project would be poorer without it.

While transcribing we found three defects in the conversion (a
duplicated skill-list entry, a description copied from the wrong
skill, and thirteen links whose anchors do not resolve), reported
upstream as
[orffen/cepheus-srd#36](https://github.com/orffen/cepheus-srd/issues/36)
with a fix in
[PR #37](https://github.com/orffen/cepheus-srd/pull/37). Until that
merges, `SOURCE_COMMIT` pins our fix branch
(`Kieleth/cepheus-srd@efb8f9d`), which is upstream plus exactly those
three commits; we re-pin to upstream when the PR lands. No rules text
is altered.

The spin draws tone from late-80s frontier sci-fi RPGs; all names,
text and settings here are our own. No trademarked titles, universes
or assets are used.

## Layout

- `srd/` — the source texts, per license, carrying only the upstream
  typo fixes described above
- `docs/ABSORPTION_INVENTORY.md` — the contract: every book section
  mapped to ontology / engine procedure / table data / referee prompt,
  with citations (the first deliverable)
- `schema/` — the game ontology packs (traveller core + setting)
- `src/` — the game

## Why this lives in the engine repo

Logovger is the engine's showcase: the first full demonstration of an
LLM controlling a game for real — an open ruleset, refereed end to end,
from character generation to the first session, all of it in the open.
It is also the example that forces the engine's space era. Every game
in this repo earned engine mechanisms by needing them (logotron the
interaction model, logogenesis worldgen, the predator the NPC layer);
logovger's needs — star systems, ship interiors, subsector views, the
things science fiction is made of — flow upstream as engine issues, not
as forks. Zero-g was an engine invariant before this game existed.

Tracked in issue #48.
