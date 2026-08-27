// =============================================================================
// THE STACK AND THE PILE — the window (G-48, G-53, G-54)
// =============================================================================
// Same scene as test_stack_stands.cpp: tests/scenes/scene_stack_stand.h
// owns the bodies, the layouts and the thresholds; neither driver holds
// any of them. One world, three VIEWS, SPACE advances (owner order:
// multi-case advances; the incoming case is re-armed through the
// teleport law and replays from its countdown):
//
//   view 0  THE STATICS PAIR — the column and the pile stand (G-48)
//   view 1  THE TORSION COLUMN — a spinner inside the column brakes
//           and drags its neighbours; L_z only decays (G-53)
//   view 2  MIXED MASSES — three verdicts: stand, FALL, stand (G-54)
//
// The worlds to compare, all from this one binary:
//   (default)      — the audited red: the frozen cache starves statics
//   WARM_LEARN=1   — G-52: the cache learns; the statics pair stands
//   + MANIFOLD_SPAN=1 TURTLE_PRICED=1 — composable (G-51, G-50)
//
// ESC or the red X quits. SPACE advances the view. Z zooms.
// "Ok to be red if its informative" — reds on the panel are findings.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_stack_stand.h"
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

using namespace scene_stack_stand;

namespace {

constexpr float COLUMN_X  = -2.5f;
constexpr float PILE_X    = +2.5f;
constexpr float TORSION_X = +9.0f;
constexpr float MIXED_X   = -11.0f;
constexpr int   N_VIEWS   = 3;
constexpr int   COUNTDOWN_FRAMES = 60;   // the lecture standard: held start
constexpr float LIGHT_D = 8.0f;

const char* VIEW_NAMES[N_VIEWS] = {
    "THE STATICS PAIR", "THE TORSION COLUMN", "MIXED MASSES" };

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
    cfg.window_title = "G-48/53/54: stacks, torsion, mixed masses";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene col, pile, tors, mixed;
    const int nc = col.build_case(ps, 0, COLUMN_X);
    const int np = pile.build_case(ps, 1, PILE_X);
    const int nt = tors.build_case(ps, 2, TORSION_X);
    const int nm = mixed.build_case(ps, 3, MIXED_X);

    make_lamps(ps, 0.0f, 0.0f, 2.5f);
    make_lamps(ps, TORSION_X, 0.0f, 2.5f);
    make_lamps(ps, MIXED_X, 0.0f, 2.0f);

    float ppu = 78.0f;                       // ~10 m vertical in frame
    cam.set_pixels_per_unit(ppu);

    // The debug overlay owns the ENTIRE left column; the panel and the
    // readout live in the right half (owner QA 2026-08-21).
    const int PANEL_X = 620;

