// ============================================================================
// GI BOUNCE (REFLECTION) TEST
// ============================================================================
// Minimal scene: white floor + red obelisk + light.
// The red obelisk should bounce red-tinted light onto the nearby white floor.
// This is the fundamental GI effect: colored indirect illumination.
//
// Detection: floor pixels near the obelisk (on the lit side) should have
// a measurable red tint compared to floor pixels far away.
//
// Run:
//   ./logosphere-tests --test test_gi_bounce
//   INTERACTIVE=1 ./logosphere-tests --test test_gi_bounce
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct BPx { uint8_t b, g, r, a; };

static BPx bpx(const uint8_t* d, int w, int x, int y) {
    BPx p; int i = (y * w + x) * 4;
    p.b = d[i]; p.g = d[i+1]; p.r = d[i+2]; p.a = d[i+3]; return p;
}

static void bdump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(d[i*4+2], f); fputc(d[i*4+1], f); fputc(d[i*4+0], f);
    }
    fclose(f);
}

bool test_gi_bounce(TestContext& ctx) {
    printf("\n=== GI Bounce (Reflection) Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: white floor + red obelisk + light
    // ================================================================
    ctx.clear_particles();

    // White floor
    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
    floor_p.width = 20; floor_p.height = 20; floor_p.thickness = 0.1f;
    floor_p.r = 0.9f; floor_p.g = 0.9f; floor_p.b = 0.9f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor_p.owner = ParticleOwner::STATIC;
    floor_p.is_at_rest = true;
    ctx.add_test_particle(floor_p);

    // Red obelisk (tall, bright red)
    Particle obelisk = {};
    obelisk.shape = ParticleShape::BOX;
    // ON the floor, not 0.1 m into it: the slab's top is 0.10 and a 2 m
    // obelisk centred at 1.0 spans 0..2 (INV-37).
    obelisk.x = 0.0f; obelisk.y = 0.0f;
    obelisk.width = 0.4f; obelisk.height = 0.4f; obelisk.thickness = 2.0f;
    obelisk.z = (floor_p.z + floor_p.thickness * 0.5f) + obelisk.thickness * 0.5f;
    obelisk.r = 1.0f; obelisk.g = 0.1f; obelisk.b = 0.1f; obelisk.a = 1.0f;
    obelisk.SetMaterial(Materials::Type::STONE);
    obelisk.owner = ParticleOwner::STATIC;
    obelisk.is_at_rest = true;
    ctx.add_test_particle(obelisk);

    // Light from one side: illuminates the obelisk's face, creating a shadow
    // on the opposite side. GI bounce from the lit red face should tint the
    // floor in the shadow zone with red.
    ps.queue_light(-5.0f, 0.0f, 3.0f, 500000.0f, 80.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene: white floor + red obelisk + side light\n");

    // Render 70 frames for GI temporal convergence
    for (int i = 0; i < 70; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> frame(buf_size);
    std::memcpy(frame.data(), rs.get_render_buffer().get_native_data(), buf_size);
    const uint8_t* data = frame.data();
    bdump(data, w, h, "/tmp/gi_bounce.ppm");
    printf("  Dumped /tmp/gi_bounce.ppm\n");

    // ================================================================
    // Find the obelisk in screen space
    // ================================================================
    int ob_cx = w / 2, ob_cy = h / 2;
    for (int y = h/4; y < 3*h/4; y += 2) {
        for (int x = w/4; x < 3*w/4; x += 2) {
            auto px = bpx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            // Obelisk is very red: R >> G, R >> B
            if (px.r > 100 && px.r > px.g * 3 && px.r > px.b * 3) {
                ob_cx = x; ob_cy = y;
                break;
            }
        }
    }
    printf("  Obelisk found at: (%d,%d) RGB=(%d,%d,%d)\n",
           ob_cx, ob_cy,
           bpx(data, w, ob_cx, ob_cy).r,
           bpx(data, w, ob_cx, ob_cy).g,
           bpx(data, w, ob_cx, ob_cy).b);

    // ================================================================
    // Sample floor near obelisk vs far from obelisk
    // ================================================================
    // "Near": floor pixels 20-40px from the obelisk base (lit side)
    // "Far": floor pixels 150-200px away (same lighting direction, no bounce)
    //
    // On a white floor, GI bounce from the red obelisk should make
    // near-floor pixels have R > G and R > B (red tint).
    // Far-floor pixels should have R ≈ G ≈ B (white light, no tint).

    // Sample all non-background floor pixels, split into "near obelisk" vs "far".
    // SSDO bounce: floor near the red obelisk gets red-tinted indirect light
    // from the obelisk's lit surface. The tint should be visible in the RGB ratio.
    float near_r = 0, near_g = 0, near_b = 0;
    int near_n = 0;
    float far_r = 0, far_g = 0, far_b = 0;
    int far_n = 0;

    for (int y = 20; y < h - 20; y++) {
        for (int x = 20; x < w - 20; x++) {
            auto px = bpx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float bri = (px.r + px.g + px.b) / 3.0f;
            if (bri < 2 || bri > 240) continue;
            // Skip obelisk surface pixels (very red, high R)
            if (px.r > 60 && px.r > px.g * 3) continue;

            int dx = x - ob_cx;
            int dy = y - ob_cy;
            float dist = std::sqrt((float)(dx*dx + dy*dy));

            // Near: within 30px of obelisk center (floor around the base)
            if (dist > 5 && dist < 30) {
                near_r += px.r; near_g += px.g; near_b += px.b;
                near_n++;
            }

            // Far: 150+ px away (open floor, no bounce influence)
            if (dist > 150 && bri > 50) {
                far_r += px.r; far_g += px.g; far_b += px.b;
                far_n++;
            }
        }
    }

    if (near_n > 0) { near_r /= near_n; near_g /= near_n; near_b /= near_n; }
    if (far_n > 0) { far_r /= far_n; far_g /= far_n; far_b /= far_n; }

    printf("\n  Near obelisk floor (n=%d): R=%.1f G=%.1f B=%.1f\n", near_n, near_r, near_g, near_b);
    printf("  Far floor (n=%d):          R=%.1f G=%.1f B=%.1f\n", far_n, far_r, far_g, far_b);

    // Red tint: near floor should have higher R relative to G,B than far floor
    float near_red_ratio = (near_g + near_b > 0.1f) ? near_r / ((near_g + near_b) / 2.0f) : 1.0f;
    float far_red_ratio = (far_g + far_b > 0.1f) ? far_r / ((far_g + far_b) / 2.0f) : 1.0f;
    float tint_excess = near_red_ratio - far_red_ratio;

    printf("\n  Near R/(G+B)/2 ratio: %.3f\n", near_red_ratio);
    printf("  Far  R/(G+B)/2 ratio: %.3f\n", far_red_ratio);
    printf("  Red tint excess: %.3f\n", tint_excess);

    // SSDO bounce: at least 1% more red near the obelisk (subtle but measurable)
    bool has_bounce = tint_excess > 0.01f;

    printf("\n%s: GI bounce (red tint excess %.3f, threshold > 0.01)\n",
           has_bounce ? "PASS" : "FAIL", tint_excess);
    return has_bounce;
}
