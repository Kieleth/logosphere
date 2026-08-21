> **Left exactly as written.** This log was kept live by the coding agent
> that built this game, in a clone where the directory was `examples/tetris/`
> and every identifier said `tetris`. Both were renamed to `logotriste` before the
> game was merged. Nothing else here has been corrected, including the
> places where it is unkind about our documentation, because those are the
> parts that were worth having.

# Newcomer log: building Tetris on Logosphere

**Start: Sat Aug 15 21:03:22 PDT 2026**

I know nothing about this engine. Everything below is what the repo
told me, in the order it told me.

---

## Phase 1 — orientation (21:03 – 21:20)

### Documents opened, in order

1. **`README.md`** (376 lines). Earned its time. Told me the shape of
   the thing in the first 15 lines: particles, knowledge graph,
   software rasterizer, macOS arm64 for the full profile. The
   "Quick start" block at line 218 is copy-pasteable and correct.
   The "Build outputs" table (line 263) told me where binaries land
   before I had to guess.

2. **Toolchain check** (not a doc). `cmake 4.0.3`, `glfw3 3.4.0`
   present, arm64. Started `cmake -S . -B build
   -DCMAKE_OSX_ARCHITECTURES="arm64"` + full build in the background
   immediately, so reading and building overlapped. **Configure exit
   0, build exit 0, first try, no edits.** Roughly 14 minutes
   wall-clock for a cold full build on 16 cores. This is the single
   best thing about this repo so far: the documented build command
   works verbatim on a clean clone.

3. **`docs/GETTING_STARTED.md`** (290 lines). Mostly earned it.
   Correct on: ontology YAML → generator → C++ registry, the
   `extendOntology` call, the CMake wiring, body plans, the frame
   hooks. **Wrong on the class you actually derive from** — see
   confusion #1 below.

4. **`include/application.h`** (168 lines). This is the real
   contract, and it is short and honest. Read it instead of trusting
   the tutorial snippet.

5. **`include/platform/platform_layer.h`** (55 lines). The missing
   link the tutorial never mentions. `PlatformLayer : IApplication`
   already implements `initialize()`, `shutdown()`, `get_window()`,
   window sizing and app name. A game derives from *this*, not from
   `IApplication`.

6. **`docs/GAME_LAYER.md`** (792 lines). Earned its time for the
   event bus, capability rules and interaction profiles. Nothing in
   it is about *rendering a scene*, which is what a newcomer building
   a visual game needs first. It repeats the same
   `display_framebuffer` error at line 21.

7. **`examples/eden/src/main.cpp`** (2165 lines, read the first 180).
   The tutorial calls this "a full working implementation". It is
   also a 2165-line file with spirit lights, boulder drift
   diagnostics and hover highlighting. Not a starting point.

8. **`examples/logotron/src/main.cpp`** (103 lines). **This is the
   document that unblocked me**, and no doc points at it. 103 lines
   containing the entire engine driver loop:
   `Engine engine(&app); engine.initialize(config); while
   (engine.should_continue()) { engine.update(dt); engine.render();
   engine.present(); }`. `docs/GETTING_STARTED.md` Step 8 says only
   "`./build/mygame/mygame`" and never shows that a game owns its own
   `main()` and its own loop.

9. **`examples/logotron/src/walls.h`** (101 lines). Best code comment
   density in the repo. Told me how to style a box particle
   (`shape`, `width/height/thickness`, `SetMaterial`, `owner`,
   `is_self_emissive`) in one screen, with the reasons — including
   which flag crashed the GPU command queue on 2026-04-27 and why the
   other one is safe.

10. **`examples/logotron/src/logotron_app.h`** (3180 lines, grepped,
    not read). Grepped for `add_particle`, `camera`,
    `create_initial_scene`. Found `apply_camera()` at line 889 and
    the note that the engine's default projection is a 2.5D
    isometric, which matters enormously for Tetris.

11. **`src/projection_system.h`** + **`src/core/projection_mode.h`**.
    Found `ProjectionMode::BirdsEye` — "Top-down orthographic". This
    is the whole reason Tetris is feasible here.
    `engine.set_projection_mode(ProjectionMode::BirdsEye)`
    (`src/core/engine.h:236`).

