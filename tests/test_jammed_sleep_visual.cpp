// =============================================================================
// THE JAMMED CLUMP MAY NOT SLEEP — the window (G-67 / G-68 / G-48).
// Same scene, same evaluator as test_jammed_sleep.cpp: the live panel
// re-reads scene_jammed::evaluate() every frame, so what the owner sees
// is what the headless driver asserts. Four cases on one stage, spaced
// along X; the camera centres the active case.
//   ESC / red X quit. SPACE advances the case (re-armed, countdown).
//   Z zooms. FPS via the debug overlay.
// INTERACTIVE=1 opens the window; otherwise the cases run in sequence.
// =============================================================================
#include "core/engine.h"
#include "scenes/scene_jammed_sleep.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace scene_jammed;

namespace {
constexpr int   COUNTDOWN_FRAMES = 60;
constexpr float LIGHT_D = 8.0f;
constexpr float CASE_SPACING = 16.0f;   // B's control tile sits 5 m off its case: 9 m put it on C's tile edge (measured: landed at z 0.300, tilted 0.075)

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
    auto* l = new ui::Label("", name);   // registered widgets live for the engine
    l->set_position(x, y);
    l->set_color(r, g, b);
    e.get_ui_system()->add_widget(l);
    return l;
}
}  // namespace

