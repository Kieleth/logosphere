// =============================================================================
// SCENE: two stations for INV-39 - a body moved from outside carries its motion
// =============================================================================
// THE PHYSICS. A KINEMATIC body is a body of infinite mass with PRESCRIBED
// motion. Its velocity is part of the world's state: a cube resting on a
// slab that slides is carried by friction (the needed acceleration is far
// inside the friction cone), and an arm nailed under a post that turns
// turns with it. Neither needs a force from the writer; both need the
// solver to KNOW the writer's motion when it prices relative velocity.
//
// THE QUESTION (G-77, G-78). The writer owns position and orientation and
// writes nothing else - Eden's fixtures, every FK bone. What does the
// solver read as that body's velocity? Today, read in the code and not
// yet measured: zero (no previous state is kept for a KINEMATIC body).
//
// STATION A, the moving platform (G-77): 60 frames still, 60 frames of
// ramp to 0.5 m/s at 0.5 m/s^2, 120 frames steady. Measured: SLIP =
// |dx_cube - dx_slab|, the cube's bottom against the slab's top, speeds.
// STATION B, the turning post (G-78): 60 frames still, then 0.5 rad/s
// about Z. Measured: the arm's spin against 0.5, its yaw against the
// post's, the centre separation a rigid nail keeps.
//
// The scene is written ONCE and both drivers run it. Neither driver holds
// a body, a force or a threshold. Argus is the witness: the asserts, the
// stdout log and the window readout read one source.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdlib>
#include <memory>

namespace scene_writers_motion {

constexpr float DT           = 1.0f / 60.0f;
constexpr int   RUN_FRAMES   = 240;          // 4 s
constexpr int   HOLD_FRAMES  = 60;           // both writers still
constexpr int   RAMP_FRAMES  = 60;           // the slab's gentle start
constexpr float SLAB_V       = 0.5f;         // m/s, steady
constexpr float SLAB_A       = SLAB_V / (RAMP_FRAMES * DT);   // 0.5 m/s^2 << mu g
constexpr float POST_OMEGA   = 0.5f;         // rad/s about Z, CW convention
constexpr float CUBE         = 0.4f;
constexpr float SLAB_LEN     = 4.0f, SLAB_WID = 2.0f, SLAB_THK = 0.4f;
constexpr float SLAB_Z       = 0.25f;        // bottom 0.05 m above the turtle
constexpr float POST         = 0.5f;
constexpr float POST_Z       = 1.2f;
constexpr float ARM_W        = 0.2f, ARM_L = 0.5f;
constexpr float STATION_Y    = 3.0f;         // A at -Y, B at +Y: they never meet
// Bars. SLOP is the engine's own 'geometric error, not motion' (INV-2);
// the law says slip and separation drift are geometric error.
constexpr float SLIP_MAX     = 10.0f * PhysicsV4::SLOP;    // m over the run
constexpr float SEAT_MAX     = 5.0f  * PhysicsV4::SLOP;    // bottom-to-top gap band
constexpr float SPIN_TOL     = 0.10f * POST_OMEGA;         // rad/s, steady phase
constexpr float YAW_ERR_MAX  = 0.05f;                       // rad
constexpr float SEP_DRIFT_MAX= 5.0f  * PhysicsV4::SLOP;    // m
constexpr float DIV_MAX_SHARP= 0.01f, DIV_MAX_FOLD = 0.015f; // G-21 two-band

struct Scene {
    logosphere::Argus argus;
    int slab = -1, cube = -1, post = -1, arm = -1;
    // the writers' own state (they own position; the solver does not)
    float slab_x = 0.0f, slab_x0 = 0.0f, slab_v = 0.0f;
    float post_yaw = 0.0f;                      // CW rotation_z convention
    float cube_x0 = 0.0f, sep0 = -1.0f;
    // latched by the witness
    float slip_max = 0.0f, seat_gap_min = 1e9f, seat_gap_max = -1e9f;
    float sep_drift_max = 0.0f, yaw_err_max = 0.0f;
    float spin_min_steady = 1e9f, spin_max_steady = 0.0f;
    float cube_speed_err_max = 0.0f;
    int   steady_frames = 0;

