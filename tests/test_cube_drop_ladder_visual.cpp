// =============================================================================
// THE CUBE DROP LADDER — the window
// =============================================================================
// Same scene, same stepping, same thresholds as test_cube_drop_ladder:
// tests/scenes/scene_cube_drop.h owns all of it. The window cycles the
// three rungs so each can be watched: the control that settles flat, the
// tilted cube that balances impossibly on its edge, and the spinning
// cube whose spin dies in the air before the floor exists.
//
// ESC or the red X quits. SPACE moves the camera in.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_cube_drop.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <functional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace scene_cube_drop;

namespace {
constexpr float LIGHT_D = 3.0f;
struct Lamps { size_t key = 0, fill = 0; };
Lamps make_lamps(ParticleSystem& ps, float x, float y, float z) {
    Lamps l;
    l.key  = ps.queue_light(x + 1.5f, y - 2.0f, z + 2.0f,
                            4000.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            1.0f, 0.95f, 0.85f);
    l.fill = ps.queue_light(x - 2.0f, y + 1.5f, z + 0.5f,
                            1500.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            0.75f, 0.8f, 1.0f);
    ps.flush_pending_particles();
    return l;
}
void move_lamps(ParticleSystem& ps, const Lamps& l, float x, float y, float z) {
    auto v = ps.lock_particles_for_write();
    v[l.key].x  = x + 1.5f; v[l.key].y  = y - 2.0f; v[l.key].z  = z + 2.0f;
    v[l.fill].x = x - 2.0f; v[l.fill].y = y + 1.5f; v[l.fill].z = z + 0.5f;
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
    cfg.window_width = 1100;
    cfg.window_height = 720;
    cfg.window_title = "cube drop ladder: does anything ever rotate?";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene scene;
    scene.build(ps);
    scene.add_backdrop(ps);   // both modes: criterion b, same bodies
    scene.arm(ps, physics, RUNGS[0], 0);
    // FIXED camera at the action column. The first version FOLLOWED the
    // cube, which put it dead-centre every frame: a falling body whose
    // camera falls with it does not appear to move ("I see a cube static
    // and nothing else"). The whole ladder spans 0.7 m; frame it once.
    const Lamps lamps = make_lamps(ps, 0.0f, 0.0f, 0.6f);
    float ppu = 240.0f;
    cam.set_pixels_per_unit(ppu);
    cam.set_position(0.0f, 0.0f, 0.55f);

    // The debug overlay owns the ENTIRE left column. Everything of
    // ours lives right of PANEL_X (protocol, 2026-08-21).
    const int PANEL_X = 560;
    const int base_y = cfg.window_height - 118;
    auto* l_rung = add_line(engine, 0, 255, 240, 140);
    auto* l_live = add_line(engine, 1, 220, 220, 220);
    auto* l_meas = add_line(engine, 2, 140, 210, 255);
    auto* l_verdict = add_line(engine, 3, 255, 120, 120);
    l_rung->set_position(PANEL_X, base_y);
    l_live->set_position(PANEL_X, base_y + 24);
    l_meas->set_position(PANEL_X, base_y + 48);
    l_verdict->set_position(PANEL_X, base_y + 72);

    // THE LIVE ASSERT PANEL (protocol, 2026-08-21): per-rung assert
    // set, every line [V]/[X] with its registered law, evaluated per
    // frame from the same scene/Argus quantities headless asserts use.
    static const bool lever_ui = std::getenv("CONTACT_TORQUE") != nullptr;
    auto* l_demo = add_line(engine, 4, 190, 220, 255);
    l_demo->set_position(PANEL_X, 40);
    constexpr int MAX_ASSERTS = 6;
    struct LiveAssert { std::string text; std::function<bool()> eval; };
    std::vector<ui::Label*> panel_labels;
    for (int i = 0; i < MAX_ASSERTS; ++i) {
        auto* l = new ui::Label("", "assert" + std::to_string(i));
        l->set_position(PANEL_X, 66 + i * 22);
        engine.get_ui_system()->add_widget(l);
        panel_labels.push_back(l);
    }
    auto* l_count = add_line(engine, 5, 220, 220, 220);
    l_count->set_position(PANEL_X, 66 + MAX_ASSERTS * 22 + 6);
    std::vector<LiveAssert> panel;
    auto rebuild_panel = [&](int r) {
        panel.clear();
        const int A = scene.actor(r);
        auto flat = [&]{ return scene.settled_rot_y(ps) < FLAT_ROT_MAX; };
        auto still_twin = [&]{
            return std::fabs(scene.displaced_x(ps, scene.twin) - 1.5f) < 0.02f &&
                   std::fabs(scene.displaced_y(ps, scene.twin)) < 0.02f; };
        auto coherent = [&, A]{
            return scene.argus.peak_divergence(A, false) < DIV_MAX_SHARP &&
                   scene.argus.peak_divergence(A, true)  < DIV_MAX_FOLD; };
        if (r == 0) {
            l_demo->set_text("DEMONSTRATING: a flat drop invents NO rotation.");
            panel.push_back({"G-35: settles flat (rot_y < 0.05)", flat});
            panel.push_back({lever_ui ? "INV-20: transient wobble bounded (< 0.05)"
                                      : "G-35: invents no rotation (peak < 0.01)",
                [&]{ return scene.peak_omega_y <
                            (lever_ui ? WOBBLE_MAX_LEVER : CONTROL_ROT_MAX); }});
            panel.push_back({"hygiene: rests ON the slab (separation)",
                [&]{ return std::fabs(scene.argus.separation(scene.cube, scene.slab)
                            - (FLOOR_TOP*0.5f + CUBE*0.5f)) < 0.01f; }});
            panel.push_back({"G-21: one orientation (two-band)", coherent});
        } else if (r == 1) {
            l_demo->set_text("DEMONSTRATING: a 20deg tilt must TIP FLAT.");
            panel.push_back({"G-35: settles FLAT (no edge rest)",
                [&]{ return scene.settled_rot_y(ps) <
                            (lever_ui ? TIP_SETTLE_MAX : FLAT_ROT_MAX); }});
            panel.push_back({"G-35: it ROTATED to get there",
                [&]{ return scene.peak_omega_y > TIP_OMEGA_MIN; }});
            panel.push_back({"G-21: the righting is coherent", coherent});
        } else if (r == 2) {
            l_demo->set_text("DEMONSTRATING: spin survives FLIGHT, dies at floor.");
            panel.push_back({"G-36: spin survives flight (keep > 0.90)",
                [&]{ return scene.keep_at_touchdown < 0.0f ||
                            scene.keep_at_touchdown > FLIGHT_KEEP_MIN; }});
            panel.push_back({"G-36: floor friction brakes it to rest",
                [&]{ return scene.settled_spin(ps) < SETTLED_SPIN_MAX; }});
            panel.push_back({"G-37: per-frame retention (> 0.99)",
                [&]{ return scene.min_frame_keep > FRAME_KEEP_MIN; }});
        } else if (r <= 5) {
            l_demo->set_text(r == 3
                ? "DEMONSTRATING: the fast top brakes IN PLACE."
                : "DEMONSTRATING: wheel spin buys travel on ONE axis.");
            panel.push_back({"G-21/23: one orientation (two-band)", coherent});
            panel.push_back({"INV-3: speeds bounded (< 10 m/s)",
                [&, A]{ return scene.argus.peak_speed(A) < 10.0f; }});
            panel.push_back({"hygiene: the still twin STAYS still", still_twin});
            if (lever_ui && r == 3)
                panel.push_back({"G-41: the big top brakes IN PLACE",
                    [&]{ return std::fabs(scene.displaced_x(ps, scene.hero)) < 0.05f &&
                                std::fabs(scene.displaced_y(ps, scene.hero)) < 0.05f &&
                                scene.settled_spin(ps, scene.hero) < 0.1f; }});
            if (lever_ui && r == 4)
                panel.push_back({"G-41 BORN RED: X-spin WALKS along Y",
                    [&]{ return std::fabs(scene.displaced_y(ps, scene.hero)) > 0.05f; }});
            if (lever_ui && r == 5)
                panel.push_back({"G-41 BORN RED: Y-spin WALKS along X",
                    [&]{ return std::fabs(scene.displaced_x(ps, scene.hero)) > 0.05f; }});
        } else {
            l_demo->set_text(r == 6
                ? "DEMONSTRATING: a corner stand MUST fall (slab rows)."
                : "DEMONSTRATING: same corner stand, turtle rows.");
            const float fallen_max = (r == 7) ? HERO * 0.5f + 0.05f
                                              : CORNER_FALLEN_Z_MAX;
            panel.push_back({"G-43: the corner stand FALLS to a face",
                [&, fallen_max]{ return scene.settled_z(ps, scene.hero) < fallen_max; }});
            panel.push_back({"G-43: and it ROTATED on the way down",
                [&]{ return scene.argus.peak_spin(scene.hero) >
                            CORNER_TOPPLE_SPIN_MIN; }});
            panel.push_back({"G-21: one orientation through the fall", coherent});
            panel.push_back({"hygiene: the face-resting twin stays put", still_twin});
        }
    };
    rebuild_panel(0);

    if (interactive)
        std::printf("\n  SPACE = next case   Z = zoom in   ESC = quit\n"
                    "  eight cases: control, tilted 20deg, top 3rad/s, fast top,\n"
                    "  wheel X (drives along Y), wheel Y (drives along X),\n"
                    "  CORNER STAND on the slab, CORNER STAND on the turtle\n\n");

    bool space_was_down = false, z_was_down = false, quit = false;
    bool advance_case = false;
    constexpr int HOLD_FRAMES = 60;   // 1 s of armed stillness per case
    int hold_frames = interactive ? HOLD_FRAMES : 0;
    int frame = 0, rung = 0, rung_frame = 0;
    char buf[256];

    const int total_frames = interactive ? 1 << 30 : RUN_FRAMES * 3;
    while (interactive ? (!quit && engine.should_continue())
                       : (frame < total_frames)) {
        const auto t0 = std::chrono::steady_clock::now();

        // OWNER ORDER 2026-08-20: interactive cases advance on SPACE,
        // never on a timer — the human decides when they have seen
        // enough of a case. Headless keeps the full audited run.
        if (!interactive && rung_frame >= RUN_FRAMES) {
            rung = (rung + 1) % RUNG_COUNT;
            if (rung == 0) break;
            scene.arm(ps, physics, RUNGS[rung], rung);
            rebuild_panel(rung);
            rung_frame = 0;
        }
        if (advance_case) {                      // SPACE, edge-triggered
            advance_case = false;
            rung = (rung + 1) % RUNG_COUNT;
            scene.arm(ps, physics, RUNGS[rung], rung);
            rebuild_panel(rung);
            rung_frame = 0;
            // The stage moves with the case: R8 performs at x = 10 on
            // the bare turtle; a fixed origin camera showed empty space
            // and the lamps' emission radius never reached it.
            const float stage_x = (rung == 7) ? 10.0f : 0.0f;
            cam.set_position(stage_x, 0.0f, 0.55f);
            move_lamps(ps, lamps, stage_x, 0.0f, 0.6f);
            hold_frames = interactive ? HOLD_FRAMES : 0;
        }
        // EVERY case opens with a held frame (interactive only): the
        // engine's warmup outlasts a 0.25 s drop, so an un-held case is
        // over before the first presented frame reaches the eye
        // ("saw nothing"). The pose is armed and frozen; the countdown
        // shows on the readout; then physics runs.
        if (hold_frames > 0) {
            --hold_frames;
        } else {
            scene.step(ps, physics, rung_frame, RUNGS[rung].spin_z);
            ++rung_frame;
        }

        const int actor = scene.actor(rung);
        float x, y, z, oy, oz, ry;
        {   auto v = ps.lock_particles_for_read();
            const Particle& p = v[actor];
            x=p.x; y=p.y; z=p.z; oy=p.omega_y; oz=p.omega_z; ry=p.rotation_y; }
        // Spin lectures: wider stage so hero AND twin are both in frame.
        {   const float want = (rung >= 3) ? 120.0f : 160.0f;
            if (ppu > want + 1.0f || ppu < want - 1.0f) {
                ppu = want; cam.set_pixels_per_unit(ppu);
            }
        }
        (void)x; (void)y;   // camera and lamps are fixed; the cube moves

        std::snprintf(buf, sizeof(buf),
                      hold_frames > 0
                          ? "== CASE %d of %d ==  %s   (starts in %.1fs)"
                          : "== CASE %d of %d ==  %s   (t %.2fs)",
                      rung + 1, RUNG_COUNT, RUNGS[rung].name,
                      hold_frames > 0 ? hold_frames / 60.0f
                                      : rung_frame / 60.0f);
        l_rung->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "rot_y %.3f rad   omega_y %.3f   omega_z %.3f   z %.3f",
                      ry, oy, oz, z);
        l_live->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "peak|omega_y| %.3f   flight keep %.3f   frame keep %.3f",
                      scene.peak_omega_y, scene.keep_at_touchdown,
                      scene.min_frame_keep);
        l_meas->set_text(buf);
        const char* verdict =
            rung >= 6 ? (scene.argus.peak_spin(scene.hero) > CORNER_TOPPLE_SPIN_MIN
                ? "R7/R8 PASS: the corner stand FELL (G-43 dead)"
                : "R7/R8: corner-down, a hair off balance — it MUST fall")
          : rung == 0 ? "R0 control: must invent no rotation"
          : rung == 1 ? (scene.peak_omega_y > TIP_OMEGA_MIN
                ? "R1 PASS: it tipped" 
                : "R1 FAIL (G-35): balanced on an edge, contact torque absent")
          : (scene.keep_at_touchdown < 0.0f
                ? "R2/R3: in flight, watch the spin die in the AIR"
                : (scene.keep_at_touchdown > FLIGHT_KEEP_MIN
                    ? "R2 PASS: spin survived flight"
                    : "R2/R3 FAIL (G-36/37): spin died before the floor existed"));
        l_verdict->set_text(verdict);