int main() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    EngineConfig cfg;
    cfg.window_width = 1400;
    cfg.window_height = 820;
    cfg.window_title = "THE JAMMED CLUMP MAY NOT SLEEP (G-67 / G-68 / G-48)";
    cfg.create_display = interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;   // (f) FPS

    Engine engine;
    if (engine.initialize(cfg) != 0) {
        std::printf("engine init failed\n");
        return 1;
    }
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    const std::vector<Case> cs = cases();
    const int n_cases = (int)cs.size();
    auto case_x = [&](int i) { return (float)i * CASE_SPACING; };

    std::vector<Scene> scenes(n_cases);
    for (int i = 0; i < n_cases; ++i) {
        scenes[i].build(ps, physics, cs[i], case_x(i));
        make_lamps(ps, case_x(i), 0.0f, 0.8f);
        if (i != 0) scenes[i].set_frozen(ps, true);   // statues until their turn
    }

    float ppu = 110.0f;
    cam.set_pixels_per_unit(ppu);

    const int PANEL_X = 600;
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);
    auto* l_world = add_line(engine, "world", PANEL_X, 84, 120, 230, 140);
    l_world->set_text("WORLD: DEFAULT (INV-36). Red lines are the frame-collapse "
                      "chain, born red on purpose (TDD)");
    // Every verdict gets a row: the longest case (C) carries seventeen.
    // Ten rows hid seven of them while the count still said n/17 (owner,
    // 2026-09-02: "the asserts counts are wrong").
    constexpr int PANEL_ROWS = 18;
    std::vector<ui::Label*> rows;
    for (int i = 0; i < PANEL_ROWS; ++i)
        rows.push_back(add_line(engine, "assert" + std::to_string(i),
                                PANEL_X, 118 + i * 22, 255, 120, 120));
    auto* l_verdict = add_line(engine, "verdict", PANEL_X,
                               118 + PANEL_ROWS * 22 + 10, 255, 120, 120);
    auto* l_waiver = add_line(engine, "waiver", PANEL_X,
                              118 + PANEL_ROWS * 22 + 32, 200, 200, 140);
    auto* l_read = add_line(engine, "read", PANEL_X,
                            cfg.window_height - 74, 255, 190, 110);
    auto* l_hold = add_line(engine, "hold", PANEL_X,
                            cfg.window_height - 52, 220, 220, 220);

    int ci = 0;
    auto load_case = [&](int i) {
        ci = i;
        const Case& c = cs[i];
        l_demo->set_text(c.demo1);
        l_demo2->set_text(c.demo2);
        l_waiver->set_text(c.waiver ? std::string("[WAIVED] ") + c.waiver : "");
        float top = 0.0f;
        for (const BodySpec& b : c.bodies) top = std::fmax(top, b.z + b.sz * 0.5f);
        cam.set_position(case_x(i), 0.0f, top * 0.5f + 0.2f);
    };
    load_case(0);

    std::printf("\n=== the jammed clump, one case at a time (%s) ===\n",
                interactive ? "WINDOW" : "headless: cases in sequence");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE advances the case "
                    "(re-armed, countdown).  Z zooms.\n\n");

    auto advance_case = [&](int next) {
        scenes[ci].set_frozen(ps, true);
        scenes[next].rearm(ps, physics, case_x(next));
        load_case(next);
    };

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;
    int headless_done = 0, headless_fails = 0;
    char buf[224];
    while (interactive ? (!quit && engine.should_continue())
                       : (headless_done < n_cases)) {
        const auto t0 = std::chrono::steady_clock::now();
        const Case& c = cs[ci];
        if (frame >= 0 && frame < c.run_frames)
            scenes[ci].step(ps, physics, c, frame, case_x(ci));   // SHARED step

        // Live readout: the numbers at the moment they matter, from the
        // same latches the evaluator reads (one source).
        const Scene& s = scenes[ci];
        std::snprintf(buf, sizeof buf, "%s", readout(ps, physics, s, c).c_str());
        l_read->set_text(buf);
        if (frame < 0)
            std::snprintf(buf, sizeof buf, "[%d/%d] %s starts in %d...",
                          ci + 1, n_cases, c.name, (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof buf, "[%d/%d] %s  frame %d / %d%s",
                          ci + 1, n_cases, c.name,
                          frame < c.run_frames ? frame : c.run_frames,
                          c.run_frames,
                          frame >= c.run_frames ? "  (done - SPACE advances)" : "");
        l_hold->set_text(buf);

        // THE LIVE ASSERT PANEL: evaluate() is the one source.
        const std::vector<Verdict> vs = evaluate(ps, physics, s, c);
        int passing = 0;
        for (int i = 0; i < PANEL_ROWS; ++i) {
            if (i >= (int)vs.size()) { rows[i]->set_text(""); continue; }
            if (vs[i].ok) ++passing;
            rows[i]->set_text((vs[i].ok ? "[V] " : "[X] ") + vs[i].text);
            if (vs[i].ok) rows[i]->set_color(120, 230, 140);
            else          rows[i]->set_color(255, 120, 120);
        }
        std::snprintf(buf, sizeof buf, "ASSERTS %d/%zu passing, all %zu shown "
                      "(sleep lines go green late)", passing, vs.size(), vs.size());
        l_verdict->set_text(buf);
        l_verdict->set_color(passing == (int)vs.size() ? 120 : 255,
                             passing == (int)vs.size() ? 230 : 120, 120);

        engine.render();
        if (interactive) {
            engine.present();
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    advance_case((ci + 1) % n_cases);
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
        } else if (frame >= c.run_frames) {
            int red = 0;
            for (const Verdict& vd : vs) if (!vd.ok) ++red;
            std::printf("  [%s] %d red of %zu\n", c.name, red, vs.size());
            for (const Verdict& vd : vs)
                std::printf("    %s %s\n", vd.ok ? "[V]" : "[X]", vd.text.c_str());
            headless_fails += red;
            ++headless_done;
            if (headless_done < n_cases) {
                advance_case(ci + 1);
                frame = -COUNTDOWN_FRAMES;
            } else {
                break;
            }
        }
        ++frame;
    }
    std::printf("\n  %s (%d red)\n",
                interactive ? "window closed (verdict lives on the panel)"
                            : headless_fails == 0 ? "EVERY CASE ANSWERS ITS LAW"
                                                  : "BORN RED where the chain is broken",
                headless_fails);
    engine.shutdown();
    // Headless, the exit code is the verdict (the sweep reads it against
    // the audit row); a window's exit is the owner closing it.
    return (!interactive && headless_fails > 0) ? 1 : 0;
}
