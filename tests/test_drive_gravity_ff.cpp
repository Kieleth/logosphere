// =============================================================================
// DRIVE GRAVITY FEED-FORWARD LADDER (task #41)
// =============================================================================
// A quat-drive row is a proportional controller, so under sustained gravity
// torque it holds a standing angle error proportional to the load: both
// humanoid drive tests fail at exactly the loaded joint (shoulder) while
// every distal joint passes. Two cures measured WORSE and are kept as
// evidence commits: the angular integral (pumps the joint's own swing) and
// a naive feed-forward whose subtree sweep climbed into the torso because
// drive-gluon a/b order is not a parent->child convention.
//
// This ladder is the empirical SIGN PROOF the third attempt must pass
// before it touches a humanoid. Analytic scene, no rig:
//
//   RUNG 1 - pendulum: KINEMATIC post, one driven arm born horizontal,
//     drive target = hold the born pose. Gravity torque about the joint is
//     m*g*r, known to the digit. Without compensation the arm sags to the
//     P-equilibrium; with correct compensation it holds; with the WRONG
//     SIGN it dives past the baseline. The rung measures COM drop.
//
//   RUNG 2 - two-joint chain: post -> seg1 -> seg2, born horizontal. The
//     inner joint holds BOTH segments' weight, the outer holds one. Tests
//     the grounded-side component split through a chain.
//
// Run: ./build-release/logosphere-tests --test test_drive_gravity_ff --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <memory>

namespace {

// A driven arm segment hanging off `parent_id` at world anchor (ax,ay,az),
// extending +X. Returns the arm particle id.
int attach_arm(Engine& engine, int parent_id, float ax, float ay, float az,
               float arm_len, float arm_thick, Materials::Type mat) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    Particle arm = {};
    arm.shape = ParticleShape::BOX;
    arm.x = ax + arm_len * 0.5f; arm.y = ay; arm.z = az;
    arm.width = arm_len; arm.height = arm_thick; arm.thickness = arm_thick;
    arm.r = 0.6f; arm.g = 0.5f; arm.b = 0.3f; arm.a = 1.0f;
    arm.SetMaterial(mat);
    arm.is_at_rest = false;
    arm.is_quat_driven = true;
    arm.rotation_q = logosphere::Quat::from_euler(0.0f, 0.0f, 0.0f);
    const int arm_id = engine.add_particle(arm);
    ps.flush_pending_particles();

    float pax, pay, paz, par_rx, par_ry, par_rz;
    {
        auto v = ps.lock_particles_for_write();
        pax = v[parent_id].x; pay = v[parent_id].y; paz = v[parent_id].z;
        par_rx = v[parent_id].rotation_x;
        par_ry = v[parent_id].rotation_y;
        par_rz = v[parent_id].rotation_z;
    }

    auto g = std::make_unique<NailGluon>();
    g->offset_a = Vec3(ax - pax, ay - pay, az - paz);   // anchor on parent
    g->offset_b = Vec3(-arm_len * 0.5f, 0.0f, 0.0f);    // arm inner end
    g->target_distance = 0.0f;
    g->breaking_force = 1e9f;
    g->rotate_offsets = true;
    g->angular_stiffness = 200.0f;
    g->angular_damping = 12.0f;
    g->max_relative_rotation = 3.14f;
    g->angular_drive_enabled = true;
    g->use_quat_target = true;
    // Born at target: hold the pose the arm was placed in.
    logosphere::Quat qa = logosphere::Quat::from_euler(par_rx, par_ry, par_rz);
    logosphere::Quat qb = logosphere::Quat::from_euler(0.0f, 0.0f, 0.0f);
    g->target_relative_q = qb * qa.conjugate();
    physics.add_gluon_between(parent_id, arm_id, std::move(g));
    return arm_id;
}

float com_z_of(Engine& engine, int id) {
    auto v = engine.get_particle_system().lock_particles_for_write();
    return v[id].z;
}

} // namespace

bool test_drive_gravity_ff() {
    printf("\n=== DRIVE GRAVITY FEED-FORWARD LADDER (task #41) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  init failed\n  FAIL\n"); return false; }
    auto& ps = engine.get_particle_system();

    // ---- RUNG 1: pendulum ---------------------------------------------------
    Particle post = {};
    post.shape = ParticleShape::BOX;
    post.x = 0.0f; post.y = 0.0f; post.z = 2.0f;
    post.width = 0.2f; post.height = 0.2f; post.thickness = 1.0f;
    post.r = 0.4f; post.g = 0.4f; post.b = 0.5f; post.a = 1.0f;
    post.SetMaterial(Materials::Type::STONE);
    post.solver_mode = ParticleSolverMode::KINEMATIC;
    post.is_at_rest = true;
    const int post_id = engine.add_particle(post);
    ps.flush_pending_particles();

    const float ARM_LEN = 0.6f;
    const int arm_id = attach_arm(engine, post_id, 0.1f, 0.0f, 2.4f,
                                  ARM_LEN, 0.08f, Materials::Type::WOOD_HARD);

    const float z0 = com_z_of(engine, arm_id);
    for (int f = 0; f < 180; ++f) engine.update(1.0 / 60.0);
    const float sag1 = z0 - com_z_of(engine, arm_id);

    // The arm COM sits 0.4 m from the joint; a sag of half the arm width
    // (0.04 m) is ~5.7 deg of standing error. The drive must do better.
    const float SAG_LIMIT = 0.04f;
    const bool r1 = std::fabs(sag1) <= SAG_LIMIT;
    printf("  RUNG 1  pendulum COM sag %+8.4f m  (|limit| %.3f)   %s\n",
           sag1, SAG_LIMIT, r1 ? "ok" : "*** RED ***");

    // ---- RUNG 2: two-joint chain -------------------------------------------
    Particle post2 = post;
    post2.x = 5.0f;
    const int post2_id = engine.add_particle(post2);
    ps.flush_pending_particles();

    const int seg1_id = attach_arm(engine, post2_id, 5.1f, 0.0f, 2.4f,
                                   ARM_LEN, 0.08f, Materials::Type::WOOD_HARD);
    const int seg2_id = attach_arm(engine, seg1_id, 5.1f + ARM_LEN, 0.0f, 2.4f,
                                   ARM_LEN, 0.07f, Materials::Type::WOOD_HARD);

    const float s1z0 = com_z_of(engine, seg1_id);
    const float s2z0 = com_z_of(engine, seg2_id);
    for (int f = 0; f < 180; ++f) engine.update(1.0 / 60.0);
    const float sag_s1 = s1z0 - com_z_of(engine, seg1_id);
    const float sag_s2 = s2z0 - com_z_of(engine, seg2_id);

    // The tip segment compounds both joints' errors; give it double.
    const bool r2a = std::fabs(sag_s1) <= SAG_LIMIT;
    const bool r2b = std::fabs(sag_s2) <= 2.0f * SAG_LIMIT;
    printf("  RUNG 2  seg1 COM sag     %+8.4f m  (|limit| %.3f)   %s\n",
           sag_s1, SAG_LIMIT, r2a ? "ok" : "*** RED ***");
    printf("  RUNG 2  seg2 COM sag     %+8.4f m  (|limit| %.3f)   %s\n",
           sag_s2, 2.0f * SAG_LIMIT, r2b ? "ok" : "*** RED ***");

    engine.shutdown();
    const bool pass = r1 && r2a && r2b;
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL (a driven joint must hold its pose under its own load)");
    return pass;
}
