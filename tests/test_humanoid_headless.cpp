// Headless smoke test for HumanoidLocomotion.
//
// Constructs ParticleSystem, KGModule, PhysicsSystem, ParticleDynamicsSystem,
// ParticleTracer, HumanoidLocomotion using the *_headless initialize variants
// — no Engine, no GLFW, no Metal. Verifies that all subsystems wire together
// and that a humanoid can be registered without segfault.
//
// Purpose: proves the post-B22/B23/B24 decoupling is real. If this builds and
// passes, the headless test infrastructure is alive and locomotion can be
// validated in CI without GPU.
//
// Run: ./build/test_humanoid_headless

// FULL-STATE NARRATION (assert-or-waive, owner directive 2026-08-19).
//
// This is a smoke test and stays one; what it may not be is a smoke test
// whose behavioural bounds are so loose that nothing can fail them. Three
// were, and they are tightened here against quantities the test already
// commands or already knows.
//
//   HIPS
//     y during the walk — was ASSERTED as "> 0.001 m". The walk is
//                     COMMANDED at 1.5 m/s for 30 frames, so the distance
//                     is 0.75 m and the assert is now against that. The
//                     old bound passes on a hips that moved one
//                     millimetre in half a second.
//     x during the walk — was UNASSERTED. A due-north walk has no lateral
//                     component; now asserted.
//     x, y in strafe   — the X-dominance check stands; +Y is now
//                     asserted near zero in its own right, since
//                     "dominant" is satisfied by two large numbers.
//     rotation_z       — ASSERTED (the facing cascade). Unchanged.
//     z                — WAIVED, watched: the rig settles onto the
//                     turtle over the run (measured 1.005 to 0.924 in
//                     the neighbouring knockback control) and that
//                     descent is the foot-plant front, not this test's.
//
//   THE RIG (Argus)
//     bone lengths     — ASSERTED as of 2026-08-19. The old coherence
//                     check was "no part further than 2.0 m from the
//                     hips" on a rig 1.65 m tall whose worst part sits
//                     at 0.958 m: more than double the slack it could
//                     ever need, and blind to a torso that stretched.
//                     Argus now measures each torso bone at rest and
//                     after the walk. Bones do not stretch.
//     limb geometry    — WAIVED: FK swings limbs by design, so their
//                     distance from the hips legitimately changes. The
//                     generous radius bound is kept for them.
//     omega, orientation — WAIVED, watched: animation owns this rig.

#include "core/argus.h"
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
#include <cstdlib>
#include <vector>

