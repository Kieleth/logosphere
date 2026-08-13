// =============================================================================
// SHADOW EDGE QUANTIZATION — catch staircase aliasing on hard shadow edges
// =============================================================================
// 2026-07-17: at SHADOW_RESOLUTION_SCALE = 0.5 the user's eye caught 2-pixel
// staircase treads on a hard shadow silhouette (SSGI visual suite case 1 —
// soft penumbra masks the sampling everywhere else, hard edges cannot be
// masked). This test measures that effect mechanically so no configuration
// ships it silently again.
//
// Method: render case 1's scene headless through the real GPU pipeline
// (point light + pillar on a flat floor), read the framebuffer back, walk
// the shadow tip's diagonal silhouette (leftmost shadow x per row), fit a
// line through the edge points, and measure the deviation of the actual
// edge from that line:
//   - full-res shadow sampling: edge quantized to 1 framebuffer pixel,
//     RMS residual ~0.3 px
//   - half-res sampling: edge quantized to 2-px treads, RMS ~0.6 px
// The assert threshold sits between the two measured populations.
//
// Run: ./build/logosphere-tests --test test_shadow_edge_quantization --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdint>

namespace {

void add_box(Engine& engine, float x, float y, float z,
             float w, float h, float t, float r, float g, float b) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = w; p.height = h; p.thickness = t;
    p.size = std::max(w, std::max(h, t));
    p.r = r; p.g = g; p.b = b; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    int id = engine.add_particle(p);
    auto view = engine.get_particle_system().lock_particles_for_write();
    view[id].solver_mode = ParticleSolverMode::KINEMATIC;
    view[id].owner = ParticleOwner::DYNAMICS;
    view[id].is_at_rest = true;
}

inline float lum(uint32_t bgra) {
    float b = float(bgra & 0xFF);
    float g = float((bgra >> 8) & 0xFF);
    float r = float((bgra >> 16) & 0xFF);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

} // namespace

