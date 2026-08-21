// =============================================================================
// A CUBE AND A SPHERE ON THE SAME RAMP — the window
// =============================================================================
// Board front F2, watchable. Same scene and same stepping as
// test_ramp_race.cpp: tests/scenes/scene_ramp_race.h owns the bodies,
// the ramp and the thresholds, and neither driver holds any of them.
//
// What you are looking at: two identical releases on one 40 degree
// slope. The orange CUBE goes through the 15-axis OBB narrow phase, meets
// the tilted face, and runs the ramp. The blue SPHERE goes through
// narrow_phase_sphere_aabb, whose box side is built by
// aabb_of_box_particle and never reads rotation
// (src/core/narrow_phase.cpp:957-976), so it meets the ramp's UPRIGHT
// BOUNDING SLAB, gets a (0,0,1) normal, and stands on a flat shelf the
// engine invented. It does not move at all.
//
// ESC or the red X quits. SPACE restarts the race. Z zooms.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_ramp_race.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <functional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace scene_ramp_race;

namespace {

// (c) LIGHT. lux = strength/(4*pi*d^2), hard zero past emission_radius.
// The ramp is 8 m, so the rig stands off far enough to cover it and is
// created ONCE (a light is a particle; queueing per frame adds bodies).
constexpr float LIGHT_D = 8.0f;

void make_lamps(ParticleSystem& ps, float cx, float cy, float cz) {
    ps.queue_light(cx + 3.0f, cy - 6.0f, cz + 5.0f,
                   4000.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D,
                   1.0f, 0.95f, 0.85f);
    ps.queue_light(cx - 4.0f, cy + 4.0f, cz + 3.0f,
                   1500.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D,
                   0.7f, 0.8f, 1.0f);
    ps.flush_pending_particles();
}

ui::Label* add_line(Engine& e, int row, uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", "line" + std::to_string(row));
    l->set_position(16, 16 + row * 24);
    l->set_color(r, g, b);
    e.get_ui_system()->add_widget(l);   // does NOT take ownership
    return l;
}

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280;
    cfg.window_height = 800;
    cfg.window_title = "F2: the cube runs the ramp, the sphere stands on air";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;      // (f) FPS
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene scene;
    scene.build(ps);

    // (c) CAMERA: set_position puts THAT POINT at exact screen centre.
    // The ramp is 8 m long, so frame its middle and fit the whole run.
    const float cx = 0.0f, cy = 0.0f, cz = ramp_centre_z();
    cam.set_position(cx, cy, cz);
    float ppu = 55.0f;                          // ~8 m across the frame
    cam.set_pixels_per_unit(ppu);
    make_lamps(ps, cx, cy, cz);

    // Live readout at the BOTTOM (the debug overlay owns the top left).
    const int base_y = cfg.window_height - 96;
    auto* l_cube = add_line(engine, 0, 255, 190, 110);
    auto* l_ball = add_line(engine, 1, 140, 210, 255);
    auto* l_spin  = add_line(engine, 2, 220, 220, 220);
    l_cube->set_position(16, base_y);
    l_ball->set_position(16, base_y + 22);
    l_spin->set_position(16, base_y + 44);

    // THE LIVE ASSERT PANEL (owner order 2026-08-21): what this test
    // demonstrates, and EVERY assert as its own [V]/[X] line, evaluated
    // each frame from the SAME scene helpers the headless asserts read.
    static const bool lever_ui = std::getenv("CONTACT_TORQUE") != nullptr;
    auto* l_demo = add_line(engine, 3, 190, 220, 255);
    l_demo->set_position(16, 118);
    l_demo->set_text(lever_ui
        ? "DEMONSTRATING: contact torque (CONTACT_TORQUE=1). A cube and a "
          "sphere race the same 40 deg ramp: both must move, both must "
          "TURN, the sphere must ROLL, and every run stays in its lane."
        : "DEMONSTRATING: the default no-torque world. Both bodies slide; "
          "the TURN asserts are born red until the torque law is default.");
    struct LiveAssert { ui::Label* label; std::string text;
                        std::function<bool()> eval; };
    std::vector<LiveAssert> panel;
    int prow = 0;
    auto add_assert = [&](const std::string& text,
                          std::function<bool()> eval) {
        auto* l = new ui::Label("", "assert" + std::to_string(prow));
        l->set_position(16, 148 + prow * 22);
        engine.get_ui_system()->add_widget(l);
        panel.push_back({l, text, std::move(eval)});
        ++prow;
    };
    add_assert("the ramp never moved (fixed datum)",
        [&]{ return Scene::held(scene.ramp_drift); });
    if (lever_ui) {
        add_assert("LEVER: the sphere ROLLS (spin > 2.0 rad/s)",
            [&]{ return scene.ball_spin_peak > ROLL_MIN_LEVER; });
        add_assert("LEVER: rolling within 10% of sliding (travel)",
            [&]{ return scene.ball_travel(ps) >
                        scene.cube_travel(ps) * 0.9f; });
    }
    add_assert("the cube slides down the slope (> 0.30 m)",
        [&]{ return Scene::travelled(scene.cube_travel(ps)); });
    add_assert("the cube TURNS as it goes",
        [&]{ return Scene::turned(scene.cube_spin_peak); });
    add_assert("the sphere ALSO moves (real oriented contact, INV-12)",
        [&]{ return Scene::travelled(scene.ball_travel(ps)); });
    add_assert("the sphere TURNS TOO (friction acts a radius out)",
        [&]{ return Scene::turned(scene.ball_spin_peak); });
    add_assert("neither leaves its lane",
        [&]{ return Scene::in_lane(scene.cube_lane_dev) &&
                    Scene::in_lane(scene.ball_lane_dev); });
    add_assert("the racers never touch",
        [&]{ return Scene::lanes_kept(scene.lane_gap_min); });
    add_assert("the cube ends AT REST ON THE TURTLE",
        [&]{ return Scene::landed_and_stopped(scene.bottom(scene.cube),
                                              scene.speed(scene.cube)); });
    add_assert("the sphere ends AT REST ON THE TURTLE",
        [&]{ return Scene::landed_and_stopped(scene.bottom(scene.ball),
                                              scene.speed(scene.ball)); });
    add_assert("one body one orientation (two-band coherence, G-23)",
        [&]{ return Scene::coherent(
                        scene.argus.peak_divergence(scene.cube, false),
                        scene.argus.peak_divergence(scene.cube, true)) &&
                    Scene::coherent(
                        scene.argus.peak_divergence(scene.ball, false),
                        scene.argus.peak_divergence(scene.ball, true)); });
    auto* l_verdict = add_line(engine, 4, 255, 120, 120);
    l_verdict->set_position(16, 148 + prow * 22 + 8);