12. **`src/core/particle_system.h`** (395 lines, read 260).
    `add_particle_to_entity(p, &kg, entity)` is marked "Preferred
    API"; `delete_particles_immediate(vector<int>)` is the removal
    path. Both documented in-header, neither in GAME_LAYER.md.

13. **`src/particle_core.h`** (grepped). Particle field names and
    defaults.

### First moment of confusion

**`docs/GETTING_STARTED.md:118`**, inside the "minimum
implementation" of `IApplication`:

```cpp
    void display_framebuffer(uint8_t* buf, int w, int h) override;
```

I expected to find `display_framebuffer` in `include/application.h`.
It is not there. `grep -rn display_framebuffer` over the whole repo
returns five hits and **not one of them is a declaration**:

- `docs/GETTING_STARTED.md:118` — tells me to override it
- `docs/GAME_LAYER.md:21` — tells me to override it
- `examples/eden/src/main.cpp:36` — comment: "display_framebuffer
  removed in the Renderer/Display split"
- `src/platform/macos_platform.mm:32` — comment: "display_framebuffer
  removed in Phase 6 of the Renderer/Display [refactor]"
- `src/core/engine.cpp:87` — comment: "now handled through
  EngineRenderState"

So the method was deleted from the interface, three comments were
left behind marking the grave, and **both documents a newcomer is
explicitly told to read first still instruct you to `override` it.**
Writing that line gives you `error: only virtual member functions
can be marked 'override'`. Cost: about 4 minutes, all of it grep.

### Second confusion, same paragraph

`docs/GETTING_STARTED.md:111` says `class MyGame : public
Logosphere::IApplication`. If you do that literally you must
implement `initialize()` and `get_window()` yourself — GLFW window
creation, from scratch, on your first day.

The actual reference example does not do that.
`examples/eden/src/main.cpp:45` says
`class EdenApplication : public Logosphere::MacOSPlatform`, and
`MacOSPlatform` derives from `PlatformLayer`, which implements all
three platform methods for you. Neither `GETTING_STARTED.md` nor
`GAME_LAYER.md` mentions `PlatformLayer` even once.

Worse, `examples/eden/src/main.cpp:39-42` re-declares the class
inline:

```cpp
namespace Logosphere {
    class MacOSPlatform : public PlatformLayer {
    };
}
```

That is a definition, not a forward declaration, and it is sitting in
a game's `main.cpp`. So the "documented path" for getting a window is:
copy an empty class declaration out of an example file. I will do
exactly that, and record it as a guess, because nothing tells me
whether `MacOSPlatform` is supposed to be a real class somewhere.

### Things I had to guess

- That a game supplies its own `main()`. Implied by nothing in the
  tutorial; learned from `examples/logotron/src/main.cpp`.
- That `EngineConfig{create_display, window_width, window_height,
  window_title, ...}` is the initialization struct. Learned the same
  way.
- Whether to use `create_initial_scene()` (documented) or to just add
  particles inside `initialize_game()` (what Logotron does). Going
  with the latter because I need to add and remove particles every
  frame anyway, so the one-shot scene callback buys me nothing.

---

## Phase 2 — the ontology (21:20 – 21:26)

14. **`examples/eden/schema/eden.yaml`** and
    **`examples/eden/CMakeLists.txt`**. Copied the shape from both.
    Both earned their time; the CMakeLists is short enough to read in
    one breath and told me the include dirs and frameworks a game
    needs.

15. **`scripts/generate_ontology.py`** (skimmed, then grepped). Line
    329 confirms `examples/*/schema/**/*.yaml` is auto-discovered, so
    a new game's schema needs no registration anywhere. That is a
    genuinely good design and the tutorial says so at line 81.

### Wall 1 — the generator's error message (5 minutes)

Wrote `examples/tetris/schema/tetris.yaml` with a flow mapping:

```yaml
      S: {description: Two offset pairs, right-leaning.}
```

`python scripts/generate_ontology.py` produced 25 lines of linkml
internals ending in:

```
TypeError: PermissibleValue.__init__() got an unexpected keyword
argument 'right-leaning.'
```

