// =============================================================================
// SPHERE LOD QUALITY — is 320 triangles per sphere worth paying for?
// =============================================================================
// A sphere particle emits 20 / 80 / 320 / 1280 triangles at icosphere
// subdivision 0 / 1 / 2 / 3. Level 2 is the shipped default, and study S10
// showed every downstream cost scales with it: surfaces built, SurfaceData
// written, shadow triangles, BVH input, bytes uploaded. Level 1 is a 4x cut
// on all of it.
//
// The perf side is measurable. The quality side is a judgement call, so this
// test makes the judgement possible: it renders ONE fixed scene at every
// level and reports, per level, how many pixels actually change against the
// highest level as reference, and by how much.
//
// The scene is a row of spheres from large to small, because faceting is a
// function of SCREEN size. A sphere twenty pixels across cannot show 320
// triangles' worth of silhouette. Finding the size where the levels diverge
// is the whole point.
//
// Env knobs:
//   INTERACTIVE=1    windowed; SPACE cycles the LOD live on the same scene
//   LOD_DUMP_DIR=/x  where the per-level PPMs land (default /tmp)
//   LOD_LEVELS=0,1,2,3   which levels to render (default all four)
//
// Run:
//   ./build-release/logosphere-tests --test test_sphere_lod_quality --no-head
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_sphere_lod_quality
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/particle_geometry_v2.h"
#include "../src/optimization_flags.h"
#include "../src/ui/ui_system.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {

int tri_count_for(int lod) {
    int n = 20;
    for (int i = 0; i < lod; ++i) n *= 4;
    return n;
}

void dump_ppm(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        uint32_t p = px[i];
        unsigned char rgb[3] = { (unsigned char)((p >> 16) & 0xFF),
                                 (unsigned char)((p >> 8) & 0xFF),
                                 (unsigned char)(p & 0xFF) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// Per-level difference against the reference level. Reported as a count and
// as a share of the pixels the spheres actually cover, because "0.3% of the
// screen" and "18% of the spheres" are very different statements.
struct Diff {
    long   pixels_changed  = 0;
    long   pixels_nonblack = 0;   // proxy for "covered by something"
    int    max_delta       = 0;
    double mean_delta      = 0.0; // over changed pixels only
};

Diff compare(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    Diff d;
    long sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const uint32_t pa = a[i], pb = b[i];
        if ((pa & 0x00FFFFFF) != 0) d.pixels_nonblack++;
        int dr = std::abs((int)((pa >> 16) & 0xFF) - (int)((pb >> 16) & 0xFF));
        int dg = std::abs((int)((pa >>  8) & 0xFF) - (int)((pb >>  8) & 0xFF));
        int db = std::abs((int)( pa        & 0xFF) - (int)( pb        & 0xFF));
        int worst = std::max(dr, std::max(dg, db));
        if (worst > 0) {
            d.pixels_changed++;
            sum += worst;
            d.max_delta = std::max(d.max_delta, worst);
        }
    }
    d.mean_delta = d.pixels_changed ? (double)sum / (double)d.pixels_changed : 0.0;
    return d;
}

} // namespace

