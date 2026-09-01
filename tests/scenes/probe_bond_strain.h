#pragma once
// =============================================================================
// THE BOND STRAIN PROBE — the tear law's own measure, read-only
// =============================================================================
// INV-14 reads: "Bonds tear only under genuine geometric strain, measured
// attachment-point-to-attachment-point against the bond's own rest, at or
// beyond the tear ratio."
//
// A test that asserts "nothing tore" and measures only a COUNT enforces half
// of that sentence. It cannot say whether a tear was LICENSED (the bond really
// was stretched past its limit, and the defect is upstream in whatever
// stretched it) or MANUFACTURED (the bond went at rest), and it cannot say how
// close a surviving bond came. Both questions are one number, and this probe
// computes it so the assert and the printed measure read the same quantity —
// the Argus contract, applied to bonds instead of bodies.
//
// MIRROR, NOT REIMPLEMENTATION. The composition below is the one at the tear
// site in src/core/physics_system_v4.cpp (search `max_strain_ratio`):
//   dist  = centre to centre
//   rest  = max(target_distance, seg_len, 1 cm)
//   seg_len = |rot_a(offset_a) - rot_b(offset_b)| when rotate_offsets is set
//             (each offset turned by its OWN body — the frame lesson from the
//             foliage tear-at-rest RCA), the raw offset difference otherwise
//   strain = dist / rest, limit = gluon->max_strain_ratio()
// If that site changes, this header changes with it or the two stop being the
// same law and the measure starts lying.
//
// READ-ONLY BY CONSTRUCTION: it takes its own read lock and writes nothing, so
// the witness cannot perturb the experiment. Never call it while holding a
// view — the particle mutex is a plain shared_mutex, not recursive.
// =============================================================================

#include "../../src/core/particle_system.h"
#include "../../src/particle.h"
#include "logosphere/physics/narrow_phase.h"
#include "logosphere/physics/physics_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <vector>

namespace probe {

// One bond, as the tear law sees it.
struct BondReading {
    size_t a = 0, b = 0;
    float  dist = 0.0f;        // centre to centre, m
    float  rest = 0.0f;        // the bond's own rest, m
    float  strain = 0.0f;      // dist / rest
    float  limit = 0.0f;       // max_strain_ratio(), the tear ratio
};

// Every live bond among a body set, plus the worst of them.
struct BondCensus {
    size_t bonds = 0;              // live bonds touching the set
    float  peak_strain = 0.0f;     // worst strain seen in this observation
    float  peak_limit = 0.0f;      // and the limit that bond carries
    size_t peak_a = 0, peak_b = 0; // and who it was
    float  peak_dist = 0.0f, peak_rest = 0.0f;
    size_t at_or_over = 0;         // bonds standing at or past their own limit
};

// The tear site's own offset rotation: each offset turned by ITS OWN body.
inline void rotate_local(const Particle& p, const Vec3& o,
                         float& wx, float& wy, float& wz) {
    const float cx = std::cos(p.rotation_x), sx = std::sin(p.rotation_x);
    const float cy = std::cos(p.rotation_y), sy = std::sin(p.rotation_y);
    const float cz = std::cos(p.rotation_z), sz = std::sin(p.rotation_z);
    const float y1 = o.y * cx - o.z * sx;
    const float z1 = o.y * sx + o.z * cx;
    const float x2 = o.x * cy + z1 * sy;
    const float z2 = -o.x * sy + z1 * cy;
    wx = x2 * cz - y1 * sz;
    wy = x2 * sz + y1 * cz;
    wz = z2;
}

// The bond's own rest length, in the frame the bond is measured in.
inline float bond_rest(const Particle& pa, const Particle& pb,
                       const GluonConstraintBase& g) {
    float seg_len;
    if (g.rotate_offsets) {
        float ax, ay, az, bx, by, bz;
        rotate_local(pa, g.offset_a, ax, ay, az);
        rotate_local(pb, g.offset_b, bx, by, bz);
        const float sx = ax - bx, sy = ay - by, sz = az - bz;
        seg_len = std::sqrt(sx * sx + sy * sy + sz * sz);
    } else {
        seg_len = g.get_segment_length();   // already world-space
    }
    return std::max({g.target_distance, seg_len, 0.01f});
}

inline BondReading read_bond(const Particle& pa, const Particle& pb,
                             const GluonConstraintBase& g) {
    BondReading r;
    r.a = g.particle_a; r.b = g.particle_b;
    const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
    r.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    r.rest = bond_rest(pa, pb, g);
    r.strain = r.rest > 0.0f ? r.dist / r.rest : 0.0f;
    r.limit = g.max_strain_ratio();
    return r;
}

// Census over every live bond that touches one of `ids`. Takes its own read
// lock: do not call while a view is held.
inline BondCensus census(ParticleSystem& ps, const PhysicsSystem& physics,
                         const std::vector<size_t>& ids) {
    BondCensus c;
    std::set<const GluonConstraintBase*> seen;
    auto v = ps.lock_particles_for_read();
    for (size_t id : ids) {
        for (const GluonConstraintBase* g : physics.get_gluons_for_particle(id)) {
            if (!g || !seen.insert(g).second) continue;
            if (g->particle_a >= v.size() || g->particle_b >= v.size()) continue;
            const BondReading r = read_bond(v[g->particle_a], v[g->particle_b], *g);
            c.bonds++;
            if (std::isfinite(r.limit) && r.strain >= r.limit) c.at_or_over++;
            if (r.strain > c.peak_strain) {
                c.peak_strain = r.strain; c.peak_limit = r.limit;
                c.peak_a = r.a; c.peak_b = r.b;
                c.peak_dist = r.dist; c.peak_rest = r.rest;
            }
        }
    }
    return c;
}

// A body's DOWN-REACH, mirroring the turtle site in physics_system_v4.cpp
// (search TURTLE_CONTACT_THRESHOLD): a rotated BOX reaches down by its ORIENTED
// bounds, every other shape by its raw thickness extent. INV-1 is stated about
// this quantity ("nothing ever ends below the turtle plane beyond SLOP"), so a
// test that measures z - thickness/2 on a tumbling box is measuring something
// the law does not talk about.
inline float body_bottom(const Particle& p) {
    float half_z = p.thickness * 0.5f;
    if (p.shape == ParticleShape::BOX && box_particle_is_rotated(p)) {
        const AABB6 w = aabb_of_obb(obb_of_box_particle(p, p.z));
        half_z = (w.max_z - w.min_z) * 0.5f;
    }
    return p.z - half_z;
}

// Latch of the worst census seen across a run, so the number the assert reads
// is the number the log printed.
struct StrainWatch {
    float  peak_strain = 0.0f;
    float  peak_limit = 0.0f;
    size_t peak_a = 0, peak_b = 0;
    float  peak_dist = 0.0f, peak_rest = 0.0f;
    int    peak_frame = -1;
    size_t min_bonds = (size_t)-1;

    void take(const BondCensus& c, int frame) {
        if (c.bonds < min_bonds) min_bonds = c.bonds;
        if (c.peak_strain > peak_strain) {
            peak_strain = c.peak_strain; peak_limit = c.peak_limit;
            peak_a = c.peak_a; peak_b = c.peak_b;
            peak_dist = c.peak_dist; peak_rest = c.peak_rest;
            peak_frame = frame;
        }
    }
};

}  // namespace probe
