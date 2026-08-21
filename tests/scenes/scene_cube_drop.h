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
constexpr float DROP       = 0.6f;    // m of free fall (~0.35 s, ~21 frames)
                                      // (owner 2026-08-20: "fall from
                                      // higher in general" — honest now
                                      // that drag is the derived law and
                                      // flight barely taxes spin)
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
// LEVER-MODE CONTRACT (CONTACT_TORQUE=1), per the 2026-08-20 decree:
// the torque slices' claims are enforced here, in the mode they are
// made for, or they are not claims. Measured values behind each bound:
constexpr float TIP_SETTLE_MAX   = 0.05f;  // rad: R1 settles FLAT (measured 0.0001)
constexpr float WOBBLE_MAX_LEVER = 0.05f;  // rad/s: R0's transient (measured 0.0198,
                                           // was 0.1929 before INV-20 pricing)
// The cube lands on a FLOOR SLAB PARTICLE, not on the bare turtle
// (owner, 2026-08-19: "I'd prefer if it falls on another particle and
// not some empty blank space"). This also aims the ladder at the right
// mechanism: a slab contact goes through BOX-BOX rows, which already
// compute real manifold contact points and throw them away — the row
// type the rotation campaign converts first. The turtle row has no
// contact point at all. Slab rests ON the turtle (bottom exactly at 0),
// so nothing is immovable by declaration (INV-1).
constexpr float FLOOR_TOP        = 0.2f;   // slab thickness; its top surface

struct RungSpec {
    const char* name;
    float tilt_rad;
    float spin_x, spin_y, spin_z;
    float drop;
};
constexpr float SPIN_FAST  = 6.0f;    // rad/s, just under MAX_OMEGA 6.28
// DROP_SHORT was 0.05 m: an ANGULAR_DRAG-era workaround ("drag steals
// ~26%, not 95%"). D7's derived law killed that constraint; the spin
// cases fall from real height like everything else (owner order).
constexpr float DROP_SHORT = 0.6f;
// The wheels RELEASE LOW, and the reason is measured physics, not
// timidity: a HERO cube spinning about a horizontal axis sweeps its
// corners half a space diagonal (0.566 m) from centre, 0.166 m BELOW
// its own face plane, so any higher release transits a corner-strike
// band above the slab and every real knock eats spin. Measured walks:
// 0.05 m drop -> 0.081 (rolling contact at full spin), 0.25 -> 0.000
// (~7 frames in the band, spin fully knocked out), 0.6 -> 0.016 (fast
// transit, 1-2 knocks). Non-monotonic, all honest. The knocking fall
// itself is a beautiful future case; THIS case's claim is that spin
// buys translation on the right axis, so it starts where rolling
// starts. R4's vertical spin sweeps no band and falls from DROP.
// The spin cases fall from the same height as everything else (owner
// order). A cube spinning about a horizontal axis strikes the floor
// with its sweeping corners on the way down, and each real knock costs
// spin: the walk after a tall fall is smaller than after a low release
// (measured 0.081 m from 0.05 m vs 0.016 m from 0.6 m). That is the
// physics of dropping a spinning die, kept honest here.
// THE LECTURE STANDARD (skill, 2026-08-20): the spin cases run on a
// HERO cube, 0.8 m — twice the extent, double the contact radius, so
// the same honest physics buys visibly more travel (omega x r budget
// 2.4 m/s at 6 rad/s) — with a STILL TWIN beside it as the on-stage
// contrast. Both pre-spawned at their own sizes so mass and inertia
// are real (resizing a live body without re-deriving both would be
// exactly the hack the no-hacks ACK forbids).
constexpr float HERO       = 0.8f;
// R7, THE CORNER STAND (G-43): the hero is PLACED corner-down, body
// diagonal vertical, nudged a hair off balance. Physics says a corner
// is the most unstable pose a cube has: it must fall to a face, and it
// must ROTATE to get there. G-43's measured attractor says the solver
// keeps it standing. Born red until the toppling channel exists.
constexpr float CORNER_NUDGE          = 0.02f;  // rad off the diagonal
constexpr float CORNER_TOPPLE_SPIN_MIN = 0.5f;  // rad/s: falling IS rotating
// fallen = resting on a FACE (with slack); standing = half diagonal up
constexpr float CORNER_FALLEN_Z_MAX   = 0.2f /*FLOOR_TOP*/ + HERO * 0.5f + 0.05f;
constexpr int   RUNG_COUNT = 8;
inline const RungSpec RUNGS[RUNG_COUNT] = {
    { "R0 control: flat, no spin",      0.0f, 0.0f, 0.0f, 0.0f,  DROP },
    { "R1 tilted 20 deg, no spin",      TILT_DEG * 3.14159265f/180.f,
                                              0.0f, 0.0f, 0.0f,  DROP },
    { "R2/R3 flat, spinning 3 rad/s",   0.0f, 0.0f, 0.0f, SPIN0, DROP },
    { "R4 THE BIG TOP: hero spins Z at 6, twin still (G-41)",
                                        0.0f, 0.0f, 0.0f, SPIN_FAST, DROP_SHORT },
    { "R5 THE WALKING WHEEL: hero spins X, falls, drives along Y",
                                        0.0f, SPIN_FAST, 0.0f, 0.0f, DROP },
    { "R6 THE WALKING WHEEL: hero spins Y, falls, drives along X",
                                        0.0f, 0.0f, SPIN_FAST, 0.0f, DROP },
    { "R7 THE CORNER STAND: corner-down, a hair off balance, it MUST fall (G-43)",
                                        0.0f, 0.0f, 0.0f, 0.0f, 0.002f },
    // R8: the SAME corner stand, on the BARE TURTLE. R7 topples through
    // box-box manifold rows (slab); the ramp's cube parks corner-down on
    // the turtle. One pose, two contact paths: whichever refuses to
    // topple names the residual stabilizer (G-43 discriminator).
    { "R8 THE CORNER STAND ON THE TURTLE: same pose, turtle rows (G-43)",
                                        0.0f, 0.0f, 0.0f, 0.0f, 0.002f },
};

