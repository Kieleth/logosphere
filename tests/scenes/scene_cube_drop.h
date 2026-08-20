// =============================================================================
// SCENE: one cube, dropped — the rotation ladder, divide and conquer
// =============================================================================
// Owner method (2026-08-19): "we first throw a cube to the floor and see
// if it flattens (it fell rotated), then we throw a rotating cube and see
// if it settles... then we check for momentum in that rotation... we
// extract G's in that specific test and we use it first to write the
// asserts of what physically we expect, and then we measure, and then we
// talk about solutions."
//
// The G's are GEDANKEN-35 (tilted cube must fall flat), 36 (spinning-top
// cube: spin may die only at the contact), 37 (angular momentum asserted
// per frame, so a constant leak and an impact loss cannot be confused).
// Every threshold below is taken from those records, not tuned here.
//
// Rungs, each one mechanism:
//   R0  control: flat drop, no spin  -> settles flat, invents NO rotation
//   R1  tilted 20 deg               -> must tip flat (contact torque)
//   R2  spinning about Z            -> spin survives FLIGHT, dies at face
//   R3  same run, per frame         -> flight retention 1.0 to noise
//
// The scene owns bodies, stepping, thresholds and latched measurements.
// Drivers own none of them (logosphere-tests skill, criterion b).
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>

namespace scene_cube_drop {

constexpr float CUBE       = 0.4f;
constexpr float DROP       = 0.3f;    // m of free fall (~0.247 s, ~15 frames)
constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 300;     // 5 s: land and fully settle

// --- thresholds, from the G's ---------------------------------------
constexpr float FLAT_ROT_MAX     = 0.05f;  // rad: settled means face-down (G-35)
constexpr float CONTROL_ROT_MAX  = 0.01f;  // rad: control invents nothing
constexpr float TIP_OMEGA_MIN    = 0.20f;  // rad/s: it must ROTATE to flatten (G-35)
constexpr float FLIGHT_KEEP_MIN  = 0.90f;  // spin at touchdown / spin at release (G-36)
constexpr float SETTLED_SPIN_MAX = 0.10f;  // rad/s: friction must brake it (G-36)
constexpr float FRAME_KEEP_MIN   = 0.99f;  // per flight frame (G-37)
constexpr float TILT_DEG         = 20.0f;  // < 45: nearest face is the original bottom
constexpr float SPIN0            = 3.0f;   // rad/s about Z
// The cube lands on a FLOOR SLAB PARTICLE, not on the bare turtle
// (owner, 2026-08-19: "I'd prefer if it falls on another particle and
// not some empty blank space"). This also aims the ladder at the right
// mechanism: a slab contact goes through BOX-BOX rows, which already
// compute real manifold contact points and throw them away — the row
// type the rotation campaign converts first. The turtle row has no
// contact point at all. Slab rests ON the turtle (bottom exactly at 0),
// so nothing is immovable by declaration (INV-1).
constexpr float FLOOR_TOP        = 0.2f;   // slab thickness; its top surface

struct RungSpec { const char* name; float tilt_rad; float spin_z; };
inline const RungSpec RUNGS[3] = {
    { "R0 control: flat, no spin",      0.0f,                          0.0f  },
    { "R1 tilted 20 deg, no spin",      TILT_DEG * 3.14159265f/180.f,  0.0f  },
    { "R2/R3 flat, spinning 3 rad/s",   0.0f,                          SPIN0 },
};

// Resting centre height ABOVE THE SLAB TOP of a cube tilted t about Y
// (edge contact), t in [0,45deg].
inline float rest_height(float t) {
    return (CUBE * 0.5f) * (std::cos(t) + std::sin(t));
}

struct Scene {
    logosphere::Argus argus;   // the witness: same values asserted and logged
    int cube = -1;
    // latched by step():
    float peak_omega_y = 0.0f;        // did it rotate while settling?
    float min_frame_keep = 1.0f;      // worst per-frame spin retention in flight
    float keep_at_touchdown = -1.0f;  // spin at first floor contact / SPIN0
    int   touchdown_frame = -1;
    float prev_omega_z = 0.0f;
    float rest_z = 0.0f;              // per-rung expected contact height

    int slab = -1;

