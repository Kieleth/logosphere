// =============================================================================
// THE CREATION DOOR — the window (INV-37).
// Same scene, same evaluator as test_creation_door.cpp: the live panel
// re-reads scene_door::evaluate() every frame, so what the owner sees is
// what the headless driver asserts. Seven cases on one stage, spaced along
// X; each case is born beat by beat (one birth every BIRTH_GAP frames) so
// the eye sees the door answer: the admitted twin appears, the refused one
// never does and its line turns [V] with the depth in millimetres.
//   ESC / red X quit. SPACE advances the case (a replay through the
//   teleport law: standing bodies return to their birth pose, refused births
//   are attempted again). Z zooms. FPS via the debug overlay.
// INTERACTIVE=1 opens the window; otherwise the cases run in sequence and
// print the same verdicts. CREATION_DOOR=0 shows the pre-door world (red by
// design, named on the WORLD line); the door is read once per process, so
// the two worlds are two runs.
// =============================================================================
#include "core/engine.h"
#include "scenes/scene_creation_door.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace scene_door;

namespace {
constexpr int   COUNTDOWN_FRAMES = 60;
constexpr float LIGHT_D = 8.0f;
constexpr float CASE_SPACING = 16.0f;

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

void colour(ui::Label* l, const Verdict& vd) {
    if (vd.pending)  l->set_color(170, 170, 170);
    else if (vd.ok)  l->set_color(120, 230, 140);
    else             l->set_color(255, 120, 120);
}
}  // namespace