The bug is mine: the comma inside the flow mapping splits the value.
The **finding is the generator's**: nowhere in those 25 frames does it
say *which schema file* it was reading. It had already printed
"Generating logotron_ontology.h…" for other schemas, so I could guess,
but a run that fails on the first schema would give a newcomer a
linkml stack trace and no filename. `generate_ontology.py` has the
path in hand — it just does not wrap the parse.

Fixed by switching to block style. Second run: clean, and it emitted
`tetris_ontology.h`, `tetris_ontology_registry.{h,cpp}` in
`examples/tetris/src/generated/`, exactly where the tutorial says.

**Side effect nobody warned me about:** `generate_ontology.py`
regenerates and rewrites *every* game's `*_ontology_registry.cpp`,
not just mine, so adding one schema dirtied logotron's, logovger's
and predator's generated files and forced them to rebuild. The
script has a `write_if_changed` helper (line 39) whose docstring says
it exists precisely to avoid this — but the registry `.cpp` outputs
print without "(unchanged)" while the `.h` outputs print with it, so
the guard is applied to headers only.

### Small surprise

The generated `tetris_ontology_registry.cpp` contains the **entire**
engine ontology (every `addEntityType` in `logosphere.yaml`), not just
my three types. Harmless, since `extendOntology` is additive and
last-write-wins per `GAME_LAYER.md:81`, but it is 1,000+ lines of
copied engine types per game and nothing says that is intended.

---

## Phase 3 — the code (21:26 – 21:34)

16. **`src/core/engine.h`** (grepped: `EngineConfig`, `set_projection_mode`,
    `get_resolution_manager`, `read_latest_framebuffer`). Earned its
    time. The comment at line 156 —

    > a windowed app must drive update() -> render() -> present();
    > forgetting present() shows an unpainted (white) window while the
    > framebuffer renders perfectly offscreen — a failure mode that
    > cost a full debugging session

    — is the kind of comment that saves a newcomer an afternoon.

17. **`src/particle_types.h`**. `ParticleOwner` and
    `ParticleSolverMode` are both documented in place, clearly, with
    the distinction between them spelled out (lines 72-76). This is
    the best-documented enum I found. It told me directly that
    `KINEMATIC` means "physics will not integrate this, position
    belongs to an external writer", which is exactly what a grid game
    needs.

18. **`src/projection_system.cpp:170`** (`BirdsEyeProjection::project`).
    Read it to learn which world axis is screen-up before placing a
    single block. `screen_y = h/2 - view_y * ppu`, so +Y is up. Saved
    me a guess.

19. **`src/core/particle_system.h`** and
    **`src/core/particle_system.cpp:1053`**. Read the implementation
    of `delete_particles_immediate` because I did not trust batch
    deletion with swap-and-pop. It queues then flushes once. Fine.

### What I wrote

- `examples/tetris/schema/tetris.yaml` — `TetrisWell`, `TetrisMino`,
  `TetrisPiece`, `TetrominoKind`.
- `examples/tetris/src/tetromino.h` — the seven pieces as tables.
- `examples/tetris/src/well.h` — board rules, no engine, no GLFW. The
  grid stores `kg::EntityID`, so "is this cell taken" and "which
  entity is standing there" are one lookup.
- `examples/tetris/src/tetris_app.h` — the `IApplication`.
- `examples/tetris/src/main.cpp` — the loop.
- `examples/tetris/CMakeLists.txt` + one line in the root
  `CMakeLists.txt` (after line 2059).

**It compiled and linked on the first attempt.** Zero errors of mine.
That is worth saying plainly, because everything above is criticism
and this part was not hard.

### First run, headless

```
[tetris] seed=7
[ENGINE] WARNING: No input entity specified by game!
[PARTICLE_SWAP WARNING] Light source particle at index 7 being
  swapped to index 3 (emission=900000)
[tetris] final: score=0 lines=0 level=1 over=no
```

Ran. KG accepted every entity and property — no validation
rejections, so the generated registry works as advertised.

---

## Phase 4 — the walls

### Wall 2 — the engine talks over the game (unsolved, ~0 effort, high annoyance)