    void build(ParticleSystem& ps) {
        {   // the floor: a visible body, held as a fixture
            Particle f{};
            f.shape = ParticleShape::BOX;
            f.width = 4.0f; f.height = 4.0f; f.thickness = FLOOR_TOP;
            f.size = 4.0f;
            f.x = 0.0f; f.y = 0.0f; f.z = FLOOR_TOP * 0.5f;  // bottom at z=0
            f.r = 0.30f; f.g = 0.33f; f.b = 0.42f; f.a = 1.0f;
            f.SetMaterial(Materials::Type::STONE);
            slab = ps.queue_particle_addition(f);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[slab].solver_mode = ParticleSolverMode::KINEMATIC;
            v[slab].owner = ParticleOwner::DYNAMICS;
            v[slab].is_at_rest = true;
        }
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.width = p.height = p.thickness = CUBE;
        p.size = CUBE;
        p.r = 0.9f; p.g = 0.6f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.z = 5.0f;   // parked; arm() places it
        cube = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        argus.watch(cube, "cube");
        argus.watch(slab, "slab");
    }

    void arm(ParticleSystem& ps, const RungSpec& r) {
        rest_z = FLOOR_TOP + rest_height(r.tilt_rad);
        auto v = ps.lock_particles_for_write();
        Particle& p = v[cube];
        p.x = 0.0f; p.y = 0.0f; p.z = rest_z + DROP;
        p.vx = p.vy = p.vz = 0.0f;
        p.rotation_x = p.rotation_z = 0.0f;
        p.rotation_y = r.tilt_rad;
        // One body, one orientation, also on a LIVE write: any writer
        // that sets the Euler triple keeps the quaternion coherent.
        // Spawn-time seeding covers newly created bodies; arm() edits an
        // existing one.
        p.rotation_q = logosphere::Quat::from_euler(0.0f, r.tilt_rad, 0.0f);
        p.omega_x = p.omega_y = 0.0f;
        p.omega_z = r.spin_z;
        p.is_at_rest = false;
        peak_omega_y = 0.0f; min_frame_keep = 1.0f;
        keep_at_touchdown = -1.0f; touchdown_frame = -1;
        prev_omega_z = r.spin_z;
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame, float spin0) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        auto v = ps.lock_particles_for_read();
        const Particle& p = v[cube];
        const float oy = std::fabs(p.omega_y);
        if (oy > peak_omega_y) peak_omega_y = oy;
        const bool airborne = touchdown_frame < 0;
        if (airborne) {
            // G-37: per-frame retention IS angular-momentum retention (I const).
            if (spin0 > 0.0f && std::fabs(prev_omega_z) > 0.05f) {
                const float keep = std::fabs(p.omega_z) / std::fabs(prev_omega_z);
                if (frame > 0 && keep < min_frame_keep) min_frame_keep = keep;
            }
            if (p.z <= rest_z + 0.005f) {
                touchdown_frame = frame;
                if (spin0 > 0.0f)
                    keep_at_touchdown = std::fabs(p.omega_z) / spin0;
            }
        }
        prev_omega_z = p.omega_z;
    }

    // Reference scenery for the WINDOW only: three dark pillars behind the
    // action (y = +1.5, the cube never leaves y = 0, so the 1.25 m gap
    // means no contact is possible) with tops at 0.2 / 0.4 / 0.6 m — a
    // height ruler the fall and the settle read against. The headless
    // driver never calls this, and the tested cube's physics is
    // untouched by construction, which is why it may live here without
    // breaking the drivers-own-no-bodies rule.
    void add_backdrop(ParticleSystem& ps) {
        const float tops[3] = { 0.2f, 0.4f, 0.6f };   // above the slab top
        for (int i = 0; i < 3; ++i) {
            Particle q{};
            q.shape = ParticleShape::BOX;
            q.width = 0.08f; q.height = 0.08f; q.thickness = tops[i];
            q.size = tops[i];
            q.x = -0.5f + 0.5f * i; q.y = 1.5f;
            q.z = FLOOR_TOP + tops[i] * 0.5f;   // standing on the slab
            q.r = 0.20f; q.g = 0.22f; q.b = 0.30f; q.a = 1.0f;
            q.SetMaterial(Materials::Type::STONE);
            int id = ps.queue_particle_addition(q);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    }

    float settled_rot_y(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[cube].rotation_y);
    }
    float settled_spin(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[cube].omega_z);
    }
    float settled_z(ParticleSystem& ps) const {
        return ps.lock_particles_for_read()[cube].z;
    }
};

}  // namespace scene_cube_drop
