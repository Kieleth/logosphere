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
// the default-torque world sinks the column 9.5 cm and collapses the
// pile — the wobble seed pumps through stacked contacts instead of
// decaying. Born red until the sustained-contact loop settles.
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
constexpr int   N_COLUMN   = 4;
constexpr int   N_PILE     = 5;
// bounds, from the laws: rest height within taller-than-slop but small
// (INV-4), spin at the R0 transient class, speeds bounded (INV-3)
constexpr float REST_TOL       = 0.02f;   // m per box
constexpr float SPIN_NOISE_MAX = 0.05f;   // rad/s after settling
constexpr float SPEED_MAX      = 10.0f;   // m/s, INV-3 sanity
inline const char* CASE_NAMES[2] = { "THE COLUMN", "THE PILE" };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> boxes;

    int build_case(ParticleSystem& ps, int which) {
        boxes.clear();
        const int n = which == 0 ? N_COLUMN : N_PILE;
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = BODY;
            p.size = BODY;
            if (which == 0) { p.x = 0.0f; p.y = 0.0f; }
            else {
                p.x = (i % 2) ? 0.18f : -0.18f;
                p.y = (i % 3) ? 0.12f : -0.12f;
            }
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