    static Particle box(float x, float y, float z, float w, float h, float t,
                        Materials::Type m, float r, float g, float b) {
        Particle p{};
        p.x = x; p.y = y; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = w; p.height = h; p.thickness = t; p.size = w;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(m);
        return p;
    }

    void build(ParticleSystem& ps, PhysicsSystem& physics) {
        // STATION A: the slab and its rider
        slab = ps.queue_particle_addition(box(0.0f, -STATION_Y, SLAB_Z, SLAB_LEN, SLAB_WID, SLAB_THK,
                                              Materials::Type::STONE, 0.35f, 0.38f, 0.45f));
        cube = ps.queue_particle_addition(box(0.0f, -STATION_Y, SLAB_Z + SLAB_THK * 0.5f + CUBE * 0.5f + 0.02f,
                                              CUBE, CUBE, CUBE, Materials::Type::STONE, 0.9f, 0.6f, 0.25f));
        // STATION B: the post and the arm hanging under its centre
        post = ps.queue_particle_addition(box(0.0f, +STATION_Y, POST_Z, POST, POST, POST,
                                              Materials::Type::STONE, 0.35f, 0.38f, 0.45f));
        arm  = ps.queue_particle_addition(box(0.0f, +STATION_Y, POST_Z - POST * 0.5f - ARM_L * 0.5f,
                                              ARM_W, ARM_W, ARM_L, Materials::Type::WOOD_HARD, 0.55f, 0.8f, 0.4f));
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            for (int id : {slab, post}) {        // fixtures: an external writer owns them (INV-1)
                v[id].solver_mode = ParticleSolverMode::KINEMATIC;
                v[id].owner = ParticleOwner::DYNAMICS;
                v[id].is_at_rest = true;
            }
            // The arm carries its anchor torque only as a quat-driven,
            // PHYSICS-owned body in today's world (G-39's gate, open).
            v[arm].is_quat_driven = true;
            v[arm].owner = ParticleOwner::PHYSICS;
            slab_x = slab_x0 = v[slab].x;
            cube_x0 = v[cube].x;
        }
        // The nail: rigid, on the post's axis, so the anchor never moves
        // and the only thing to transmit is the turn.
        auto nail = std::make_unique<NailGluon>();
        nail->offset_a = Vec3(0.0f, 0.0f, -POST * 0.5f);
        nail->offset_b = Vec3(0.0f, 0.0f, +ARM_L * 0.5f);
        nail->target_distance = 0.0f;
        nail->rotate_offsets = true;
        nail->breaking_force = 1.0e5f;             // a weld declares its strength
        nail->angular_stiffness = 200.0f;          // declared, as the bond door asks
        nail->angular_damping = 12.0f;
        physics.add_gluon_between((size_t)post, (size_t)arm, std::move(nail));
        argus.watch(slab, "slab"); argus.watch(cube, "cube");
        argus.watch(post, "post"); argus.watch(arm,  "arm");
    }

