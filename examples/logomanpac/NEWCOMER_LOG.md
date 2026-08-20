> **Left exactly as written.** This log was kept live by the coding agent
> that built this game, in a clone where the directory was `examples/pacman/`
> and every identifier said `pacman`. Both were renamed to `logomanpac` before the
> game was merged. Nothing else here has been corrected, including the
> places where it is unkind about our documentation, because those are the
> parts that were worth having.

# Newcomer log: building Pacman on Logosphere

Start: Sat Aug 15 21:03:15 PDT 2026

I have never seen this engine. Everything below is what the repo told me,
in the order it told me, plus every place it did not tell me.

## Documents opened, in order

1. **`README.md`** — earned it. Told me the platform (macOS arm64 for the
   full engine), the three build profiles, the exact quick-start commands,
   and that there are four example games. The "Quick start" block is
   copy-pasteable and correct. Best document in the repo.
2. **`docs/GETTING_STARTED.md`** — mostly earned it, one stale block (see
   Confusion #1). Steps 1-8 are the right skeleton. Step 4's CMake snippet
   is accurate and I reused it almost verbatim.
3. **`include/application.h`** — earned it. This is the actual contract.
   Read this, not the doc snippets.
4. **`docs/GAME_LAYER.md`** — earned it, and it is dense. §3 event bus,
   §6 interaction profiles/transformation rules, §8 engine boundaries.
   Also carries the same stale `display_framebuffer` line.
5. **`examples/consumer-smoke/main.cpp`** + **`examples/logotron/src/main.cpp`**
   — earned it. `logotron/src/main.cpp` is the single most useful file for a
   newcomer: 103 lines, shows `EngineConfig`, `engine.initialize()`, the
   update/render/present loop, and headless handling. It should be what
   GETTING_STARTED links to, not the 2165-line `eden/src/main.cpp`.
6. **`examples/logotron/src/walls.h`** — earned it. Showed me how a static
   box particle is actually configured (`ParticleShape::BOX`,
   `ParticleOwner::STATIC`, `SetMaterial`, `is_self_emissive`).

## Build: worked first try

```
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --target logosphere eden -j 16
```
Exit 0, no intervention. `brew` already had glfw/cmake/pkg-config.
This is the only thing in this log that went entirely to plan.

## Confusion #1 (first one, ~8 minutes in)

`docs/GETTING_STARTED.md:118` and `docs/GAME_LAYER.md:21` both tell you to
write:

```cpp
void display_framebuffer(uint8_t* buf, int w, int h) override;
```

There is no such method on `IApplication`. I read
`include/application.h` end to end (168 lines) looking for it. It is not
there. `grep -rn display_framebuffer .` returns exactly four hits and
three of them are comments saying it was deleted:

- `src/platform/macos_platform.mm:32` — "display_framebuffer removed in
  Phase 6 of the Renderer/Display"
- `src/core/engine.cpp:87` — "now handled through EngineRenderState"

So both onboarding documents instruct a newcomer to `override` a method
that was removed. Writing it as documented produces
`error: only virtual member functions can be marked 'override'`. Cost:
about six minutes of not trusting my own reading of the header.

Same paragraph, second problem: both docs show `get_window()` as
something the game implements to hand the engine a window. Every
shipping example returns `nullptr`
(`examples/logotron/src/logotron_app.h:133`). The engine creates the
window itself from `EngineConfig::create_display`. Nothing in either
onboarding doc mentions `EngineConfig`. I only found it by reading
`examples/logotron/src/main.cpp`.

## Orientation, 21:03 to 21:09

Files read to answer "how do I actually put a thing on screen", none of
which any onboarding doc pointed me at:

7. **`src/particle_types.h` / `src/particle_core.h`** — the Particle
   struct. Note `include/particle.h` does not exist; `particle.h` lives
   in `src/`. GETTING_STARTED's CMake snippet adds `${CMAKE_SOURCE_DIR}/src`
   to the include path, which is the only reason `#include "particle.h"`
   resolves. It never says why.
8. **`src/core/engine.h`** — `EngineConfig`, `get_kg()`,
   `get_particle_system()`, `get_event_bus()`, `set_physics_enabled()`,
   `set_projection_mode()`. This is the real API surface and it is
   undocumented outside the header.
9. **`src/core/projection_mode.h`** — found `ProjectionMode::BirdsEye`
   ("Top-down orthographic"). Exactly what a Pacman needs. Zero mentions
   in any `docs/*.md`. I found it by grepping the source.
10. **`src/core/particle_system.h`** — `add_particle_to_entity`,
    `lock_particles_for_write()`, `get_entity_particle_indices()`.
11. **`examples/logotron/src/logotron_app.h`** (lines 1100-1230) — the
    only complete worked example of "make a static box appear at a world
    position and bind it to a KG entity". Six lines of Particle setup
    plus `ps.add_particle_to_entity(p, &kg, e)`. This snippet should be
    in GETTING_STARTED. It is not.
12. **`src/ui/text_window.h`** + logotron's `score_hud_` — the HUD path.
13. **`include/logosphere/rendering/font_renderer.h`**,
    **`include/logosphere/rendering/i_draw_surface.h`**.
14. **`schema/logosphere.yaml`** (the class hierarchy) and
    **`examples/eden/schema/eden.yaml`** — to learn what I can `is_a`
    from. `WorldEntity`, `Structure`, `Creature`, `LivingEntity`.
15. **`src/interaction/particle_interaction_system.cpp`** (lines
    400-545) — to confirm the `delete` / `fade_out` transformation
    effects exist, because I need to remove a pellet when it is eaten
    and the only deletion API on ParticleSystem is labelled
    "TEST HELPERS: ... Safe immediate deletion for tests"
    (`src/core/particle_system.h:132,146`). See Gap #2.

### Gap #1: there is no documented "how do I see anything" path

Neither GETTING_STARTED nor GAME_LAYER tells you how to get a particle
rendered. GETTING_STARTED Step 5 jumps straight to body plans and
capability aggregation, which a maze game does not want. What I actually
needed was: pick a projection, set `pixels_per_unit`, set the camera
position, and set `is_self_emissive` so you do not need to build a
lighting rig. I reverse-engineered all four from logotron.

`is_self_emissive` deserves a paragraph in GAME_LAYER and does not get
one. It is the difference between "my game is a black screen" and "my
game is visible", and the only explanation of it is a code comment in
`examples/logotron/src/walls.h:26-42`.

### Gap #2: no game-facing particle deletion

`ParticleSystem::delete_particle_immediate` sits under a comment block
headed `TEST HELPERS` (`src/core/particle_system.h:130`), and
GAME_LAYER.md:739 separately warns "the old immediate path raced
in-flight GPU frames". `queue_particle_deletion(index, frame_number)`
needs a frame number the game is not given. So a game that wants to
remove one object has three choices and the docs endorse none of them
directly. The path I took is the one GAME_LAYER §6 demonstrates for
Logotron's trails: declare a `TransformationRule` with
`trigger=on_timer`, `effect=fade_out`, then `arm_transformation()` on
the particle's `KGParticleID`s. That works, but "how do I delete a
thing" should not require reading the interaction-system chapter.

### Small stale thing

`GETTING_STARTED.md:273` shows `./build/mygame/mygame --no-head` as if
the engine parsed that flag. It does not. `./build/eden/eden --no-head`
opens a window anyway (verified: `[ENGINE] Mode is Interactive -
creating window...`). Each game parses `--no-head` in its own `main`;
logotron does, eden does not.

### Guessed, because nothing said

- Whether `ParticleOwner::STATIC` is safe for scenery. `particle_types.h:51`
  says "Reserved — only the turtle world boundary is truly immovable",
  which reads like "do not use this", but every example uses it for
  walls. Meanwhile `ParticleSolverMode::STATIC` was **deleted** on
  2026-08-14 with the comment "a body set STATIC fell silently"
  (`src/particle_types.h:84-88`). Two enums, same name, opposite
  status, one deleted three days ago. I sidestepped the whole question
  by calling `engine.set_physics_enabled(false)` — a maze game owns
  every position anyway.
- The world-unit-to-screen scale. `set_pixels_per_unit` has no
  documented range. Logotron uses 24 for a 50 m arena. I guessed and
  tuned by eye.

## Building it, 21:09 to 21:35

Plan: `examples/pacman/` with a LinkML schema, a pure `maze.h/.cpp` with
no engine dependency, a header-only `PacmanApplication`, a `main.cpp`,
and a headless acceptance test that plays the game.

### Wall #1 — the ontology generator does not create its own output directory

Documented path, GETTING_STARTED.md Step 2, verbatim:

```bash
python scripts/generate_ontology.py
```

Result:

```
Generating pacman_ontology.h from pacman.yaml...
Traceback (most recent call last):
  ...
  File ".../pathlib.py", line 1013, in open
    return io.open(self, mode, buffering, encoding, errors, newline)
FileNotFoundError: [Errno 2] No such file or directory:
  '.../examples/pacman/src/generated/pacman_ontology.h'
```

A bare Python traceback, no message, no hint. The generator discovers
schemas by globbing `examples/*/schema/**/*.yaml`
(`scripts/generate_ontology.py:329`) but never `mkdir -p`s the
`src/generated/` it is about to write into. Every existing example has
that directory committed, so the failure only ever hits the first
person creating a new game — exactly the person following the tutorial.

Cost: two minutes, because the traceback happens to name the missing
path. Fix on their side is one line: `path.parent.mkdir(parents=True,
exist_ok=True)` in `write_if_changed` (`scripts/generate_ontology.py:52`).
Workaround on mine: `mkdir -p examples/pacman/src/generated`.

Credit where it is due: after that, the generator worked perfectly.
It emitted a correct 47 KB registry and an 84 KB header from my 307-line
YAML, first try, and it did not touch any other game's generated files
(`git status` clean apart from mine). The ontology path is the best part
of this repo.

### Wall #2 — the world turtle aborts your process on the first floor you place

First run of the acceptance test:

```
[pacman] board 28x31, 246 pellets, 0 defect(s)
[TURTLE VIOLATION] add_particle: body placed BELOW the world floor.
  z=0 thickness=0.2 => bottom=-0.1 < TURTLE_Z=0 (by 0.1 m). The turtle
  will lift this body every substep forever and the lift is free energy.
  Place it so its BOTTOM sits at or above 0.
[TURTLE VIOLATION] TURTLE_STRICT set — aborting so the placement is fixed
  rather than absorbed.
```

This is `std::abort()`, strict by default
(`src/core/particle_system.cpp:112`), and it fires even though I had
already called `engine.set_physics_enabled(false)`.

The error message is excellent. It names the value, the threshold, the
margin, and the fix, in one sentence. It is the best diagnostic I hit
all night and it cost me about ninety seconds.

The problem is that nothing tells you the rule beforehand. `z` is a
CENTRE, and the rule is about the BOTTOM. GETTING_STARTED never places
a particle at all, so it cannot warn you. GAME_LAYER never mentions the
turtle. `docs/ARCHITECTURE.md` and `docs/ENGINE_INVARIANTS.md` do —
but a newcomer building a game reads the two documents titled "Getting
Started" and "Game Layer", and neither says "your first floor will
abort the process if you centre it on zero." The one place the rule is
actually demonstrated is a code comment in
`examples/logotron/src/logotron_app.h:1207`: "The arena rests ON the
turtle; half its own thickness does that."

Fix: floor centre = half its thickness. One line.

### Wall #3 — four failing assertions, three of them mine

First green build of the AT gave 14 passed, 5 failed. Worth writing
down which, because the split is the interesting part:

1. `a pellet already eaten does not score again` — MY test bug. I wrote
   `app.queue(Dir::NONE)` expecting the avatar to stop. In Pacman you
   cannot stop; the heading persists and the avatar kept eating. Fixed
   by parking it against a wall, which is the only brake the game has.
2. `avatar reached the west energizer cell` — MY maze misreading. Row
   23 is walled at columns 4-5, so "walk west from the start" reaches
   column 6 and stops. Fixed by adding a five-leg route driver to the
   test.
3. `a walker leaving the west edge reappears in the east` — MY wrong
   expectation. Measured output was `ended at col 18 (min seen -0.4,
   max seen 27.47)`. The wrap worked perfectly; I asserted on the
   final column instead of the maximum column visited, and 120 frames
   at 8 cells/s carries you 16 cells past the wrap. The printed
   `[measure]` line is what told me; an assertion without it would have
   sent me into the wrap code.
4. `all four ghosts left the house` — a real behaviour I had not
   thought through, surfaced by the test. The avatar was standing still
   and got eaten repeatedly; each death resets the round, which resets
   the ghost release timers, so a snapshot at t=22 s answers a
   different question than the one I was asking. Changed the test to
   sample every frame and assert "each ghost got out at least once",
   and to press R on GAME_OVER.

Total cost: about twelve minutes. Nothing to do with the engine. Worth
logging only because it is the shape of the work: the engine got out of
the way and I was left arguing with my own game.

### Wall #4 — I rendered the game in the wrong colours and it still looked right

Wrote a framebuffer dump so I could actually look at the thing. First
capture: a **red** maze with a **cyan** Pacman and a **blue** Blinky.

I spent a few minutes suspecting the renderer before sampling a wall
pixel and comparing it to the colour I had set. `Engine::read_latest_framebuffer`
is documented, correctly, as BGRA (`src/core/engine.h:358-361`). I had
written the low byte out as red. The engine was right; I was wrong.

Recording it because of the failure mode, not the mistake: a
channel-swapped Pacman still looks like a Pacman. The eyeball check
passes. The only thing that caught it was sampling one pixel and
comparing it against a number I had typed myself.

There is no helper anywhere in this repo for "save the framebuffer to a
file so I can look at it." Every game will want that on day one.
`examples/predator/at_predator_hunger_visual.cpp` counts overlay pixels
but never writes an image. I wrote a nine-line PPM dumper.

### Things I had to guess

- **How to make anything visible.** Not documented. I copied
  `is_self_emissive = true` from `examples/logotron/src/walls.h` and
  the whole game needs no lighting because of it. Nobody says this.
- **Which projection.** `ProjectionMode::BirdsEye` exists in
  `src/core/projection_mode.h` and appears in no `.md` file in the
  repo. Found by grep.
- **That screen centre is the camera position, not the look-at
  target.** Found in a code comment at
  `examples/logotron/src/logotron_app.h:892`. Without it, "centre the
  board" is a guessing game.
- **`pixels_per_unit` scale.** No documented range or meaning. Derived
  it from `get_resolution_manager().get_render_width()`, which I found
  by reading logotron.
- **Whether particle render indices are stable.** They are not —
  deletion is swap-and-pop. But `get_entity_particle_indices(entity)`
  IS maintained across the swap
  (`src/core/particle_system.cpp:282`). I read the implementation to
  find that out, because nothing states it. If I had cached indices,
  the first eaten pellet would have started teleporting other objects.
- **Whether two `Engine` instances can exist in one process.** Nothing
  says. They can; the AT builds four. Found out by trying.
- **Whether `handle_key` on IApplication is actually reached.** Traced
  it: GLFW callback -> `InputSystem::handle_key_callback`
  (`src/core/input_system.cpp:177`) -> `KeyMapper::process_key_event`
  -> `application_->handle_key` (`src/key_mapper.cpp:46`). The AT now
  steers through that whole path rather than calling my handler
  directly.

### Things I expected the engine to provide and it did not

- **A save-the-frame helper.** See Wall #4.
- **A game-facing "delete this object" call.** See Gap #2 above. What
  exists is a test helper the docs warn against, an index-plus-frame
  number API the game has no frame number for, and a declarative
  transformation rule three chapters into a different document.
- **A quiet Release build.** Running the game prints, unconditionally,
  `[TIMING] Frame N | Dynamics=... BVH=... Physics=...` every 30
  frames, `[PARTICLE_DELETE_FLUSH] Frame N - deleting 1 particles` on
  every pellet, and a `=== OPTIMIZATION FLAGS STATUS ===` banner at
  startup. My game's own output is a minority of its stdout. There is
  no switch for this that I could find.
- **Warning-clean engine headers.** `-Wall -Wextra` on the example
  target, which is what GETTING_STARTED's own CMake snippet implies
  and what every shipped example does, produces 27 warnings from
  engine headers before my code is even compiled
  (`include/application.h:52`, `src/ui/widget.h:70-79`,
  `include/logosphere/physics/physics_system.h:297`, and others). A
  newcomer cannot tell their warnings from the engine's.

### One thing that is genuinely unusual, in a good way

I never touched engine code. Not one line. Everything above is docs,
guesses and my own bugs. For a from-scratch engine with a hand-rolled
rasterizer, a game that was not in anybody's plan compiled against it
and ran correctly without a single engine patch. That is rarer than it
sounds and it should be said plainly.

The `git diff` outside `examples/pacman/` is two lines: one
`add_subdirectory` and one CHANGELOG entry.

## Final state

**Builds.** `cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"` then
`cmake --build build -j 16`. Whole repo, exit 0, zero errors, including
the other four example games and the combined test harness. Two targets
added: `build/pacman/pacman` and `build/at_pacman`.

**Runs.** Windowed and headless. `./build/pacman/pacman` opens a window
and renders at 60 fps. `./build/pacman/pacman --no-head --exit-after 12`
runs the same game with no window and reports its final score.

**Plays.** Yes. Verified two ways.

Headless, `./build/at_pacman`, 21 assertions, all green, all printing
the values they assert on:

```
[measure] cols=28 rows=31 pellets=246 defects=0
[measure] pushed UP for 0.8 s: row 23 -> 23
[measure] CONTROL, pushed LEFT for 0.8 s: col 13 -> 7.88
[measure] parked against the wall at col 6 (still 6 after 1.5 s): score 80 -> 80
[measure] avatar at col 1 row 23, score 230, frightened ghosts 4/4
[measure] after 8 s more: frightened 0/4
[measure] walked LEFT off the west edge, ended at col 18 (min -0.4, max 27.47)
[measure] ghosts that left the house during 30 s: 4/4, deaths=1
[measure] 90 s of bot play: score=1410 pellets eaten=189 of 246
          deaths=1 ghosts eaten=2 frames inside a wall=0/5399
```

On screen, `LOGOSPHERE_VISUAL=1 ./build/at_pacman`, 27 assertions, all
green, asserted as pixels rather than promised:

```
[measure] framebuffer 1100x1051 lit=467944 blue=429538 yellow=1321
[measure] UI overlay lit=23400 glyph=1343
```

The captured frames show a blue 28x31 maze, tan pellets, four corner
energizers, a yellow avatar, red Blinky outside the house, and Pinky,
Inky and Clyde inside it behind a pink door sill. Mid-run captures show
cleared corridors, a live score, and all four ghosts flashing white
during the energizer warning.

**What works:** grid movement with the arcade's turn-commit rule, walls,
the wrapping tunnel, 246 pellets, four energizers, FRIGHTENED with the
200/400/800/1600 chain, eyes returning to the house and coming back out,
all four distinct chase targets, SCATTER/CHASE waves with the reversal
tell, the ghost-house release schedule, three lives, death and restart,
score and high score, a HUD, and a board that verifies its own
connectivity at startup.

**What is missing:** one level only (clearing the board stops rather
than re-laying it), no fruit bonus, no sound (the engine has no audio),
and the avatar is a pulsing sphere rather than a chomping wedge because
the engine has no sprite path — that is a deliberate engine decision,
not a gap.

**Engine code touched: none.** The diff outside `examples/pacman/` is
one `add_subdirectory(examples/pacman)` line in the root `CMakeLists.txt`
and one CHANGELOG entry.

**Written:** `examples/pacman/` (schema 307 lines, `maze.h/.cpp` 500,
`pacman_app.h` 933, `main.cpp` 74, `at_pacman.cpp` 468, plus
`PACMAN.md`), and generated ontology output.

## If I could change one thing in this repository

Put the twelve lines that make a particle appear on screen into
`docs/GETTING_STARTED.md`, before Step 5. Not a link to an example — the
literal lines:

```cpp
engine.set_projection_mode(ProjectionMode::BirdsEye);   // or Isometric
engine.get_camera_system().set_position(0, 0, 30);      // screen centre IS this
engine.get_camera_system().set_pixels_per_unit(30.0f);

Particle p{};
p.shape = ParticleShape::BOX;
p.x = 0; p.y = 0; p.z = 0.5f;      // z is a CENTRE; the BOTTOM must be >= 0
p.width = p.height = 1.0f; p.thickness = 0.6f;
p.r = 0.2f; p.g = 0.4f; p.b = 1.0f; p.a = 1.0f;
p.is_self_emissive = true;          // colour IS the pixel; no lighting rig needed
p.SetMaterial(Materials::Type::STONE);
engine.get_particle_system().add_particle_to_entity(p, &kg, my_entity);
```

Everything in that block cost me time to find, none of it is in any
document, and all of it is the first thing anybody building a game needs.
Half of them are hard requirements the engine will abort over. Right now
the only place the whole set appears together is inside a 3180-line
example, and the two documents named "Getting Started" and "Game Layer"
open instead with a method signature that was deleted from the codebase.

End: Sat Aug 15 21:32:35 PDT 2026
