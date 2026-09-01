# Visual test patterns — mined from what already works

Source for the future test skill. Everything below is either **VERIFIED**
(read out of this tree, with file:line) or **RECOMMENDED** (a proposal
derived from verified mechanism). Nothing here is designed from first
principles; the working patterns already exist in the repo and are cited.

Requirements this answers, verbatim from the owner:

> a) every test needs to be headless and head
> b) the headless needs to be programatically apt to capture and they are
>    reflections one of the other and they share code for the tested parts,
>    no duplication
> c) interactive means there's proper light, zoom, camera position and angle
>    to see the test
> d) ESC stops always the test
> e) we use SPACE to move towards the test
> f) always FPS need to be present in the test
> g) clear assertions in each of the tests and proper logging is presented
>    in the ui for the test, always

---

## 0. The engine facts every pattern rests on

These were read out of the engine, not inferred. Get one of them wrong and
the window is black, frozen, or empty.

| Fact | Where | Consequence |
|---|---|---|
| `engine.update()` already calls `platform_->poll_events()` | `src/core/engine.cpp:1127` | An extra `poll_events()` in the test loop is harmless but redundant. `docs/VISUAL_TESTS.md`'s "REQUIRED in interactive mode" is stale — the engine polls for you. |
| ESC is bound by the engine at init | `src/key_mapper.cpp:266` → `src/main_key_handler.cpp:33-37`, wired at `src/core/engine.cpp:391` | ESC calls `platform_->set_should_close(true)`. Nothing else. No test needs to bind ESC. |
| ESC is a "system key" that bypasses UI focus | `src/core/input_system.cpp:117-121, 146-152` | ESC works even when a chat/UI widget has exclusive input. |
| `engine.is_running()` returns `is_running_` **only** | `src/core/engine.h:294` | It does **not** read `should_close()`. `while (engine.is_running())` never exits on ESC or on the window's red X. **This is the "could not close it" bug.** |
| `engine.should_continue()` returns `is_running_ && !platform_->should_close()` | `src/core/engine.cpp:649-651` | This is the loop condition that actually works. |
| `engine.stop()` sets `is_running_ = false` | `src/core/engine.h:164` | The other way to make `is_running()` loops exit: call it yourself when you see ESC. |
| windowed: `draw_ui_overlays()` runs inside `present()` | `src/core/engine.cpp:1857` | and its first act is to **clear the overlay plane** (`engine.cpp:1659-1668`), then re-render the debug overlay and every **registered widget** (`engine.cpp:1670-1673`). Immediate-mode `ui->draw_text()` called by a test between `render()` and `present()` is erased before anything reaches the screen. |
| headless: `draw_ui_overlays()` + `composite_ui_overlay()` run inside `render()` | `src/core/engine.cpp:1608-1615` | so a headless framebuffer read-back does contain the HUD. |
| `read_latest_framebuffer` composites `ui_buffer_` itself | `src/core/engine.cpp:705-755` | Fixed 2026-08-01. Before that the read-back was scene-only by construction and every HUD in a captured frame was invisible. Guarded by `tests/test_ui_label_actually_renders.cpp`. |
| `metrics_.current_fps` is real and free | computed `src/core/engine.cpp:1766-1781`, published from `update()` at `engine.cpp:1085` | `engine.get_metrics().current_fps` works with no telemetry env var. It lags two frames and it measures **loop rate including your pacing sleep**, which is what you want on a paced viewer. |
| `cfg.show_debug_overlay = true` draws FPS on screen | wired `src/core/engine.cpp:503`, collected `engine.cpp:1541-1548`, drawn `src/ui/ui_system.cpp:1254-1261` as `FPS: N (min:x max:y)` | Zero test code. Backtick (`` ` ``) toggles it live (`src/key_mapper.cpp:267`). |
| light falloff is `strength / (4π · d²)` with a **hard cutoff** at `emission_radius` | GPU: `src/rendering/gpu/shadow_rays_deferred.metal:1112, 1121, 1126`; CPU: `src/simd_multi_light.h:154, 167` | Beyond `emission_radius` a light contributes exactly zero, and the last 20 % of the radius is linearly faded. |
| lux → RGB is a zone curve: 10 lux → RGB 75, 100 lux → RGB 200 | `src/rendering/gpu/apply_lighting_deferred.metal:132-153` | This is what turns "inverse square is not a suggestion" into arithmetic. |
| the isometric projection ignores `look_at`; camera **position** is the only framing control | `src/projection_system.cpp:7-39` | `screen = viewport/2 ± iso · ppu`, `iso_x = (vx-vy)·0.866`, `iso_y = (vx+vy)·0.5 + vz`, `v = world - camera`. Camera **at** the subject ⇒ subject at exact screen centre. |
| but `look_at` still drives **backface culling** in parallel projections | `src/core/camera_system.cpp:517-555` | `forward_` alone decides which faces are culled. Its default (from the projection's default camera at `(-10,-10,20)` looking at origin, `src/projection_system.h:158-162`) has the sign pattern `(+,+,-)`, which matches the iso view axis. A `look_at` that flattens one component puts those faces on the cull boundary. |
| `update_follow_target` **never writes camera z** and applies a diagonal offset | `src/core/camera_system.cpp:66-110` | Designed for Eden's camera height. In a test whose subject is not near z=0 it frames nothing. |
| camera viewport = **render** buffer size, not window size | `src/core/engine.cpp:306` | On retina, `get_render_buffer().width()` is ~2× the configured `window_width`. `pixels_per_unit` is in render pixels. |
| `set_pixels_per_unit` is unclamped; `adjust_zoom` clamps to [5, 200] | `src/core/camera_system.cpp:312-327` | A test at ppu 220 that then calls `adjust_zoom` snaps to 200. |
| `PHYSICS_TIMESTEP = 1/30`, and `engine.update(dt)` accumulates | `src/core/time_system.h:74`, `time_system.cpp:98-121`, called from `engine.cpp:1197-1203` | **`engine.update(1.0/60.0)` runs a physics step only every OTHER call.** `PhysicsSystem::update(1.0/60.0)` called directly runs one step of 1/60 every time. The two drivers are *not* the same simulation. See §B. |
| `UISystem::add_widget` does **not** take ownership | `src/ui/ui_system.cpp:2196-2203` | It stores a raw pointer in `root_widgets_`. `docs/VISUAL_TESTS.md` line 63 ("owned by the UI system once added") is wrong. `create_panel`/`create_list_menu` *do* own (`ui_system.cpp:2253-2262`). A widget freed while still registered dangles. |
| SPACE maps to `CREATE_PARTICLE_AT_MOUSE` but **no handler is registered** | bound `src/key_mapper.cpp:311`; deliberately unregistered, `src/main_key_handler.cpp:141-145` | SPACE is free for tests to use. It spawns nothing. |
| `--no-head` is a **no-op** for `logosphere-tests` | `tests/test_main.cpp:29-84` parses no such flag | The combined runner's harness engine is always `create_display=false` (`tests/test_harness.cpp:29`); standalone tests decide for themselves. The flag in every usage comment is cargo. |

---

## a) Headless AND head from one file

**VERIFIED, canonical:** one `bool` off an env var, threaded into
`EngineConfig::create_display`.

```cpp
// tests/test_walk_through_grass.cpp:63-70
const bool interactive = std::getenv("INTERACTIVE") != nullptr;
printf("  mode: %s\n", interactive ? "INTERACTIVE (ESC quits)" : "HEADLESS");

EngineConfig cfg;
cfg.create_display    = interactive;
cfg.enable_chat_window = false;
cfg.show_debug_overlay = false;      // see (f): this should be `interactive`
Engine engine;
if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }
```

Two env-var conventions exist and they are not interchangeable:

- `INTERACTIVE=1` — 59 tests. The house convention. Use this.
- `LOGOSPHERE_VISUAL=1` — 5 tests (`test_knockback_scene`,
  `test_predator_hunt`, `test_predator_senses`, `test_tree_collapse_demo`,
  `test_humanoid_terrain_scenarios`). These are standalone `main()`
  executables. Same idea, different spelling. Converge on `INTERACTIVE`.

**Trap, VERIFIED:** never set `create_display = true` in anything ctest runs.
`glfwInit()` on Cocoa runs `[NSApp run]` and blocks on the window-server
handshake — that is what made `ctest -j 8` deadlock, and it is why
`tests/test_headless_no_window_server.cpp` exists (`CMakeLists.txt:611-615`).

---

## b) Headless capture, and one scene shared by both modes

This is the architectural requirement, and it is the one the tree gets
wrong most often. Split it in three, because there are three modes, not two:

| Mode | Profile | What it proves |
|---|---|---|
| **CI headless** | `physics` / `core` (Linux) | The numbers. No renderer exists, so no capture is possible. |
| **Capture headless** | `full` (macOS), `create_display=false` | The numbers **and** the pixels. PPM dump, brightness assertion. |
| **Interactive** | `full` (macOS), `create_display=true` | A human's eyes. |

### b1. The capture pattern — VERIFIED

Best implementation: `tests/test_shadow_edge_quantization.cpp:85-116`.

```cpp
// Settle: temporal shadow / SSDO accumulation needs frames, and the frame
// you read must be the frame you rendered.
for (int i = 0; i < 8; ++i) {
    engine.update(1.0 / 60.0);
    engine.render();
    engine.get_renderer().wait_for_completion();
}

