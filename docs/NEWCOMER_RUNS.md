# Two newcomers, measured

**2026-08-15, against `b2ceeac`.**

There is an obvious path onto this engine for someone who does not want to
hand-write C++: clone the repo, put a coding agent on it, and ask for a
game. That is easy to assert and we did not want to assert it. So we ran it
twice and wrote down what happened, including the parts that do not
flatter us.

## Method

Two clean clones of the public repository at `b2ceeac`. Two coding agents,
each with no prior knowledge of the engine, no access to anything outside
its own clone, and nobody to ask. One instruction each: build a playable
Pacman, build a playable Tetris.

Each was told to keep a log as it went, to be unflattering, to record every
wall with the exact error, and to record it as a finding rather than a
detail if it had to change engine code to make a game work. Neither was
given a hint, an entry point, or a correction.

## The prompt, verbatim

Both agents got the same text, differing only in the game and the path.
It is longer than a beginner needs because it is an experiment protocol,
not an onboarding script: most of it is about keeping an honest log.

> You are a newcomer to a codebase you have never seen. You are a
> competent programmer, but you know nothing about this engine, and
> nobody is available to help you. Everything you learn must come from
> the repository itself.
>
> The repository is at `<path>`. Work ONLY inside that directory.
>
> YOUR GOAL: build a playable Pacman on this engine.
>
> TWO DELIVERABLES, both required: the game, as far as you can get it;
> and a log at `<repo>/NEWCOMER_LOG.md`, written AS YOU GO, not
> reconstructed at the end. Every document you opened and whether it
> earned the time. The first moment you were confused, quoted. Every
> wall: the exact error, what you tried, whether you got past it.
> Anything you had to guess because the docs did not say. Anything you
> expected the engine to provide that it did not.
>
> RULES: Do not ask questions, there is nobody to ask. Do not soften the
> log; a log full of praise is a failed run. You may change engine code,
> but record every time you had to, because that is a finding. Prefer
> the documented path, and if you invent a workaround, first record that
> the documented path failed. Stop when you have a running game, or when
> one wall has consumed an unreasonable amount of effort.

Note what is NOT in it: no hint about where to start, no mention of
`IApplication`, no entry point, no correction when they went wrong.

## Results

| | Logomanpac (maze) | Logotriste (blocks) |
|---|---|---|
| Builds | Yes. Documented command, verbatim, first try | Yes. Same |
| Runs | Windowed and headless | Windowed and headless |
| Plays | Yes | Yes |
| Engine source changed | **None** | **None** |
| Diff outside the game directory | One `add_subdirectory` line, one CHANGELOG entry | The same two |
| Wall clock, clone to playing | **29 min 20 s** | **24 min 54 s** |

What "plays" means, in their own accounting. Pacman: 28x31 maze, 246
pellets, wrapping tunnel, four energizers with the 200/400/800/1600 chain,
four ghosts with the four distinct chase targets, scatter and chase waves,
the ghost-house release schedule, eyes returning to the house, three lives,
score and HUD. Tetris: seven pieces, four rotations with wall kicks,
level-scaled gravity, soft and hard drop, multi-line clears, guideline
scoring, the speed curve, top-out, restart, live HUD.

Each verified its own work rather than declaring victory. Pacman: 21
assertions headless, 27 with framebuffer pixel assertions, and a 90-second
bot run that ate 189 pellets and was never inside a wall across 5,399
frames. Tetris: 26 assertions in a standalone binary, a headless soak
clearing 1,600 lines on seed 1, and GPU screenshots.

## What it cost them

This is the part worth having. Findings both runs hit independently are
marked, because two strangers tripping over the same step is a different
signal from one.

**1. The tutorial does not compile. (Both runs.)**
`docs/GETTING_STARTED.md:118` and `docs/GAME_LAYER.md:21` both instruct the
reader to write `void display_framebuffer(uint8_t* buf, int w, int h)
override;`. That method was deleted: `src/platform/macos_platform.mm:32`
records it as "removed in Phase 6". The documented line is a compile error.
The same paragraph says to derive from `IApplication`, while both shipping
examples derive from `MacOSPlatform` / `PlatformLayer`, which no onboarding
document mentions.

