// =============================================================================
// SCENE: a cube and a sphere released on the same ramp
// =============================================================================
// The watchable form of board front F2, and of the gap `part 5` of
// test_collision_bounds_rotation pins at bounds level.
//
// THE PHYSICS. A body resting on a slope feels gravity split by the
// surface normal: one component pressing into the slope, one along it.
// The along-slope component is what moves it. On a 30 degree ramp that
// is g*sin(30) = 4.9 m/s^2 before friction. A cube slides. A sphere
// rolls, and rolls further, because rolling without slipping dissipates
// nothing at the contact.
//
// THE DEFECT THIS SCENE WAS BUILT FOR, and its fix (2026-08-19). The
// narrow phase used to build the box side of a SPHERE-vs-BOX pair with
// `aabb_of_box_particle`, which never reads rotation. The sphere met the
// ramp's upright bounding slab, got a (0,0,1) normal, and sat on a flat
// shelf while the cube — which goes through the 15-axis OBB path and
// gets (0,-0.5,0.866) — slid away. `narrow_phase_sphere_obb` closed
// that: the sphere now travels 6.251 m against the cube's 6.356 m.
//
// WHAT IS STILL RED HERE is a different mechanism, D2 1.2: contact rows
// carry jx/jy/jz from the manifold normal and NO LEVER ARM, so no
// contact in this engine can spin a body up. Both bodies leave the ramp
// edge, fall, and land on the turtle with peak |omega| of exactly 0.
//
// THE WITNESS. Argus watches all three bodies (ramp, cube, ball) every
// frame, so the asserts, the stdout log and the window readout read the
// same numbers. The scene latches what only a per-frame observer can
// see: lane deviation, the fixture's drift, the two-ledger divergence,
// and how close the two racers ever came to each other.
//
// The scene is written ONCE here and both drivers run it. Neither holds
// a body, a force or a threshold.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdlib>