int w = engine.get_render_buffer().width();
int h = engine.get_render_buffer().height();
std::vector<uint32_t> px((size_t)w * h);
if (!engine.read_latest_framebuffer(px.data(), w, h)) {
    printf("  ERROR: framebuffer readback failed\n"); return false;
}

FILE* f = fopen("/tmp/mytest.ppm", "wb");                 // 0x00RRGGBB in, RGB out
if (f) {
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        unsigned char rgb[3] = { (unsigned char)((px[i] >> 16) & 0xFF),
                                 (unsigned char)((px[i] >>  8) & 0xFF),
                                 (unsigned char)( px[i]        & 0xFF) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
```

Same shape at `tests/test_rube_goldberg_machine.cpp:899-922` (behind
`RUBE_DUMP=<frame>`), `tests/test_multi_light_progressive.cpp:69-82,
454-463`, `tests/test_rotation_ladder.cpp:93-106`.

**`read_latest_framebuffer` semantics, VERIFIED** (`engine.cpp:705-755`):
it asks the renderer for the GPU scene buffer, then composites `ui_buffer_`
over it pixel by pixel, honouring per-pixel alpha. So a captured frame
**does** include your Labels. That was a bug fixed 2026-08-01: the scene
buffer is scene-only by construction (pixels only ever flow GPU →
`render_buffer_`), and separately the headless composite landed on frame
N-1 which frame N's completion callback then memcpy'd over — so
`wait_for_completion()` *guaranteed* the HUD was erased. Reading
`ui_buffer_` at the last moment fixes both. Guarded by
`tests/test_ui_label_actually_renders.cpp` (0 pixels moved by ≥32 before,
367 after, max delta 245).

**Trap:** UI pixels only exist in `ui_buffer_` after a `draw_ui_overlays()`
that ran *after* your `set_text()`. In headless that happens at the end of
`render()` (`engine.cpp:1612`), so the order is `set_text → render →
wait_for_completion → read`.

### b2. "Is it lit?" as a measurement, not a promise — VERIFIED

Two implementations, and the second is strictly better.

```cpp
// tests/test_knockback_scene.cpp:400-430 — counts pixels above black
CHECK(lit_pct > 20.0, "the scene is actually lit");
```

```cpp
// tests/test_rotation_ladder.cpp:74-107 — counts STRUCTURED pixels.
// An all-black frame (no light) AND an all-white frame (light in the lens)
// both score ~0. This catches the failure knockback's version cannot.
for (uint32_t c : px) {
    const int sum = ((c>>16)&0xFF) + ((c>>8)&0xFF) + (c&0xFF);
    if (sum <= 60) black++; else if (sum >= 720) white++; else n++;
}
```

Use `structured_pixels`. **This is requirement (c) turned into an
assertion**, and it belongs in the headless half so a scene that goes dark
fails in CI instead of at the owner's window.

### b3. Sharing the scene — the honest analysis

**`tests/test_angular_dissipation.cpp` + its `test_angular_dissipation_visual`
target (`CMakeLists.txt:1280-1309`) is the first attempt. Critique, all
verified:**

1. **The physics is duplicated, and the two copies do not agree.**
   `spin_alone()` (`:94-124`) builds a bare `ParticleSystem` +
   `PhysicsSystem` and steps `physics.update(1.0/60.0)` 60 times.
   `run_visual()` (`:131-220`) builds an `Engine` and steps
   `engine.update(1.0/60.0)`. Because `PHYSICS_TIMESTEP = 1.0/30.0`
   (`src/core/time_system.h:74`) and `engine.update` accumulates
   (`time_system.cpp:113-121`), the windowed half takes **one physics step
   per two frames, at dt = 1/30**. The headless half takes one per frame at
   dt = 1/60. `ANGULAR_DRAG` is applied per substep, so the two halves
   measure different spin-down by construction. They are not reflections;
   they are two different experiments that share a file.
2. **The visual half asserts nothing.** No `check()`, no verdict, no exit
   code. The owner is asked to look, but not told what pass looks like.
3. **ESC does not work.** `while (engine.is_running())` at `:190` — ESC
   sets `should_close`, which `is_running()` never reads
   (`src/core/engine.h:294`). The printed banner at `:185` promises
   "ESC or close the window when you have seen enough". Both are false.
4. **The subject is never on screen.** `cam.set_pixels_per_unit(220)` and
   `cam.set_camera_deadzone(0)` at `:156-157`, and then the camera is never
   positioned and never follows. It stays at the isometric default
   `(-10,-10,20)`. The cube is at `(0,0,40)`, so `iso_y = (10+10)·0.5 +
   (40−20) = 30` world units → `30 × 220 = 6600 px` above screen centre in
   a 720-tall window. The file header at `:60-61` says "the camera follows
   it because gravity is a hardcoded -Z acceleration and it therefore falls
   while it spins". The code does not follow it.
5. **The lights do not follow either.** `queue_light(..., z=41.5, radius=30)`
   at `:159`. The cube falls. Past ~30 m of fall it is outside
   `emission_radius` and receives exactly zero
   (`shadow_rays_deferred.metal:1112`).
6. **The CI claim is false.** `:52` says "the headless half runs in Linux
   CI". `test_angular_dissipation` has no `add_test()` and the
   `physics-linux` lane runs a hand-written list of two binaries plus the
   guard suite (`.github/workflows/ci.yml:151-163`), not `ctest`. It is
   built and never run. The `core` profile has a fatal-error guard for
   exactly this (`CMakeLists.txt:1129-1140`); the `physics` profile has none.

So: a black window that cannot be closed, showing nothing, with a physics
half that measures a different thing. That is the window the owner was
handed.

**RECOMMENDED factoring** — one scene, three drivers, no duplication:

```
tests/scenes/scene_<name>.h        header-only, links nothing beyond
                                   particle_system.h + physics_system.h
tests/test_<name>.cpp              CI headless   (physics profile)
tests/test_<name>_visual.cpp       capture + window (full profile)
```

The header owns four things and **nothing else**:

```cpp
// tests/scenes/scene_<name>.h
namespace scene_<name> {

// The one timestep both drivers use. It MUST equal TimeSystem::PHYSICS_TIMESTEP
// (1/30, src/core/time_system.h:74) or the two modes run different physics.
constexpr double SIM_DT = 1.0 / 30.0;
constexpr int    FRAMES = 300;

struct Ids   { int subject = -1; /* ... */ };
struct Vitals{ float spin, height, worst_speed; /* pure numbers */ };
struct Verdict{ bool ok; char line[192]; };   // ONE judge, both modes call it

Ids    build  (ParticleSystem& ps, PhysicsSystem& phys);        // places bodies
Vitals measure(ParticleSystem& ps, const Ids&);                 // reads state
Verdict judge (const Vitals&);                                  // THE assertion

}  // namespace
```

- `build` takes `ParticleSystem&` and `PhysicsSystem&`, never `Engine&`.
  Both are reachable from an Engine (`engine.get_particle_system()`,
  `engine.get_physics_system()`), so the same builder serves both.
- `judge` returns a **bool and a human line**. The CI driver asserts on the
  bool; the visual driver puts the line in a Label and colours it green or
  red. Requirement (g) and requirement (b) are the same function.
- The CI driver steps `ps.update_bvh(); phys.update(SIM_DT);` once per
  frame. The Engine driver steps `engine.update(SIM_DT)` once per frame.
  At `SIM_DT = 1/30` the accumulator releases exactly one step per call, so
  the step count and step size match. **Verified** against
  `time_system.cpp:113-121`.

**Residual, stated honestly:** `engine.update()` also runs the dynamics,
locomotion, interaction and event systems. For a scene of plain particles
those are no-ops and the two drivers agree. For a scene containing a
humanoid, grass generated by worldgen, or anything driven by a locomotion
system, they do not agree and **no CI half is possible** — such a test is
capture-headless + interactive only, in the full profile. Say which kind
your test is in its header comment.

**And register it.** A `physics`-profile test with no `add_test()` is dead
weight (§b3 item 6). Either `register_headless_test(name)`
(`CMakeLists.txt:340-342`) or add it to the `physics-linux` list in
`.github/workflows/ci.yml:151-158`.

---

## c) Light, zoom, camera position and angle

### c1. Light — the rule, derived from the shader

Both the GPU and CPU paths compute

```
lux = emission_strength / (4π · d²)          and 0 if d > emission_radius
```

(`src/rendering/gpu/shadow_rays_deferred.metal:1121, 1112`;
`src/simd_multi_light.h:167, 154`), then the zone tone-map turns lux into
RGB (`src/rendering/gpu/apply_lighting_deferred.metal:132-153`):

| raw lux at the surface | RGB |
|---|---|
| 10 | 75 (bottom of the shadow zone) |
| 100 | 200 (top of the midtone zone) |
| ≥ 200 | saturating toward 255 |

A Lambertian surface at a typical angle keeps roughly half, so aim for
**300 lux at the subject**, which gives:

> **`emission_strength ≈ 4π · 300 · d²  ≈  4000 · d²`**
> **`emission_radius   ≥ 1.25 · d`** (the last 20 % of the radius is faded,
> `shadow_rays_deferred.metal:1126`)

where `d` is the light-to-subject distance. Checked against every working
test in the tree:

| test | pos → subject `d` | strength used | rule says | ratio |
|---|---|---|---|---|
| `test_walk_through_grass.cpp:100` | ~15 m | 500 000 | 900 000 | 0.6× |
| `test_single_blade_contact.cpp:107` | ~10 m | 350 000 | 400 000 | 0.9× |
| `test_rotation_ladder.cpp:66` | ~12 m | 650 000 | 576 000 | 1.1× |
| `test_grass_natures.cpp:242` | ~14 m | 600 000 | 784 000 | 0.8× |
| `test_shadow_edge_quantization.cpp:81` | ~9 m | 800 000 | 324 000 | 2.5× |
| `test_knockback_scene.cpp:381` | ~52 m | 26 000 000 | 10 800 000 | 2.4× |
| `test_rube_goldberg_machine.cpp:450` | ~5 m | 250 000 | 100 000 | 2.5× |
| **the documented failure**, `test_rube_goldberg_machine.cpp:255` and its comment at `:451-453` | 14 m | 60 000 | 784 000 | **0.08×** |

The rule reproduces the whole working cluster within 0.6–2.5×, and it
explains the one that failed: one 60 000 light 14 m up delivers 24 lux,
which after Lambert is RGB ≈ 78 — the dark grey the owner reported as
black. `test_totem_gluon_nails.cpp:759` (100 000 at d ≈ 16 m → 32 lux) and
`test_eva_movement.cpp:390` (80 000 at d ≈ 11 m → 56 lux) are the same
defect, still shipped.

**Canonical rig — VERIFIED, `test_rube_goldberg_machine.cpp:438-456`:**

```cpp
// Light and camera are UNCONDITIONAL, not inside `if (interactive)`.
// The headless capture path needs them too: with these behind the flag,
// the frame-200 dump was a black frame with a tiny unlit machine.
auto& cam = engine.get_camera_system();
cam.set_pixels_per_unit(90.0f);
cam.set_camera_deadzone(0.0f);
// Three lights strung along the chain, close enough to matter. One 60k
// light 14 m up left the whole machine in the dark.
ps.queue_light(1.5f, -2.5f, 8.5f, 250000.0f, 40.0f, 1.0f, 0.95f, 0.85f);
ps.queue_light(6.5f, -2.5f, 7.0f, 250000.0f, 40.0f, 1.0f, 0.95f, 0.85f);
ps.queue_light(11.0f, -2.5f, 5.5f, 250000.0f, 40.0f, 1.0f, 0.92f, 0.80f);
```

Notes:
- **Never gate lights on `interactive`.** Verified lesson, cited above.
- **One light per ~6 m of scene extent**, placed south of and above the
  action. A single light cannot serve a 13 m machine at inverse square.
- Lights are **queued** particles (`src/core/particle_system.cpp:697-715`).
  They appear after `flush_pending_particles()` or one `update()`.
  `test_knockback_scene.cpp:393` runs six updates before showing the window
  precisely because "the first frames render unlit, which is what a viewer
  sees first".
- The manual route (`Particle p; p.is_light_source = true;
  p.emission_strength = …; p.emission_radius = …;`) is equivalent —
  `test_spine_fk_lookat.cpp:65-75`. `queue_light` is the one-liner. Prefer it.
- **A falling subject leaves the light.** `emission_radius` is a hard
  cutoff. Either raise the radius past the whole fall or move the lights
  with the subject.

### c2. Zoom and position — the formula, derived from the projection

Under the default isometric projection (`src/projection_system.cpp:26-38`):

```
view = world − camera
iso_x = (view_x − view_y) · 0.866
iso_y = (view_x + view_y) · 0.5 + view_z
screen_x = W/2 + iso_x · ppu
screen_y = H/2 − iso_y · ppu          W,H = get_render_buffer() dims
```

Two consequences that decide every framing question:

**1. `cam.set_position(sx, sy, sz)` puts the point `(sx,sy,sz)` at exact
screen centre.** VERIFIED and used deliberately:

```cpp
// tests/test_rube_goldberg_machine.cpp:876-883
// Direct centering: iso_x=(vx-vy)*.866, iso_y=(vx+vy)*.5+vz with
// view = world - camera, so camera AT the subject puts it at exact screen
// center. update_follow_target applies a diagonal offset tuned for Eden's
// camera height and never writes camera z — at z=0 it centered a point
// ~30 world units off-frame (measured: an all-black dump).
const Particle b = w.read(w.ball);
w.engine.get_camera_system().set_position(b.x, b.y, b.z);
```

**This is the answer to "how do I keep a FALLING body on screen":** call
`set_position` with the body's live position every frame. It is one line,
it costs nothing, and it is the only follow that works for a body that
moves in z. `update_follow_target` is the wrong tool — verified, it never
writes camera z (`src/core/camera_system.cpp:105-110`) and it subtracts a
diagonal offset meant for Eden.

**2. `pixels_per_unit` to frame a subject — RECOMMENDED** (derived from the
projection math above; there is no such helper in the tree today):

For a subject with half-extents `(hx, hy, hz)` centred at the camera,
`|iso_x|max = 0.866·(hx+hy)` and `|iso_y|max = 0.5·(hx+hy) + hz`, so

```cpp
// RECOMMENDED helper. W,H must be the RENDER buffer, not the window:
// camera viewport is set from render_width_/render_height_ (engine.cpp:306),
// which is ~2x the configured window size on retina.
float frame_ppu(Engine& e, float hx, float hy, float hz, float margin = 0.85f) {
    const float W = (float)e.get_render_buffer().width();
    const float H = (float)e.get_render_buffer().height();
    const float sx = 0.866f * (hx + hy);
    const float sy = 0.5f   * (hx + hy) + hz;
    return margin * std::min(W / (2.0f * sx), H / (2.0f * sy));
}
```

Sanity-checked against the tree's hand-tuned values (render buffer
1600×1200 or its retina double):

| subject | test | ppu used |
|---|---|---|
| one grass blade, ~0.4 m | `test_single_blade_contact.cpp:225` | **140** ("close: one blade is the frame") |
| a grass patch, ~1 m | `test_grass_natures.cpp:246` | **170** |
| a walking humanoid, ~2 m | `test_walk_through_grass.cpp:172` | **90** |
| a humanoid + 2 m of rig | `test_rotation_ladder.cpp:1239` | **74** |
| a humanoid at range | `test_spine_fk_lookat.cpp:217` | **80** |
| a 13 m machine, following one actor | `test_rube_goldberg_machine.cpp:445` | **90** |
| two creatures 24 m apart on an open field | `test_knockback_scene.cpp:388` | **24** |

So in practice: **~140–220 for a sub-metre subject, ~70–90 for a
person-sized one, ~20–30 for a scene tens of metres across.** Keep
`set_pixels_per_unit` (unclamped) and avoid `adjust_zoom` (clamps to 200,
`camera_system.cpp:322-326`).

### c3. Angle

Under the default isometric projection there is exactly one lever:
**`cam.set_view_azimuth(radians)`** (`src/core/camera_system.h:31-37`,
implemented `src/projection_system.h:118-124`), which orbits the view
around world +Z, CW-positive from above, azimuth 0 = the classic view.
Round-trip-locked by `tests/test_iso_azimuth_roundtrip.cpp`.

`look_at()` does **not** change the isometric picture — `project()` reads
only the camera position. It changes `forward_`, which parallel-projection
backface culling uses (`camera_system.cpp:535-540`). The constructor
default already matches the iso view axis. **RECOMMENDED: in iso mode set
`set_position` and leave `forward_` alone**, or if you do call `look_at`,
call it from a camera that is south-west of and above the target so
`forward_` keeps its `(+,+,−)` sign pattern. `test_walk_through_grass.cpp:170-171`
looks due east (`forward ≈ (0.95, 0, −0.32)`), which puts every ±Y-facing
surface on the cull boundary — untested, but it is the shape of a bug.

For a genuinely different camera, `engine.set_projection_mode(...)`
(`src/core/engine.h:236`) switches to `Perspective`, where `look_at` and
camera distance do what you expect. `CameraDirector::focus_on` exists
(`src/core/camera_director.h:40`) but sets position + look-at only, so
under iso its `distance` argument changes nothing — it is not the framing
tool.

---

## d) ESC always stops the test

The engine already does the hard part. What a test must not do is loop on
`is_running()` alone.

**BEST EXISTING — `tests/test_rotation_ladder.cpp:111-121`.** ESC checked
on *every* stepped frame, converted into `engine.stop()` so that every
`while (engine.is_running())` in the file terminates:

```cpp
// One paced step. Interactive needs the FULL trio update+render+present:
// update() alone dispatches nothing, and render() without present() fills a
// buffer no window ever shows (the white-window bug, three launches deep).
void step(Engine& engine) {
    engine.update(1.0 / 60.0);
    if (g_interactive) {
        engine.render();
        engine.present();
        // ESC MUST QUIT, FROM ANY PHASE. It was polled only inside the hold
        // loops, so during a sweep or a settle the key did nothing and the
        // owner had to kill the process. Every stepped frame checks now.
        if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE])
            engine.stop();
    }
}
```

**SECOND — `tests/test_knockback_scene.cpp:249-257`,** the only version
that also catches the window's close button, via a direct GLFW poll:

```cpp
bool pump(Engine& e) {
    e.get_platform()->poll_events();
    auto* win = static_cast<GLFWwindow*>(e.get_platform()->get_native_window_handle());
    if (!win) return true;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return false; }
    if (e.get_platform()->should_close())               { g_quit = true; return false; }
    return true;
}
```

**THIRD — `tests/test_spine_fk_lookat.cpp:227`,** the only test that reads
`should_close()` through the platform interface without GLFW:

```cpp
if (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) break;
```

**RECOMMENDED canonical form** — combines all three, no GLFW include, works
from any phase, catches ESC *and* the red X:

```cpp
// ESC is bound by the ENGINE (key_mapper.cpp:266 -> main_key_handler.cpp:33),
// and engine.update() polls (engine.cpp:1127). All a test must do is READ
// the right predicate. is_running() is the wrong one: it never reads
// should_close (engine.h:294), which is why so many viewers cannot be closed.
inline bool alive(Engine& e) {
    if (!e.should_continue()) return false;                       // ESC + window X
    if (e.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) {
        e.stop();                                                 // belt and braces
        return false;
    }
    return true;
}
```

`GLFW_KEY_ESCAPE` is available without linking GLFW via
`src/platform/glfw_compat.h:88` (`#define GLFW_KEY_ESCAPE 256`), which
`input_system.h` pulls in for the `keys[GLFW_KEY_LAST]` array
(`src/core/input_system.h:27`).

