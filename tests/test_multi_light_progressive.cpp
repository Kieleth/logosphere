// =============================================================================
// PROGRESSIVE MULTI-LIGHT — scaling vehicle for the GPU campaign
// =============================================================================
// A grid of particles under a growing set of FLOATING lights. Each phase
// adds one orbiting light and measures per-frame cost, producing the
// light-scaling curve. Everything is frame-count-driven (fixed 1/60 dt,
// KINEMATIC particles), so headless runs are deterministic and each
// phase's framebuffer dump is a byte-stable pixel-diff oracle for
// zero-quality-cost optimization A/Bs.
//
// Env knobs:
//   MLP_MODE=       lights | particles | both — the sweep shape:
//                     lights    lights grow per phase, particles fixed
//                     particles lights fixed, particles rain in
//                     both      both grow together
//   PARTICLES=N     grid size (default 144; thousands supported)
//   LIGHTS=N        number of phases/lights (default 4)
//   MLP_SPAWN_RATE= bodies dropped per second (ramp); 0 = none
//   MLP_DEMO=1      demo dressing: coloured floor, pillars, walls, bodies
//   INTERACTIVE=1   windowed, watchable (phases auto-advance)
//   MLP_DUMP_DIR=/x per-phase PPM dump dir (default /tmp; headless only)
//
// Run:
//   ./build/logosphere-tests --test test_multi_light_progressive            # headless metrics
//   INTERACTIVE=1 ./build/logosphere-tests --test test_multi_light_progressive
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/optimization_flags.h"
#include "../src/ui/ui_system.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// env_int treats 0 as "unset" (it returns the fallback), which is fine for
// counts that must be positive but cannot express "disable". Knobs that need
// a real zero use this instead.
int env_int_allow_zero(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    return std::atoi(v);
}

int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    int n = std::atoi(v);
    return n > 0 ? n : fallback;
}

