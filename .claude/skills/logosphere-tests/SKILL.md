---
name: logosphere-tests
description: Use when writing, converting or reviewing ANY logosphere test. Every test runs headless (asserting, capturable) and windowed (owner QA) from one shared scene with no duplication. Carries the seven acceptance criteria and the verified engine facts for light, camera, ESC, SPACE, FPS and on-screen readout.
---

# Writing a logosphere test

Full pattern reference with citations:
`docs/todo_plans/VISUAL_TEST_PATTERNS.md`.

## Why this exists

The owner does final QA by watching. On 2026-08-16 he was handed a
window that was **black**, showed its subject **6600 px off screen**,
could **not be closed**, and whose headless half **measured different
physics from the windowed half**. All four in one file, written the same
hour by someone who had read the visual-test documentation.

## The seven criteria (owner, verbatim, binding)

> a) every test needs to be headless and head
> b) the headless needs to be programatically apt to capture and they are
>    reflections one of the other and they share code for the tested
>    parts, no duplication
> c) interactie means there's proper light, zoom, camera position and
>    angle to see the test
> d) ESC stops always the test
> e) we use SPACE to move towards the test
> f) always FPS need to be present in the test
> g) clear assertions in each of the tests and proper logging is
>    presented in the ui for the test Always

## THE FILE LAYOUT (criterion b)

Three files. The scene is written ONCE.

```
tests/scenes/scene_<name>.h    the bodies, the stepping, the measurements.
                               Includes particle_system.h + physics_system.h
                               and NOTHING from the engine or rendering.
tests/test_<name>.cpp          headless driver: asserts, prints numbers.
tests/test_<name>_visual.cpp   window + capture driver. Full profile.
```

The scene header owns `build()`, `step()` and `measure()`. Neither
driver may contain a body, a force or a threshold. **If a driver creates
a particle, the factoring is wrong.**

**THE TIMESTEP TRAP, which is what broke the first attempt.**
`PHYSICS_TIMESTEP = 1/30` (`time_system.h:74`) and `engine.update(dt)`
ACCUMULATES (`time_system.cpp:113-121`). So `engine.update(1.0/60.0)`
runs a physics step only every OTHER call, at dt = 1/30, while
`physics.update(1.0/60.0)` called directly runs one step of 1/60 every
time. Any per-substep quantity then differs between the modes and they
stop being reflections. **The scene header owns the stepping and both
drivers call it**, or the windowed run is a different experiment.

## THE ENGINE FACTS (verified from code; get one wrong and the window is useless)

| Fact | Consequence |
|---|---|
| `engine.is_running()` returns `is_running_` only (`engine.h:294`) | **It never reads `should_close()`, so `while (engine.is_running())` cannot be exited by ESC or by the red X.** This is the "could not close it" bug. |
| `engine.should_continue()` = `is_running_ && !should_close()` (`engine.cpp:649-651`) | **The only correct loop condition.** |
| ESC is bound by the engine at init (`key_mapper.cpp:266`) | No test binds ESC; it already sets `should_close`. Read the right predicate and it works. |
| `engine.update()` already polls events (`engine.cpp:1127`) | An extra `poll_events()` is redundant. `VISUAL_TESTS.md` calling it REQUIRED is stale. |
| `present()` clears the overlay plane, then re-renders **registered widgets only** (`engine.cpp:1857, 1659-1673`) | Immediate-mode `ui->draw_text()` from a test is **erased before it reaches the screen**. |
| `add_widget` does NOT take ownership (`ui_system.cpp:2196-2203`) | It stores a raw pointer. Freeing a registered widget dangles. |
| `cfg.show_debug_overlay = true` draws FPS (`ui_system.cpp:1254-1261`) | **FPS is one line of config.** Backtick toggles it live. |
| light: `lux = strength / (4π·d²)`, **hard zero past `emission_radius`** | 100 lux → RGB 200. |
| iso projection ignores `look_at` (`projection_system.cpp:7-39`) | `cam.set_position(x,y,z)` puts that point at **exact screen centre**. That is the framing control. |
| `update_follow_target` never writes camera z (`camera_system.cpp:66-110`) | Built for Eden's ground camera; frames nothing when the subject is not near z=0. |
| viewport = **render** buffer size (`engine.cpp:306`) | Retina is ~2× the configured window width. `pixels_per_unit` is in render pixels. |
| SPACE is bound but has no handler (`main_key_handler.cpp:141-145`) | Free for tests. It spawns nothing. |
| `--no-head` is a no-op for `logosphere-tests` (`test_main.cpp:29-84`) | The flag in every usage comment is cargo. |

## THE CANONICAL VISUAL DRIVER

