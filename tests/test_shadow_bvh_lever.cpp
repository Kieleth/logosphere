// =============================================================================
// SHADOW BVH REBUILD LEVER — can you see the cost of a stale shadow tree?
// =============================================================================
// A refit cannot place NEW triangles into the shadow BVH (no leaves exist for
// them), so a spawning scene must fully rebuild. The rebuild trigger is
// relative (>1% of the live triangle count), which a steady spawn rate outruns
// while the scene is small: measured on the falling-bodies ramp, that meant a
// FULL REBUILD EVERY FRAME between 839 and 2,039 bodies, up to 97 ms each.
//
// Optimizations::SHADOW_BVH_MIN_REBUILD_FRAMES caps how often a rebuild may
// happen. Higher = smoother, at the price of bodies spawned since the last
// rebuild casting NO SHADOW until the next one.
//
// The headless pixel sweep could not find a visual difference even at N=240,
// in two different scenes. That contradicts the mechanism, so this scene exists
// to settle it by eye: sparse, strongly lit, big slow boulders falling onto an
// open floor, where one missing shadow is unmistakable.
//
// SPACE cycles N. The HUD shows N, live rebuild count, and frame time.
//
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_shadow_bvh_lever
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/optimization_flags.h"
#include "../src/ui/ui_system.h"
#include "logosphere/rendering/render_pipeline.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {
const size_t kLevels[] = {1, 2, 4, 8, 16, 240};
const int    kNumLevels = (int)(sizeof(kLevels) / sizeof(kLevels[0]));
}  // namespace