**2. There is no small example. (Both runs.)**
The smallest game in the tree is `examples/logotron/src/logotron_app.h` at
3,180 lines in one header. `GETTING_STARTED.md:142` points at
`examples/eden/src/main.cpp`, 2,165 lines, as the working reference. That a
game owns its own `main()` and drives update, render, and present appears in
no document; one run found it by reading `examples/logotron/src/main.cpp:14`.

**3. Nothing tells you how to make a pixel appear. (Both runs.)**
`ProjectionMode::BirdsEye` is what makes a 2D game feasible here. It occurs
in five source files and **zero** documentation files. The same is true of
`set_pixels_per_unit`, `is_self_emissive`, and the convention that screen
centre is the camera position. Related: `ParticleSystem::assert_above_turtle`
(`src/core/particle_system.cpp:112`) aborts the process the first time you
centre a floor at z = 0. The guard is right and its message is good, and the
`TURTLE_LENIENT=1` escape hatch beside it is not mentioned anywhere a
newcomer would look first.

**4. The ontology generator cannot generate into a new game.**
`write_if_changed` (`scripts/generate_ontology.py:52`) calls
`path.write_text()` without creating the parent directory. Every existing
example already has `src/generated/` committed, so the documented Step 2
one-liner dies with a bare `FileNotFoundError` for any new game, and only
for a new game. One line to fix.

**5. The engine talks over the game. (Both runs.)**
`src/core/engine.cpp:1292` is commented `// TEMP: Log timing every 30
frames` and is guarded by nothing but `timing_frame % 30 == 0`. One run
measured roughly 434,000 lines of `[TIMING]` from an eight-second headless
run with `show_performance_metrics` and `show_debug_overlay` both false. The
other reported `[PARTICLE_DELETE_FLUSH]` on every pellet eaten and an
optimization-flags banner, also unconditional in Release.

## What changed because of it

All five landed in the same pull request as this record, because a
defect list that ships without its fixes is a to-do list wearing a
report's clothes.

1. `examples/minimal/` now exists: 248 lines, 65 of them comments, one
   file, no schema, no generated code. It builds in CI, and
   `GETTING_STARTED.md` points at it instead of at a 2,165-line game.
   The six things that are mandatory and unguessable are numbered in a
   comment at the top of it, in the order the runs discovered them.
2. `display_framebuffer` is gone from both documents, replaced by the
   `get_window()` that shipping games actually write.
3. `ProjectionMode::BirdsEye`, `set_pixels_per_unit` and
   `is_self_emissive` are notes 3, 4 and 5 in the minimal example, and
   the turtle rule is note 6.
4. `write_if_changed` creates its parent directory. One line, plus a
   comment saying it is invisible until somebody adds a game, which is
   why it survived this long.
5. Not fixed: the unconditional `[TIMING]` print. It is engine
   behaviour with a `TEMP` comment on it, not a documentation defect,
   and it is tracked separately rather than fixed in a docs branch.

Both games are in the tree as `examples/logomanpac/` and
`examples/logotriste/`, with the log each agent wrote beside it. They
were renamed after the fact: the prompts above say Pacman and Tetris
because that is what we typed, and what shipped carries our own names
rather than somebody else's trademark. Each agent's log is left exactly
as written, old paths and all, because a record that gets tidied
afterwards is not a record.

## What this does and does not establish

It establishes that the engine's capabilities were sufficient, and that they
were reachable without changing the engine. Both games came out of the
public API as it stands. Neither agent needed a favour from us.

It does not establish that a newcomer will have this experience.

- Two runs is two runs.
- Both agents were the same model family, so their blind spots correlate.
- The toolchain was already installed. Nobody measured `brew install glfw`,
  which is a real first-time reader's actual first step.
- Neither could visually confirm its own window: the shell was not a GUI
  login session, so both proved pixels through the framebuffer path rather
  than with eyes. The windows were created, the frames were presented, and
  no human watched either game run.
- Both were told to keep a log, which a real user would not do, and being
  observed changes behaviour at the margin.

The honest summary is that the engine held up and the documentation did not.
Every wall in the list above is a documentation defect except the generator
directory bug, and every one of them cost a competent reader time that the
engine had already made unnecessary.
