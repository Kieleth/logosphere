// =============================================================================
// PARTICLE QUAT <-> EULER SYNC — rotational-DOF upgrade Stage 3 bridge
// =============================================================================
// is_quat_driven particles have rotation_q as the truth; the solver's
// integrate_angular_velocities is responsible for deriving rotation_x/y/z
// from rotation_q after omega integration so every downstream reader
// (rendering, FK cascade, shape snap) sees a coherent Euler triple.
//
// The Stage 2 3-axis gluon test proves convergence but asserts only on
// rotation_q. This isolates the Euler sync so a regression in
// to_euler_zyx, the derivation gate, or the integration ordering fails
// here loud and obvious.
//
// Scenes:
//   A. quat-driven particle with a seeded non-identity rotation_q and
//      zero omega. After one physics step, rotation_q is unchanged and
//      rotation_x/y/z match to_euler_zyx(rotation_q).
//   B. DYNAMIC particle, not quat-driven, same seeded rotation_q. Under
//      quaternion truth (the flip, owner ruling 2026-08-19, default ON,
//      LOGOSPHERE_QUAT_TRUTH=0 the kill switch) every DYNAMIC body
//      publishes its Euler triple from rotation_q: one orientation
//      whichever field a consumer reads. Same expectation as A.
//   C. KINEMATIC particle, same seed. The flip's publish excludes
//      KINEMATIC (G-38): its external writer owns both ledgers, so the
//      Euler triple stays at zero.
//   Rewritten to the ruled world in the night of 2026-09-04 (journal 14);
//   the pre-flip law 'B stays at zero' had been red since the flip.
//
// Run: ./build/logosphere-tests --test test_particle_quat_euler_sync --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/particle.h"
#include "../src/math/quat.h"
#include <cstdio>
#include <cmath>

static bool approx_eq(float a, float b, float tol, const char* label, float& out_err) {
    float err = std::abs(a - b);
    if (err > out_err) out_err = err;
    if (err > tol) {
        printf("    FAIL: %s expected %+.4f got %+.4f (err %.2e)\n", label, b, a, err);
        return false;
    }
    return true;
}

