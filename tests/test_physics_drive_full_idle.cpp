// =============================================================================
// PHYSICS DRIVE — FULL-BODY IDLE (Phase 3c)
// =============================================================================
// The Phase 3 milestone from the Physics-Driven Skeleton plan: flip every
// non-bridge joint on Eva into physics-drive at once, with target_q =
// identity (rest pose relative to parent), and verify she holds her
// standing idle pose instead of collapsing.
//
// This is the "unlock" step: if it works, the FK cascade / shape snap /
// position writes no longer own any skeletal particle. Every rotation is
// solver-driven through the gluon PD. Animation becomes intent (rotate
// the commanded target quat over time) and the solver handles chain
// integrity, contacts, and pose holding.
//
// What we're testing:
//   - A dozen+ physics-drive joints coexisting on one humanoid.
//   - Target quat = identity everywhere (everyone matches parent).
//   - Hips stay on dynamics (they're the body root — gravity + ground
//     support). Spine, arms, legs all physics-drive.
//   - Eva doesn't collapse, drift, or float away.
//
// Assertions (generous, this is the first shot):
//   - Head stays within 30 cm of its initial world position.
//   - Feet stay within 20 cm of their initial world position.
//   - Hands stay within 30 cm of their initial world position.
//   - Hips don't sink below 0.5 m.
//
// Run: ./build/logosphere-tests --test test_physics_drive_full_idle --no-head
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