bool test_sphere_lod_quality() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    const char* dump_env = std::getenv("LOD_DUMP_DIR");
    const std::string dump_dir = dump_env ? dump_env : "/tmp";

    std::vector<int> levels;
    if (const char* lv = std::getenv("LOD_LEVELS")) {
        const char* p = lv;
        while (*p) {
            if (*p >= '0' && *p <= '4') levels.push_back(*p - '0');
            ++p;
        }
    }
    if (levels.empty()) levels = {0, 1, 2, 3};

    printf("\n=== SPHERE LOD QUALITY (%s) ===\n",
           interactive ? "interactive, SPACE cycles LOD" : "headless, dumps + pixel diffs");
    printf("Default is Optimizations::SPHERE_SUBDIVISIONS = %d (%d triangles/sphere)\n",
           Optimizations::SPHERE_SUBDIVISIONS,
           tri_count_for(Optimizations::SPHERE_SUBDIVISIONS));

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_title = "Sphere LOD quality";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }
    auto& ps = engine.get_particle_system();

    // Floor: a flat box grid. Spheres need something to sit on and cast onto,
    // since half of what LOD costs you shows up in the shadow silhouette.
    const int   FLOOR_SIDE = 26;
    const float FLOOR_STEP = 1.0f;
    for (int r = 0; r < FLOOR_SIDE; ++r) {
        for (int c = 0; c < FLOOR_SIDE; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (c - FLOOR_SIDE / 2.0f) * FLOOR_STEP;
            p.y = (r - FLOOR_SIDE / 2.0f) * FLOOR_STEP;
            p.z = 0.05f;
            p.width = p.height = FLOOR_STEP; p.thickness = 0.1f;
            p.size = p.width;
            p.r = 0.30f; p.g = 0.30f; p.b = 0.33f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto view = ps.lock_particles_for_write();
            view[id].solver_mode = ParticleSolverMode::KINEMATIC;
            view[id].owner = ParticleOwner::DYNAMICS;
            view[id].is_at_rest = true;
        }
    }

    // The subjects: spheres from 3.0 m down to 0.25 m. Faceting is a function
    // of how many pixels the silhouette spans, so sweeping size sweeps the
    // only variable that matters.
    const float SIZES[] = { 3.0f, 2.0f, 1.4f, 1.0f, 0.7f, 0.5f, 0.35f, 0.25f };
    const int   N_SPHERES = (int)(sizeof(SIZES) / sizeof(SIZES[0]));
    float cursor_x = -9.0f;
    for (int i = 0; i < N_SPHERES; ++i) {
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.size = SIZES[i];
        p.width = p.height = p.thickness = SIZES[i];
        cursor_x += SIZES[i] * 0.5f + 0.6f;
        p.x = cursor_x;
        p.y = 0.0f;
        p.z = 0.1f + SIZES[i] * 0.5f;
        cursor_x += SIZES[i] * 0.5f;
        p.r = 0.85f; p.g = 0.80f; p.b = 0.72f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto view = ps.lock_particles_for_write();
        view[id].solver_mode = ParticleSolverMode::KINEMATIC;
        view[id].owner = ParticleOwner::DYNAMICS;
        view[id].is_at_rest = true;
    }
    printf("Scene: %dx%d floor, %d spheres from %.2f m to %.2f m\n",
           FLOOR_SIDE, FLOOR_SIDE, N_SPHERES, SIZES[0], SIZES[N_SPHERES - 1]);

    // Two fixed lights. Static, because a moving light would make the frames
    // non-comparable across levels.
    ps.queue_light(-8.0f,  -7.0f, 7.0f, 90000.0f, 60.0f, 1.0f, 0.96f, 0.90f);
    ps.queue_light( 9.0f,   6.0f, 5.0f, 45000.0f, 60.0f, 0.72f, 0.80f, 1.0f);
    ps.flush_pending_particles();

    const int SETTLE = 45;   // frames to let temporal passes converge

    if (interactive) {
        printf("\nSPACE cycles LOD. Watch the silhouettes, left (large) to right (small).\n");
        int cur = 0;
        logosphere::set_sphere_lod(levels[cur]);
        printf("  LOD %d — %d triangles/sphere\n", levels[cur], tri_count_for(levels[cur]));
        bool space_was_down = false;
        long frame = 0;
        while (engine.is_running()) {
            engine.update(1.0 / 60.0);
            engine.render();

            // On-screen readout: comparing levels by eye is useless if you
            // cannot see which one you are looking at.
            if (auto* ui = engine.get_ui_system()) {
                char hud[160];
                snprintf(hud, sizeof(hud), "LOD %d  —  %d triangles/sphere",
                         levels[cur], tri_count_for(levels[cur]));
                ui->draw_text(10, 10, hud, 255, 255, 160);
                ui->draw_text(10, 34, "SPACE: next LOD | ESC: exit", 150, 150, 150);
            }
            // Without present() the frame never reaches the window.
            engine.present();
            ++frame;

            const auto& input = engine.get_input_system();
            const bool down = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (down && !space_was_down) {
                cur = (cur + 1) % (int)levels.size();
                logosphere::set_sphere_lod(levels[cur]);
                printf("  LOD %d — %d triangles/sphere\n",
                       levels[cur], tri_count_for(levels[cur]));
                fflush(stdout);
            }
            space_was_down = down;
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                printf("\n  ESC — exiting\n");
                break;
            }
        }
        printf("\n=== closed after %ld frames ===\n", frame);
        engine.shutdown();
        return true;
    }

    // Headless: render the same scene once per level, dump it, and diff.
    std::vector<std::vector<uint32_t>> shots(levels.size());
    int w = 0, h = 0;
    for (size_t li = 0; li < levels.size(); ++li) {
        logosphere::set_sphere_lod(levels[li]);
        for (int f = 0; f < SETTLE; ++f) {
            engine.update(1.0 / 60.0);
            engine.render();
            // Serialize: the non-blocking frame sync drops frames when the GPU
            // is behind, which would make which frame we captured depend on
            // timing rather than on the LOD under test.
            engine.get_renderer().wait_for_completion();
        }
        w = engine.get_render_buffer().width();
        h = engine.get_render_buffer().height();
        shots[li].assign((size_t)w * h, 0u);
        if (!engine.read_latest_framebuffer(shots[li].data(), w, h)) {
            printf("  ERROR: framebuffer read failed at LOD %d\n", levels[li]);
            engine.shutdown();
            return false;
        }
        const std::string path = dump_dir + "/sphere_lod" + std::to_string(levels[li]) + ".ppm";
        dump_ppm(path, shots[li], w, h);
        printf("  LOD %d (%4d tris/sphere) -> %s\n",
               levels[li], tri_count_for(levels[li]), path.c_str());
    }

    // Highest requested level is the reference: it is the most faithful sphere
    // available, so "can I tell the difference" means "against that one".
    const size_t ref = levels.size() - 1;
    printf("\n%-6s %-8s %14s %12s %10s %10s\n",
           "LOD", "tris", "pixels changed", "% of lit", "max Δ", "mean Δ");
    bool any_indistinguishable = false;
    for (size_t li = 0; li < levels.size(); ++li) {
        Diff d = compare(shots[li], shots[ref]);
        const double pct = d.pixels_nonblack
            ? 100.0 * (double)d.pixels_changed / (double)d.pixels_nonblack : 0.0;
        printf("%-6d %-8d %14ld %11.2f%% %10d %10.1f%s\n",
               levels[li], tri_count_for(levels[li]), d.pixels_changed, pct,
               d.max_delta, d.mean_delta, li == ref ? "   (reference)" : "");
        if (li != ref && d.pixels_changed == 0) any_indistinguishable = true;
    }
    printf("\nImages are in %s. The numbers bound the difference; the eye decides\n"
           "whether it matters. A large 'pixels changed' with a small 'mean Δ' is\n"
           "shading noise, not visible faceting.\n", dump_dir.c_str());
    if (any_indistinguishable) {
        printf("NOTE: at least one lower level is BYTE-IDENTICAL to the reference.\n");
    }

    engine.shutdown();
    return true;
}