**Broken today by this criterion:** `test_rube_goldberg_machine` (the
owner's declared acceptance vehicle — its hold loop at `:1043-1053` and its
main loop at `:869` both use `is_running()` and never read ESC at all),
`test_angular_dissipation_visual`, `test_totem_gluon_nails`,
`test_hunter_rotation`, `test_gluon_tree`, `test_physics_tree`,
`test_running`, `test_eva_movement`, `test_gi_bounce`,
`test_experiment_totem_builder`, `test_humanoid_impact`,
`test_physics_experiment_01`, `test_physics_experiment_03_eva_constraints`,
`test_physics_rock`, `test_spirit_light_artifacts`, `test_gluon_tree_v34`,
`test_physics_profile`, `test_dynamics_override`. Every one of these must
be killed from another terminal.

---

## e) SPACE moves towards the test

**What exists — VERIFIED.** In every test that binds SPACE, it means
**advance to the next stage/phase**. There is no camera-dolly-on-SPACE
anywhere in the tree.

Best implementation, `tests/test_rotation_ladder.cpp:126-141`:

```cpp
// Freeze the world (dt = 0 still pumps the window) until SPACE. Returns
// false if the owner quit instead.
bool wait_for_space(Engine& engine) {
    if (!g_interactive || g_autopilot) return true;
    bool space_was = true;      // swallow a still-held press from the last stage
    while (engine.is_running()) {
        const auto& in = engine.get_input_system().get_input_state();
        const bool space = in.keys[GLFW_KEY_SPACE];
        if (space && !space_was) return true;      // rising EDGE, not level
        space_was = space;
        if (in.keys[GLFW_KEY_ESCAPE]) return false;
        engine.update(0.0);      // dt = 0: world frozen, window still alive
        engine.render();
        engine.present();
    }
    return false;
}
```

