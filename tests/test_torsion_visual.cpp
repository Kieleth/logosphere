// =============================================================================
// TORSION RUNGS — the window (G-53, KISS)
// =============================================================================
// Same scene as test_torsion_transmission.cpp: scene_torsion_rungs.h
// owns the rungs and the thresholds. One rung on screen at a time,
// SPACE advances (multi-case law), each rung re-armed via the teleport
// law with a countdown. The spinner is the warm-coloured cube.
//
//   R1 one spinning cube: the spin dies, nothing is created.
//   R2 spinner UNDER a free passenger: the passenger MUST be dragged.
//   R3 spinner ON a carrier: the turtle anchor WINS.
//   R4 three cubes, middle spins (G-56): the 49 kN bracket.
//   R5 four cubes, box1 spins (G-56): the 73.5 kN grind interface.
//
// ESC or the red X quits. SPACE advances the rung. Z zooms.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_torsion_rungs.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace scene_torsion_rungs;

namespace {

constexpr int   COUNTDOWN_FRAMES = 60;
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

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280;
    cfg.window_height = 800;
    cfg.window_title = "G-53: torsion, one rung at a time";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    // One scene per rung, spread so they never meet; the camera shows
    // the active rung only.
    Scene rungs[N_RUNGS];
    int nb[N_RUNGS];
    const float RUNG_X[N_RUNGS] = { -16.0f, -8.0f, 0.0f, +8.0f, +16.0f };
    for (int r = 0; r < N_RUNGS; ++r) {
        nb[r] = rungs[r].build_rung(ps, r, RUNG_X[r]);
        make_lamps(ps, RUNG_X[r], 0.0f, 1.5f);
    }

    float ppu = 110.0f;                      // two cubes fill the frame
    cam.set_pixels_per_unit(ppu);

