// =============================================================================
// LEG SHOOT-OUT DURING ROTATION — no per-frame leg teleports on yaw change
// =============================================================================
// Regression wedge for a visual bug: during idle rotation toward a yaw
// target (the Stage 1/2 rotation cascade + twist-step), a leg appears to
// "shoot out" — one of the leg particles jumps a large distance in a
// single frame instead of moving smoothly. The cascade rotates the hips
// gradually; feet should swing in an arc under hips (FK) or teleport
// precisely to a new plant_target once per twist-step. Either way, the
// leg should NOT have single-frame deltas larger than a natural swing
// step.
//
// Mechanism: this test instruments every leg particle (both feet, shins,
// thighs, toes) each frame. It records world position and computes the
// per-frame delta. Any delta exceeding LEG_JUMP_THRESHOLD (0.25 m) is
// flagged as a shoot-out. The threshold is well above FK arc-drag
// (≈0.15 m for 90°) and above a clean twist-step plant (≈0.30 m for a
// half-stride). A shoot-out is "a particle moved way past its plant,
// way past its FK target, or chain integrity broke".
//
// Expected today: RED. Stage 2 lets the foot snap to the new plant at
// twist-step with plant_blend=1, but there is no instrumentation on
// what happens to the shin/thigh/toe during that snap, or to the leg
// that was just released from stance.
//
// Run: ./build/logosphere-tests --test test_leg_shoot_out_during_rotation --no-head
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
#include <array>
#include <algorithm>

bool test_leg_shoot_out_during_rotation() {
    printf("\n=== Leg Shoot-Out During Rotation ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "leg shoot-out";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

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

    // Track every leg particle. Generator order: [foot, shin, thigh, toe].
    struct Leg {
        const char* name;
        int foot, shin, thigh, toe;
    };
    Leg L = {"L", eva.left_leg_ids.at(0),  eva.left_leg_ids.at(1),
                  eva.left_leg_ids.at(2),  eva.left_leg_ids.at(3)};
    Leg R = {"R", eva.right_leg_ids.at(0), eva.right_leg_ids.at(1),
                  eva.right_leg_ids.at(2), eva.right_leg_ids.at(3)};
    int hips_id = eva.hips_id;

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(L.foot); fix(L.shin); fix(L.thigh); fix(L.toe);
        fix(R.foot); fix(R.shin); fix(R.thigh); fix(R.toe);
        fix(hips_id);
    });

    // Wire the permanent ParticleTracer so we get the causal site for any
    // jump we catch. Labels mirror the Eden convention.
    auto& tracer = engine.get_particle_tracer();
    tracer.trace(L.foot,  "eva/l_foot");
    tracer.trace(L.shin,  "eva/l_shin");
    tracer.trace(L.thigh, "eva/l_thigh");
    tracer.trace(L.toe,   "eva/l_toe");
    tracer.trace(R.foot,  "eva/r_foot");
    tracer.trace(R.shin,  "eva/r_shin");
    tracer.trace(R.thigh, "eva/r_thigh");
    tracer.trace(R.toe,   "eva/r_toe");

    ps.add_swap_callback([&tracer](size_t old_idx, size_t new_idx) {
        if (tracer.is_traced((int)old_idx)) {
            auto label = tracer.label_of((int)old_idx);
            tracer.untrace((int)old_idx);
            tracer.trace((int)new_idx, std::move(label));
        }
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // 90° CW yaw target.
    engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id, 10.0f, 0.0f);

    constexpr int FRAMES = 180;
    const float JUMP_THRESHOLD = 0.25f;  // single-frame XYZ delta

    struct Prev { float x, y, z; bool valid; };
    std::array<std::pair<const char*, int>, 8> watched = {{
        {"L_foot",  L.foot}, {"L_shin",  L.shin},
        {"L_thigh", L.thigh}, {"L_toe",  L.toe},
        {"R_foot",  R.foot}, {"R_shin",  R.shin},
        {"R_thigh", R.thigh}, {"R_toe",  R.toe},
    }};
    std::array<Prev, 8> prev{};
    for (auto& p : prev) p.valid = false;

    struct Jump {
        int   frame;
        const char* name;
        float delta;
        float from_x, from_y, from_z;
        float to_x, to_y, to_z;
    };
    std::vector<Jump> jumps;

    float max_delta = 0.0f;
    const char* max_delta_name = "";
    int max_delta_frame = -1;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        for (size_t i = 0; i < watched.size(); ++i) {
            const Particle& p = v[watched[i].second];
            if (prev[i].valid) {
                float dx = p.x - prev[i].x;
                float dy = p.y - prev[i].y;
                float dz = p.z - prev[i].z;
                float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (d > max_delta) {
                    max_delta = d;
                    max_delta_name = watched[i].first;
                    max_delta_frame = frame;
                }
                if (d > JUMP_THRESHOLD) {
                    jumps.push_back({frame, watched[i].first, d,
                                     prev[i].x, prev[i].y, prev[i].z,
                                     p.x, p.y, p.z});
                }
            }
            prev[i] = {p.x, p.y, p.z, true};
        }
    }

    printf("  Max single-frame delta: %.3f m on %s at frame %d\n",
           max_delta, max_delta_name, max_delta_frame);
    printf("  Jumps (> %.2f m): %zu\n", JUMP_THRESHOLD, jumps.size());
    int print_n = std::min<int>(jumps.size(), 8);
    for (int i = 0; i < print_n; ++i) {
        const auto& j = jumps[i];
        printf("    [f%3d] %s  Δ=%.3f m  (%.3f,%.3f,%.3f) → (%.3f,%.3f,%.3f)\n",
               j.frame, j.name, j.delta,
               j.from_x, j.from_y, j.from_z,
               j.to_x,   j.to_y,   j.to_z);
    }

    // On failure, dump the tracer for the recent window so the causal
    // site (FK / IK / chain.project / kinematic_root.transfer) is visible.
    if (!jumps.empty()) {
        int dump_from = std::max(0, jumps.front().frame - 2);
        int dump_to   = std::min(FRAMES - 1, jumps.front().frame + 2);
        (void)dump_from; (void)dump_to;
        printf("\n  [TRACER DUMP — last 20 frames, filtered to leg writes]\n");
        tracer.dump(std::cout, /*last_n_frames=*/20);
    }

    bool ok = jumps.empty();
    printf("\n  %s (max %.3f m, %zu jumps)\n",
           ok ? "[PASS]" : "[FAIL — a leg particle shot out]",
           max_delta, jumps.size());
    return ok;
}
