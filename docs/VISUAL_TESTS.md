# Visual tests: proving a rendering change with your eyes and with CI

_Every trap in this document was hit for real. The ones marked COST are ones
that produced a confident wrong answer or burned a working session before being
noticed._

A visual test does two jobs from one file:

- **headless**: dumps frames, measures differences, asserts, runs in CI
- **`INTERACTIVE=1`**: same scene in a window, so a human can judge what a
  number cannot

Write both. The headless half stops regressions; the interactive half is how
you find out that the thing you measured was not the thing you cared about.

---

## The traps

### COST: `draw_text` between `render()` and `present()` is silently erased

`Engine::present()` calls `draw_ui_overlays()`, and the first thing that does is
**clear the overlay plane**, then re-render only the debug overlay and
registered widgets. Immediate-mode text drawn from a test never reaches the
screen. `draw_ui_overlays()` is private, so a test cannot draw into that window.

This affects existing tests. Any HUD written that way has never been visible.

**Use the window title instead.** It is outside the overlay system entirely:

```cpp
if (GLFWwindow* win = (GLFWwindow*)engine.get_window_handle()) {
    char title[200];
    snprintf(title, sizeof(title), "LOD %d | %d bodies | %.1f ms", lod, n, ms);
    glfwSetWindowTitle(win, title);
}
```

Cost: three runs where the user pressed keys, saw nothing, and correctly
reported the test as broken.

### COST: without `poll_events()`, input is stale garbage AND fires phantom keys

```cpp
engine.update(dt);
engine.render();
engine.get_platform()->poll_events();   // REQUIRED in interactive mode
```

Omit it and the input state is never refreshed. Real presses do nothing, and
the uninitialised key array triggers spurious SPACE and ESC. A log showed 18
state changes the user never made, and that log was then read as evidence.

If you are reading input, poll. If a log shows input you did not perform, do
not interpret the log. Fix the polling.

### COST: "pixels differ" is not a signal. Take an A-vs-A control first

Rendering the **identical** configuration twice moves ~30,000 pixels in this
engine. Equal-depth geometry (a floor of identical tiles) has a documented
depth-tie nondeterminism showing up as ±1 LSB scatter.

**Always capture the same config twice and measure that first.** Anything at or
below that floor is noise.

```cpp
const Shot A  = capture(config);
const Shot A2 = capture(config);   // noise floor: do this BEFORE any comparison
const Shot B  = capture(changed);
```

Cost: reported "the flat BVH IS read, 1.577% of pixels differ", which was the
noise floor, max delta 1. Confident, documented, wrong.

### Judge by magnitude, not by count

A lost shadow moves pixels by tens or hundreds. Noise moves them by 1 or 2.
Bucket the differences and judge on the bucket noise cannot reach:

```cpp
struct Diff { long any = 0, over8 = 0, over32 = 0; int max_delta = 0; };
```

`max_delta` alone tells you most of what you need. If it is 1, you are looking
at noise no matter how many pixels changed.

### Include a sensitivity control, or the test may be blind

A test that suppresses feature X and sees no change has two possible meanings:
X does nothing, or **the test cannot see X at all**. Distinguish them by
including a configuration that must obviously differ (lights off, feature
fully removed) and asserting that it does.

```cpp
if (unlit_vs_lit.over8 <= noise.over8) {
    printf("FAIL: the test is blind. Do not read the other rows as evidence.\n");
    return false;
}
```

Cost: five separate attempts to observe a difference all returned zero,
which looked like "harmless". The real reason was that the code under test was
never read by the path being rendered. A sensitivity control says so
immediately instead of after five runs.

### Choose a scene that can express the effect

- A **dense self-occluding pile** cannot show a missing shadow; everything is
  already dark. Use sparse, well-separated occluders on a pale open floor.
- A scene with **one rebuild in 900 frames** cannot show a rebuild-rate change.
  Check that your scene actually exercises the mechanism before drawing
  conclusions from it.

State what the scene is for in the file header, so the next reader can tell
whether it still fits.

### Do not let a startup constant override the environment

```cpp
int cur = 0;
set_level(kLevels[cur]);      // silently overrides LOGOSPHERE_* from the env
```

Two headless runs then measure the same configuration and write the same file.
Initialise from the current value instead.

---

## Skeleton

```cpp
// One scene, two modes. Headless asserts; INTERACTIVE=1 lets a human judge.
bool test_my_feature() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    EngineConfig cfg;
    cfg.create_display = interactive;
    Engine engine;
    if (engine.initialize(cfg) != 0) return false;

    build_scene(engine);   // deterministic: KINEMATIC, no spawning, fixed lights

    if (interactive) {
        int level = 0;
        bool space_was = false;
        while (engine.is_running()) {
            engine.update(1.0 / 60.0);
            engine.render();
            engine.get_platform()->poll_events();          // trap 2
            set_window_title(engine, level);               // trap 1
            engine.present();

            const auto& in = engine.get_input_system();
            const bool down = in.get_input_state().keys[GLFW_KEY_SPACE];
            if (down && !space_was) { level = next(level); apply(level); }
            space_was = down;
            if (in.get_input_state().keys[GLFW_KEY_ESCAPE]) break;
        }
        engine.shutdown();
        return true;
    }

    const Shot ref   = capture(engine, BASELINE);
    const Shot ref2  = capture(engine, BASELINE);   // trap 3: noise floor FIRST
    const Shot cand  = capture(engine, CHANGED);
    const Shot blind = capture(engine, OBVIOUSLY_DIFFERENT);   // sensitivity

    const Diff noise = compare(ref, ref2);
    const Diff got   = compare(ref, cand);
    const Diff sens  = compare(ref, blind);

    if (sens.over8 <= noise.over8) { printf("FAIL: test is blind\n"); return false; }
    // ... assert on got.over8, never on got.any
}
```

`capture()` should settle for ~50 frames (temporal shadow and SSDO accumulation
need to converge) and call `engine.get_renderer().wait_for_completion()` so the
frame you read is the frame you rendered.

---

## RED/GREEN still applies

Write the assertion first and watch it **fail for the reason you expect**. A
visual test that passes the first time has usually proved nothing. It is
measuring something other than the change, and you will not find out until it
fails to catch a regression.

When a test discovers a defect that is real but out of scope, report it loudly
and do not fail the suite, then leave the exact line to flip:

```cpp
// KNOWN DEFECT, pre-existing. Reported, not failed: no supported target hits it.
// FLIP THIS TO ok = false THE MOMENT IT IS FIXED. That is how a porter knows
// they are finished.
```

See `tests/test_shadow_accel_backend.cpp` for a worked example carrying a noise
floor, a sensitivity control and a known-defect report, and
`tests/test_sphere_lod_quality.cpp` for the quality/cost sweep shape.

---

## Registering the test

Standalone tests own their own `Engine`. Add the file to `CMakeLists.txt`, then
register it in `src/unified_test_runner.cpp` (extern declaration, registry
entry, and the name lists). See CONTRIBUTING.md, "Adding a test", for which
profile it belongs in.