    const int PANEL_X = 620;
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);
    // MODE-AWARE line (logosphere-tests skill, owner order 2026-08-21):
    // the lever world and the default world demonstrate different
    // things and the window must say which one is on stage.
    auto* l_world = add_line(engine, "world", PANEL_X, 84, 255, 190, 110);
    const bool lv_twist = std::getenv("FRICTION_TWIST") != nullptr;
    const bool lv_learn = std::getenv("WARM_LEARN") != nullptr;
    const bool lv_span  = std::getenv("MANIFOLD_SPAN") != nullptr;
    const bool trio = lv_twist && lv_learn && lv_span;
    std::string world_text;
    if (trio) {
        world_text = "WORLD: all 3 levers ON (the cure) - every line "
                     "must end [V] green";
        l_world->set_color(120, 230, 140);
    } else if (!lv_twist && !lv_learn && !lv_span) {
        world_text = "WORLD: DEFAULT (no levers) - the booked reds stay "
                     "[X] on purpose";
    } else {
        world_text = std::string("WORLD: partial levers (") +
                     (lv_twist ? " twist" : "") + (lv_learn ? " learn" : "") +
                     (lv_span ? " span" : "") +
                     " ) - some lines stay [X] by design";
    }
    l_world->set_text(world_text);

    struct LiveAssert { std::string text; std::function<bool()> eval; };
    constexpr int PANEL_ROWS = 8;
    std::vector<ui::Label*> rows;
    for (int i = 0; i < PANEL_ROWS; ++i)
        rows.push_back(add_line(engine, "assert" + std::to_string(i),
                                PANEL_X, 118 + i * 22, 255, 120, 120));
    auto* l_verdict = add_line(engine, "verdict",
                               PANEL_X, 118 + PANEL_ROWS * 22 + 10,
                               255, 120, 120);
    auto* l_read1 = add_line(engine, "read1", PANEL_X,
                             cfg.window_height - 96, 255, 190, 110);
    auto* l_read2 = add_line(engine, "read2", PANEL_X,
                             cfg.window_height - 74, 140, 210, 255);
    auto* l_hold  = add_line(engine, "hold", PANEL_X,
                             cfg.window_height - 52, 220, 220, 220);

    // Per-rung latches: signed excursions and the L_z ledger, armed
    // while that rung is active; reset on its rearm.
    const float sgn = OMEGA0 > 0 ? 1.0f : -1.0f;
    std::vector<float> peak_signed_wz;
    float L0 = 0.0f, peak_absL = 0.0f;

    std::vector<LiveAssert> panel;
    int rung = 0;
    auto load_rung = [&](int r) {
        rung = r;
        Scene& s = rungs[r];
        peak_signed_wz.assign(nb[r], -1e9f);
        L0 = s.total_Lz(ps);
        peak_absL = std::fabs(L0);
        panel.clear();
        if (r == 0) {
            l_demo->set_text("DEMONSTRATING: one stone cube, born "
                             "spinning like a top (G-53/G-55).");
            l_demo2->set_text("WATCH: ground friction alone kills the "
                              "spin in ~1/4 s; the cube never drifts.");
        } else if (r == 1) {
            l_demo->set_text("DEMONSTRATING: a FREE cube riding the "
                             "spinner (G-53).");
            l_demo2->set_text("WATCH: nothing holds the top cube, so it "
                              "MUST get dragged around, then both stop.");
        } else if (r == 2) {
            l_demo->set_text("DEMONSTRATING: the spinner rides a cube "
                             "the ground holds (G-53).");
            l_demo2->set_text("WATCH: the bottom cube barely turns - "
                              "the ground's grip must win.");
        } else if (r == 3) {
            l_demo->set_text("DEMONSTRATING: 3-cube tower, the MIDDLE "
                             "cube spins (G-56).");
            l_demo2->set_text("WATCH the heights: at this weight the "
                              "spinner must NOT sink into the cube below.");
        } else {
            l_demo->set_text("DEMONSTRATING: 4-cube tower, 2nd cube "
                             "spins - the grind case (G-56).");
            l_demo2->set_text("WATCH: this spinner used to sink INTO "
                              "the cube below and stay. Heights are "
                              "the verdict.");
        }
        const int sp = SPINNER_OF[r];
        panel.push_back({"G-53: the spinner's spin DIES",
            [&, sp]{ return rungs[rung].box_spin(ps, sp) < SPIN_NOISE_MAX; }});
        panel.push_back({"G-53/INV-3: L_z is never created (peak <= L0)",
            [&]{ return peak_absL <= std::fabs(L0) * LZ_BAND; }});
        panel.push_back({"G-53: the turtle drains the spin (|L| falls)",
            [&]{ return std::fabs(rungs[rung].total_Lz(ps))
                            < std::fabs(L0); }});
        if (r == 1)
            panel.push_back({"G-53: the FREE passenger is dragged, same sign",
                [&]{ return peak_signed_wz[PARTNER_OF[1]] >= PASSENGER_MIN; }});
        if (r == 2)
            panel.push_back({"G-53: the ANCHORED carrier is NOT dragged strongly",
                [&]{ return peak_signed_wz[PARTNER_OF[2]] < CARRIER_MAX; }});
        panel.push_back({r >= 3
                ? "G-56/INV-4: everything stands (the grind assert)"
                : "G-48/INV-4: everything stands at static height",
            [&]{ for (int i = 0; i < nb[rung]; ++i)
                     if (std::fabs(rungs[rung].box_z(ps, i)
                                   - rungs[rung].static_z(i)) >= REST_TOL)
                         return false;
                 return true; }});
        panel.push_back({"INV-3: speeds stay bounded",
            [&]{ for (int i = 0; i < nb[rung]; ++i)
                     if (rungs[rung].argus.peak_speed(rungs[rung].boxes[i])
                             >= SPEED_MAX)
                         return false;
                 return true; }});
        // Taller columns centre higher; rungs 1-3 keep the QA'd frame.
        cam.set_position(RUNG_X[r], 0.0f,
                         r <= 2 ? 1.0f : 0.5f * (float)nb[r] + 0.5f);
    };
    load_rung(0);

    std::printf("\n=== torsion, one rung at a time (%s) ===\n",
                interactive ? "WINDOW" : "headless: rungs in sequence");
    std::printf("  %s\n", world_text.c_str());
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE advances the rung "
                    "(re-armed, countdown).  Z zooms.\n\n");

    auto advance_rung = [&](int next) {
        rungs[next].rearm(ps, physics);
        load_rung(next);
    };

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;
    int headless_done = 0, headless_fails = 0;
    char buf[224];

    while (interactive ? (!quit && engine.should_continue())
                       : (headless_done < N_RUNGS)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (frame >= 0 && frame < RUN_FRAMES) {
            ps.update_bvh();
            physics.update(DT);
            for (int r = 0; r < N_RUNGS; ++r)
                rungs[r].argus.observe(ps, frame);
            for (int i = 0; i < nb[rung]; ++i)
                peak_signed_wz[i] = std::fmax(
                    peak_signed_wz[i], sgn * rungs[rung].box_omega_z(ps, i));
            peak_absL = std::fmax(peak_absL,
                                  std::fabs(rungs[rung].total_Lz(ps)));
        }

        const int sp = SPINNER_OF[rung];
        std::snprintf(buf, sizeof(buf), "spinner wz %.3f",
                      rungs[rung].box_omega_z(ps, sp));
        l_read1->set_text(buf);
        if (PARTNER_OF[rung] >= 0)
            std::snprintf(buf, sizeof(buf),
                          "partner wz %.3f  peak %.3f   L_z %.1f of %.1f",
                          rungs[rung].box_omega_z(ps, PARTNER_OF[rung]),
                          peak_signed_wz[PARTNER_OF[rung]] < -1e8f
                              ? 0.0f : peak_signed_wz[PARTNER_OF[rung]],
                          rungs[rung].total_Lz(ps), L0);
        else
            std::snprintf(buf, sizeof(buf), "L_z %.1f of %.1f",
                          rungs[rung].total_Lz(ps), L0);
        l_read2->set_text(buf);
        if (frame < 0)
            std::snprintf(buf, sizeof(buf), "%s starts in %d...",
                          RUNG_NAMES[rung], (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof(buf), "%s  frame %d / %d%s",
                          RUNG_NAMES[rung],
                          frame < RUN_FRAMES ? frame : RUN_FRAMES, RUN_FRAMES,
                          frame >= RUN_FRAMES ? "  (done — SPACE advances)"
                                              : "");
        l_hold->set_text(buf);

        int passing = 0;
        for (int i = 0; i < PANEL_ROWS; ++i) {
            if (i >= (int)panel.size()) { rows[i]->set_text(""); continue; }
            const bool ok = panel[i].eval();
            if (ok) ++passing;
            rows[i]->set_text((ok ? "[V] " : "[X] ") + panel[i].text);
            if (ok) rows[i]->set_color(120, 230, 140);
            else    rows[i]->set_color(255, 120, 120);
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
            // its own accumulator; keys are read straight from GLFW.
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down =
                    glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    advance_rung((rung + 1) % N_RUNGS);
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
        } else if (frame >= RUN_FRAMES) {
            int red = 0;
            for (auto& a : panel) if (!a.eval()) ++red;
            std::printf("  [%s] %d red of %zu\n",
                        RUNG_NAMES[rung], red, panel.size());
            for (auto& a : panel)
                std::printf("    %s %s\n", a.eval() ? "[V]" : "[X]",
                            a.text.c_str());
            headless_fails += red;
            ++headless_done;
            if (headless_done < N_RUNGS) {
                advance_rung(rung + 1);
                frame = -COUNTDOWN_FRAMES;
            } else {
                break;
            }
        }
        ++frame;
    }

    std::printf("\n  %s (%d red)\n",
                interactive
                    ? "window closed (verdict lives on the panel)"
                    : headless_fails == 0
                        ? "EVERY RUNG ANSWERS ITS LAW"
                        : "RED where informative (G-53): booked, not hidden",
                headless_fails);
    engine.shutdown();
    return interactive ? 0 : (headless_fails == 0 ? 0 : 1);
}
