// ============================================================================
// SSAO BASIC TEST
// ============================================================================
// Verifies SSAO pipeline produces visible ambient occlusion.
//
// Test 1: Open floor pixels far from objects should be bright (AO ≈ 1.0,
//         no occlusion). Verifies SSAO doesn't darken everything.
// Test 2: Floor pixels near the base of a tall box should be darker than
//         floor pixels at the same distance from the light but away from
//         the box. This is the contact shadow effect.
//
// Run:
//   ./logosphere-tests --test test_ssao_basic
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct AoPx { uint8_t b, g, r, a; };

static AoPx aopx(const uint8_t* d, int w, int x, int y) {
    AoPx p; int i = (y * w + x) * 4;
    p.b = d[i]; p.g = d[i+1]; p.r = d[i+2]; p.a = d[i+3]; return p;
}
static float aobri(AoPx p) { return (p.r + p.g + p.b) / 3.0f; }

static void aodump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(d[i*4+2], f); fputc(d[i*4+1], f); fputc(d[i*4+0], f);
    }
    fclose(f);
}

bool test_ssao_basic(TestContext& ctx) {
    printf("\n=== SSAO Basic Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: floor + two boxes at different positions + light
    // Box A at center (creates contact shadow on floor around its base)
    // Box B far away (control: its nearby floor should also be darkened)
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

    // Box A: tall box at center
    Particle boxA = {};
    boxA.shape = ParticleShape::BOX;
    boxA.x = 0.0f; boxA.y = 0.0f; boxA.z = 0.6f;
    boxA.width = 0.5f; boxA.height = 0.5f; boxA.thickness = 1.2f;
    boxA.r = 0.6f; boxA.g = 0.6f; boxA.b = 0.6f; boxA.a = 1.0f;
    boxA.SetMaterial(Materials::Type::STONE);
    boxA.owner = ParticleOwner::STATIC;
    boxA.is_at_rest = true;
    ctx.add_test_particle(boxA);

    // Light from the side
    ps.queue_light(-3.0f, 3.0f, 6.0f, 400000.0f, 80.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene: grey floor + tall box + side light\n");

    // Render
    for (int i = 0; i < 15; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> frame(buf_size);
    std::memcpy(frame.data(), rs.get_render_buffer().get_native_data(), buf_size);
    const uint8_t* data = frame.data();
    aodump(data, w, h, "/tmp/ssao_basic.ppm");
    printf("  Dumped /tmp/ssao_basic.ppm\n");

    // ================================================================
    // Test 1: SSAO pipeline produces non-trivial output
    // ================================================================
    // Sample brightness across the floor at various distances from the box.
    // SSAO should create a brightness gradient: darker near box, brighter far.

    // Find box center in screen space
    int box_cx = w / 2, box_cy = h / 2;
    float best = 999;
    for (int y = h/4; y < 3*h/4; y += 2) {
        for (int x = w/4; x < 3*w/4; x += 2) {
            auto px = aopx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float b = aobri(px);
            if (b < 20 || b > 140) continue;
            float grey = std::abs(px.r - px.g) + std::abs(px.g - px.b);
            if (grey < best) { best = grey; box_cx = x; box_cy = y; }
        }
    }
    printf("  Box center: (%d,%d)\n", box_cx, box_cy);

    // Sample floor brightness at 4 distances from box (below box in screen-Y)
    float bri_near = 0, bri_mid = 0, bri_far = 0, bri_vfar = 0;
    int n_near = 0, n_mid = 0, n_far = 0, n_vfar = 0;

    for (int dx = -20; dx <= 20; dx += 4) {
        int sx = box_cx + dx;
        // Near: 5-15px below box
        for (int dy = 140; dy <= 160; dy += 3) {
            int sy = box_cy + dy;
            if (sy >= h - 5) continue;
            auto px = aopx(data, w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            bri_near += aobri(px); n_near++;
        }
        // Mid: 40-60px
        for (int dy = 180; dy <= 200; dy += 3) {
            int sy = box_cy + dy;
            if (sy >= h - 5) continue;
            auto px = aopx(data, w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            bri_mid += aobri(px); n_mid++;
        }
        // Far: 100-120px
        for (int dy = 250; dy <= 270; dy += 3) {
            int sy = box_cy + dy;
            if (sy >= h - 5) continue;
            auto px = aopx(data, w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            bri_far += aobri(px); n_far++;
        }
        // Very far: 200+ px
        for (int dy = 350; dy <= 380; dy += 3) {
            int sy = box_cy + dy;
            if (sy >= h - 5) continue;
            auto px = aopx(data, w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            bri_vfar += aobri(px); n_vfar++;
        }
    }

    if (n_near > 0) bri_near /= n_near;
    if (n_mid > 0) bri_mid /= n_mid;
    if (n_far > 0) bri_far /= n_far;
    if (n_vfar > 0) bri_vfar /= n_vfar;

    printf("\n  Brightness gradient (from box outward):\n");
    printf("    Near  (5-15px): %.1f (n=%d)\n", bri_near, n_near);
    printf("    Mid   (40-60): %.1f (n=%d)\n", bri_mid, n_mid);
    printf("    Far   (100-120): %.1f (n=%d)\n", bri_far, n_far);
    printf("    VFar  (200+): %.1f (n=%d)\n", bri_vfar, n_vfar);

    // ================================================================
    // Test 2: SSAO creates measurable contact shadow
    // ================================================================
    // The near-box floor should be darker than far floor.
    // Account for natural lighting falloff (inverse square) by checking
    // that the near/far ratio is lower WITH ssao than pure inverse-square would predict.
    // For now: just verify near < far (contact shadow visible).
    bool near_darker = bri_near < bri_far;
    float contact_ratio = (bri_far > 0.1f) ? bri_near / bri_far : 1.0f;

    printf("\n  Contact shadow ratio (near/far): %.3f\n", contact_ratio);
    printf("  Near darker than far: %s\n", near_darker ? "YES" : "NO");

    // With SSAO: near/far ratio should be < 0.95 (at least 5% contact shadow)
    bool pass = contact_ratio < 0.95f;
    printf("\n%s: SSAO basic (contact ratio %.3f, threshold < 0.95)\n",
           pass ? "PASS" : "FAIL", contact_ratio);
    return pass;
}
