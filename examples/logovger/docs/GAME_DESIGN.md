# Voyager: game design document

**The one place design lives.** Mechanics, pillars and intent belong
here. Architecture belongs in `docs/todo_plans/VOYAGER_PLAN.md`;
absorbed rules belong in `docs/RPG_MODULE.md`. When this document and
the code disagree about what the game *should* do, this document wins
and the code has a bug. When it disagrees about what the game *does*,
the code wins and this document is stale.

Sections marked **DREAMLAND** are not commitments. They are recorded so
they are not lost and so nobody builds something that forecloses them.

---

## 1. The pitch

A character generator, and later a game, where a language model is the
referee and the engine rolls every die. The rules come from a published
book, absorbed until the graph plays it. Then the model starts
*authoring within* those rules, and eventually *extending* them, with
every extension typed, cited, and reusable by everyone afterwards.

---

## 2. Pillars

### 2.1 Equal opportunity, tilted luck

This is the design's centre and it is easy to get wrong.

A dishonourably discharged belter with Dexterity 9 and Education 2 is
not offered the same twenty-four doors as a Marquis. That is the point
of a narrative authoring the options. But the belter is **not luckier**,
and the game does not take pity.

What differs is the *kind* of opportunity, not the *amount*. Someone who
looks out of options may find the odds tilted toward an encounter with
something alien, strange, off the map. The intellectual finds a
different gate, one that opens on what they know. Neither gets a walk in
the park.

**And luck must be worked for.** Discovery is earned. A door appears
because the character went somewhere, risked something, or paid
attention. An option that arrives for free is not opportunity, it is
charity, and it is boring.

The test for any authored option: *would a good DM offer this to this
character at this moment, and would the player feel they had earned the
chance to try it?*

### 2.2 Risk you cannot see is not a decision

An option states what it risks and what it reaches for. A player
choosing blind is watching a cutscene.

But see §3.1: "stated" does not have to mean "numeric".

### 2.3 The engine rolls, the model proposes

The model never decides an outcome it could roll for. It authors the
option, narrates the result, and colours the world. The die is the
engine's, always. This is the division that makes any of it checkable.

Within that, the model may be generous, cruel or theatrical. A DM's
prerogative is real and worth keeping.

### 2.4 Everything the model invents becomes part of the game

A rule invented in play is typed, recorded, and usable by everyone
afterwards. It is not a one-off narration. This is the thesis: **a rule
set that grows through play, where the growth is checkable.**

---

## 3. Mechanics

### 3.1 Hidden probabilities, and Foresight

**Phase one: explicit.** Every option shows its throw and its target,
as chargen already does. This is how we learn whether authored options
are any good, and a designer cannot tune what they cannot see.

**Phase two: the numbers go under the narrative.** The odds stop being
printed. The prose carries them: *"the fixer looks you over and does
not seem impressed"* is a bad target number, said in the language of the
world. The information is still there; it is encoded rather than
tabulated.

**And then a skill uncovers them.** A character with **Foresight**
(working name; Prescience is the alternative) sees through the prose to
the shape underneath. A high enough rating reveals the throw, the
target, or the modifier. A low one reveals that there *is* something to
see.

This turns a UI decision into a game mechanic, which is the correct
place for it. Reading the odds becomes a thing the character can be good
at, a build choice, and something that can be lost — a character whose
Foresight is damaged stops seeing clearly, and the player feels it.

Open: whether Foresight is a normal skill, a characteristic, or a
property of equipment. Also open: whether the model is *told* the
player's Foresight rating, which decides whether it writes prose that
leaks the numbers deliberately.

### 3.2 Options are authored, throws are owned

At each decision the model receives the life so far and authors the
alternatives. Each carries a throw the engine owns. The model cannot
author an outcome; it authors a *chance at* an outcome.

Constraint, and it exists to prevent free lunch: every option carries
**a throw and a cost**. An option with no downside is not an option, it
is a reward, and rewards are earned elsewhere.

### 3.3 The unmodelled action

