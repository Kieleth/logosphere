// =============================================================================
// PHYSICS DRIVE — WALK WITH PHYSICS-DRIVEN LEGS (Phase 4b capstone, 1 m/s)
// =============================================================================
// Walks Eva forward at 1 m/s on the default Phase 5 physics-drive legs path.
// The foot-planting IK solves the 2-bone IK for hip + knee rotations and
// publishes them as parent-relative quaternion targets on the stance-side
// hip and knee gluons (no particle position writes). The pin gluon holds
// the stance foot at plant_target with stiffness ramped by plant_blend.
// Upper body runs on Phase 4a identity-targets so the full body is
// solver-driven.
//
// Originated as the C3 walk-legs proof, restored from
// archive/physics-drive-2026-04 with three deltas: create_display=false
// instead of EngineMode::Headless, humanoid API lives on
// HumanoidLocomotion, and Phase 5 made the path default-on (so no
// opt-in call is needed; register_humanoid_direct wires it).
//
// Assertions (looser than test_walk_forward_progress's; the gluon-based
// drive reaches plant_target more slowly than direct position writes):
//   - Forward displacement ≥ 50 % of commanded.
//   - No teleports (no single-frame hips delta > 0.30 m).
//   - Backward-per-frame frames ≤ 40.
//
// Companion: test_physics_drive_walk_legs_fast at 2 m/s (substepping
// proof).
//
// Run: ./build/logosphere-tests --test test_physics_drive_walk_legs --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_physics_drive_walk_legs() {
    printf("\n=== Physics Drive: Walk with physics-driven legs (Phase 4b C3) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "phase 4b walk legs";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& humanoid = engine.get_humanoid_locomotion();

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
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    const kg::EntityID eva_entity = eva.entity_id;
    humanoid.register_humanoid_direct(
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

    // Upper body on Phase 4a path (identity targets, held by PD).
    auto identity = logosphere::Quat::identity();
    const float UPPER_STIFF = 2000.0f;
    const float UPPER_DAMP  = 60.0f;
    const char* upper_joints[] = {
        "lower_spine", "upper_spine", "neck", "head",
        "right_shoulder", "right_elbow", "right_wrist",
        "left_shoulder",  "left_elbow",  "left_wrist",
    };
    int upper_enabled = 0;
    for (const char* name : upper_joints) {
        if (humanoid.set_joint_physics_drive_q(eva_entity, name, identity, UPPER_STIFF, UPPER_DAMP)) {
            upper_enabled++;
        }
    }
    printf("  Upper body: physics-drive on %d joints\n", upper_enabled);

    // Phase 5: physics-drive legs is the default for every humanoid;
    // register_humanoid_direct already initialized the leg-particle
    // flip + gluon angular profile, so the IK→gluon publish path
    // is live with no opt-in.

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
    humanoid.set_volitional(eva.hips_id, true);
    humanoid.set_body_relative_velocity(eva.hips_id, FORWARD_SPEED, 0.0f);

    constexpr int FRAMES = 300;
    // 1/60 game seconds per update (0.033 encoded the retired
    // double-advance world; see tests/test_position_authority.cpp).
    constexpr float PHYS_DT = 1.0f / 60.0f;
    const float expected_total = FRAMES * PHYS_DT * FORWARD_SPEED;

    printf("\n  Walking %d frames at %.2f m/s (expected %.2f m, full body physics-driven)\n",
           FRAMES, FORWARD_SPEED, expected_total);

    float prev_forward = 0.0f;
    int   backward_frames = 0;
    float max_hips_teleport = 0.0f;
    float prev_hx = hx0, prev_hy = hy0;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt_frame);

        float hx, hy;
        {
            auto v = ps.lock_particles_for_read();
            const Particle& hips = v[hips_id];
            hx = hips.x; hy = hips.y;
        }
        strata.update(hx, hy);

        float frame_dx = hx - prev_hx;
        float frame_dy = hy - prev_hy;
        float frame_delta_len = std::sqrt(frame_dx*frame_dx + frame_dy*frame_dy);
        if (frame_delta_len > max_hips_teleport) max_hips_teleport = frame_delta_len;

        float forward = (hx - hx0) * facing_x + (hy - hy0) * facing_y;
        float delta = forward - prev_forward;
        if (delta < -0.005f) backward_frames++;

        if (frame % 30 == 0) {
            printf("  [f%3d] hips=(%.3f,%.3f) forward=%+.4f Δframe=%.4f\n",
                   frame, hx, hy, forward, frame_delta_len);
        }

        prev_forward = forward;
        prev_hx = hx;
        prev_hy = hy;
    }

    const float total_forward = prev_forward;

    printf("\n  Summary:\n");
    printf("    forward displacement    = %.3f m (expected %.3f)\n",
           total_forward, expected_total);
    printf("    backward frames         = %d\n", backward_frames);
    printf("    max per-frame hips Δ    = %.3f m\n", max_hips_teleport);

    const float MIN_PROGRESS_FRAC = 0.50f;
    bool progressed = (total_forward >= MIN_PROGRESS_FRAC * expected_total);
    bool no_yank    = (backward_frames <= 40);
    bool no_teleport = (max_hips_teleport <= 0.30f);

    printf("\n  Assertions:\n");
    printf("    %s: forward >= 50%% expected\n", progressed ? "PASS" : "FAIL");
    printf("    %s: backward frames <= 40 (got %d)\n", no_yank ? "PASS" : "FAIL", backward_frames);
    printf("    %s: no hips teleport (max %.3f m <= 0.30)\n", no_teleport ? "PASS" : "FAIL", max_hips_teleport);

    bool ok = progressed && no_yank && no_teleport;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — physics-driven legs broke walking]");
    return ok;
}
