// =============================================================================
// A BODY MOVED FROM OUTSIDE CARRIES ITS MOTION (INV-39) - the window
// =============================================================================
// Same scene, same stepping as test_writers_motion.cpp. Left station: a
// cube on a slab an external writer slides. Right station: an arm nailed
// under a post the writer turns. Under INV-39 the cube rides and the arm
// turns. Born red: the solver reads both writers' motion as zero.
// ESC or the red X quits. SPACE restarts both stations. Z zooms.
// =============================================================================
#include "core/engine.h"
#include "scenes/scene_writers_motion.h"
#include "../src/ui/widgets.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <functional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
using namespace scene_writers_motion;
namespace {
constexpr float LIGHT_D = 7.0f;
void make_lamps(ParticleSystem& ps, float cx, float cy, float cz) {
    ps.queue_light(cx + 3.0f, cy - 6.0f, cz + 5.0f, 4000.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D, 1.0f, 0.95f, 0.85f);
    ps.queue_light(cx - 4.0f, cy + 4.0f, cz + 3.0f, 1500.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D, 0.7f, 0.8f, 1.0f);
    ps.flush_pending_particles();
}
ui::Label* add_line(Engine& e, int row, uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", "line" + std::to_string(row));
    l->set_position(16, 16 + row * 24);
    l->set_color(r, g, b);
    e.get_ui_system()->add_widget(l);
    return l;
}
}
int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280; cfg.window_height = 800;
    cfg.window_title = "INV-39: the slab slides, the post turns - does the solver know?";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();
    Scene scene;
    scene.build(ps, physics);
    const float cx = 0.0f, cy = 0.0f, cz = 0.8f;
    cam.set_position(cx, cy, cz);
    float ppu = 70.0f;
    cam.set_pixels_per_unit(ppu);
    make_lamps(ps, cx, cy, cz);
    const int PANEL_X = 620;
    const int base_y = cfg.window_height - 96;
    auto* l_a = add_line(engine, 0, 255, 190, 110);
    auto* l_b = add_line(engine, 1, 140, 210, 255);
    l_a->set_position(PANEL_X, base_y);
    l_b->set_position(PANEL_X, base_y + 22);
    auto* l_demo = add_line(engine, 3, 190, 220, 255);
    l_demo->set_position(PANEL_X, 40);
    l_demo->set_text("DEMONSTRATING INV-39: a body moved from outside carries its motion.");
    auto* l_demo2 = add_line(engine, 5, 190, 220, 255);
    l_demo2->set_position(PANEL_X, 62);
    l_demo2->set_text(std::getenv("KINEMATIC_LEDGER") ? "Lever ON: the writers' motion is in the ledger." : "Default: the ledger reads zero; born red.");
    struct LiveAssert { ui::Label* label; std::string text; std::function<bool()> eval; };
    std::vector<LiveAssert> panel; int prow = 0;
    auto add_assert = [&](const std::string& text, std::function<bool()> eval) {
        auto* l = new ui::Label("", "assert" + std::to_string(prow));
        l->set_position(PANEL_X, 96 + prow * 22);
        engine.get_ui_system()->add_widget(l);
        panel.push_back({l, text, std::move(eval)}); ++prow;
    };
    add_assert("INV-39/G-77: the cube RIDES the slab (slip < 10 SLOP)", [&]{ return Scene::rides(scene.slip_max); });
    add_assert("INV-2/G-77: the cube stays SEATED on the slab", [&]{ return Scene::seated(scene.seat_gap_min, scene.seat_gap_max); });
    add_assert("INV-39/G-77: steady: cube velocity = slab velocity", [&]{ return scene.steady_frames > 0 && Scene::carried(scene.cube_speed_err_max); });
    add_assert("INV-39/G-78: the arm TURNS WITH the post (0.5 rad/s)", [&]{ return scene.steady_frames > 0 && Scene::turns_with(scene.spin_min_steady, scene.spin_max_steady); });
    add_assert("INV-39/G-78: the arm's yaw follows the post's", [&]{ return Scene::aligned(scene.yaw_err_max); });
    add_assert("INV-22/G-78: the nail stays rigid (separation constant)", [&]{ return Scene::rigid(scene.sep_drift_max); });
    add_assert("G-21/G-23: one orientation per body", [&]{ return Scene::coherent(scene.argus.peak_divergence(scene.cube, false), scene.argus.peak_divergence(scene.cube, true)) && Scene::coherent(scene.argus.peak_divergence(scene.arm, false), scene.argus.peak_divergence(scene.arm, true)); });
    auto* l_verdict = add_line(engine, 4, 255, 120, 120);
    l_verdict->set_position(PANEL_X, 96 + prow * 22 + 10);
    std::printf("\n=== INV-39: the writers' motion (%s) ===\n", interactive ? "WINDOW" : "headless");
    if (interactive) std::printf("  ESC or the red X quits.  SPACE restarts.  Z zooms in.\n\n");
    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = 0; char buf[224];
    while (interactive ? (!quit && engine.should_continue()) : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();
        if (frame < RUN_FRAMES) scene.step(ps, physics, frame);
        std::snprintf(buf, sizeof(buf), "SLAB v %.2f m/s   cube slip %.4f m   seat [%+.4f,%+.4f]", scene.slab_v, scene.slip_max, scene.seat_gap_min, scene.seat_gap_max);
        l_a->set_text(buf);
        std::snprintf(buf, sizeof(buf), "POST w %.2f rad/s   arm spin %.3f   yaw err %.3f   sep drift %.5f", POST_OMEGA, scene.argus.spin(scene.arm), scene.yaw_err_max, scene.sep_drift_max);
        l_b->set_text(buf);
        int passing = 0;
        for (auto& a : panel) {
            const bool ok = a.eval(); if (ok) ++passing;
            a.label->set_text((ok ? "[V] " : "[X] ") + a.text);
            if (ok) a.label->set_color(120, 230, 140); else a.label->set_color(255, 120, 120);
        }
        std::snprintf(buf, sizeof(buf), "ASSERTS %d/%zu passing", passing, panel.size());
        l_verdict->set_text(buf);
        l_verdict->set_color(passing == (int)panel.size() ? 120 : 255, passing == (int)panel.size() ? 230 : 120, 120);
        engine.render();
        if (interactive) {
            engine.present();
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) { scene.rearm(ps, physics); frame = 0; }
                space_was_down = down;
                const bool zk = glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS;
                if (zk && !z_was_down && ppu < 195.0f) { ppu *= 1.15f; if (ppu > 195.0f) ppu = 195.0f; cam.set_pixels_per_unit(ppu); }
                z_was_down = zk;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame;
    }
    const bool ok = Scene::rides(scene.slip_max) && Scene::turns_with(scene.spin_min_steady, scene.spin_max_steady);
    std::printf("  [measure] slip max %.4f m; arm spin steady [%.3f, %.3f] rad/s\n", scene.slip_max, scene.spin_min_steady, scene.spin_max_steady);
    std::printf("\n  %s\n", ok ? "THE SOLVER READS THE WRITERS' MOTION" : "RED: INV-39");
    engine.shutdown();
    return ok ? 0 : 1;
}
