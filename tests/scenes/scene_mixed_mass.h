// =============================================================================
// SCENE: MIXED MASSES — G-54's single purpose: three verdicts
// =============================================================================
// Three unequal structures, side by side (owner order 2026-08-26,
// split to its own test by the KISS ruling 2026-08-27):
//   M1  a 1.4 m stone cube (6860 kg) centred on a 0.7 m stone cube
//       (857 kg): centre of mass inside the support — STANDS.
//   M2  the same pair, big cube offset +0.45 m: its CoM sits 0.10 m
//       OUTSIDE the small cube's support edge — statics certifies the
//       FALL. A solver that keeps it aloft manufactures support.
//       The small perch's own fate is measured and WAIVED (the
//       departing load passes over its base edge, a marginal case
//       whose statics we have not done).
//   M3  a 1 m stone cube (2500 kg) on two 1 m soft-wood cubes
//       (500 kg each): 5:1 at the top interface — STANDS, bounded.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <vector>

namespace scene_mixed_mass {

constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 300;
constexpr float REST_TOL       = 0.02f;   // m (INV-4)
constexpr float SPEED_MAX      = 10.0f;   // m/s (INV-3)
constexpr float M_SMALL        = 0.7f;
constexpr float M_BIG          = 1.4f;    // 8:1 mass vs the small cube
constexpr float M2_OFFSET      = 0.45f;   // CoM 0.10 m outside the edge
constexpr float M2_DEPART_Z    = 1.2f;    // big must end below this (static 1.4)
constexpr float SUB_SPACING    = 3.0f;
// box indices in build order:
constexpr int M1_SMALL = 0, M1_BIG = 1, M2_SMALL = 2, M2_BIG = 3;
constexpr int M3_WOOD0 = 4, M3_WOOD1 = 5, M3_STONE = 6;

struct Scene {
    logosphere::Argus argus;
    std::vector<int> boxes;
    float x_offset = 0.0f;

    struct Birth {
        float x, z, size;
        Materials::Type material;
        float expected_z;      // NAN = measured and waived (M2 pair)
    };
    std::vector<Birth> birth;

    int build(ParticleSystem& ps, float x_off = 0.0f) {
        boxes.clear();
        birth.clear();
        x_offset = x_off;
        auto add = [&](float x, float z, float size, Materials::Type mat,
                       float ez) { birth.push_back({x, z, size, mat, ez}); };
        add(x_off - SUB_SPACING, M_SMALL * 0.5f, M_SMALL,
            Materials::Type::STONE, M_SMALL * 0.5f);
        add(x_off - SUB_SPACING, M_SMALL + M_BIG * 0.5f, M_BIG,
            Materials::Type::STONE, M_SMALL + M_BIG * 0.5f);
        add(x_off, M_SMALL * 0.5f, M_SMALL, Materials::Type::STONE, NAN);
        add(x_off + M2_OFFSET, M_SMALL + M_BIG * 0.5f, M_BIG,
            Materials::Type::STONE, NAN);   // expected to DEPART
        add(x_off + SUB_SPACING, 0.5f, 1.0f, Materials::Type::WOOD_SOFT, 0.5f);
        add(x_off + SUB_SPACING, 1.5f, 1.0f, Materials::Type::WOOD_SOFT, 1.5f);
        add(x_off + SUB_SPACING, 2.5f, 1.0f, Materials::Type::STONE, 2.5f);
        for (size_t i = 0; i < birth.size(); ++i) {
            const Birth& b = birth[i];
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = b.size;
            p.size = b.size;
            p.x = b.x; p.y = 0.0f;
            p.z = b.z;                     // born touching, never overlapped
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

    // TELEPORT LAW: same resets as scene_ramp_race::rearm.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < boxes.size(); ++i) {
            const Birth& b = birth[i];
            Particle& p = parts[boxes[i]];
            p.x = b.x; p.y = 0.0f; p.z = b.z;
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
    float static_z(int i) const { return birth[i].expected_z; }
    bool  expects_static(int i) const { return !std::isnan(birth[i].expected_z); }
};

}  // namespace scene_mixed_mass
