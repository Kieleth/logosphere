// =============================================================================
// GLUON 3-AXIS ANGULAR DRIVE CONVERGENCE — rotational-DOF upgrade Stage 2
// =============================================================================
// Mirror of test_gluon_angular_drive_converges, but on the quaternion path:
// the gluon is commanded with a non-Z target rotation (pi/4 around the Y
// axis) and the solver must drive the child particle's rotation_q to match.
//
// This exercises the new machinery in one pass:
//   - gluon->use_quat_target + target_relative_q field
//   - 3 per-axis angular constraint rows built from the Rodrigues error
//   - angular_axis_idx dispatch in the impulse-application loop
//   - omega_x/y integration in integrate_angular_velocities
//   - rotation_q integration via axis-angle exponential map
//
// Pass criteria:
//   A. After 1 s settle, |q_err| angle within 5 % of pi/4.
//   B. Hold-phase (1-2 s) max angle error within 10 % of pi/4.
//
// Run: ./build/logosphere-tests --test test_gluon_3axis_drive_converges --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/particle.h"
#include "../src/math/quat.h"
#include <cstdio>
#include <cmath>

bool test_gluon_3axis_drive_converges() {
    printf("\n=== Gluon 3-axis Angular Drive Convergence ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "3-axis drive";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Two free-floating particles, stacked on Z.
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

    // Flip both particles into quat-driven mode so the solver reads
    // rotation_q as the authoritative orientation source (Stage 3
    // added an is_quat_driven gate that defaults to false).
    {
        auto view = ps.lock_particles_for_write();
        view[a_id].is_quat_driven = true;
        view[b_id].is_quat_driven = true;
    }

    // Target: rotate child pi/4 around the Y axis relative to parent.
    const float TARGET_THETA = static_cast<float>(M_PI) / 4.0f;
    auto target_q = logosphere::Quat::from_axis_angle(0.0f, 1.0f, 0.0f, TARGET_THETA);

    auto gluon = std::make_unique<NailGluon>();
    gluon->offset_a = Vec3(0.0f, 0.0f, +0.05f);
    gluon->offset_b = Vec3(0.0f, 0.0f, -0.05f);
    gluon->target_distance = 0.2f;
    gluon->breaking_force = 100000.0f;
    gluon->angular_stiffness = 200.0f;
    gluon->angular_damping   = 12.0f;
    gluon->max_relative_rotation = 3.14f;
    gluon->angular_drive_enabled = true;
    gluon->use_quat_target = true;
    gluon->target_relative_q = target_q;

    physics.add_gluon_between(a_id, b_id, std::move(gluon));

    const float dt = 1.0f / 60.0f;
    constexpr int FRAMES = 120;
    constexpr int HOLD_START = 60;

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float final_err      = 0.0f;

    auto err_angle = [&](const logosphere::Quat& qa, const logosphere::Quat& qb) {
        logosphere::Quat q_err = qb * qa.conjugate() * target_q.conjugate();
        float ax, ay, az, theta;
        q_err.to_axis_angle(ax, ay, az, theta);
        if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
        return std::abs(theta);
    };

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& pa = v[a_id];
        const Particle& pb = v[b_id];
        float err = err_angle(pa.rotation_q, pb.rotation_q);
        final_err = err;

        if (f < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (err > max_err_hold) max_err_hold = err;
        }

        if (f % 10 == 0) {
            printf("  [f%3d] pa.q=(%+.3f,%+.3f,%+.3f,%+.3f) pb.q=(%+.3f,%+.3f,%+.3f,%+.3f) err=%+.4f rad\n",
                   f,
                   pa.rotation_q.w, pa.rotation_q.x, pa.rotation_q.y, pa.rotation_q.z,
                   pb.rotation_q.w, pb.rotation_q.x, pb.rotation_q.y, pb.rotation_q.z,
                   err);
        }
    }

    const float FINAL_BUDGET = 0.05f * TARGET_THETA;
    const float HOLD_BUDGET  = 0.10f * TARGET_THETA;

    printf("\n  Target:         pi/4 about Y (%.4f rad)\n", TARGET_THETA);
    printf("  Final err:      %.4f (budget %.4f)\n", final_err, FINAL_BUDGET);
    printf("  Max settle err: %.4f\n", max_err_settle);
    printf("  Max hold err:   %.4f (budget %.4f)\n", max_err_hold, HOLD_BUDGET);

    bool final_ok = (final_err    <= FINAL_BUDGET);
    bool hold_ok  = (max_err_hold <= HOLD_BUDGET);

    printf("\n  Assertions:\n");
    printf("    %s INV-13: final |q_err| within 5%% of target angle\n",  final_ok ? "PASS" : "FAIL");
    printf("    %s INV-13: hold-phase error within 10%% of target (holds, no oscillation)\n",    hold_ok  ? "PASS" : "FAIL");

    bool ok = final_ok && hold_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL INV-13 - 3-axis PD drive did not converge]");
    return ok;
}
