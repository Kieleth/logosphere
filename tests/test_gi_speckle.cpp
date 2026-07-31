// ============================================================================
// GI SPECKLE ARTIFACT TEST
// ============================================================================
// Detects GI speckle caused by temporal accumulation failing to converge.
//
// Root cause: prev_shadow_camera_ not updated in Metal RT shadow path,
// causing camera delta to be huge every frame, triggering perpetual
// temporal reset. gi_fade stays at 0.125, leaking noisy single-frame GI.
//
// Detection: render 60 frames of a static scene, then compare two
// consecutive frames. If temporal converged, frames are identical.
// If temporal resets every frame, frames differ (different Halton seeds).
//
// Also checks absolute noise level: converged GI should produce smooth
// indirect lighting with no visible speckle.
//
// Run:
//   ./logosphere-tests --test test_gi_speckle
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct SpPx { uint8_t b, g, r, a; };

static SpPx sppx(const uint8_t* d, int w, int x, int y) {
    SpPx p; int i = (y * w + x) * 4;
    p.b = d[i]; p.g = d[i+1]; p.r = d[i+2]; p.a = d[i+3]; return p;
}
static float spbri(SpPx p) { return (p.r + p.g + p.b) / 3.0f; }

static void spdump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(d[i*4+2], f); fputc(d[i*4+1], f); fputc(d[i*4+0], f);
    }
    fclose(f);
}

