// =============================================================================
// ROTATION DURING WALK — path curves when target yaw shifts mid-stride
// =============================================================================
// Stage 3 of the biomechanical rotation TDD. Stage 1 set up the head-
// leads-torso-leads-hips cascade. Stage 2 made the feet replant when
// the hips out-twist the committed foot yaw. Stage 3 closes the loop:
// when walking, the motion direction must track the evolving hips yaw
// through the stride, not just at heel-strike. A human walking while
// turning traces a curved path — the body yaws continuously, each foot
// plants slightly rotated from the last, and the result is an arc.
//
// This test codifies that invariant. Eva walks forward (body-relative
// +Y) while a look-at target is placed to her right. The cascade rotates
// hips toward the target; plant_target samples motion direction; motion
// direction is recomputed from hips_yaw_world (base_rotation). If
// everything is wired, the path curves. If motion direction is frozen at
// the initial facing, she walks straight into oblivion.
//
// Expected today (Stage 1 + 2 shipped, Stage 3 not explicit): DEPENDS.
// The existing body-relative path already reads base_rotation, which the
// cascade updates. If it Just Works the test passes immediately; if not,
// the failure will tell us which wire is loose.
//
// Run: ./build/logosphere-tests --test test_rotation_during_walk --no-head
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

bool test_rotation_during_walk() {
    printf("\n=== Rotation During Walk — path must curve ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "rotation during walk";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    Particle floor = {};
    floor.shape = ParticleShape::BOX;
    floor.x = 0; floor.y = 0; floor.z = 0.0f;
    floor.width = 50; floor.height = 50; floor.thickness = 0.1f;
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

    int hips_id = eva.hips_id;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        if (hips_id == (int)old_idx) hips_id = (int)new_idx;
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Baseline.
    float hx0, hy0;
    {
        auto v = ps.lock_particles_for_read();
        hx0 = v[hips_id].x;
        hy0 = v[hips_id].y;
    }
    printf("  Baseline hips: (%.3f, %.3f)\n", hx0, hy0);

    // Start walking forward (body-relative +Y at 1 m/s intent).
    // Simultaneously set a look-at target 90° to her right (+X).
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, 1.0f, 0.0f);
    engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id, 10.0f, 0.0f);

    // Run 240 game frames ≈ 4 s at 60 Hz. Sample position every frame.
    constexpr int FRAMES = 240;
    struct P { float x, y, yaw; };
    std::vector<P> path;
    path.reserve(FRAMES);

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        path.push_back({v[hips_id].x, v[hips_id].y, v[hips_id].rotation_z});
        if (frame % 20 == 0 || frame < 5) {
            printf("  [f%3d] hips=(%+.3f,%+.3f) yaw=%+.3f\n",
                   frame, v[hips_id].x, v[hips_id].y, v[hips_id].rotation_z);
        }
    }

    // --- Metrics.
    // Final position.
    P end = path.back();
    float dx = end.x - hx0;
    float dy = end.y - hy0;
    float end_dist = std::sqrt(dx*dx + dy*dy);

    // If Eva walked a straight line along initial facing (+Y), she would
    // end at (0, FRAMES*PHYS_DT*speed) roughly. Call that the "no-turn"
    // projection. The perpendicular deviation from that straight line is
    // the curvature signal.
    // Straight-line projection: onto initial facing (+Y).
    float along_initial = dy;          // initial facing = +Y
    float perp_initial  = dx;           // perpendicular = +X (turn target direction)

    // Maximum X deviation across the whole path (how far she bent).
    float max_x = 0.0f;
    for (const auto& p : path) {
        float x = p.x - hx0;
        if (x > max_x) max_x = x;
    }

    printf("\n  End hips delta: (%+.3f, %+.3f)  dist=%.3f m\n", dx, dy, end_dist);
    printf("  Along initial facing (+Y): %.3f m\n", along_initial);
    printf("  Perpendicular (+X, turn direction): %.3f m\n", perp_initial);
    printf("  Max X deviation during path: %.3f m\n", max_x);
    printf("  Final hips yaw: %+.3f rad (target ~+%.3f)\n",
           end.yaw, static_cast<float>(M_PI) / 2.0f);

    // --- Assertions.
    // A. She actually moved: end_dist > 1 m (4 s * 1 m/s = 4 m ideal;
    //    curved path is shorter, but >1 m rules out "stuck").
    // B. Final yaw is within 20° of the 90° target — she rotated.
    // C. Path curves INTO the turn: final X displacement > 0.5 m (not
    //    a straight line, and in the correct direction).
    // D. Path stayed intact: no sudden jumps; test by max per-frame step.
    bool moved = (end_dist > 1.0f);
    float yaw_err = std::abs(end.yaw - static_cast<float>(M_PI) / 2.0f);
    bool yaw_ok = (yaw_err < 0.35f);   // ~20°
    bool curved = (perp_initial > 0.5f);

    // Per-frame step sanity.
    float max_step = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        float sdx = path[i].x - path[i-1].x;
        float sdy = path[i].y - path[i-1].y;
        float s = std::sqrt(sdx*sdx + sdy*sdy);
        if (s > max_step) max_step = s;
    }
    bool smooth = (max_step < 0.5f);  // 1 m/s * dt = ~0.017 m; 0.5 m/frame would be a teleport

    printf("\n  Assertions:\n");
    printf("    %s: Eva moved meaningfully (dist=%.3f m > 1.0)\n",
           moved ? "PASS" : "FAIL", end_dist);
    printf("    %s: yaw rotated toward target (err %.3f rad < 0.35)\n",
           yaw_ok ? "PASS" : "FAIL", yaw_err);
    printf("    %s: path curved into the turn (Δx=%.3f m > 0.5)\n",
           curved ? "PASS" : "FAIL", perp_initial);
    printf("    %s: no teleport jumps (max step %.3f m < 0.5)\n",
           smooth ? "PASS" : "FAIL", max_step);

    bool ok = moved && yaw_ok && curved && smooth;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — rotation during walk incomplete]");
    return ok;
}
