# Why this exists

The engine is unusual in ways that cost real time, and none of them were
taste. Each one came from a single bet made at the start. This page is that
bet, what it forced, and what it means for you if you do not care about the
bet at all.

## The word

Logos is the old Greek word that never translates clean. Word, speech,
reason, account: the principle that orders a thing and the act of saying it,
both at once. Heraclitus used it for the pattern underneath change. A sphere
is a world with nothing outside it. Media theorists already had "logosphere"
for the realm where language lives, and I took the word literally. A world
made of word. Speak, and it is so.

## The bet

Give a language model control of a world. Not a chatbot bolted onto a quest
log, not dialogue trees with better sentences. Control. The model should be
able to make a tree exist, raise the ground, redesign an opponent mid-match,
change the hour of the sky, and break things it should not, in a world
physical enough that its choices carry weight.

I tried the obvious route first, on paper: take a mature engine and wire a
model into it. Every path hit the same wall. Existing engines are built for
human authors with human tools. Their world state lives in scene graphs,
prefabs, and serialized editor magic, in places a model cannot see and
should not blindly touch. What you can actually hand a model is spawn here,
play animation, set flag. A puppet with three strings, and you call it
control.

So the engine got written from zero, around the opposite bet: make the whole
world one legible, mutable substrate.

## What the bet forced

Everything is a particle. Every particle is a node in a knowledge graph.
Types, relations, and events are declared in an ontology the model reads
like a spec sheet, and the only way anything changes, whether the mover is
the player, the physics, or the model, is an operation validated against
that ontology.

That is one sentence and it is expensive. It rules out the triangle mesh,
because a mesh is geometry a model cannot reason about without a renderer in
its head. It rules out the second bookkeeping layer that every engine grows,
because two records of the same fact drift and the model reads the wrong
one. It rules out the privileged writer, the one system allowed to poke
state directly, because the moment one exists the ontology stops being the
truth and becomes a suggestion.

What is left is narrow and it has consequences that look unrelated until you
trace them back. A software rasterizer, because the pixels have to come from
particle data the graph already holds. One immovable thing in the world, the
turtle at z = 0, and everything else negotiable. Light computed rather than
faked, and shadow as its absence, because a model that can move a lamp has
to get a real shadow for it without anyone having pre-baked one.

None of those were design preferences. They are what the bet costs.

## The deadline, by accident

The idea arrived around the time I decided to finally finish my degree, and
the final project became the channel. Nearly all my free time went into the
engine, and the thesis gave the obsession a shape and a due date. The
university marked it Matrícula de Honor, which it gives to a handful of
projects a year. The degree closed. The engine kept going.

## What this means if you never touch a model

Here is the part worth staying for.

Chasing legibility for a model produced an engine that is legible, full
stop. The two are the same property and only one of them was the goal.

The ontology that lets a model write into the world is the same ontology
that refuses your own bad write, at load, with a reason. The citation
discipline built so a model could be trusted with somebody else's rulebook
is what lets you click a number on a character sheet and get back the table
cell it came from. The refusal to let any system poke state directly is why
a bug in this engine has one place to look. The determinism that makes a
model's run auditable is what turns your bug report into a seed that
reproduces byte for byte.

You can switch the model off. `LLMSystemHTTP` is nullable and the engine
does not ask about it. Build with the `physics` profile and you get a
render-free engine, solver, and locomotion, on any C++17 toolchain. Build
with `core` and you get the graph, the rules, and the event bus, and nothing
that needs a GPU. A puzzle game uses none of the AI and loses nothing that
was built for it.

The bet is why the engine is shaped this way. It was never a tax on the
people who do not take it.

---

What the engine can do today, and how finished each part of it is, lives in
[CAPABILITIES.md](CAPABILITIES.md). The invariants that any change must
honour are in [ARCHITECTURE.md](ARCHITECTURE.md).
