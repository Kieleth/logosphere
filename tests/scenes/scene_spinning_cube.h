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
// =============================================================================
#pragma once

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

struct Scene {
    int id = -1;

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
    }

    // ONE definition of a step, so both modes advance identically.
    void step(ParticleSystem& ps, PhysicsSystem& physics) {
        ps.update_bvh();
        physics.update(DT);
    }

    float spin_now(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[id].omega_z);
    }
    float retained(ParticleSystem& ps) const { return spin_now(ps) / SPIN0; }
    void  position(ParticleSystem& ps, float& x, float& y, float& z) const {
        auto v = ps.lock_particles_for_read();
        x = v[id].x; y = v[id].y; z = v[id].z;
    }
    static bool passes(float retained_fraction) {
        return retained_fraction > KEEP_MIN;
    }
};

}  // namespace scene_spinning_cube
