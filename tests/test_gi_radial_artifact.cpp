// ============================================================================
// GI RADIAL ARTIFACT TEST
// ============================================================================
// Detects GI (global illumination) noise leaking into the final image when
// temporal convergence hasn't been reached.
//
// Root cause: with always-reset temporal buffer (for moving lights),
// frame_index is always 1. If the gi_fade formula lets frame 1 through,
// noisy single-frame GI contaminates the output.
//
// Detection: Place a bright RED wall next to a grey floor. GI bounce from
// the wall should tint the floor red. With gi_fade=0, no GI → no tint.
// With gi_fade > 0, noisy GI → detectable red channel bias near the wall.
//
// Run:
//   ./logosphere-tests --test test_gi_radial_artifact
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct GiPx { uint8_t b, g, r, a; };

static GiPx gipx(const uint8_t* d, int w, int x, int y) {
    GiPx p; int i = (y * w + x) * 4;
    p.b = d[i]; p.g = d[i+1]; p.r = d[i+2]; p.a = d[i+3]; return p;
}
static float gibri(GiPx p) { return (p.r + p.g + p.b) / 3.0f; }

static void gidump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(d[i*4+2], f); fputc(d[i*4+1], f); fputc(d[i*4+0], f);
    }
    fclose(f);
}

