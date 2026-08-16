# Tetris

Tetris on Logosphere. Ten columns, twenty rows, seven pieces, seen
from directly above.

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --target tetris test_tetris_well -j
./build/tetris/tetris                 # play
./build/test_tetris_well              # the rules, headless
```

Controls: LEFT / RIGHT move, UP or X rotate clockwise, Z rotate
counter-clockwise, DOWN soft drop, SPACE hard drop, R restart,
ESC quit.

## What is a particle here

Every occupied cell is one box particle. The falling piece is four of
them; a locked cell is one. There is no sprite layer and no tile map:
the board you see is the particle system, projected with
`ProjectionMode::BirdsEye`, which is the engine's top-down orthographic
projection and the reason a square well reads as a square.

Nothing in the scene is lit. Blocks and rails are `is_self_emissive`,
so the lighting pass has no work and the lights array stays empty. An
earlier version did put an overhead light in, and the engine
immediately complained:

```
[PARTICLE_SWAP WARNING] Light source particle at index 7 being
swapped to index 3 (emission=900000)
```

Deleting the falling piece's particles swap-and-popped the light.
`examples/logotron/src/walls.h:30` records that same churn crashing
the GPU command queue, so the light came out rather than the warning
being ignored.

## What is in the graph

`schema/tetris.yaml` declares three types and one enum:

| Type | One per | Carries |
|---|---|---|
| `TetrisWell` | the game | `well_columns`, `well_rows`, the frame particles |
| `TetrisPiece` | the game | the falling tetromino's kind, column, row, rotation |
| `TetrisMino` | locked cell | its column, row, and which piece it came from |

`Well` (`src/well.h`) stores `kg::EntityID` per cell, not a colour.
"Is this square taken" and "which entity is standing there" are one
question, so there is no second grid to drift out of step with the
graph. Regenerate the C++ from the YAML with
`python scripts/generate_ontology.py`.

Over a 436-line autoplay run — roughly 4,400 mino entities created
and destroyed — the graph ended at 122 entities and 331 KB.
`destroyEntity` reclaims; nothing accumulates.

## Nothing falls under gravity

A tetromino descends because a timer says so. The physics solver never
sees it: every particle is `ParticleSolverMode::KINEMATIC`, so
`inv_mass` is zero and the solver does not integrate it. Real gravity
would need friction, rest thresholds and stacking tolerances to hold
two hundred boxes on a grid, and would still drift off it. A grid game
wants exact cells, and exact cells are the game's job, not the
solver's.

## Flags

| Flag | Effect |
|---|---|
| `--no-head` | no window; needs `--exit-after` |
| `--exit-after N` | stop after N seconds |
| `--seed N` | fix the piece bag |
| `--auto` | let the built-in placer play |
| `--shot FILE --shot-frame N` | render headless and write frame N as a PPM |
| `--test-restart` | drive R after a top out and check the board comes back empty |

`--auto` is a test harness, not a feature. Headless is the source of
truth in this repo, and "line clears work" is not something eight
seconds of watching a piece fall can establish. It picks the best
landing for the current piece and hard-drops, so a run reaches a clear
in under a second and a top out shortly after.

`--shot` is how the board was checked. `read_latest_framebuffer` runs
the same GPU path as the window, so a PPM written headless is proof
that pixels landed where the rules say they should.

Restart reseeds with the same seed on purpose: the same seed replays
the same game exactly, which is the property `docs/RECORD_AND_REPLAY.md`
asks for.

## Two bugs the tests found and the screen did not

`tests/test_tetris_well.cpp` is pure C++17 — no engine, no window, no
GPU. Both of these looked fine in a screenshot.

**The I piece could not stand up.** A vertical I occupies four rows;
its bounding box reaches three cells above its floor, and at the spawn
row that cell is outside the well. `fits()` refused, so the rotation
key silently did nothing until gravity had moved the piece down one
row. Fixed by the two downward entries in `kKicks`. The test asserts
the kick works AND asserts the naive rotation is still blocked, so it
cannot quietly stop testing anything.

**`collapse()` removed empty rows instead of cleared rows.** An
all-empty row can sit under an overhang without having been cleared;
squeezing it out dropped the stack a row nobody earned. The first
run of the test caught it. Measured effect of both fixes together, over
a 20-second autoplay on seed 99: 11 lines before, 195 after.
