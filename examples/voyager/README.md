# Voyager

One screen. Six numbers are rolled, a referee says where that person
came from, a handful of careers are offered, you take one. Then the
game's own book takes over, season by season: you choose how each is
spent, the director rates every kind of trouble as a probability and
the engine draws each, and when one lands the director dresses it,
states its weight, and writes three doors, each with a chance and two
lists, what it risks and what it reaches for. There is always a fourth
door: say what you would do, and the director prices it. The engine
draws, exactly the chosen list lands, the telling goes on your sheet
with the marks, standings and turns it left, and the people and
places the director named are now part of the world. It ends when you
say it does, or when a turn ends the life. That is as far as the book
is written, on purpose.

It is plumbing, and the plumbing is the point: everything the rulebook
fixes lives in the knowledge graph and is read from where it is used,
so the next thing built on top of it does not have to unpick a C++ copy
of the rules first.

```
cmake -S . -B build && cmake --build build -j

# windowed (full profile only: it needs a window)
ANTHROPIC_API_KEY=... ./build/voyager/voyager

# headless: no key, reproducible, exercises the rules
./build/voyager-headless --random 7

# headless: a model plays, every answer taped
#   --record needs a build with the LLM library, which the
#   headless-only profile does not have. It says so rather than
#   quietly playing without one.
ANTHROPIC_API_KEY=... ./build/voyager-headless --record /tmp/life.tape

# headless: the same character, exactly, offline, no model call
./build/voyager-headless --replay /tmp/life.tape

# what the graph holds of the game's own book, in the words it cites.
# The writing loop's endpoint: edit a chapter under
# corpora/voyager-book, run tools/regenerate_seeds.sh, read back.
./build/voyager-headless --book
```

## No API, no game

The referee answers two questions the rules leave open: where this
person comes from, and which careers are worth offering *this* person.
Neither is derivable, so there is no fallback and there will not be
one. Without a key the windowed game says so on screen and stops before
a die is thrown.

`--random N` is not a fallback and is not the game. It is the fuzzing
lane: a seeded generator answers every question, including the
referee's, and it says so in the prose it hands back rather than
producing a stand-in narration you could mistake for a real one.

## What is in the graph, and what is not

Everything the book fixes is in the graph:

| Thing | Where it comes from |
|---|---|
| The six characteristics, their order and their short names | `Characteristic` entities, extracted, cited to the numbered profile list |
| What each is rolled with | `characteristic_dice`, a `DiceExpression` entity |
| Score to modifier | a `LookupTable` of `CharacteristicModifierEntry` rows, cited cell by cell |
| The age of majority | a `RuleConstant`, read where it is used |
| 24 careers, what each is | `Career` entities, cited to the book's own description |
| Qualification and survival throws | `TaskCheck` entities, each cited to the CELL it was printed in |
| The order the steps run in | a `Procedure` of three `ProcedureStep`s, each naming a primitive |
| What was chosen, from what field, by whom | an `ArbiterDecision`, pointed at by the character |
| Where the person comes from | a `Narration`, held by the character |

What is in C++ is the mechanism and nothing else: three primitives, a
screen, and a referee that speaks HTTP. No characteristic is named in
it, and `test_characteristics_from_graph` scans every shipping source
to prove it.

## Readings this game makes

Extraction judges structure. Three judgements are this game's and are
worth arguing with; the first two live in the extractor's own docstring
and this is the third.

**`determine_background` cites "Determine homeworld."** The book's
checklist has a step for deciding where a character comes from, under
an optional Homeworld heading, and this slice's referee call is that
step. It is a reading, not a transcription: the book asks for a
homeworld and this asks for a life. Nothing else in the chapter is a
checklist step for the same thing, and citing nothing would have meant
a procedure step with no address at all.

## The seeds

`seeds/voyager_cepheus_rules.json` carries `generated_by` and is the
extractor's. Never hand-edit it: teach `tools/extract_chargen.py` and
run `tools/regenerate_seeds.sh`.

`seeds/voyager_chargen_procedure.json` is hand-authored and carries no
`generated_by`. Which steps run in which order is a composition
decision, not something read out of the book; the citations on each
step say which instruction it serves.

## The tape

A recorded run holds the dice seed, the two referee answers, and the
player's pick. The background is written by a model and is not
derivable from the seed, so it is taped like any other answer, at a
free-form site:

```
{"kind":"rules","edition":"ingestion-edition:v1:7:cepheus:sha256:..."}
{"kind":"seed","site":"chargen","answer":"..."}
{"kind":"ask","site":"referee.background","answer":"<the prose>", ...}
{"kind":"ask","site":"referee.careers","answer":"Agent | ...\nNoble | ...", ...}
{"kind":"ask","site":"chargen.career","answer":"Agent","offered":["Agent","Noble",...]}
```

The model-authored option set reaches the tape twice over: as the
answer to `referee.careers`, which is what rebuilds the screen on
replay, and as `offered` on the player's question, which is what makes
the run forkable.

```
./build/voyager-headless --forks /tmp/life.tape
[3] took 'Agent', could have taken 'Diplomat' 'Bureaucrat' 'Noble' 'Entertainer'

./build/voyager-headless --fork /tmp/life.tape --at 3 --instead Noble
```

A fork keeps the trunk's seed, so the character is the same person and
exactly one thing changed.

## Environment

| Variable | Default | What it does |
|---|---|---|
| `ANTHROPIC_API_KEY` | — | required, unless `VOYAGER_LLM=mlx` |
| `VOYAGER_LLM_MODEL` | `claude-haiku-4-5-20251001` | which model referees |
| `VOYAGER_LLM` | — | `mlx` to use a local server instead |
| `VOYAGER_LLM_URL` | `http://127.0.0.1:8081` | where that server is |
| `VOYAGER_REFEREE_TIMEOUT_MS` | `45000` | how long a call may take |
| `VOYAGER_REFEREE_MAX_TOKENS` | `1024` | reply budget, sized not defaulted |

## Tests

Neither calls a model. Both run in the headless profile.

- `test_characteristics_from_graph` — the claim this game exists to
  keep. Adds a seventh characteristic to the graph at run time and
  fails if the sheet does not grow, then scans every shipping source
  for the words the graph owns so the first check cannot pass against a
  hardcoded list.
- `test_voyager_rules` — the seeds through the ingestion verifier, then
  one whole character with a scripted referee, then the refusals: no
  referee, an invented career, an empty offer, a referee that fails.
