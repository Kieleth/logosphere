// =============================================================================
// GLUON DISTANCE HOLDS FROM OFFSET — TDD for the rehab
// =============================================================================
// Exposes the bug that blocks Option C Phase 5: the gluon distance
// constraint in the physics solver does NOT pull two particles toward
// their target_distance today. Both enforcement flags in
// include/logosphere/physics/physics_solver.h are false:
//
//   ENABLE_GLUON_POSITION_PROJECTION    = false
//   ENABLE_GLUON_JACOBIAN_POSITION_BIAS = false
//
// Result: `c.bias = 0.0f` in the Jacobian build (physics_system_v4.cpp:1171),
// and the hard projection call site at physics_system_v4.cpp:263 is
// guarded `if constexpr (false) { ... }`. The gluon matches velocities
// but does NOT close a position gap. This has been hidden because FK +
// snap_to_hips write particle positions directly at the correct
// attachment-point distance every frame — the gluon never has to do
// position work in today's setups.
//
// This test puts the gluon in charge:
//   - KINEMATIC anchor at (0, 0, 1), never moves.
//   - DYNAMIC subject at (0.3, 0, 1), is_quat_driven so gravity is
//     skipped (matches the Phase 4b foot / leg particle condition).
//   - NailGluon foot→anchor, target_distance=0, no angular coupling.
//   - No forces other than the gluon; the subject should converge to
//     the anchor as the solver tightens the distance constraint.
//
// Assertion: the subject ends within 3 cm of the anchor after 180
// frames (3 s at 60 Hz).
//
// STATUS 2026-08-01, and the description ABOVE IS STALE. Both halves of it
// were true when written and neither is now:
//
//   ENABLE_GLUON_JACOBIAN_POSITION_BIAS is TRUE (V4.10 unified solver), not
//   false. The position bias exists and works.
//
//   The subject does NOT sit at 0.3 m forever. It converges 0.30 -> 0.0813 m
//   and stalls there, against a 0.03 m budget. So the constraint IS doing
//   position work; it simply stops short.
//
// This is therefore still RED, but for the remaining half of the original
// plan: "and tune BETA". Solver beta is 0.4 with 0.001 m slop, and 0.0813 m is
// far above slop, so the stall is convergence rate rather than a floor.
//
// DELIBERATELY NOT FIXED HERE. Tuning beta changes what the solver computes for
// EVERY body in the engine, and apply_all_forces is about to be refactored
// under a characterization checksum whose entire value is that behaviour did
// not change (tests/test_physics_characterization.cpp). Doing a behaviour
// change and a refactor at once destroys the ability to attribute either.
// Order: refactor with the net green, then tune beta and re-pin deliberately.
//
// Not gating: this test is in neither the physics guard suite nor CI.
//
// Run: ./build/logosphere-tests --test test_gluon_distance_holds_offset --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>

bool test_gluon_distance_holds_offset() {
    printf("\n=== Gluon distance holds from offset (rehab TDD) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "gluon distance offset";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // KINEMATIC anchor at the origin point we want to pull toward.
    Particle anchor = {};
    anchor.shape = ParticleShape::BOX;
    anchor.x = 0.0f; anchor.y = 0.0f; anchor.z = 1.0f;
    anchor.width = 0.01f; anchor.height = 0.01f; anchor.thickness = 0.01f;
    anchor.is_light_source = true;   // skips gravity + material mass
    anchor.a = 0.0f;
    int anchor_id = engine.add_particle(anchor);

    // DYNAMIC subject, offset by 0.3 m on X.
    Particle subject = {};
    subject.shape = ParticleShape::SPHERE;
    subject.x = 0.3f; subject.y = 0.0f; subject.z = 1.0f;
    subject.width = 0.1f; subject.height = 0.1f; subject.thickness = 0.1f;
    subject.r = 0.2f; subject.g = 0.8f; subject.b = 0.2f; subject.a = 1.0f;
    subject.SetMaterial(Materials::Type::STONE);
    subject.is_at_rest = false;
    int subject_id = engine.add_particle(subject);

    {
        auto view = ps.lock_particles_for_write();
        view[anchor_id].solver_mode = ParticleSolverMode::KINEMATIC;
        view[anchor_id].is_at_rest = false;
        view[subject_id].is_quat_driven = true;  // skips gravity
        view[subject_id].is_at_rest = false;
    }

    // Pin gluon with target_distance=0. No angular coupling.
    auto g = std::make_unique<NailGluon>();
    g->offset_a = Vec3{0.0f, 0.0f, 0.0f};
    g->offset_b = Vec3{0.0f, 0.0f, 0.0f};
    g->target_distance = 0.0f;
    g->stiffness = 50000.0f;
    g->damping = 500.0f;
    g->breaking_force = 1e9f;
    g->enable_angular_constraint = false;
    g->angular_stiffness = 0.0f;
    g->angular_damping = 0.0f;
    g->rotate_offsets = false;
    physics.add_gluon_between(
        static_cast<size_t>(subject_id),
        static_cast<size_t>(anchor_id),
        std::move(g));

    const GluonConstraintBase* pin =
        physics.get_gluon(static_cast<size_t>(subject_id),
                          static_cast<size_t>(anchor_id));
    bool created = (pin != nullptr);
    printf("  %s: pin gluon created\n", created ? "PASS" : "FAIL");

    float initial_gap;
    {
        auto v = ps.lock_particles_for_read();
        float dx = v[subject_id].x - v[anchor_id].x;
        float dy = v[subject_id].y - v[anchor_id].y;
        float dz = v[subject_id].z - v[anchor_id].z;
        initial_gap = std::sqrt(dx*dx + dy*dy + dz*dz);
    }
    printf("  initial gap = %.4f m\n", initial_gap);

    // Run 3 s of physics and watch the gap close (or not).
    const float dt = 1.0f / 60.0f;
    constexpr int FRAMES = 180;

    float final_gap = initial_gap;
    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);

        float sx, sy, sz, ax, ay, az;
        {
            auto v = ps.lock_particles_for_read();
            ax = v[anchor_id].x; ay = v[anchor_id].y; az = v[anchor_id].z;
            sx = v[subject_id].x; sy = v[subject_id].y; sz = v[subject_id].z;
        }
        float dx = sx - ax, dy = sy - ay, dz = sz - az;
        float d = std::sqrt(dx*dx + dy*dy + dz*dz);
        final_gap = d;

        if (f % 30 == 0) {
            printf("  [f%3d] anchor=(%.3f,%.3f,%.3f) subject=(%.3f,%.3f,%.3f) gap=%.4f\n",
                   f, ax, ay, az, sx, sy, sz, d);
        }
    }

    const float BUDGET = 0.03f;  // 3 cm
    bool converged = (final_gap <= BUDGET);
    printf("\n  final gap = %.4f m (budget %.2f m)\n", final_gap, BUDGET);
    printf("  %s: subject converges from 0.30 m start to within %.0f cm of anchor\n",
           converged ? "PASS" : "FAIL", BUDGET * 100.0f);
    printf("\n  %s\n",
           (created && converged) ? "[PASS]"
                                   : "[FAIL — gluon distance constraint not enforcing]");
    return created && converged;
}