A player says *"my character grabs a handful of sand and throws it in
the enemy's eyes."* Nothing models it. It is obviously plausible.

It becomes a typed rule in the graph with a throw attached, and from
that moment anyone can do it, including the model.

Mechanically this is **R12 accommodation**: a term used before it is
defined creates a provisional concept, reinforced by every later use,
promoted to a settled rule only when a definition is found and judged.
The rule written for reading a book turns out to be the rule for playing
one.

---

## 4. Seeds are alive

The word was chosen carefully and the metaphor turns out to run the
whole way down.

A **seed** is a crystallised representation of the relations of a game
at a moment in time. Not yet germinated. A piece of the puzzle, not the
puzzle.

Which means all of this is available:

- **Germination.** A seed loaded into a world becomes live rules.
- **Cuttings.** Take a part of a grown game and start another from it.
- **Grafting.** Put one seed's rules on top of another's stock.
- **Breeding.** Two games that diverged produce a third carrying traits
  of both.
- **Cross-pollination.** Players share their evolutions, and a game
  grows as it is played, everywhere it is played.
- **Isolation.** Evolution in separate places, coevolving, recombining
  later. Islands, and then bridges between them.

### 4.1 What we build first, and what waits

**Now**: seeds evolve in-game, and the evolution is stored. A game
produces a delta. Post-mortem, the delta is readable, so learnings can
be extracted from what actually happened rather than remembered.

Requirements the first pass must meet, because they are what makes the
rest possible later: an evolved seed is **serialisable**, has the **same
shape** as any other seed, and is **identifiable and versionable**.

**DREAMLAND**: the genetics. Breeding, dominance, which traits carry and
which are recessive, how two divergent rule sets recombine without
producing nonsense. This is Mendel's work and it is a project of its
own. It is not scheduled. It is written down so that nothing built now
makes it impossible, and the three requirements above are exactly what
keeps the door open.

---

## 5. Where the outside gets in

One component owns the boundary between the world outside the game and
the world inside it: bytes arrive, and typed claims come out. It does
not care whether the bytes are a published rulebook, a sentence the
player typed, or a proposal the model wrote. Same pipeline, same
refusals.

**Naming**, and the leading candidate is from Tron:

- **Dumont** — keeper of the I/O Tower. Enslaved by the MCP yet still
  the one who controls the gateway between the users' world and the
  digital one, and who refuses to open it for anyone who cannot prove
  they belong. That refusal is the component's actual job. Leading.
- **The I/O Tower** — the place rather than its keeper. Plainer, less
  personal, and it names the boundary rather than the judgement.
- **Janus** — the god of doorways, who faces both ways at once. Accurate
  and a little cold.
- **Heimdall** — who guards the bridge and can see and hear everything
  approaching. Good for a gate that must judge what it lets through.
- **The Threshold** — no myth, no reference, says exactly what it is.

Recommendation: **Dumont**, with the I/O Tower as the name for the
boundary itself if the two ever need separating.

---

## 6. What "fun" means here

Stated so it can be argued with rather than assumed.

- **Consequence over optimisation.** A life worth reading beats a
  build worth copying.
- **Earned surprise.** The best moment is one the player did not see
  coming but can see, afterwards, that they walked into.
- **Legible risk.** A player who loses should know what they gambled,
  even if the odds were dressed as prose.
- **The world remembers.** Something invented in play is still there
  next session, for everyone.
- **No pity, no cruelty for its own sake.** The dice are honest. The
  DM is not trying to kill you and is not trying to save you.

---

## 7. Open design questions

Numbered so they can be answered one at a time.

1. Is Foresight a skill, a characteristic, or equipment?
2. Is the model told the player's Foresight rating?
3. When do the numbers go under the narrative: from the start, or after
   authored options are shown to work?
4. What is the default scope of a rule created in play — session,
   campaign, or global? *(The machinery supports all three. This is
   policy, and it is unanswered.)*
5. Can a player refuse an authored option set and ask for others, and
   what does that cost?
6. Does the model know the dice results before it narrates, or does it
   narrate the throw it asked for and learn the outcome with the player?