bool test_particle_quat_euler_sync() {
    printf("\n=== Particle Quat <-> Euler sync (Stage 3 bridge) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "quat/euler sync";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps = engine.get_particle_system();

    // Build a non-identity seed rotation: 0.3 rad about a mixed axis.
    logosphere::Quat seed_q = logosphere::Quat::from_axis_angle(
        0.3f, 0.5f, 0.7f, /*theta=*/0.4f);

    // Scene A: quat-driven particle.
    Particle a = {};
    a.shape = ParticleShape::BOX;
    a.x = 0.0f; a.y = 0.0f; a.z = 10.0f;  // high in the air, no collisions
    a.width = 0.1f; a.height = 0.1f; a.thickness = 0.1f;
    a.SetMaterial(Materials::Type::STONE);
    a.is_at_rest = false;
    int a_id = engine.add_particle(a);

    // Scene B: non-quat-driven particle, same seed in rotation_q but Euler
    // left at zeros (simulating a particle where Euler is the legacy truth).
    Particle b = {};
    b.shape = ParticleShape::BOX;
    b.x = 2.0f; b.y = 0.0f; b.z = 10.0f;
    b.width = 0.1f; b.height = 0.1f; b.thickness = 0.1f;
    b.SetMaterial(Materials::Type::STONE);
    b.is_at_rest = false;
    int b_id = engine.add_particle(b);

    // Scene C: KINEMATIC body, same seed; the publish must leave it alone.
    Particle c = {};
    c.shape = ParticleShape::BOX;
    c.x = 4.0f; c.y = 0.0f; c.z = 10.0f;
    c.width = 0.1f; c.height = 0.1f; c.thickness = 0.1f;
    c.SetMaterial(Materials::Type::STONE);
    c.solver_mode = ParticleSolverMode::KINEMATIC;
    int c_id = engine.add_particle(c);

    // Flip A into quat-driven mode + seed rotation_q. Do the same seed on
    // B (but leave is_quat_driven=false) to prove the solver doesn't touch
    // Euler on non-opted-in particles.
    {
        auto view = ps.lock_particles_for_write();
        view[a_id].rotation_q = seed_q;
        view[a_id].is_quat_driven = true;
        view[b_id].rotation_q = seed_q;
        // view[b_id].is_quat_driven stays false
        view[c_id].rotation_q = seed_q;
    }

    const float dt = 1.0f / 60.0f;
    // Physics runs at a fixed internal timestep; one dt may not flush a
    // full step. Run a few frames to be sure the solver pass fires.
    for (int i = 0; i < 5; ++i) engine.update(dt);

    // Expected Euler for scene A (seed_q derivation)
    float ex, ey, ez;
    seed_q.to_euler_zyx(ex, ey, ez);

    bool ok = true;
    float max_err = 0.0f;

    {
        auto v = ps.lock_particles_for_read();
        const Particle& pa = v[a_id];
        const Particle& pb = v[b_id];
        const Particle& pc = v[c_id];

        printf("  A: is_quat_driven=%s solver_mode=%s at_rest=%s\n",
               pa.is_quat_driven ? "true" : "false",
               pa.solver_mode == ParticleSolverMode::DYNAMIC ? "DYNAMIC" :
               (pa.solver_mode == ParticleSolverMode::KINEMATIC ? "KINEMATIC" : "DYNAMIC"),
               pa.is_at_rest ? "true" : "false");
        printf("  A (is_quat_driven): rot_q=(%+.3f,%+.3f,%+.3f,%+.3f) euler=(%+.4f,%+.4f,%+.4f)\n",
               pa.rotation_q.w, pa.rotation_q.x, pa.rotation_q.y, pa.rotation_q.z,
               pa.rotation_x, pa.rotation_y, pa.rotation_z);
        printf("  Expected euler = (%+.4f,%+.4f,%+.4f)\n", ex, ey, ez);

        // A should have rotation_q preserved (no omega → no integration)
        // AND Euler derived from it.
        ok &= approx_eq(pa.rotation_x, ex, 1e-4f, "A.rotation_x", max_err);
        ok &= approx_eq(pa.rotation_y, ey, 1e-4f, "A.rotation_y", max_err);
        ok &= approx_eq(pa.rotation_z, ez, 1e-4f, "A.rotation_z", max_err);

        printf("  B (DYNAMIC):        rot_q=(%+.3f,%+.3f,%+.3f,%+.3f) euler=(%+.4f,%+.4f,%+.4f)\n",
               pb.rotation_q.w, pb.rotation_q.x, pb.rotation_q.y, pb.rotation_q.z,
               pb.rotation_x, pb.rotation_y, pb.rotation_z);

        // B is DYNAMIC: under quaternion truth its Euler triple is published
        // from rotation_q like A's (one orientation, whichever field).
        ok &= approx_eq(pb.rotation_x, ex, 1e-4f, "B.rotation_x (DYNAMIC publishes)", max_err);
        ok &= approx_eq(pb.rotation_y, ey, 1e-4f, "B.rotation_y (DYNAMIC publishes)", max_err);
        ok &= approx_eq(pb.rotation_z, ez, 1e-4f, "B.rotation_z (DYNAMIC publishes)", max_err);

        printf("  C (KINEMATIC):      rot_q=(%+.3f,%+.3f,%+.3f,%+.3f) euler=(%+.4f,%+.4f,%+.4f)\n",
               pc.rotation_q.w, pc.rotation_q.x, pc.rotation_q.y, pc.rotation_q.z,
               pc.rotation_x, pc.rotation_y, pc.rotation_z);
        // C is KINEMATIC: the publish excludes it (G-38), its writer owns both.
        ok &= approx_eq(pc.rotation_x, 0.0f, 1e-4f, "C.rotation_x (KINEMATIC, untouched)", max_err);
        ok &= approx_eq(pc.rotation_y, 0.0f, 1e-4f, "C.rotation_y (KINEMATIC, untouched)", max_err);
        ok &= approx_eq(pc.rotation_z, 0.0f, 1e-4f, "C.rotation_z (KINEMATIC, untouched)", max_err);
    }

    printf("\n  Max abs err: %.2e\n", max_err);
    printf("  %s\n", ok ? "[PASS]" : "[FAIL]");
    return ok;
}