void dump_ppm(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        uint32_t p = px[i];
        unsigned char rgb[3] = {
            (unsigned char)((p >> 16) & 0xFF),
            (unsigned char)((p >> 8) & 0xFF),
            (unsigned char)(p & 0xFF) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

} // namespace

bool test_multi_light_progressive() {
    const int particles_requested = env_int("PARTICLES", 144);
    const int num_lights = env_int("LIGHTS", 6);
    // MLP_DEMO=1 dresses the scene for showing off: coloured floor, shadow
    // -casting pillars, layered orbits. It deliberately does NOT touch the
    // default scene — the phase dumps are pixel oracles, and changing what
    // they render would invalidate every stored comparison.
    const bool demo = env_int("MLP_DEMO", 0) != 0;
    // MLP_SINGLE_PHASE=1 spawns all LIGHTS at once and measures that count
    // ONLY. Needed because the default walks phases 1..N sequentially inside
    // one process, which makes light count perfectly correlated with elapsed
    // time — so thermal drift is indistinguishable from light cost, and the
    // sweep's interleaving cannot help (it is inside the process). One light
    // count per process is what lets bench_sweep interleave them.
    // MLP_MODE picks the sweep SHAPE; the low-level knobs below still win
    // if set explicitly.
    //   lights     lights grow one per phase, particle count held fixed
    //   particles  lights held at LIGHTS, particles rain in
    //   both       lights grow AND particles rain in
    const char* mode_env = std::getenv("MLP_MODE");
    const std::string mode = mode_env ? mode_env : "";
    const bool mode_particles = (mode == "particles");
    const bool mode_both      = (mode == "both");
    if (!mode.empty() && !mode_particles && !mode_both && mode != "lights") {
        printf("  WARNING: unknown MLP_MODE='%s' (expected lights|particles|both)\n",
               mode.c_str());
    }

    const bool single_phase =
        env_int("MLP_SINGLE_PHASE", mode_particles ? 1 : 0) != 0;
    // RAMP: keep dropping bodies at a fixed rate so particle count climbs
    // steadily during the run. One run then traces a CONTINUOUS cost curve
    // (frame time vs live particle count) instead of a handful of discrete
    // configs — and with lights held fixed it shows where particles, not
    // lights, start to bite. bench_report --ramp reads it back.
    const int spawn_rate = env_int("MLP_SPAWN_RATE",
                                   (mode_particles || mode_both) ? 90 : 0);
    const int max_particles = env_int("MLP_MAX_PARTICLES", 20000);
    const int sphere_every = env_int_allow_zero("MLP_SPHERE_EVERY", 3);
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    const char* dump_dir_env = std::getenv("MLP_DUMP_DIR");
    const std::string dump_dir = dump_dir_env ? dump_dir_env : "/tmp";

    printf("\n=== PROGRESSIVE MULTI-LIGHT (%s) ===\n",
           interactive ? "interactive" : "headless metrics");
    printf("PARTICLES=%d LIGHTS=%d USE_LIGHT_CULLING=%s\n",
           particles_requested, num_lights,
           Optimizations::USE_LIGHT_CULLING ? "on" : "off");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_title = "Progressive multi-light";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = interactive;  // engine FPS overlay, user-visible
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }
    auto& ps = engine.get_particle_system();

    // Particle grid: flat, KINEMATIC, at rest — physics never moves it,
    // so frames are a pure function of the frame counter.
    const int side = std::max(2, (int)std::ceil(std::sqrt((double)particles_requested)));
    const float spacing = std::max(0.45f, 18.0f / (float)side);
    int spawned = 0;
    for (int row = 0; row < side && spawned < particles_requested; ++row) {
        for (int col = 0; col < side && spawned < particles_requested; ++col) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (col - side / 2.0f) * spacing;
            p.y = (row - side / 2.0f) * spacing;
            p.width = p.height = p.thickness = spacing * 0.55f;
            // The grid is the scene's floor: rest each cube ON the turtle
            // (bottom at z=0). The old fixed z=0.2 sank spacing-derived
            // cubes 0.21 m under the world floor.
            p.z = p.thickness * 0.5f;
            p.size = p.width;
            if (demo) {
                // Dark, slightly varying blue-violet: near-black under no
                // light, so every coloured light writes its own hue.
                float t = (float)((row * 7 + col * 13) % 11) / 11.0f;
                p.r = 0.06f + 0.05f * t;
                p.g = 0.07f + 0.04f * t;
                p.b = 0.16f + 0.10f * t;
            } else {
                p.r = 0.59f; p.g = 0.39f; p.b = 0.39f;
            }
            p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            {
                auto view = ps.lock_particles_for_write();
                view[id].solver_mode = ParticleSolverMode::KINEMATIC;
                view[id].owner = ParticleOwner::DYNAMICS;
                view[id].is_at_rest = true;
            }
            spawned++;
        }
    }
    printf("Grid: %dx%d, %d particles, spacing %.2f\n", side, side, spawned, spacing);

    // DEMO ONLY: shadow casters. A lit flat grid is undramatic; tall
    // geometry under moving coloured lights throws long sweeping shadows,
    // which is the whole visual. Deterministic placement (no RNG) so the
    // demo is reproducible frame for frame like the benchmark scene.
    int pillars = 0;
    if (demo) {
        const float extent = side * spacing * 0.42f;
        for (int i = 0; i < 14; ++i) {
            float a = (float)i * 2.399963f;               // golden angle
            float r = extent * std::sqrt((float)(i + 1) / 14.0f);
            float h = 2.5f + 3.5f * (float)((i * 5) % 7) / 6.0f;
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = r * std::cos(a);
            p.y = r * std::sin(a);
            p.z = h * 0.5f;
            p.width = p.height = spacing * 1.1f;
            p.thickness = h;
            p.size = std::max(p.width, h);
            p.r = 0.82f; p.g = 0.84f; p.b = 0.90f;        // near-white: takes any hue
            p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            {
                auto view = ps.lock_particles_for_write();
                view[id].solver_mode = ParticleSolverMode::KINEMATIC;
                view[id].owner = ParticleOwner::DYNAMICS;
                view[id].is_at_rest = true;
            }
            pillars++;
        }
        // Two big white walls forming a corner behind the square. Shadows
        // need something to land on: without them the pillars' shadows fall
        // only on the floor and read flat. Only 2 particles, but they cover
        // a lot of screen, so their cost is raster (pixels), not per-particle.
        {
            const float span = side * spacing * 1.25f;
            const float wall_h = 16.0f;
            const float thin = 0.6f;
            struct { float x, y, w, hgt; } walls[2] = {
                { 0.0f,  span * 0.5f, span, thin },   // far wall, spans X
                { span * 0.5f, 0.0f,  thin, span },   // side wall, spans Y
            };
            for (auto& wd : walls) {
                Particle p = {};
                p.shape = ParticleShape::BOX;
                p.x = wd.x; p.y = wd.y; p.z = wall_h * 0.5f;
                p.width = wd.w; p.height = wd.hgt; p.thickness = wall_h;
                p.size = std::max(std::max(p.width, p.height), wall_h);
                p.r = 0.92f; p.g = 0.93f; p.b = 0.95f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                int id = engine.add_particle(p);
                auto view = ps.lock_particles_for_write();
                view[id].solver_mode = ParticleSolverMode::KINEMATIC;
                view[id].owner = ParticleOwner::DYNAMICS;
                view[id].is_at_rest = true;
            }
        }

        // Falling bodies: real DYNAMIC particles under the solver, dropped
        // among the pillars. They tumble, collide and settle while the
        // coloured lights rake across them — the physics IS the show.
        // Demo only: these are nondeterministic, which is exactly why the
        // benchmark scene stays KINEMATIC and at rest.
        int movers = 0;
        for (int i = 0; i < 48; ++i) {
            float a = (float)i * 2.399963f;
            float r = extent * 0.85f * std::sqrt((float)(i + 1) / 48.0f);
            Particle p = {};
            p.shape = (i % 3 == 0) ? ParticleShape::SPHERE : ParticleShape::BOX;
            p.x = r * std::cos(a);
            p.y = r * std::sin(a);
            p.z = 9.0f + 0.55f * (float)i;          // staggered drop, no clumping
            p.width = p.height = p.thickness = spacing * (0.5f + 0.35f * (float)(i % 4) / 3.0f);
            p.size = p.width;
            // Bright neutral: takes the hue of whichever light reaches it.
            p.r = 0.86f; p.g = 0.88f; p.b = 0.92f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            {
                auto view = ps.lock_particles_for_write();
                view[id].solver_mode = ParticleSolverMode::DYNAMIC;
                view[id].owner = ParticleOwner::DYNAMICS;
                view[id].is_at_rest = false;
            }
            movers++;
        }
        printf("Demo: %d shadow-casting pillars, %d falling bodies\n",
               pillars, movers);
    }

    const float ORBIT_R = std::max(10.0f, side * spacing * 0.75f);
    const float LIGHT_Z = 6.0f;
    std::vector<int> light_ids;

    const int WARMUP = 30;
    // MLP_FRAMES overrides the measured window. The defaults are sized for a
    // benchmark; watching the GPU HUD under a live load needs a long run.
    const int MEASURED = env_int("MLP_FRAMES", interactive ? 360 : 240);
    const int DUMP_FRAME = 200;  // within the measured window, past settle

    long total_frame = 0;
    bool all_phases_ran = true;

    const int first_phase = single_phase ? num_lights : 1;
    for (int phase = first_phase; phase <= num_lights; ++phase) {
        // Bring the light count up to `phase`. Normally that adds one per
        // phase; in single-phase mode the first iteration spawns them all.
        while ((int)light_ids.size() < phase) {
            const int idx = (int)light_ids.size();          // 0-based slot
            float base = (float)idx * 2.0f * (float)M_PI / (float)num_lights;
            float lx = ORBIT_R * std::cos(base);
            float ly = ORBIT_R * std::sin(base);
            // Vivid rainbow: full-saturation hue wheel per light index.
            float hue6 = 6.0f * (float)idx / (float)num_lights;
            float rr = std::clamp(std::fabs(hue6 - 3.0f) - 1.0f, 0.0f, 1.0f);
            float gg = std::clamp(2.0f - std::fabs(hue6 - 2.0f), 0.0f, 1.0f);
            float bb = std::clamp(2.0f - std::fabs(hue6 - 4.0f), 0.0f, 1.0f);
            int id = (int)ps.queue_light(lx, ly, LIGHT_Z, 60000.0f, 60.0f, rr, gg, bb);
            ps.flush_pending_particles();
            light_ids.push_back(id);
        }
        printf("\n=== PHASE %d: %d light(s) ===\n", phase, phase);

        std::vector<double> frame_ms;
        frame_ms.reserve(MEASURED);

        for (int f = 0; f < WARMUP + MEASURED; ++f) {
            // Float the lights: orbit + bob, driven purely by frame count.
            {
                auto view = ps.lock_particles_for_write();
                for (size_t i = 0; i < light_ids.size(); ++i) {
                    float base = (float)i * 2.0f * (float)M_PI / (float)num_lights;
                    float ang = base + (float)total_frame * 0.012f * (1.0f + 0.15f * (float)i);
                    Particle& lp = view[light_ids[i]];
                    if (demo) {
                        // Layered orbits: alternating radii and directions,
                        // with lights that dive low. Low lights rake across
                        // the pillars and throw the long shadows.
                        float rad = ORBIT_R * (0.45f + 0.55f * (float)((i % 3) + 1) / 3.0f);
                        float dir = (i % 2) ? -1.0f : 1.0f;
                        float a2  = base + dir * (float)total_frame * 0.011f * (1.0f + 0.22f * (float)i);
                        lp.x = rad * std::cos(a2);
                        lp.y = rad * std::sin(a2);
                        lp.z = 2.0f + 5.5f * (0.5f + 0.5f * std::sin(
                                   (float)total_frame * 0.017f + (float)i * 1.7f));
                    } else {
                        lp.x = ORBIT_R * std::cos(ang);
                        lp.y = ORBIT_R * std::sin(ang);
                        lp.z = LIGHT_Z + 1.5f * std::sin((float)total_frame * 0.02f + (float)i);
                    }
                }
            }

            // Ramp: drop bodies at a steady rate, frame-count driven.
            if (spawn_rate > 0 && (int)ps.count() < max_particles) {
                static double spawn_accum = 0.0;
                static int spawn_seq = 0;
                spawn_accum += (double)spawn_rate / 60.0;
                while (spawn_accum >= 1.0) {
                    spawn_accum -= 1.0;
                    const float a = (float)spawn_seq * 2.399963f;   // golden angle
                    const float r = ORBIT_R * 0.55f *
                        std::sqrt((float)((spawn_seq % 64) + 1) / 64.0f);
                    Particle p = {};
                    // MLP_SPHERE_EVERY=N: every Nth body is a sphere (0 = none).
                    // Spheres are 320 surfaces at subdivision 2 vs 12 for a
                    // box, so the mix dominates surface count — see study S7.
                    p.shape = (sphere_every > 0 && (spawn_seq % sphere_every) == 0)
                                ? ParticleShape::SPHERE : ParticleShape::BOX;
                    p.x = r * std::cos(a);
                    p.y = r * std::sin(a);
                    p.z = 14.0f;
                    p.width = p.height = p.thickness = spacing * 0.6f;
                    p.size = p.width;
                    p.r = 0.88f; p.g = 0.90f; p.b = 0.95f; p.a = 1.0f;
                    p.SetMaterial(Materials::Type::STONE);
                    int id = engine.add_particle(p);
                    {
                        auto view = ps.lock_particles_for_write();
                        view[id].solver_mode = ParticleSolverMode::DYNAMIC;
                        view[id].owner = ParticleOwner::DYNAMICS;
                        view[id].is_at_rest = false;
                    }
                    spawn_seq++;
                }
            }

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.update(1.0 / 60.0);
            engine.render();
            // Headless: serialize every frame. The non-blocking frame sync
            // DROPS frames when the GPU is behind, so which animation frames
            // render becomes timing-dependent — run-to-run nondeterminism
            // that poisons the pixel oracle (caught 2026-07-24: the phase-1
            // glow flipped between runs of the same binary). Waiting per
            // frame makes the dump deterministic; the per-phase stats then
            // measure full GPU frame time, which is the honest scaling
            // number anyway.
            if (!interactive) {
                engine.get_renderer().wait_for_completion();
            }
            if (interactive) {
                // HUD: phase + live FPS (rolling mean of the last 30 frames).
                double recent = 0.0;
                size_t nrecent = std::min<size_t>(30, frame_ms.size());
                for (size_t k = frame_ms.size() - nrecent; k < frame_ms.size(); ++k)
                    recent += frame_ms[k];
                double fps = nrecent ? 1000.0 * nrecent / recent : 0.0;
                if (auto* ui = engine.get_ui_system()) {
                    char hud[160];
                    snprintf(hud, sizeof(hud),
                             "phase %d/%d | lights %d | particles %d | %.1f FPS",
                             phase, num_lights, (int)light_ids.size(),
                             // LIVE count, not the initial grid: with a spawn
                             // ramp this is the variable being watched.
                             (int)ps.count(), fps);
                    ui->draw_text(10, 10, hud, 255, 255, 160);
                    ui->draw_text(10, 34, "SPACE: next phase | ESC: exit", 150, 150, 150);
                }
            }
            engine.present();
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (f >= WARMUP) frame_ms.push_back(ms);
            total_frame++;

            if (interactive) {
                const auto& input = engine.get_input_system();
                static bool space_was = false;
                bool space_now = input.get_input_state().keys[GLFW_KEY_SPACE];
                if (space_now && !space_was) { space_was = space_now; break; }
                space_was = space_now;
                if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                    printf("\n[MLP] ESC — exiting\n");
                    return true;
                }
            }

            // MLP_DUMP_TWO=1: dump DUMP_FRAME and DUMP_FRAME+1 (suffixes _a/_b)
            // to discriminate a completed-slot frame-offset race from true
            // render nondeterminism (cross-compare _a vs _b across runs).
            const bool dump_two = std::getenv("MLP_DUMP_TWO") != nullptr;
            const bool at_dump = (f == WARMUP + DUMP_FRAME);
            const bool at_dump2 = dump_two && (f == WARMUP + DUMP_FRAME + 1);
            if (!interactive && (at_dump || at_dump2)) {
                engine.get_renderer().wait_for_completion();
                int w = engine.get_render_buffer().width();
                int h = engine.get_render_buffer().height();
                std::vector<uint32_t> px((size_t)w * h);
                if (engine.read_latest_framebuffer(px.data(), w, h)) {
                    std::string suffix = dump_two ? (at_dump ? "_a" : "_b") : "";
                    dump_ppm(dump_dir + "/mlp_phase" + std::to_string(phase) + suffix + ".ppm",
                             px, w, h);
                }
                // MLP_SSDO_PROBE="x0,y0,x1,y1": scan the SSDO buffer over a
                // rect and print channel ranges (diagnostic; see
                // read_ssdo_debug).
                if (const char* probe = std::getenv("MLP_SSDO_PROBE")) {
                    int x0, y0, x1, y1;
                    if (sscanf(probe, "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4) {
                        float mn[4] = {1e30f, 1e30f, 1e30f, 1e30f};
                        float mx[4] = {-1e30f, -1e30f, -1e30f, -1e30f};
                        double sum[4] = {0, 0, 0, 0};
                        long n = 0, nonzero_r = 0;
                        for (int y = y0; y <= y1; y += 2) {
                            for (int x = x0; x <= x1; x += 2) {
                                float v[4];
                                if (engine.get_render_pipeline().read_ssdo_debug(
                                        x, y, v[0], v[1], v[2], v[3])) {
                                    for (int c = 0; c < 4; c++) {
                                        mn[c] = std::min(mn[c], v[c]);
                                        mx[c] = std::max(mx[c], v[c]);
                                        sum[c] += v[c];
                                    }
                                    if (v[0] > 0.0f) nonzero_r++;
                                    n++;
                                }
                            }
                        }
                        if (n > 0) {
                            printf("[MLP_SSDO_PROBE] phase=%d n=%ld nonzero_r=%ld\n",
                                   phase, n, nonzero_r);
                            const char* ch = "RGBA";
                            for (int c = 0; c < 4; c++) {
                                printf("[MLP_SSDO_PROBE]   %c min=%.6g max=%.6g mean=%.6g\n",
                                       ch[c], mn[c], mx[c], sum[c] / n);
                            }
                        }
                    }
                }
            }
        }

        std::vector<double> sorted = frame_ms;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double m : sorted) sum += m;
        double mean = sum / sorted.size();
        double median = sorted[sorted.size() / 2];
        double p90 = sorted[(size_t)(sorted.size() * 0.9)];
        // Live counts, not the initial grid: with a spawn ramp both the
        // light count and the particle count move during the run, and
        // reporting the stale grid size makes the curve unreadable.
        printf("[MLP] phase=%d lights=%d particles=%d mean=%.2fms (%.1f FPS) median=%.2fms p90=%.2fms\n",
               phase, (int)light_ids.size(), (int)ps.count(),
               mean, 1000.0 / mean, median, p90);
    }

    printf("\n=== PROGRESSIVE MULTI-LIGHT COMPLETE (%d phases) ===\n", num_lights);
    return all_phases_ran;
}