Two details that matter and are easy to miss:
- **Edge, not level.** `keys[]` is a held-state array
  (`src/core/input_system.cpp:134-138`), so a level test fires every frame.
  Every working implementation tracks `space_was`.
- **`update(0.0)` keeps the window alive while the world is frozen.** The
  event poll lives inside `update()` (`engine.cpp:1127`), so a bare
  `render()/present()` loop would stop responding.

Same pattern: `test_multi_light_progressive.cpp:436-446` (SPACE breaks to
the next light-count phase), `test_ssgi_visual.cpp:553-560` (SPACE cycles
scene cases and rebuilds), `test_totem_gluon_nails.cpp:28-55`
("Press SPACE to continue" gate between sub-tests),
`test_tree_shadow_wiggly.cpp:303-329` (SPACE = "shadows OK", ESC =
"shadows BAD" — SPACE as a *verdict*, which is a nice variant),
`test_grass_natures.cpp:56-67`.

**SPACE is safe.** It is bound to `CREATE_PARTICLE_AT_MOUSE`
(`src/key_mapper.cpp:311`) but the engine deliberately registers no handler
for it (`src/main_key_handler.cpp:141-145`), so pressing it spawns nothing.

**RECOMMENDATION for "move towards the test".** Read literally it could
mean a camera dolly. Mechanically, a dolly is available
(`cam.set_position` toward the subject each press, or
`set_pixels_per_unit` up — **not** `adjust_zoom`, which clamps at 200).
But every existing test, and the phrase "move towards" in the context of a
staged test, points at **stage advance**: hold at each stage, let the owner
look, SPACE to proceed. Recommend:

- **SPACE = advance one stage**, freezing the world at every stage boundary
  (`wait_for_space` above). This is the existing convention and it is what
  makes a physics test *readable* — the owner sees each claim settle before
  the next one starts.
- Add **`+` / `-` for zoom** and **`[` / `]` for azimuth** if a test wants
  camera control, so SPACE keeps one meaning across all tests. This is a
  recommendation; no test does it today.

---

## f) FPS always present