namespace scene_ramp_race {

// 40 degrees, not 30. tan(30) = 0.577 against the solver's 0.5 friction
// is marginal, and a test whose subject only just moves cannot tell
// "held by friction" from "standing on the wrong normal". tan(40) =
// 0.839 slides decisively.
constexpr float SLOPE_DEG  = 40.0f;
constexpr float SLOPE_RAD  = SLOPE_DEG * 3.14159265f / 180.0f;
constexpr float BODY       = 0.4f;
constexpr float LANE       = 1.2f;    // m, the two lanes, so they never meet
constexpr float DROP       = 0.35f;   // m above the ramp face at release
constexpr float DT         = 1.0f / 60.0f;
constexpr int   RUN_FRAMES = 240;     // 4 s: time to land and then travel
// Both must travel. The claim is not "the same distance" (a sphere
// rolls further than a cube slides, and should) but "gravity along the
// slope moved each of them", which is the thing a flat normal denies.
constexpr float TRAVEL_MIN = 0.30f;   // m downhill
// A body that leaves the ramp edge and lands on the turtle MUST turn.
// Steady sliding on a uniform slope legitimately has no angular
// acceleration (the normal-force distribution balances the friction
// moment), so this is deliberately NOT a claim about the slide. It is a
// claim about the launch and the landing: unsupported past the edge,
// gravity acts about the last contact; landing from a fall, the impulse
// arrives off the centre of mass. Both are lever arms. Exactly zero is
// impossible for either, so any positive threshold discriminates.
constexpr float SPIN_MIN   = 0.05f;   // rad/s, peak over the run
constexpr float RAMP_LEN   = 8.0f;
constexpr float RAMP_THICK = 0.4f;
// --- thresholds for the DOFs the witness added ------------------------
// Lanes: no lateral force exists in this experiment. The ramp is tilted
// about Y only, gravity is -Z, and the two bodies are 2.4 m apart, so
// neither may wander out of its lane. Slop-sized, not tuned.
constexpr float LANE_DEV_MAX = 0.02f;   // m off its release lane (default mode)
// LEVER-MODE CONTRACT (CONTACT_TORQUE=1), 2026-08-20 decree: the
// torque slices' claims are enforced in the mode they are made for.
// The lever lane bound is a RATCHET at the measured value, not an
// acceptance: the residual walk has two NAMED mechanisms (the omega
// seed from sequential per-point solving at the first strike, and
// D7's damper parking the tumble at the 45-degree balance), each
// boarded. This bound may only shrink.
constexpr float LANE_DEV_MAX_LEVER = 0.30f;  // measured 0.2605 after the patch
constexpr float ROLL_MIN_LEVER     = 2.0f;   // rad/s: the sphere must ROLL (measured 5.30)
// A KINEMATIC fixture is held by its external writer. If the ramp moves
// at all, every travel number below is measured against a moving datum.
constexpr float FIXTURE_DRIFT_MAX = 1e-4f;  // m, any axis
// Each racer must reach the turtle and stop there. Bottom at z = 0 is
// the turtle plane; the solver's slop is 0.001 m and a settled body
// rides one slop above it.
constexpr float REST_BOTTOM_MAX = 0.01f;   // m above the turtle
constexpr float REST_SPEED_MAX  = 0.05f;   // m/s at the deadline
// One body, one orientation (G-23). Quaternion truth is the default
// since 2026-08-19; a body whose two ledgers disagree is a defect.
constexpr float COHERENCE_MAX = 0.01f;     // rad
// Two-band coherence (G-21 ruling, 2026-08-21): float32 Euler
// extraction has a measured worst error of 0.014 rad inside
// Argus::FOLD_BAND of the gimbal fold and 0.0002 outside it, so the
// contract is sharp away from the fold and the representational
// ceiling plus margin inside it (adaptive thresholds, owner ruling).
constexpr float DIV_MAX_SHARP = 0.01f;
constexpr float DIV_MAX_FOLD  = 0.015f;
// The lanes exist so the two experiments cannot contaminate each other.
// Centre-to-centre must never fall to where their shapes could meet.
constexpr float LANE_GAP_MIN = 2.0f * BODY;   // m, centre to centre
// Rest the ramp ON the turtle rather than through it. A tilted 8 m box
// has a Z half-extent of len*sin/2 + thick*cos/2 = 2.724 m at 40 deg, so
// a centre at z = 2.0 puts its lowest corner 0.72 m UNDER the turtle and
// the turtle clamp lifts the whole body until it is not. That is INV-1
// working correctly on a careless placement, and it moved the ramp by a
// measured 0.72 m before this line existed.
inline float ramp_centre_z() {
    return RAMP_LEN * std::sin(SLOPE_RAD) * 0.5f
         + RAMP_THICK * std::cos(SLOPE_RAD) * 0.5f;
}

struct Scene {
    logosphere::Argus argus;   // the witness: asserts and logs, one source
    int ramp = -1, cube = -1, ball = -1;
    float cube_x0 = 0.0f, ball_x0 = 0.0f;
    // Peak angular speed each body reached at ANY point. Latched, because
    // a body that spins up and settles is quiet by the deadline.
    float cube_spin_peak = 0.0f, ball_spin_peak = 0.0f;
    // --- latched by step() from the witness, per frame ----------------
    float cube_lane_dev = 0.0f, ball_lane_dev = 0.0f;   // worst |y - lane|
    float ramp_drift    = 0.0f;                          // worst fixture move
    float cube_div_max  = 0.0f, ball_div_max  = 0.0f;   // worst q-vs-Euler
    float lane_gap_min  = 1e9f;                          // closest approach
    float ramp_x0 = 0.0f, ramp_y0 = 0.0f, ramp_z0 = 0.0f;

