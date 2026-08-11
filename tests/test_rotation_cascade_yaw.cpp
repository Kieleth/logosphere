// =============================================================================
// ROTATION CASCADE — head leads, torso follows, hips catch up
// =============================================================================
// Real human turning is not a rigid-body spin. The documented motor-control
// sequence during locomotor steering is:
//
//   eyes    (0 ms reference)
//   head    ~ 100 ms later
//   torso   ~ 100–200 ms after head
//   hips    ~ 100 ms after torso
//   feet    discrete, up to 1.5 s after the gaze shift
//
// Between hip rotation and feet stepping, the body TWISTS — hips lead, upper
// body lags — producing the characteristic "check the shoulder before you
// turn" look that feels natural to the eye. Our current implementation has
// none of this cascade: a single rotation_z is propagated to every particle
// uniformly each frame.
//
// This test codifies the cascade as a behavioural invariant. Eva stands
// idle. A 90° yaw target is applied. Over the next second of simulation,
// head/chest/hips rotation_z should:
//
//   1. Reach the target at different times (head first, hips last).
//   2. Satisfy head ≥ chest ≥ hips at every frame during the transient
//      (the cascade ordering).
//
// Expected state TODAY: RED. Our single-rotation model has head, chest,
// and hips all matching hips.rotation_z exactly from frame 1.
//
// Run: ./build/logosphere-tests --test test_rotation_cascade_yaw --no-head
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

// Helper — normalize angle into (-pi, +pi] so we can compare deltas.
static float normalize_angle(float a) {
    while (a > static_cast<float>(M_PI))  a -= 2.0f * static_cast<float>(M_PI);
    while (a <= -static_cast<float>(M_PI)) a += 2.0f * static_cast<float>(M_PI);
    return a;
}

bool test_rotation_cascade_yaw() {
    printf("\n=== Rotation Cascade (head → torso → hips) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "rotation cascade";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // Simple flat floor so we have something to stand on.
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

    // body_ids indices (see humanoid_generator.cpp):
    //   8: hips, 10: chest, 12: head
    int hips_id  = eva.body_ids.at(8);
    int chest_id = eva.body_ids.at(10);
    int head_id  = eva.body_ids.at(12);

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(hips_id); fix(chest_id); fix(head_id);
    });

    const float dt = 1.0f / 60.0f;
    // Settle 30 frames — idle, no input.
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Baseline facing (should be near 0 / +Y).
    float base_yaw;
    {
        auto v = ps.lock_particles_for_read();
        base_yaw = v[hips_id].rotation_z;
    }
    printf("  Baseline yaw: %.3f rad (%.1f°)\n",
           base_yaw, base_yaw * 180.0f / static_cast<float>(M_PI));

    // --- Apply a 90° yaw target: place the look-at target off to the right.
    // Eva is at origin facing +Y. 90° CW (right) target is +X direction.
    // In the engine's angle convention, target_angle = atan2(dx, dy).
    // For (+X, 0) that's atan2(1, 0) = π/2.
    const float TARGET_YAW = static_cast<float>(M_PI) / 2.0f;  // +90° CW
    float tx = 10.0f;  // well out to the right
    float ty = 0.0f;
    engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id, tx, ty);

    // Let it evolve. Walk cycle disabled by default when not volitional.
    // We run 120 game frames (~2 s at 60 Hz) and sample every frame.
    constexpr int FRAMES = 120;

    struct Sample {
        int    frame;
        float  head_yaw;
        float  chest_yaw;
        float  hips_yaw;
    };
    std::vector<Sample> samples;
    samples.reserve(FRAMES);

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        Sample s;
        s.frame     = frame;
        s.head_yaw  = normalize_angle(v[head_id].rotation_z  - base_yaw);
        s.chest_yaw = normalize_angle(v[chest_id].rotation_z - base_yaw);
        s.hips_yaw  = normalize_angle(v[hips_id].rotation_z  - base_yaw);
        samples.push_back(s);
        if (frame < 20 || frame % 15 == 0) {
            printf("  [f%2d] head=%+.3f  chest=%+.3f  hips=%+.3f  (target=%.3f)\n",
                   frame, s.head_yaw, s.chest_yaw, s.hips_yaw, TARGET_YAW);
        }
    }

    // --- Assertions.
    // A. head_yaw  reaches ≥ 90% of TARGET within 18 game frames (~300 ms @ 60 Hz).
    // B. chest_yaw reaches ≥ 90% of TARGET within 48 game frames (~800 ms).
    // C. hips_yaw  reaches ≥ 90% of TARGET within 84 game frames (~1.4 s).
    // D. During the transient (first ~30 frames), head_yaw > chest_yaw
    //    > hips_yaw at every frame (the cascade ordering).
    const float REACH_FRAC = 0.9f;
    auto find_reach_frame = [&](float get(const Sample&)) -> int {
        float target = REACH_FRAC * TARGET_YAW;
        for (const auto& s : samples) {
            if (get(s) >= target) return s.frame;
        }
        return -1;
    };

    int head_reach  = find_reach_frame([](const Sample& s){ return s.head_yaw; });
    int chest_reach = find_reach_frame([](const Sample& s){ return s.chest_yaw; });
    int hips_reach  = find_reach_frame([](const Sample& s){ return s.hips_yaw; });

    printf("\n  Reach frames (to 90%% of 90° target):\n");
    printf("    head:  %d  (budget ≤ 18, ≈300 ms @60Hz)\n", head_reach);
    printf("    chest: %d  (budget ≤ 48, ≈800 ms)\n",        chest_reach);
    printf("    hips:  %d  (budget ≤ 84, ≈1.4 s)\n",          hips_reach);

    // Cascade ordering during the transient: count frames where
    // head ≥ chest ≥ hips with a 1 mrad tolerance.
    int  cascade_ok_frames = 0;
    int  cascade_violation_frames = 0;
    const float EPS = 0.001f;
    for (int i = 0; i < std::min(30, (int)samples.size()); ++i) {
        const auto& s = samples[i];
        bool ok = (s.head_yaw + EPS >= s.chest_yaw) &&
                  (s.chest_yaw + EPS >= s.hips_yaw);
        if (ok) cascade_ok_frames++;
        else    cascade_violation_frames++;
    }

    bool reach_head  = (head_reach  >= 0 && head_reach  <= 18);
    bool reach_chest = (chest_reach >= 0 && chest_reach <= 48);
    bool reach_hips  = (hips_reach  >= 0 && hips_reach  <= 84);
    bool cascade_ok  = (cascade_violation_frames == 0);

    printf("\n  Assertions:\n");
    printf("    %s: head reaches 90%% within 18 frames (got %d)\n",
           reach_head ? "PASS" : "FAIL", head_reach);
    printf("    %s: chest reaches 90%% within 48 frames (got %d)\n",
           reach_chest ? "PASS" : "FAIL", chest_reach);
    printf("    %s: hips reaches 90%% within 84 frames (got %d)\n",
           reach_hips ? "PASS" : "FAIL", hips_reach);
    printf("    %s: cascade (head ≥ chest ≥ hips) holds every transient frame (%d ok / %d viol)\n",
           cascade_ok ? "PASS" : "FAIL", cascade_ok_frames, cascade_violation_frames);

    bool ok = reach_head && reach_chest && reach_hips && cascade_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — rotation cascade not implemented]");
    return ok;
}