// Resting centre height ABOVE THE SLAB TOP of a cube tilted t about Y
// (edge contact), t in [0,45deg].
inline float rest_height(float t) {
    return (CUBE * 0.5f) * (std::cos(t) + std::sin(t));
}

struct Scene {
    logosphere::Argus argus;   // the witness: same values asserted and logged
    int cube = -1;
    int hero = -1, twin = -1;   // the 0.8 m pair for the spin lectures
    // latched by step():
    float peak_omega_y = 0.0f;        // did it rotate while settling?
    float min_frame_keep = 1.0f;      // worst per-frame spin retention in flight
    float keep_at_touchdown = -1.0f;  // spin at first floor contact / SPIN0
    int   touchdown_frame = -1;
    int   touch_actor = -1;          // the rung's performer; only ITS
                                     // contact ends the flight window
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
        // The hero pair, spawned at their true size so mass and inertia
        // are derived honestly. Parked far on the slab until their cases.
        Particle h = p;
        h.width = h.height = h.thickness = HERO;
        h.size = HERO;
        h.x = 30.0f; h.z = FLOOR_TOP + HERO * 0.5f;
        hero = ps.queue_particle_addition(h);
        ps.flush_pending_particles();
        h.x = 33.0f;
        h.r = 0.35f; h.g = 0.75f; h.b = 0.95f;   // twin in blue
        twin = ps.queue_particle_addition(h);
        ps.flush_pending_particles();
        argus.watch(cube, "cube");
        argus.watch(slab, "slab");
        argus.watch(hero, "hero");
        argus.watch(twin, "twin");
    }

    // Which body a rung performs on.
    int actor(int r) const { return r >= 3 ? hero : cube; }

    // A teleported body carries NO history (QA 2026-08-20: cases in the
    // window's continuous world inherited the previous case's sleep
    // counters and cached support; corner stands froze at frame 0 and
    // spin died mid-air). Every direct reposition voids the transient
    // solver state and the contact cache.
    static void void_history(Particle& p, PhysicsSystem& physics, int id) {
        p.frames_at_rest = 0;
        p.low_velocity_frames = 0;
        p.quiet_growth_run = 0;
        p.rest_quiet_sq = 1e9f;
        physics.forget_body((size_t)id);
    }

    void park(ParticleSystem& ps, PhysicsSystem& physics, int id, float x) {
        auto v = ps.lock_particles_for_write();
        Particle& p = v[id];
        const float half = p.thickness * 0.5f;
        // The park station (x = 30) is OFF the 4x4 slab: parked bodies
        // rest on the TURTLE, bottom at 0. The old slab-height z left
        // them floating 0.2 m and falling at every case start — noise,
        // and the collision event that once faked a touchdown.
        p.x = x; p.y = 0.0f; p.z = half;
        p.vx = p.vy = p.vz = 0.0f;
        p.omega_x = p.omega_y = p.omega_z = 0.0f;
        p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
        p.rotation_q = logosphere::Quat::identity();
        p.is_at_rest = true;
        void_history(p, physics, id);
    }

    void arm(ParticleSystem& ps, PhysicsSystem& physics,
             const RungSpec& r, int rung_index = 0) {
        const bool spin_case = rung_index >= 3;
        // Stage the non-performers out of the experiment.
        park(ps, physics, spin_case ? cube : hero, 30.0f);
        if (spin_case) {
            // The still twin: same size, same floor, no spin — the
            // on-stage contrast the lecture standard demands. ONE
            // station for every case, (1.5, 0): clear of R5's Y-drive
            // and R6's X-walk (0.19 m measured, 0.7 m margin), and
            // clear of the backdrop pillars at y = 1.5 — the first
            // placement put the twin dead-centre on the middle pillar,
            // window only, which is exactly where the owner saw it
            // land on another body (QA 2026-08-20).
            const float tx = 1.5f;
            const float ty = 0.0f;
            auto v2 = ps.lock_particles_for_write();
            Particle& t = v2[twin];
            t.x = tx; t.y = ty;
            t.z = FLOOR_TOP + HERO * 0.5f + r.drop;
            t.vx = t.vy = t.vz = 0.0f;
            t.omega_x = t.omega_y = t.omega_z = 0.0f;
            t.rotation_x = t.rotation_y = t.rotation_z = 0.0f;
            t.rotation_q = logosphere::Quat::identity();
            t.is_at_rest = false;
            void_history(t, physics, twin);
        } else {
            park(ps, physics, twin, 33.0f);
        }
        const bool corner = (rung_index >= 6);
        const bool on_turtle = (rung_index == 7);   // off the 4x4 slab
        const float half = spin_case ? HERO * 0.5f : CUBE * 0.5f;
        rest_z = (on_turtle ? 0.0f : FLOOR_TOP) + (corner
                     ? HERO * 0.5f * 1.7320508f      // half space diagonal
                     : (spin_case ? half : rest_height(r.tilt_rad)));
        auto v = ps.lock_particles_for_write();
        Particle& p = v[actor(rung_index)];
        p.x = on_turtle ? 10.0f : 0.0f;   // clear of the slab's 4x4 span
        p.y = 0.0f; p.z = rest_z + r.drop;
        p.vx = p.vy = p.vz = 0.0f;
        if (corner) {
            // Body diagonal vertical: rotate (1,1,1) onto +Z so corner
            // (-h,-h,-h) points down, then a hair of tilt about X so the
            // balance is broken and physics has no excuse.
            logosphere::Quat q = logosphere::Quat::from_axis_angle(
                                     1, 0, 0, CORNER_NUDGE)
                               * logosphere::Quat::from_two_vectors(
                                     1, 1, 1, 0, 0, 1);
            p.rotation_q = q;
            // One body, one orientation: the Euler ledger follows the quat.
            q.to_euler_zyx(p.rotation_x, p.rotation_y, p.rotation_z);
        } else {
        p.rotation_x = p.rotation_z = 0.0f;
        p.rotation_y = r.tilt_rad;
        // One body, one orientation, also on a LIVE write: any writer
        // that sets the Euler triple keeps the quaternion coherent.
        // Spawn-time seeding covers newly created bodies; arm() edits an
        // existing one.
        p.rotation_q = logosphere::Quat::from_euler(0.0f, r.tilt_rad, 0.0f);
        }
        p.omega_x = r.spin_x;
        p.omega_y = r.spin_y;
        p.omega_z = r.spin_z;
        p.is_at_rest = false;
        void_history(p, physics, actor(rung_index));
        touch_actor = actor(rung_index);
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
            // FLIGHT ENDS AT THE FIRST CONTACT EVENT, never at a height
            // (rotation item 1, the interactions directive applied to the
            // instrument itself). The z-threshold called frames "flight"
            // in which speculative contact rows were already exchanging
            // friction, so honest braking read as a flight leak and
            // R2/R3 false-failed under the lever. The engine reports
            // every contact; the scene asks it, not the altimeter.
            // Only the rung's ACTOR ends the flight window. The first
            // version accepted any cube-or-hero event, and the parked
            // hero's own landing latched "touchdown" frames early with
            // the cube still spinning — a false 0.9998 keep that hid
            // the phantom-friction kill the owner's eyes then caught.
            for (const auto& ev : physics.get_collision_events()) {
                const int a = (int)ev.particle_a, b = (int)ev.particle_b;
                const int me = touch_actor;
                if (a == me || b == me) {
                    if (ps.lock_particles_for_read()[me].z > 0.0f) {
                        touchdown_frame = frame;
                        if (spin0 > 0.0f) {
                            const Particle& q =
                                ps.lock_particles_for_read()[me];
                            keep_at_touchdown = std::sqrt(
                                q.omega_x*q.omega_x + q.omega_y*q.omega_y +
                                q.omega_z*q.omega_z) / spin0;
                        }
                        break;
                    }
                }
            }
        }
        prev_omega_z = p.omega_z;
    }

    // Reference scenery: three dark pillars behind the action at
    // y = +1.5, tops at 0.2 / 0.4 / 0.6 m above the slab — a height
    // ruler the fall and the settle read against. BOTH drivers add it
    // (criterion b): scenery is made of PARTICLES, and a body that
    // exists in one world and not the other is a reflection break —
    // the twin once landed on the middle pillar in the window while
    // headless, pillarless, swore everything was clean. All performer
    // and twin stations are chosen clear of the pillars; if that ever
    // rots, the asserted world now contains the pillars to catch it.
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

    float displaced_x(ParticleSystem& ps, int id = -1) const {
        return ps.lock_particles_for_read()[id < 0 ? cube : id].x;
    }
    float displaced_y(ParticleSystem& ps, int id = -1) const {
        return ps.lock_particles_for_read()[id < 0 ? cube : id].y;
    }
    float settled_rot_y(ParticleSystem& ps) const {
        return std::fabs(ps.lock_particles_for_read()[cube].rotation_y);
    }
    float settled_spin(ParticleSystem& ps, int id = -1) const {
        const Particle& p = ps.lock_particles_for_read()[id < 0 ? cube : id];
        return std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y
                       + p.omega_z*p.omega_z);
    }
    float settled_z(ParticleSystem& ps, int id = -1) const {
        return ps.lock_particles_for_read()[id < 0 ? cube : id].z;
    }
};

}  // namespace scene_cube_drop
