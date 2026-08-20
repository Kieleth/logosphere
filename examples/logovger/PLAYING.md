# Playing Logovger

A character generator you sit at, where every value on the sheet can be
clicked and the book answers with the cell it came from.

Two ways to run it. The window is the game; the headless binary is the
same session with an `InputSource` instead of a person, which is how it
is tested, replayed and forked.

## The window

```bash
cd ~/Projects/logosphere-public
export ANTHROPIC_API_KEY="$(tr -d '[:space:]' < ~/Projects/.env)"
./build-full/logovger/logovger
```

`--seed N` replays a specific life. The seed of every life is printed on
screen, so one worth keeping can be asked for again.

**The key is not optional, and the refusal is deliberate.** Aging and
injuries fix how many characteristics go and leave WHICH open. The
engine will not choose, so without a referee those rules fail. The game
stops at startup and says so, rather than at term four where it would
read as a bug. The narrator is separate and degrades: no key, no prose,
and it says `NARRATOR OFF` rather than going quiet.

## The same life, no window, no model

```bash
./build/logovger-headless --random 7
```

One integer seeds everything: the dice and every answer. No API key, and
the same number gives the same life forever.

This is the fastest way to show me something. **If a life looks wrong,
send me the seed.** I can reproduce it exactly and then fork it.

## Three worth watching

| seed | what happens |
|---|---|
| **7** | Merchant, six terms. The aging crisis takes Strength, medical care costs Cr30000, and she dies of unpaid care at 42. |
| **3** | The Diplomat turns her away and she takes the Draft rather than drifting. |
| **21** | Two careers. Commissioned Corporal, promoted Lieutenant, musters out with Cr50000, then does not survive her first term in the second career. |

## Recording a life a model plays

```bash
export ANTHROPIC_API_KEY="$(tr -d '[:space:]' < ~/Projects/.env)"
./build-full/logovger-headless --record /tmp/life.tape
```

The model plays the character. Every question goes to it with the life
so far for context, and every answer is taped, so a life it plays once
replays forever without calling it again.

```bash
./build/logovger-headless --replay /tmp/life.tape
```

**Replay never calls a model**, needs no key, and costs nothing. The
referee goes through the same seam as the player, which is why.

## The roads not taken

Every decision records what else was legal, so a life carries its own
branches:

```bash
./build/logovger-headless --forks /tmp/logovger-7.tape
```

```
[2] took '1', could have taken '2'
    The Diplomat will not have you this term. The book gives you two ways...
```

And one can be taken:

```bash
./build/logovger-headless --fork /tmp/logovger-7.tape --at 2 --instead 2
```

Everything before the fork is replayed exactly, including the dice,
because the seed rides along on the tape. Only the one decision differs.
Measured on one life: the trunk is a Drifter dead at 22 with two skills;
forked at the draft alone, the SAME character (UPP `879778`, identical,
because the rolls before the fork do not change) serves seven terms and
musters out a Merchant at 46 with eight skills.

A fork refuses an answer that was never offered, the answer already
given, and any fork at all on a tape recorded before alternatives were
kept, which reports that it CANNOT SEE the branches rather than that
there are none.

## If something looks wrong

Send the seed. With it I can replay your exact life headless, fork it at
any decision, and tell you whether the problem was the choice or the
rules.

Every number on the sheet is answerable: the rules are entities in a
graph loaded from seeds that must prove themselves first, and the
verifier reopens the book at the cited address and refuses the seed when
a quote, a number or a table address does not match.