**No interactive test in this repo shows FPS on screen today** except one.
`grep -c "current_fps\|get_metrics"` over all 62 interactive tests returns
zero for every file.

**CHEAPEST CORRECT ROUTE — VERIFIED, one line:**

```cpp
cfg.show_debug_overlay = interactive;   // engine FPS overlay, user-visible
```

`tests/test_multi_light_progressive.cpp:139` is the only test that does
this. The chain is fully verified: `engine.cpp:503` enables the overlay,
`engine.cpp:1541-1548` collects metrics inside `render()`,
`debug_overlay.cpp:190-228` renders it, `ui_system.cpp:1254-1261` draws
`FPS: N (min:x max:y)` at the top-left, and it survives the overlay clear
because `draw_ui_overlays()` re-renders the debug overlay explicitly
(`engine.cpp:1670-1672`). Backtick toggles it live
(`src/key_mapper.cpp:267`).

Cost: the overlay is a ~20-line block occupying the top-left of the screen
(resolution, aspect, frame breakdown, particle counts). If a test wants the
corner for its own readout, put the test's Labels lower or on the right.

**SECOND ROUTE — the number in your own Label**, when you want it inline
with the test's other vitals:

```cpp
const auto& m = engine.get_metrics();
snprintf(buf, sizeof buf, "%.0f FPS  (frame %.1f ms, render %.1f ms)",
         m.current_fps, m.total_frame_time, m.render_time);
l_fps->set_text(buf);
```

`EngineMetrics::current_fps` is a 10-frame EMA, `min_fps`/`max_fps` reset
each second (`engine.cpp:1766-1781`). **Caveat, verified:** the `Frame`
telemetry phase closes at the *start* of the next `update()`
(`engine.cpp:1083-1085`), so your pacing `sleep_until` is inside the
measurement. On a 60 Hz-paced viewer `current_fps` reads ~60 and means
"the loop is keeping up"; `m.render_time` is the number that means "the
renderer costs this much".

**THIRD ROUTE — compute it yourself**, only if you want a window that
excludes the sleep. `test_multi_light_progressive.cpp:411-417` does a
rolling mean of the last 30 measured frames. More code, and it then
reported it through `ui->draw_text`, which is erased (see (g)).

**RECOMMENDED: do both.** `show_debug_overlay = interactive` for the
engine's own FPS block, and one Label line carrying `current_fps` +
`render_time` next to the test's vitals, so the number sits with the thing
it explains.

---

## g) Clear assertions and a readout in the UI

### g1. The transport: registered widgets, never immediate mode

**VERIFIED, and the failure is silent.** `Engine::present()` calls
`draw_ui_overlays()` (`engine.cpp:1857`), whose first act is to clear the
overlay plane (`engine.cpp:1659-1668`) and whose second is to re-render the
debug overlay and every registered widget (`engine.cpp:1670-1673`). Any
`ui->draw_text()` a test calls between `render()` and `present()` is wiped
before a pixel reaches the window. `draw_ui_overlays()` is private, so a
test cannot draw into that pass.

**35 of the 62 interactive tests report through this route and only this
route** (census in the backlog section below), so their entire on-screen
readout is invisible. The worst examples:
`test_ssgi_visual.cpp:534-548` (its per-assertion PASS/FAIL list — the
best-*designed* readout in the tree, on the wrong API),
`test_multi_light_progressive.cpp:426-427`,
`test_totem_gluon_nails.cpp:39-47, 412-415, 568-578, 681-685`,
`test_tree_shadow_wiggly.cpp:197, 267-270, 312-315`.

The widget route is proven, not inferred: `tests/test_ui_label_actually_renders.cpp`
renders the same frame with and without a `ui::Label`, takes an A-vs-A
noise floor first, and asserts on `over32`. Before the read-back fix, 0
pixels moved by ≥32; after, 367 with max delta 245.

### g2. Multi-line readout — the working factory

**BEST — `tests/test_single_blade_contact.cpp:227-238`.** A lambda that
mints Labels at a fixed line pitch, so adding a line is one call:

```cpp
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"

ui::Label *l_title = nullptr, *l_live = nullptr, *l_bonds = nullptr;
if (auto* uis = engine.get_ui_system()) {
    auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
        auto* L = new ui::Label("", "");
        L->set_position(24, y); L->set_size(1500, 22); L->set_color(r, g, b);
        uis->add_widget(L);
        return L;
    };
    l_title = mk(24, 255, 255, 255);   // what this test claims
    l_live  = mk(54, 120, 220, 255);   // the numbers, moving
    l_bonds = mk(84, 110, 235, 130);   // the second family of numbers
}
// ... per frame, before present():
snprintf(buf, sizeof buf, "f %d  Eva y=%5.2f  contacts %llu  worst %4.1f m/s", ...);
l_live->set_text(buf);
```

