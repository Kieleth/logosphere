// =============================================================================
// THE STACK AND THE PILE — the window (G-48)
// =============================================================================
// Same scene as test_stack_stands.cpp: tests/scenes/scene_stack_stand.h
// owns the bodies, the layouts and the thresholds; neither driver holds
// any of them. The window runs BOTH structures side by side in one
// world (continuous-world pattern): the perfect COLUMN of four on the
// left, the zigzag PILE of five on the right. A stonemason certifies
// both; the law (G-48, serving INV-4/INV-24/INV-3) says they STAND.
//
// The worlds to compare, all from this one binary:
//   (default)                       — the audited red: interfaces leak,
//                                     wobble pumps, the pile can fall
//   WARM_LEARN=1                    — G-52: the warm cache learns the
//                                     true support; both structures
//                                     stand to sub-mm and SLEEP
//   MANIFOLD_SPAN=1 TURTLE_PRICED=1 — G-51 + G-50, composable with the
//                                     above; all-levers world is green
//
// ESC or the red X quits. SPACE replays the experiment (teleport law:
// bodies re-armed at their birth poses, history voided). Z zooms.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_stack_stand.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace scene_stack_stand;

namespace {

constexpr float COLUMN_X = -2.5f;
constexpr float PILE_X   = +2.5f;
constexpr int   COUNTDOWN_FRAMES = 60;   // the lecture standard: held start
constexpr float LIGHT_D = 8.0f;

void make_lamps(ParticleSystem& ps, float cx, float cy, float cz) {
    ps.queue_light(cx + 4.0f, cy - 6.0f, cz + 5.0f,
                   4000.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D,
                   1.0f, 0.95f, 0.85f);
    ps.queue_light(cx - 5.0f, cy + 4.0f, cz + 3.0f,
                   1500.0f * LIGHT_D * LIGHT_D, 1.4f * LIGHT_D,
                   0.7f, 0.8f, 1.0f);
    ps.flush_pending_particles();
}

ui::Label* add_line(Engine& e, const std::string& name, int x, int y,
                    uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", name);
    l->set_position(x, y);
    l->set_color(r, g, b);
    e.get_ui_system()->add_widget(l);   // does NOT take ownership
    return l;
}

bool lever_on(const char* name) { return std::getenv(name) != nullptr; }

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280;
    cfg.window_height = 800;
    cfg.window_title = "G-48: the stack and the pile stand still";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene column, pile;
    const int nc = column.build_case(ps, 0, COLUMN_X);
    const int np = pile.build_case(ps, 1, PILE_X);

    const float cx = 0.0f, cy = 0.0f, cz = 2.5f;
    cam.set_position(cx, cy, cz);
    float ppu = 78.0f;                       // ~10 m vertical in frame
    cam.set_pixels_per_unit(ppu);
    make_lamps(ps, cx, cy, cz);

    // The debug overlay owns the ENTIRE left column; the panel and the
    // readout live in the right half (owner QA 2026-08-21).
    const int PANEL_X = 620;