```cpp
// (c) LIGHT: strength ~= 4000 * d^2, radius >= 1.25 * d, and the light
//     MOVES WITH THE SUBJECT. Gravity is a hardcoded -Z acceleration, so
//     an airborne body falls out of a fixed light's emission_radius and
//     then receives exactly zero.
auto light_the_subject = [&](float x, float y, float z) {
    const float d = 3.0f;                      // stand-off
    ps.queue_light(x + 1.5f, y - 2.0f, z + 2.0f,
                   4000.0f * d * d, 1.25f * d, 1.0f, 0.95f, 0.85f);
};

// (c) CAMERA: set_position centres the subject exactly. Re-set it every
//     frame for a moving subject; do NOT use update_follow_target.
cam.set_position(sx, sy, sz);
cam.set_pixels_per_unit(subject_px / subject_metres);

// (f) FPS: one line, at config time.
cfg.show_debug_overlay = interactive;

// (g) READOUT: registered widgets, never draw_text. Keep them alive for
//     the life of the engine.
auto* line = new ui::Label("", "l0");
line->set_position(16, 16);
line->set_color(255, 240, 140);
engine.get_ui_system()->add_widget(line);

// (d) LOOP: should_continue(), never is_running().
// (e) SPACE: multi-case tests advance the case on SPACE (zoom on Z);
//     single-scene tests dolly the camera in.
while (engine.should_continue()) {
    if (space_edge()) stand_off *= 0.8f;
    scene.step();                    // SHARED with the headless driver
    cam.set_position(subject);       // re-centre
    relight(subject);                // re-light
    line->set_text(values_and_the_verdict);
    engine.render();
    engine.present();
}
```

## (g) ASSERT IN BOTH MODES — and assert-or-waive every DOF

Full-state narration first (physics skill, 2026-08-19 directive): name
what every degree of freedom of every tracked body should do, then
assert each or waive it by name. Observe through **Argus**
(`src/core/argus.h`): the asserts and the on-screen log read the same
queries, so they cannot drift apart. `tests/test_cube_drop_ladder.cpp`
is the pattern.

The windowed run reaches the same verdict and prints it **on screen**,
not only to stdout. A viewer that shows values without saying what pass
looks like asks the owner to judge a number he has no threshold for. The
first attempt asserted nothing in the window at all.

## EXPERIMENTS ARE LECTURES (owner standard, 2026-08-20)

Owner: interactive cases must be "worth of the coolest physics classes
in history" — Lewin hanging off his own pendulum, Feynman's O-ring in
ice water. A case that technically exercises the mechanism but shows a
centimetre of motion for half a second is an assert with a window, not
an experiment. The standard, mechanical:

1. **A staged QUESTION with an arc**: setup, the event, the outcome —
   and it replays on demand (SPACE), never on a timer.
2. **CONTRAST on stage**: the twin that does not spin beside the one
   that does; the ice lane beside the rock lane. The difference IS the
   lesson; a lone body proves nothing to an eye.
3. **Motion visible from the back row**: the travel spans a large part
   of the stage, the subject fills a good part of the frame, reference
   scenery (pillars, lanes) makes motion legible. If the honest physics
   is small, make the BODY bigger or the runway longer — never fake the
   physics to fake the drama.
4. **The number on screen at the moment it matters**, from Argus, the
   same value the assert reads.

The failure that earned this: the G-41 wheel cases dropped a 0.4 m cube
five centimetres and bought five centimetres of travel — an experiment
smaller than its own subject, jerky and over before the eye arrived.

## SHIP CHECKLIST FOR A TEST

- [ ] Scene in `tests/scenes/`; drivers contain no bodies and no thresholds
- [ ] Both drivers step through the SAME scene function (timestep trap)
- [ ] Headless captures a deterministic frame when it claims anything visual
- [ ] `should_continue()`, never `is_running()`
- [ ] SPACE: in a MULTI-CASE test it advances the case, manually, never
      on a timer (owner order 2026-08-20: the human decides when a case
      is seen); zoom then lives on Z. Single-scene tests keep SPACE as
      move-toward. On-screen hint says which.
- [ ] `show_debug_overlay = interactive`
- [ ] Full-state narration written; every DOF asserted or waived by name
- [ ] Observed through Argus; asserts read the same queries the log prints
- [ ] Readout through registered widgets, carrying the verdict
- [ ] Launched for the owner, teed to /tmp, never killed, never piped through head/tail/grep

## THE BACKLOG THIS SKILL EXISTS TO CLEAR

Measured 2026-08-16 across 62 windowed tests:
- **61 have no FPS.** Only `test_multi_light_progressive`.
- **35 draw their readout with `draw_text`, which has never been visible.**
- **17 have no ESC handling at all**, including
  `test_rube_goldberg_machine`, the owner's declared acceptance vehicle.

Best in tree to copy: `test_rotation_ladder` (step/ESC/SPACE),
`test_single_blade_contact` (Label factory), `test_knockback_scene`
(panel, focus, brightness assert), `test_rube_goldberg_machine` (light
rig, direct camera centring, watchdogs), `test_shadow_edge_quantization`
(capture + PPM).
