// =============================================================================
// STRAFE FORWARD PROGRESS — body moves in the intent direction
// =============================================================================
// Counterpart to test_walk_forward_progress. When Eva is driven with a
// body-relative RIGHT velocity (strafe), hips must translate IN THAT
// DIRECTION at approximately that speed — not along the facing vector.
//
// Historical bug (2026-04-17): plant_target computed its "ahead" vector
// from facing direction only (sin_f, cos_f). While strafing, body moves
// sideways but foot planted forward — the stance foot ended up offset
// from where the body was heading, producing visible weirdness.
//
// Expected post-fix: hips moves at intent speed in the intent direction
// regardless of which body-local axis (forward / right / diagonal) was
// used to drive it.
//
// Run: ./build/logosphere-tests --test test_strafe_progress --no-head
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

// Run one directional-walk scenario and return pass/fail.
// forward, right are body-relative inputs passed to
// set_body_relative_velocity. The test projects actual motion onto the
// expected world direction (from initial facing) and asserts >=70% of
// intent distance.
static bool run_walk_direction(const char* label,
                               float forward, float right,
                               float intent_speed)
{
    printf("\n--- %s (fwd=%+.1f right=%+.1f)\n", label, forward, right);

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "strafe progress";
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
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55 (INV-37).
        0.0f, 0.0f, 0.55f, -1, HumanoidSpec::eva(), false);
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

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    float hx0, hy0, facing_x, facing_y, right_x, right_y;
    {
        auto v = ps.lock_particles_for_read();
        const Particle& hips = v[hp_ref];
        hx0 = hips.x; hy0 = hips.y;
        float rot = hips.rotation_z;
        facing_x = std::sin(rot);
        facing_y = std::cos(rot);
        // Body-right: 90° CW from facing. rotate facing by -pi/2.
        right_x = facing_y;   // cos(rot - pi/2) = sin(rot) rotated — simpler: (cos(rot), -sin(rot))
        right_y = -facing_x;
    }
    // Expected motion direction in world frame: forward*facing + right*body_right
    float move_dx = forward * facing_x + right * right_x;
    float move_dy = forward * facing_y + right * right_y;
    float move_mag = std::sqrt(move_dx*move_dx + move_dy*move_dy);
    if (move_mag < 1e-5f) { printf("  ERROR: zero motion direction\n"); return false; }
    float move_ux = move_dx / move_mag;
    float move_uy = move_dy / move_mag;

    printf("  settle: hips=(%.3f,%.3f) facing=(%.2f,%.2f) move_dir=(%.2f,%.2f)\n",
           hx0, hy0, facing_x, facing_y, move_ux, move_uy);

    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, forward, right);

    constexpr int FRAMES = 300;
    // 1/60 game seconds per update (0.033 encoded the retired
    // double-advance world; see tests/test_position_authority.cpp).
    constexpr float PHYS_DT = 1.0f / 60.0f;
    const float expected_total = FRAMES * PHYS_DT * intent_speed;

    float prev_along = 0.0f, max_along = 0.0f;
    int backward_frames = 0;

    for (int frame = 0; frame < FRAMES; ++frame) {
        float hx, hy;
        {
            auto v = ps.lock_particles_for_read();
            hx = v[hp_ref].x; hy = v[hp_ref].y;
        }
        engine.update(dt);
        strata.update(hx, hy);

        float along = (hx - hx0) * move_ux + (hy - hy0) * move_uy;
        float delta = along - prev_along;
        if (along > max_along) max_along = along;
        if (delta < -0.005f) backward_frames++;
        prev_along = along;
    }

    float total = prev_along;
    printf("  final: hips=(%.3f,%.3f)  along_move_dir=%.3f m (expected %.3f)\n",
           (hx0 + total * move_ux), (hy0 + total * move_uy), total, expected_total);
    printf("  backward frames: %d\n", backward_frames);

    const float MIN_PROGRESS_FRAC = 0.70f;
    bool progressed = (total >= MIN_PROGRESS_FRAC * expected_total);
    bool no_yank    = (backward_frames <= 20);
    bool ok = progressed && no_yank;
    printf("  %s (progress %.1f%% of intent, %d backward frames)\n",
           ok ? "PASS" : "FAIL",
           100.0f * total / expected_total,
           backward_frames);
    return ok;
}

bool test_strafe_progress() {
    printf("\n=== Strafe Forward Progress ===\n");

    bool a = run_walk_direction("Forward (Y+)",       1.0f,  0.0f, 1.0f);
    bool b = run_walk_direction("Strafe right (X+)",  0.0f,  1.0f, 1.0f);
    bool c = run_walk_direction("Strafe left (X-)",   0.0f, -1.0f, 1.0f);
    bool d = run_walk_direction("Backward (Y-)",     -1.0f,  0.0f, 1.0f);
    bool e = run_walk_direction("Diag fwd-right",     0.707f, 0.707f, 1.0f);

    bool all = a && b && c && d && e;
    printf("\n  %s\n", all ? "[PASS]" : "[FAIL — one or more directions broken]");
    return all;
}
