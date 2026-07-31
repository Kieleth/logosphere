// =============================================================================
// PHYSICS DRIVE — TWO JOINTS CONCURRENT (Phase 3a)
// =============================================================================
// First real test of multiple physics-driven joints on the same humanoid.
// Drives Eva's neck (scalar Z via set_joint_physics_drive) AND right
// shoulder (quaternion via set_joint_physics_drive_q) at the same time.
// Targets two independent axes so any cross-joint interference shows up
// as convergence failure or steady-state wobble on one while the other
// holds.
//
// What this proves:
//   - physics_drive_children set handles more than one child per
//     humanoid.
//   - Scalar-Z drive and quaternion drive coexist on the same entity.
//   - Each joint's gluon PD converges to its own target without being
//     pulled off by the other joint's impulses.
//
// Targets:
//   - head joint (neck → head gluon): 22.5 deg yaw about Z
//     (pi/8 on the scalar path).
//   - right_shoulder joint: 30 deg flex about X composed with 20 deg
//     abduct about Y (quaternion path).
//
// Assertions: both joints settle within 5 % of their respective target
// angles after 3 s, and hold without oscillation for the last 1 s.
//
// Run: ./build/logosphere-tests --test test_physics_drive_two_joints --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_physics_drive_two_joints() {
    printf("\n=== Physics Drive: Two joints concurrent (Phase 3a) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "two joints";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);
    std::vector<StrataLayerSpec> layers;
    auto add_layer = [&](const char* n, Materials::Type m, float th,
                         float r, float g, float b, bool bond, float bs) {
        StrataLayerSpec s;
        s.name = n; s.material = m; s.thickness = th;
        s.r = r; s.g = g; s.b = b;
        s.bond_within_layer = bond;
        s.bond_strength = bs;
        layers.push_back(s);
    };
    add_layer("bedrock",  Materials::Type::STONE, 0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
    add_layer("sediment", Materials::Type::STONE, 0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
    add_layer("organic",  Materials::Type::DIRT,  0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
    strata.set_layers(std::move(layers));
    strata.set_enabled(true);
    strata.preload_chunks_around(0.0f, 0.0f, 3);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva.entity_id);

    int head_id = eva.head_id;
    int upper_arm_id = -1;
    int shoulder_id = -1;
    if (eva.right_arm_ids.size() >= 2) {
        shoulder_id  = eva.right_arm_ids[0];
        upper_arm_id = eva.right_arm_ids[1];
    }
    if (head_id < 0 || upper_arm_id < 0) {
        printf("  FAIL: Eva missing required particles (head=%d upper_arm=%d)\n",
               head_id, upper_arm_id);
        return false;
    }

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(head_id);
        fix(upper_arm_id);
        fix(shoulder_id);
    });

    const float dt = 1.0f / 60.0f;

    // Settle briefly so FK establishes the rest pose.
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Drive #1: neck yaw (scalar Z) = pi/8.
    const float NECK_TARGET = static_cast<float>(M_PI) / 8.0f;
    if (!engine.get_humanoid_locomotion().set_joint_physics_drive(eva.entity_id, "head",
                                     NECK_TARGET,
                                     /*stiffness=*/200.0f,
                                     /*damping=*/12.0f)) {
        printf("  FAIL: set_joint_physics_drive(head) returned false\n");
        return false;
    }

    // Drive #2: shoulder flex+abduct (quaternion).
    const float FLEX_RAD   = 30.0f * static_cast<float>(M_PI) / 180.0f;
    const float ABDUCT_RAD = 20.0f * static_cast<float>(M_PI) / 180.0f;
    auto q_flex   = logosphere::Quat::from_axis_angle(1.0f, 0.0f, 0.0f, FLEX_RAD);
    auto q_abduct = logosphere::Quat::from_axis_angle(0.0f, 1.0f, 0.0f, ABDUCT_RAD);
    auto shoulder_target = q_abduct * q_flex;
    if (!engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_shoulder",
                                       shoulder_target,
                                       /*stiffness=*/2000.0f,
                                       /*damping=*/60.0f)) {
        printf("  FAIL: set_joint_physics_drive_q(right_shoulder) returned false\n");
        return false;
    }

    // Run 3 s. Measure hold-phase in the last 1 s.
    constexpr int FRAMES = 180;
    constexpr int HOLD_START = 120;

    float neck_final = 0.0f;
    float neck_max_hold_err = 0.0f;
    float shoulder_final_err = 0.0f;
    float shoulder_max_hold_err = 0.0f;

    auto shoulder_err = [&](const logosphere::Quat& qa, const logosphere::Quat& qb) {
        logosphere::Quat q_err = qb * qa.conjugate() * shoulder_target.conjugate();
        float ax, ay, az, theta;
        q_err.to_axis_angle(ax, ay, az, theta);
        if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
        return std::abs(theta);
    };

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& head = v[head_id];
        const Particle& shoulder = v[shoulder_id];
        const Particle& upper_arm = v[upper_arm_id];

        // Neck yaw: head.rotation_z relative to cascade-driven neck.
        // Cascade has no look-at target so neck-yaw target is 0; the
        // drive bias equals head.rotation_z.
        float neck_err = std::abs(head.rotation_z - NECK_TARGET);
        neck_final = head.rotation_z;

        // Shoulder: quaternion error vs target.
        logosphere::Quat q_a = shoulder.is_quat_driven
            ? shoulder.rotation_q
            : logosphere::Quat::from_euler(shoulder.rotation_x, shoulder.rotation_y, shoulder.rotation_z);
        float sh_err = shoulder_err(q_a, upper_arm.rotation_q);
        shoulder_final_err = sh_err;

        if (f >= HOLD_START) {
            if (neck_err > neck_max_hold_err) neck_max_hold_err = neck_err;
            if (sh_err > shoulder_max_hold_err) shoulder_max_hold_err = sh_err;
        }

        if (f % 20 == 0) {
            printf("  [f%3d] neck_z=%+.4f (err %.4f)  shoulder err=%+.4f\n",
                   f, head.rotation_z, neck_err, sh_err);
        }
    }

    const float NECK_FINAL_BUDGET = 0.05f * NECK_TARGET;
    const float NECK_HOLD_BUDGET  = 0.10f * NECK_TARGET;

    // Shoulder target magnitude for error budgets.
    float tax, tay, taz, ttheta;
    shoulder_target.to_axis_angle(tax, tay, taz, ttheta);
    if (ttheta > static_cast<float>(M_PI)) ttheta -= 2.0f * static_cast<float>(M_PI);
    float SHOULDER_MAG = std::abs(ttheta);
    const float SHOULDER_FINAL_BUDGET = 0.05f * SHOULDER_MAG;
    const float SHOULDER_HOLD_BUDGET  = 0.10f * SHOULDER_MAG;

    float neck_final_err = std::abs(neck_final - NECK_TARGET);

    printf("\n  Neck target:      %.4f rad, final %.4f (err %.4f / budget %.4f)\n",
           NECK_TARGET, neck_final, neck_final_err, NECK_FINAL_BUDGET);
    printf("  Neck hold max:    %.4f (budget %.4f)\n", neck_max_hold_err, NECK_HOLD_BUDGET);
    printf("  Shoulder target:  %.4f rad, final_err %.4f (budget %.4f)\n",
           SHOULDER_MAG, shoulder_final_err, SHOULDER_FINAL_BUDGET);
    printf("  Shoulder hold max:%.4f (budget %.4f)\n", shoulder_max_hold_err, SHOULDER_HOLD_BUDGET);

    bool neck_ok     = (neck_final_err     <= NECK_FINAL_BUDGET);
    bool neck_hold   = (neck_max_hold_err  <= NECK_HOLD_BUDGET);
    bool sh_ok       = (shoulder_final_err <= SHOULDER_FINAL_BUDGET);
    bool sh_hold     = (shoulder_max_hold_err <= SHOULDER_HOLD_BUDGET);

    printf("\n  Assertions:\n");
    printf("    %s: neck final within 5%%\n",        neck_ok   ? "PASS" : "FAIL");
    printf("    %s: neck hold within 10%%\n",        neck_hold ? "PASS" : "FAIL");
    printf("    %s: shoulder final within 5%%\n",    sh_ok     ? "PASS" : "FAIL");
    printf("    %s: shoulder hold within 10%%\n",    sh_hold   ? "PASS" : "FAIL");

    bool ok = neck_ok && neck_hold && sh_ok && sh_hold;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — two-joint drive did not converge cleanly]");
    return ok;
}
