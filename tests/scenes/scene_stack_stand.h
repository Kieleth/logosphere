// =============================================================================
// SCENE: THE STACK AND THE PILE — G-48's minimal instrument, plus controls
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
// Case 2, THE TORSION COLUMN (G-53): the same column, but the second
// cube is born spinning 3 rad/s about the vertical. The spin must DIE
// (face friction under load), the neighbours must show a same-sign
// torsion transient (friction is a two-way street), total L_z may only
// decay, and the column must stand through all of it.
//
// Case 3, MIXED MASSES (G-54): three unequal structures. M1 big-on-
// small centred (stands), M2 big-on-small OVERHUNG so statics
// certifies the FALL (a solver that keeps it aloft manufactures
// support), M3 heavy stone on a light wood column (stands, no
// detonation, INV-3 bounded).
//
// Owner order 2026-08-26: contrast cases after SPACE; "ok to be red if
// its informative."
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
constexpr int   RUN_FRAMES = 300;     // 5 s: settle, transmit, or collapse
constexpr int   N_CASES    = 4;
constexpr int   N_COLUMN   = 4;
constexpr int   N_PILE     = 5;
// bounds, from the laws: rest height within taller-than-slop but small
// (INV-4), spin at the R0 transient class, speeds bounded (INV-3)
constexpr float REST_TOL       = 0.02f;   // m per box
constexpr float SPIN_NOISE_MAX = 0.05f;   // rad/s after settling
constexpr float SPEED_MAX      = 10.0f;   // m/s, INV-3 sanity
// G-53, the torsion column (derivation in the registry record):
constexpr int   SPINNER_IDX    = 1;       // second cube from the bottom
constexpr float TORSION_OMEGA0 = 3.0f;    // rad/s about +Z at birth
constexpr float SPIN_DEAD_MAX  = 0.2f;    // the spinner must die below this
// Transmission thresholds are per-side, from the anchoring derivation
// in the G-53 record: the cube above is dragged 73.5 kN*mu against a
// 49 kN*mu anchor (super-unity, strong), the cube below is dragged the
// same against the turtle's 98 kN*mu anchor (sub-unity, weak - its
// witness is same-sign motion above the noise floor).
constexpr float TRANSMIT_MIN_ABOVE = 0.1f;
constexpr float TRANSMIT_MIN_BELOW = SPIN_NOISE_MAX;
constexpr float LZ_BAND        = 1.05f;   // peak |L_z| <= initial * this
// G-54, mixed masses:
constexpr float M_SMALL        = 0.7f;    // small cube edge
constexpr float M_BIG          = 1.4f;    // big cube edge (8:1 mass vs small)
constexpr float M2_OFFSET      = 0.45f;   // CoM 0.10 m outside the support edge
constexpr float M2_DEPART_Z    = 1.2f;    // big must end below this (static 1.4)
constexpr float SUB_SPACING    = 3.0f;    // M1 / M2 / M3 lateral spacing
inline const char* CASE_NAMES[N_CASES] = {
    "THE COLUMN", "THE PILE", "THE TORSION COLUMN", "MIXED MASSES" };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> boxes;
    int   which_case = 0;
    float x_offset   = 0.0f;

    struct Birth {
        float x, y, z;
        float size;
        Materials::Type material;
        float omega_z0;
        float expected_z;      // NAN = expected to DEPART (G-54 M2 big)
    };
    std::vector<Birth> birth;

    void plan_case(int which, float x_off) {
        birth.clear();
        which_case = which;
        x_offset = x_off;
        auto add = [&](float x, float y, float z, float size,
                       Materials::Type mat, float w0, float ez) {
            birth.push_back({x, y, z, size, mat, w0, ez});
        };
        if (which == 0 || which == 2) {
            for (int i = 0; i < N_COLUMN; ++i)
                add(x_off, 0.0f, BODY * 0.5f + i * BODY, BODY,
                    Materials::Type::STONE,
                    (which == 2 && i == SPINNER_IDX) ? TORSION_OMEGA0 : 0.0f,
                    BODY * 0.5f + i * BODY);
        } else if (which == 1) {
            for (int i = 0; i < N_PILE; ++i)
                add(x_off + ((i % 2) ? 0.18f : -0.18f),
                    (i % 3) ? 0.12f : -0.12f,
                    BODY * 0.5f + i * BODY, BODY,
                    Materials::Type::STONE, 0.0f,
                    BODY * 0.5f + i * BODY);
        } else {
            // M1: centred big-on-small — stands.
            add(x_off - SUB_SPACING, 0.0f, M_SMALL * 0.5f, M_SMALL,
                Materials::Type::STONE, 0.0f, M_SMALL * 0.5f);
            add(x_off - SUB_SPACING, 0.0f, M_SMALL + M_BIG * 0.5f, M_BIG,
                Materials::Type::STONE, 0.0f, M_SMALL + M_BIG * 0.5f);
            // M2: overhung big-on-small — statics certifies the FALL.
            add(x_off, 0.0f, M_SMALL * 0.5f, M_SMALL,
                Materials::Type::STONE, 0.0f, NAN);   // marginal: measured, waived
            add(x_off + M2_OFFSET, 0.0f, M_SMALL + M_BIG * 0.5f, M_BIG,
                Materials::Type::STONE, 0.0f, NAN);   // expected to DEPART
            // M3: heavy stone on a light wood column — stands, bounded.
            add(x_off + SUB_SPACING, 0.0f, 0.5f, BODY,
                Materials::Type::WOOD_SOFT, 0.0f, 0.5f);
            add(x_off + SUB_SPACING, 0.0f, 1.5f, BODY,
                Materials::Type::WOOD_SOFT, 0.0f, 1.5f);
            add(x_off + SUB_SPACING, 0.0f, 2.5f, BODY,
                Materials::Type::STONE, 0.0f, 2.5f);
        }
    }

    int build_case(ParticleSystem& ps, int which, float x_off = 0.0f) {
        boxes.clear();
        plan_case(which, x_off);
        for (size_t i = 0; i < birth.size(); ++i) {
            const Birth& b = birth[i];
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = b.size;
            p.size = b.size;
            p.x = b.x; p.y = b.y; p.z = b.z;   // born touching, never overlapped
            p.omega_z = b.omega_z0;
            p.r = 0.85f - 0.1f * (float)i; p.g = 0.55f;
            p.b = 0.3f + 0.1f * (float)i; p.a = 1.0f;
            p.SetMaterial(b.material);
            boxes.push_back(ps.queue_particle_addition(p));
        }
        ps.flush_pending_particles();
        char label[16];
        for (size_t i = 0; i < birth.size(); ++i) {
            std::snprintf(label, sizeof(label), "box%zu", i);
            argus.watch(boxes[i], label);
        }
        return (int)birth.size();
    }

    // TELEPORT LAW (SPACE replays the experiment): a repositioned body
    // voids its history — cached impulses forgotten, rest state reset,
    // Argus milestones re-armed. Same resets as scene_ramp_race::rearm.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < boxes.size(); ++i) {
            const Birth& b = birth[i];
            Particle& p = parts[boxes[i]];
            p.x = b.x; p.y = b.y; p.z = b.z;
            p.vx = p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = 0.0f;
            p.omega_z = b.omega_z0;
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
    float box_omega_z(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[boxes[i]].omega_z;
    }
    // Total vertical angular momentum of the case's cast. Contacts may
    // transfer it and the turtle may drain it; nothing may create it.
    float total_Lz(ParticleSystem& ps) const {
        auto parts = ps.lock_particles_for_read();
        float L = 0.0f;
        for (int id : boxes) {
            const Particle& p = parts[id];
            L += p.GetMomentOfInertia() * p.omega_z;
        }
        return L;
    }
    float static_z(int i) const { return birth[i].expected_z; }
    bool  expects_static(int i) const { return !std::isnan(birth[i].expected_z); }
};

}  // namespace scene_stack_stand
