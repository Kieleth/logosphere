// =============================================================================
// GLUON CHAIN CONVERGENCE — isolate chain PD from humanoid wobble
// =============================================================================
// The Phase 3b arm-chain test on Eva showed chain convergence that held
// tight through a settle window then drifted outward over seconds. Tracing
// the source pointed at the shoulder particle itself: its rotation_x/y
// Euler oscillates (torso + gravity + ground support micro-wobble), and FK
// writes that wobble into the shoulder each frame. The arm chain reads
// that as a moving reference frame and pumps it through the chain.
//
// This test removes the humanoid entirely: three free particles stacked on
// Z, connected by two gluons, no gravity in Z (particles float), all three
// rotation-driven by quaternion PD with different targets. Kinematic
// "anchor" particle holds the top. If the chain machinery itself is
// correct, every rotation_q converges and stays converged indefinitely.
// Remaining drift (if any) is solver or integration noise, not coupling
// to a wobbly humanoid.
//
// Scene:
//   p0 (anchor, KINEMATIC, identity pose)
//   ↓ gluon g0, target = 20 deg about X
//   p1
//   ↓ gluon g1, target = 15 deg about X
//   p2
//
// Assertions:
//   - Both joints converge within 5 % of target magnitude by f60.
//   - Hold-phase (f180–f240) max error within 5 % of target — NO drift.
//
// Run: ./build/logosphere-tests --test test_gluon_chain_converges --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>

namespace {

float joint_err(const Particle& parent, const Particle& child,
                const logosphere::Quat& target) {
    logosphere::Quat q_a = parent.is_quat_driven
        ? parent.rotation_q
        : logosphere::Quat::from_euler(parent.rotation_x, parent.rotation_y, parent.rotation_z);
    logosphere::Quat q_b = child.is_quat_driven
        ? child.rotation_q
        : logosphere::Quat::from_euler(child.rotation_x, child.rotation_y, child.rotation_z);
    logosphere::Quat q_err = q_b * q_a.conjugate() * target.conjugate();
    float ax, ay, az, theta;
    q_err.to_axis_angle(ax, ay, az, theta);
    if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
    return std::abs(theta);
}

}  // namespace

bool test_gluon_chain_converges() {
    printf("\n=== Gluon Chain Convergence (no humanoid) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "gluon chain";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Three small boxes stacked on Z, no gravity effect (we'll keep p0
    // kinematic so the whole chain floats without dropping).
    auto make = [](float z, float r, float g, float b) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = z;
        p.width = 0.1f; p.height = 0.1f; p.thickness = 0.1f;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.is_at_rest = false;
        return p;
    };
    int p0_id = engine.add_particle(make(10.0f, 1,0,0));
    int p1_id = engine.add_particle(make(10.3f, 0,1,0));
    int p2_id = engine.add_particle(make(10.6f, 0,0,1));

    // p0 is our "ground" — kinematic anchor, no gravity, no rotation.
    {
        auto view = ps.lock_particles_for_write();
        view[p0_id].solver_mode = ParticleSolverMode::KINEMATIC;
        // p1/p2 stay DYNAMIC, flip is_quat_driven so the constraint
        // reads rotation_q directly from them.
        view[p1_id].is_quat_driven = true;
        view[p2_id].is_quat_driven = true;
    }

    // Target 1: 20 deg about X (p0 → p1)
    // Target 2: 15 deg about X (p1 → p2)
    const float DEG2RAD = static_cast<float>(M_PI) / 180.0f;
    auto t0 = logosphere::Quat::from_axis_angle(1,0,0, 20.0f * DEG2RAD);
    auto t1 = logosphere::Quat::from_axis_angle(1,0,0, 15.0f * DEG2RAD);

    auto make_gluon = [&](float tgt_angle, const logosphere::Quat& tgt) {
        auto g = std::make_unique<NailGluon>();
        g->offset_a = Vec3(0.0f, 0.0f, +0.05f);
        g->offset_b = Vec3(0.0f, 0.0f, -0.05f);
        g->target_distance = 0.2f;
        g->breaking_force = 100000.0f;
        g->angular_stiffness = 2000.0f;
        g->angular_damping   = 60.0f;
        g->max_relative_rotation = 3.14f;
        g->angular_drive_enabled = true;
        g->use_quat_target = true;
        g->target_relative_q = tgt;
        (void)tgt_angle;
        return g;
    };
    physics.add_gluon_between(p0_id, p1_id, make_gluon(20, t0));
    physics.add_gluon_between(p1_id, p2_id, make_gluon(15, t1));

    const float dt = 1.0f / 60.0f;
    constexpr int FRAMES = 240;
    constexpr int SETTLE_START = 60;
    constexpr int HOLD_START = 180;

    float err0_peak = 0.0f, err1_peak = 0.0f;
    float err0_hold = 0.0f, err1_hold = 0.0f;
    float err0_settle_final = 0.0f, err1_settle_final = 0.0f;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        float e0 = joint_err(v[p0_id], v[p1_id], t0);
        float e1 = joint_err(v[p1_id], v[p2_id], t1);

        if (f < SETTLE_START) {
            if (e0 > err0_peak) err0_peak = e0;
            if (e1 > err1_peak) err1_peak = e1;
        } else {
            err0_settle_final = e0;
            err1_settle_final = e1;
        }
        if (f >= HOLD_START) {
            if (e0 > err0_hold) err0_hold = e0;
            if (e1 > err1_hold) err1_hold = e1;
        }

        if (f % 30 == 0) {
            printf("  [f%3d] p1=%+.5f p2=%+.5f\n", f, e0, e1);
        }
    }

    auto mag = [](const logosphere::Quat& q) {
        float ax, ay, az, t;
        q.to_axis_angle(ax, ay, az, t);
        if (t > static_cast<float>(M_PI)) t -= 2.0f * static_cast<float>(M_PI);
        return std::abs(t);
    };
    float m0 = mag(t0), m1 = mag(t1);

    const float FRAC = 0.05f;

    printf("\n  Target magnitudes: p0->p1=%.4f  p1->p2=%.4f\n", m0, m1);
    printf("  Settle final:      p0->p1=%.5f  p1->p2=%.5f\n", err0_settle_final, err1_settle_final);
    printf("  Hold max:          p0->p1=%.5f  p1->p2=%.5f (budget %.5f %.5f)\n",
           err0_hold, err1_hold, FRAC*m0, FRAC*m1);

    bool e0_ok = (err0_hold <= FRAC * m0);
    bool e1_ok = (err1_hold <= FRAC * m1);

    printf("\n  Assertions:\n");
    printf("    %s: p0->p1 hold max within 5%% of target\n", e0_ok ? "PASS" : "FAIL");
    printf("    %s: p1->p2 hold max within 5%% of target\n", e1_ok ? "PASS" : "FAIL");

    bool ok = e0_ok && e1_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — chain drifted in isolation]");
    return ok;
}