int main() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    EngineConfig cfg;
    cfg.window_width = 1400;
    cfg.window_height = 820;
    cfg.window_title = "THE CREATION DOOR: nothing is born inside anything (INV-37)";
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
    std::vector<bool> lit(n_cases, false);

    // Every verdict gets a row: size the panel from the evaluator itself.
    size_t max_rows = 0;
    for (const Case& c : cs) {
        Scene probe;
        probe.arm(ps, c, 0.0f);
        max_rows = std::max(max_rows, evaluate(ps, physics, probe, c).size());
    }

    float ppu = 110.0f;
    cam.set_pixels_per_unit(ppu);

    const int PANEL_X = 600;
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);
    auto* l_world = add_line(engine, "world", PANEL_X, 84, 120, 230, 140);
    l_world->set_text(world_line());
    if (!logosphere::creation_door_enabled()) l_world->set_color(255, 120, 120);
    // THE READINGS, once, on a private world (the same functions the
    // headless driver prints).
    const Reading readings[2] = {cost_reading(), turtle_reading()};
    auto* l_read1 = add_line(engine, "reading1", PANEL_X, 106, 200, 200, 140);
    auto* l_read2 = add_line(engine, "reading2", PANEL_X, 128, 200, 200, 140);
    for (int i = 0; i < 2; ++i) {
        ui::Label* l = i == 0 ? l_read1 : l_read2;
        l->set_text(std::string(readings[i].skipped ? "[SKIP] " : readings[i].ok ? "[V] " : "[X] ") +
                    readings[i].text);
        if (!readings[i].skipped) l->set_color(readings[i].ok ? 120 : 255, readings[i].ok ? 230 : 120, 120);
    }
    const int ROWS_Y = 162;
    std::vector<ui::Label*> rows;
    for (size_t i = 0; i < max_rows; ++i)
        rows.push_back(add_line(engine, "assert" + std::to_string(i),
                                PANEL_X, ROWS_Y + (int)i * 22, 255, 120, 120));
    auto* l_verdict = add_line(engine, "verdict", PANEL_X,
                               ROWS_Y + (int)max_rows * 22 + 10, 255, 120, 120);
    auto* l_waiver = add_line(engine, "waiver", PANEL_X,
                              ROWS_Y + (int)max_rows * 22 + 32, 200, 200, 140);
    auto* l_read = add_line(engine, "read", PANEL_X, cfg.window_height - 96, 255, 190, 110);
    auto* l_hold = add_line(engine, "hold", PANEL_X, cfg.window_height - 74, 220, 220, 220);
    auto* l_hint = add_line(engine, "hint", PANEL_X, cfg.window_height - 52, 220, 220, 220);
    l_hint->set_text(interactive ? "ESC quits.  SPACE advances the case (replay through the teleport law).  "
                                   "Z zooms.  ` toggles the overlay."
                                 : "");

    int ci = 0;
    auto activate = [&](int i) {
        ci = i;
        const Case& c = cs[i];
        if (!lit[i]) { make_lamps(ps, case_x(i), 0.0f, 0.8f); lit[i] = true; }
        scenes[i].arm(ps, c, case_x(i));
        l_demo->set_text(c.demo1);
        l_demo2->set_text(c.demo2);
        l_waiver->set_text(c.waiver ? std::string("[WAIVED] ") + c.waiver : "");
        float top = 0.0f;
        for (const Birth& k : c.births) top = std::fmax(top, k.body.z + k.body.sz * 0.5f);
        cam.set_position(case_x(i), 0.0f, top * 0.5f + 0.2f);
    };
    activate(0);

    std::printf("\n=== the creation door, one case at a time (%s) ===\n",
                interactive ? "WINDOW" : "headless: cases in sequence");
    std::printf("  %s\n", world_line());
    for (const Reading& r : readings)
        std::printf("  %s %s\n", r.skipped ? "[SKIP]" : r.ok ? "[V]" : "[X]", r.text.c_str());
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE advances the case (replay).  Z zooms.\n\n");

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;
    int headless_done = 0, headless_fails = 0;
    char buf[300];
    std::string last_panel;
    while (interactive ? (!quit && engine.should_continue())
                       : (headless_done < n_cases)) {
        const auto t0 = std::chrono::steady_clock::now();
        const Case& c = cs[ci];
        scenes[ci].step(ps, physics, c, frame);   // SHARED step: the beats fall at their frames

        const Scene& s = scenes[ci];
        l_read->set_text(readout(ps, s, c));
        if (frame < 0)
            std::snprintf(buf, sizeof buf, "[%d/%d] %s starts in %d...",
                          ci + 1, n_cases, c.name, (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof buf, "[%d/%d] %s  frame %d / %d%s",
                          ci + 1, n_cases, c.name,
                          frame < c.run_frames ? frame : c.run_frames, c.run_frames,
                          frame >= c.run_frames ? "  (done - SPACE advances)" : "");
        l_hold->set_text(buf);

        // THE LIVE ASSERT PANEL: evaluate() is the one source.
        const std::vector<Verdict> vs = evaluate(ps, physics, s, c);
        int passing = 0, pending = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i >= vs.size()) { rows[i]->set_text(""); continue; }
            if (vs[i].ok) ++passing;
            if (vs[i].pending) ++pending;
            rows[i]->set_text((vs[i].pending ? "[ ] " : vs[i].ok ? "[V] " : "[X] ") + vs[i].text);
            colour(rows[i], vs[i]);
        }
        std::snprintf(buf, sizeof buf, "ASSERTS %d/%zu passing, all %zu shown%s%s", passing, vs.size(),
                      vs.size(), pending ? "  (grey rows await their beat)" : "",
                      !pending && passing < (int)vs.size() ? "  (sleep lines go green late)" : "");
        l_verdict->set_text(buf);
        l_verdict->set_color(passing == (int)vs.size() ? 120 : 255,
                             passing == (int)vs.size() ? 230 : 120, 120);
        // The window's log is evidence: the panel's count line goes to the
        // tee'd log every time it changes, so a verdict is never read from a
        // closing line (owner, 2026-10-06).
        if (interactive && last_panel != buf) {
            last_panel = buf;
            std::printf("  [panel f%d] %s: %s\n", frame, c.name, buf);
            std::fflush(stdout);
        }

        engine.render();
        if (interactive) {
            engine.present();
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    activate((ci + 1) % n_cases);
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
                activate(ci + 1);
                frame = -COUNTDOWN_FRAMES;
            } else {
                break;
            }
        }
        ++frame;
    }
    for (const Reading& r : readings) if (!r.ok) ++headless_fails;
    std::printf("\n  %s\n",
                interactive ? "window closed (the verdict lives on the panel, not on this line)"
                            : headless_fails == 0 ? "EVERY CASE ANSWERS ITS LAW"
                                                  : "RED (CREATION_DOOR=0 is red by design)");
    if (!interactive) std::printf("  (%d red)\n", headless_fails);
    engine.shutdown();
    // Headless, the exit code is the verdict (the sweep reads it against
    // the audit row); a window's exit is the owner closing it.
    return (!interactive && headless_fails > 0) ? 1 : 0;
}
