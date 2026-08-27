// =============================================================================
// SCENE: THE STACK AND THE PILE — G-48's minimal instrument
// =============================================================================
// Case 0, THE COLUMN: four stone cubes born touching in a perfect
// column on the turtle. No torque exists anywhere in the symmetric
// statics; every interface carries exactly the weight above it.
// Case 1, THE PILE: five cubes in a zigzag whose every centre of mass
// sits well inside the box below (0.18/0.12 against a 0.5 half-extent).
// A stonemason certifies both. The law (G-48, serving INV-32/INV-4/
// INV-24): both structures STAND, boxes at their static heights, spin
// at solver-noise. Kill-switch world holds both at 0.0007 peak pen;
// the WARM_LEARN world (G-52) holds both to sub-mm.
//
// Single purpose (owner KISS ruling 2026-08-27): torsion lives in
// scene_torsion_rungs.h, mixed masses in scene_mixed_mass.h.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <vector>

namespace scene_stack_stand {

constexpr float BODY       = 1.0f;
constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 300;     // 5 s: settle or collapse, visibly
constexpr int   N_CASES    = 2;
constexpr int   N_COLUMN   = 4;
constexpr int   N_PILE     = 5;
// bounds, from the laws: rest height within taller-than-slop but small
// (INV-4), spin at the R0 transient class, speeds bounded (INV-3)
constexpr float REST_TOL       = 0.02f;   // m per box
constexpr float SPIN_NOISE_MAX = 0.05f;   // rad/s after settling
constexpr float SPEED_MAX      = 10.0f;   // m/s, INV-3 sanity
inline const char* CASE_NAMES[N_CASES] = { "THE COLUMN", "THE PILE" };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> boxes;
    int   which_case = 0;
    float x_offset   = 0.0f;   // the visual places both cases in one world

    static float birth_x(int which, int i, float x_off) {
        if (which == 0) return x_off;
        return x_off + ((i % 2) ? 0.18f : -0.18f);
    }
    static float birth_y(int which, int i) {
        if (which == 0) return 0.0f;
        return (i % 3) ? 0.12f : -0.12f;
    }

    int build_case(ParticleSystem& ps, int which, float x_off = 0.0f) {
        boxes.clear();
        which_case = which;
        x_offset = x_off;
        const int n = which == 0 ? N_COLUMN : N_PILE;
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = BODY;
            p.size = BODY;
            p.x = birth_x(which, i, x_off);
            p.y = birth_y(which, i);
            p.z = BODY * 0.5f + i * BODY;   // born touching, never overlapped
            p.r = 0.85f - 0.12f * i; p.g = 0.55f; p.b = 0.3f + 0.12f * i;
            p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            boxes.push_back(ps.queue_particle_addition(p));
        }
        ps.flush_pending_particles();
        char label[16];
        for (int i = 0; i < n; ++i) {
            std::snprintf(label, sizeof(label), "box%d", i);
            argus.watch(boxes[i], label);
        }
        return n;
    }

    // TELEPORT LAW (SPACE replays the experiment): a repositioned body
    // voids its history — cached impulses forgotten, rest state reset,
    // Argus milestones re-armed. Same resets as scene_ramp_race::rearm.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < boxes.size(); ++i) {
            Particle& p = parts[boxes[i]];
            p.x = birth_x(which_case, (int)i, x_offset);
            p.y = birth_y(which_case, (int)i);
            p.z = BODY * 0.5f + (float)i * BODY;
            p.vx = p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = p.omega_z = 0.0f;
            p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
            p.rotation_q = logosphere::Quat::identity();
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.low_velocity_frames = 0;
            p.quiet_growth_run = 0;
            p.rest_quiet_sq = 1e9f;
            physics.forget_body((size_t)boxes[i]);
            argus.reset_milestones(boxes[i]);
        }
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
    }

    float box_z(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[boxes[i]].z;
    }
    float box_spin(ParticleSystem& ps, int i) const {
        const Particle& p = ps.lock_particles_for_read()[boxes[i]];
        return std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y
                       + p.omega_z*p.omega_z);
    }
    float static_z(int i) const { return BODY * 0.5f + i * BODY; }
};

}  // namespace scene_stack_stand
