# Recording and replaying a run

Run a game with no window, capture what happened and what was decided,
and play it back exactly. Or play it with a model. Or with nobody at
all.

Three engine pieces, adoptable separately. A game needs none of them to
work, and can take one without the others.

| Piece | Answers | Needs from your game |
|---|---|---|
| `HeadlessRun` | how the frames advance | nothing, if you tick an `Engine` |
| `RunRecorder` | what the engine **did** | nothing at all |
| `RunTape` | what was **decided** | route your decisions through it |

The split between the last two is the important idea, so it comes
first.

---

## Facts and decisions are different things

**Facts** are what the engine did: dice rolled, properties changed,
relations made, things spawned and died. The engine emits these, so
`RunRecorder` takes them with **no cooperation from your game**.

**Decisions** are what could not be derived: what a player chose, what
a model replied, which seed a stream started from. The engine never
sees these. A decision is a value handed to your game and dropped, so
only your game can record one.

They fail differently, which is why they are separate mechanisms:

- a fact the recorder misses costs you **fidelity**;
- an input the tape misses makes a replay **silently wrong**.

So: **record facts to diff runs. Record decisions to reproduce them.**
You usually want both.

---

## `RunRecorder` — what happened

```cpp
#include "logosphere/replay/run_recorder.h"
#include "logosphere/telemetry/session.h"

logosphere::EventBus bus;                    // declare BEFORE the session
logosphere::telemetry::Session telemetry;    // (see Lifetime below)
world.set_event_bus(&bus);

auto* facts = static_cast<replay::RunRecorder*>(
    telemetry.register_instrument(
        std::make_unique<replay::RunRecorder>(bus)));
```

You get `run.jsonl` in the session directory:

```json
{"seq":0,"channel":"state","target":"2751","property":"strength","value":"8","prev":""}
{"seq":1,"channel":"dice","roll_id":1,"expression":"2D6","total":8,"stream":"chargen"}
```

One dense sequence across **all** channels, so the file is the order
things happened in. Two identical runs produce identical bytes, which
is what makes golden-file testing possible.

### Attach it after your world is built

Loading rule data writes properties, and those are facts too. In
Logovger, attaching before the rulebook loaded produced **35,597
records before the first die was rolled**; attaching after produced
**55**. Attach the bus once the static content is in and the run
proper begins.

### Name your ids

Creating an entity emits nothing, so a trace is deltas against numbers.
`snapshot_kg` writes the world once so those numbers mean something:

```cpp
kg::Query who;
who.types = {"Character", "SkillRating"};   // or who.facets = {"world"}
facts->snapshot_kg(world, who);
```

A default-constructed `Query` selects nothing useful. Name types or
facets.

### What is recorded by default

Dice, property changes, relations, damage, deaths, spawns and
transformations. Collisions are **off** (thousands per second, no
identity beyond ids), as are perception, volume and contact-filtered.
Turn any of them on through `RecordSpec`.

---

## `RunTape` — what was decided

One interface, three sources. Your game asks; where the answer comes
from is the caller's choice, and your game **cannot tell the
difference**.

```cpp
#include "logosphere/replay/run_tape.h"

replay::Ask ask;
ask.site    = "chargen";              // stable name; replay matches on it
ask.prompt  = session.prompt();       // recorded, never matched on
ask.offered = {"1", "2", "finish"};   // empty = free-form text

std::string answer, error;
if (!tape.ask(ask, answer, error)) return fail(error);
```

| Source | Where the answer comes from | Use |
|---|---|---|
| `LiveInput` | your callback: a UI, stdin, a model | recording |
| `TapedInput` | a previous run, in order | replay |
| `RandomInput` | a seeded generator | fuzzing |

Seeds go through it too, or the dice diverge and nothing else matters:

```cpp
dice.seed_stream("chargen", tape.seed("chargen", fallback));
```

A live source returns your fallback, so pass **entropy** there and the
recorded seed keeps the run reproducible anyway. Passing a constant is
how every recording ends up on the same dice.

A seeded source ignores the fallback and hands out its own seed, so one
number reproduces the entire run: the dice and every answer. That is
why Logovger refuses `--seed` alongside `--random` rather than
accepting a flag with nothing to do.

### A mismatch aborts

A tape that no longer fits the code stops the run and names what it
expected: wrong site, an answer the rules no longer offer, or a run
that wants more than the tape holds. It never falls back to asking
live, because a replay that quietly becomes a live run is a test
reporting success for a build it never exercised.

