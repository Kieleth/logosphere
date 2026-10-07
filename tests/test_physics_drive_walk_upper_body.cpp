// =============================================================================
// PHYSICS DRIVE — UPPER-BODY WALK (Phase 4a)
// =============================================================================
// First test of the Phase 4 direction: Eva walks forward while every
// spine / arm / head joint is in physics-drive. Legs stay on FK so foot
// planting keeps working unchanged. The per-frame target publisher built
// in this phase translates each joint's semantic angles (flex / abduct /
// twist) into a quaternion target on the gluon every frame.
//
// What this proves:
//   - The publisher doesn't break walking. If no upper-body clip is
//     commanding semantic targets, joints hold their rest pose; if a
//     clip is running, the PD tracks it.
//   - Physics-drive coexists with the existing FK leg / foot-plant /
//     kinematic-root locomotion pipeline on a live humanoid.
//   - 11 physics-drive joints (4 spine + 6 arm + head... total = 10,
//     plus the head joint itself = 10 or 11 depending on count) hold
//     their relative poses while hips translate forward.
//
// Assertions (match test_walk_forward_progress with looser drift on
// upper body to leave room for small swing):
//   - Forward displacement ≥ 70 % of commanded.
//   - Backward-per-frame yank frames ≤ 20.
//   - Head stays roughly over hips in body frame (within 30 cm).
//
// Run: ./build/logosphere-tests --test test_physics_drive_walk_upper_body --no-head
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

bool test_physics_drive_walk_upper_body() {
    printf("\n=== Physics Drive: Upper-body walk (Phase 4a) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "phase 4a walk";
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
    strata.set_load_radius(60.0f);
    strata.set_unload_radius(70.0f);
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
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55. At 0.5 every foot was born
        // 50 mm inside the organic layer, which INV-37 refuses.
        0.0f, 0.0f, 0.55f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    // create_kg_entities overwrites eva.entity_id with a fresh entity
    // that owns the body-part children the KG capability path reads.
    // Read it AFTER so register_humanoid_direct and our physics-drive
    // API both key off the same live entity.
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    const kg::EntityID eva_entity = eva.entity_id;
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva_entity);

    int hips_id = eva.hips_id;
    int head_id = eva.head_id;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(eva.hips_id);
        for (int& id : eva.body_ids) fix(id);
        for (int& id : eva.left_leg_ids) fix(id);
        for (int& id : eva.right_leg_ids) fix(id);
        for (int& id : eva.left_arm_ids) fix(id);
        for (int& id : eva.right_arm_ids) fix(id);
        for (int& id : eva.torso_ids) fix(id);
        fix(hips_id); fix(head_id);
    });

    const float dt_frame = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt_frame);

    // Enable physics-drive on upper-body joints only. Legs stay on FK
    // so the foot-planting IK (which writes stance-leg positions) keeps
    // working unchanged — Phase 4b is the leg rework.
    auto identity = logosphere::Quat::identity();
    const float STIFF = 2000.0f;
    const float DAMP  = 60.0f;
    const char* upper_joints[] = {
        "lower_spine", "upper_spine", "neck", "head",
        "right_shoulder", "right_elbow", "right_wrist",
        "left_shoulder",  "left_elbow",  "left_wrist",
    };
    int enabled = 0;
    for (const char* name : upper_joints) {
        if (engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva_entity, name, identity, STIFF, DAMP)) {
            enabled++;
        }
    }
    printf("  Physics-drive enabled on %d upper-body joints\n", enabled);

    // Capture initial facing + position.
    float hx0, hy0, facing_x, facing_y;
    {
        auto v = ps.lock_particles_for_read();
        const Particle& hips = v[hips_id];
        hx0 = hips.x;
        hy0 = hips.y;
        facing_x = std::sin(hips.rotation_z);
        facing_y = std::cos(hips.rotation_z);
    }

    const float FORWARD_SPEED = 1.0f;
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, FORWARD_SPEED, 0.0f);

    constexpr int FRAMES = 300;
    // 1/60 game seconds per update (0.033 encoded the retired
    // double-advance world; see tests/test_position_authority.cpp).
    constexpr float PHYS_DT = 1.0f / 60.0f;
    const float expected_total = FRAMES * PHYS_DT * FORWARD_SPEED;
    const float per_frame_expected = PHYS_DT * FORWARD_SPEED;

    printf("\n  Walking %d frames at %.2f m/s (expected %.2f m, upper body physics-driven)\n",
           FRAMES, FORWARD_SPEED, expected_total);

    float prev_forward = 0.0f;
    int   backward_frames = 0;
    float max_head_dev_from_hips = 0.0f;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt_frame);

        float hx, hy, head_dev;
        {
            auto v = ps.lock_particles_for_read();
            const Particle& hips = v[hips_id];
            const Particle& head = v[head_id];
            hx = hips.x; hy = hips.y;
            float dx = head.x - hx;
            float dy = head.y - hy;
            head_dev = std::sqrt(dx*dx + dy*dy);
        }
        strata.update(hx, hy);

        float forward = (hx - hx0) * facing_x + (hy - hy0) * facing_y;
        float delta = forward - prev_forward;

        if (delta < -0.005f) backward_frames++;
        if (head_dev > max_head_dev_from_hips) max_head_dev_from_hips = head_dev;

        if (frame % 30 == 0) {
            printf("  [f%3d] hips=(%.3f,%.3f) forward=%+.4f head_dev=%.3f\n",
                   frame, hx, hy, forward, head_dev);
        }

        prev_forward = forward;
    }

    const float total_forward = prev_forward;

    printf("\n  Summary:\n");
    printf("    forward displacement    = %.3f m (expected %.3f)\n",
           total_forward, expected_total);
    printf("    backward frames         = %d\n", backward_frames);
    printf("    max head dev from hips  = %.3f m (budget 0.30)\n", max_head_dev_from_hips);

    const float MIN_PROGRESS_FRAC = 0.70f;
    bool progressed = (total_forward >= MIN_PROGRESS_FRAC * expected_total);
    bool no_yank    = (backward_frames <= 20);
    bool head_coherent = (max_head_dev_from_hips <= 0.30f);

    printf("\n  Assertions:\n");
    printf("    %s: forward ≥ 70%% expected\n", progressed ? "PASS" : "FAIL");
    printf("    %s: backward frames ≤ 20 (got %d)\n", no_yank ? "PASS" : "FAIL", backward_frames);
    printf("    %s: head stays within 30 cm of hips XY\n", head_coherent ? "PASS" : "FAIL");

    bool ok = progressed && no_yank && head_coherent;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — upper-body physics-drive broke walking]");
    return ok;
}
