// Phase 4b — pin-gluon lifecycle round-trip.
//
// Spawns a stub humanoid (physics-drive legs default-on since
// Phase 5), drives a few walk frames so a heel-strike fires, then
// checks that an anchor particle exists + the foot↔anchor pin gluon
// is wired. Stops the humanoid, runs a few more frames, and checks
// the anchor + gluon are torn down.
//
// Headless. No GPU. Mirrors the test_humanoid_headless setup.
//
// Run: ./build/test_pin_gluon_lifecycle

#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cstdio>
#include <vector>

int main() {
    int failures = 0;

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
        ids.push_back(spawn(side_x, 0.0f, 0.05f));
        ids.push_back(spawn(side_x, 0.0f, 0.45f));
        ids.push_back(spawn(side_x, 0.0f, 0.85f));
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

    // Phase 5: physics-drive legs is the default for every humanoid;
    // register_humanoid_direct already wired the pin-gluon lifecycle
    // gates, so heel-strikes spawn anchors with no opt-in.

    humanoid.set_volitional(hips_id, true);
    humanoid.set_target_velocity(hips_id, 0.0f, 1.5f);

    const double dt = 1.0 / 60.0;

    // Walk for enough frames that at least one heel-strike fires.
    // Heel-strikes happen at walk_phase half-cycle boundaries; with
    // the stub humanoid's clip frequency, ~30 frames is plenty.
    for (int i = 0; i < 60; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }

    int anchor_after_walk = humanoid.get_plant_anchor_particle_id(hips_id);

    if (anchor_after_walk < 0) {
        printf("[FAIL] no plant_anchor_particle_id after 60 walk frames "
               "(heel-strike never fired or ops never flushed)\n");
        failures++;
    } else {
        printf("[PASS] heel-strike spawned anchor: id=%d\n", anchor_after_walk);
    }

    // Verify the gluon was wired: stance foot (whichever side planted)
    // ↔ anchor particle.
    bool gluon_found = false;
    if (anchor_after_walk >= 0) {
        auto try_foot = [&](int foot_id) {
            return physics.get_gluon(static_cast<size_t>(foot_id),
                                     static_cast<size_t>(anchor_after_walk)) != nullptr;
        };
        gluon_found = try_foot(left_leg[0]) || try_foot(right_leg[0]);
    }
    if (gluon_found) {
        printf("[PASS] foot↔anchor pin gluon wired\n");
    } else {
        printf("[FAIL] no foot↔anchor pin gluon found after heel-strike\n");
        failures++;
    }

    // Stop the humanoid. Walk→idle should release the plant.
    humanoid.set_target_velocity(hips_id, 0.0f, 0.0f);
    for (int i = 0; i < 30; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }

    int anchor_after_stop = humanoid.get_plant_anchor_particle_id(hips_id);

    if (anchor_after_stop != -1) {
        printf("[FAIL] plant_anchor_particle_id not cleared after stop: %d\n",
               anchor_after_stop);
        failures++;
    } else {
        printf("[PASS] walk→idle cleared plant_anchor_particle_id\n");
    }

    // Gluon teardown: the anchor particle id may have been queue-deleted,
    // and remove_gluons_for_particle was called on the anchor. The gluon
    // referencing (foot, anchor) should be gone.
    bool gluon_after_stop = false;
    if (anchor_after_walk >= 0) {
        auto try_foot = [&](int foot_id) {
            return physics.get_gluon(static_cast<size_t>(foot_id),
                                     static_cast<size_t>(anchor_after_walk)) != nullptr;
        };
        gluon_after_stop = try_foot(left_leg[0]) || try_foot(right_leg[0]);
    }
    if (gluon_after_stop) {
        printf("[FAIL] foot↔anchor gluon still wired after stop\n");
        failures++;
    } else {
        printf("[PASS] foot↔anchor gluon torn down on stop\n");
    }

    humanoid.shutdown();
    dyn.shutdown();
    physics.shutdown();

    if (failures > 0) {
        printf("\n[FAIL] %d failures\n", failures);
        return 1;
    }
    printf("\n[PASS] pin-gluon lifecycle round-trip\n");
    return 0;
}