It also refuses to **record** an answer that was never offered, so a
bad live source cannot write a tape that only fails later.

### Fuzzing free-form sites

`RandomInput` picks uniformly from `offered`. For a free-form ask
(`offered` empty) it returns **empty**, because a generic engine has no
idea what a plausible answer looks like in your game. Supply one:

```cpp
replay::RandomInput source(seed,
    [](const replay::Ask& ask, uint64_t roll) {
        return my_game::synthetic_reply(ask.prompt, roll);
    });
```

---

## `HeadlessRun` — driving frames

For games that tick. Skip it if yours is turn-based: Logovger's chargen
never touches the `Engine` at all.

```cpp
EngineConfig config;
config.create_display = false;
engine.initialize(config);

replay::HeadlessRun run(engine, {.dt = 1.0 / 60.0, .max_frames = 600});
const uint64_t frames = run.run([&](uint64_t) { return !game.done(); });
if (run.exhausted()) { /* hit the cap rather than finishing */ }
```

Fixed dt, and `update()` only: never `render()`, `present()` or
`poll_events()`. Game time is process-wide, so it resets by default;
turn that off with `reset_game_time` if you deliberately want a second
run to continue the first.

---

## Adopting it: the recipe

1. **Route decisions** through `tape.ask()` wherever you ask a human or
   a model.
2. **Route seeds** through `tape.seed()` instead of entropy.
3. **Attach a `RunRecorder`** to your bus, after your world is built.
4. **Drive with `HeadlessRun`** if your game ticks.

Steps 1 and 2 need thought, because only you know where your decisions
are. Steps 3 and 4 are mechanical.

### Worked example: Logovger

`examples/logovger/logovger_headless.cpp` is the reference. It plays a
whole character with no window:

```
logovger-headless --random 7          # no key, no model, reproducible
logovger-headless --record life.tape  # a model plays; every answer taped
logovger-headless --replay life.tape  # exact, offline, free
```

The **referee** goes through the same seam as the player, which is why
`--random` needs no API key and `--replay` never calls a model: "which
characteristic gives way" is a decision like any other.

### What a frame-based game with an LLM would do

A game like Logotron, whose Director asks a model between rounds and
whose responder is already a `std::function`, needs:

- its responder wrapped so the reply goes through `tape.ask()` as a
  free-form site, plus a synthetic-reply stub for `--random`;
- any `std::random_device` seeding replaced by `tape.seed()`;
- its hand-rolled headless harness replaced by `HeadlessRun`;
- a `RunRecorder` on the engine's bus.

None of that needs an engine change.

---

## What replay can and cannot promise

**Byte-exact replay holds for**: headless runs at a fixed dt, with no
rendering, no async chunk streaming, no procedural rock generation, and
no un-taped external input, **on the same binary**.

That is not a toy subset. It is the whole of Logovger's chargen and
most of what `tests/` already does.

**It does not hold across** compilers or platforms (float association
differs), for interactive runs, or for anything reading the shared
global `rand()`, which several engine subsystems still do without
seeding. Those are known and listed in the design notes; none of them
is in the path of a turn-based or fixed-step run.

If you need certainty for your own game, record a run twice and diff
the two `run.jsonl` files. That answers the question for your code
rather than for the engine's in general.

---

## Lifetime

`Session` owns the recorder and destroys it; the recorder unsubscribes
from the bus on the way out. **Declare the bus before the session**, so
it outlives it. The other order is a use-after-free that only shows
under a sanitizer.

## Where things live

| Thing | File |
|---|---|
| Fixed-step driver | `include/logosphere/replay/headless_run.h` |
| Fact recorder | `include/logosphere/replay/run_recorder.h` |
| Decision tape | `include/logosphere/replay/run_tape.h` |
| Worked example | `examples/logovger/logovger_headless.cpp` |

| Behaviour | Test |
|---|---|
| Fixed dt, no clock, cap vs finish | `tests/at_headless_run.cpp` |
| Completeness, ordering, byte-stability | `tests/test_run_recorder.cpp` |
| Seeded repeatability, replay, abort on mismatch | `tests/test_run_tape.cpp` |

Related: [OBSERVING_CHANGE.md](OBSERVING_CHANGE.md) for why events are
transactional and what that rules out.
