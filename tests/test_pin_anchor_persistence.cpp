// =============================================================================
// PIN ANCHOR PERSISTENCE — foot-plant anchors are moved, never churned
// =============================================================================
// Phase 4b originally destroyed + created a 0.01³ anchor particle on
// every heel strike. Eden RCA 2026-07-13: each deletion flush costs a
// synchronous GPU pipeline wait (51 ms) plus a full BVH rebuild (count
// changed), and at low FPS heel strikes land every 2-3 frames — a death
// spiral to 8.5 FPS. The particle churn also feeds the deletion queue
// and swap cascade continuously.
//
// Contract: each foot owns ONE persistent anchor, created lazily on its
// first plant and MOVED on every subsequent plant. Steady-state walking
// changes the particle count by ZERO.
//
//   p1  after the second heel strike the particle count never changes
//       again (through walking AND after stopping).
//   p2  the engaged anchor id only ever takes two distinct values
//       (left anchor, right anchor).
//   p3  whenever a plant is engaged, the foot↔anchor pin gluon exists
//       (the [PIN_LOST] class, sampled every frame).
//   p4  walk→idle disengages: engaged id goes -1, anchors persist.
//
// Bare-subsystem harness (no Engine): the only particle count changes
// possible are the pin machinery's own. Mirrors test_pin_gluon_lifecycle.
//
// Run: ./build/logosphere-tests --test test_pin_anchor_persistence --no-head
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

#include <cstdio>
#include <set>
#include <vector>

bool test_pin_anchor_persistence() {
    printf("\n=== Pin Anchor Persistence (move, never churn) ===\n");

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

    humanoid.set_volitional(hips_id, true);
    humanoid.set_target_velocity(hips_id, 0.0f, 1.5f);

    const double dt = 1.0 / 60.0;
    auto step = [&]() {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    };

    constexpr int WALK_FRAMES = 300;
    constexpr int IDLE_FRAMES = 60;

    std::set<int> anchor_ids_seen;
    int strikes = 0;
    int prev_anchor = -1;
    int second_strike_frame = -1;
    size_t count_at_strike2 = 0;
    size_t count_min = 0, count_max = 0;
    int gluon_misses = 0;
    int engaged_samples = 0;

    auto gluon_exists = [&](int anchor) {
        return physics.get_gluon(static_cast<size_t>(left_leg[0]),
                                 static_cast<size_t>(anchor)) != nullptr
            || physics.get_gluon(static_cast<size_t>(right_leg[0]),
                                 static_cast<size_t>(anchor)) != nullptr;
    };

    for (int f = 0; f < WALK_FRAMES; ++f) {
        step();
        int anchor = humanoid.get_plant_anchor_particle_id(hips_id);
        if (anchor >= 0) {
            anchor_ids_seen.insert(anchor);
            engaged_samples++;
            if (!gluon_exists(anchor)) gluon_misses++;
            if (anchor != prev_anchor) {
                strikes++;
                if (strikes == 2) {
                    second_strike_frame = f;
                    count_at_strike2 = ps.count();
                    count_min = count_max = count_at_strike2;
                }
            }
        }
        if (second_strike_frame >= 0) {
            size_t c = ps.count();
            if (c < count_min) count_min = c;
            if (c > count_max) count_max = c;
        }
        prev_anchor = anchor;
    }

    humanoid.set_target_velocity(hips_id, 0.0f, 0.0f);
    for (int f = 0; f < IDLE_FRAMES; ++f) {
        step();
        if (second_strike_frame >= 0) {
            size_t c = ps.count();
            if (c < count_min) count_min = c;
            if (c > count_max) count_max = c;
        }
    }
    int anchor_after_stop = humanoid.get_plant_anchor_particle_id(hips_id);

    printf("  strikes=%d distinct_anchor_ids=%zu engaged_samples=%d\n",
           strikes, anchor_ids_seen.size(), engaged_samples);
    printf("  count at strike 2 = %zu, min/max after = %zu/%zu\n",
           count_at_strike2, count_min, count_max);

    bool enough_strikes = (strikes >= 4);
    printf("  %s: sanity — at least 4 heel strikes fired (got %d)\n",
           enough_strikes ? "PASS" : "FAIL", strikes);

    bool p1 = enough_strikes && (count_min == count_max);
    printf("  %s: p1 particle count constant after second strike (min=%zu max=%zu)\n",
           p1 ? "PASS" : "FAIL", count_min, count_max);

    bool p2 = enough_strikes && (anchor_ids_seen.size() <= 2);
    printf("  %s: p2 anchors persistent — %zu distinct ids across %d strikes (want <= 2)\n",
           p2 ? "PASS" : "FAIL", anchor_ids_seen.size(), strikes);

    bool p3 = (gluon_misses == 0) && (engaged_samples > 0);
    printf("  %s: p3 pin gluon present on every engaged frame (%d misses / %d samples)\n",
           p3 ? "PASS" : "FAIL", gluon_misses, engaged_samples);

    bool p4 = (anchor_after_stop == -1);
    printf("  %s: p4 walk->idle disengages (engaged id = %d)\n",
           p4 ? "PASS" : "FAIL", anchor_after_stop);

    humanoid.shutdown();
    dyn.shutdown();
    physics.shutdown();

    bool ok = enough_strikes && p1 && p2 && p3 && p4;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL - pin anchors churn particles]");
    return ok;
}
