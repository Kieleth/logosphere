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

#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
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
    const float z_before = ps.lock_particles_for_read()[hips_id].z;
    for (int f = 0; f < 30; ++f) physics.update(1.0 / 60.0);
    const float z_after = ps.lock_particles_for_read()[hips_id].z;
    const float fell = z_before - z_after;
    printf("  [measure] hips fell %.3f m in 0.5 s (from z=%.2f to %.2f)\n",
           fell, z_before, z_after);
    check(fell > 0.05f, "released: the corpse falls under gravity");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "RAGDOLL OK" : "RAGDOLL BROKEN", failures);
    return failures == 0 ? 0 : 1;
}
