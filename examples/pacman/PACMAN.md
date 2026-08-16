# Pacman

The arcade maze chase, on Logosphere. Written as the smallest complete
game the engine can carry: a board, an avatar, four pursuers with
different minds, food, a win condition and a lose condition.

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --target pacman at_pacman -j

./build/pacman/pacman                       # play
./build/pacman/pacman --no-head --exit-after 20   # no window, same logic
./build/at_pacman                           # headless acceptance test
LOGOSPHERE_VISUAL=1 ./build/at_pacman       # + framebuffer pixel assertions
```

Arrows or WASD steer. `R` restarts. `ESC` quits.

## What it is

A 28x31 board, 246 pellets including four energizers, a tunnel on row
14 that wraps east to west, and a ghost house that releases its
occupants on a schedule.

The four ghosts differ only in where they aim, which is the whole
design of the original:

| Ghost | Chase target |
|---|---|
| Blinky | the avatar's cell |
| Pinky | four cells ahead of the avatar |
| Inky | the avatar two ahead, reflected through Blinky |
| Clyde | the avatar when far, his own corner when within eight cells |

Every ghost alternates SCATTER and CHASE on the arcade's level-one
schedule and reverses direction the moment the wave turns, which is how
you can see the mood change without being told. An energizer flips every
ghost to FRIGHTENED for seven seconds; eating them chains 200, 400, 800,
1600. Eyes return to the house at speed and come back out.

## How it sits on the engine

Four decisions carry most of the weight, and each is a fact about the
engine worth knowing before you write your own game.

**The camera is `ProjectionMode::BirdsEye`.** Top-down orthographic,
defined in `src/core/projection_mode.h`. Screen centre is the CAMERA
POSITION, not a look-at target (`src/projection_system.cpp:190`), so
centring the board means parking the camera on the board's centre. The
camera sits 1.6 cells north of centre to leave a band at the top for the
HUD.

**Every particle is `is_self_emissive`.** That flag makes the deferred
shader treat `colour * emission_strength` as the final pixel and skip
lighting. Pacman is a flat 2D game; with it set, the whole thing needs
no sun, no lights, no shadow rig and no BVH. Without it, the first run
is a black screen.

**Physics is off.** `engine.set_physics_enabled(false)`. Positions here
are grid-constrained and written by the game every frame; a solver would
only have opinions to argue with. Note that the world-turtle guard still
applies at particle creation: every body's BOTTOM must sit at or above
z = 0 or `ParticleSystem::assert_above_turtle` aborts the process.

**A pellet is removed by a `TransformationRule`.** `trigger=on_timer`,
`effect=fade_out`, `duration_s=0.12`, armed with the pellet's
`KGParticleID`s when it is eaten. The interaction system ramps the alpha
and hands the index to the engine's deferred deletion queue, which is
the path that is safe against in-flight GPU frames. See
`docs/GAME_LAYER.md` §6.

## The graph is the record

Score, lives, remaining pellets and round phase live on one `Maze`
entity. Each ghost carries its name, mood, and the cell it is steering
toward *this frame*. The avatar carries its continuous cell position and
both its committed and queued heading. All of it through `setProperty`,
so a subscriber on `state_changes` sees the entire game without the game
knowing it is being watched.

```cpp
kg::Query q;
q.types = {"Ghost"};
q.props = {"ghost_name", "ghost_mood", "target_col", "target_row"};
auto rows = kg::run_query(engine.get_kg(), q);
```

## Layering

| File | Depends on | Why |
|---|---|---|
| `src/maze.h` / `maze.cpp` | nothing | The board and the movement rules, pure. Testable with no engine at all. |
| `src/pacman_app.h` | engine, KG | The same rules bound to entities and particles. |
| `src/main.cpp` | the above | Argument parsing and the loop. |
| `at_pacman.cpp` | the above | Plays the game headless. |

`maze.cpp` carries `Board::validate()`, which flood-fills the board from
the avatar's start and reports any pellet it cannot reach, any row of
the wrong width, a walled tunnel, or a ghost house with no exit. A maze
is a hand-typed data file with no compiler; that function is its
compiler, and it runs on every startup.

## The acceptance test

`at_pacman` drives the same `PacmanApplication` the window drives — not
a reconstruction of it — and prints every number it asserts on.

It carries controls where a control is what makes the check mean
anything. "UP into a wall does not move the avatar" is satisfied by an
avatar that never moves at all, so the next line pushes LEFT down an
open lane and requires more than two cells of travel. "A walker leaving
the west edge reappears in the east" is paired with the same walk on a
walled row, which must stop at column 1.

The last section runs a wandering bot for 90 simulated seconds and
checks, on every one of the 5400 frames, that the avatar is not standing
inside a wall. That is the check that would catch a maze-geometry or
turn-commit bug the unit checks cannot see.

With `LOGOSPHERE_VISUAL=1` it opens a real window, renders, reads the
framebuffer back and asserts the maze is blue, the avatar is yellow and
the HUD reached the overlay buffer as glyphs rather than as an empty
panel. `LOGOSPHERE_SHOT=<path>` writes that frame out as a PPM, and
`LOGOSPHERE_SHOT_AFTER=<seconds>` lets the bot play first so the capture
shows a game in progress.

Note when reading pixels back: `Engine::read_latest_framebuffer` hands
you **BGRA**, little-endian, so the low byte is blue. Getting that
backwards produces a red maze with a cyan Pacman, which still looks
enough like a Pacman to pass an eyeball check.

## Known gaps

- One level. Clearing the board sets `LEVEL_CLEARED` and stops; it does
  not re-lay the pellets and speed the ghosts up.
- No fruit bonus.
- The avatar is a sphere that pulses instead of a wedge that chomps.
  The engine has no sprite or mesh path by design, and a mouth would
  have to be built out of particles.
- Ghosts are spheres, not the classic sheet-with-eyes silhouette.
- No sound. The engine has no audio.