bool test_physics_drive_full_idle() {
    printf("\n=== Physics Drive: Full-body idle (Phase 3c) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "full body idle";
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

    // Capture a few key particle ids we'll track.
    int head_id = eva.head_id;
    int hips_id = eva.hips_id;
    int l_hand_id = eva.left_arm_ids.size()  >= 4 ? eva.left_arm_ids[3]  : -1;
    int r_hand_id = eva.right_arm_ids.size() >= 4 ? eva.right_arm_ids[3] : -1;
    int l_foot_id = eva.left_leg_ids.size()  >= 1 ? eva.left_leg_ids[0]  : -1;
    int r_foot_id = eva.right_leg_ids.size() >= 1 ? eva.right_leg_ids[0] : -1;

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(head_id); fix(hips_id);
        fix(l_hand_id); fix(r_hand_id);
        fix(l_foot_id); fix(r_foot_id);
    });

    const float dt = 1.0f / 60.0f;

    // Settle on FK so particle positions land at rest.
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Record rest positions for drift assertions.
    float head_rest_x = 0, head_rest_y = 0, head_rest_z = 0;
    float l_hand_rest_z = 0, r_hand_rest_z = 0;
    float l_foot_rest_z = 0, r_foot_rest_z = 0;
    {
        auto v = ps.lock_particles_for_read();
        head_rest_x = v[head_id].x;
        head_rest_y = v[head_id].y;
        head_rest_z = v[head_id].z;
        if (l_hand_id >= 0) l_hand_rest_z = v[l_hand_id].z;
        if (r_hand_id >= 0) r_hand_rest_z = v[r_hand_id].z;
        if (l_foot_id >= 0) l_foot_rest_z = v[l_foot_id].z;
        if (r_foot_id >= 0) r_foot_rest_z = v[r_foot_id].z;
    }
    printf("  Rest positions:\n");
    printf("    head  (%.3f, %.3f, %.3f)\n", head_rest_x, head_rest_y, head_rest_z);
    if (l_hand_id >= 0) printf("    L hand z=%.3f\n", l_hand_rest_z);
    if (r_hand_id >= 0) printf("    R hand z=%.3f\n", r_hand_rest_z);
    if (l_foot_id >= 0) printf("    L foot z=%.3f\n", l_foot_rest_z);
    if (r_foot_id >= 0) printf("    R foot z=%.3f\n", r_foot_rest_z);

    // Flip every non-bridge joint into physics-drive with identity target.
    auto identity = logosphere::Quat::identity();
    const float STIFF = 2000.0f;
    const float DAMP  = 60.0f;
    const char* joint_names[] = {
        // Spine
        "lower_spine", "upper_spine", "neck", "head",
        // Right arm
        "right_shoulder", "right_elbow", "right_wrist",
        // Left arm
        "left_shoulder", "left_elbow", "left_wrist",
        // Right leg
        "right_hip", "right_knee", "right_ankle", "right_toe",
        // Left leg
        "left_hip", "left_knee", "left_ankle", "left_toe",
    };
    int enabled_count = 0;
    for (const char* name : joint_names) {
        if (engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, name, identity, STIFF, DAMP)) {
            enabled_count++;
        } else {
            printf("  WARN: set_joint_physics_drive_q(%s) returned false (joint missing?)\n", name);
        }
    }
    printf("  Enabled physics-drive on %d joints\n", enabled_count);

    // Run 3 seconds.
    constexpr int FRAMES = 180;
    float max_head_drift = 0.0f, max_hand_drift_z = 0.0f, max_foot_drift_z = 0.0f;
    float min_hips_z = 1e9f;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& head = v[head_id];
        const Particle& hips = v[hips_id];
        float dx = head.x - head_rest_x;
        float dy = head.y - head_rest_y;
        float dz = head.z - head_rest_z;
        float head_drift = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (head_drift > max_head_drift) max_head_drift = head_drift;
        if (hips.z < min_hips_z) min_hips_z = hips.z;

        if (l_foot_id >= 0) {
            float fd = std::abs(v[l_foot_id].z - l_foot_rest_z);
            if (fd > max_foot_drift_z) max_foot_drift_z = fd;
        }
        if (r_foot_id >= 0) {
            float fd = std::abs(v[r_foot_id].z - r_foot_rest_z);
            if (fd > max_foot_drift_z) max_foot_drift_z = fd;
        }
        if (l_hand_id >= 0) {
            float hd = std::abs(v[l_hand_id].z - l_hand_rest_z);
            if (hd > max_hand_drift_z) max_hand_drift_z = hd;
        }
        if (r_hand_id >= 0) {
            float hd = std::abs(v[r_hand_id].z - r_hand_rest_z);
            if (hd > max_hand_drift_z) max_hand_drift_z = hd;
        }

        if (f % 30 == 0) {
            printf("  [f%3d] head=(%.3f,%.3f,%.3f) hips.z=%.3f head_drift=%.3f foot_dz_max=%.3f hand_dz_max=%.3f\n",
                   f, head.x, head.y, head.z, hips.z,
                   head_drift, max_foot_drift_z, max_hand_drift_z);
        }
    }

    printf("\n  Summary:\n");
    printf("    head max drift:   %.3f m  (budget 0.30)\n", max_head_drift);
    printf("    foot max |dz|:    %.3f m  (budget 0.20)\n", max_foot_drift_z);
    printf("    hand max |dz|:    %.3f m  (budget 0.30)\n", max_hand_drift_z);
    printf("    hips min z:       %.3f m  (budget 0.50)\n", min_hips_z);

    bool head_ok = (max_head_drift     <= 0.30f);
    bool foot_ok = (max_foot_drift_z   <= 0.20f);
    bool hand_ok = (max_hand_drift_z   <= 0.30f);
    bool hips_ok = (min_hips_z         >= 0.50f);

    printf("\n  Assertions:\n");
    printf("    %s: head stays within 30 cm\n",  head_ok ? "PASS" : "FAIL");
    printf("    %s: feet stay within 20 cm vertically\n", foot_ok ? "PASS" : "FAIL");
    printf("    %s: hands stay within 30 cm vertically\n", hand_ok ? "PASS" : "FAIL");
    printf("    %s: hips don't sink below 0.5 m\n", hips_ok ? "PASS" : "FAIL");

    bool ok = head_ok && foot_ok && hand_ok && hips_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — Eva collapsed or drifted]");
    return ok;
}
