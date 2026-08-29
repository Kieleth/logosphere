// =============================================================================
// THE LIMITS CAMPAIGN — the window (G-57..G-62)
// =============================================================================
// Same case tables and evaluator source as the six headless probes:
// scene_limits.h owns everything. One case on screen at a time, SPACE
// advances (teleport-law rearm, countdown), Z zooms, ESC quits.
//
// COMBINED WORLD NOTE: all cases coexist 12 m apart (the camera shows
// one); the headless probes are the instruments of record — a drift
// here that the isolated probes lack is a booked world-composition
// sensitivity (the torsion R3 precedent), not a new law.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_limits.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace scene_limits;

namespace {

constexpr int   COUNTDOWN_FRAMES = 60;
constexpr float LIGHT_D = 8.0f;
constexpr float CASE_SPACING = 12.0f;

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
    set_trio_world();   // BEFORE engine init: the levers latch at first use
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    // The full campaign, in the run order of the headless probes.
    std::vector<Case> cases;
    for (auto& v : {cases_ice(), cases_anvil(), cases_size(),
                    cases_footprint(), cases_slipjoint(), cases_floor()})
        for (const Case& c : v) cases.push_back(c);
    // LIMITS_CASE=<n> runs one case alone (RCA focus / the reflection
    // check against the isolated probes).
    if (const char* only = std::getenv("LIMITS_CASE")) {
        const int k = std::atoi(only);
        if (k >= 0 && k < (int)cases.size())
            cases = {cases[(size_t)k]};
    }
    const int n_cases = (int)cases.size();

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_width = 1280;
    cfg.window_height = 800;
    cfg.window_title = "G-57..62: the limits campaign, one case at a time";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    std::vector<Scene> scenes(n_cases);
    auto case_x = [&](int i) {
        return ((float)i - (float)(n_cases - 1) * 0.5f) * CASE_SPACING;
    };
    for (int i = 0; i < n_cases; ++i) {
        scenes[i].build(ps, cases[i].bodies, case_x(i));
        float top = 0.0f;
        for (const BodySpec& b : cases[i].bodies)
            top = std::fmax(top, b.z + b.sz * 0.5f);
        make_lamps(ps, case_x(i), 0.0f, top * 0.5f + 1.0f);
        // Inactive cases are statues (reflection law: no pre-evolution
        // before a case's own run). rearm() wakes them on their turn.
        if (i != 0) scenes[i].set_frozen(ps, true);
    }

    float ppu = 110.0f;
    cam.set_pixels_per_unit(ppu);

    const int PANEL_X = 620;
    auto* l_demo  = add_line(engine, "demo",  PANEL_X, 40, 190, 220, 255);
    auto* l_demo2 = add_line(engine, "demo2", PANEL_X, 62, 190, 220, 255);
    auto* l_world = add_line(engine, "world", PANEL_X, 84, 120, 230, 140);
    l_world->set_text("WORLD: trio, set by the test (the candidate default) "
                      "- reds here are BOOKED LIMITS");

    struct LiveAssert { std::string text; std::function<bool()> eval; };
    constexpr int PANEL_ROWS = 8;
    std::vector<ui::Label*> rows;
    for (int i = 0; i < PANEL_ROWS; ++i)
        rows.push_back(add_line(engine, "assert" + std::to_string(i),
                                PANEL_X, 118 + i * 22, 255, 120, 120));
    auto* l_verdict = add_line(engine, "verdict",
                               PANEL_X, 118 + PANEL_ROWS * 22 + 10,
                               255, 120, 120);
    auto* l_waiver = add_line(engine, "waiver", PANEL_X,
                              118 + PANEL_ROWS * 22 + 32, 200, 200, 140);
    auto* l_read = add_line(engine, "read", PANEL_X,
                            cfg.window_height - 74, 255, 190, 110);
    auto* l_hold = add_line(engine, "hold", PANEL_X,
                            cfg.window_height - 52, 220, 220, 220);

    // Per-case latches, reset on load; the SAME quantities the
    // headless runner latches.
    std::vector<float> stop_time, peak_abs_wz;
    float L0 = 0.0f, peak_absL = 0.0f, x0_slide = 0.0f;