bool test_gi_speckle(TestContext& ctx) {
    printf("\n=== GI Speckle Artifact Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: green floor + rocks + humanoid + light
    // Matches Eden layout to reproduce the same rendering conditions.
    // ================================================================
    ctx.clear_particles();

    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
    floor_p.width = 40; floor_p.height = 40; floor_p.thickness = 0.1f;
    floor_p.r = 0.3f; floor_p.g = 0.5f; floor_p.b = 0.2f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor_p.owner = ParticleOwner::STATIC;
    floor_p.is_at_rest = true;
    ctx.add_test_particle(floor_p);

    // Rocks (GI bounce surfaces)
    float rock_spots[][4] = {
        { 2.0f, -2.0f, 0.15f, 0.3f},
        {-1.0f,  3.0f, 0.15f, 0.25f},
        { 4.0f,  1.0f, 0.15f, 0.35f},
    };
    for (auto& rs2 : rock_spots) {
        Particle rock = {};
        rock.shape = ParticleShape::BOX;
        rock.x = rs2[0]; rock.y = rs2[1]; rock.z = rs2[2];
        rock.width = rs2[3]; rock.height = rs2[3] * 0.7f; rock.thickness = rs2[3] * 0.5f;
        rock.r = 0.5f; rock.g = 0.45f; rock.b = 0.4f; rock.a = 1.0f;
        rock.SetMaterial(Materials::Type::STONE);
        rock.owner = ParticleOwner::STATIC;
        rock.is_at_rest = true;
        ctx.add_test_particle(rock);
    }

    // Humanoid-sized box
    Particle humanoid = {};
    humanoid.shape = ParticleShape::BOX;
    humanoid.x = 0.0f; humanoid.y = 0.0f; humanoid.z = 0.9f;
    humanoid.width = 0.4f; humanoid.height = 0.3f; humanoid.thickness = 1.8f;
    humanoid.r = 0.6f; humanoid.g = 0.4f; humanoid.b = 0.6f; humanoid.a = 1.0f;
    humanoid.SetMaterial(Materials::Type::STONE);
    humanoid.owner = ParticleOwner::STATIC;
    humanoid.is_at_rest = true;
    ctx.add_test_particle(humanoid);

    // Streetlight
    ps.queue_light(-6.0f, 6.0f, 5.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

    printf("  Scene: green floor + 3 rocks + humanoid + light\n");

    // ================================================================
    // Render 70 frames: temporal should converge by frame 64
    // (GI_CONVERGENCE_FRAMES). After that, gi_temporal is frozen.
    // ================================================================
    for (int i = 0; i < 70; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    // Capture frame A
    std::vector<uint8_t> frame_a(buf_size);
    std::memcpy(frame_a.data(), rs.get_render_buffer().get_native_data(), buf_size);
    spdump(frame_a.data(), w, h, "/tmp/gi_speckle_a.ppm");

    // Render one more frame, capture B
    engine.update(dt); engine.render();
    rs.get_render_pipeline().wait_for_gpu_completion();
    std::vector<uint8_t> frame_b(buf_size);
    std::memcpy(frame_b.data(), rs.get_render_buffer().get_native_data(), buf_size);
    spdump(frame_b.data(), w, h, "/tmp/gi_speckle_b.ppm");

    printf("  Dumped /tmp/gi_speckle_a.ppm and /tmp/gi_speckle_b.ppm\n");

    // ================================================================
    // Test 1: Frame-to-frame stability (temporal convergence check)
    // If temporal is converged, consecutive frames of a static scene
    // are identical. If temporal resets every frame, Halton seeds differ
    // and frames diverge.
    // ================================================================
    printf("\n  Test 1: Temporal convergence (frame stability)\n");

    int changed = 0;
    int total = 0;
    float max_diff = 0;

    for (int y = 10; y < h - 10; y++) {
        for (int x = 10; x < w - 10; x++) {
            auto pa = sppx(frame_a.data(), w, x, y);
            auto pb = sppx(frame_b.data(), w, x, y);
            if (pa.r == 10 && pa.g == 10 && pa.b == 15) continue;
            total++;
            float da = spbri(pa);
            float db = spbri(pb);
            float d = std::abs(da - db);
            if (d > 0.5f) {
                changed++;
                if (d > max_diff) max_diff = d;
            }
        }
    }

    float changed_pct = total > 0 ? 100.0f * changed / total : 0;
    printf("  Pixels: %d, changed: %d (%.2f%%), max diff: %.1f\n",
           total, changed, changed_pct, max_diff);

    // ================================================================
    // Test 2: Per-pixel noise on floor (speckle detection)
    // Check if isolated pixels differ from neighbors.
    // ================================================================
    printf("\n  Test 2: Floor speckle (pixel noise)\n");

    int speckle = 0;
    int floor_px = 0;

    for (int y = 20; y < h - 20; y++) {
        for (int x = 20; x < w - 20; x++) {
            auto px = sppx(frame_a.data(), w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float b = spbri(px);
            if (b < 5.0f || b > 240.0f) continue;

            floor_px++;

            float n[4]; int nn = 0;
            int offsets[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            for (auto& off : offsets) {
                int nx = x + off[0], ny = y + off[1];
                auto np = sppx(frame_a.data(), w, nx, ny);
                if (np.r == 10 && np.g == 10 && np.b == 15) continue;
                n[nn++] = spbri(np);
            }
            if (nn < 3) continue;

            float n_avg = 0;
            for (int i = 0; i < nn; i++) n_avg += n[i];
            n_avg /= nn;

            float n_var = 0;
            for (int i = 0; i < nn; i++) n_var += (n[i] - n_avg) * (n[i] - n_avg);
            n_var /= nn;

            float diff = std::abs(b - n_avg);
            if (diff > 1.0f && n_var < 4.0f) {
                speckle++;
                if (speckle <= 3) {
                    printf("    SPECKLE at (%d,%d): bri=%.1f neighbors=%.1f diff=%.1f\n",
                           x, y, b, n_avg, diff);
                }
            }
        }
    }

    float speckle_pct = floor_px > 0 ? 100.0f * speckle / floor_px : 0;
    printf("  Floor pixels: %d, speckles: %d (%.2f%%)\n",
           floor_px, speckle, speckle_pct);

    // ================================================================
    // Verdict
    // ================================================================
    bool converged = changed_pct < 0.1f;
    bool clean = speckle_pct < 0.5f;  // Full-res GI has ~0.3% intrinsic noise (1024 rays)

    printf("\n  Temporal convergence: %s (%.2f%% changed, threshold 0.1%%)\n",
           converged ? "PASS" : "FAIL", changed_pct);
    printf("  Floor speckle: %s (%.2f%%, threshold 0.5%%)\n",
           clean ? "PASS" : "FAIL", speckle_pct);

    bool pass = converged && clean;
    printf("\n%s: GI speckle\n", pass ? "PASS" : "FAIL");
    return pass;
}
