// =============================================================================
// SCENE: TORSION RUNGS — G-53 rebuilt from the irreducible case (KISS)
// =============================================================================
// Owner ruling 2026-08-27: "simpler in a 2 cube first, asserts in
// place, and then three cubes if needed." One rung, one purpose, one
// derivable expectation (derivations in the G-53 record):
//
// R1 THE IRREDUCIBLE CASE — one stone cube on the turtle, born
//    spinning 3 rad/s about the vertical. Turtle face friction brakes
//    it (~4.9 kN*m against I_z 312.5 -> dead in a fraction of a
//    second). Spin dies, nothing is created, the cube stands.
//
// R2 THE PASSENGER — the spinner on the turtle, a FREE cube resting
//    on it. Nothing anchors the passenger but its own inertia, so
//    being dragged is GUARANTEED: it must spin up same-sign (>= 0.5
//    rad/s from the coupling arithmetic) before everything dies.
//
// R3 THE CARRIER — the spinner ON TOP of a cube standing on the
//    turtle. The carrier is dragged through 24.5 kN of face but
//    anchored by 49 kN of turtle friction: the anchor WINS. Sub-unity
//    transmission is not guaranteed by physics, so the assert is the
//    UPPER bound: the carrier must NOT be dragged strongly.
//
// The four-cube sandwich (both directions at once, higher loads)
// retires until a rung needs it. Per-rung static heights localize the
// spinning-interface grind by pressure if it reproduces.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <vector>

namespace scene_torsion_rungs {

constexpr float BODY       = 1.0f;
constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 300;     // 5 s: spin, transmit, die, settle
constexpr int   N_RUNGS    = 3;
constexpr float OMEGA0     = 3.0f;    // rad/s about +Z at birth
constexpr float REST_TOL       = 0.02f;   // m (INV-4)
constexpr float SPIN_NOISE_MAX = 0.05f;   // rad/s at the end (INV-24 class)
constexpr float SPEED_MAX      = 10.0f;   // m/s (INV-3)
constexpr float LZ_BAND        = 1.05f;   // peak |L_z| <= initial * this
constexpr float PASSENGER_MIN  = 0.5f;    // R2: guaranteed drag, from below
constexpr float CARRIER_MAX    = 0.3f;    // R3: the anchor wins, from above
inline const char* RUNG_NAMES[N_RUNGS] = {
    "R1 THE IRREDUCIBLE CASE", "R2 THE PASSENGER", "R3 THE CARRIER" };
// Which body spins at birth, per rung (index into boxes).
inline const int SPINNER_OF[N_RUNGS] = { 0, 0, 1 };
// The witness body of the rung's transmission claim (-1 = none).
inline const int PARTNER_OF[N_RUNGS] = { -1, 1, 0 };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> boxes;
    int   rung     = 0;
    float x_offset = 0.0f;

    int n_boxes() const { return rung == 0 ? 1 : 2; }

    int build_rung(ParticleSystem& ps, int which, float x_off = 0.0f) {
        boxes.clear();
        rung = which;
        x_offset = x_off;
        const int n = n_boxes();
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = BODY;
            p.size = BODY;
            p.x = x_off; p.y = 0.0f;
            p.z = BODY * 0.5f + i * BODY;   // born touching, never overlapped
            p.omega_z = (i == SPINNER_OF[which]) ? OMEGA0 : 0.0f;
            p.r = (i == SPINNER_OF[which]) ? 0.95f : 0.55f;
            p.g = 0.6f; p.b = (i == SPINNER_OF[which]) ? 0.3f : 0.8f;
            p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            boxes.push_back(ps.queue_particle_addition(p));
        }
        ps.flush_pending_particles();
        char label[24];
        for (int i = 0; i < n; ++i) {
            std::snprintf(label, sizeof(label), "%s box%d",
                          i == SPINNER_OF[which] ? "spinner" : "partner", i);
            argus.watch(boxes[i], label);
        }
        return n;
    }

    // TELEPORT LAW: same resets as scene_ramp_race::rearm.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < boxes.size(); ++i) {
            Particle& p = parts[boxes[i]];
            p.x = x_offset; p.y = 0.0f;
            p.z = BODY * 0.5f + (float)i * BODY;
            p.vx = p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = 0.0f;
            p.omega_z = ((int)i == SPINNER_OF[rung]) ? OMEGA0 : 0.0f;
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
    float total_Lz(ParticleSystem& ps) const {
        auto parts = ps.lock_particles_for_read();
        float L = 0.0f;
        for (int id : boxes) {
            const Particle& p = parts[id];
            L += p.GetMomentOfInertia() * p.omega_z;
        }
        return L;
    }
    float static_z(int i) const { return BODY * 0.5f + i * BODY; }
};

}  // namespace scene_torsion_rungs