        {   // the panel, live (protocol 2026-08-21)
            int passing = 0;
            for (int i = 0; i < MAX_ASSERTS; ++i) {
                if (i < (int)panel.size()) {
                    const bool ok = panel[i].eval();
                    if (ok) ++passing;
                    panel_labels[i]->set_text((ok ? "[V] " : "[X] ")
                                              + panel[i].text);
                    if (ok) panel_labels[i]->set_color(120, 230, 140);
                    else    panel_labels[i]->set_color(255, 120, 120);
                } else {
                    panel_labels[i]->set_text("");
                }
            }
            std::snprintf(buf, sizeof(buf),
                          "ASSERTS %d/%zu passing (end-state lines settle late)",
                          passing, panel.size());
            l_count->set_text(buf);
            l_count->set_color(passing == (int)panel.size() ? 120 : 255,
                               passing == (int)panel.size() ? 230 : 120,
                               120);
        }
        engine.render();
        if (interactive) {
            engine.present();
            engine.get_platform()->poll_events();
            auto* win = static_cast<GLFWwindow*>(
                engine.get_platform()->get_native_window_handle());
            if (win) {
                if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
                if (glfwWindowShouldClose(win)) quit = true;
                // SPACE = next case (owner order); zoom moved to Z.
                const bool sp = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (sp && !space_was_down) advance_case = true;
                space_was_down = sp;
                const bool zk = glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS;
                if (zk && !z_was_down && ppu < 260.0f) {
                    ppu *= 1.15f;
                    cam.set_pixels_per_unit(ppu);
                }
                z_was_down = zk;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame;
    }

    std::printf("  window closed after %d frames.\n", frame);
    engine.shutdown();
    return 0;
}
