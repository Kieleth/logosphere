// =============================================================================
// PHYSICS DRIVE — SHOULDER MULTI-AXIS (rotational-DOF upgrade Stage 3+4)
// =============================================================================
// Mirror of test_physics_drive_neck_yaw, but hitting a joint whose target
// mixes two non-Z axes at once. The right shoulder is a ball-socket joint
// with flex (X), abduct (Y), and twist (Z) axes. Combining flex+abduct is
// the canonical multi-axis pose and the reason Stage 4 replaced the
// three-independent-row angular constraint with a single Rodrigues-axis
// vector row: independent per-axis bias piles up a small-angle
// approximation that left combined targets at ~0.1 rad steady error.
//
// Target: 30 deg flex about X composed with 20 deg abduct about Y
// (=~ 36 deg total Rodrigues angle).
// Assertion: after settle, the upper arm particle's rotation_q matches
// the target relative to the parent (the proximal shoulder particle),
// within 5 % angular error, and holds flat without oscillation.
//
// Run: ./build/logosphere-tests --test test_physics_drive_shoulder_multiaxis --no-head
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

bool test_physics_drive_shoulder_multiaxis() {
    printf("\n=== Physics Drive: Shoulder multi-axis (Stage 3) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "shoulder multiaxis";
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

    // right_arm_ids layout (from HumanoidGenerator): [0]=shoulder,
    // [1]=upper_arm, [2]=forearm, [3]=hand. The right_shoulder joint
    // is between [0] (parent) and [1] (child = upper arm).
    if (eva.right_arm_ids.size() < 2) {
        printf("  FAIL: Eva right arm missing particles (size=%zu)\n",
               eva.right_arm_ids.size());
        return false;
    }
    int upper_arm_id = eva.right_arm_ids[1];

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        if (upper_arm_id == (int)old_idx) upper_arm_id = (int)new_idx;
    });

    const float dt = 1.0f / 60.0f;

    // Settle a few frames so FK establishes the rest pose and parent
    // Euler reads the correct "facing north" frame.
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Combined flex + abduct target. Stage 4's single Rodrigues-axis
    // vector constraint handles this as one composed rotation.
    const float flex_deg   = 30.0f;
    const float abduct_deg = 20.0f;
    const float flex_rad   = flex_deg   * static_cast<float>(M_PI) / 180.0f;
    const float abduct_rad = abduct_deg * static_cast<float>(M_PI) / 180.0f;

    auto q_flex   = logosphere::Quat::from_axis_angle(1.0f, 0.0f, 0.0f, flex_rad);
    auto q_abduct = logosphere::Quat::from_axis_angle(0.0f, 1.0f, 0.0f, abduct_rad);
    auto target_q = q_abduct * q_flex;

    // Humanoid arm inertias are different from the free-particle test;
    // the 200/12 defaults leave a steady-state error around 0.1 rad.
    // Stiffer gains pull the pose closer.
    bool enabled = engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_shoulder",
                                                 target_q,
                                                 /*stiffness=*/2000.0f,
                                                 /*damping=*/60.0f);
    if (!enabled) {
        printf("  FAIL: set_joint_physics_drive_q returned false\n");
        return false;
    }

    // Run long enough for PD to settle then hold.
    constexpr int FRAMES = 240;
    constexpr int HOLD_START = 180;

    auto err_angle = [&](const logosphere::Quat& qa, const logosphere::Quat& qb) {
        logosphere::Quat q_err = qb * qa.conjugate() * target_q.conjugate();
        float ax, ay, az, theta;
        q_err.to_axis_angle(ax, ay, az, theta);
        if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
        return std::abs(theta);
    };

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float final_err      = 0.0f;

    // Parent particle id — used to compute error relative to parent.
    int parent_id = eva.right_arm_ids[0];
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        if (parent_id == (int)old_idx) parent_id = (int)new_idx;
    });

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& pa = v[parent_id];
        const Particle& pb = v[upper_arm_id];
        // Parent might be FK-owned; use its Euler as the quaternion
        // source on read, matching what the solver does.
        logosphere::Quat q_a = pa.is_quat_driven
            ? pa.rotation_q
            : logosphere::Quat::from_euler(pa.rotation_x, pa.rotation_y, pa.rotation_z);
        logosphere::Quat q_b = pb.rotation_q;  // child is quat-driven
        float err = err_angle(q_a, q_b);
        final_err = err;

        if (f < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (err > max_err_hold) max_err_hold = err;
        }
        if (f % 20 == 0) {
            printf("  [f%3d] upper_arm rot_q=(%+.3f,%+.3f,%+.3f,%+.3f) err=%+.4f rad\n",
                   f, pb.rotation_q.w, pb.rotation_q.x, pb.rotation_q.y, pb.rotation_q.z,
                   err);
        }
    }

    // Compute target angular magnitude for budget sizing.
    float tax, tay, taz, ttheta;
    target_q.to_axis_angle(tax, tay, taz, ttheta);
    if (ttheta > static_cast<float>(M_PI)) ttheta -= 2.0f * static_cast<float>(M_PI);
    float target_mag = std::abs(ttheta);

    const float FINAL_BUDGET = 0.05f * target_mag;
    const float HOLD_BUDGET  = 0.10f * target_mag;

    printf("\n  Target mag:     %.4f rad (%.1f deg)\n", target_mag, target_mag * 180.0f / static_cast<float>(M_PI));
    printf("  Final err:      %.4f (budget %.4f)\n", final_err, FINAL_BUDGET);
    printf("  Max settle err: %.4f\n", max_err_settle);
    printf("  Max hold err:   %.4f (budget %.4f)\n", max_err_hold, HOLD_BUDGET);

    bool final_ok = (final_err    <= FINAL_BUDGET);
    bool hold_ok  = (max_err_hold <= HOLD_BUDGET);

    printf("\n  Assertions:\n");
    printf("    %s: final |q_err| within 5%% of target magnitude\n",  final_ok ? "PASS" : "FAIL");
    printf("    %s: hold-phase error within 10%% of target magnitude\n", hold_ok  ? "PASS" : "FAIL");

    bool ok = final_ok && hold_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL - shoulder quat drive did not converge]");
    return ok;
}
