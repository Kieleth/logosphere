// =============================================================================
// RAGDOLL: when the driver lets go, the body falls
// =============================================================================
// KINEMATIC is a TRANSIENT AUTHORITY (owner ruling 2026-08-14, LEDGER.md):
// an external writer owns a body's position WHILE IT IS DRIVING IT, and
// whoever takes that authority owns handing it back. A body left KINEMATIC
// after its driver stops can never fall, tip, or ragdoll again — it is
// pinned to the air by a system that is no longer there.
//
// This test is the humanoid's half of that law. A registered humanoid is
// animation-driven and correctly KINEMATIC. Unregister it — the locomotion
// system is explicitly letting go — and every particle must return to
// physics: DYNAMIC, gravity-eligible, collidable. The corpse falls.
//
// WHAT IT CAUGHT (2026-08-14): unregister_humanoid removed the humanoid
// from the driver list and tore down its plant anchors, but released not
// one particle. So a humanoid could not be knocked over or ragdoll at all,
// which is also why TransformationEffect::KNOCKBACK is an enum with no
// implementation and why test_humanoid_impact asserts a hips displacement
// that is structurally always zero (and blames friction for it).
//
// The hips are the specific victim: register pins EVERY particle in
// all_particle_indices, and the two release paths in humanoid_locomotion
// walk JOINT CHILDREN. The hips are the root of the joint hierarchy — no
// joint's child — so no release path could ever reach them.
//
// Run: ./build/test_humanoid_ragdoll
// =============================================================================

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19), observed through Argus so the asserts and the log read
// one source. Seventeen bodies, and the claim is about all of them.
//
//   EVERY PARTICLE (all 17, after the driver lets go)
//     solver_mode   — ASSERTED DYNAMIC. This was the whole test.
//     z             — ASSERTED, and this is the addition that matters.
//                     solver_mode is a label; falling is the behaviour.
//                     A body can be DYNAMIC and still inert, because
//                     is_quat_driven takes one out of gravity and
//                     response entirely with no release path — the third
//                     unnamed pin, documented in test_humanoid_knockback
//                     and owed to task #43. "Every particle is handed
//                     back to physics" is now measured by every particle
//                     actually falling, not by 17 enum reads plus one
//                     body's descent.
//     fall distance — ASSERTED against the analytic 0.5*g*t^2, not
//                     against "more than 5 cm". The loose bound passes
//                     on a body that barely twitched.
//     x, y          — ASSERTED unmoved: nothing pushes sideways, so a
//                     corpse that drifts is being written by something
//                     that has not let go.
//     omega         — ASSERTED at zero: no contact, no torque, nothing
//                     to spin any of them.
//     orientation   — ASSERTED at identity; coherence between the two
//                     ledgers ASSERTED (G-23).
//     relative      — ASSERTED: with no bonds and no contacts every
//                     particle falls at the same rate, so the rig's
//                     internal separations must be preserved exactly.
//                     A corpse that came apart while falling would
//                     satisfy every per-body assert above.
//     vz            — implied by the fall assert; WAIVED, printed.

#include "core/argus.h"
#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "generated/physics_constants.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}

}  // namespace

