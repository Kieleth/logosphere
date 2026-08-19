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
    scene.arm(ps, RUNGS[0]);
    float lx, ly, lz;
    { auto v = ps.lock_particles_for_read(); lx=v[scene.cube].x; ly=v[scene.cube].y; lz=v[scene.cube].z; }
    const Lamps lamps = make_lamps(ps, lx, ly, lz);
    float ppu = 160.0f;
    cam.set_pixels_per_unit(ppu);

    auto* l_rung = add_line(engine, 0, 255, 240, 140);
    auto* l_live = add_line(engine, 1, 220, 220, 220);
    auto* l_meas = add_line(engine, 2, 140, 210, 255);
    auto* l_verdict = add_line(engine, 3, 255, 120, 120);

    if (interactive)
        std::printf("\n  ESC or the red X quits.  SPACE moves the camera in.\n"
                    "  cycling: control -> tilted 20 deg -> spinning 3 rad/s\n\n");

    bool space_was_down = false, quit = false;
    int frame = 0, rung = 0, rung_frame = 0;
    char buf[256];

    const int total_frames = interactive ? 1 << 30 : RUN_FRAMES * 3;
    while (interactive ? (!quit && engine.should_continue())
                       : (frame < total_frames)) {
        const auto t0 = std::chrono::steady_clock::now();

        if (rung_frame >= RUN_FRAMES) {          // next rung, wrap in window mode
            rung = (rung + 1) % 3;
            if (!interactive && rung == 0) break;
            scene.arm(ps, RUNGS[rung]);
            rung_frame = 0;
        }
        scene.step(ps, physics, rung_frame, RUNGS[rung].spin_z);

        float x, y, z, oy, oz, ry;
        {   auto v = ps.lock_particles_for_read();
            const Particle& p = v[scene.cube];
            x=p.x; y=p.y; z=p.z; oy=p.omega_y; oz=p.omega_z; ry=p.rotation_y; }
        cam.set_position(x, y, z);
        move_lamps(ps, lamps, x, y, z);

        std::snprintf(buf, sizeof(buf), "%s   (t %.2fs)", RUNGS[rung].name,
                      rung_frame / 60.0f);
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
            rung == 0 ? "R0 control: must invent no rotation"
          : rung == 1 ? (scene.peak_omega_y > TIP_OMEGA_MIN
                ? "R1 PASS: it tipped" 
                : "R1 FAIL (G-35): balanced on an edge, contact torque absent")
          : (scene.keep_at_touchdown < 0.0f
                ? "R2/R3: in flight, watch the spin die in the AIR"
                : (scene.keep_at_touchdown > FLIGHT_KEEP_MIN
                    ? "R2 PASS: spin survived flight"
                    : "R2/R3 FAIL (G-36/37): spin died before the floor existed"));
        l_verdict->set_text(verdict);

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
                if (down && !space_was_down && ppu < 195.0f) {
                    ppu *= 1.15f; if (ppu > 195.0f) ppu = 195.0f;
                    cam.set_pixels_per_unit(ppu);
                }
                space_was_down = down;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame; ++rung_frame;
    }

    std::printf("  window closed after %d frames.\n", frame);
    engine.shutdown();
    return 0;
}
