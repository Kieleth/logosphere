// =============================================================================
// PHYSICS DRIVE NECK YAW — Phase 2 of physics-driven skeleton
// =============================================================================
// Phase 2 milestone: enable the gluon PD drive on a single humanoid joint
// and verify it runs to target alongside the rest of the humanoid still
// driven by FK. The neck→head gluon ("Head" joint) is the obvious Z-axis
// choice — head yaw is rotation_z and the existing particle representation
// can express it without the full 3D rotation-DOF upgrade.
//
// Scene: Eva idle on a strata floor, no look-at target, no velocity. We
// enable angular drive on the "Head" joint's gluon with
// target_relative_rotation = pi/8. The yaw cascade, FK position writes,
// and shape-snap are gated for the head particle so they don't clobber
// the solver's output. After 2 s the head's rotation_z should sit at
// pi/8 (within ~5 %).
//
// Pass criteria:
//   A. After settle (120 frames), head.rotation_z within 5 % of pi/8.
//   B. Hold-phase max error (frames 120-180) within 10 % of pi/8 — no
//      runaway oscillation.
//   C. Head particle position hasn't drifted absurdly (sanity: within
//      0.3 m of its t=0 position, since it shouldn't be moving).
//
// Run: ./build/logosphere-tests --test test_physics_drive_neck_yaw --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_physics_drive_neck_yaw() {
    printf("\n=== Physics Drive: Neck Yaw (Phase 2) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "physics drive neck yaw";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // Strata floor to keep the setup comparable to Eden.
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
    const float eva_spawn_x = 0.0f;
    const float eva_spawn_y = 0.0f;
    auto eva = hgen.generate_humanoid_physics(
        eva_spawn_x, eva_spawn_y, 1.0f, -1, HumanoidSpec::eva(), false);
    // generate_humanoid_physics already created the KG entity; skip
    // create_kg_entities (its "Human" type isn't in the core ontology
    // and it would overwrite eva.entity_id with INVALID).
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva.entity_id);

    int head_id = eva.head_id;

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        if (head_id == (int)old_idx) head_id = (int)new_idx;
    });

    const float dt = 1.0f / 60.0f;

    // Let Eva settle briefly so FK initializes joint transforms and the
    // cascade establishes head_yaw_world=0 (no look-at target set).
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Sample head rest rotation + position.
    float rest_head_x, rest_head_y, rest_head_z, rest_head_rot_z;
    {
        auto v = ps.lock_particles_for_read();
        rest_head_x = v[head_id].x;
        rest_head_y = v[head_id].y;
        rest_head_z = v[head_id].z;
        rest_head_rot_z = v[head_id].rotation_z;
    }
    printf("  Rest: head pos=(%.3f, %.3f, %.3f) rot_z=%+.4f\n",
           rest_head_x, rest_head_y, rest_head_z, rest_head_rot_z);

    // Flip the "Head" joint (neck → head) into physics-drive with target pi/8.
    const float TARGET = static_cast<float>(M_PI) / 8.0f;
    // Joint name is "head" (the neck→head gluon); lowercase per
    // derive_joints_from_gluons registration.
    bool enabled = engine.get_humanoid_locomotion().set_joint_physics_drive(eva.entity_id, "head",
                                               TARGET,
                                               /*stiffness=*/200.0f,
                                               /*damping=*/12.0f);
    if (!enabled) {
        printf("  FAIL: set_joint_physics_drive returned false (joint or gluon missing)\n");
        return false;
    }

    // Run 3 s. Settle in the first 2 s, hold for the last 1 s.
    constexpr int FRAMES = 180;
    constexpr int HOLD_START = 120;

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float final_rot_z    = 0.0f;
    float max_pos_drift  = 0.0f;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& h = v[head_id];
        float err = std::abs(h.rotation_z - TARGET);
        float dx = h.x - rest_head_x;
        float dy = h.y - rest_head_y;
        float dz = h.z - rest_head_z;
        float drift = std::sqrt(dx*dx + dy*dy + dz*dz);
        final_rot_z = h.rotation_z;

        if (f < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (err > max_err_hold) max_err_hold = err;
        }
        if (drift > max_pos_drift) max_pos_drift = drift;

        if (f % 15 == 0) {
            printf("  [f%3d] head rot_z=%+.4f err=%+.4f pos=(%.3f,%.3f,%.3f) drift=%.3f\n",
                   f, h.rotation_z, err, h.x, h.y, h.z, drift);
        }
    }

    const float FINAL_BUDGET = 0.05f * TARGET;
    const float HOLD_BUDGET  = 0.10f * TARGET;
    const float POS_BUDGET   = 0.30f;  // metres — generous sanity, head shouldn't wander

    float final_err = std::abs(final_rot_z - TARGET);

    printf("\n  Target:         %+.4f rad (pi/8)\n", TARGET);
    printf("  Final rot_z:    %+.4f (err %.4f / budget %.4f)\n",
           final_rot_z, final_err, FINAL_BUDGET);
    printf("  Hold max err:   %.4f (budget %.4f)\n", max_err_hold, HOLD_BUDGET);
    printf("  Max pos drift:  %.3f m (budget %.3f)\n", max_pos_drift, POS_BUDGET);

    bool final_ok  = (final_err    <= FINAL_BUDGET);
    bool hold_ok   = (max_err_hold <= HOLD_BUDGET);
    bool pos_ok    = (max_pos_drift <= POS_BUDGET);

    printf("\n  Assertions:\n");
    printf("    %s: final rot_z within 5%% of target\n",      final_ok ? "PASS" : "FAIL");
    printf("    %s: hold-phase error within 10%% of target\n", hold_ok  ? "PASS" : "FAIL");
    printf("    %s: head position drift <= %.2f m\n",          pos_ok   ? "PASS" : "FAIL", POS_BUDGET);

    bool ok = final_ok && hold_ok && pos_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL]");
    return ok;
}
