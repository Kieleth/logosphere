// =============================================================================
// ONE BODY, ONE ORIENTATION — the window
// =============================================================================
// Twin cubes, identical spin about Y. The LEFT one is quat-truth: its
// Euler triple is published from the quaternion, so you see it turn.
// The RIGHT one is Euler-truth (the engine default): the same spin
// integrates into a quaternion nobody reads, and the cube you see is
// FROZEN. Same body, same spin, one visible rotation.
//
// ESC or the red X quits. SPACE moves the camera in.
// =============================================================================

#include "core/engine.h"
#include "scenes/scene_orientation_truth.h"
#include "../src/ui/widgets.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace scene_orientation_truth;

namespace {
constexpr float LIGHT_D = 3.0f;
struct Lamps { size_t key = 0, fill = 0; };
Lamps make_lamps(ParticleSystem& ps, float x, float y, float z) {
    Lamps l;
    l.key  = ps.queue_light(x + 1.5f, y - 2.0f, z + 2.0f,
                            4000.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            1.0f, 0.95f, 0.85f);
    l.fill = ps.queue_light(x - 2.0f, y + 1.5f, z - 1.0f,
                            1500.0f * LIGHT_D * LIGHT_D, 1.25f * LIGHT_D,
                            0.75f, 0.8f, 1.0f);
    ps.flush_pending_particles();
    return l;
}
void move_lamps(ParticleSystem& ps, const Lamps& l, float x, float y, float z) {
    auto v = ps.lock_particles_for_write();
    v[l.key].x  = x + 1.5f; v[l.key].y  = y - 2.0f; v[l.key].z  = z + 2.0f;
    v[l.fill].x = x - 2.0f; v[l.fill].y = y + 1.5f; v[l.fill].z = z - 1.0f;
}
ui::Label* add_line(Engine& e, int row, int base_y, uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", "line" + std::to_string(row));
    l->set_position(16, base_y + row * 24);
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
    cfg.window_title = "one body, one orientation: same spin, one turns";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    if (engine.initialize(cfg) != 0) { std::printf("  ERROR: init\n"); return 1; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& cam = engine.get_camera_system();

    Scene scene;
    scene.build(ps);
    const Lamps lamps = make_lamps(ps, 0.0f, 0.0f, 40.0f);
    float ppu = 190.0f;
    cam.set_pixels_per_unit(ppu);

    const int base_y = cfg.window_height - 118;   // clear of the debug overlay
    auto* l_head = add_line(engine, 0, base_y, 255, 240, 140);
    auto* l_q    = add_line(engine, 1, base_y, 140, 255, 170);
    auto* l_e    = add_line(engine, 2, base_y, 255, 150, 150);
    auto* l_verd = add_line(engine, 3, base_y, 190, 220, 255);
    l_head->set_text("twin cubes, identical spin about Y. LEFT quat-truth, "
                     "RIGHT Euler-truth. re-spun every 3 s");

    if (interactive)
        std::printf("\n  ESC or the red X quits.  SPACE moves the camera in.\n\n");

    bool space_was_down = false, quit = false;
    int frame = 0;
    char buf[256];

    while (interactive ? (!quit && engine.should_continue())
                       : (frame < RUN_FRAMES)) {
        const auto t0 = std::chrono::steady_clock::now();
        if (frame > 0 && frame % 180 == 0) {   // re-arm so the contrast repeats
            auto v = ps.lock_particles_for_write();
            for (int id : { scene.quat_twin, scene.euler_twin }) {
                v[id].x = 0.0f; v[id].y = (id == scene.quat_twin) ? -0.6f : 0.6f;
                v[id].z = 40.0f;
                v[id].vx = v[id].vy = v[id].vz = 0.0f;
                v[id].omega_x = v[id].omega_z = 0.0f;
                v[id].omega_y = SPIN_Y;
                v[id].rotation_x = v[id].rotation_y = v[id].rotation_z = 0.0f;
                v[id].rotation_q = logosphere::Quat::identity();
                v[id].is_at_rest = false;
            }
        }
        scene.step(ps, physics);

        float cx, cy, cz;
        {   auto v = ps.lock_particles_for_read();
            cx = 0.0f; cy = 0.0f; cz = v[scene.quat_twin].z; }
        cam.set_position(cx, cy, cz);          // both twins fall together
        move_lamps(ps, lamps, cx, cy, cz);

        std::snprintf(buf, sizeof(buf),
                      "LEFT  (quat-truth):  visible rot_y %+.3f   divergence %.4f rad",
                      scene.visible_rot_y(ps, scene.quat_twin),
                      scene.divergence(ps, scene.quat_twin));
        l_q->set_text(buf);
        std::snprintf(buf, sizeof(buf),
                      "RIGHT (Euler-truth): visible rot_y %+.3f   divergence %.4f rad",
                      scene.visible_rot_y(ps, scene.euler_twin),
                      scene.divergence(ps, scene.euler_twin));
        l_e->set_text(buf);
        const float de = scene.divergence(ps, scene.euler_twin);
        l_verd->set_text(de < COHERENCE_MAX
            ? "PASS: one body, one orientation"
            : "FAIL (G-23): the right cube's turn is trapped in a field "
              "nobody reads");

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
                if (down && !space_was_down && ppu < 260.0f) {
                    ppu *= 1.15f;
                    cam.set_pixels_per_unit(ppu);
                }
                space_was_down = down;
            }
            std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
        }
        ++frame;
    }
    std::printf("  window closed after %d frames.\n", frame);
    engine.shutdown();
    return 0;
}
