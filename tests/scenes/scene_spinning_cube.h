// =============================================================================
// SCENE: one stone cube, spinning, with nothing touching it
// =============================================================================
// The scene is written ONCE and both drivers run it. Neither the headless
// driver nor the visual one may create a body, apply a force, or hold a
// threshold; if either does, they stop being reflections of each other
// and the thing the owner watched is not the thing CI asserted.
//
// THE TIMESTEP IS OWNED HERE, deliberately. `PHYSICS_TIMESTEP = 1/30`
// and `engine.update()` ACCUMULATES, so an engine-driven loop takes one
// physics step every OTHER frame at dt = 1/30 while a direct
// `physics.update(1/60)` takes one per call. ANGULAR_DRAG is applied per
// substep, so driving the two modes differently would make them measure
// different spin-down. Both call `step()` below.
//
// Includes nothing from the engine or rendering, so the headless driver
// links `logosphere_core` + physics and no window code exists in it.
//
// THE WITNESS. Argus watches the one body every frame. "Isolated" is a
// claim about the whole state, not about omega_z alone: if the cube
// drifted sideways, picked up spin on an axis nothing excited, met the
// turtle, or split its two orientation ledgers, the retention number
// would still read exactly the same and the experiment would be a
// different one. Each of those is now latched here and asserted by the
// driver.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>

namespace scene_spinning_cube {

constexpr float SPIN0     = 4.0f;    // rad/s, well under MAX_OMEGA
constexpr float CUBE      = 0.4f;    // m, edge
constexpr float START_Z   = 40.0f;   // m, clear of the turtle for a full run
constexpr float DT        = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 60;     // one second
// The claim is CONSERVATION. An isolated body has no external torque, so
// 5% loss would be as wrong as 95%: this tolerance is integrator noise,
// never an allowance for dissipation.
constexpr float KEEP_MIN  = 0.99f;
// The SAME claim at frame resolution. An end-to-end retention number
// cannot tell a constant leak from a single event; per frame, it can
// (G-37's lesson, borrowed from the cube-drop ladder). Torque-free means
// every frame conserves, not just the average.
constexpr float FRAME_KEEP_MIN = 0.99f;
// Nothing in this experiment pushes sideways or spins another axis up.
// Exactly zero is the honest expectation; these are float-noise bounds.
constexpr float LATERAL_MAX       = 1e-5f;   // m from the release column
constexpr float OFF_AXIS_SPIN_MAX = 1e-5f;   // rad/s on x and y
// One body, one orientation (G-23). Quaternion truth is the default.
constexpr float COHERENCE_MAX     = 0.01f;   // rad
// "Isolated" made checkable: the turtle is at z = 0 and nothing else
// exists, so the body must stay far clear of the only thing it could
// possibly meet for the whole run.
constexpr float CLEARANCE_MIN     = 20.0f;   // m above the turtle
constexpr float GRAVITY           = 9.81f;   // m/s^2, the analytic fall

struct Scene {
    logosphere::Argus argus;   // the witness: asserts and logs, one source
    int id = -1;
    // --- latched by step() from the witness ---------------------------
    float worst_frame_keep = 1.0f;   // min per-frame omega_z retention
    float max_lateral      = 0.0f;   // worst sqrt(x^2 + y^2)
    float max_off_axis     = 0.0f;   // worst sqrt(omega_x^2 + omega_y^2)
    float max_div          = 0.0f;   // worst q-vs-Euler divergence
    float min_z            = START_Z;
    float prev_spin        = SPIN0;

    // Place the one body. Nothing else exists in this world.
    void build(ParticleSystem& ps) {
        Particle p{};
        p.x = 0.0f; p.y = 0.0f; p.z = START_Z;
        p.shape = ParticleShape::BOX;
        p.width = p.height = p.thickness = CUBE;
        p.size = CUBE;
        p.r = 0.85f; p.g = 0.55f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        id = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        argus.watch(id, "cube");
        spin(ps);
    }

    // Re-arm: back to the start, spinning again. The visual driver calls
    // this so the same second can be watched more than once.
    void spin(ParticleSystem& ps) {
        auto v = ps.lock_particles_for_write();
        v[id].x = 0.0f; v[id].y = 0.0f; v[id].z = START_Z;
        v[id].vx = v[id].vy = v[id].vz = 0.0f;
        v[id].omega_x = v[id].omega_y = 0.0f;
        v[id].omega_z = SPIN0;
        v[id].is_at_rest = false;
        worst_frame_keep = 1.0f; max_lateral = 0.0f; max_off_axis = 0.0f;
        max_div = 0.0f; min_z = START_Z; prev_spin = SPIN0;
    }

    // ONE definition of a step, so both modes advance identically.
    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame = -1) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        const logosphere::Argus::State* s = argus.latest(id);
        if (!s) return;
        const float lat = std::sqrt(s->x * s->x + s->y * s->y);
        if (lat > max_lateral) max_lateral = lat;
        const float off = std::sqrt(s->ox * s->ox + s->oy * s->oy);
        if (off > max_off_axis) max_off_axis = off;
        const float d = argus.divergence(id);
        if (d > max_div) max_div = d;
        if (s->z < min_z) min_z = s->z;
        // G-37 at frame resolution: with I constant, spin retention IS
        // angular-momentum retention. A constant leak and a single event
        // are indistinguishable end to end and obvious here.
        if (std::fabs(prev_spin) > 0.05f) {
            const float keep = std::fabs(s->oz) / std::fabs(prev_spin);
            if (keep < worst_frame_keep) worst_frame_keep = keep;
        }
        prev_spin = s->oz;
    }

    // How far the fall departs from the analytic vacuum drop after n
    // frames. Positive means it fell SHORT of vacuum, which is what a
    // medium acting on linear velocity would do.
    float fall_shortfall(int frames) const {
        const logosphere::Argus::State* s = argus.latest(id);
        if (!s) return 0.0f;
        const float t = frames * DT;
        return s->z - (START_Z - 0.5f * GRAVITY * t * t);
    }

    float spin_now(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[id].omega_z);
    }
    float retained(ParticleSystem& ps) const { return spin_now(ps) / SPIN0; }
    void  position(ParticleSystem& ps, float& x, float& y, float& z) const {
        auto v = ps.lock_particles_for_read();
        x = v[id].x; y = v[id].y; z = v[id].z;
    }
    // The witness answers the rest of the state, so the driver's asserts
    // and its log read one source.
    float visible_turn(ParticleSystem&) const {
        const logosphere::Argus::State* s = argus.latest(id);
        return s ? std::fabs(s->rz) : 0.0f;
    }

    static bool passes(float retained_fraction) {
        return retained_fraction > KEEP_MIN;
    }
    static bool conserves_per_frame(float keep) { return keep > FRAME_KEEP_MIN; }
    static bool stayed_in_column(float lat)     { return lat < LATERAL_MAX; }
    static bool axis_pure(float off)            { return off < OFF_AXIS_SPIN_MAX; }
    static bool coherent(float div)             { return div < COHERENCE_MAX; }
    static bool untouched(float lowest_z)       { return lowest_z > CLEARANCE_MIN; }
};

}  // namespace scene_spinning_cube