    // THE WRITERS. Position and orientation only, never velocity: the
    // question the scene asks is what the solver reads for a body it
    // does not move. The post's writer keeps BOTH orientation ledgers
    // (G-38: whoever writes orientation maintains both).
    void write(ParticleSystem& ps, int frame) {
        if (frame >= HOLD_FRAMES) {
            const int f = frame - HOLD_FRAMES;
            slab_v = (f < RAMP_FRAMES) ? SLAB_A * (f + 1) * DT : SLAB_V;
            slab_x += slab_v * DT;
            post_yaw += POST_OMEGA * DT;
        } else {
            slab_v = 0.0f;
        }
        auto v = ps.lock_particles_for_write();
        v[slab].x = slab_x;
        v[post].rotation_z = post_yaw;
        v[post].rotation_q = logosphere::Quat::from_euler(0.0f, 0.0f, post_yaw);
    }

    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        {
            auto v = ps.lock_particles_for_write();
            v[slab].x = slab_x0;
            v[post].rotation_z = 0.0f; v[post].rotation_q = logosphere::Quat::identity();
            auto reset = [&](int id, float x, float y, float z) {
                Particle& p = v[id]; p.x = x; p.y = y; p.z = z;
                p.vx = p.vy = p.vz = 0.0f; p.omega_x = p.omega_y = p.omega_z = 0.0f;
                p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
                p.rotation_q = logosphere::Quat::identity();
                p.is_at_rest = false; p.frames_at_rest = 0; p.low_velocity_frames = 0;
            };
            reset(cube, cube_x0, -STATION_Y, SLAB_Z + SLAB_THK * 0.5f + CUBE * 0.5f + 0.02f);
            reset(arm,  0.0f, +STATION_Y, POST_Z - POST * 0.5f - ARM_L * 0.5f);
        }
        physics.forget_body((size_t)cube); physics.forget_body((size_t)arm);
        slab_x = slab_x0; slab_v = 0.0f; post_yaw = 0.0f; sep0 = -1.0f;
        slip_max = 0.0f; seat_gap_min = 1e9f; seat_gap_max = -1e9f;
        sep_drift_max = 0.0f; yaw_err_max = 0.0f;
        spin_min_steady = 1e9f; spin_max_steady = 0.0f; cube_speed_err_max = 0.0f;
        steady_frames = 0;
        argus.reset_milestones(cube); argus.reset_milestones(arm);
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame) {
        write(ps, frame);
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        const auto* S = argus.latest(slab); const auto* C = argus.latest(cube);
        const auto* P = argus.latest(post); const auto* A = argus.latest(arm);
        if (!S || !C || !P || !A) return;
        // station A
        const float slip = std::fabs((C->x - cube_x0) - (S->x - slab_x0));
        if (slip > slip_max) slip_max = slip;
        const float gap = (C->z - CUBE * 0.5f) - (S->z + SLAB_THK * 0.5f);
        if (gap < seat_gap_min) seat_gap_min = gap;
        if (gap > seat_gap_max) seat_gap_max = gap;
        // station B
        const float sep = argus.separation(post, arm);
        if (sep0 < 0.0f) sep0 = sep;
        const float sd = std::fabs(sep - sep0);
        if (sd > sep_drift_max) sep_drift_max = sd;
        float ye = std::fabs(A->rz - P->rz);
        while (ye > 3.14159265f) ye = std::fabs(ye - 6.2831853f);
        if (ye > yaw_err_max) yaw_err_max = ye;
        if (frame >= HOLD_FRAMES + RAMP_FRAMES) {          // the steady phase
            ++steady_frames;
            const float sp = argus.spin(arm);
            if (sp < spin_min_steady) spin_min_steady = sp;
            if (sp > spin_max_steady) spin_max_steady = sp;
            const float cv = std::fabs(C->vx - SLAB_V);
            if (cv > cube_speed_err_max) cube_speed_err_max = cv;
        }
    }

    // --- the claims, one helper each, shared by both drivers ---------------
    static bool rides(float slip)                 { return slip < SLIP_MAX; }
    static bool seated(float gmin, float gmax)    { return gmin > -PhysicsV4::SLOP && gmax < SEAT_MAX; }
    static bool carried(float verr)               { return verr < SPIN_TOL * 0.0f + 0.05f * SLAB_V; }
    static bool turns_with(float smin, float smax){ return smin > POST_OMEGA - SPIN_TOL && smax < POST_OMEGA + SPIN_TOL; }
    static bool aligned(float yerr)               { return yerr < YAW_ERR_MAX; }
    static bool rigid(float sdrift)               { return sdrift < SEP_DRIFT_MAX; }
    static bool coherent(float sharp, float fold) { return sharp < DIV_MAX_SHARP && fold < DIV_MAX_FOLD; }
};

}  // namespace scene_writers_motion