    std::printf("\n=== a cube and a sphere on the same %.0f degree ramp (%s) ===\n",
                SLOPE_DEG, interactive ? "WINDOW" : "headless");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE restarts the race.  "
                    "Z zooms in.\n\n");

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = 0;
    char buf[224];

    while (interactive ? (!quit && engine.should_continue())
                       : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (frame < RUN_FRAMES) scene.step(ps, physics);   // (b) SHARED

        const float cd = scene.cube_travel(ps);
        const float bd = scene.ball_travel(ps);
        std::snprintf(buf, sizeof(buf),
                      "CUBE   travelled %.3f m   (OBB path: meets the tilted face)",
                      cd);
        l_cube->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "SPHERE travelled %.3f m   (sphere-vs-AABB: meets an "
                      "upright slab, normal (0,0,1))", bd);
        l_ball->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "peak |omega|  cube %.4f   sphere %.4f rad/s   "
                      "(D2 1.2: contact rows carry no lever arm)",
                      scene.cube_spin_peak, scene.ball_spin_peak);
        l_spin->set_text(buf);
        // The panel: every assert, live, [V]/[X], same helpers as headless.
        int passing = 0;
        for (auto& a : panel) {
            const bool ok = a.eval();
            if (ok) ++passing;
            a.label->set_text((ok ? "[V] " : "[X] ") + a.text);
            if (ok) a.label->set_color(120, 230, 140);
            else    a.label->set_color(255, 120, 120);
        }
        std::snprintf(buf, sizeof(buf),
                      "ASSERTS %d/%zu passing (live; end-state lines settle "
                      "as the run settles)", passing, panel.size());
        l_verdict->set_text(buf);
        l_verdict->set_color(passing == (int)panel.size() ? 120 : 255,
                             passing == (int)panel.size() ? 230 : 120,
                             120);

        engine.render();
        if (interactive) {
            engine.present();
            // engine.update() is never called here: it would step physics
            // on its own 1/30 accumulator and break parity with the
            // headless driver. It is also what dispatches input, so the
            // keys are read straight from GLFW.
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                // SPACE restarts the race (owner order 2026-08-21:
                // the experiment replays on demand); zoom lives on Z.
                const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    scene.rearm(ps, physics);
                    frame = 0;
                }
                space_was_down = down;
                const bool zk = glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS;
                if (zk && !z_was_down && ppu < 195.0f) {
                    ppu *= 1.15f;
                    if (ppu > 195.0f) ppu = 195.0f;
                    cam.set_pixels_per_unit(ppu);
                }
                z_was_down = zk;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame;
    }

    const float cd = scene.cube_travel(ps), bd = scene.ball_travel(ps);
    const bool ok = Scene::travelled(cd) && Scene::travelled(bd)
                 && Scene::turned(scene.cube_spin_peak)
                 && Scene::turned(scene.ball_spin_peak);
    std::printf("  [measure] cube   travelled %.3f m downhill\n", cd);
    std::printf("  [measure] sphere travelled %.3f m downhill\n", bd);
    std::printf("  [measure] peak |omega|: cube %.4f, sphere %.4f rad/s\n",
                scene.cube_spin_peak, scene.ball_spin_peak);
    // Name only the fronts that are ACTUALLY failing. A verdict that
    // lists a front already fixed is the same class of lie as a comment
    // that outlived its code.
    std::string verdict;
    if (!Scene::travelled(cd) || !Scene::travelled(bd))
        verdict += "F2 (a body does not feel the slope)";
    if (!Scene::turned(scene.cube_spin_peak) || !Scene::turned(scene.ball_spin_peak)) {
        if (!verdict.empty()) verdict += " + ";
        verdict += "D2 1.2 (contacts carry no torque, so nothing turns)";
    }
    std::printf("\n  %s\n", ok ? "BOTH FEEL THE SLOPE AND BOTH TURN"
                               : ("RED: " + verdict).c_str());
    engine.shutdown();
    return ok ? 0 : 1;
}