`[TIMING] Frame N | Dynamics=… BVH=… Physics=…` prints every 30
frames on stdout with `EngineConfig::show_performance_metrics = false`
and `show_debug_overlay = false`. A headless 8-second run produced
**434,000 lines of engine chatter**, which is why my first attempt to
read the game's own output showed nothing: `tail -40` was all
`[TIMING]`.

Also unconditional: `[SURFACE RASTERIZER]` ×6, the optimization-flags
banner, `[ResolutionManager]` ×4, `[PHYSICS V4.1]` banner,
`[EntityManager] Registered entity type: …` ×15+, `[BOTTLENECK]` every
60 frames, `[PASS3_HANDLER]`, `[UISYSTEM_RENDER_DEBUG]`,
`[GPU_TIMESTAMP]` ×8 per frame.

There is no flag for it. `EngineConfig` has six `debug_*` booleans
(engine.h:130-135) and every one of them is about lighting. Worked
around with `grep -v '^\[TIMING\]'`. **Recorded, not fixed** — I did
not touch engine sources for it.

### Wall 3 — the on-screen window (unsolved, ~10 minutes, environmental)

The windowed binary runs: GLFW window created, `visible=1`, Metal
layer at 1000x1051 with contentsScale 2, 780+ render/present pairs in
6 seconds, clean exit 0. But `screencapture` of the display shows the
desktop, not the game. `osascript` lists no tetris process among
visible ones. Most likely my shell session is not a GUI login session,
so the layer never composites.

**I therefore cannot claim I saw the game in a window.** What I can
claim is stronger than a promise and weaker than an eyeball: the
offscreen GPU read-back below goes through the same render pipeline
and the pixels are correct.

### Wall 4 — the light I should not have added (10 minutes, fixed)

`[PARTICLE_SWAP WARNING] Light source particle at index 7 being
swapped to index 3`. My overhead light is a particle, and deleting the
falling piece's four particles swap-and-popped it.
`examples/logotron/src/walls.h:30-36` says that exact churn crashed
the GPU command queue on 2026-04-27. Fixed by deleting the light and
making everything `is_self_emissive`.

**This is a doc gap, not a bug.** `docs/GAME_LAYER.md` never mentions
`is_light_source` vs `is_self_emissive`, or that lights are ordinary
particles subject to swap-and-pop. The only place that knowledge
exists is a comment in a game example. A newcomer who does not happen
to read `logotron/src/walls.h` will add a light, see a warning they do
not understand, and ship it.

### Wall 5 — two bugs of my own that the screen could not show (25 minutes, fixed)

`docs/testing_guidelines.md` and `CLAUDE.md` both insist on headless
proof and on instrumenting before concluding. I nearly did not, because
the screenshot looked right. Then I noticed column 0 was never used in
14 placements and wrote a 30-line standalone probe over `Well::fits`
instead of reasoning about it. Column 0 turned out to be fine. The
probe found something else:

```
I r1: fits_at_spawn=NEVER
I r3: fits_at_spawn=NEVER
```

A vertical I occupies four rows; its box reaches three cells above its
floor, which at the spawn row is outside the well. The rotate key
silently did nothing on I pieces until gravity moved them down one
row. Fixed with two downward kick offsets.

Then I turned the probe into `examples/tetris/tests/test_tetris_well.cpp`
with the controls the guidelines demand, and the **first run failed
four assertions**:

- **One real bug.** `Well::collapse` removed every *empty* row rather
  than every *cleared* row. An all-empty row can sit under an overhang
  without having been cleared, and squeezing it out drops the stack a
  row nobody earned. Measured: marker placed at row 2 landed at row 0
  instead of row 1.
- **Three wrong expectations of mine**, all the same mistake: `py`
  addresses the piece's bounding box, and several pieces have an empty
  bottom row, so `py = -1` still puts every occupied cell at row ≥ 0.
  Per the guidelines I did not loosen the assertions; I rewrote them
  to derive the boundary from the shape and said in the file which of
  the two was wrong.

**Measured effect of both fixes**, 20-second headless autoplay,
seed 99: **11 lines cleared before, 195 after.** Seeds 1 and 7 reach
1,600 and 801 lines. That number is the whole argument for headless
testing in one line.