Three-role convention, and it is consistent across the good tests:
**title = the claim** (`test_walk_through_grass.cpp:172-175`: "EVA WALKS
THROUGH 3 GRASS PATCHES. Good = she crosses, grass bends but stays,
nothing detonates."), **live = the measured numbers**, **verdict = the
judgement**.

`ui::Label` API, verified from `src/ui/widgets.h:9-27` and
`src/ui/widget.h:89-102`: `set_text`, `set_color(r,g,b)`, `set_position`,
`set_size`, `set_visible`. `Label::render` (`src/ui/widgets.cpp:18-41`)
reads only `bounds.x/y`, so `set_size` is not required for the text to
appear — set it anyway for clarity.

**Ownership trap, VERIFIED:** `UISystem::add_widget` keeps a raw pointer
and does **not** own (`ui_system.cpp:2196-2203`). `docs/VISUAL_TESTS.md:63`
says otherwise and is wrong. A `new ui::Label` never freed is a harmless
test-lifetime leak; a Label freed while still registered is a dangling
pointer the next `draw_ui_overlays()` will follow. If you must own it, hold
it and `remove_widget()` before destruction — or use
`UISystem::create_panel`, which does own (`ui_system.cpp:2253-2262`).

**For a boxed, scrolling panel** rather than free-floating lines,
`TextWindow` (`src/ui/text_window.h`) is the widget:

```cpp
// tests/test_knockback_scene.cpp:361-372 and 325-345
panel = std::make_unique<TextWindow>("knockback", "knockback_log");
panel->set_position(700, 40);  panel->set_size(370, 260);
panel->set_max_lines(20);      panel->set_newest_at_top(false);
panel->set_background_alpha(210);
ui->add_widget(panel.get());
// per frame:
panel->clear();
panel->add_line("KNOCKBACK: a contact rule pushing back");
snprintf(line, sizeof line, "separation   %.2f m", sep);  panel->add_line(line);
panel->add_line(sep < kTouchDistance ? "STATE  overlapping" : "STATE  clear");
```

`TextWindow` is a `ui::Window` subclass, so it draws a titled,
semi-transparent, draggable box. Best choice when the readout is a table.
Labels are better when the readout is 2–4 fixed lines.

### g3. The assertions themselves

Two disciplines already in the tree, both worth keeping:

**Per-stage, named, with the measured value in the same line** —
`tests/test_rube_goldberg_machine.cpp:523-529, 963-979`:

```cpp
struct Stage {
    const char* name;
    const char* invs;        // which invariants this stage exercises
    const char* frontier;    // nullptr = expected green today
    int deadline;            // absolute frame
    std::function<bool(World&, std::string&)> pass;   // returns ok + measured text
};
// ...
if (st.pass(w, measured)) {
    printf("  [%4d] GREEN  %-20s (%s)  %s\n", frame, st.name, st.invs, measured.c_str());
} else if (frame >= st.deadline) {
    printf("  [%4d] RED    %-20s (%s)\n", frame, st.name, st.invs);
    printf("         measured: %s\n", measured.c_str());
}
```

The predicate returns **both the boolean and the human string**. That is
exactly the `Verdict` shape §b3 recommends, and it is why this test can
report the same judgement to stdout and (once it grows Labels) to the screen.

**Global watchdogs that halt on the first violation** —
`test_rube_goldberg_machine.cpp:117-120, 927-961`: a speed ceiling
(INV-11), a penetration ceiling (INV-2), NaN. Any test that runs physics
for hundreds of frames should carry them; they turn "it looked wrong" into
"body 43 hit 87 m/s at frame 512".

**A frame-chain tripwire** — `test_rube_goldberg_machine.cpp:893-897`:

```cpp
if (frame == 60 && w.engine.presents_completed() == 0) {
    printf("  MACHINE RED: window shown but nothing presented (frame-chain broken)\n");
    return false;
}
```

`presents_completed()` (`src/core/engine.h:163`) counts `present()` calls.
This catches the white-window failure — a test that renders but never
presents — as a red instead of a puzzled owner.

**RECOMMENDED addition:** put every assertion's PASS/FAIL line on screen,
green or red, the way `test_ssgi_visual.cpp:537-544` designed it — just
through Labels instead of `draw_text`. That is requirement (g) satisfied
literally, and it is why `Verdict` carries a `bool` and a `char[]`.

---

## The skeleton

RECOMMENDED. Every mechanism in it is cited above and verified; the
*assembly* is the proposal. Three files.

### 1. `tests/scenes/scene_falling_cube.h` — the scene, shared, no engine

```cpp
// =============================================================================
// SCENE: <one sentence saying what this scene is FOR>
// =============================================================================
// This header is the single source of truth for the bodies, the numbers and
// the verdict. It links nothing beyond ParticleSystem + PhysicsSystem, so it
// compiles in the `physics` CI profile AND inside the full macOS engine.
// Both drivers call build() / measure() / judge(). Nothing is duplicated.
// =============================================================================
#pragma once

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"
#include "materials.h"

#include <cmath>
#include <cstdio>

namespace scene_falling_cube {

// THE STEP. Must equal TimeSystem::PHYSICS_TIMESTEP (1/30, time_system.h:74).
// engine.update(dt) accumulates and only fires a physics step when the
// accumulator reaches PHYSICS_TIMESTEP (time_system.cpp:113-121), so any other
// value makes the two drivers run DIFFERENT simulations.
constexpr double SIM_DT = 1.0 / 30.0;
constexpr int    FRAMES = 300;

// The subject's half-extents, used by the visual driver to frame it.
constexpr float HALF = 0.2f;

struct Ids { int subject = -1; };

// Pure numbers. No formatting, no judgement.
struct Vitals {
    float x = 0, y = 0, z = 0;
    float speed = 0;
    float spin  = 0;
};

// The judgement, and its one-line explanation. BOTH modes call this.
struct Verdict { bool ok = false; char line[192] = {0}; };

inline Ids build(ParticleSystem& ps, PhysicsSystem& phys) {
    (void)phys;
    Ids ids;
    Particle p{};
    p.shape = ParticleShape::BOX;
    p.x = 0.0f; p.y = 0.0f; p.z = 4.0f;
    p.width = p.height = p.thickness = HALF * 2.0f;
    p.size = HALF * 2.0f;
    p.r = 0.85f; p.g = 0.55f; p.b = 0.25f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    ids.subject = ps.queue_particle_addition(p);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[ids.subject].omega_z    = 4.0f;
        v[ids.subject].is_at_rest = false;
    }
    return ids;
}

inline Vitals measure(ParticleSystem& ps, const Ids& ids) {
    auto v = ps.lock_particles_for_read();
    const auto& p = v[ids.subject];
    Vitals vt;
    vt.x = p.x; vt.y = p.y; vt.z = p.z;
    vt.speed = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
    vt.spin  = std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y + p.omega_z*p.omega_z);
    return vt;
}

// ONE assertion, stated once, in the words the owner will read on screen.
inline Verdict judge(const Vitals& v) {
    Verdict d;
    d.ok = (v.z > 0.0f) && (v.speed < 50.0f) && (v.spin > 3.9f);
    snprintf(d.line, sizeof d.line,
             "%s  z=%.3f m  |v|=%.2f m/s  spin=%.4f rad/s (started 4.0)",
             d.ok ? "PASS" : "FAIL", v.z, v.speed, v.spin);
    return d;
}

}  // namespace scene_falling_cube
```

### 2. `tests/test_falling_cube.cpp` — CI headless, numbers only

```cpp
// Headless half. Linux `physics` profile: no renderer exists here, so this
// half asserts numbers and nothing else. The pixels are the visual target's
// job (test_falling_cube_visual, capture mode).
//
//   ./build/test_falling_cube
#include "scenes/scene_falling_cube.h"
#include <cstdio>

namespace S = scene_falling_cube;

int main() {
    printf("\n=== falling cube: headless ===\n");

    ParticleSystem ps;
    PhysicsSystem  phys;
    if (!phys.initialize(ps)) { printf("  ERROR: physics init failed\n"); return 1; }

    const S::Ids ids = S::build(ps, phys);
    for (int f = 0; f < S::FRAMES; ++f) {
        ps.update_bvh();
        phys.update(S::SIM_DT);      // one step per frame, same size as the
    }                                // engine's accumulator releases
    const S::Verdict d = S::judge(S::measure(ps, ids));

    printf("  %s\n", d.line);
    phys.shutdown();
    return d.ok ? 0 : 1;
}
```

### 3. `tests/test_falling_cube_visual.cpp` — capture AND window, one loop

```cpp
// =============================================================================
// FALLING CUBE — the same scene, with pixels.
// =============================================================================
//   ./build/test_falling_cube_visual                 headless CAPTURE:
//                                                    renders offscreen, asserts
//                                                    the frame is lit and
//                                                    structured, dumps a PPM
//   INTERACTIVE=1 ./build/test_falling_cube_visual    windowed, watchable
//
//   ESC          quit, from any phase
//   SPACE        advance to the next stage (the world freezes between stages)
//   `            toggle the engine debug overlay (FPS block)
//
// The scene, the numbers and the verdict come from scenes/scene_falling_cube.h
// and are shared verbatim with test_falling_cube (the Linux CI half).
// Nothing about the physics is restated here.
// =============================================================================
#include "core/engine.h"
#include "scenes/scene_falling_cube.h"
#include "ui/ui_system.h"
#include "ui/widgets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace S = scene_falling_cube;

namespace {

bool g_interactive = false;

// ---------------------------------------------------------------- input
// ESC is bound by the ENGINE (key_mapper.cpp:266 -> main_key_handler.cpp:33)
// and engine.update() polls (engine.cpp:1127). All a test must do is read the
// right predicate: is_running() never reads should_close (engine.h:294), which
// is why so many viewers in this tree cannot be closed.
bool alive(Engine& e) {
    if (!e.should_continue()) return false;                       // ESC + window X
    if (e.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) {
        e.stop();
        return false;
    }
    return true;
}

// Rising EDGE, because keys[] is a held-state array (input_system.cpp:134-138).
bool space_pressed(Engine& e) {
    static bool was = true;      // swallow a press still held from the last stage
    const bool now = e.get_input_system().get_input_state().keys[GLFW_KEY_SPACE];
    const bool edge = now && !was;
    was = now;
    return edge;
}

// ---------------------------------------------------------------- camera
// The isometric projection puts the camera position at exact screen centre:
// iso_x = (vx-vy)*0.866, iso_y = (vx+vy)*0.5 + vz, view = world - camera
// (projection_system.cpp:26-38). So following a body is set_position(body).
// update_follow_target is the WRONG tool: it never writes camera z
// (camera_system.cpp:105-110) and subtracts an Eden-tuned diagonal offset.
void follow(Engine& e, const S::Vitals& v) {
    e.get_camera_system().set_position(v.x, v.y, v.z);
}

// pixels_per_unit to fit a subject of the given half-extents. W,H are the
// RENDER buffer, not the window: the camera viewport is set from
// render_width_/render_height_ (engine.cpp:306), ~2x the window on retina.
float frame_ppu(Engine& e, float hx, float hy, float hz, float margin = 0.85f) {
    const float W = (float)e.get_render_buffer().width();
    const float H = (float)e.get_render_buffer().height();
    const float sx = 0.866f * (hx + hy);
    const float sy = 0.5f   * (hx + hy) + hz;
    return margin * std::min(W / (2.0f * sx), H / (2.0f * sy));
}

// ---------------------------------------------------------------- light
// lux = strength / (4*pi*d^2), hard cutoff at emission_radius
// (shadow_rays_deferred.metal:1112,1121). The zone tone-map wants ~100 lux for
// an RGB-200 midtone (apply_lighting_deferred.metal:132-153) and Lambert eats
// half, so aim at 300 lux: strength ~= 4000 * d^2, radius >= 1.25 * d.
// NEVER gate this on `interactive` — the capture path needs light too
// (test_rube_goldberg_machine.cpp:438-443: with lights behind the flag, the
// headless dump was a black frame with a tiny unlit machine).
void light_the_scene(Engine& e, float cx, float cy, float cz, float extent) {
    auto& ps = e.get_particle_system();
    const float d = extent * 2.0f + 4.0f;             // how far the lights sit
    const float s = 4000.0f * d * d;
    ps.queue_light(cx - d * 0.5f, cy - d * 0.7f, cz + d * 0.6f, s, d * 1.6f,
                   1.0f, 0.96f, 0.90f);
    ps.queue_light(cx + d * 0.6f, cy + d * 0.4f, cz + d * 0.5f, s * 0.6f, d * 1.6f,
                   0.85f, 0.90f, 1.00f);
    ps.flush_pending_particles();
    // Lights are QUEUED particles (particle_system.cpp:697-715). Flush them and
    // let a few frames run, or the viewer's first frames render unlit
    // (test_knockback_scene.cpp:391-393).
    for (int i = 0; i < 6; ++i) e.update(S::SIM_DT);
}

// ------------------------------------------------------------- readout
// Registered WIDGETS, never ui->draw_text(): present() clears the overlay
// plane and re-renders only the debug overlay and registered widgets
// (engine.cpp:1857 -> 1659-1673), so immediate-mode text from a test is
// erased before it reaches the screen. Proven by
// tests/test_ui_label_actually_renders.cpp.
struct Readout {
    ui::Label* title = nullptr;   // what this test claims
    ui::Label* vitals = nullptr;  // the numbers, moving
    ui::Label* verdict = nullptr; // PASS/FAIL, green or red
    ui::Label* perf = nullptr;    // FPS and frame cost
    ui::Label* keys = nullptr;    // the controls

    void build(Engine& e, const char* claim) {
        auto* uis = e.get_ui_system();
        if (!uis) return;
        auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
            auto* L = new ui::Label("", "");
            // add_widget does NOT take ownership (ui_system.cpp:2196-2203);
            // these live for the process. Do not free them while registered.
            L->set_position(24, y); L->set_size(1600, 22); L->set_color(r, g, b);
            uis->add_widget(L);
            return L;
        };
        title   = mk(300, 255, 255, 255);
        vitals  = mk(330, 120, 220, 255);
        verdict = mk(360, 255, 255, 255);
        perf    = mk(390, 180, 180, 180);
        keys    = mk(420, 140, 140, 140);
        if (title) title->set_text(claim);
        if (keys)  keys->set_text("ESC quit   SPACE next stage   ` debug overlay");
    }

    void update(Engine& e, const S::Vitals& v, const S::Verdict& d) {
        char buf[256];
        if (vitals) {
            snprintf(buf, sizeof buf, "z %6.3f m   |v| %5.2f m/s   spin %6.4f rad/s",
                     v.z, v.speed, v.spin);
            vitals->set_text(buf);
        }
        if (verdict) {
            verdict->set_text(d.line);
            if (d.ok) verdict->set_color(110, 235, 130);
            else      verdict->set_color(255, 90, 90);
        }
        if (perf) {
            const auto& m = e.get_metrics();
            snprintf(buf, sizeof buf, "%5.1f FPS   frame %5.2f ms   render %5.2f ms",
                     m.current_fps, m.total_frame_time, m.render_time);
            perf->set_text(buf);
        }
    }
};

// -------------------------------------------------------------- capture
// An all-black frame (no light) and an all-white frame (light in the lens)
// both score ~0 here — that is the point. test_rotation_ladder.cpp:74-107.
long structured_pixels(Engine& e, const char* ppm_path) {
    e.get_renderer().wait_for_completion();
    int w = e.get_render_buffer().width();
    int h = e.get_render_buffer().height();
    std::vector<uint32_t> px((size_t)w * h, 0u);
    if (!e.read_latest_framebuffer(px.data(), w, h)) {
        printf("  [frame] readback FAILED\n");
        return -1;
    }
    long black = 0, white = 0, structured = 0;
    for (uint32_t c : px) {
        const int sum = ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF);
        if (sum <= 60) black++; else if (sum >= 720) white++; else structured++;
    }
    printf("  [frame] %dx%d black=%ld white=%ld structured=%ld\n",
           w, h, black, white, structured);
    if (ppm_path) {
        if (FILE* f = fopen(ppm_path, "wb")) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (uint32_t c : px) {
                unsigned char rgb[3] = { (unsigned char)((c >> 16) & 0xFF),
                                         (unsigned char)((c >>  8) & 0xFF),
                                         (unsigned char)( c        & 0xFF) };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            printf("  [frame] dumped %s\n", ppm_path);
        }
    }
    return structured;
}

