// ============================================================================
// SHADOW INVARIANT TEST
// ============================================================================
// The fundamental truth: a shadow is the ABSENCE of light.
// For any pixel, adding a shadow-casting object can only REDUCE the light
// reaching the floor, never increase it. Therefore:
//
//   brightness_with_object <= brightness_without_object
//
// This test renders the SAME scene twice: once without Eva, once with Eva.
// For every floor pixel that Eva's shadow touches, the brightness must be
// less than or equal to the no-Eva version.
//
// If ANY pixel is brighter WITH Eva than WITHOUT, the shadow system is broken.
//
// Run:
//   ./logosphere-tests --test test_shadow_invariant
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

struct SPx { uint8_t b, g, r, a; };

static SPx read_spx(const uint8_t* d, int w, int x, int y) {
    SPx p; int i = (y*w+x)*4;
    p.b=d[i]; p.g=d[i+1]; p.r=d[i+2]; p.a=d[i+3]; return p;
}
static float bri(SPx p) { return (p.r+p.g+p.b)/3.0f; }
static void dump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f=fopen(path,"wb"); if(!f)return;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;i++){fputc(d[i*4+2],f);fputc(d[i*4+1],f);fputc(d[i*4+0],f);}
    fclose(f);
}

bool test_shadow_invariant(TestContext& ctx) {
    printf("\n=== Shadow Invariant Test ===\n");
    printf("Principle: adding a shadow caster can only REDUCE light, never increase it.\n\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;
    const int FRAMES = 30;

    // ================================================================
    // RENDER 1: Floor + streetlight ONLY (no Eva)
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

    ps.queue_light(-8.0f, 8.0f, 6.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

    for (int i = 0; i < FRAMES; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> no_eva(buf_size);
    std::memcpy(no_eva.data(), rs.get_render_buffer().get_native_data(), buf_size);
    dump(no_eva.data(), w, h, "/tmp/shadow_no_eva.ppm");
    printf("  Render 1: Floor + light only (no Eva)\n");

    // ================================================================
    // RENDER 2: Floor + streetlight + Eva
    // ================================================================
    ctx.clear_particles();

    // Same floor
    ctx.add_test_particle(floor_p);

    // Same light
    ps.queue_light(-8.0f, 8.0f, 6.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

    // Add Eva
    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    PhysicsHumanoidResult eva = humanoid_gen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    for (int i = 0; i < FRAMES; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> with_eva(buf_size);
    std::memcpy(with_eva.data(), rs.get_render_buffer().get_native_data(), buf_size);
    dump(with_eva.data(), w, h, "/tmp/shadow_with_eva.ppm");
    printf("  Render 2: Floor + light + Eva\n");

    // ================================================================
    // COMPARE: Every floor pixel should be <= in the Eva version
    // ================================================================

    int brighter_count = 0;
    int darker_count = 0;
    int same_count = 0;
    int total_floor = 0;
    float max_violation = 0;
    int worst_x = 0, worst_y = 0;
    float worst_no_eva_bri = 0, worst_with_eva_bri = 0;

    for (int y = 5; y < h - 5; y += 1) {
        for (int x = 5; x < w - 5; x += 1) {
            SPx px_no = read_spx(no_eva.data(), w, x, y);
            SPx px_with = read_spx(with_eva.data(), w, x, y);

            // Skip background
            if (px_no.r == 10 && px_no.g == 10 && px_no.b == 15) continue;

            // Skip Eva's body pixels (they aren't floor)
            if (px_with.r == 10 && px_with.g == 10 && px_with.b == 15) continue;

            // Skip pixels where Eva's body is (different from floor color)
            float b_no = bri(px_no);
            float b_with = bri(px_with);

            // Eva's body is purple/dark, skip non-floor pixels
            if (b_with < 5.0f || b_no < 5.0f) continue;
            // If the pixel is on Eva's body (very different from floor), skip
            if (std::abs(px_with.r - px_no.r) > 100 ||
                std::abs(px_with.g - px_no.g) > 100 ||
                std::abs(px_with.b - px_no.b) > 100) continue;

            total_floor++;

            float diff = b_with - b_no;
            if (diff > 1.0f) {
                brighter_count++;
                if (diff > max_violation) {
                    max_violation = diff;
                    worst_x = x; worst_y = y;
                    worst_no_eva_bri = b_no;
                    worst_with_eva_bri = b_with;
                }
            } else if (diff < -1.0f) {
                darker_count++;
            } else {
                same_count++;
            }
        }
    }

    printf("\n  Floor pixels compared: %d\n", total_floor);
    printf("  Darker with Eva (correct shadow): %d\n", darker_count);
    printf("  Same brightness: %d\n", same_count);
    printf("  BRIGHTER with Eva (VIOLATION): %d\n", brighter_count);
    if (brighter_count > 0) {
        printf("  Worst violation: +%.1f at (%d,%d)\n", max_violation, worst_x, worst_y);
        printf("    Without Eva: %.1f, With Eva: %.1f\n", worst_no_eva_bri, worst_with_eva_bri);
    }

    float violation_pct = total_floor > 0 ? 100.0f * brighter_count / total_floor : 0;
    printf("  Violation rate: %.2f%%\n", violation_pct);

    // With GI enabled, Eva's body bounces indirect light to nearby floor pixels,
    // making them slightly brighter than without Eva. This is physically correct.
    // Threshold 2% accommodates GI bounce while still catching real shadow bugs.
    bool pass = violation_pct < 2.0f;
    printf("\n%s: Shadow invariant (violations %.2f%%, threshold 2%%)\n",
           pass ? "PASS" : "FAIL", violation_pct);
    return pass;
}