### Wall 6 — the DPI numbers do not agree (not fixed, cosmetic for me)

One windowed run, three different answers:

```
[ResolutionManager] Framebuffer size set to 2000x2102 (DPI scale: 1.75167)
[ENGINE] DPI scale: 2 (fb 2000×2102 → win 1000×1051)
UISystem: Initialized with screen 1000x1200 (DPI scale: 2)
```

I asked for a 1000×1200 window and the platform gave me 1000×1051
(clamped to the display), which is correct behaviour. But `UISystem`
was initialised with **1000×1200 — the size that no longer exists** —
while `ResolutionManager` knows it is 1051. My HUD is anchored top-left
so it does not care. Logotron's dashboard is anchored to
`get_render_height() - dash_h - margin`, which is the *right* value, so
Logotron is fine too. Anything anchored to the UI system's own screen
height would be 149 px off the bottom of the window and nobody would
know why.

---

## Things I expected the engine to provide and it did not

- **A minimal example.** The smallest game in the tree before this one
  was Logotron at 3,180 lines in one header. Eden, which the tutorial
  points at as "a full working implementation", is 2,165 lines. There
  is no 150-line "here is a window with a cube in it" to copy. This is
  the single biggest cost in the run: everything I needed existed, and
  I found it by grepping two large game files.
- **A way to silence the engine's own logging.** See wall 2.
- **Any mention of how to draw a 2D game.** `BirdsEye` is the
  difference between Tetris being an afternoon and Tetris being
  impossible, and it appears in exactly one place outside the source:
  nowhere. Not in `README.md`, not in `GETTING_STARTED.md`, not in
  `GAME_LAYER.md`. I found it by grepping `src/core/` for "projection"
  after noticing a comment in Logotron saying the camera was
  isometric.
- **`create_initial_scene` being worth using.** It is the only
  scene-creation hook the tutorial documents, it takes a single
  particle-add callback, and the engine warns
  `[ENGINE] WARNING: No input entity specified by game!` when you
  return 0 from it. Both reference games ignore it and add particles
  in `initialize_game` instead. The README already flags this as
  TODO[ARCH-003]; the tutorial does not.

## Things I had to guess

- Deriving from `Logosphere::MacOSPlatform` rather than
  `IApplication`, and that the way to name `MacOSPlatform` is to
  re-declare an empty class in your own `main.cpp` because it has no
  header. Copied from `examples/eden/src/main.cpp:39-42` without
  understanding why it works.
- That `owner = STATIC` plus `solver_mode = KINEMATIC` is the way to
  say "the game owns this position, leave it alone". `particle_types.h`
  documents each field, but nothing says which combination a static
  game object should use. Logotron's walls set `owner = STATIC` and
  `is_at_rest = true` and leave `solver_mode` at its `DYNAMIC` default,
  which I chose not to copy.
- The `pixels_per_unit` to use. Logotron's comment ("24 px/unit fits
  the 50 m arena in ~1200 px") let me back out that it means literally
  pixels per world unit, so I compute it from
  `get_render_height() / (rows + 3)`. Nothing documents the units.
- Whether `read_latest_framebuffer` returns BGRA or RGBA. The comment
  says BGRA; I trusted it and the colours came out right, so it is
  correct — but that is one comment in `engine.h:359`, not a doc.