int main() {
    printf("\n=== RAGDOLL: the driver lets go, the body falls ===\n");

    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg(registry);
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;

    if (!physics.initialize(ps) || !dyn.initialize_headless(ps) ||
        !humanoid.initialize_headless(ps, physics, kg, dyn, tracer)) {
        printf("  [FAIL] headless init\n");
        return 1;
    }

    auto spawn = [&](float x, float y, float z) {
        Particle p{};
        p.x = x; p.y = y; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = 0.1f; p.height = 0.1f; p.thickness = 0.1f;
        p.SetMaterial(Materials::Type::FLESH);
        int id = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        return id;
    };

    // A humanoid standing well clear of the world floor, so "did it fall"
    // is a question about gravity and nothing else.
    const float HIPS_Z0 = 3.0f;
    int hips_id    = spawn(0.0f, 0.0f, HIPS_Z0);
    int abdomen_id = spawn(0.0f, 0.0f, HIPS_Z0 + 0.10f);
    int chest_id   = spawn(0.0f, 0.0f, HIPS_Z0 + 0.30f);
    int neck_id    = spawn(0.0f, 0.0f, HIPS_Z0 + 0.50f);
    int head_id    = spawn(0.0f, 0.0f, HIPS_Z0 + 0.65f);
    std::vector<int> torso_ids = { hips_id, abdomen_id, chest_id,
                                   neck_id, head_id };
    auto limb = [&](float side_x, float z0) {
        return std::vector<int>{ spawn(side_x, 0.0f, z0),
                                 spawn(side_x, 0.0f, z0 + 0.40f),
                                 spawn(side_x, 0.0f, z0 + 0.80f) };
    };
    std::vector<int> left_leg  = limb(-0.10f, HIPS_Z0 - 0.95f);
    std::vector<int> right_leg = limb( 0.10f, HIPS_Z0 - 0.95f);
    std::vector<int> left_arm  = limb(-0.20f, HIPS_Z0 - 0.25f);
    std::vector<int> right_arm = limb( 0.20f, HIPS_Z0 - 0.25f);

    humanoid.register_humanoid_direct(
        hips_id, left_leg, right_leg, left_arm, right_arm, torso_ids,
        250.0f, 500.0f, kg::INVALID_ENTITY);

    // ---- 1. While registered, the animation owns the body. -------------
    {
        auto v = ps.lock_particles_for_read();
        check(v[hips_id].solver_mode == ParticleSolverMode::KINEMATIC,
              "registered: the animation holds authority over the hips");
    }

    // ---- 2. The driver lets go. ----------------------------------------
    humanoid.unregister_humanoid(hips_id);

    {
        auto v = ps.lock_particles_for_read();
        int pinned = 0;
        for (int id : { hips_id, abdomen_id, chest_id, neck_id, head_id,
                        left_leg[0], left_leg[1], left_leg[2],
                        right_leg[0], right_leg[1], right_leg[2],
                        left_arm[0], left_arm[1], left_arm[2],
                        right_arm[0], right_arm[1], right_arm[2] })
            if (v[id].solver_mode == ParticleSolverMode::KINEMATIC) pinned++;
        printf("  [measure] still KINEMATIC after unregister: %d of 17\n",
               pinned);
        check(pinned == 0,
              "unregistered: every particle is handed back to physics");
        check(v[hips_id].solver_mode == ParticleSolverMode::DYNAMIC,
              "unregistered: the HIPS specifically are DYNAMIC "
              "(the root no release path reached)");
    }

    // ---- 3. And therefore it falls. ------------------------------------
    // The measurement that matters: a released body is subject to gravity.
    // 0.5*g*t^2 over 0.5 s is ~1.2 m; anything past a few cm proves the
    // solver owns it again. Tolerance is loose on purpose — this test is
    // about authority, not about integrator accuracy.
    const std::vector<int> all = { hips_id, abdomen_id, chest_id, neck_id,
                                   head_id,
                                   left_leg[0], left_leg[1], left_leg[2],
                                   right_leg[0], right_leg[1], right_leg[2],
                                   left_arm[0], left_arm[1], left_arm[2],
                                   right_arm[0], right_arm[1], right_arm[2] };
    // THE WITNESS watches the whole corpse, not just the hips. A body can
    // be DYNAMIC and still inert: is_quat_driven exempts it from gravity
    // and response with no release path, so seventeen enum reads plus one
    // body's descent do not prove seventeen bodies were released.
    logosphere::Argus argus;
    static const char* NAMES[17] = {
        "hips", "abdomen", "chest", "neck", "head",
        "l_thigh", "l_shin", "l_foot", "r_thigh", "r_shin", "r_foot",
        "l_upper", "l_fore", "l_hand", "r_upper", "r_fore", "r_hand" };
    for (size_t i = 0; i < all.size(); ++i) argus.watch(all[i], NAMES[i]);

    std::vector<float> x0(all.size()), y0(all.size()), z0(all.size());
    {
        auto v = ps.lock_particles_for_read();
        for (size_t i = 0; i < all.size(); ++i) {
            x0[i] = v[all[i]].x; y0[i] = v[all[i]].y; z0[i] = v[all[i]].z;
        }
    }
    const float sep0 = std::fabs(z0[4] - z0[0]);   // hips to head, at rest
    const float z_before = ps.lock_particles_for_read()[hips_id].z;
    const int   FALL_FRAMES = 30;
    for (int f = 0; f < FALL_FRAMES; ++f) {
        physics.update(1.0 / 60.0);
        argus.observe(ps, f);
    }
    const float z_after = ps.lock_particles_for_read()[hips_id].z;
    const float fell = z_before - z_after;
    const float t_fall = FALL_FRAMES / 60.0f;
    const float analytic = 0.5f * PhysicsV4::GRAVITY * t_fall * t_fall;
    printf("  [measure] hips fell %.3f m in 0.5 s (from z=%.2f to %.2f); "
           "analytic 0.5*g*t^2 = %.3f\n", fell, z_before, z_after, analytic);

    // Every particle, not just the hips.
    float worst_fall_err = 0.0f, worst_lateral = 0.0f, worst_spin = 0.0f;
    float worst_tilt = 0.0f, worst_div = 0.0f;
    int   least_fallen = 0;
    float least_fall = 1e9f;
    for (size_t i = 0; i < all.size(); ++i) {
        const logosphere::Argus::State* s = argus.latest(all[i]);
        if (!s) continue;
        const float d = z0[i] - s->z;
        if (d < least_fall) { least_fall = d; least_fallen = (int)i; }
        const float e = std::fabs(d - analytic);
        if (e > worst_fall_err) worst_fall_err = e;
        const float lat = std::sqrt((s->x - x0[i]) * (s->x - x0[i])
                                  + (s->y - y0[i]) * (s->y - y0[i]));
        if (lat > worst_lateral) worst_lateral = lat;
        const float sp = argus.spin(all[i]);
        if (sp > worst_spin) worst_spin = sp;
        const float tilt = std::fmax(std::fabs(s->rx),
                           std::fmax(std::fabs(s->ry), std::fabs(s->rz)));
        if (tilt > worst_tilt) worst_tilt = tilt;
        const float dv = argus.divergence(all[i]);
        if (dv > worst_div) worst_div = dv;
    }
    const float sep1 = argus.separation(hips_id, head_id);
    printf("  [argus] all 17: least-fallen part is %s at %.3f m; worst "
           "departure from the analytic fall %.4f m\n",
           NAMES[least_fallen], least_fall, worst_fall_err);
    printf("  [argus] worst lateral drift %.2e m, worst spin %.2e rad/s, "
           "worst tilt %.2e rad, worst divergence %.6f rad\n",
           worst_lateral, worst_spin, worst_tilt, worst_div);
    printf("  [argus] hips-to-head separation %.4f -> %.4f m "
           "(the corpse must not come apart)\n", sep0, sep1);
    printf("\n  the witness's last frame:\n");
    argus.dump(std::cout, 1);
    printf("\n");

    check(fell > 0.05f, "released: the corpse falls under gravity");
    check(std::fabs(fell - analytic) < 0.02f,
          "and it falls the RIGHT distance: 0.5*g*t^2, not merely 'more "
          "than five centimetres'");
    check(least_fall > 0.9f * analytic,
          "and ALL SEVENTEEN fall, not just the hips. solver_mode is a "
          "label; falling is the behaviour, and is_quat_driven can leave "
          "a DYNAMIC body inert with no release path (the third pin, "
          "task #43).");
    check(worst_fall_err < 0.02f,
          "and they fall TOGETHER: no part lags or leads the rest");
    check(worst_lateral < 1e-4f,
          "nothing drifted sideways: a corpse that wanders is being "
          "written by something that has not let go");
    check(worst_spin < 1e-4f && worst_tilt < 1e-4f,
          "and nothing spun: there is no contact and no torque in this "
          "fall");
    check(std::fabs(sep1 - sep0) < 1e-4f,
          "the corpse did not come apart: with no bonds and no contacts "
          "every part falls at the same rate, so the rig's internal "
          "geometry is preserved exactly");
    check(worst_div < 0.01f,
          "one body, one orientation, for all seventeen (G-23)");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "RAGDOLL OK" : "RAGDOLL BROKEN", failures);
    return failures == 0 ? 0 : 1;
}