    void build(ParticleSystem& ps) {
        // The ramp: a long box tilted about Y, so downhill runs along -X.
        Particle r{};
        r.x = 0.0f; r.y = 0.0f; r.z = ramp_centre_z();
        r.shape = ParticleShape::BOX;
        r.width = RAMP_LEN; r.height = 4.0f; r.thickness = RAMP_THICK;
        r.size = RAMP_LEN;
        r.rotation_y = SLOPE_RAD;
        r.r = 0.35f; r.g = 0.38f; r.b = 0.45f; r.a = 1.0f;
        r.SetMaterial(Materials::Type::STONE);
        ramp = ps.queue_particle_addition(r);

        // MEASURED, not assumed: with rotation_y positive the face
        // descends toward +X, so uphill is -X and downhill is +X. The
        // first version had this backwards and dropped both bodies from
        // 2.8 m above a surface that was 1.0 m below them.
        const float sx = -2.2f;                                  // uphill
        const float sz = ramp_centre_z() + std::tan(SLOPE_RAD) * (-sx)
                       + 0.2f / std::cos(SLOPE_RAD)              // half-thickness
                       + BODY * 0.5f + DROP;

        Particle c{};
        c.x = sx; c.y = -LANE; c.z = sz;
        c.shape = ParticleShape::BOX;
        c.width = c.height = c.thickness = BODY;
        c.size = BODY;
        c.r = 0.9f; c.g = 0.6f; c.b = 0.25f; c.a = 1.0f;
        c.SetMaterial(Materials::Type::STONE);
        cube = ps.queue_particle_addition(c);

        Particle s{};
        s.x = sx; s.y = +LANE; s.z = sz;
        s.shape = ParticleShape::SPHERE;
        s.size = BODY;
        s.width = s.height = s.thickness = BODY;
        s.r = 0.35f; s.g = 0.75f; s.b = 0.95f; s.a = 1.0f;
        s.SetMaterial(Materials::Type::STONE);
        ball = ps.queue_particle_addition(s);

        ps.flush_pending_particles();
        {   // The ramp is held by an external writer: it is a fixture,
            // not scenery that happens to be immovable (INV-1).
            auto v = ps.lock_particles_for_write();
            v[ramp].solver_mode = ParticleSolverMode::KINEMATIC;
            v[ramp].owner = ParticleOwner::DYNAMICS;
            v[ramp].is_at_rest = true;
        }
        cube_x0 = sx; ball_x0 = sx;
        {   // The fixture's datum, read back from the engine rather than
            // from the constants: the turtle clamp may have moved it.
            auto v = ps.lock_particles_for_read();
            ramp_x0 = v[ramp].x; ramp_y0 = v[ramp].y; ramp_z0 = v[ramp].z;
        }
        argus.watch(ramp, "ramp");
        argus.watch(cube, "cube");
        argus.watch(ball, "ball");
    }