bool test_gi_radial_artifact(TestContext& ctx) {
    printf("\n=== GI Radial Artifact Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene A: grey floor + light + bright RED wall
    // If GI leaks, floor near wall gets red tint.
    // ================================================================
    ctx.clear_particles();

    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
    floor_p.width = 30; floor_p.height = 30; floor_p.thickness = 0.1f;
    floor_p.r = 0.5f; floor_p.g = 0.5f; floor_p.b = 0.5f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor_p.owner = ParticleOwner::STATIC;
    floor_p.is_at_rest = true;
    ctx.add_test_particle(floor_p);

    // Bright red wall, close to floor center
    Particle wall = {};
    wall.shape = ParticleShape::BOX;
    wall.x = 2.0f; wall.y = 0.0f; wall.z = 1.5f;
    wall.width = 0.2f; wall.height = 4.0f; wall.thickness = 3.0f;
    wall.r = 1.0f; wall.g = 0.1f; wall.b = 0.1f; wall.a = 1.0f;
    wall.SetMaterial(Materials::Type::STONE);
    wall.owner = ParticleOwner::STATIC;
    wall.is_at_rest = true;
    ctx.add_test_particle(wall);

    // Light illuminating the wall from the left
    ps.queue_light(-4.0f, 0.0f, 5.0f, 500000.0f, 100.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene A: grey floor + red wall + light (GI bounce test)\n");

    for (int i = 0; i < 30; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> with_wall(buf_size);
    std::memcpy(with_wall.data(), rs.get_render_buffer().get_native_data(), buf_size);
    gidump(with_wall.data(), w, h, "/tmp/gi_with_wall.ppm");

    // ================================================================
    // Scene B: same floor + light, NO wall
    // ================================================================
    ctx.clear_particles();
    ctx.add_test_particle(floor_p);
    ps.queue_light(-4.0f, 0.0f, 5.0f, 500000.0f, 100.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene B: grey floor + light (no wall, reference)\n");

    for (int i = 0; i < 30; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> no_wall(buf_size);
    std::memcpy(no_wall.data(), rs.get_render_buffer().get_native_data(), buf_size);
    gidump(no_wall.data(), w, h, "/tmp/gi_no_wall.ppm");

    printf("  Dumped /tmp/gi_with_wall.ppm and /tmp/gi_no_wall.ppm\n");

    // ================================================================
    // Method 1: Red channel bias from GI bounce
    // ================================================================
    // Near the red wall, floor pixels in scene A should have the same
    // color as scene B if GI is suppressed. If GI leaks, scene A's
    // floor pixels near the wall will have higher R relative to G,B.

    printf("\n  Method 1: Red channel bias from GI bounce\n");

    int red_bias_pixels = 0;
    int comparable_pixels = 0;
    float max_red_bias = 0;

    // Scan floor pixels in the region near the wall (screen center area)
    for (int y = h/4; y < 3*h/4; y += 2) {
        for (int x = w/4; x < 3*w/4; x += 2) {
            auto pa = gipx(with_wall.data(), w, x, y);
            auto pb = gipx(no_wall.data(), w, x, y);

            // Skip background
            if (pa.r == 10 && pa.g == 10 && pa.b == 15) continue;
            if (pb.r == 10 && pb.g == 10 && pb.b == 15) continue;

            float bri_a = gibri(pa);
            float bri_b = gibri(pb);
            if (bri_a < 5.0f || bri_b < 5.0f) continue;

            // Skip wall surface pixels (very red in scene A)
            if (pa.r > pa.g * 2) continue;

            // Skip pixels that differ a lot (wall shadow region)
            if (std::abs(bri_a - bri_b) > 30.0f) continue;

            comparable_pixels++;

            // Red channel bias: (R_a - R_b) relative to overall difference
            float r_diff = (float)pa.r - (float)pb.r;
            float g_diff = (float)pa.g - (float)pb.g;
            float b_diff = (float)pa.b - (float)pb.b;

            // GI bounce from red wall: R increases more than G,B
            float red_excess = r_diff - (g_diff + b_diff) / 2.0f;

            if (red_excess > 2.0f) {
                red_bias_pixels++;
                if (red_excess > max_red_bias) max_red_bias = red_excess;
                if (red_bias_pixels <= 3) {
                    printf("    RED BIAS at (%d,%d): R_diff=%.0f G_diff=%.0f B_diff=%.0f excess=%.1f\n",
                           x, y, r_diff, g_diff, b_diff, red_excess);
                }
            }
        }
    }

    float red_pct = comparable_pixels > 0 ? 100.0f * red_bias_pixels / comparable_pixels : 0;
    printf("  Comparable pixels: %d, red-biased: %d (%.2f%%), max excess: %.1f\n",
           comparable_pixels, red_bias_pixels, red_pct, max_red_bias);

    // ================================================================
    // Method 2: Frame-to-frame stability (static scene)
    // ================================================================
    // Even with the wall, consecutive frames should be identical.

    printf("\n  Method 2: Frame-to-frame stability\n");

    // Render one more frame of scene B (already set up)
    engine.update(dt); engine.render();
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> frame_c(buf_size);
    std::memcpy(frame_c.data(), rs.get_render_buffer().get_native_data(), buf_size);

    int changed_pixels = 0;
    int floor_pixels = 0;

    for (int y = 10; y < h - 10; y++) {
        for (int x = 10; x < w - 10; x++) {
            auto pa = gipx(no_wall.data(), w, x, y);
            auto pb = gipx(frame_c.data(), w, x, y);
            if (pa.r == 10 && pa.g == 10 && pa.b == 15) continue;
            if (pb.r == 10 && pb.g == 10 && pb.b == 15) continue;

            floor_pixels++;
            if (pa.r != pb.r || pa.g != pb.g || pa.b != pb.b) {
                changed_pixels++;
            }
        }
    }

    float changed_pct = floor_pixels > 0 ? 100.0f * changed_pixels / floor_pixels : 0;
    printf("  Floor pixels: %d, changed: %d (%.2f%%)\n",
           floor_pixels, changed_pixels, changed_pct);

    // ================================================================
    // Method 3: Per-pixel noise in flat regions
    // ================================================================
    // On the grey floor away from the wall, brightness should be smooth.
    // GI noise creates random per-pixel variations.

    printf("\n  Method 3: Per-pixel noise in flat regions\n");

    int noisy_count = 0;
    int flat_pixels = 0;

    for (int y = 30; y < h - 30; y += 2) {
        for (int x = 30; x < w - 30; x += 2) {
            auto px = gipx(with_wall.data(), w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float b = gibri(px);
            if (b < 10.0f || b > 240.0f) continue;
            // Skip pixels near wall
            if (px.r > px.g * 1.5f) continue;

            // Compare with 4 neighbors at distance 1
            float center = gibri(px);
            int offsets[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            float n_sum = 0; int nn = 0;
            for (auto& off : offsets) {
                int nx = x + off[0], ny = y + off[1];
                auto np = gipx(with_wall.data(), w, nx, ny);
                if (np.r == 10 && np.g == 10 && np.b == 15) continue;
                n_sum += gibri(np);
                nn++;
            }
            if (nn < 3) continue;

            flat_pixels++;
            float n_avg = n_sum / nn;
            // On smooth gradient, adjacent pixels differ by < 1 unit
            if (std::abs(center - n_avg) > 2.0f) {
                noisy_count++;
            }
        }
    }

    float noise_pct = flat_pixels > 0 ? 100.0f * noisy_count / flat_pixels : 0;
    printf("  Flat region pixels: %d, noisy: %d (%.2f%%)\n",
           flat_pixels, noisy_count, noise_pct);

    // ================================================================
    // Verdict
    // ================================================================
    // GI suppressed: no red bias from wall bounce
    bool gi_suppressed = red_pct < 0.5f;
    // Frame stable: static scene identical between frames
    bool stable = changed_pct < 0.1f;
    // Low noise: smooth brightness on flat surfaces
    // Baseline rendering noise (edge quantization, tone mapping) is ~0.6%.
    // GI noise at high intensity would push this above 2%.
    bool clean = noise_pct < 2.0f;

    printf("\n  GI suppressed: %s (%.2f%% red-biased, threshold 0.5%%)\n",
           gi_suppressed ? "PASS" : "FAIL", red_pct);
    printf("  Frame stability: %s (%.2f%% changed, threshold 0.1%%)\n",
           stable ? "PASS" : "FAIL", changed_pct);
    printf("  Surface noise: %s (%.2f%% noisy, threshold 2%%)\n",
           clean ? "PASS" : "FAIL", noise_pct);

    bool pass = gi_suppressed && stable && clean;
    printf("\n%s: GI radial artifact\n", pass ? "PASS" : "FAIL");
    return pass;
}