    std::vector<LiveAssert> panel;
    int ci = 0;
    auto load_case = [&](int i) {
        ci = i;
        const Case& c = cases[i];
        Scene& s = scenes[i];
        const int n = (int)c.bodies.size();
        stop_time.assign(n, -1.0f);
        peak_abs_wz.assign(n, 0.0f);
        L0 = s.total_Lz(ps);
        peak_absL = std::fabs(L0);
        x0_slide = c.slide_body >= 0 ? s.x(ps, c.slide_body) : 0.0f;
        l_demo->set_text(c.demo1);
        l_demo2->set_text(c.demo2);
        l_waiver->set_text(c.waiver ? std::string("[WAIVED] ") + c.waiver
                                    : "");
        panel.clear();
        for (const StopBand& b : c.stop_bands) {
            char t[96];
            std::snprintf(t, sizeof(t),
                          "%s: %s dies in [%.2f, %.2f] s", c.gid,
                          c.bodies[b.body].label, b.lo, b.hi);
            const int body = b.body; const float lo = b.lo, hi = b.hi;
            panel.push_back({t, [&, body, lo, hi] {
                return stop_time[body] >= lo && stop_time[body] <= hi; }});
        }
        panel.push_back({std::string(c.gid) +
                             "/INV-24: every spin is dead at the end",
            [&]{ for (int b = 0; b < (int)cases[ci].bodies.size(); ++b)
                     if (scenes[ci].spin(ps, b) >= SPIN_NOISE) return false;
                 return true; }});
        if (L0 != 0.0f)
            panel.push_back({std::string(c.gid) +
                                 "/INV-3: L_z is never created",
                [&]{ return peak_absL <= std::fabs(L0) * LZ_BAND; }});
        if (!c.still.empty())
            panel.push_back({std::string(c.gid) +
                                 ": the anchored bodies are NOT dragged",
                [&]{ for (int b : cases[ci].still)
                         if (peak_abs_wz[b] >= SPIN_NOISE) return false;
                     return true; }});
        panel.push_back({std::string(c.gid) +
                             "/INV-4: everything stands at static height",
            [&]{ const Case& cc = cases[ci];
                 for (const HeightRef& h : cc.heights)
                     if (std::fabs(scenes[ci].z(ps, h.body) - h.z_static)
                             >= h.tol) return false;
                 return true; }});
        if (c.slide_body >= 0)
            panel.push_back({std::string(c.gid) +
                                 ": the glide travels mu*g's distance",
                [&]{ const Case& cc = cases[ci];
                     const float d = scenes[ci].x(ps, cc.slide_body)
                                     - x0_slide;
                     return d >= cc.slide_lo && d <= cc.slide_hi; }});
        if (c.must_sleep)
            panel.push_back({std::string(c.gid) +
                                 "/INV-24: the world falls ASLEEP",
                [&]{ for (int b = 0; b < (int)cases[ci].bodies.size(); ++b)
                         if (!scenes[ci].asleep(ps, b)) return false;
                     return true; }});
        float top = 0.0f;
        for (const BodySpec& b : c.bodies)
            top = std::fmax(top, b.z + b.sz * 0.5f);
        cam.set_position(case_x(i), 0.0f, top * 0.5f + 0.3f);
    };
    load_case(0);

    std::printf("\n=== the limits campaign, one case at a time (%s) ===\n",
                interactive ? "WINDOW" : "headless: cases in sequence");
    std::printf("  WORLD: trio (set by the test) - reds are booked limits\n");
    if (interactive)
        std::printf("  ESC or the red X quits.  SPACE advances the case "
                    "(re-armed, countdown).  Z zooms.\n\n");

    auto advance_case = [&](int next) {
        scenes[ci].set_frozen(ps, true);   // the finished case becomes a statue
        scenes[next].rearm(ps, physics, case_x(next));  // wakes to DYNAMIC
        load_case(next);
    };

    bool space_was_down = false, z_was_down = false, quit = false;
    int frame = -COUNTDOWN_FRAMES;
    int headless_done = 0, headless_fails = 0;
    char buf[224];

    while (interactive ? (!quit && engine.should_continue())
                       : (headless_done < n_cases)) {
        const auto t0 = std::chrono::steady_clock::now();
        const Case& c = cases[ci];
        const int nb = (int)c.bodies.size();

        if (frame >= 0 && frame < c.run_frames) {
            ps.update_bvh();
            physics.update(DT);
            for (int i = 0; i < n_cases; ++i)
                scenes[i].argus.observe(ps, frame);
            peak_absL = std::fmax(peak_absL,
                                  std::fabs(scenes[ci].total_Lz(ps)));
            for (int b = 0; b < nb; ++b) {
                peak_abs_wz[b] = std::fmax(peak_abs_wz[b],
                                           std::fabs(scenes[ci].wz(ps, b)));
                if (stop_time[b] < 0.0f &&
                    scenes[ci].spin(ps, b) < SPIN_NOISE)
                    stop_time[b] = (float)(frame + 1) * DT;
            }
        }

        const int hot = [&]{ for (int b = 0; b < nb; ++b)
                                 if (c.bodies[b].wz0 != 0.0f) return b;
                             return 0; }();
        if (c.slide_body >= 0)
            std::snprintf(buf, sizeof(buf),
                          "%s wz %.3f  z %.4f  dist %.3f m",
                          c.bodies[hot].label, scenes[ci].wz(ps, hot),
                          scenes[ci].z(ps, hot),
                          scenes[ci].x(ps, c.slide_body) - x0_slide);
        else
            std::snprintf(buf, sizeof(buf), "%s wz %.3f  z %.4f",
                          c.bodies[hot].label, scenes[ci].wz(ps, hot),
                          scenes[ci].z(ps, hot));
        l_read->set_text(buf);
        if (frame < 0)
            std::snprintf(buf, sizeof(buf), "[%d/%d] %s starts in %d...",
                          ci + 1, n_cases, c.name, (-frame + 29) / 30);
        else
            std::snprintf(buf, sizeof(buf), "[%d/%d] %s  frame %d / %d%s",
                          ci + 1, n_cases, c.name,
                          frame < c.run_frames ? frame : c.run_frames,
                          c.run_frames,
                          frame >= c.run_frames
                              ? "  (done — SPACE advances)" : "");
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
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                const bool down =
                    glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
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
            for (auto& a : panel) if (!a.eval()) ++red;
            std::printf("  [%s] %d red of %zu\n", c.name, red, panel.size());
            for (auto& a : panel)
                std::printf("    %s %s\n", a.eval() ? "[V]" : "[X]",
                            a.text.c_str());
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
                interactive
                    ? "window closed (verdict lives on the panel)"
                    : headless_fails == 0
                        ? "EVERY CASE ANSWERS ITS LAW"
                        : "RED where informative: the limit map is the point",
                headless_fails);
    engine.shutdown();
    return 0;
}
