#include "logosphere/physics/creation_door.h"

#include "logosphere/physics/narrow_phase.h"
#include "particle.h"

#include <cmath>
#include <cstdio>

namespace logosphere {

namespace {

const char* shape_name(ParticleShape s) {
    switch (s) {
        case ParticleShape::SPHERE:    return "SPHERE";
        case ParticleShape::ELLIPSOID: return "ELLIPSOID";
        case ParticleShape::BOX:
        default:                       return "BOX";
    }
}

// Deepest point of a manifold. The solver turns EVERY point into a row, so
// the pair's illegality is the worst of them, not their average.
float deepest_point(const ContactManifold& m) {
    float d = 0.0f;
    for (int i = 0; i < m.num_points; ++i)
        if (m.points[i].penetration > d) d = m.points[i].penetration;
    return d;
}

}  // namespace

CreationBody describe_creation_body(int index, const Particle& p) {
    CreationBody b;
    b.index   = index;
    b.shape   = shape_name(p.shape);
    b.x = p.x; b.y = p.y; b.z = p.z;
    b.rot[0] = p.rotation_x; b.rot[1] = p.rotation_y; b.rot[2] = p.rotation_z;
    b.mass  = p.GetMass();

    switch (p.shape) {
        case ParticleShape::SPHERE:
            b.half[0] = b.half[1] = b.half[2] = p.size * 0.5f;
            b.world_min_z = p.z - p.size * 0.5f;
            break;
        case ParticleShape::ELLIPSOID:
            b.half[0] = p.width * 0.5f;
            b.half[1] = p.height * 0.5f;
            b.half[2] = p.thickness * 0.5f;
            b.world_min_z = p.z - b.half[2];
            break;
        case ParticleShape::BOX:
        default: {
            b.half[0] = p.width * 0.5f;
            b.half[1] = p.height * 0.5f;
            b.half[2] = p.thickness * 0.5f;
            b.rotated = box_particle_is_rotated(p);
            // THE ORIENTED BOTTOM, NOT THE AXIS-ALIGNED ONE. A log laid flat
            // carries its LENGTH on the thickness axis and its DIAMETER in
            // world Z; reading z - thickness/2 for it describes a solid the
            // body does not have. Pinned by test_collision_bounds_rotation.
            if (b.rotated) {
                const AABB6 w = aabb_of_obb(obb_of_box_particle(p, p.z));
                b.world_min_z = w.min_z;
            } else {
                b.world_min_z = p.z - b.half[2];
            }
            break;
        }
    }
    return b;
}

float oriented_bottom_offset(const Particle& p) {
    return p.z - describe_creation_body(-1, p).world_min_z;
}

float max_bottom_reach(const Particle& p) {
    switch (p.shape) {
        case ParticleShape::SPHERE:
            return p.size * 0.5f;                     // isotropic: exact
        case ParticleShape::ELLIPSOID:
        case ParticleShape::BOX:
        default: {
            const float hx = p.width * 0.5f;
            const float hy = p.height * 0.5f;
            const float hz = p.thickness * 0.5f;
            return std::sqrt(hx * hx + hy * hy + hz * hz);
        }
    }
}

TurtleVerdict turtle_verdict(const Particle& p, float plane_z, float slop) {
    TurtleVerdict v;
    v.naive_bottom = p.z - p.thickness * 0.5f;

    // Cost guard: no orientation reaches past the half diagonal, so a body
    // with that much clearance is above the plane in every pose and its exact
    // extent (quat + matrix) never needs building. This runs on every body of
    // every world at spawn. Same guard the solver's turtle pass uses.
    if (p.z - max_bottom_reach(p) >= plane_z - slop) {
        v.oriented_bottom = p.z - max_bottom_reach(p);
        return v;                              // below = false
    }

    v.oriented_bottom = p.z - oriented_bottom_offset(p);
    v.below = v.oriented_bottom < plane_z - slop;
    v.newly_visible = v.below && (v.naive_bottom >= plane_z - slop);
    return v;
}

float creation_penetration(const Particle& a, const Particle& b,
                           float& out_nx, float& out_ny, float& out_nz) {
    out_nx = out_ny = out_nz = 0.0f;

    ContactManifold m{};
    const bool both_box = (a.shape == ParticleShape::BOX && b.shape == ParticleShape::BOX);
    const bool either_rotated = both_box &&
        (box_particle_is_rotated(a) || box_particle_is_rotated(b));

    bool hit;
    if (either_rotated) {
        // BOX-BOX does not go through narrow_phase_particle_pair (that path
        // keeps the axis-aligned surface-merging the static tiles need), so
        // the oriented case is dispatched here explicitly. A tilted branch
        // compared as its world-axis extents is the wrong solid in both
        // directions: it reports overlaps that are not there and misses ones
        // that are.
        hit = narrow_phase_obb(obb_of_box_particle(a, a.z),
                               obb_of_box_particle(b, b.z),
                               0, 1, /*margin=*/0.0f, m);
    } else {
        hit = narrow_phase_particle_pair(a, b, 0, 1, /*margin=*/0.0f, m);
    }
    if (!hit || m.num_points == 0) return 0.0f;

    out_nx = m.normal_x; out_ny = m.normal_y; out_nz = m.normal_z;
    return deepest_point(m);
}

std::string creation_violation_text(const CreationOverlap& v) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "P%d %s pos(%.4f, %.4f, %.4f) half(%.4f, %.4f, %.4f) "
        "rot(%.4f, %.4f, %.4f)%s mass=%.4f bottomZ=%.4f\n"
        "        INSIDE\n"
        "      P%d %s pos(%.4f, %.4f, %.4f) half(%.4f, %.4f, %.4f) "
        "rot(%.4f, %.4f, %.4f)%s mass=%.4f bottomZ=%.4f\n"
        "      penetration %.6f m along normal (%.4f, %.4f, %.4f)",
        v.a.index, v.a.shape, v.a.x, v.a.y, v.a.z,
        v.a.half[0], v.a.half[1], v.a.half[2],
        v.a.rot[0], v.a.rot[1], v.a.rot[2], v.a.rotated ? " ROTATED" : "",
        v.a.mass, v.a.world_min_z,
        v.b.index, v.b.shape, v.b.x, v.b.y, v.b.z,
        v.b.half[0], v.b.half[1], v.b.half[2],
        v.b.rot[0], v.b.rot[1], v.b.rot[2], v.b.rotated ? " ROTATED" : "",
        v.b.mass, v.b.world_min_z,
        v.depth, v.nx, v.ny, v.nz);
    return std::string(buf);
}

}  // namespace logosphere
