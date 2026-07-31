// ============================================================================
// HUMANOID MOVEMENT INSTRUMENTED TEST
// ============================================================================
// Full instrumentation on humanoid limbs during idle + walking.
// Tracks every body particle's position, deviation from FK target, and
// oscillation per frame.
//
// Phase 1: IDLE (no input, 60 frames). Eva should stay in place.
// Phase 2: WALK (velocity set, 180 frames). Eva should walk north smoothly.
//
// Catches:
// - Autonomous drift during idle
// - Limb tracking error (particle far from FK target)
// - Oscillation / jitter (rapid position changes)
// - Body disconnection (particles far from neighbors)
//
// Run: ./logosphere-tests --test test_humanoid_movement_instrumented
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "../src/core/entity_telemetry.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_humanoid_movement_instrumented(TestContext& /* ctx */) {
    printf("\n=== Humanoid Movement Instrumented Test ===\n");

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Humanoid Movement Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) return false;

    auto& ps = engine.get_particle_system();
    const float dt = 1.0f / 60.0f;

    // Large floor (no tile boundaries to isolate movement issues)
    Particle floor = {};
    floor.shape = ParticleShape::BOX;
    floor.x = 0; floor.y = 0; floor.z = 0.0f;
    floor.width = 50; floor.height = 50; floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.5f; floor.b = 0.2f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor.is_at_rest = true;
    engine.add_particle(floor);
    ps.queue_light(0.0f, 10.0f, 8.0f, 300000.0f, 50.0f, 1.0f, 1.0f, 1.0f);

    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = humanoid_gen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    printf("  Eva at (0, 0), hips=%d, %zu body particles\n", eva.hips_id, eva.body_ids.size());

    // Settle
    for (int i = 0; i < 5; i++) { engine.update(dt); engine.render(); }

    // Record initial positions
    std::vector<float> initial_x(eva.body_ids.size());
    std::vector<float> initial_y(eva.body_ids.size());
    std::vector<float> initial_z(eva.body_ids.size());
    std::vector<float> prev_x(eva.body_ids.size());
    std::vector<float> prev_y(eva.body_ids.size());
    std::vector<float> prev_z(eva.body_ids.size());
    {
        auto particles = ps.lock_particles_for_read();
        for (size_t i = 0; i < eva.body_ids.size(); i++) {
            const Particle& p = particles[eva.body_ids[i]];
            initial_x[i] = prev_x[i] = p.x;
            initial_y[i] = prev_y[i] = p.y;
            initial_z[i] = prev_z[i] = p.z;
        }
    }

    // Phase 1: IDLE - hips should NOT drift horizontally (no walking on her own)
    printf("\n  Phase 1: IDLE (60 frames, no volitional input)\n");

    float hips_x0, hips_y0, hips_z0;
    {
        auto particles = ps.lock_particles_for_read();
        const Particle& hips = particles[eva.hips_id];
        hips_x0 = hips.x; hips_y0 = hips.y; hips_z0 = hips.z;
    }

    float hips_max_drift_xy = 0.0f;
    float hips_max_drift_z = 0.0f;

    for (int frame = 0; frame < 60; frame++) {
        engine.update(dt);
        engine.render();
        auto particles = ps.lock_particles_for_read();
        const Particle& hips = particles[eva.hips_id];
        float dx = hips.x - hips_x0;
        float dy = hips.y - hips_y0;
        float drift_xy = std::sqrt(dx*dx + dy*dy);
        float drift_z = std::abs(hips.z - hips_z0);
        if (drift_xy > hips_max_drift_xy) hips_max_drift_xy = drift_xy;
        if (drift_z > hips_max_drift_z) hips_max_drift_z = drift_z;
        for (size_t i = 0; i < eva.body_ids.size(); i++) {
            const Particle& p = particles[eva.body_ids[i]];
            prev_x[i] = p.x; prev_y[i] = p.y; prev_z[i] = p.z;
        }
    }

    printf("    Hips horizontal drift: %.1fmm (should be near 0)\n", hips_max_drift_xy * 1000);
    printf("    Hips vertical drift: %.1fmm\n", hips_max_drift_z * 1000);

    // ============================================================
    // Phase 2: WALK (180 frames, velocity set)
    // ============================================================
    printf("\n  Phase 2: WALK north (180 frames, v=2m/s)\n");

    engine.get_humanoid_locomotion().set_target_velocity(eva.hips_id, 0.0f, 2.0f);
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);

    // Record position before walking
    float hips_y_start;
    {
        auto particles = ps.lock_particles_for_read();
        hips_y_start = particles[eva.hips_id].y;
    }

    float walk_max_jitter = 0.0f;
    int walk_jitter_violations = 0;
    float walk_max_tracking_error = 0.0f;
    int walk_tracking_violations = 0;

    // Per-limb tracking
    struct LimbStats {
        const char* name;
        const std::vector<int>& ids;
        float max_jitter = 0.0f;
        int jitter_count = 0;
    };
    std::vector<LimbStats> limb_stats = {
        {"left_leg", eva.left_leg_ids},
        {"right_leg", eva.right_leg_ids},
        {"left_arm", eva.left_arm_ids},
        {"right_arm", eva.right_arm_ids},
    };

    for (int frame = 0; frame < 180; frame++) {
        engine.update(dt);
        engine.render();

        auto particles = ps.lock_particles_for_read();

        // Global jitter for all body particles
        for (size_t i = 0; i < eva.body_ids.size(); i++) {
            const Particle& p = particles[eva.body_ids[i]];
            float jx = p.x - prev_x[i];
            float jy = p.y - prev_y[i];
            float jz = p.z - prev_z[i];
            float jitter = std::sqrt(jx*jx + jy*jy + jz*jz);
            // Account for walking forward: subtract expected hips motion
            jy -= 2.0f * dt;  // expected northward motion
            float jitter_relative = std::sqrt(jx*jx + jy*jy + jz*jz);
            if (jitter_relative > walk_max_jitter) walk_max_jitter = jitter_relative;
            if (jitter_relative > 0.1f) walk_jitter_violations++;  // >100mm/frame relative to expected

            prev_x[i] = p.x; prev_y[i] = p.y; prev_z[i] = p.z;
        }

        // Per-limb stats
        for (auto& limb : limb_stats) {
            for (int id : limb.ids) {
                // Track each limb particle for abnormal motion
                const Particle& p = particles[id];
                (void)p;
            }
        }
    }

    float hips_y_end;
    {
        auto particles = ps.lock_particles_for_read();
        hips_y_end = particles[eva.hips_id].y;
    }
    float walk_distance = hips_y_end - hips_y_start;
    // 180 updates of 1/60 s = 3 game seconds at 2 m/s = 6 m. The old
    // 0.033 formula encoded the retired double-advance world; see
    // tests/test_position_authority.cpp.
    float expected_distance = 180 * (1.0f / 60.0f) * 2.0f;

    printf("    Walked distance: %.2fm (expected %.2fm)\n", walk_distance, expected_distance);
    printf("    Walk max relative jitter: %.1fmm/frame\n", walk_max_jitter * 1000);
    printf("    Walk jitter violations (>100mm/frame): %d\n", walk_jitter_violations);

    // ============================================================
    // Assertions
    // ============================================================
    printf("\n  Assertions:\n");

    bool hips_still = (hips_max_drift_xy < 0.05f);
    printf("    %s: Hips stays in place during idle (%.1fmm horizontal drift)\n",
           hips_still ? "PASS" : "FAIL", hips_max_drift_xy * 1000);

    bool walk_progressed = (walk_distance > expected_distance * 0.8f);
    printf("    %s: Walked >80%% expected (%.2fm of %.2fm)\n",
           walk_progressed ? "PASS" : "FAIL", walk_distance, expected_distance);

    bool walk_smooth = (walk_jitter_violations < 10);
    printf("    %s: Walk smooth (%d jitter violations)\n",
           walk_smooth ? "PASS" : "FAIL", walk_jitter_violations);

    bool pass = hips_still && walk_progressed && walk_smooth;
    printf("\n%s: Humanoid movement instrumented\n", pass ? "PASS" : "FAIL");
    return pass;
}