// ----------------------------------------------------------------- loop
// update() simulates, render() paints, present() puts the paint on the window.
// All three every frame, or the viewer sees an unpainted (white) surface while
// the framebuffer is perfect (test_rotation_ladder.cpp:109-115).
void step(Engine& e) {
    const auto t0 = std::chrono::steady_clock::now();
    e.update(S::SIM_DT);
    e.render();
    if (g_interactive) {
        e.present();
        std::this_thread::sleep_until(
            t0 + std::chrono::microseconds((long)(S::SIM_DT * 1e6)));
    } else {
        e.get_renderer().wait_for_completion();   // deterministic capture
    }
}

// Freeze the world (dt = 0 still pumps the window, because the event poll is
// inside update(), engine.cpp:1127) until SPACE or ESC.
bool wait_for_space(Engine& e) {
    if (!g_interactive) return true;
    while (alive(e)) {
        if (space_pressed(e)) return true;
        e.update(0.0);
        e.render();
        e.present();
    }
    return false;
}

}  // namespace

int main() {
    g_interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== falling cube: %s ===\n",
           g_interactive ? "INTERACTIVE (ESC quits, SPACE advances)"
                         : "HEADLESS CAPTURE");

    EngineConfig cfg;
    cfg.create_display     = g_interactive;
    cfg.window_width       = 1600;
    cfg.window_height      = 1200;
    cfg.window_title       = "falling cube: nothing is touching it";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = g_interactive;   // (f): the engine's own FPS block
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return 1; }

    // THE SCENE — the same call the CI half makes. Nothing restated.
    const S::Ids ids = S::build(engine.get_particle_system(),
                                engine.get_physics_system());

    light_the_scene(engine, 0.0f, 0.0f, 2.0f, /*extent=*/4.0f);

    auto& cam = engine.get_camera_system();
    cam.set_pixels_per_unit(frame_ppu(engine, S::HALF * 6, S::HALF * 6, S::HALF * 6));
    cam.set_camera_deadzone(0.0f);
    follow(engine, S::measure(engine.get_particle_system(), ids));

    Readout hud;
    if (g_interactive)
        hud.build(engine, "A CUBE FALLS AND SPINS. Nothing touches it. "
                          "PASS = it keeps its spin and never leaves the world.");

    if (!wait_for_space(engine)) { engine.shutdown(); return 0; }

    S::Verdict final_verdict{};
    for (int f = 0; f < S::FRAMES; ++f) {
        if (g_interactive && !alive(engine)) break;

        const S::Vitals v = S::measure(engine.get_particle_system(), ids);
        follow(engine, v);                        // gravity is -Z: keep it framed
        final_verdict = S::judge(v);
        if (g_interactive) hud.update(engine, v, final_verdict);

        step(engine);

        // Frame-chain tripwire: a window that renders but never presents shows
        // white (test_rube_goldberg_machine.cpp:893-897).
        if (g_interactive && f == 60 && engine.presents_completed() == 0) {
            printf("  RED: window shown but nothing presented (frame chain broken)\n");
            engine.shutdown();
            return 1;
        }
    }

    // Headless capture: prove the frame is LIT AND STRUCTURED, then dump it.
    bool pixels_ok = true;
    if (!g_interactive) {
        const long n = structured_pixels(engine, "/tmp/falling_cube.ppm");
        pixels_ok = (n > 0) &&
                    (n > (long)(engine.get_render_buffer().width() *
                                engine.get_render_buffer().height()) / 200);
        printf("  %s: the frame is lit and structured\n", pixels_ok ? "PASS" : "FAIL");
    }

    printf("  %s\n", final_verdict.line);

    // Interactive: hold the final state so the wreck can be inspected.
    if (g_interactive) {
        printf("  (window holds the final state — ESC or close it to end)\n");
        while (alive(engine)) step(engine);
    }

    engine.shutdown();
    return (final_verdict.ok && pixels_ok) ? 0 : 1;
}
```

### 4. CMake

```cmake
# Headless half: physics profile, and REGISTERED so ctest actually runs it.
# An unregistered test_ target in the physics profile is built and never run
# (test_angular_dissipation is exactly that today).
add_executable(test_falling_cube tests/test_falling_cube.cpp)
target_link_libraries(test_falling_cube PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)
target_include_directories(test_falling_cube PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_falling_cube PRIVATE cxx_std_17)
register_headless_test(test_falling_cube)