    static const bool torque_on = []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == '\0'); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
    std::string mode = torque_on ? "torque ON" : "torque OFF(kill)";
    if (lever_on("WARM_LEARN"))    mode += " +WARM_LEARN";
    if (lever_on("MANIFOLD_SPAN")) mode += " +MANIFOLD_SPAN";
    if (lever_on("TURTLE_PRICED")) mode += " +TURTLE_PRICED";
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);

    // THE LIVE ASSERT PANEL: a fixed pool of lines; each view fills it
    // with ITS asserts, every one named for its law, evaluated per
    // frame from the SAME scene helpers the headless asserts read.
    struct LiveAssert { std::string text; std::function<bool()> eval; };
    constexpr int PANEL_ROWS = 9;
    std::vector<ui::Label*> rows;
    for (int i = 0; i < PANEL_ROWS; ++i)
        rows.push_back(add_line(engine, "assert" + std::to_string(i),
                                PANEL_X, 96 + i * 22, 255, 120, 120));
    auto* l_verdict = add_line(engine, "verdict",
                               PANEL_X, 96 + PANEL_ROWS * 22 + 10,
                               255, 120, 120);
    auto* l_read1 = add_line(engine, "read1", PANEL_X,
                             cfg.window_height - 96, 255, 190, 110);
    auto* l_read2 = add_line(engine, "read2", PANEL_X,
                             cfg.window_height - 74, 140, 210, 255);
    auto* l_hold  = add_line(engine, "hold", PANEL_X,
                             cfg.window_height - 52, 220, 220, 220);

    auto heights_hold = [&](Scene& s, int n) {
        for (int i = 0; i < n; ++i) {
            if (!s.expects_static(i)) continue;
            if (std::fabs(s.box_z(ps, i) - s.static_z(i)) >= REST_TOL)
                return false;
        }
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

    // G-53 latches (signed excursions and the L_z ledger), armed while
    // the torsion view is active; reset on its rearm.
    const float sgn = TORSION_OMEGA0 > 0 ? 1.0f : -1.0f;
    std::vector<float> peak_signed_wz(nt, -1e9f);
    float L0 = tors.total_Lz(ps);
    float peak_absL = std::fabs(L0);
    auto reset_torsion_latches = [&]{
        for (auto& v : peak_signed_wz) v = -1e9f;
        L0 = tors.total_Lz(ps);
        peak_absL = std::fabs(L0);
    };

    std::vector<LiveAssert> panel;
    int view = 0;
    auto load_view = [&](int v) {
        view = v;
        panel.clear();
        if (v == 0) {
            l_demo->set_text("DEMONSTRATING: statics owes a stack (G-48). "
                             + mode);
            l_demo2->set_text("The column and the pile: no torque exists; "
                              "the solver must invent none.");
            panel.push_back({"G-48/INV-4: the COLUMN stands at its static heights",
                [&]{ return heights_hold(col, nc); }});
            panel.push_back({"G-48/INV-24: the COLUMN's spin settles to noise",
                [&]{ return spins_noise(col, nc); }});
            panel.push_back({"INV-3: the COLUMN's speeds stay bounded",
                [&]{ return speeds_bounded(col, nc); }});
            panel.push_back({"G-48/INV-4: the PILE stands at its static heights",
                [&]{ return heights_hold(pile, np); }});
            panel.push_back({"G-48/INV-24: the PILE's spin settles to noise",
                [&]{ return spins_noise(pile, np); }});
            panel.push_back({"INV-3: the PILE's speeds stay bounded",
                [&]{ return speeds_bounded(pile, np); }});
            cam.set_position(0.0f, 0.0f, 2.5f);
        } else if (v == 1) {
            l_demo->set_text("DEMONSTRATING: torsion walks the column (G-53). "
                             + mode);
            l_demo2->set_text("The 2nd cube is born spinning 3 rad/s: it must "
                              "brake AND drag its neighbours.");
            panel.push_back({"G-53: the spinner's spin DIES under load",
                [&]{ return tors.box_spin(ps, SPINNER_IDX) < SPIN_DEAD_MAX; }});
            panel.push_back({"G-53: torsion TRANSMITS below, same sign",
                [&]{ return peak_signed_wz[SPINNER_IDX - 1]
                                >= TRANSMIT_MIN_BELOW; }});
            panel.push_back({"G-53: torsion TRANSMITS above, same sign",
                [&]{ return peak_signed_wz[SPINNER_IDX + 1]
                                >= TRANSMIT_MIN_ABOVE; }});
            panel.push_back({"G-53/INV-3: L_z is never created (peak <= L0)",
                [&]{ return peak_absL <= std::fabs(L0) * LZ_BAND; }});
            panel.push_back({"G-53: the turtle drains the spin (|L| falls)",
                [&]{ return std::fabs(tors.total_Lz(ps)) < std::fabs(L0); }});
            panel.push_back({"G-48/INV-4: the column STANDS through it",
                [&]{ return heights_hold(tors, nt); }});
            panel.push_back({"INV-3: speeds stay bounded",
                [&]{ return speeds_bounded(tors, nt); }});
            cam.set_position(TORSION_X, 0.0f, 2.5f);
        } else {
            l_demo->set_text("DEMONSTRATING: mixed masses, mixed verdicts "
                             "(G-54). " + mode);
            l_demo2->set_text("Stand, FALL, stand: the overhung cube MUST "
                              "leave its perch; phantom support is the red.");
            panel.push_back({"G-54/INV-4: M1 (big centred on small) STANDS",
                [&]{ return std::fabs(mixed.box_z(ps, 0) - mixed.static_z(0))
                                < REST_TOL &&
                            std::fabs(mixed.box_z(ps, 1) - mixed.static_z(1))
                                < REST_TOL; }});
            panel.push_back({"G-54: M2's overhung big cube DEPARTS its perch",
                [&]{ return mixed.box_z(ps, 3) < M2_DEPART_Z; }});
            panel.push_back({"G-54/INV-4: M3 (stone on wood column) STANDS",
                [&]{ return std::fabs(mixed.box_z(ps, 4) - mixed.static_z(4))
                                < REST_TOL &&
                            std::fabs(mixed.box_z(ps, 5) - mixed.static_z(5))
                                < REST_TOL &&
                            std::fabs(mixed.box_z(ps, 6) - mixed.static_z(6))
                                < REST_TOL; }});
            panel.push_back({"INV-3: speeds stay bounded (5:1 and 8:1 masses)",
                [&]{ return speeds_bounded(mixed, nm); }});
            cam.set_position(MIXED_X, 0.0f, 2.0f);
        }
    };
    load_view(0);

    std::printf("\n=== stacks, torsion, mixed masses (%s) ===\n",
                interactive ? "WINDOW" : "headless: all views in sequence");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE advances the view "
                    "(re-armed, countdown).  Z zooms.\n\n");

    auto rearm_view = [&](int v) {
        if (v == 0) { col.rearm(ps, physics); pile.rearm(ps, physics); }
        else if (v == 1) { tors.rearm(ps, physics); reset_torsion_latches(); }
        else { mixed.rearm(ps, physics); }
    };

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;           // negative = held countdown
    int headless_views_done = 0;
    int headless_fails = 0;
    char buf[224];

    auto advance_view = [&](int next) {
        rearm_view(next);
        load_view(next);
        frame = -COUNTDOWN_FRAMES;
    };

    while (interactive ? (!quit && engine.should_continue())
                       : (headless_views_done < N_VIEWS)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (frame >= 0 && frame < RUN_FRAMES) {
            // ONE world, ONE step: same stepping shape as the headless
            // driver; every witness observes the same tick.
            ps.update_bvh();
            physics.update(DT);
            col.argus.observe(ps, frame);
            pile.argus.observe(ps, frame);
            tors.argus.observe(ps, frame);
            mixed.argus.observe(ps, frame);
            if (view == 1) {
                for (int i = 0; i < nt; ++i)
                    peak_signed_wz[i] = std::fmax(peak_signed_wz[i],
                                                  sgn * tors.box_omega_z(ps, i));
                peak_absL = std::fmax(peak_absL,
                                      std::fabs(tors.total_Lz(ps)));
            }
        }

        // The readout: two lines of the active view's live numbers.
        if (view == 0) {
            float w0 = 0.0f, w1 = 0.0f;
            for (int i = 0; i < nc; ++i)
                w0 = std::fmax(w0, std::fabs(col.box_z(ps, i)
                                             - col.static_z(i)));
            for (int i = 0; i < np; ++i)
                w1 = std::fmax(w1, std::fabs(pile.box_z(ps, i)
                                             - pile.static_z(i)));
            std::snprintf(buf, sizeof(buf),
                          "COLUMN worst height error %.4f m", w0);
            l_read1->set_text(buf);
            std::snprintf(buf, sizeof(buf),
                          "PILE   worst height error %.4f m", w1);
            l_read2->set_text(buf);
        } else if (view == 1) {
            std::snprintf(buf, sizeof(buf),
                          "spinner wz %.3f   below peak %.3f  above peak %.3f",
                          tors.box_omega_z(ps, SPINNER_IDX),
                          peak_signed_wz[SPINNER_IDX - 1] < -1e8f
                              ? 0.0f : peak_signed_wz[SPINNER_IDX - 1],
                          peak_signed_wz[SPINNER_IDX + 1] < -1e8f
                              ? 0.0f : peak_signed_wz[SPINNER_IDX + 1]);
            l_read1->set_text(buf);
            std::snprintf(buf, sizeof(buf),
                          "L_z now %.1f  initial %.1f  peak %.1f",
                          tors.total_Lz(ps), L0, peak_absL);
            l_read2->set_text(buf);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "M2 big z %.3f (departs below %.2f, perch was %.2f)",
                          mixed.box_z(ps, 3), M2_DEPART_Z,
                          M_SMALL + M_BIG * 0.5f);
            l_read1->set_text(buf);
            std::snprintf(buf, sizeof(buf),
                          "M2 small z %.3f (measured, WAIVED: marginal edge)",
                          mixed.box_z(ps, 2));
            l_read2->set_text(buf);
        }
        if (frame < 0)
            std::snprintf(buf, sizeof(buf), "%s starts in %d...",
                          VIEW_NAMES[view], (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof(buf), "%s  frame %d / %d%s",
                          VIEW_NAMES[view],
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
                if (down && !space_was_down)
                    advance_view((view + 1) % N_VIEWS);
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
            // Headless: score this view, then advance like a SPACE press.
            int red = 0;
            for (auto& a : panel) if (!a.eval()) ++red;
            std::printf("  [view %d %s] %d red of %zu\n",
                        view, VIEW_NAMES[view], red, panel.size());
            for (auto& a : panel)
                std::printf("    %s %s\n", a.eval() ? "[V]" : "[X]",
                            a.text.c_str());
            headless_fails += red;
            ++headless_views_done;
            if (headless_views_done < N_VIEWS)
                advance_view(view + 1);
            else
                break;
        }
        ++frame;
    }

    std::printf("\n  %s (%d red)\n",
                headless_fails == 0 && interactive == false
                    ? "ALL VIEWS ANSWER THEIR LAWS"
                    : interactive
                        ? "window closed (verdict lives on the panel)"
                        : "RED where informative (G-48/G-53/G-54): booked, "
                          "not hidden",
                headless_fails);
    engine.shutdown();
    return interactive ? 0 : (headless_fails == 0 ? 0 : 1);
}
