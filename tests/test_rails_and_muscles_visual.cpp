// =============================================================================
// RAILS AND MUSCLES (INV-40) - the window
// =============================================================================
// Same scene, same stepping as test_rails_and_muscles.cpp. Left: Eva walks
// north on her two rails and twenty muscles. Right: three bodies in the air,
// one DYNAMIC with the muscles' flags, one KINEMATIC, one driven on a nail.
// Born red: hands still on the muscles, the flagged box hovers, no jump is
// declared. The run is RUN_FRAMES long and then holds on its verdict. ESC
// or the red X quits. SPACE replays: Eva returns to the start through the
// teleport door, the boxes re-drop, every count restarts. Z zooms.
// =============================================================================
#include "core/engine.h"
#include "scenes/scene_rails_and_muscles.h"
#include "../src/ui/widgets.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <functional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
using namespace scene_rails_and_muscles;
namespace {
constexpr float LIGHT_D = 8.0f;
int lamp_a = -1, lamp_b = -1;
void make_lamps(ParticleSystem& ps, float cx, float cy, float cz) {
    lamp_a = ps.queue_light(cx + 3.0f, cy - 7.0f, cz + 6.0f, 4000.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D, 1.0f, 0.95f, 0.85f);
    lamp_b = ps.queue_light(cx - 5.0f, cy + 5.0f, cz + 4.0f, 1500.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D, 0.7f, 0.8f, 1.0f);
    ps.flush_pending_particles();
}
void move_lamps(ParticleSystem& ps, float cx, float cy, float cz) {
    auto v = ps.lock_particles_for_write();
    if (lamp_a >= 0 && (size_t)lamp_a < v.size()) { v[lamp_a].x = cx + 3.0f; v[lamp_a].y = cy - 7.0f; v[lamp_a].z = cz + 6.0f; }
    if (lamp_b >= 0 && (size_t)lamp_b < v.size()) { v[lamp_b].x = cx - 5.0f; v[lamp_b].y = cy + 5.0f; v[lamp_b].z = cz + 4.0f; }
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
    const bool ledger = std::getenv("KINEMATIC_LEDGER") != nullptr;
    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280; cfg.window_height = 800;
    cfg.window_title = "INV-40: two rails, twenty muscles - whose hands are on the limbs?";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }
    auto& ps = engine.get_particle_system();
    auto& cam = engine.get_camera_system();
    Scene scene;
    scene.build(engine);
    auto centre = [&](float& cx, float& cy, float& cz) {
        auto v = ps.lock_particles_for_read();
        cx = v[scene.hips].x + B_X * 0.5f; cy = v[scene.hips].y; cz = 1.0f;
    };
    float cx, cy, cz; centre(cx, cy, cz);
    cam.set_position(cx, cy, cz);
    float ppu = 80.0f;
    cam.set_pixels_per_unit(ppu);
    make_lamps(ps, cx, cy, cz);
    // Streaming swaps particle indices; the lamps were born last, so they
    // are the first to be moved. Without this they stay behind and she
    // walks out of their light.
    ps.add_swap_callback([](size_t o, size_t n) { if (lamp_a == (int)o) lamp_a = (int)n; if (lamp_b == (int)o) lamp_b = (int)n; });
    const int PANEL_X = 560;
    const int base_y = cfg.window_height - 120;
    auto* l_a = add_line(engine, 0, 255, 190, 110);
    auto* l_b = add_line(engine, 1, 140, 210, 255);
    auto* l_c = add_line(engine, 2, 200, 200, 200);
    l_a->set_position(PANEL_X, base_y);
    l_b->set_position(PANEL_X, base_y + 22);
    l_c->set_position(PANEL_X, base_y + 44);
    auto* l_demo = add_line(engine, 3, 190, 220, 255);
    l_demo->set_position(PANEL_X, 40);
    l_demo->set_text("DEMONSTRATING INV-40: animation is a rail or a muscle; nobody else moves a physics body.");
    auto* l_demo2 = add_line(engine, 5, 190, 220, 255);
    l_demo2->set_position(PANEL_X, 62);
    {
        std::string mode = std::string("INV40_STEP=") + std::to_string(Scene::inv40_step()) + (ledger ? ", KINEMATIC_LEDGER=1" : ", default ledger")
                         + (std::getenv("GLUON_OFFSETS_CW") ? ", GLUON_OFFSETS_CW=1" : ", offsets anticlockwise (legacy)");
        const int st = Scene::inv40_step();
        mode += st >= 4 ? ": no hand on any muscle, the muscles weigh, the harness reads the ground."
              : st >= 3 ? ": no hand on any muscle; the harness reads the ground."
              : st >= 2 ? ": the shape pass keeps its hands off the muscles."
              :           ": today's hands are on every muscle every frame.";
        l_demo2->set_text(mode);
    }
    struct LiveAssert { ui::Label* label; std::string text; std::function<bool()> eval; };
    std::vector<LiveAssert> panel; int prow = 0;
    auto add_assert = [&](const std::string& text, std::function<bool()> eval) {
        auto* l = new ui::Label("", "assert" + std::to_string(prow));
        l->set_position(PANEL_X, 96 + prow * 22);
        engine.get_ui_system()->add_widget(l);
        panel.push_back({l, text, std::move(eval)}); ++prow;
    };
    add_assert("hygiene/INV-35: the drive children are live bodies",         [&]{ return Scene::muscles_live(scene.muscles_stale); });
    add_assert("INV-40/INV-35/G-81: no hand but the solver's on any muscle",  [&]{ return Scene::hands_off(scene.hand_records); });
    add_assert("G-81 gauge: the limbs follow the rail and she walks",          [&]{ return Scene::walks(scene.forward, scene.walk_frames); });
    add_assert("INV-28/INV-22: every nail holds its two attachment points (gap <= 10 SLOP)", [&]{ return Scene::holds_together(scene.joint_gap_max); });
    add_assert("G-81/INV-22: every bone within its standing reach of the hips", [&]{ return Scene::whole(scene.reach_over_max); });
    add_assert("INV-40/INV-15/G-82: the flagged DYNAMIC box FALLS",           [&]{ return Scene::fell(scene.a_drop_max); });
    add_assert("INV-1/G-82: the rail stays where its writer prescribes",      [&]{ return Scene::stayed(scene.b_drift_max); });
    add_assert("INV-13/G-82: the driven arm holds its pose on its nail",      [&]{ return Scene::holds(scene.arm_err_max, scene.arm_sep_drift_max); });
    add_assert("INV-40/G-83: every replant is DECLARED (rail.jump)",           [&]{ return Scene::declared(scene.replants, scene.replants_declared); });
    add_assert(std::string("INV-39/G-83: no replant read as a velocity") + (ledger ? "" : " [vacuous by default]"),
               [&]{ return Scene::ledger_quiet(scene.replants, scene.replants_ledger_loud); });
    auto* l_verdict = add_line(engine, 4, 255, 120, 120);
    l_verdict->set_position(PANEL_X, 96 + prow * 22 + 10);
    std::printf("\n=== INV-40: rails and muscles (%s) ===\n", interactive ? "WINDOW" : "headless");
    if (interactive) std::printf("  ESC or the red X quits.  SPACE replays the run (Eva back to the start through the teleport door).  Z zooms in.\n\n");
    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = 0; char buf[256];
    while (interactive ? (!quit && engine.should_continue()) : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();
        const bool live = frame < RUN_FRAMES;
        if (live) scene.step(engine, frame);           // the run holds on its verdict at RUN_FRAMES
        centre(cx, cy, cz);
        cam.set_position(cx, cy, cz);
        move_lamps(ps, cx, cy, cz);
        std::snprintf(buf, sizeof(buf), "MUSCLES %zu (%d stale)  HANDS: %d records, %d frames  (%s)", scene.muscles.size(), scene.muscles_stale, scene.hand_records, scene.frames_with_hands, scene.hands_summary(2).c_str());
        l_a->set_text(buf);
        std::snprintf(buf, sizeof(buf), "WALK fwd %.2f m back %d | REPLANTS %d declared %d loud %d (ledger %.1f m/s)",
                      scene.forward, scene.backward_frames, scene.replants, scene.replants_declared, scene.replants_ledger_loud, scene.replant_ledger_speed_max);
        l_b->set_text(buf);
        std::snprintf(buf, sizeof(buf), "BOX A drop %.2f m   BOX B drift %.4f   ARM err %.3f rad sep drift %.4f | BODY nail gap %.4f (%s) reach over %+.3f (%s)",
                      scene.a_drop_max, scene.b_drift_max, scene.arm_err_max, scene.arm_sep_drift_max,
                      scene.joint_gap_max, scene.joint_gap_worst.c_str(), scene.reach_over_max, scene.reach_worst.c_str());
        l_c->set_text(buf);
        int passing = 0;
        for (auto& a : panel) {
            const bool ok = a.eval(); if (ok) ++passing;
            a.label->set_text((ok ? "[V] " : "[X] ") + a.text);
            if (ok) a.label->set_color(120, 230, 140); else a.label->set_color(255, 120, 120);
        }
        std::snprintf(buf, sizeof(buf), live ? "ASSERTS %d/%zu passing  (frame %d of %d)" : "ASSERTS %d/%zu passing  RUN COMPLETE (%d of %d frames) - SPACE replays", passing, panel.size(), frame, RUN_FRAMES);
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
                if (down && !space_was_down) { scene.rearm(engine); frame = 0; }
                space_was_down = down;
                const bool zk = glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS;
                if (zk && !z_was_down && ppu < 200.0f) { ppu *= 1.15f; if (ppu > 200.0f) ppu = 200.0f; cam.set_pixels_per_unit(ppu); }
                z_was_down = zk;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        if (live) ++frame;
    }
    int passing = 0;
    for (auto& a : panel) if (a.eval()) ++passing;
    std::printf("  [measure] hands %d records; walk %.3f m; replants %d/%d declared; A drop %.3f; arm err %.4f\n",
                scene.hand_records, scene.forward, scene.replants_declared, scene.replants, scene.a_drop_max, scene.arm_err_max);
    std::printf("\n  %s (%d/%zu)\n", passing == (int)panel.size() ? "TWO RAILS AND TWENTY MUSCLES" : "RED: INV-40", passing, panel.size());
    engine.shutdown();
    return passing == (int)panel.size() ? 0 : 1;
}