    // DEMONSTRATING line, mode-aware: the lever world is the physics
    // being claimed, so the QA reads which world this window runs.
    static const bool torque_on = []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == '\0'); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
    const bool learn  = lever_on("WARM_LEARN");
    const bool span   = lever_on("MANIFOLD_SPAN");
    const bool priced = lever_on("TURTLE_PRICED");
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);
    {
        std::string mode = torque_on ? "torque ON" : "torque OFF(kill)";
        if (learn)  mode += " +WARM_LEARN";
        if (span)   mode += " +MANIFOLD_SPAN";
        if (priced) mode += " +TURTLE_PRICED";
        l_demo->set_text("DEMONSTRATING: statics owes a stack (G-48). " + mode);
        l_demo2->set_text(learn
            ? "The cache learns the true support (G-52): both STAND."
            : "The audited world: the frozen cache starves the stack.");
    }

    // THE LIVE ASSERT PANEL: every assert its own [V]/[X] line, per
    // frame, from the SAME scene helpers the headless asserts read.
    struct LiveAssert { ui::Label* label; std::string text;
                        std::function<bool()> eval; };
    std::vector<LiveAssert> panel;
    int prow = 0;
    auto add_assert = [&](const std::string& text,
                          std::function<bool()> eval) {
        auto* l = add_line(engine, "assert" + std::to_string(prow),
                           PANEL_X, 96 + prow * 22, 255, 120, 120);
        panel.push_back({l, text, std::move(eval)});
        ++prow;
    };
    auto heights_hold = [&](Scene& s, int n) {
        for (int i = 0; i < n; ++i)
            if (std::fabs(s.box_z(ps, i) - s.static_z(i)) >= REST_TOL)
                return false;
        return true;
    };
    auto spins_noise = [&](Scene& s, int n) {
        for (int i = 0; i < n; ++i)
            if (s.box_spin(ps, i) >= SPIN_NOISE_MAX) return false;
        return true;
    };
    auto speeds_bounded = [&](Scene& s, int n) {
        for (int i = 0; i < n; ++i)
            if (s.argus.peak_speed(s.boxes[i]) >= SPEED_MAX) return false;
        return true;
    };
    add_assert("G-48/INV-4: the COLUMN stands at its static heights",
        [&]{ return heights_hold(column, nc); });
    add_assert("G-48/INV-24: the COLUMN's spin settles to noise",
        [&]{ return spins_noise(column, nc); });
    add_assert("INV-3: the COLUMN's speeds stay bounded",
        [&]{ return speeds_bounded(column, nc); });
    add_assert("G-48/INV-4: the PILE stands at its static heights",
        [&]{ return heights_hold(pile, np); });
    add_assert("G-48/INV-24: the PILE's spin settles to noise",
        [&]{ return spins_noise(pile, np); });
    add_assert("INV-3: the PILE's speeds stay bounded",
        [&]{ return speeds_bounded(pile, np); });
    auto* l_verdict = add_line(engine, "verdict",
                               PANEL_X, 96 + prow * 22 + 10, 255, 120, 120);

    auto* l_col  = add_line(engine, "col",  PANEL_X,
                            cfg.window_height - 96, 255, 190, 110);
    auto* l_pile = add_line(engine, "pile", PANEL_X,
                            cfg.window_height - 74, 140, 210, 255);
    auto* l_hold = add_line(engine, "hold", PANEL_X,
                            cfg.window_height - 52, 220, 220, 220);

    std::printf("\n=== the stack and the pile, side by side (%s) ===\n",
                interactive ? "WINDOW" : "headless");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE replays.  Z zooms.\n\n");

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;           // negative = held countdown
    char buf[224];

    while (interactive ? (!quit && engine.should_continue())
                       : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (frame >= 0 && frame < RUN_FRAMES) {
            // ONE world, ONE step: same stepping shape as the headless
            // driver (update_bvh + physics.update(DT)); both witnesses
            // observe the same tick.
            ps.update_bvh();
            physics.update(DT);
            column.argus.observe(ps, frame);
            pile.argus.observe(ps, frame);
        }

        const float col_worst = [&]{
            float w = 0.0f;
            for (int i = 0; i < nc; ++i)
                w = std::fmax(w, std::fabs(column.box_z(ps, i)
                                           - column.static_z(i)));
            return w; }();
        const float pile_worst = [&]{
            float w = 0.0f;
            for (int i = 0; i < np; ++i)
                w = std::fmax(w, std::fabs(pile.box_z(ps, i)
                                           - pile.static_z(i)));
            return w; }();
        std::snprintf(buf, sizeof(buf),
                      "COLUMN worst height error %.4f m   top spin %.4f",
                      col_worst, column.box_spin(ps, nc - 1));
        l_col->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "PILE   worst height error %.4f m   top spin %.4f",
                      pile_worst, pile.box_spin(ps, np - 1));
        l_pile->set_text(buf);
        if (frame < 0)
            std::snprintf(buf, sizeof(buf),
                          "starts in %d...", (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof(buf), "frame %d / %d%s",
                          frame < RUN_FRAMES ? frame : RUN_FRAMES, RUN_FRAMES,
                          frame >= RUN_FRAMES ? "  (done — SPACE replays)" : "");
        l_hold->set_text(buf);

        int passing = 0;
        for (auto& a : panel) {
            const bool ok = a.eval();
            if (ok) ++passing;
            a.label->set_text((ok ? "[V] " : "[X] ") + a.text);
            if (ok) a.label->set_color(120, 230, 140);
            else    a.label->set_color(255, 120, 120);
        }
        std::snprintf(buf, sizeof(buf),
                      "ASSERTS %d/%zu passing (settle lines go green late)",
                      passing, panel.size());
        l_verdict->set_text(buf);
        l_verdict->set_color(passing == (int)panel.size() ? 120 : 255,
                             passing == (int)panel.size() ? 230 : 120,
                             120);

        engine.render();
        if (interactive) {
            engine.present();
            // engine.update() is never called: it would step physics on
            // its own accumulator and break parity with the headless
            // driver; keys are read straight from GLFW.
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down =
                    glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    column.rearm(ps, physics);
                    pile.rearm(ps, physics);
                    frame = -COUNTDOWN_FRAMES;
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

    int fails = 0;
    for (auto& a : panel) if (!a.eval()) ++fails;
    for (int i = 0; i < nc; ++i)
        std::printf("  [measure] column box%d z %.4f (static %.1f) spin %.4f\n",
                    i, column.box_z(ps, i), column.static_z(i),
                    column.box_spin(ps, i));
    for (int i = 0; i < np; ++i)
        std::printf("  [measure] pile   box%d z %.4f (static %.1f) spin %.4f\n",
                    i, pile.box_z(ps, i), pile.static_z(i),
                    pile.box_spin(ps, i));
    std::printf("\n  %s (%d red)\n",
                fails == 0 ? "BOTH STRUCTURES STAND"
                           : "RED (G-48): the sustained-contact loop does "
                             "not hold statics in this world",
                fails);
    engine.shutdown();
    return fails == 0 ? 0 : 1;
}