    // Restart the race (owner order 2026-08-21: SPACE cycles the
    // experiment, zoom lives on Z). Racers return to their start
    // stations; the teleport law applies: history voided, contact
    // caches forgotten (G-43). Latched measurements reset so each run
    // is a fresh experiment.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics) {
        const float sx = cube_x0;
        const float sz = ramp_centre_z() + std::tan(SLOPE_RAD) * (-sx)
                       + 0.2f / std::cos(SLOPE_RAD)
                       + BODY * 0.5f + DROP;
        auto place = [&](int id, float y) {
            auto v = ps.lock_particles_for_write();
            Particle& p = v[id];
            p.x = sx; p.y = y; p.z = sz;
            p.vx = p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = p.omega_z = 0.0f;
            p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
            p.rotation_q = logosphere::Quat::identity();
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.low_velocity_frames = 0;
            p.quiet_growth_run = 0;
            p.rest_quiet_sq = 1e9f;
            physics.forget_body((size_t)id);
        };
        place(cube, -LANE);
        place(ball, +LANE);
        cube_spin_peak = ball_spin_peak = 0.0f;
        cube_lane_dev = ball_lane_dev = 0.0f;
        cube_div_max = ball_div_max = 0.0f;
        lane_gap_min = 1e9f;
        argus.reset_milestones(cube);
        argus.reset_milestones(ball);
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame = -1) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        // Everything below reads the WITNESS, not the particles: the
        // asserts and the log cannot drift apart if there is one source.
        const float c = argus.spin(cube), b = argus.spin(ball);
        if (c > cube_spin_peak) cube_spin_peak = c;
        if (b > ball_spin_peak) ball_spin_peak = b;
        if (const auto* s = argus.latest(cube)) {
            const float d = std::fabs(s->y - (-LANE));
            if (d > cube_lane_dev) cube_lane_dev = d;
        }
        if (const auto* s = argus.latest(ball)) {
            const float d = std::fabs(s->y - (+LANE));
            if (d > ball_lane_dev) ball_lane_dev = d;
        }
        if (const auto* s = argus.latest(ramp)) {
            const float dx = s->x - ramp_x0, dy = s->y - ramp_y0,
                        dz = s->z - ramp_z0;
            const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d > ramp_drift) ramp_drift = d;
        }
        const float dc = argus.divergence(cube), db = argus.divergence(ball);
        if (dc > cube_div_max) cube_div_max = dc;
        if (db > ball_div_max) ball_div_max = db;
        const float gap = argus.separation(cube, ball);
        if (gap >= 0.0f && gap < lane_gap_min) lane_gap_min = gap;
    }

    // Downhill is +X, so travel is how far each has come from its start.
    float cube_travel(ParticleSystem& ps) const {
        return ps.lock_particles_for_read()[cube].x - cube_x0;
    }
    float ball_travel(ParticleSystem& ps) const {
        return ps.lock_particles_for_read()[ball].x - ball_x0;
    }
    void position(ParticleSystem& ps, int id, float& x, float& y, float& z) const {
        auto v = ps.lock_particles_for_read();
        x = v[id].x; y = v[id].y; z = v[id].z;
    }
    // Where the ramp's REAL tilted face is at a given x, and where its
    // UNROTATED box top is. A body resting on the first is on the ramp.
    // A body resting on the second has fallen through the ramp it can see
    // and landed on the flat shelf aabb_of_box_particle invents.
    static float face_z_at(float x)  {
        return ramp_centre_z() - std::tan(SLOPE_RAD) * x
             + RAMP_THICK * 0.5f / std::cos(SLOPE_RAD);
    }
    static float shelf_z()           { return ramp_centre_z() + RAMP_THICK * 0.5f; }

    float ball_bottom(ParticleSystem& ps) const {
        return ps.lock_particles_for_read()[ball].z - BODY * 0.5f;
    }

    // --- the witness answers the rest of the state ---------------------
    float speed(int id) const {
        const logosphere::Argus::State* s = argus.latest(id);
        return s ? std::sqrt(s->vx*s->vx + s->vy*s->vy + s->vz*s->vz) : -1.0f;
    }
    float bottom(int id) const {
        const logosphere::Argus::State* s = argus.latest(id);
        return s ? s->z - BODY * 0.5f : -1.0f;
    }

    static bool travelled(float d) { return d > TRAVEL_MIN; }
    static bool turned(float peak_omega) { return peak_omega > SPIN_MIN; }
    static bool in_lane(float dev) {
        static const bool lever = []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == ' '); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
        return dev < (lever ? LANE_DEV_MAX_LEVER : LANE_DEV_MAX);
    }
    static bool held(float drift)         { return drift < FIXTURE_DRIFT_MAX; }
    static bool coherent(float sharp, float fold) {
        return sharp < DIV_MAX_SHARP && fold < DIV_MAX_FOLD;
    }
    static bool lanes_kept(float gap)     { return gap > LANE_GAP_MIN; }
    static bool landed_and_stopped(float bottom_z, float spd) {
        return std::fabs(bottom_z) < REST_BOTTOM_MAX && spd < REST_SPEED_MAX;
    }
};

}  // namespace scene_ramp_race
