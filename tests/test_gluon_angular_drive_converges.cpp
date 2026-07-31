// =============================================================================
// GLUON ANGULAR DRIVE CONVERGENCE — Phase 1 of physics-driven skeleton
// =============================================================================
// Phase 1 of the Option C refactor: prove that a gluon can act as a PD
// controller driving two particles to a commanded relative rotation. The
// new `angular_drive_enabled` flag plus the existing angular_stiffness /
// angular_damping fields should, under physics simulation, pull particle
// B's rotation_z to (pa.rotation_z + target_relative_rotation) within a
// reasonable settling time.
//
// Scene: two particles floating in space (no gravity effect — both at-
// rest, no floor under them). One gluon with drive enabled, target
// rotation pi/4. Start with B matching A. After 0.5 s of simulation, B
// should be within ~5 % of target — and hold there without oscillation
// for another 0.5 s.
//
// Run: ./build/logosphere-tests --test test_gluon_angular_drive_converges --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/gluon_particle.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>

bool test_gluon_angular_drive_converges() {
    printf("\n=== Gluon Angular Drive Convergence ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "angular drive";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Two particles floating 5 m above ground, 0.3 m apart on Z so a
    // NailGluon can connect them with a clear pivot. Both particles are
    // ACTIVE (is_at_rest=false) so the angular solver actually integrates
    // their rotations. They fall under gravity together, but gravity pulls
    // both equally and doesn't torque them relative to each other — the
    // measurement is purely relative rotation.
    Particle a = {};
    a.shape = ParticleShape::BOX;
    a.x = 0.0f; a.y = 0.0f; a.z = 5.0f;
    a.width = 0.1f; a.height = 0.1f; a.thickness = 0.1f;
    a.r = 0.7f; a.g = 0.4f; a.b = 0.4f; a.a = 1.0f;
    a.SetMaterial(Materials::Type::STONE);
    a.is_at_rest = false;
    int a_id = engine.add_particle(a);

    Particle b = {};
    b.shape = ParticleShape::BOX;
    b.x = 0.0f; b.y = 0.0f; b.z = 5.3f;
    b.width = 0.1f; b.height = 0.1f; b.thickness = 0.1f;
    b.r = 0.4f; b.g = 0.7f; b.b = 0.4f; b.a = 1.0f;
    b.SetMaterial(Materials::Type::STONE);
    b.is_at_rest = false;
    int b_id = engine.add_particle(b);

    // Gluon between them with the PD drive enabled. Target 45°.
    auto gluon = std::make_unique<NailGluon>();
    gluon->offset_a = Vec3(0.0f, 0.0f, +0.05f);
    gluon->offset_b = Vec3(0.0f, 0.0f, -0.05f);
    gluon->target_distance = 0.2f;
    gluon->breaking_force = 100000.0f;
    gluon->angular_stiffness = 200.0f;   // N·m/rad — firm for small particle inertia
    gluon->angular_damping   = 12.0f;    // critical-ish damping at this stiffness
    gluon->max_relative_rotation = 3.14f;  // effectively unlimited
    gluon->target_relative_rotation = static_cast<float>(M_PI) / 4.0f;  // 45°
    gluon->angular_drive_enabled = true;

    physics.add_gluon_between(a_id, b_id, std::move(gluon));

    const float dt = 1.0f / 60.0f;

    // Run 1.0 s total — 0.5 s settle, then 0.5 s hold.
    constexpr int FRAMES = 60;
    constexpr int HOLD_START = 30;

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float final_rel_rot  = 0.0f;

    for (int frame = 0; frame < FRAMES; ++frame) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& pa = v[a_id];
        const Particle& pb = v[b_id];
        float rel = pb.rotation_z - pa.rotation_z;
        float err = std::abs(rel - static_cast<float>(M_PI) / 4.0f);
        final_rel_rot = rel;

        if (frame < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (err > max_err_hold) max_err_hold = err;
        }
        if (frame % 5 == 0) {
            printf("  [f%2d] a_z=%.3f b_z=%.3f a_rot=%+.4f b_rot=%+.4f rel=%+.4f err=%+.4f\n",
                   frame, pa.z, pb.z, pa.rotation_z, pb.rotation_z,
                   rel, err);
        }
    }

    // Assertions.
    // A. Final relative rotation within 5 % of target. 5 % of pi/4 ≈ 0.039 rad.
    // B. Hold-phase max error ≤ 10 % of target (no runaway or growing
    //    oscillation once settled).
    const float TARGET = static_cast<float>(M_PI) / 4.0f;
    const float FINAL_BUDGET  = 0.05f * TARGET;  // 5 % final
    const float HOLD_BUDGET   = 0.10f * TARGET;  // 10 % during hold

    float final_err = std::abs(final_rel_rot - TARGET);

    printf("\n  Target:      %.4f rad\n", TARGET);
    printf("  Final rel:   %.4f rad (err %.4f)\n", final_rel_rot, final_err);
    printf("  Max settle err: %.4f\n", max_err_settle);
    printf("  Max hold err:   %.4f (budget %.4f)\n", max_err_hold, HOLD_BUDGET);

    bool final_ok  = (final_err    <= FINAL_BUDGET);
    bool hold_ok   = (max_err_hold <= HOLD_BUDGET);

    printf("\n  Assertions:\n");
    printf("    %s: final rel rotation within 5%% of target\n",
           final_ok ? "PASS" : "FAIL");
    printf("    %s: hold-phase max err within 10%% of target (no oscillation)\n",
           hold_ok ? "PASS" : "FAIL");

    bool ok = final_ok && hold_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL - gluon PD drive did not converge]");
    return ok;
}
