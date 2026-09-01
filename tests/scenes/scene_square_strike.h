// =============================================================================
// SCENE: THE SQUARE STRIKE — G-47's minimal instrument
// =============================================================================
// One striker, one braced identical box, dead centre, mirror-symmetric
// about the strike axis. The law (G-47, serving INV-32/INV-3): a
// symmetric impact produces ZERO net spin at ANY speed — angular
// momentum cannot come from iteration order. Speeds climb so the
// sequential-manifold seed's violence scaling is readable per rung:
// gentle contacts rebalance their seed, violent strikes carry it away.
// Airborne bodies: no floor rows muddy the accounting (the
// refused-momentum ledger's proven staging, minimized).
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>

namespace scene_square_strike {

constexpr float BODY       = 0.4f;
constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 90;
// GROUNDED staging. The first version was airborne "so no floor rows
// muddy the accounting" and gravity made it a lie twice over: a slow
// striker fell metres before arriving (the gentle rung never struck),
// and any fall makes the hit OFF-CENTRE vertically, where pitch is
// REAL physics, not artifact. On the floor, both boxes share height
// forever and the claim sharpens to the mirror symmetry that nothing
// physical can break: the y-plane. Floor friction and the strike are
// both y-symmetric, so ROLL (omega_x), YAW (omega_z) and SIDEWAYS
// velocity (vy) must be zero at any speed. PITCH (omega_y) is
// physical in grounded staging (floor friction acts below the COM,
// the nose dips) and is WAIVED BY NAME, not asserted.
constexpr float GAP        = 0.05f;    // face gap at launch
constexpr float SPIN_NOISE_MAX = 0.05f;   // rad/s, R0-wobble class
constexpr float LATERAL_MAX    = 0.05f;   // m/s
inline const float SPEEDS[3] = { 1.0f, 3.0f, 9.0f };
// The PITCHED rung (G-47 refined): the same y-symmetric strike with
// the striker pre-pitched about Y. Geometry stays mirror-perfect, but
// the manifold is now built from a ROTATED pose — where the ledger's
// tumbling striker measured 0.3353 rad/s of forbidden roll. Zero roll
// and yaw are still owed by symmetry; this rung is born red until the
// rotated-pose manifold holds the mirror.
constexpr float PITCH_POSE = 0.3f;   // rad about Y, y-mirror preserved

struct Scene {
    logosphere::Argus argus;
    int striker = -1, target = -1;

    void build(ParticleSystem& ps) {
        Particle t{};
        t.shape = ParticleShape::BOX;
        t.width = t.height = t.thickness = BODY;
        t.size = BODY;
        t.x = 0.0f; t.y = 0.0f; t.z = BODY * 0.5f;
        t.r = 0.35f; t.g = 0.75f; t.b = 0.95f; t.a = 1.0f;
        t.SetMaterial(Materials::Type::STONE);
        target = ps.queue_particle_addition(t);
        Particle s = t;
        s.x = -(BODY + GAP);
        s.r = 0.9f; s.g = 0.6f; s.b = 0.25f;
        striker = ps.queue_particle_addition(s);
        ps.flush_pending_particles();
        auto v = ps.lock_particles_for_write();
        v[target].solver_mode = ParticleSolverMode::KINEMATIC;  // braced
        v[target].owner = ParticleOwner::DYNAMICS;
        v[target].is_at_rest = true;
        argus.watch(striker, "striker");
        argus.watch(target, "target");
    }

    void arm(ParticleSystem& ps, PhysicsSystem& physics, float speed,
             float pitch_pose = 0.0f) {
        auto v = ps.lock_particles_for_write();
        Particle& s = v[striker];
        s.x = -(BODY + GAP); s.y = 0.0f; s.z = BODY * 0.5f;
        s.vx = speed; s.vy = s.vz = 0.0f;
        s.omega_x = s.omega_y = s.omega_z = 0.0f;
        s.rotation_x = s.rotation_z = 0.0f;
        s.rotation_y = pitch_pose;
        s.rotation_q = logosphere::Quat::from_euler(0.0f, pitch_pose, 0.0f);
        if (pitch_pose != 0.0f) {
            // pitched corner reaches lower: lift so the low corner
            // clears the turtle and the body settles into contact
            const float half = BODY * 0.5f;
            const float reach = half * (std::fabs(std::cos(pitch_pose)) +
                                        std::fabs(std::sin(pitch_pose)));
            s.z = reach;
        }
        s.is_at_rest = false;
        // teleport law (G-43): a re-armed body carries no history
        s.frames_at_rest = 0; s.low_velocity_frames = 0;
        s.quiet_growth_run = 0; s.rest_quiet_sq = 1e9f;
        physics.forget_body((size_t)striker);
        Particle& t = v[target];
        t.x = 0.0f; t.y = 0.0f; t.z = BODY * 0.5f;   // KINEMATIC: braced
        physics.forget_body((size_t)target);
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
    }

    // The mirror-forbidden components, separately: roll and yaw break
    // y-symmetry; pitch is grounded-staging physics and waived by name.
    float roll(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[striker].omega_x);
    }
    float yaw(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[striker].omega_z);
    }
    float pitch(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[striker].omega_y);
    }
    float lateral(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[striker].vy);
    }
};

}  // namespace scene_square_strike
