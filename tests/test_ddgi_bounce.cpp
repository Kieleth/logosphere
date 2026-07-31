// ============================================================================
// DDGI BOUNCE TEST
// ============================================================================
// Verifies DDGI (Dynamic Diffuse Global Illumination) produces visible
// colored bounce from nearby surfaces.
//
// Scene: white floor + red obelisk + side light.
// Expected: floor near the obelisk has measurable red tint from probe-based
// indirect lighting. Probes near the obelisk accumulate red irradiance from
// the lit obelisk face, and floor pixels query those probes.
//
// Run:
//   ./logosphere-tests --test test_ddgi_bounce
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct DPx { uint8_t b, g, r, a; };

static DPx dpx(const uint8_t* d, int w, int x, int y) {
    DPx p; int i = (y * w + x) * 4;
    p.b = d[i]; p.g = d[i+1]; p.r = d[i+2]; p.a = d[i+3]; return p;
}

static void ddump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(d[i*4+2], f); fputc(d[i*4+1], f); fputc(d[i*4+0], f);
    }
    fclose(f);
}

bool test_ddgi_bounce(TestContext& ctx) {
    printf("\n=== DDGI Bounce Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: white floor + red obelisk + side light
    // Same as test_gi_bounce for direct comparison.
    // ================================================================
    ctx.clear_particles();

    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
    floor_p.width = 20; floor_p.height = 20; floor_p.thickness = 0.1f;
    floor_p.r = 0.9f; floor_p.g = 0.9f; floor_p.b = 0.9f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor_p.owner = ParticleOwner::STATIC;
    floor_p.is_at_rest = true;
    ctx.add_test_particle(floor_p);

    Particle obelisk = {};
    obelisk.shape = ParticleShape::BOX;
    obelisk.x = 0.0f; obelisk.y = 0.0f; obelisk.z = 1.0f;
    obelisk.width = 0.4f; obelisk.height = 0.4f; obelisk.thickness = 2.0f;
    obelisk.r = 1.0f; obelisk.g = 0.1f; obelisk.b = 0.1f; obelisk.a = 1.0f;
    obelisk.SetMaterial(Materials::Type::STONE);
    obelisk.owner = ParticleOwner::STATIC;
    obelisk.is_at_rest = true;
    ctx.add_test_particle(obelisk);

    ps.queue_light(-5.0f, 0.0f, 3.0f, 500000.0f, 80.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene: white floor + red obelisk + side light\n");

    // Render 30 frames (DDGI converges in ~10 frames with 3% hysteresis)
    for (int i = 0; i < 30; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> frame(buf_size);
    std::memcpy(frame.data(), rs.get_render_buffer().get_native_data(), buf_size);
    const uint8_t* data = frame.data();
    ddump(data, w, h, "/tmp/ddgi_bounce.ppm");
    printf("  Dumped /tmp/ddgi_bounce.ppm\n");

    // ================================================================
    // Find obelisk in screen space
    // ================================================================
    int ob_cx = w / 2, ob_cy = h / 2;
    for (int y = h/4; y < 3*h/4; y += 2) {
        for (int x = w/4; x < 3*w/4; x += 2) {
            auto px = dpx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            if (px.r > 100 && px.r > px.g * 3 && px.r > px.b * 3) {
                ob_cx = x; ob_cy = y;
                break;
            }
        }
    }
    printf("  Obelisk at: (%d,%d)\n", ob_cx, ob_cy);

    // ================================================================
    // Sample near vs far floor
    // ================================================================
    float near_r = 0, near_g = 0, near_b = 0;
    int near_n = 0;
    float far_r = 0, far_g = 0, far_b = 0;
    int far_n = 0;

    for (int y = 20; y < h - 20; y++) {
        for (int x = 20; x < w - 20; x++) {
            auto px = dpx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float bri = (px.r + px.g + px.b) / 3.0f;
            if (bri < 2 || bri > 240) continue;
            if (px.r > 60 && px.r > px.g * 3) continue; // skip obelisk

            int dx = x - ob_cx;
            int dy = y - ob_cy;
            float dist = std::sqrt((float)(dx*dx + dy*dy));

            if (dist > 5 && dist < 30) {
                near_r += px.r; near_g += px.g; near_b += px.b;
                near_n++;
            }
            if (dist > 150 && bri > 50) {
                far_r += px.r; far_g += px.g; far_b += px.b;
                far_n++;
            }
        }
    }

    if (near_n > 0) { near_r /= near_n; near_g /= near_n; near_b /= near_n; }
    if (far_n > 0) { far_r /= far_n; far_g /= far_n; far_b /= far_n; }

    printf("\n  Near obelisk (n=%d): R=%.1f G=%.1f B=%.1f\n", near_n, near_r, near_g, near_b);
    printf("  Far floor (n=%d):    R=%.1f G=%.1f B=%.1f\n", far_n, far_r, far_g, far_b);

    float near_ratio = (near_g + near_b > 0.1f) ? near_r / ((near_g + near_b) / 2.0f) : 1.0f;
    float far_ratio = (far_g + far_b > 0.1f) ? far_r / ((far_g + far_b) / 2.0f) : 1.0f;
    float tint_excess = near_ratio - far_ratio;

    printf("  Near R/(G+B)/2: %.3f\n", near_ratio);
    printf("  Far  R/(G+B)/2: %.3f\n", far_ratio);
    printf("  Red tint excess: %.3f\n", tint_excess);

    // DDGI probes should produce at least 2% red tint excess
    bool pass = tint_excess > 0.02f;
    printf("\n%s: DDGI bounce (tint %.3f, threshold > 0.02)\n",
           pass ? "PASS" : "FAIL", tint_excess);
    return pass;
}
