# Voyager: the narrative authors the options

**Status: PLAN. Nothing here is built. Decisions marked OPEN block the
work below them.**

Cepheus chargen is finished and frozen as a milestone. This is the
successor, and it is not more of the same: it inverts where options come
from.

## The inversion, in one sentence

Today the rules generate the options. In Voyager the **narrative**
generates the options, and each one is still backed by a die the engine
rolls.

Today chargen offers twenty-four careers because the book lists
twenty-four careers. The menu is a projection of a table. Tomorrow the
model reads the life so far, which is itself made of dice outcomes, and
authors the alternatives that make sense for *this* character at *this*
moment. A dishonourably discharged belter with Dexterity 9 and Education
2 is not offered the same twenty-four doors as a Marquis.

Three constraints keep that from becoming freeform fiction:

1. **Every authored option carries a throw.** The model proposes; the
   engine rolls. The model never decides an outcome it could roll for.
2. **The player sees the odds.** An option states what it risks and what
   it reaches for, as chargen already does. Risk you cannot see is not a
   decision, it is a cutscene.
3. **The model may compose only from building blocks the engine already
   understands.** This is not a new rule. It was decided 2026-08-10:
   *"the referee may create and compose rules only from ontology and KG
   content the engine already understands. The engine derives results.
   New executable operators remain engine work."*

The model may be generous, cruel, or theatrical within those bounds. A
DM's prerogative is real and this preserves it. What it cannot do is
produce an effect the engine cannot derive and check.

## The end state, and it is the actual thesis

A player says: *"my character reaches down, grabs a handful of sand, and
throws it in the enemy's eyes."*

Nothing in the game models that. It is obviously plausible and obviously
allowed. What should happen is that it becomes a **typed rule in the
graph** with a throw attached, and from that moment anyone can do it,
including the model, because it is now part of the vocabulary rather
than a one-off narration.

That is the whole bet: **a rule set that grows through play, where the
growth is typed and therefore checkable, replayable and reusable.**

## What this rests on, most of which already exists

The surprising part of writing this plan was how little is new.

| The need | What already answers it | State |
|---|---|---|
| Turning text into executable rules | the reflection protocol, L0 to L8 | built, in use |
| A term used before it is defined | **R12 accommodation**: mention creates a provisional concept, reinforced by use, promoted only when a definition is found and judged | decided, ungated |
| Rules as typed programs in the graph | the ontology-native rule language | active implementation |
| "Compose only from what exists" | the rule-authoring boundary | decided 2026-08-10 |
| Where a runtime-created rule lives | KnowledgeContext: session, campaign, user, group, global, plus copy-on-write forks | built |
| Who decided, and why | ArbiterDecision, one arbiter per decision, readable identity | built |
| Reproducing a session | the tape, plus node names and the rulebook edition | built |
| Asking what else could have happened | tape forks: every decision records its untaken branches | built |

**The unification worth naming.** Absorbing a rulebook and absorbing a
player's invention are the *same pipeline*. L0 is a source, addressable
and byte-exact; it does not care whether the bytes are a published SRD
or a sentence the player typed. L1 reads it against everything already
ingested. L4 refuses a type that does not exist. The sand-throw is an
ingestion of one sentence, at play time, by the same protocol that ate
the Cepheus SRD.

And the sand-throw is **R12 exactly**: a term used before it is defined.
It creates a provisional concept, it is reinforced each time it is used,
and it is promoted to a settled rule only when a definition is found and
judged. The rule we wrote for reading a book turns out to be the rule
for playing one.

## What is genuinely new

1. **Narrative to options.** The model authors the option set for a
   step, each option carrying its throw. New.
2. **Authored options must be taped.** A model-authored option is not
   derivable from the seed, so replay cannot reconstruct it. It has to
   be recorded like an answer. `Ask.offered` already records what was
   offered, as of 2026-08-19, so the format is ready and the forks
   machinery will work on authored options for free.
3. **Odds surfaced to the player.** Partly exists: chargen already
   states the throw and the prize on each choice.
4. **Free-form input.** The player types a sentence; it comes back as a
   typed proposal inside the game's vocabulary.
5. **Runtime rule creation.** The proposal becomes a rule in the graph,
   under a KnowledgeContext, usable thereafter.

## Sequence, by dependency

No estimates. Each stage is usable on its own and each depends only on
what precedes it.

**V1. The model authors the options.**
Replace the fixed career menu with an authored one, at one step only,
with everything else unchanged. Each option carries a throw the engine
owns. The tape records the authored options. Proves: the inversion, the
taping, and that replay still reproduces a life exactly.

**V2. Authored options everywhere chargen asks.**
Training tables, re-enlistment, the crises. Same mechanism, more sites.
Proves: it holds up across a whole life rather than one prompt.

**V3. Odds, stated.**
Every authored option shows what it risks and what it reaches for,
derived from the throw rather than narrated. Proves: the player is
deciding, not watching.

**V4. Free-form in, typed proposal out.**
The player types a sentence. The reflection pipeline turns it into a
proposal: which existing building blocks it composes, what throw it
implies, what it would add. Nothing is committed. Proves: the hard half,
which is understanding, separated from the risky half, which is writing.

**V5. The proposal becomes a rule.**
Committed to the graph under a KnowledgeContext, as a provisional
concept under R12, reinforced by use, promoted when ratified. Usable by
the player and the model from that point. Proves: the thesis.

**V6. The model composes rules of its own.**
The same path, with the model as the proposer rather than the player.
Everything above must hold first, because this is the one that can run
away.

## OPEN decisions, which block the stages that need them

1. **Who ratifies a new rule, and when?** Immediate on proposal, staged
   for the owner's assent, or automatic under a stated condition? Malleus
   has an assent protocol binding a content hash to an actor and a role.
   *Blocks V5.*
2. **What scope does a created rule get by default?** Session, campaign
   or global. The machinery supports all; the default is policy.
   *Blocks V5.*
3. **What is the declared set of building blocks a proposal may
   compose from?** The boundary decision says "content the engine
   already understands", which is a principle rather than a list. A list
   is checkable; a principle is not. *Blocks V4.*
4. **What stops an authored option from being free lunch?** A model can
   propose "you succeed automatically". Candidate answer: every option
   must carry a throw and a cost, and the option-authoring step is
   itself validated against a rule the engine owns. *Blocks V1.*
5. **Does a provisional rule created in a session survive a replay of
   that session?** It was created by a model, so it is not derivable; it
   must be taped like an answer, and then a replay recreates it. Needs
   confirming rather than assuming. *Blocks V5.*

## What this is not

Not a rewrite. Cepheus stays: it is the rule set Voyager starts from,
the proof that absorption works, and the fallback when the model is
absent. Voyager adds a layer that authors within those rules and,
eventually, extends them.

Not the model replacing the engine. The engine still rolls every die,
owns every outcome, and refuses anything it cannot derive. The model
proposes and narrates. That division is the reason any of this is
checkable.
