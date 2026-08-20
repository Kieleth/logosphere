// =============================================================================
// WHAT SLOWS A SPINNING BODY DOWN? — the window
// =============================================================================
// The visual half of `test_angular_dissipation`. Same scene, same
// stepping, same verdict: both drivers run `scenes/scene_spinning_cube.h`
// and neither owns a body or a threshold.
//
// This file replaces a first attempt that was black, framed 6600 px off
// screen, could not be closed, and stepped different physics from its
// headless twin. Each of those is now a rule in the logosphere-tests
// skill, and each is answered below at the line that answers it.
//
//   ./build/test_angular_dissipation_visual              headless + capture
//   INTERACTIVE=1 ./build/test_angular_dissipation_visual  window
//
// ESC or the red X quits. SPACE moves the camera towards the cube.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_spinning_cube.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace scene_spinning_cube;

namespace {

// (c) LIGHT. lux = strength / (4*pi*d^2) with a HARD ZERO past
// emission_radius, so a light that does not move with a falling body
// stops lighting it entirely. strength ~= 4000*d^2 puts the surface in
// the top of the midtone zone; radius 1.25*d keeps it inside the cutoff.
//
// CREATED ONCE, THEN MOVED. A light is a particle, so calling queue_light
// every frame added two more BODIES per frame: measured, the scene went
// "[CULLING STATS] Particles: 3 -> 123 -> 243" in two seconds.
constexpr float LIGHT_D = 3.0f;

struct Lamps { size_t key = 0, fill = 0; };

Lamps make_lamps(ParticleSystem& ps, float x, float y, float z) {
    Lamps l;
    l.key  = ps.queue_light(x + 1.5f, y - 2.0f, z + 2.0f,
                            4000.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            1.0f, 0.95f, 0.85f);
    l.fill = ps.queue_light(x - 2.0f, y + 1.5f, z - 1.0f,
                            1500.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            0.75f, 0.8f, 1.0f);
    ps.flush_pending_particles();
    return l;
}

void move_lamps(ParticleSystem& ps, const Lamps& l, float x, float y, float z) {
    auto v = ps.lock_particles_for_write();
    v[l.key].x  = x + 1.5f; v[l.key].y  = y - 2.0f; v[l.key].z  = z + 2.0f;
    v[l.fill].x = x - 2.0f; v[l.fill].y = y + 1.5f; v[l.fill].z = z - 1.0f;
}

ui::Label* add_line(Engine& e, int row, uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", "line" + std::to_string(row));
    l->set_position(16, 16 + row * 24);
    l->set_color(r, g, b);
    e.get_ui_system()->add_widget(l);   // does NOT take ownership: keep alive
    return l;
}

}  // namespace