bool test_shadow_bvh_lever() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    printf("\n=== SHADOW BVH REBUILD LEVER ===\n");
    printf("SPACE cycles the minimum frames between shadow-BVH rebuilds.\n");
    printf("Watch the shadows under freshly landed boulders.\n\n");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_title = "Shadow BVH rebuild lever — SPACE to cycle";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }
    auto& ps = engine.get_particle_system();

    // Open floor: wide, flat, pale, so a shadow on it is obvious.
    const int   SIDE = 34;
    const float STEP = 1.0f;
    for (int r = 0; r < SIDE; ++r) {
        for (int c = 0; c < SIDE; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (c - SIDE / 2.0f) * STEP;
            p.y = (r - SIDE / 2.0f) * STEP;
            p.z = 0.05f;
            p.width = p.height = STEP; p.thickness = 0.1f;
            p.size = p.width;
            p.r = 0.62f; p.g = 0.60f; p.b = 0.56f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    }

    // One strong, high, slightly-off-centre light: crisp single shadows with a
    // clear direction, rather than the wash a light rig would give.
    ps.queue_light(-7.0f, -6.0f, 16.0f, 220000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    ps.flush_pending_particles();

    // Start from whatever the lever already says (flag default, or
    // LOGOSPHERE_BVH_REBUILD_FRAMES). Forcing kLevels[0] here silently
    // overrode the env var and made two headless runs measure the same N.
    int cur = 0;
    {
        const size_t current = logosphere::get_shadow_bvh_rebuild_frames();
        for (int i = 0; i < kNumLevels; ++i) if (kLevels[i] == current) { cur = i; break; }
    }

    // Big, slow, well-separated boulders. Sparse on purpose: the whole question
    // is whether ONE missing shadow is visible, which a pile cannot answer.
    const int SPAWN_EVERY = 18;   // frames between drops
    int spawn_seq = 0;
    auto drop_boulder = [&](int seq) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        const float a = (float)seq * 2.399963f;               // golden angle
        const float rad = 3.0f + 6.0f * std::sqrt((float)((seq % 12) + 1) / 12.0f);
        p.x = rad * std::cos(a);
        p.y = rad * std::sin(a);
        p.z = 14.0f;
        const float s = 0.9f + 0.5f * (float)((seq % 3)) / 3.0f;
        p.width = p.height = p.thickness = s;
        p.size = s;
        p.r = 0.80f; p.g = 0.34f; p.b = 0.24f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::DYNAMIC;
        v[id].owner = ParticleOwner::DYNAMICS;
        v[id].is_at_rest = false;
    };

    if (!interactive) {
        // Headless: run the SAME scene so the interactive observation can be
        // checked against numbers. LOGOSPHERE_BVH_REBUILD_FRAMES picks N;
        // LOGOSPHERE_METRICS captures per-frame phases. Dumps a PPM at a fixed
        // frame so quality can be diffed across N as well.
        const int FRAMES = 900;
        const int DUMP_AT = 700;
        const char* dd = std::getenv("BVH_DUMP_DIR");
        for (int f = 0; f < FRAMES; ++f) {
            if ((f % SPAWN_EVERY) == 0 && (int)ps.count() < 4000) drop_boulder(spawn_seq++);
            engine.update(1.0 / 60.0);
            engine.render();
            engine.get_renderer().wait_for_completion();   // deterministic capture
            if (f == DUMP_AT && dd) {
                int w = engine.get_render_buffer().width();
                int h = engine.get_render_buffer().height();
                std::vector<uint32_t> px((size_t)w * h);
                if (engine.read_latest_framebuffer(px.data(), w, h)) {
                    std::string path = std::string(dd) + "/bvh_n" +
                        std::to_string(logosphere::get_shadow_bvh_rebuild_frames()) + ".ppm";
                    FILE* fp = fopen(path.c_str(), "wb");
                    if (fp) {
                        fprintf(fp, "P6\n%d %d\n255\n", w, h);
                        for (int i = 0; i < w * h; ++i) {
                            uint32_t q = px[i];
                            unsigned char rgb[3] = {(unsigned char)((q>>16)&0xFF),
                                                    (unsigned char)((q>>8)&0xFF),
                                                    (unsigned char)(q&0xFF)};
                            fwrite(rgb, 1, 3, fp);
                        }
                        fclose(fp);
                        printf("  dumped %s\n", path.c_str());
                    }
                }
            }
        }
        printf("  headless done: N=%zu, %d boulders, %d particles\n",
               logosphere::get_shadow_bvh_rebuild_frames(), spawn_seq, (int)ps.count());
        engine.shutdown();
        return true;
    }

    printf("  N = %zu  (1 = rebuild on every change, highest fidelity)\n", kLevels[cur]);
    bool space_was = false;
    long frame = 0;
    double ms_avg = 16.0;
    size_t last_shown = 0;
    while (engine.is_running()) {
        if ((frame % SPAWN_EVERY) == 0 && (int)ps.count() < 4000) drop_boulder(spawn_seq++);

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.update(1.0 / 60.0);
        engine.render();

        // Pump the platform. Without this the input state is never refreshed:
        // keys read as stale garbage, so real presses do nothing AND phantom
        // ones fire. Cost me two runs; test_ssgi_visual has always done it.
        engine.get_platform()->poll_events();

        // Feedback goes in the WINDOW TITLE, not the UI plane.
        // Engine::present() calls draw_ui_overlays(), whose first act is to
        // clear the overlay buffer; only the debug overlay and registered
        // widgets are re-rendered inside it. Immediate-mode draw_text issued
        // between render() and present() is therefore wiped before it ever
        // reaches the screen, which is why three attempts at an on-screen HUD
        // showed nothing. The title bar is outside all of that.
        if (GLFWwindow* win = (GLFWwindow*)engine.get_window_handle()) {
            const size_t n = logosphere::get_shadow_bvh_rebuild_frames();
            if (n != last_shown || (frame % 30) == 0) {
                char title[200];
                snprintf(title, sizeof(title),
                         "N = %zu  (rebuild at most every %zu frame%s)   |   %d boulders   |   %.1f ms"
                         "   |   SPACE = next N, ESC = quit",
                         n, n, n == 1 ? "" : "s", spawn_seq, ms_avg);
                glfwSetWindowTitle(win, title);
                last_shown = n;
            }
        }

        engine.present();

        auto t1 = std::chrono::high_resolution_clock::now();
        ms_avg = ms_avg * 0.94 + 0.06 *
                 std::chrono::duration<double, std::milli>(t1 - t0).count();
        ++frame;

        const auto& in = engine.get_input_system();
        const bool down = in.get_input_state().keys[GLFW_KEY_SPACE];
        if (down && !space_was) {
            cur = (cur + 1) % kNumLevels;
            logosphere::set_shadow_bvh_rebuild_frames(kLevels[cur]);
            printf("  N = %zu%s\n", kLevels[cur],
                   kLevels[cur] == 240 ? "   (deliberately absurd — the tree barely updates)" : "");
            fflush(stdout);
        }
        space_was = down;
        if (in.get_input_state().keys[GLFW_KEY_ESCAPE]) { printf("\n  ESC\n"); break; }
    }

    printf("\n=== closed after %ld frames, %d boulders ===\n", frame, spawn_seq);
    engine.shutdown();
    return true;
}
