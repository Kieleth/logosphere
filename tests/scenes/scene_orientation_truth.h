// =============================================================================
// SCENE: twin spinning cubes — one engine, two orientation truths
// =============================================================================
// The instruments the quaternion-truth unification (owner ruling, R7
// dissolution) is measured by, BEFORE any engine line moves:
//
//   O0  G-20: the compass round trip. from_euler -> to_euler_zyx -> back
//       must be identity away from the gimbal band, or every consumer of
//       direction goes quietly wrong when the representation unifies.
//   O1  G-23's observable form: TWIN cubes, identical spin about Y, one
//       is_quat_driven, one Euler-truth. Each body's rotation_q and its
//       Euler triple must describe THE SAME orientation. Today the
//       Euler-truth twin's quaternion integrates while its Euler triple
//       never moves: one body, two orientations, and the one the
//       renderer and narrow phase read is the frozen one.
//   O2  G-19: the bit-identical baseline for the unification lever.
//       Pending until LOGOSPHERE_QUAT_TRUTH exists; the harness slot is
//       here so the lever lands into a waiting instrument.
//
// Scene owns bodies, stepping, thresholds. Drivers own none of them.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"
#include "math/quat.h"

#include <cmath>

namespace scene_orientation_truth {

constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 60;
constexpr float SPIN_Y     = 4.0f;    // rad/s about Y, airborne, no contact
constexpr float COHERENCE_MAX = 0.01f; // rad: q and Euler describe one orientation
constexpr float ROUNDTRIP_TOL = 2e-3f; // basis-vector error, float precision

// Angle between the orientations two representations describe.
inline float divergence_rad(const Particle& p) {
    logosphere::Quat qe = logosphere::Quat::from_euler(
        p.rotation_x, p.rotation_y, p.rotation_z);
    logosphere::Quat r = p.rotation_q * qe.conjugate();
    float w = std::fabs(r.w); if (w > 1.0f) w = 1.0f;
    return 2.0f * std::acos(w);
}

// G-20: sweep the round trip over the safe range. Returns worst basis error.
inline float roundtrip_sweep() {
    float worst = 0.0f;
    for (float x = -3.0f; x <= 3.01f; x += 0.5f)
        for (float y = -1.4f; y <= 1.41f; y += 0.2f)     // |sin y| < 0.9999
            for (float z = -3.0f; z <= 3.01f; z += 0.5f) {
                logosphere::Quat q = logosphere::Quat::from_euler(x, y, z);
                float ex, ey, ez;
                q.to_euler_zyx(ex, ey, ez);
                logosphere::Quat q2 = logosphere::Quat::from_euler(ex, ey, ez);
                float m1[9], m2[9];
                q.to_matrix3x3(m1); q2.to_matrix3x3(m2);
                for (int i = 0; i < 9; ++i) {
                    const float e = std::fabs(m1[i] - m2[i]);
                    if (e > worst) worst = e;
                }
            }
    return worst;
}

struct Scene {
    logosphere::Argus argus;   // the witness: asserts and logs, one source
    int quat_twin = -1;    // is_quat_driven = true, owner PHYSICS
    int euler_twin = -1;   // the engine default

    void build(ParticleSystem& ps) {
        auto make = [&](float y) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = 0.4f;
            p.size = 0.4f;
            p.x = 0.0f; p.y = y; p.z = 40.0f;    // airborne for the whole run
            p.r = 0.85f; p.g = 0.55f; p.b = 0.25f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = ps.queue_particle_addition(p);
            ps.flush_pending_particles();
            return id;
        };
        quat_twin = make(-0.6f);
        euler_twin = make(+0.6f);
        argus.watch(quat_twin, "quat_twin");
        argus.watch(euler_twin, "euler_twin");
        auto v = ps.lock_particles_for_write();
        v[quat_twin].is_quat_driven = true;
        v[quat_twin].owner = ParticleOwner::PHYSICS;
        for (int id : { quat_twin, euler_twin }) {
            v[id].omega_y = SPIN_Y;
            v[id].is_at_rest = false;
        }
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame = -1) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
    }

    // Both queries now answer FROM THE WITNESS, so the assert, the log
    // and the on-screen readout are one source (the Argus discipline).
    float divergence(ParticleSystem&, int id) const {
        return argus.divergence(id);
    }
    // What the renderer/narrow phase actually read: the Euler triple.
    float visible_rot_y(ParticleSystem&, int id) const {
        const logosphere::Argus::State* s = argus.latest(id);
        return s ? s->ry : 0.0f;
    }
};

// G-19's subject: one cube that NEVER rotates, dropped. Under the lever
// its publish converts identity to zero; the whole trajectory must be
// bit-identical with the lever on and off.
struct BaselineScene {
    int cube = -1;
    void build(ParticleSystem& ps) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.width = p.height = p.thickness = 0.4f;
        p.size = 0.4f;
        p.x = 0.0f; p.y = 0.0f; p.z = 40.0f;
        p.SetMaterial(Materials::Type::STONE);
        cube = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        auto v = ps.lock_particles_for_write();
        v[cube].is_at_rest = false;
    }
    // FNV-1a over the full kinematic + orientation state, bitwise.
    void hash_state(ParticleSystem& ps, unsigned long long& h) const {
        auto v = ps.lock_particles_for_read();
        const Particle& p = v[cube];
        const float f[10] = { p.x, p.y, p.z, p.rotation_x, p.rotation_y,
                              p.rotation_z, p.rotation_q.w, p.rotation_q.x,
                              p.rotation_q.y, p.rotation_q.z };
        const unsigned char* b = reinterpret_cast<const unsigned char*>(f);
        for (unsigned i = 0; i < sizeof(f); ++i) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
};

}  // namespace scene_orientation_truth