- Whether the engine renders at all with `create_display = false`. It
  does; only `present()` needs a window. `EngineConfig` line 124-126
  hints at it ("fully offscreen rendering… pair with
  read_latest_framebuffer"), which is enough, barely.

## Documents that were wrong, with lines

| File:line | Says | Reality |
|---|---|---|
| `docs/GETTING_STARTED.md:118` | `void display_framebuffer(...) override;` | Removed from `IApplication`. `override` will not compile. |
| `docs/GAME_LAYER.md:21` | same | same |
| `docs/GETTING_STARTED.md:111` | `class MyGame : public Logosphere::IApplication` | Both example games derive from `MacOSPlatform`. Deriving from `IApplication` means writing GLFW window creation yourself. `PlatformLayer` is never mentioned in either doc. |
| `docs/VISUAL_TESTS.md:88` | `engine.get_platform()->poll_events(); // REQUIRED` | `Engine::update()` already polls (`engine.cpp:1127`). The doc's own skeleton calls `update()` and then polls again. Neither example game polls. |
| `docs/GETTING_STARTED.md` Step 8 | `./build/mygame/mygame` | Omits that a game owns its own `main()` and drives `update`/`render`/`present`. That is the single most load-bearing fact for getting a game running and it is only in `examples/logotron/src/main.cpp`. |

Also worth noting: `README.md:228` presents
`./build/logosphere-tests --no-head` as "the combined test harness".
It runs 4 modules and 27 tests in 0.5 s, against 300+ files under
`tests/`. It passes, and it did not catch anything, because almost
nothing is in it. The real suite is the standalone `build/test_*`
binaries, which the README's build-outputs table does mention.

## Documents that were right and worth the time

- **`docs/VISUAL_TESTS.md`** — the best document in the repository,
  by a distance. Every trap in it is a real one with the cost written
  next to it. Line 20 ("`draw_text` between `render()` and `present()`
  is silently erased… four visual tests shipped with readouts nobody
  could see") saved me from writing exactly that HUD. The widget route
  it prescribes worked first try. If a newcomer reads one document
  after the README, it should be this one, and nothing links to it
  from `GETTING_STARTED.md`.
- **`src/particle_types.h`** and **`examples/logotron/src/walls.h`** —
  both explain *why*, with dates and incident references.
- **`src/core/engine.h:156`** — the present() warning.

## Things I touched outside my own game

Exactly two, both additive, neither a workaround:

1. `CMakeLists.txt` — one `add_subdirectory(examples/tetris)` line.
2. `CHANGELOG.md` — one entry under `[Unreleased]`, per `CLAUDE.md`.

**I did not modify a single line of engine source.** Everything the
game needed, the engine already had.

## Final state

**Builds.** `cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"`
then `cmake --build build -j16`: exit 0, whole repo, no errors, no new
warnings from my files. `./build/logosphere-tests --no-head`: 27/27
pass.

**Runs.** Windowed: window created, 780+ render/present pairs in 6 s,
clean exit. Headless: yes, with `--exit-after`.

**Plays.** Seven pieces with correct shapes and colours, four
rotations with kicks, gravity on a level-scaled timer, soft drop, hard
drop, wall and stack collision, line detection, multi-line clears,
guideline scoring with a level multiplier, speed curve, top-out
detection, restart on R, live HUD. Verified three ways:

- `./build/test_tetris_well` — 26 assertions with printed measurements
  and controls, pure C++17, exit 0.
- `--auto` headless soak — seeds 1/7/99 reach 1,600 / 801 / 195 lines
  and top out. Line clears, level advancement and game over all fire.
- `--shot` — headless GPU read-back written as a PPM and looked at.
  The board, the stack, the falling piece and the HUD are all where
  the rules say they should be.

**Not a leak, measured:** after 436 cleared lines (≈4,400 mino
entities created and destroyed) the graph held 122 entities and
331 KB. `destroyEntity` reclaims.

**Missing:** no next-piece preview, no hold, no ghost piece, no
lock delay, no DAS/ARR tuning, no sound, no pause, no persistent high
score. Rotation uses a 7-offset kick list, not the full SRS wall-kick
tables, so a few tight T-spin placements a competitive player expects
will be refused. Restart replays the same seed on purpose. And I never
saw the window with my own eyes — see wall 3.

---

## The one change that would have helped most

**Ship a minimal example game.** `examples/minimal/` — one file, under
200 lines, a window, a camera, a handful of box particles, a key
handler and the `update`/`render`/`present` loop, with
`GETTING_STARTED.md` pointing at it in Step 3 instead of at Eden's
2,165 lines.

Everything I needed already existed and worked. The cost was not
missing capability; it was that the smallest working demonstration of
"how does a game start" is a 3,180-line header, so the answer had to
be reassembled by grep from two large games. The four doc errors in
the table above matter less than that, because a correct 200-line
example would have made all four self-evidently stale the day they
broke.

---
**End: Sat Aug 15 21:28:16 PDT 2026**