int main() {
    // The last two runs ended without their verdict reaching the log:
    // stdout through a pipe is block-buffered and the buffer died with
    // the process. Criterion (g) says the log is always present.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1100;
    cfg.window_height = 720;
    cfg.window_title = "angular dissipation: nothing is touching this cube";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;   // (f) FPS, one line, free
    if (engine.initialize(cfg) != 0) {
        std::printf("  ERROR: engine init failed\n");
        return 1;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene scene;
    scene.build(ps);
    float lx, ly, lz;
    scene.position(ps, lx, ly, lz);
    const Lamps lamps = make_lamps(ps, lx, ly, lz);

    // (c) ZOOM. Start with the cube a modest part of the frame and let
    //     SPACE bring it closer. adjust_zoom clamps to [5,200], so stay
    //     inside that range: at ppu 150 a 0.4 m cube is 60 logical px
    //     (120 on retina, since the viewport is the RENDER buffer).
    float ppu = 150.0f;
    cam.set_pixels_per_unit(ppu);

    auto* l_spin = add_line(engine, 0, 255, 240, 140);
    auto* l_keep = add_line(engine, 1, 255, 190, 190);
    auto* l_claim = add_line(engine, 2, 190, 220, 255);
    auto* l_verdict = add_line(engine, 3, 255, 120, 120);
    l_claim->set_text("ASSERTS: an isolated body keeps its spin. "
                      "pass = retained > 0.99 after 1.00 s");

    std::printf("\n=== what slows a spinning body down? (%s) ===\n",
                interactive ? "WINDOW" : "headless + capture");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE moves the camera in.\n"
                    "  the cube is re-spun to %.1f rad/s every 3 s.\n\n", SPIN0);

    bool  space_was_down = false;
    bool  quit = false;
    int   frame = 0, run = 1;
    float retained_at_1s = -1.0f;
    char  buf[256];

    // (d) should_continue(), NEVER is_running(): is_running() does not
    // read should_close(), which is the only thing ESC sets.
    while (interactive ? (!quit && engine.should_continue())
                       : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (frame > 0 && frame % (RUN_FRAMES * 3) == 0) {
            scene.spin(ps);
            ++run;
        }

        scene.step(ps, physics);      // (b) THE SHARED STEP

        float x, y, z;
        scene.position(ps, x, y, z);
        const float keep = scene.retained(ps);
        const float t = (frame % RUN_FRAMES) / 60.0f;
        if (frame == RUN_FRAMES - 1 && retained_at_1s < 0.0f)
            retained_at_1s = keep;

        // (c) CAMERA: set_position puts THAT POINT at exact screen centre,
        // so it must be the subject itself. An earlier version offset it
        // by a stand-off distance, which under the iso projection threw
        // the cube 346 px left and 200 px up: "the camera is off from the
        // cube". Closeness is ZOOM, never camera offset.
        cam.set_position(x, y, z);
        move_lamps(ps, lamps, x, y, z);   // (c) the light falls with it

        std::snprintf(buf, sizeof(buf),
                      "spin %.4f rad/s  (started %.1f)   t %.2fs   "
                      "fallen %.1f m   run %d", scene.spin_now(ps), SPIN0,
                      t, START_Z - z, run);
        l_spin->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "retained %.6f   -   no contact, no bond, no torque",
                      keep);
        l_keep->set_text(buf);
        l_verdict->set_text(Scene::passes(keep)
            ? "PASS so far: the body is keeping its angular momentum"
            : "FAIL: something with no physical story is eating the spin");

        engine.render();
        if (interactive) {
            engine.present();
            // `engine.update()` is what normally polls events
            // (engine.cpp:1127) and this loop deliberately does NOT call
            // it: it would step physics on its own accumulator at
            // PHYSICS_TIMESTEP = 1/30 and the two drivers would stop
            // being reflections. So poll here, or ESC never arrives and
            // should_continue() never goes false.
            engine.get_platform()->poll_events();
            // (d) and (e). This loop never calls engine.update(), because
            // that would step physics on its own 1/30 accumulator and the
            // two drivers would stop being reflections. engine.update()
            // is also what dispatches input, so the keys are read straight
            // from GLFW here, which is the route test_knockback_scene:255
            // already proved. Without this the window cannot be quit.
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down && ppu < 195.0f) {
                    ppu *= 1.15f;
                    if (ppu > 195.0f) ppu = 195.0f;
                    cam.set_pixels_per_unit(ppu);
                }
                space_was_down = down;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame;
    }

    if (retained_at_1s < 0.0f) retained_at_1s = scene.retained(ps);
    const bool ok = Scene::passes(retained_at_1s);
    std::printf("  [measure] retained %.6f of its spin after 1.00 s\n",
                retained_at_1s);
    std::printf("  [measure] threshold for PASS: > %.2f\n", KEEP_MIN);
    std::printf("\n  %s\n", ok ? "ANGULAR DISSIPATION IS PHYSICAL"
                               : "ANGULAR DISSIPATION IS A CONSTANT "
                                 "(expected red: this is the before-value)");
    engine.shutdown();
    return ok ? 0 : 1;
}
