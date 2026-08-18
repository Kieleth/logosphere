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
// THE DEFECT. The narrow phase builds the box side of a SPHERE-vs-BOX
// pair with `aabb_of_box_particle`, which never reads rotation
// (src/core/narrow_phase.cpp:957-976). So the sphere does not meet a
// tilted surface at all; it meets the ramp's upright bounding slab, and
// the contact normal comes back (0,0,1). A flat shelf. There is no
// along-slope component, so nothing drives it downhill. The cube, which
// goes through the 15-axis OBB path, gets (0,-0.5,0.866) and behaves.
//
// So: same slope, same release, and only one of them moves. That is
// INV-12 (contacts come from bodies' actual oriented shapes) broken in
// one dispatch branch, and it is visible in three seconds.
//
// The scene is written ONCE here and both drivers run it. Neither holds
// a body, a force or a threshold.
// =============================================================================
#pragma once

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>

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
constexpr float RAMP_LEN   = 8.0f;
constexpr float RAMP_THICK = 0.4f;
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
    int ramp = -1, cube = -1, ball = -1;
    float cube_x0 = 0.0f, ball_x0 = 0.0f;

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
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics) {
        ps.update_bvh();
        physics.update(DT);
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
    static bool travelled(float d) { return d > TRAVEL_MIN; }
};

}  // namespace scene_ramp_race