bool test_shadow_edge_quantization() {
    printf("\n=== Shadow Edge Quantization (staircase catcher) ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "shadow edge quantization";
    cfg.enable_chat_window = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    // SSGI-case-1 style scene scaled up so the silhouette spans hundreds
    // of framebuffer pixels at the default camera: big floor, tall
    // pillar, low offset light — a long hard shadow with straight
    // diagonal edges (world-straight lines stay screen-straight under
    // the isometric projection, which is what the line fit relies on).
    // The slab RESTS ON the turtle (bottom at z=0); pillar and light ride
    // 0.5 m up with it, so the scene is the pre-strict-turtle one rigidly
    // translated — same relative geometry, no body below the world floor.
    add_box(engine, 0.0f, 0.0f, 0.25f, 30.0f, 30.0f, 0.5f, 0.8f, 0.8f, 0.8f); // floor slab
    add_box(engine, 0.0f, 0.0f, 3.5f, 1.0f, 1.0f, 6.0f, 0.9f, 0.9f, 0.9f);    // tall pillar (bright: must not read as shadow)
    engine.get_particle_system().queue_light(4.0f, -5.0f, 7.5f, 800000.0f, 80.0f,
                                             1.0f, 1.0f, 1.0f);
    engine.get_particle_system().flush_pending_particles();

    // Let the pipeline settle (BVH, shaders, a few full frames).
    for (int i = 0; i < 8; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();
        engine.get_renderer().wait_for_completion();
    }

    int w = engine.get_render_buffer().width();
    int h = engine.get_render_buffer().height();
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    if (!engine.read_latest_framebuffer(pixels.data(), w, h)) {
        printf("  ERROR: framebuffer readback failed\n");
        return false;
    }

    // Debug frame dump for calibrating the scene/scan (harmless to keep:
    // one small file per run, invaluable when the catcher errors out).
    {
        FILE* f = fopen("/tmp/shadow_edge_debug.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; ++i) {
                uint32_t p = pixels[i];
                unsigned char rgb[3] = {
                    (unsigned char)((p >> 16) & 0xFF),
                    (unsigned char)((p >> 8) & 0xFF),
                    (unsigned char)(p & 0xFF) };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }

    // Luminance threshold from the frame itself: midpoint between the
    // darkest and brightest common values (shadow core vs lit floor).
    float lo = 255.0f, hi = 0.0f;
    for (int y = h / 4; y < 3 * h / 4; y += 3) {
        for (int x = w / 8; x < 7 * w / 8; x += 3) {
            float l = lum(pixels[static_cast<size_t>(y) * w + x]);
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
    }
    const float thresh = 0.5f * (lo + hi);
    printf("  luminance range %.1f..%.1f, threshold %.1f\n", lo, hi, thresh);

    // Shadow mask: per row, the first LIT->DARK transition scanning left
    // to right — the true silhouette entry. (A plain "first dark pixel"
    // scan latches onto the dark vignette at the frame border.) The
    // shadow tip is the row with the global minimum entry x; its
    // neighbors hold the diagonal silhouette this test measures.
    std::vector<int> edge_x(h, -1);
    int tip_row = -1, tip_x = w;
    const int x_lo = w / 16, x_hi = (w * 5) / 8;
    for (int y = h / 5; y < (h * 4) / 5; ++y) {
        int lit_run = 0;
        for (int x = x_lo; x < x_hi; ++x) {
            float l = lum(pixels[static_cast<size_t>(y) * w + x]);
            if (l >= thresh) {
                lit_run++;
            } else if (lit_run >= 8) {
                // Genuinely entered shadow from lit floor: the silhouette.
                edge_x[y] = x;
                if (x < tip_x) { tip_x = x; tip_row = y; }
                break;
            } else {
                lit_run = 0;  // still in the dark background: keep scanning
            }
        }
    }
    if (tip_row < 0) {
        printf("  ERROR: no shadow found in frame\n");
        return false;
    }
    printf("  shadow tip at (%d, %d)\n", tip_x, tip_row);

    // Collect every monotonic bounded-step run of the row-wise edge, fit
    // a line to each DIAGONAL run (near-vertical edges cannot show
    // x-quantization; near-horizontal ones break the per-row bound), and
    // report the WORST run — a staircase on any silhouette is a failure.
    struct Run { int lo, hi; };
    std::vector<Run> runs;
    {
        int run_lo = -1, run_sign = 0;
        auto close_run = [&](int hi) {
            if (run_lo >= 0 && hi - run_lo >= 24) runs.push_back({run_lo, hi});
            run_lo = -1; run_sign = 0;
        };
        for (int y = h / 5 + 1; y < (h * 4) / 5; ++y) {
            if (edge_x[y] < 0 || edge_x[y - 1] < 0) { close_run(y - 1); continue; }
            int dx = edge_x[y] - edge_x[y - 1];
            int sign = (dx > 0) - (dx < 0);
            bool ok_step = std::abs(dx) <= 6 &&
                           (sign == 0 || run_sign == 0 || sign == run_sign);
            if (!ok_step) { close_run(y - 1); continue; }
            if (run_lo < 0) run_lo = y - 1;
            if (sign != 0 && run_sign == 0) run_sign = sign;
        }
        close_run((h * 4) / 5 - 1);
    }

    float worst_rms = -1.0f, worst_slope = 0, worst_max_dev = 0, worst_max_step = 0;
    size_t worst_n = 0;
    int diagonal_runs = 0;
    for (const Run& run : runs) {
        std::vector<float> ys, xs;
        for (int y = run.lo; y <= run.hi; ++y) {
            ys.push_back((float)y);
            xs.push_back((float)edge_x[y]);
        }
        float n = (float)ys.size(), sy = 0, sx = 0, syy = 0, sxy = 0;
        for (size_t i = 0; i < ys.size(); ++i) {
            sy += ys[i]; sx += xs[i]; syy += ys[i] * ys[i]; sxy += ys[i] * xs[i];
        }
        float denom = n * syy - sy * sy;
        if (std::fabs(denom) < 1e-3f) continue;
        float a = (n * sxy - sy * sx) / denom;
        float b = (sx - a * sy) / n;
        if (std::fabs(a) < 0.5f || std::fabs(a) > 6.0f) continue;  // not a diagonal
        diagonal_runs++;

        float rss = 0, max_dev = 0, max_step = 0;
        for (size_t i = 0; i < ys.size(); ++i) {
            float res = xs[i] - (a * ys[i] + b);
            rss += res * res;
            max_dev = std::max(max_dev, std::fabs(res));
            if (i > 0) max_step = std::max(max_step, std::fabs(xs[i] - xs[i - 1]));
        }
        float rms = std::sqrt(rss / n);
        printf("  run rows %d..%d: %zu pts, slope %.2f px/row, RMS %.3f, max dev %.3f, max step %.1f\n",
               run.lo, run.hi, ys.size(), a, rms, max_dev, max_step);
        if (rms > worst_rms) {
            worst_rms = rms; worst_slope = a; worst_max_dev = max_dev;
            worst_max_step = max_step; worst_n = ys.size();
        }
    }
    if (diagonal_runs == 0) {
        printf("  ERROR: no diagonal silhouette run found (%zu runs total)\n", runs.size());
        return false;
    }
    float rms = worst_rms;
    printf("  worst diagonal: %zu pts, slope %.2f px/row\n", worst_n, worst_slope);
    printf("  staircase metrics: RMS deviation %.3f px, max deviation %.3f px, max step %.1f px\n",
           rms, worst_max_dev, worst_max_step);

    // Calibrated 2026-07-17 (1600x1200 render): full-res shadows measure
    // RMS ~0.3 px; half-res (SHADOW_RESOLUTION_SCALE=0.5) ~0.6 px with
    // 2 px treads. Bar sits between the populations.
    const float RMS_BAR = 0.45f;
    bool ok = (rms <= RMS_BAR);
    printf("  %s: shadow edge RMS %.3f <= %.2f px (staircase %s)\n",
           ok ? "PASS" : "FAIL", rms, RMS_BAR,
           ok ? "not detectable" : "DETECTED - hard shadow edges are quantized");
    return ok;
}
