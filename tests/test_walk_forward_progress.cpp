// =============================================================================
// WALK FORWARD PROGRESS — kinematic-root post-shift regression wedge
// =============================================================================
// The invariant this test codifies:
//
//     When Eva is set to walk forward at a constant body-relative velocity,
//     hips must translate forward in world space at approximately that
//     speed. No treadmilling, no yank-back-per-frame, no anchor-shift
//     cancellation.
//
// Context: feat/kinematic-root shipped a post-FK anchor shift that pins
// the stance foot to root.anchor_world every frame. This fixed the
// spider-Eva dismemberment (Z-axis ratchet). Eden visual testing
// reports walking is now broken — the body is intact but not
// advancing correctly.
//
// Diagnosis (see plan): at heel-strike, plant_target_x/y is computed
// from hips + stride_ahead, while anchor_world.x/y is captured from
// the FK-placed foot's current world position. These disagree by the
// FK walk clip's reach vs. dynamics.walk_stride_length mismatch. The
// per-frame shift translates the body by that mismatch, cancelling
// velocity-driven forward motion.
//
// Expected behavior (this test, once fix lands):
//   - Total horizontal displacement ≈ vx * total_time, within 70%.
//   - No frame shows significant backward motion (> 5 mm against the
//     intended direction), which would indicate yanked-back stutter.
//
// Expected state TODAY: RED. The fix is pending.
//
// Run: ./build/logosphere-tests --test test_walk_forward_progress --no-head
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

bool test_walk_forward_progress() {
    printf("\n=== Walk Forward Progress (kinematic-root post-shift wedge) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "walk forward progress";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // --- Strata floor, Eden-matched.
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

    // --- Eva at origin.
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    int hips_id = eva.hips_id;
    int& hp_ref = hips_id;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(eva.hips_id);
        for (int& id : eva.body_ids) fix(id);
        for (int& id : eva.left_leg_ids) fix(id);
        for (int& id : eva.right_leg_ids) fix(id);
        for (int& id : eva.left_arm_ids) fix(id);
        for (int& id : eva.right_arm_ids) fix(id);
        for (int& id : eva.torso_ids) fix(id);
        if (hp_ref == (int)old_idx) hp_ref = (int)new_idx;
    });

    // --- Settle.
    const float dt_frame = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt_frame);

    // Record initial facing and position. Body-relative forward lies
    // along the (sin(rot_z), cos(rot_z)) world vector for this engine.
    float hx0, hy0, facing_x, facing_y;
    {
        auto v = ps.lock_particles_for_read();
        const Particle& hips = v[hp_ref];
        hx0 = hips.x;
        hy0 = hips.y;
        facing_x = std::sin(hips.rotation_z);
        facing_y = std::cos(hips.rotation_z);
    }
    printf("  After settle: hips=(%.3f, %.3f)  facing=(%.2f, %.2f)\n",
           hx0, hy0, facing_x, facing_y);

    // --- Drive forward at 1 m/s for a fixed duration.
    const float FORWARD_SPEED = 1.0f;
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, FORWARD_SPEED, 0.0f);

    constexpr int FRAMES = 300;
    // Each engine.update(1/60) simulates 1/60 s; the hips (DYNAMICS-
    // owned) integrate once per update. The old 0.033 here encoded the
    // pre-2026-07 double-advance world (the deleted universal
    // ParticleSystem::update loop added a second v·dt per frame). See
    // tests/test_position_authority.cpp for the contract.
    constexpr float PHYS_DT = 1.0f / 60.0f;
    const float expected_total = FRAMES * PHYS_DT * FORWARD_SPEED;   // ≈ 9.9 m
    const float per_frame_expected = PHYS_DT * FORWARD_SPEED;        // ≈ 3.3 cm

    printf("\n  Walking %d frames at %.2f m/s forward (expected %.2f m total)\n",
           FRAMES, FORWARD_SPEED, expected_total);

    float prev_forward = 0.0f;
    float max_forward = 0.0f;
    float min_forward = 0.0f;
    int   backward_frames = 0;          // forward delta < -5 mm
    int   stall_frames = 0;             // forward delta < 25% of expected per-frame
    float max_backward_magnitude = 0.0f;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt_frame);

        float hx, hy;
        {
            auto v = ps.lock_particles_for_read();
            const Particle& hips = v[hp_ref];
            hx = hips.x; hy = hips.y;
        }
        strata.update(hx, hy);

        // Project (hx - hx0, hy - hy0) onto the initial facing vector.
        // Using the initial facing (not per-frame) so any drift in
        // rotation doesn't muddy the measurement.
        float forward = (hx - hx0) * facing_x + (hy - hy0) * facing_y;
        float delta = forward - prev_forward;

        if (forward > max_forward) max_forward = forward;
        if (forward < min_forward) min_forward = forward;
        if (delta < -0.005f) {
            backward_frames++;
            float mag = -delta;
            if (mag > max_backward_magnitude) max_backward_magnitude = mag;
        }
        if (delta < 0.25f * per_frame_expected) stall_frames++;

        if (frame % 30 == 0) {
            printf("  [f%3d] hips=(%.3f,%.3f)  forward=%+.4f m  Δ=%+.4f m\n",
                   frame, hx, hy, forward, delta);
        }

        prev_forward = forward;
    }

    const float total_forward = prev_forward;

    printf("\n  Summary:\n");
    printf("    forward displacement  = %.3f m (expected %.3f)\n",
           total_forward, expected_total);
    printf("    max forward           = %.3f m\n", max_forward);
    printf("    min forward           = %.3f m (should not go negative)\n", min_forward);
    printf("    backward frames (Δ < -5 mm) = %d\n", backward_frames);
    printf("    max single-frame backward   = %.4f m\n", max_backward_magnitude);
    printf("    stall frames (Δ < 25%% expected) = %d / %d\n", stall_frames, FRAMES);

    // --- Assertions.
    const float MIN_PROGRESS_FRAC = 0.70f;       // Forward motion ≥ 70% of intended
    const int   MAX_BACKWARD_FRAMES = 20;        // Allow some gait retreat but not per-frame yank
    const int   MAX_STALL_FRAMES    = FRAMES/2;  // At least half the frames advancing meaningfully

    bool progressed = (total_forward >= MIN_PROGRESS_FRAC * expected_total);
    bool no_yank    = (backward_frames <= MAX_BACKWARD_FRAMES);
    bool no_stall   = (stall_frames    <= MAX_STALL_FRAMES);

    printf("\n  Assertions:\n");
    printf("    %s: forward ≥ %.0f%% expected (%.3f / %.3f m)\n",
           progressed ? "PASS" : "FAIL",
           MIN_PROGRESS_FRAC * 100.0f, total_forward, expected_total);
    printf("    %s: backward frames ≤ %d (got %d)\n",
           no_yank ? "PASS" : "FAIL", MAX_BACKWARD_FRAMES, backward_frames);
    printf("    %s: stall frames ≤ %d (got %d)\n",
           no_stall ? "PASS" : "FAIL", MAX_STALL_FRAMES, stall_frames);

    bool ok = progressed && no_yank && no_stall;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — walking broken]");
    return ok;
}
