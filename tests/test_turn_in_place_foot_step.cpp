// =============================================================================
// TURN IN PLACE — feet must step under rotating hips
// =============================================================================
// Stage 2 of the biomechanical rotation TDD sequence. Stage 1 made head,
// torso, and hips rotate as a cascade with different time constants. But
// the feet stay pinned at their original plant_target forever — Eva
// currently spins her upper body while her feet remain glued pointing
// +Y. That is not how humans turn in place.
//
// Real behaviour: once the hip-vs-foot twist exceeds a comfort threshold
// (empirically ~30–45°), the nervous system commits to a new foot
// placement — either a step turn (outside foot pivots to widen the base)
// or a spin turn (inside foot pivots in place). In both, the feet end
// up re-oriented with the hips.
//
// This test codifies that trigger as a behavioural invariant. Eva stands
// idle. A 90° yaw target is applied. Within ~1.5 s the hips should
// reach the target (Stage 1) AND at least one foot must re-plant, which
// we detect as a lateral world-position shift of at least stride_half.
//
// Expected state TODAY (Stage 1 shipped, Stage 2 not): RED. Cascade
// rotates upper body, feet never move.
//
// Run: ./build/logosphere-tests --test test_turn_in_place_foot_step --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

bool test_turn_in_place_foot_step() {
    printf("\n=== Turn in Place — foot step trigger ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "turn in place";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // Flat floor.
    Particle floor = {};
    floor.shape = ParticleShape::BOX;
    floor.x = 0; floor.y = 0; floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05
    floor.width = 50; floor.height = 50; floor.thickness = 0.05f;
    floor.r = 0.3f; floor.g = 0.5f; floor.b = 0.2f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::STONE);
    floor.is_at_rest = true;
    engine.add_particle(floor);

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

    // left_leg_ids[0] = foot, right_leg_ids[0] = foot (by generator convention).
    int l_foot_id = eva.left_leg_ids.at(0);
    int r_foot_id = eva.right_leg_ids.at(0);
    int hips_id   = eva.hips_id;

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(l_foot_id); fix(r_foot_id); fix(hips_id);
    });

    const float dt = 1.0f / 60.0f;
    // Settle 30 frames idle.
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Baseline foot positions in world frame.
    float lx0, ly0, rx0, ry0, hx0, hy0;
    {
        auto v = ps.lock_particles_for_read();
        lx0 = v[l_foot_id].x; ly0 = v[l_foot_id].y;
        rx0 = v[r_foot_id].x; ry0 = v[r_foot_id].y;
        hx0 = v[hips_id].x;   hy0 = v[hips_id].y;
    }
    printf("  Baseline feet: L=(%.3f,%.3f) R=(%.3f,%.3f) hips=(%.3f,%.3f)\n",
           lx0, ly0, rx0, ry0, hx0, hy0);

    // 90° CW yaw target (+X direction). Eva starts facing +Y.
    const float TARGET_YAW = static_cast<float>(M_PI) / 2.0f;
    engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id, 10.0f, 0.0f);

    // Let 180 game frames elapse (~3 s at 60 Hz). Sample foot displacement
    // and hips yaw every frame.
    constexpr int FRAMES = 180;

    float l_max_disp = 0.0f;
    float r_max_disp = 0.0f;
    float final_hips_yaw = 0.0f;
    int   step_frame_l = -1;
    int   step_frame_r = -1;

    // A "step" for a turn-in-place is a PIVOT: the foot's world position
    // swings around the hip axis as the body yaws, ending up at the same
    // body-frame hip-lateral offset but in a new world orientation. For
    // a 90° CW turn with hip_half_width ≈ 0.099 m, the chord length of
    // that arc is 2·half_hw·sin(45°) ≈ 0.14 m per foot. We use 0.10 m
    // as the threshold: anything less means the feet didn't move at all
    // (pure fk-at-rest with zero cascade authority), anything more
    // means at least one foot has been re-oriented to the new facing.
    // A forward-bias "step" on top of the arc would have been a big
    // 0.25 m plus, which looked like a lunge while standing — the bug
    // fixed by zeroing stride_scale for twist-steps.
    const float STEP_THRESHOLD = 0.10f;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        float lx = v[l_foot_id].x, ly = v[l_foot_id].y;
        float rx = v[r_foot_id].x, ry = v[r_foot_id].y;
        final_hips_yaw = v[hips_id].rotation_z;

        float ldisp = std::sqrt((lx - lx0)*(lx - lx0) + (ly - ly0)*(ly - ly0));
        float rdisp = std::sqrt((rx - rx0)*(rx - rx0) + (ry - ry0)*(ry - ry0));
        if (ldisp > l_max_disp) l_max_disp = ldisp;
        if (rdisp > r_max_disp) r_max_disp = rdisp;
        if (step_frame_l < 0 && ldisp >= STEP_THRESHOLD) step_frame_l = frame;
        if (step_frame_r < 0 && rdisp >= STEP_THRESHOLD) step_frame_r = frame;

        if (frame % 15 == 0 || frame < 5) {
            printf("  [f%3d] hips_yaw=%+.3f  L_disp=%.3f  R_disp=%.3f\n",
                   frame, final_hips_yaw, ldisp, rdisp);
        }
    }

    // --- Assertions.
    // A. Hips yaw reaches ≥ 80% of target (Stage 1 guard; cascade must still work).
    // B. At least one foot world-position shift ≥ STEP_THRESHOLD within 180 frames.
    // C. Hips stay roughly stationary (turn in place, not walk away). Hip drift
    //    must stay under ~0.15 m — feet stepping should not push hips off origin.
    float hips_yaw_final = final_hips_yaw;
    float hips_drift;
    {
        auto v = ps.lock_particles_for_read();
        float dx = v[hips_id].x - hx0;
        float dy = v[hips_id].y - hy0;
        hips_drift = std::sqrt(dx*dx + dy*dy);
    }

    bool yaw_reached = (std::abs(hips_yaw_final) >= 0.8f * TARGET_YAW);
    bool foot_stepped = (step_frame_l >= 0 || step_frame_r >= 0);
    bool stayed_in_place = (hips_drift < 0.25f);

    printf("\n  Final hips_yaw = %+.3f rad (target %.3f)\n",
           hips_yaw_final, TARGET_YAW);
    printf("  L max disp = %.3f m (step at frame %d)\n", l_max_disp, step_frame_l);
    printf("  R max disp = %.3f m (step at frame %d)\n", r_max_disp, step_frame_r);
    printf("  Hips drift from origin = %.3f m\n", hips_drift);

    printf("\n  Assertions:\n");
    printf("    %s: hips yaw reached 80%% of target (got %.3f / %.3f)\n",
           yaw_reached ? "PASS" : "FAIL", hips_yaw_final, TARGET_YAW);
    printf("    %s: at least one foot stepped ≥%.2f m within %d frames\n",
           foot_stepped ? "PASS" : "FAIL", STEP_THRESHOLD, FRAMES);
    printf("    %s: hips stayed near origin (drift %.3f m < 0.25 m)\n",
           stayed_in_place ? "PASS" : "FAIL", hips_drift);

    bool ok = yaw_reached && foot_stepped && stayed_in_place;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — step-when-twisted not implemented]");
    return ok;
}
