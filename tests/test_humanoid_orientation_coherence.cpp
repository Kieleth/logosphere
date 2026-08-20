// =============================================================================
// A WALKING HUMANOID'S BONES HAVE TWO LEDGERS TOO — the G-38 ladder
// =============================================================================
// The flip made every DYNAMIC body's orientation single-truth. KINEMATIC
// bodies were deliberately excluded: their orientation belongs to their
// external writer. G-38 states the surviving rule — WHOEVER WRITES
// ORIENTATION MAINTAINS BOTH LEDGERS — and this ladder holds the
// animation layer to it.
//
// The experiment: register a humanoid (its particles become KINEMATIC,
// animation-driven), walk it north, then command it EAST. The turn is
// the point: on this direct-registered rig the limbs swing by POSITION
// (IK writes positions, measured: a straight walk changes no bone's
// Euler rotation at all), and the orientation WRITER is the YAW
// CASCADE, which writes rotation_z onto every spine and limb particle
// as the body turns (humanoid_locomotion.cpp:3527ff — the drive_set
// writer G-22 named). If that writer maintains both ledgers, each
// bone's quaternion agrees with its Euler triple through the turn. If
// not, the divergence IS the turn: the stale quaternion still faces
// north while the body faces east.
//
// Why this matters beyond hygiene: gluon error terms and the oriented
// narrow phase read the quaternion for quat-driven bodies and compose it
// from Euler otherwise, selected by `is_quat_driven`. That flag can only
// die (owner ruling) once BOTH ledgers agree for every writer class —
// this ladder is the KINEMATIC half of that precondition.
//
// FULL-STATE NARRATION (assert-or-waive, per DOF):
//   position/velocity — the walk's own asserts live in
//     test_humanoid_headless (commanded speed, no lateral drift): WAIVED
//     here by name, not re-asserted.
//   orientation coherence — ASSERTED: max q-vs-Euler divergence across
//     every bone, every frame, < 0.01 rad.
//   turn activity — ASSERTED (the control): the bones' rotation_z
//     really changes during the turn, so a green cannot come from a rig
//     that never rotated anything. (A first draft controlled on
//     rotation_x during a straight walk and measured 0.0000: position-
//     swung limbs, no rotation writer engaged. The control caught it.)
//
//   ./build/test_humanoid_orientation_coherence
// =============================================================================

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
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== a walking humanoid's bones have two ledgers too ===\n");

    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg(registry);
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;
    logosphere::Argus argus;

    if (!physics.initialize(ps) || !dyn.initialize_headless(ps) ||
        !humanoid.initialize_headless(ps, physics, kg, dyn, tracer)) {
        std::printf("  [FAIL] headless init\n");
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

    // The ragdoll test's rig, standing on the ground so it can walk.
    const float HIPS_Z0 = 1.00f;   // legs reach z0-0.95; keep every bottom at or above the turtle
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

    std::vector<int> all = torso_ids;
    for (auto* v : { &left_leg, &right_leg, &left_arm, &right_arm })
        for (int id : *v) all.push_back(id);
    const char* names[] = { "hips","abdomen","chest","neck","head",
        "l_thigh","l_shin","l_foot","r_thigh","r_shin","r_foot",
        "l_uparm","l_forearm","l_hand","r_uparm","r_forearm","r_hand" };
    for (size_t i = 0; i < all.size(); ++i) argus.watch(all[i], names[i]);

    humanoid.set_target_velocity(hips_id, 0.0f, 1.5f);   // walk north
    // Look EAST while walking north: the yaw cascade is the orientation
    // writer under test, and it runs only toward a custom look target.
    humanoid.set_look_at_target(hips_id, 50.0f, 0.0f);

    const double dt = 1.0 / 60.0;
    float max_div = 0.0f;
    int worst_bone = -1, worst_frame = -1;
    float max_yaw_delta = 0.0f;            // the control: did anything turn?
    std::vector<float> rz0(all.size(), 0.0f);
    {
        auto v = ps.lock_particles_for_read();
        for (size_t i = 0; i < all.size(); ++i) rz0[i] = v[all[i]].rotation_z;
    }

    for (int f = 0; f < 120; ++f) {
        humanoid.update_pre_physics(dt);
        humanoid.update_post_physics(dt);
        argus.observe(ps, f);
        auto v = ps.lock_particles_for_read();
        for (size_t i = 0; i < all.size(); ++i) {
            const float d = argus.divergence(all[i]);
            if (d > max_div) { max_div = d; worst_bone = (int)i; worst_frame = f; }
            const float ed = std::fabs(v[all[i]].rotation_z - rz0[i]);
            if (ed > max_yaw_delta) max_yaw_delta = ed;
        }
    }

    std::printf("  [measure] max yaw written during the turn: %.4f rad\n",
                max_yaw_delta);
    std::printf("  [measure] worst q-vs-Euler divergence: %.4f rad on %s "
                "at frame %d\n", max_div,
                worst_bone >= 0 ? names[worst_bone] : "?", worst_frame);

    check(max_yaw_delta > 0.5f,
          "control: the yaw cascade really wrote the turn onto the bones");
    check(max_div < 0.01f,
          "G-38: every writer maintains both ledgers — each bone's "
          "quaternion agrees with its Euler triple, every frame");

    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ONE ORIENTATION, EVEN MID-STRIDE"
                              : "TWO LEDGERS ON A WALKING BODY (expected red "
                                "until the writer-site sync lands)",
                failures);
    return failures == 0 ? 0 : 1;
}
