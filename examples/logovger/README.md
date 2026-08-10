# Logovger

Traveller, absorbed whole. An LLM referees; the engine rolls the dice.
The first real showcase of an LLM running a game: fully open, from
character generation to the first session.

A Logosphere game: the classic 2D6 science-fiction RPG adapted
faithfully from its open rule texts, merged with a psionics-forward
frontier sensibility. No invented mechanics. The book is the spec.

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
The spin draws tone from late-80s frontier sci-fi RPGs; all names,
text and settings here are our own. No trademarked titles, universes
or assets are used.

## Layout

- `srd/` — the source texts, per license, unmodified
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
