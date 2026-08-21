# What absorbing a rulebook taught us

Cepheus chargen is frozen. This is what came out of it that outlived it,
written down because most of it was not what we set out to learn.

## The thing we set out to prove

That a published rulebook could be read into a knowledge graph until the
graph plays the game, with every value on the sheet answering where it
came from. That works. 330 lives run without a failure, 288 lives over
24 careers reach 978 of 1173 absorbed outcomes, and **0 of 171 tables
are dead** — every absorbed rule is reached by some life.

## What we did not expect

### 1. Extraction is cumulative, and that is a rule about knowledge

One sentence forced it: *"Characters who end their careers receive one
benefit per term served in which they did not lose benefits."* Five
load-bearing terms, none of them defined in the sentence.

A book is not a bag of statements. It is a vocabulary that grows as it
is read. Extraction that treats passages as independent extracts
sentences that happen to be printed in a book.

That became **R11**, and chasing its prior art turned up that several
fields had worked it out independently: dynamic semantics gives it a
type signature, extension by definition gives it a formal core, ISO 704
gives it a standards vocabulary, and induction heads explain why a model
reads it correctly and a regular expression cannot.

**The reframe that came out of that** is the most valuable sentence in
the project: attention already resolves later terms against earlier ones,
inside one context window, for one forward pass, and leaves nothing
behind. So this architecture is **not compensating for a model that
fails at this. It is compensating for one that succeeds and does not
persist.** That belongs to malleus, not to a game.

### 2. Reading a book and playing one are the same pipeline

R11 needed a partner: no real document admits an order where every term
precedes its use. So **R12**: a term used before it is defined creates a
provisional concept, reinforced by every later reference, promoted only
when a definition is found and judged. Lewis's rule of accommodation,
with the *"provided nobody objects"* clause implemented rather than
assumed.

Then the same rule turned out to describe play. A player inventing an
action nobody modelled is a term used before it is defined. The rule we
wrote for reading turns out to be the rule for playing, which is why
Voyager is a layer rather than a rewrite.

### 3. A decision log is a graph, and it is git-shaped in one half

A tape records what was decided; the world is derived. Many runs share
long prefixes and part at one decision, which is exactly commits sharing
history.

A recon into git's object database settled how to name a node, and the
finding was not what we expected: git does **both** keyings, at
different layers, and the layering is the trick. Trees are keyed by
state, so equal states collapse. Commits are keyed by state plus every
ancestor, so equal states reached by different routes stay distinct.

Naming a history node by the state it produces creates a **cycle** —
walk north then south and you are back where you started. Mercurial's
author hit that in 2005 and wrote down why. Across git, Mercurial, Pijul
and event sourcing, **not one system names a history node by the state
it produces.**

So: a decision is named `sha256(parent, answer)`. Git's commit with the
tree slot removed.

### 4. A guarantee with no gate is an intention

Said in many forms, all of them paid for:

- A rule that primitives must be approved went **nine additions**
  without being honoured while it lived only in a document.
- Every schema in the repository failed to construct under malleus, so
  eight rites had never run against anything, and the state read as
  ordinary health for weeks.
- A dependency order was documented in prose with a comment asserting
  that violating it "fails loudly", and no test had ever loaded the
  seeds out of order.
- 3,616 provenance records claimed to be owner-directed. Nobody had
  directed them.
- A required CI check could not report on a pull request by
  construction, so nine merges passed by override while the gate looked
  enforced.

The pattern is one thing: **the discipline existed and nothing required
it.** The successor should assume that any rule not attached to a gate
is decoration.

### 5. Absence must have an address

The coverage sweep found a use-after-lifetime bug that no unit test on
any platform caught, because it asked a question nothing else asked:
*is every absorbed table reached by some life?* Six career paths were
unreachable on Windows and nowhere else.

An unabsorbed rule and a nonexistent rule look identical from inside the
repository. Reporting what was **not** ingested, with a reason, is the
only defence.

### 6. The model's failures were informative and ours were not

Letting a model play revealed two defects in a single run, neither
visible to any test. The option keys reached it as bare `1, 2, 3` with
the labels discarded — and it **stopped and asked what the options
were** rather than guessing, which is the only reason we saw it. Then a
200-token reply cap truncated its answer before the required line, with
no retry, and the caller blamed the model for an answer it was never
allowed to finish.

Both were ours. The model behaved better than the harness.

## What left the project

**malleus-ocr**, the idea that cumulative ingestion is a general
protocol rather than a feature of one game: any document, any domain,
with concepts captured semantically as they are defined and referenced
later against what was already captured.

**Absorbing games from their instructions**, which is now a roadmap item
in its own right: point the engine at a body of rules and get something
playable that cites itself.

## The number that matters

3,616 recorded decisions, each with its disposition, its gap kind, and
who made it. Not because the count is impressive, but because the
alternative — a graph full of rules with no record of how they got there
— is what every other adaptation of a rulebook is.
