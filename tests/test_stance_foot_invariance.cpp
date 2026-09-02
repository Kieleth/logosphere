// =============================================================================
// STANCE FOOT INVARIANCE — kinematic-root guard test
// =============================================================================
// The invariant this test codifies:
//
//     When a humanoid is walking on a flat floor, at every frame the
//     LOWER of the two foot Z values must stay close to the floor top.
//
// One foot is always planted (stance); the other swings through the air.
// min(l_foot.z, r_foot.z) therefore tracks the ground. If the minimum
// rises over time, both feet are off the ground simultaneously and
// climbing — the body is ratcheting upward. That's the spider-Eva
// mechanism: hip_dip + ground_correct + plant_target_z work asymmetrically
// and the stance foot drifts upward step by step.
//
// Role: GUARD, not RED→GREEN driver. On the current (pre-refactor) code,
// pure-headless walking via set_body_relative_velocity does not reproduce
// Eden's ratchet — chunk boundary crossings and mouse-driven rotation
// amplify the feedback loop in ways straight-line headless walking does
// not. So this test passes TODAY, and must keep passing through Stages 2
// and 3 of the kinematic-root refactor. If it flips to RED during the
// refactor, something in the new root machinery is wrong.
//
// The actual RED→GREEN driver for Stage 1 is test_reverse_leg_chain.
//
// Run: ./build/logosphere-tests --test test_stance_foot_invariance --no-head
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
#include <algorithm>

bool test_stance_foot_invariance() {
    printf("\n=== Stance Foot Invariance (kinematic-root wedge) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "stance foot invariance";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // --- Flat strata floor, Eden-matched config.
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

    const float floor_top_z = 0.55f;  // sum of layer thicknesses, flat

    // --- Eva at origin.
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55. At 0.5 every foot was born
        // 50 mm inside the organic layer, which INV-37 refuses.
        0.0f, 0.0f, 0.55f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    int l_foot_id = eva.left_leg_ids.empty()  ? -1 : eva.left_leg_ids[0];
    int r_foot_id = eva.right_leg_ids.empty() ? -1 : eva.right_leg_ids[0];
    int hips_id   = eva.hips_id;
    if (l_foot_id < 0 || r_foot_id < 0) {
        printf("  ERROR: could not resolve foot particle IDs\n");
        return false;
    }

    // Swap-safe ID tracking.
    int& lf_ref = l_foot_id;
    int& rf_ref = r_foot_id;
    int& hp_ref = hips_id;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(lf_ref); fix(rf_ref); fix(hp_ref);
    });

    // --- Settle 30 frames.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    float settle_min_foot_z = 1e9f;
    {
        auto v = ps.lock_particles_for_read();
        float lfz = v[lf_ref].z - v[lf_ref].thickness * 0.5f;
        float rfz = v[rf_ref].z - v[rf_ref].thickness * 0.5f;
        settle_min_foot_z = std::min(lfz, rfz);
    }
    printf("  After settle: min(foot.bottom.z) = %.3f m (floor top = %.3f)\n",
           settle_min_foot_z, floor_top_z);

    // --- Walk forward at 3 m/s for 200 frames.
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, 3.0f, 0.0f);

    // The invariant: at every frame, the LOWER of the two foot bottoms
    // must stay within RISE_LIMIT of the floor top. Normal walking keeps
    // one foot planted at all times, so this minimum tracks the ground.
    // Ratcheting lifts both feet → this fails.
    constexpr float RISE_LIMIT = 0.15f;   // 15 cm is very generous

    constexpr int MAX_FRAMES = 200;
    float max_min_foot_bottom = -1e9f;
    int   max_min_frame = -1;
    bool  breached = false;
    int   breach_frame = -1;
    float breach_value = 0.0f;

    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        float hx = 0.0f, hy = 0.0f;
        float min_foot_bottom = 1e9f;
        {
            auto v = ps.lock_particles_for_read();
            const Particle& hips  = v[hp_ref];
            const Particle& lfoot = v[lf_ref];
            const Particle& rfoot = v[rf_ref];
            hx = hips.x; hy = hips.y;
            float lfb = lfoot.z - lfoot.thickness * 0.5f;
            float rfb = rfoot.z - rfoot.thickness * 0.5f;
            min_foot_bottom = std::min(lfb, rfb);
        }
        engine.update(dt);
        strata.update(hx, hy);

        if (min_foot_bottom > max_min_foot_bottom) {
            max_min_foot_bottom = min_foot_bottom;
            max_min_frame = frame;
        }
        if (!breached && min_foot_bottom > floor_top_z + RISE_LIMIT) {
            breached     = true;
            breach_frame = frame;
            breach_value = min_foot_bottom;
            printf("  >>> INVARIANT BROKEN f%d: min(foot.bottom.z)=%.3f "
                   "(floor top %.3f + limit %.2f = %.3f)\n",
                   frame, min_foot_bottom, floor_top_z, RISE_LIMIT,
                   floor_top_z + RISE_LIMIT);
        }

        if (frame % 40 == 0) {
            printf("  [f%3d] hips=(%.2f,%.2f)  min(foot.bottom.z)=%.3f\n",
                   frame, hx, hy, min_foot_bottom);
        }
    }

    printf("\n  Summary:\n");
    printf("    settle min(foot.bottom.z) = %.3f\n", settle_min_foot_z);
    printf("    max lower-foot bottom over walk = %.3f @ f%d\n",
           max_min_foot_bottom, max_min_frame);
    printf("    floor top + %.2f = %.3f (invariant ceiling)\n",
           RISE_LIMIT, floor_top_z + RISE_LIMIT);

    bool ok = !breached;
    if (!ok) {
        printf("    FIRST BREACH: f%d  value=%.3f\n", breach_frame, breach_value);
    }
    printf("  %s\n", ok ? "[PASS]" : "[FAIL — stance foot ratcheting off ground]");
    return ok;
}