int main() {
    int failures = 0;

    // 1. Construct all subsystems standalone.
    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg(registry);
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;

    // 2. Initialize each one in headless mode.
    if (!physics.initialize(ps)) {
        printf("[FAIL] PhysicsSystem::initialize(ParticleSystem&)\n");
        failures++;
    } else {
        printf("[PASS] PhysicsSystem::initialize(ParticleSystem&)\n");
    }

    if (!dyn.initialize_headless(ps)) {
        printf("[FAIL] ParticleDynamicsSystem::initialize_headless\n");
        failures++;
    } else {
        printf("[PASS] ParticleDynamicsSystem::initialize_headless\n");
    }

    if (!humanoid.initialize_headless(ps, physics, kg, dyn, tracer)) {
        printf("[FAIL] HumanoidLocomotion::initialize_headless\n");
        failures++;
    } else {
        printf("[PASS] HumanoidLocomotion::initialize_headless\n");
    }

    // 3. Verify update_pre_physics + update_post_physics don't segfault on
    //    an empty entity set.
    humanoid.update_pre_physics(1.0 / 60.0);
    humanoid.update_post_physics(1.0 / 60.0);
    printf("[PASS] HumanoidLocomotion::update_pre_physics + update_post_physics (empty)\n");

    // 4. Spawn a minimal humanoid: 5 torso particles + 4 per leg + 4 per arm
    //    laid out in a vertical line at the origin. Stub geometry — what
    //    matters here is that the registration + per-frame update path
    //    doesn't blow up and that locomotion advances the hips.
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
        ids.push_back(spawn(side_x, 0.0f, 0.05f));   // foot
        ids.push_back(spawn(side_x, 0.0f, 0.45f));   // shin
        ids.push_back(spawn(side_x, 0.0f, 0.85f));   // thigh
        return ids;
    };
    auto arm = [&](float side_x) {
        std::vector<int> ids;
        ids.push_back(spawn(side_x, 0.0f, 1.30f));   // shoulder
        ids.push_back(spawn(side_x, 0.0f, 1.05f));   // upper arm
        ids.push_back(spawn(side_x, 0.0f, 0.75f));   // forearm
        return ids;
    };
    std::vector<int> left_leg  = leg(-0.10f);
    std::vector<int> right_leg = leg( 0.10f);
    std::vector<int> left_arm  = arm(-0.20f);
    std::vector<int> right_arm = arm( 0.20f);
    printf("[PASS] spawned %d particles (humanoid stub)\n",
           static_cast<int>(ps.lock_particles_for_read().size()));

    humanoid.register_humanoid_direct(
        hips_id, left_leg, right_leg, left_arm, right_arm,
        torso_ids,
        250.0f, 500.0f,
        kg::INVALID_ENTITY);
    printf("[PASS] register_humanoid_direct\n");

    // THE WITNESS. It answers the one structural question the radius
    // bound below cannot: did any BONE change length? Torso segments are
    // rigid by construction; FK swings limbs, so only the torso chain is
    // held to it.
    logosphere::Argus argus;
    argus.watch(hips_id, "hips");
    argus.watch(abdomen_id, "abdomen");
    argus.watch(chest_id, "chest");
    argus.watch(neck_id, "neck");
    argus.watch(head_id, "head");
    const int BONES[4][2] = { { hips_id, abdomen_id }, { abdomen_id, chest_id },
                              { chest_id, neck_id },   { neck_id, head_id } };
    static const char* BONE_NAMES[4] = { "hips-abdomen", "abdomen-chest",
                                         "chest-neck", "neck-head" };
    float bone0[4];
    argus.observe(ps, -1);
    for (int b = 0; b < 4; ++b) bone0[b] = argus.separation(BONES[b][0], BONES[b][1]);

    // 5. Drive locomotion: target +Y at 1.5 m/s, run a handful of frames,
    //    check the hips advanced. We're not running real physics here, so
    //    we don't expect end-of-walk-cycle perfection — just that the
    //    target-velocity pipeline + maintain_entity_shape + locomotion
    //    update path moves the hips by *some* positive amount.
    humanoid.set_volitional(hips_id, true);
    humanoid.set_target_velocity(hips_id, 0.0f, 1.5f);

    float start_y;
    {
        auto view = ps.lock_particles_for_read();
        start_y = view[hips_id].y;
    }

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 30; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }

    float end_y;
    {
        auto view = ps.lock_particles_for_read();
        end_y = view[hips_id].y;
    }
    float dy = end_y - start_y;
    // The walk was COMMANDED at 1.5 m/s for 30 frames. The distance it
    // should cover is that, not "more than a millimetre".
    const float COMMANDED = 1.5f * 30.0f * (float)dt;
    printf("[measure] hips advanced +Y by %.4f m in 30 frames "
           "(commanded 1.5 m/s = %.4f m)\n", dy, COMMANDED);
    if (dy > 0.8f * COMMANDED && dy < 1.2f * COMMANDED) {
        printf("[PASS] hips advanced +Y at the speed it was told to, "
               "%.0f%% of commanded\n", 100.0f * dy / COMMANDED);
    } else {
        printf("[FAIL] hips advance %.4f is not the commanded %.4f\n",
               dy, COMMANDED);
        failures++;
    }
    {   // A due-north walk has no lateral component. This was unasserted.
        auto view = ps.lock_particles_for_read();
        const float dx_walk = std::fabs(view[hips_id].x - 0.0f);
        if (dx_walk < 0.01f) {
            printf("[PASS] and it walked STRAIGHT: lateral drift %.4f m\n",
                   dx_walk);
        } else {
            printf("[FAIL] a due-north walk drifted %.4f m sideways\n", dx_walk);
            failures++;
        }
    }
    {   // BONES DO NOT STRETCH. The radius bound below is 2.0 m on a rig
        // 1.65 m tall; it cannot see a torso coming apart.
        argus.observe(ps, 30);
        float worst_stretch = 0.0f; int worst_bone = 0;
        for (int b = 0; b < 4; ++b) {
            const float now = argus.separation(BONES[b][0], BONES[b][1]);
            const float d = std::fabs(now - bone0[b]);
            printf("[measure] bone %-14s %.4f -> %.4f m\n",
                   BONE_NAMES[b], bone0[b], now);
            if (d > worst_stretch) { worst_stretch = d; worst_bone = b; }
        }
        if (worst_stretch < 0.01f) {
            printf("[PASS] no torso bone changed length: worst %s by "
                   "%.4f m\n", BONE_NAMES[worst_bone], worst_stretch);
        } else {
            printf("[FAIL] torso bone %s changed length by %.4f m\n",
                   BONE_NAMES[worst_bone], worst_stretch);
            failures++;
        }
    }

    // 6. Body coherence — after 30 frames of locomotion, every body
    //    particle should still be inside a sane radius of hips. The
    //    spawned humanoid is ~1.65 m tall, so head-to-hips ≈ 0.65 m;
    //    use 2.0 m as a generous upper bound (FK can swing limbs and
    //    the walk cycle adds vertical bounce).
    {
        auto view = ps.lock_particles_for_read();
        const Particle& hp = view[hips_id];
        float worst = -1.0f;
        int worst_id = -1;
        std::vector<int> all_ids;
        for (int id : torso_ids) if (id != hips_id) all_ids.push_back(id);
        for (int id : left_leg) all_ids.push_back(id);
        for (int id : right_leg) all_ids.push_back(id);
        for (int id : left_arm) all_ids.push_back(id);
        for (int id : right_arm) all_ids.push_back(id);
        for (int id : all_ids) {
            const Particle& p = view[id];
            float ddx = p.x - hp.x;
            float ddy = p.y - hp.y;
            float ddz = p.z - hp.z;
            float d = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
            if (d > worst) { worst = d; worst_id = id; }
        }
        if (worst >= 0.0f && worst < 2.0f) {
            printf("[PASS] body coherent: max distance from hips = %.3fm (id=%d)\n",
                   worst, worst_id);
        } else {
            printf("[FAIL] body drifting apart: max distance %.3fm (id=%d, expected < 2.0)\n",
                   worst, worst_id);
            failures++;
        }
    }

    // 7. Turn-in-place — set_facing_direction to π/2 (face +X). The yaw
    //    cascade in update_pre_physics should ramp head/torso/hips
    //    rotation_z toward the new value over time. Stop the velocity
    //    first so we're testing the cascade, not walk-driven rotation.
    humanoid.set_target_velocity(hips_id, 0.0f, 0.0f);
    for (int i = 0; i < 5; i++) {  // stabilize at zero velocity
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }
    const float TARGET_FACING = static_cast<float>(M_PI) / 2.0f;
    humanoid.set_facing_direction(hips_id, TARGET_FACING);
    for (int i = 0; i < 60; i++) {  // 1s of cascade time
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }
    {
        auto view = ps.lock_particles_for_read();
        float hips_rot = view[hips_id].rotation_z;
        float diff = std::abs(hips_rot - TARGET_FACING);
        if (diff < 0.2f) {
            printf("[PASS] hips facing converged: rotation_z=%.4f (target %.4f, diff %.4f)\n",
                   hips_rot, TARGET_FACING, diff);
        } else {
            printf("[FAIL] hips facing diverged: rotation_z=%.4f (target %.4f, diff %.4f)\n",
                   hips_rot, TARGET_FACING, diff);
            failures++;
        }
    }

    // 8. Strafe — reset facing to 0 (faces +Y north) so the
    //    body-relative axes are unambiguous: forward = +Y, right = +X.
    //    Then strafe right at 1 m/s. Hips should advance on +X
    //    dominantly; +Y movement should stay near zero.
    humanoid.zero_all_velocities(hips_id);
    humanoid.set_facing_direction(hips_id, 0.0f);
    for (int i = 0; i < 30; i++) {  // let the cascade re-converge
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }
    humanoid.zero_all_velocities(hips_id);
    humanoid.set_volitional(hips_id, true);
    humanoid.set_body_relative_velocity(hips_id, 0.0f, 1.0f);

    float strafe_start_x, strafe_start_y;
    {
        auto view = ps.lock_particles_for_read();
        strafe_start_x = view[hips_id].x;
        strafe_start_y = view[hips_id].y;
    }
    for (int i = 0; i < 30; i++) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
    }
    {
        auto view = ps.lock_particles_for_read();
        float dxs = view[hips_id].x - strafe_start_x;
        float dys = view[hips_id].y - strafe_start_y;
        // Facing +Y, strafing right → +X world dominant.
        if (std::abs(dxs) > std::abs(dys) && std::abs(dxs) > 0.001f) {
            printf("[PASS] strafe right: dx=%+.4f dy=%+.4f (X-dominant)\n", dxs, dys);
        } else {
            printf("[FAIL] strafe didn't go X-dominant: dx=%+.4f dy=%+.4f\n", dxs, dys);
            failures++;
        }
        // "Dominant" is satisfied by two large numbers. A pure right
        // strafe has no forward component at all, and that is its own
        // claim.
        if (std::abs(dys) < 0.01f) {
            printf("[PASS] and it was a PURE strafe: forward component "
                   "%+.4f m\n", dys);
        } else {
            printf("[FAIL] strafe carried %+.4f m of forward motion\n", dys);
            failures++;
        }
        // Same argument as the walk: it was COMMANDED at 1 m/s.
        const float STRAFE_CMD = 1.0f * 30.0f * (float)dt;
        printf("[measure] strafe covered %.4f m (commanded 1.0 m/s = "
               "%.4f m)\n", dxs, STRAFE_CMD);
        if (dxs > 0.8f * STRAFE_CMD && dxs < 1.2f * STRAFE_CMD) {
            printf("[PASS] and it strafed at the speed it was told to, "
                   "%.0f%% of commanded\n", 100.0f * dxs / STRAFE_CMD);
        } else {
            printf("[FAIL] strafe distance %.4f is not the commanded %.4f\n",
                   dxs, STRAFE_CMD);
            failures++;
        }
    }

    // 9. Clean shutdown.
    humanoid.shutdown();
    dyn.shutdown();
    physics.shutdown();
    printf("[PASS] all subsystems shut down\n");

    if (failures > 0) {
        printf("\n[FAIL] %d failures\n", failures);
        return 1;
    }
    printf("\n[PASS] headless smoke complete\n");
    return 0;
}