# Visual half: full profile only. Capture by default, window under INTERACTIVE=1.
# NOT registered with ctest even in capture mode unless you are sure the GPU is
# available on the runner; glfwInit on a CI box deadlocks (CMakeLists.txt:611-615).
if(LOGOSPHERE_FULL)
    add_engine_at(test_falling_cube_visual)   # CMakeLists.txt:590-606
endif()
```

---

## The backlog: which interactive tests are broken today

Mechanical audit of all 62 files matching `INTERACTIVE|LOGOSPHERE_VISUAL`
in `tests/`, against the seven criteria. Counts from grep, causes from
reading the code.

**FPS on screen: 61 of 62 fail.** Only
`test_multi_light_progressive.cpp:139` sets `show_debug_overlay`. No test
anywhere reads `get_metrics().current_fps`.

**ESC: 17 tests that open a window have no ESC handling at all** (no
`GLFW_KEY_ESCAPE`, no `should_close`, no `should_continue`) and must be
killed from another terminal — `test_rube_goldberg_machine` (the owner's
declared acceptance vehicle), `test_angular_dissipation`,
`test_totem_gluon_nails`, `test_hunter_rotation`, `test_gluon_tree`,
`test_gluon_tree_v34`, `test_physics_tree`, `test_running`,
`test_eva_movement`, `test_experiment_totem_builder`,
`test_humanoid_impact`, `test_physics_experiment_01`,
`test_physics_experiment_03_eva_constraints`, `test_physics_rock`,
`test_spirit_light_artifacts`, `test_physics_profile`,
`test_dynamics_override`. (`test_gi_bounce` also lacks ESC, but it never
reads `INTERACTIVE` and never calls `present()` — its usage comment
advertises a windowed mode it does not have. That is its own defect.)

**Window close button: effectively all of them.** Only
`test_knockback_scene.cpp:255` and `test_spine_fk_lookat.cpp:227` read
`should_close()`. Everything else loops on `is_running()`, which never sees it.

**On-screen readout, exact census of the 62:**

| route | count | verdict |
|---|---|---|
| registered widget (`ui::Label` / `TextWindow`) | **12** | visible |
| immediate-mode `ui->draw_text` **only** | **35** | **erased — nobody has ever seen it** |
| nothing at all | **15** | — |

The 12 that work: `test_angular_dissipation`, `test_blockers_visual`,
`test_divergence_microscope`, `test_grass_natures`, `test_grass_yields`,
`test_immovable_pair_phantom_impulse`, `test_knockback_scene`,
`test_predator_hunt`, `test_rotation_ladder`, `test_single_blade_contact`,
`test_tree_repair_visual`, `test_walk_through_grass`.

Of the 35 erased, `test_ssgi_visual` is the painful one — its per-assertion
PASS/FAIL list (`:537-544`) is the best-designed readout in the tree and it
has never reached a screen. `test_rube_goldberg_machine` is in the 15 with
nothing at all.

Only 2 files anywhere use `glfwSetWindowTitle`, the other route
`docs/VISUAL_TESTS.md` recommends.

**Under-lit or unlit:** `test_totem_gluon_nails` (`:759`, 100 000 from
`(0,-10,15)` to a totem near the origin, d ≈ 16 m → 32 lux, 10 % of the
rule), `test_eva_movement` (`:390`, 80 000 from `(0,-8,8)`, d ≈ 11 m →
56 lux, 18 % of the rule),
`test_angular_dissipation_visual` (well lit at spawn, then the subject
falls out of `emission_radius`). ~20 tests create no light of any kind and
rely on whatever the scene generator provides.

**Never framed:** `test_angular_dissipation_visual` — subject 6600 px above
screen centre, computed in §b3 item 4.

**Worst-offender ranking** (most criteria failed, most likely to waste the
owner's time):

1. `test_angular_dissipation_visual` — nothing on screen, ESC dead, no
   assertion in the visual half, headless half measures different physics.
   **This is the window that prompted the request.**
2. `test_totem_gluon_nails` — under-lit, ESC dead, readout erased.
3. `test_rube_goldberg_machine` — good light, good camera, good stage
   assertions to **stdout**; but ESC dead, close button dead, nothing on
   screen, no FPS. The acceptance vehicle needs the readout it never got.
4. `test_ssgi_visual` — the readout exists and is well designed; it is on
   the wrong API.
5. `test_multi_light_progressive` — FPS computed and shown through the
   erased path; the debug overlay saves it by accident.

**Best-in-tree, to copy from:**

| Aspect | File | Lines |
|---|---|---|
| paced step + ESC from any phase | `test_rotation_ladder.cpp` | 111-121 |
| SPACE stage gate | `test_rotation_ladder.cpp` | 126-141 |
| lit-and-structured frame proof | `test_rotation_ladder.cpp` | 74-107 |
| multi-line Label factory | `test_single_blade_contact.cpp` | 227-238 |
| title/live/verdict line roles | `test_walk_through_grass.cpp` | 168-190, 202-220 |
| boxed panel readout | `test_knockback_scene.cpp` | 325-345, 361-372 |
| window focus + ESC + close | `test_knockback_scene.cpp` | 236-257 |
| brightness assertion | `test_knockback_scene.cpp` | 400-430 |
| `should_close()` in the loop | `test_spine_fk_lookat.cpp` | 227 |
| light rig for a long scene | `test_rube_goldberg_machine.cpp` | 438-456 |
| direct camera centring on a moving subject | `test_rube_goldberg_machine.cpp` | 876-883 |
| staged assertions with measured strings | `test_rube_goldberg_machine.cpp` | 523-529, 963-979 |
| watchdogs + frame-chain tripwire | `test_rube_goldberg_machine.cpp` | 893-897, 927-961 |
| headless capture + PPM | `test_shadow_edge_quantization.cpp` | 85-116 |
| the widget-renders proof | `test_ui_label_actually_renders.cpp` | whole file |
| engine FPS overlay | `test_multi_light_progressive.cpp` | 139 |

---

## Corrections owed to `docs/VISUAL_TESTS.md`

Three of its claims no longer match the code:

1. **"`Engine::present()` calls `draw_ui_overlays()`"** — true windowed
   (`engine.cpp:1857`), but headless it runs inside `render()`
   (`engine.cpp:1612`). The doc's skeleton draws the readout before
   `present()`, which only works windowed.
2. **"`poll_events()` REQUIRED in interactive mode"** — `engine.update()`
   already polls (`engine.cpp:1127`). The extra call is harmless; the
   sentence is stale and sends people looking in the wrong place when input
   misbehaves.
3. **"the widget is owned by the UI system once added"** (line 63) —
   false. `add_widget` stores a raw pointer (`ui_system.cpp:2196-2203`).
   `create_panel`/`create_list_menu` own; `add_widget` does not.

Its skeleton also uses `while (engine.is_running())`, which is the loop
that cannot be closed. That line should become `while (alive(engine))`.

## Demos are real in all senses (owner order, 2026-08-28)

No demo subject stands on the bare turtle: the ground is a real body
(slab, tile raft) and only the ground touches the turtle. Materials
are themselves — ice is an ICE particle (density 917), large and
white; color by material, never by role. A dialed property (mu, mass)
gets a real carrier on stage. The rule is load-bearing physics: moving
the limits demos from turtle ground to slab bodies collapsed the
15.6:1 anvil tunnel from 993 mm to 4.6 mm and dissolved the die's
stop anomaly — the turtle interface was part of the finding. Full
text in the logosphere-tests skill.
