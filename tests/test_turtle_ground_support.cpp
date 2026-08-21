// =============================================================================
// TURTLE GROUND SUPPORT — the world floor is ground even with no floor tiles
// =============================================================================
// THE LAW: INV-1, the turtle boundary is the only intrinsically immovable
// thing and NOTHING is ever placed or ends below the turtle plane beyond SLOP.
// INV-30 is the second half of this file: the walker is owner=DYNAMICS and
// solver KINEMATIC, so its position comes from an EXTERNAL WRITER, and the
// doors into the solver must refuse an illegal placement rather than trusting
// the physics inside it. INV-2 covers t2, the hips not sinking into the floor.
//
// Task #42 regression (CLASS-1 foot-sink). The entity ground-support path
// (apply_entity_gravity + maintain_entity_shape) accepted only BVH
// particles as support. The turtle plane at z = TURTLE_Z — the one surface
// guaranteed under every footprint — was invisible to it, and the solver's
// own turtle plane lifts solver-DYNAMIC bodies only; a registered humanoid
// is owner=DYNAMICS + solver KINEMATIC, so nothing ever lifted it.
//
// Measured failure this test locks out (bare harness, pre-fix):
//   [FOOT_PLANT] target z=0.047, foot z drifting -0.4..-0.9 at blend=1.00,
//   per-frame delta growing by exactly g*dt^2 (free fall), then
//   queue_particle_addition of the pin anchor at z=-0.9055 tripping the
//   TURTLE_STRICT abort.
//
// Control (what made the old behavior red): before the fix the minimum
// foot z in this exact scenario measured below -0.5 within 120 walk
// frames. After the fix it stays at plant height (~+0.05).
//
//   t1  during walk + idle on the bare turtle, no foot center ever drops
//       below TURTLE_Z (feet stay on or above the world floor)
//   t2  the hips never sink below standing height minus a step-dip budget
//   t3  a plant anchor, if spawned, sits at or above TURTLE_Z
//
// Bare-subsystem harness (no Engine, no BVH build): exactly the world in
// which the class was found — the fix must hold with no acceleration
// structure at all.
//
// Run: ./build/logosphere-tests --test test_turtle_ground_support --no-head
// =============================================================================

#include "../src/core/particle_system.h"
#include "../src/core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "../src/particle.h"
#include "../src/materials.h"

#include <algorithm>
#include <cstdio>
#include <vector>

bool test_turtle_ground_support() {
    printf("\n=== Turtle Ground Support (bare world floor is ground) ===\n");

    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg(registry);
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;

    physics.initialize(ps);
    dyn.initialize_headless(ps);
    humanoid.initialize_headless(ps, physics, kg, dyn, tracer);

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

    int hips_id    = spawn(0.0f, 0.0f, 1.00f);
    int abdomen_id = spawn(0.0f, 0.0f, 1.10f);
    int chest_id   = spawn(0.0f, 0.0f, 1.30f);
    int neck_id    = spawn(0.0f, 0.0f, 1.50f);
    int head_id    = spawn(0.0f, 0.0f, 1.65f);
    std::vector<int> torso_ids = { hips_id, abdomen_id, chest_id, neck_id, head_id };

    auto leg = [&](float side_x) {
        std::vector<int> ids;
        ids.push_back(spawn(side_x, 0.0f, 0.05f));   // foot — on the turtle
        ids.push_back(spawn(side_x, 0.0f, 0.45f));   // shin
        ids.push_back(spawn(side_x, 0.0f, 0.85f));   // thigh
        return ids;
    };
    auto arm = [&](float side_x) {
        std::vector<int> ids;
        ids.push_back(spawn(side_x, 0.0f, 1.30f));
        ids.push_back(spawn(side_x, 0.0f, 1.05f));
        ids.push_back(spawn(side_x, 0.0f, 0.75f));
        return ids;
    };
    std::vector<int> left_leg  = leg(-0.10f);
    std::vector<int> right_leg = leg( 0.10f);
    std::vector<int> left_arm  = arm(-0.20f);
    std::vector<int> right_arm = arm( 0.20f);

    humanoid.register_humanoid_direct(
        hips_id, left_leg, right_leg, left_arm, right_arm,
        torso_ids,
        250.0f, 500.0f,
        kg::INVALID_ENTITY);

    humanoid.set_volitional(hips_id, true);
    humanoid.set_target_velocity(hips_id, 0.0f, 1.2f);

    const double dt = 1.0 / 60.0;
    float min_foot_z   = 1e9f;
    float min_hips_z   = 1e9f;
    float min_anchor_z = 1e9f;
    bool  saw_anchor   = false;

    auto sample = [&]() {
        auto v = ps.lock_particles_for_read();
        min_foot_z = std::min({min_foot_z,
                               v[left_leg[0]].z, v[right_leg[0]].z});
        min_hips_z = std::min(min_hips_z, v[hips_id].z);
        int anchor = humanoid.get_plant_anchor_particle_id(hips_id);
        if (anchor >= 0 && static_cast<size_t>(anchor) < v.size()) {
            saw_anchor = true;
            min_anchor_z = std::min(min_anchor_z, v[anchor].z);
        }
    };

    // Walk 120 frames on the bare turtle, then stop and idle 180 frames
    // (the pre-fix sink reached z < -0.5 well inside the walk phase).
    for (int i = 0; i < 120; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
        sample();
    }
    humanoid.set_target_velocity(hips_id, 0.0f, 0.0f);
    for (int i = 0; i < 180; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
        sample();
    }

    printf("  min foot z   = %+.4f  (budget >= %.4f)\n", min_foot_z, 0.0f);
    printf("  min hips z   = %+.4f  (budget >= 0.60)\n", min_hips_z);
    if (saw_anchor)
        printf("  min anchor z = %+.4f  (budget >= 0.0)\n", min_anchor_z);
    else
        printf("  (no plant anchor observed)\n");

    // t1: foot CENTERS stay at or above the turtle plane. Centers sit
    // half a foot-thickness above the sole, so >= 0 leaves room for the
    // sole to ride exactly on TURTLE_Z without tripping.
    bool t1 = (min_foot_z >= 0.0f);
    // t2: hips never sink materially below standing height (1.0 spawn;
    // gait dip + ground correction land around 0.9-1.0; the pre-fix
    // failure took them to 0.06 and below).
    bool t2 = (min_hips_z >= 0.60f);
    // t3: every observed anchor sits at or above the world floor — the
    // pre-fix abort was exactly an anchor queued at z=-0.9055.
    bool t3 = (!saw_anchor || min_anchor_z >= 0.0f);

    printf("  %s: t1 INV-1: feet never below the turtle plane\n", t1 ? "PASS" : "FAIL");
    printf("  %s: t2 INV-2: hips never sink toward the floor\n",  t2 ? "PASS" : "FAIL");
    printf("  %s: t3 INV-1/INV-30: plant anchors at or above TURTLE_Z — an external writer places nothing illegal\n", t3 ? "PASS" : "FAIL");

    humanoid.shutdown();
    dyn.shutdown();
    physics.shutdown();

    bool ok = t1 && t2 && t3;
    printf("\n  %s\n", ok ? "[PASS]"
        : "[FAIL INV-1 — walker sank through the world floor]");
    return ok;
}
