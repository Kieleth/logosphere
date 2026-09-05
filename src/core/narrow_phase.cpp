#include "logosphere/physics/narrow_phase.h"
// INV-29 constants registry (schema/physics.yaml -> generated header).
#include "generated/physics_constants.h"
using namespace PhysicsV4;
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

// ============================================================================
// SWEPT AABB vs STATIC AABB
// ============================================================================
// Computes the time of first impact between a moving AABB and a static AABB.
// Uses slab intersection on the Minkowski-summed target (expanded by mover's
// half-extents). The ray is from the mover's center at t=0 to t=1.
// Returns normalized time [0,1] of first contact, and the contact normal.
// ============================================================================

bool swept_aabb_vs_aabb(const AABB6& a_start, const AABB6& a_end, const AABB6& b,
                       float& out_t, float& out_nx, float& out_ny, float& out_nz) {
    // Half-extents of moving AABB
    float a_hx = (a_start.max_x - a_start.min_x) * 0.5f;
    float a_hy = (a_start.max_y - a_start.min_y) * 0.5f;
    float a_hz = (a_start.max_z - a_start.min_z) * 0.5f;

    // Start center
    float sx = (a_start.min_x + a_start.max_x) * 0.5f;
    float sy = (a_start.min_y + a_start.max_y) * 0.5f;
    float sz = (a_start.min_z + a_start.max_z) * 0.5f;

    // End center
    float ex = (a_end.min_x + a_end.max_x) * 0.5f;
    float ey = (a_end.min_y + a_end.max_y) * 0.5f;
    float ez = (a_end.min_z + a_end.max_z) * 0.5f;

    // Motion vector
    float dx = ex - sx, dy = ey - sy, dz = ez - sz;

    // Minkowski-summed target: expand b by a's half-extents
    float tx_min = b.min_x - a_hx, tx_max = b.max_x + a_hx;
    float ty_min = b.min_y - a_hy, ty_max = b.max_y + a_hy;
    float tz_min = b.min_z - a_hz, tz_max = b.max_z + a_hz;

    // Slab intersection: compute entry/exit time on each axis
    float t_entry = -std::numeric_limits<float>::infinity();
    float t_exit = std::numeric_limits<float>::infinity();
    int entry_axis = -1;
    float entry_sign = 0.0f;

    auto slab_check = [&](float origin, float dir, float smin, float smax, int axis) -> bool {
        if (std::abs(dir) < 1e-8f) {
            // Parallel: must be inside the slab
            return origin >= smin && origin <= smax;
        }
        float inv = 1.0f / dir;
        float t1 = (smin - origin) * inv;
        float t2 = (smax - origin) * inv;
        float entry = std::min(t1, t2);
        float exit = std::max(t1, t2);
        float sign = (dir > 0) ? -1.0f : 1.0f;  // normal points against motion
        if (entry > t_entry) {
            t_entry = entry;
            entry_axis = axis;
            entry_sign = sign;
        }
        if (exit < t_exit) t_exit = exit;
        return t_entry <= t_exit;
    };

    if (!slab_check(sx, dx, tx_min, tx_max, 0)) return false;
    if (!slab_check(sy, dy, ty_min, ty_max, 1)) return false;
    if (!slab_check(sz, dz, tz_min, tz_max, 2)) return false;

    // Valid hit only if entry is in [0, 1]
    if (t_entry < 0.0f || t_entry > 1.0f) return false;
    if (entry_axis < 0) return false;  // Started inside (already overlapping)

    out_t = t_entry;
    out_nx = (entry_axis == 0) ? entry_sign : 0.0f;
    out_ny = (entry_axis == 1) ? entry_sign : 0.0f;
    out_nz = (entry_axis == 2) ? entry_sign : 0.0f;
    return true;
}

// ============================================================================
// SUTHERLAND-HODGMAN POLYGON CLIPPING (AABB-specialized)
// ============================================================================
// Clips a convex polygon against axis-aligned half-planes.
// For AABB face clipping, we clip the incident face quad against the
// 4 side planes of the reference face.
//
// Since all planes are axis-aligned, inside/outside tests are single
// coordinate comparisons and intersections are single-axis lerps.
// ============================================================================

// MAX_CLIP_VERTICES: INV-29 registry (ContactGeometryTolerances group).

struct ClipVertex {
    float x, y, z;
};

// Clip polygon against half-plane: coordinate[axis] >= limit
static int clip_against_min(const ClipVertex* in, int in_count,
                            ClipVertex* out, int axis, float limit) {
    if (in_count == 0) return 0;
    int out_count = 0;

    for (int i = 0; i < in_count; i++) {
        int next = (i + 1) % in_count;
        const float* ci = &in[i].x;
        const float* cn = &in[next].x;
        bool i_inside = ci[axis] >= limit;
        bool n_inside = cn[axis] >= limit;

        if (i_inside && n_inside) {
            out[out_count++] = in[next];
        } else if (i_inside && !n_inside) {
            // Exiting: add intersection
            float t = (limit - ci[axis]) / (cn[axis] - ci[axis]);
            ClipVertex intersection;
            intersection.x = in[i].x + t * (in[next].x - in[i].x);
            intersection.y = in[i].y + t * (in[next].y - in[i].y);
            intersection.z = in[i].z + t * (in[next].z - in[i].z);
            out[out_count++] = intersection;
        } else if (!i_inside && n_inside) {
            // Entering: add intersection then next
            float t = (limit - ci[axis]) / (cn[axis] - ci[axis]);
            ClipVertex intersection;
            intersection.x = in[i].x + t * (in[next].x - in[i].x);
            intersection.y = in[i].y + t * (in[next].y - in[i].y);
            intersection.z = in[i].z + t * (in[next].z - in[i].z);
            out[out_count++] = intersection;
            out[out_count++] = in[next];
        }
        // Both outside: skip

        if (out_count >= MAX_CLIP_VERTICES) break;
    }
    return out_count;
}

// Clip polygon against half-plane: coordinate[axis] <= limit
static int clip_against_max(const ClipVertex* in, int in_count,
                            ClipVertex* out, int axis, float limit) {
    if (in_count == 0) return 0;
    int out_count = 0;

    for (int i = 0; i < in_count; i++) {
        int next = (i + 1) % in_count;
        const float* ci = &in[i].x;
        const float* cn = &in[next].x;
        bool i_inside = ci[axis] <= limit;
        bool n_inside = cn[axis] <= limit;

        if (i_inside && n_inside) {
            out[out_count++] = in[next];
        } else if (i_inside && !n_inside) {
            float t = (limit - ci[axis]) / (cn[axis] - ci[axis]);
            ClipVertex intersection;
            intersection.x = in[i].x + t * (in[next].x - in[i].x);
            intersection.y = in[i].y + t * (in[next].y - in[i].y);
            intersection.z = in[i].z + t * (in[next].z - in[i].z);
            out[out_count++] = intersection;
        } else if (!i_inside && n_inside) {
            float t = (limit - ci[axis]) / (cn[axis] - ci[axis]);
            ClipVertex intersection;
            intersection.x = in[i].x + t * (in[next].x - in[i].x);
            intersection.y = in[i].y + t * (in[next].y - in[i].y);
            intersection.z = in[i].z + t * (in[next].z - in[i].z);
            out[out_count++] = intersection;
            out[out_count++] = in[next];
        }

        if (out_count >= MAX_CLIP_VERTICES) break;
    }
    return out_count;
}

// ============================================================================
// SAT + FACE CLIPPING FOR AABB-AABB
// ============================================================================

bool narrow_phase_aabb(const AABB6& a, const AABB6& b,
                       size_t id_a, size_t id_b,
                       float margin,
                       ContactManifold& out) {
    out.num_points = 0;

    // ------------------------------------------------------------------
    // Step 1: Compute overlap on each axis
    // ------------------------------------------------------------------
    float overlap_x = std::min(a.max_x, b.max_x) - std::max(a.min_x, b.min_x);
    float overlap_y = std::min(a.max_y, b.max_y) - std::max(a.min_y, b.min_y);
    float overlap_z = std::min(a.max_z, b.max_z) - std::max(a.min_z, b.min_z);

    // If any axis has no overlap beyond margin, no contact
    if (overlap_x < -margin || overlap_y < -margin || overlap_z < -margin) {
        return false;
    }

    // ------------------------------------------------------------------
    // Step 2: Containment-aware SAT axis selection
    // ------------------------------------------------------------------
    // For each axis, determine whether one box is fully contained in the
    // other. If contained, use dist_to_exit (large value, won't be picked
    // unless it's genuinely the thinnest direction). If partially overlapping,
    // use plain overlap (the actual penetration depth).
    //
    // This fixes the small-on-large problem (foot on floor: X/Y show large
    // dist_to_exit, Z wins) AND the tile boundary problem (foot straddles
    // tile edge: overlap_y on non-contained axis is larger than overlap_z).
    // ------------------------------------------------------------------
    // BOUNDARY_EPSILON, ALIGNED_PENALTY: INV-29 registry.

    auto compute_metric = [&](float min_i, float max_i, float min_j, float max_j, float overlap) -> float {
        // Check if bounds are aligned (same extent on this axis)
        if (std::abs(min_i - min_j) < BOUNDARY_EPSILON &&
            std::abs(max_i - max_j) < BOUNDARY_EPSILON) {
            return ALIGNED_PENALTY;
        }

        // Check containment: is one range fully inside the other?
        bool a_in_b = (min_i >= min_j - BOUNDARY_EPSILON) && (max_i <= max_j + BOUNDARY_EPSILON);
        bool b_in_a = (min_j >= min_i - BOUNDARY_EPSILON) && (max_j <= max_i + BOUNDARY_EPSILON);

        if (a_in_b || b_in_a) {
            // Contained: use distance-to-exit
            float inner_min = std::max(min_i, min_j);
            float inner_max = std::min(max_i, max_j);
            float outer_min = std::min(min_i, min_j);
            float outer_max = std::max(max_i, max_j);
            return std::min(inner_max - outer_min, outer_max - inner_min);
        }

        // Partial overlap: use plain overlap (actual penetration)
        return overlap;
    };

    float metric_x = compute_metric(a.min_x, a.max_x, b.min_x, b.max_x, overlap_x);
    float metric_y = compute_metric(a.min_y, a.max_y, b.min_y, b.max_y, overlap_y);
    float metric_z = compute_metric(a.min_z, a.max_z, b.min_z, b.max_z, overlap_z);

    // Pick axis with minimum metric
    int normal_axis = 0;
    float min_metric = metric_x;
    if (metric_y < min_metric) { min_metric = metric_y; normal_axis = 1; }
    if (metric_z < min_metric) { min_metric = metric_z; normal_axis = 2; }

    // Use actual overlap for penetration (not the metric)
    float penetration = (normal_axis == 0) ? overlap_x :
                        (normal_axis == 1) ? overlap_y : overlap_z;

    // Skip if beyond margin
    if (penetration < -margin) return false;

    // A FACE HAS AREA (G-69, 2026-09-02). A row along one axis is a face
    // contact only where the boxes overlap on the other two; a footprint
    // that merely touches (overlap 0) or misses is a line or nothing, and
    // no approach along the normal ever lands on it. Without this, a tile
    // 2 mm above its diagonal neighbour read a speculative z-row (the gap,
    // -2 mm, is "more separated" than the touching x, 0, so z won the
    // metric), and every falling dirt tile pressed on six tiles below it
    // through such rows: the edge tiles of case G landed 3 mm deep. The
    // approach a speculative row exists to stop needs the same face. The
    // bar is SLOP, the engine's own "geometric error, not geometry": a tile
    // settled 126 um into its bedrock read a side row to the DIAGONAL bedrock
    // tile through that hair of z-overlap, and the row carried 15 N s.
    // SEAM_FACE_AREA=0 restores the old rows for A/B (INV-36's pattern).
    //
    // OPT-IN (SEAM_FACE_AREA=1), 2026-09-02: on Eden this rule costs 250 ms
    // per steady frame (621 -> 872 ms, alone, twice) while the merge's
    // precondition alone already gives case H, the blade study and the leg
    // choreography their greens. What it fixes - case G's four edge tiles
    // landing 3 mm deep - is a symptom of a row that carries load with no
    // approach (G-73); this rule removes the row, not the reason. Kept as
    // the experiment that proves the diagnosis, off until the reason is
    // found or the owner rules the price worth it.
    {
        static const bool face_area_off = [] {
            const char* v = std::getenv("SEAM_FACE_AREA");
            return !(v && v[0] == '1');
        }();
        const float o1 = (normal_axis == 0) ? overlap_y : overlap_x;
        const float o2 = (normal_axis == 2) ? overlap_y : overlap_z;
        if (!face_area_off && (o1 <= PhysicsV4::SLOP || o2 <= PhysicsV4::SLOP)) return false;
    }

    // ------------------------------------------------------------------
    // Step 3: Determine face/depth sign (internal) and output normal (convention)
    // ------------------------------------------------------------------
    // Internal `face_sign` (from A center toward B center) selects the
    // reference/incident faces and computes penetration depth — this is
    // how the Sutherland-Hodgman clipping below was written.
    //
    // Output normal uses the OPPOSITE sign, per the solver's convention:
    // `pa.vx += c.jx * impulse * inv_ma` at physics_system_v4.cpp:1871
    // applies impulse to A in the +normal direction. For that to push A
    // AWAY from B, the normal must point from B toward A. Turtle contact
    // at physics_system_v4.cpp:508 uses this convention (normal +Z pushes
    // particle up off the turtle below).
    // ------------------------------------------------------------------
    float center_a, center_b;
    if (normal_axis == 0) {
        center_a = (a.min_x + a.max_x) * 0.5f;
        center_b = (b.min_x + b.max_x) * 0.5f;
    } else if (normal_axis == 1) {
        center_a = (a.min_y + a.max_y) * 0.5f;
        center_b = (b.min_y + b.max_y) * 0.5f;
    } else {
        center_a = (a.min_z + a.max_z) * 0.5f;
        center_b = (b.min_z + b.max_z) * 0.5f;
    }
    float face_sign   = (center_b > center_a) ? 1.0f : -1.0f;  // internal
    float normal_sign = -face_sign;                             // output

    out.body_a = id_a;
    out.body_b = id_b;
    out.normal_x = (normal_axis == 0) ? normal_sign : 0.0f;
    out.normal_y = (normal_axis == 1) ? normal_sign : 0.0f;
    out.normal_z = (normal_axis == 2) ? normal_sign : 0.0f;
    out.reference_axis = normal_axis;
    out.is_face_contact = true;

    // ------------------------------------------------------------------
    // Step 4: Identify reference and incident faces
    // ------------------------------------------------------------------
    // Reference face: the face of one box perpendicular to the normal,
    // on the side closest to the other box.
    // Incident face: the face of the other box most anti-parallel.
    //
    // For AABB, the incident face is always the opposite face on the
    // same axis. We choose reference_body so that the reference face
    // is the one "closer" to the other box's center.
    // ------------------------------------------------------------------

    // Reference face bounds (the face plane + its 4 side planes)
    float ref_plane;   // The reference face plane coordinate
    float side_min[2]; // Side plane minimums (2 axes perpendicular to normal)
    float side_max[2]; // Side plane maximums
    int side_axes[2];  // Which world axes are the side planes

    // Incident face vertices (quad on the other box)
    ClipVertex incident[4];

    if (normal_axis == 0) {
        side_axes[0] = 1; side_axes[1] = 2;  // Y and Z are side planes
        if (face_sign > 0) {
            // A→B in +X: reference face is A's +X face, incident is B's -X face
            ref_plane = a.max_x;
            side_min[0] = a.min_y; side_max[0] = a.max_y;
            side_min[1] = a.min_z; side_max[1] = a.max_z;
            out.reference_body = 0;
            float ix = b.min_x;  // B's -X face
            incident[0] = {ix, b.min_y, b.min_z};
            incident[1] = {ix, b.max_y, b.min_z};
            incident[2] = {ix, b.max_y, b.max_z};
            incident[3] = {ix, b.min_y, b.max_z};
        } else {
            // A→B in -X: reference face is A's -X face, incident is B's +X face
            ref_plane = a.min_x;
            side_min[0] = a.min_y; side_max[0] = a.max_y;
            side_min[1] = a.min_z; side_max[1] = a.max_z;
            out.reference_body = 0;
            float ix = b.max_x;
            incident[0] = {ix, b.min_y, b.min_z};
            incident[1] = {ix, b.max_y, b.min_z};
            incident[2] = {ix, b.max_y, b.max_z};
            incident[3] = {ix, b.min_y, b.max_z};
        }
    } else if (normal_axis == 1) {
        side_axes[0] = 0; side_axes[1] = 2;  // X and Z
        if (face_sign > 0) {
            ref_plane = a.max_y;
            side_min[0] = a.min_x; side_max[0] = a.max_x;
            side_min[1] = a.min_z; side_max[1] = a.max_z;
            out.reference_body = 0;
            float iy = b.min_y;
            incident[0] = {b.min_x, iy, b.min_z};
            incident[1] = {b.max_x, iy, b.min_z};
            incident[2] = {b.max_x, iy, b.max_z};
            incident[3] = {b.min_x, iy, b.max_z};
        } else {
            ref_plane = a.min_y;
            side_min[0] = a.min_x; side_max[0] = a.max_x;
            side_min[1] = a.min_z; side_max[1] = a.max_z;
            out.reference_body = 0;
            float iy = b.max_y;
            incident[0] = {b.min_x, iy, b.min_z};
            incident[1] = {b.max_x, iy, b.min_z};
            incident[2] = {b.max_x, iy, b.max_z};
            incident[3] = {b.min_x, iy, b.max_z};
        }
    } else {
        side_axes[0] = 0; side_axes[1] = 1;  // X and Y
        if (face_sign > 0) {
            ref_plane = a.max_z;
            side_min[0] = a.min_x; side_max[0] = a.max_x;
            side_min[1] = a.min_y; side_max[1] = a.max_y;
            out.reference_body = 0;
            float iz = b.min_z;
            incident[0] = {b.min_x, b.min_y, iz};
            incident[1] = {b.max_x, b.min_y, iz};
            incident[2] = {b.max_x, b.max_y, iz};
            incident[3] = {b.min_x, b.max_y, iz};
        } else {
            ref_plane = a.min_z;
            side_min[0] = a.min_x; side_max[0] = a.max_x;
            side_min[1] = a.min_y; side_max[1] = a.max_y;
            out.reference_body = 0;
            float iz = b.max_z;
            incident[0] = {b.min_x, b.min_y, iz};
            incident[1] = {b.max_x, b.min_y, iz};
            incident[2] = {b.max_x, b.max_y, iz};
            incident[3] = {b.min_x, b.max_y, iz};
        }
    }

    // ------------------------------------------------------------------
    // Step 5: Sutherland-Hodgman clipping
    // ------------------------------------------------------------------
    // Clip incident face quad against the 4 side planes of the reference face.
    // For AABB, side planes are axis-aligned: 2 axes x (min, max) = 4 planes.
    // ------------------------------------------------------------------
    ClipVertex buf1[MAX_CLIP_VERTICES];
    ClipVertex buf2[MAX_CLIP_VERTICES];

    // Start with incident face
    std::memcpy(buf1, incident, 4 * sizeof(ClipVertex));
    int count = 4;

    // Clip against side_axes[0] >= side_min[0]
    count = clip_against_min(buf1, count, buf2, side_axes[0], side_min[0]);
    // Clip against side_axes[0] <= side_max[0]
    count = clip_against_max(buf2, count, buf1, side_axes[0], side_max[0]);
    // Clip against side_axes[1] >= side_min[1]
    count = clip_against_min(buf1, count, buf2, side_axes[1], side_min[1]);
    // Clip against side_axes[1] <= side_max[1]
    count = clip_against_max(buf2, count, buf1, side_axes[1], side_max[1]);

    if (count == 0) return false;

    // ------------------------------------------------------------------
    // Step 6: Keep points behind (or at) reference face plane
    // ------------------------------------------------------------------
    // Penetration = how far the point is behind the reference face.
    // For normal pointing in +axis direction: depth = ref_plane - point[axis]
    // For normal pointing in -axis direction: depth = point[axis] - ref_plane
    // ------------------------------------------------------------------
    out.num_points = 0;
    for (int i = 0; i < count && out.num_points < 4; i++) {
        const float* coords = &buf1[i].x;
        float depth;
        if (face_sign > 0) {
            depth = ref_plane - coords[normal_axis];
            // Point must be on the incident side (behind reference face from normal direction)
            // Actually for +normal: incident is on the - side, so depth measures how far
            // the incident point penetrates past the reference face
            // Reconsider: reference face at a.max_x, incident at b.min_x
            // A point at x = b.min_x has depth = a.max_x - b.min_x = overlap
            // A point at x = a.max_x has depth = 0 (just touching)
            // A point beyond (x > a.max_x) has depth < 0 (not penetrating)
        } else {
            depth = coords[normal_axis] - ref_plane;
        }

        // Keep points that are penetrating or within margin
        if (depth >= -margin) {
            ContactPoint& cp = out.points[out.num_points];
            cp.px = buf1[i].x;
            cp.py = buf1[i].y;
            cp.pz = buf1[i].z;
            cp.penetration = depth;
            cp.point_id = out.num_points;  // Simple sequential ID
            out.num_points++;
        }
    }

    return out.num_points > 0;
}

// ============================================================================
// Sphere and sphere/box analytic handlers
// ============================================================================
// Normal convention matches narrow_phase_aabb: unit vector from B toward A
// so `pa += normal * impulse` in the solver pushes A out of B.

#include "../particle.h"

namespace {

AABB6 aabb_of_box_particle(const Particle& p) {
    float hw = p.width * 0.5f;
    float hh = p.height * 0.5f;
    float ht = p.thickness * 0.5f;
    return AABB6{p.x - hw, p.x + hw,
                 p.y - hh, p.y + hh,
                 p.z - ht, p.z + ht};
}

// Conservative AABB for ellipsoid: enclose the axis-scaled icosphere by
// its per-axis extents. Used by the v1 dispatcher as a fallback.
AABB6 aabb_of_ellipsoid_particle(const Particle& p) {
    float hw = p.width * 0.5f;
    float hh = p.height * 0.5f;
    float ht = p.thickness * 0.5f;
    return AABB6{p.x - hw, p.x + hw,
                 p.y - hh, p.y + hh,
                 p.z - ht, p.z + ht};
}

AABB6 aabb_of_sphere_particle(const Particle& p) {
    float r = p.size * 0.5f;
    return AABB6{p.x - r, p.x + r,
                 p.y - r, p.y + r,
                 p.z - r, p.z + r};
}

} // namespace

bool narrow_phase_sphere_sphere(
    float ax, float ay, float az, float ra,
    float bx, float by, float bz, float rb,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out)
{
    float dx = ax - bx;
    float dy = ay - by;
    float dz = az - bz;
    float d_sq = dx * dx + dy * dy + dz * dz;
    float r_sum = ra + rb;
    float r_with_margin = r_sum + margin;
    if (d_sq > r_with_margin * r_with_margin) return false;

    float d = std::sqrt(d_sq);
    float nx, ny, nz;
    if (d > 1e-6f) {
        nx = dx / d; ny = dy / d; nz = dz / d;
    } else {
        // Degenerate coincident centers — pick +Z as a deterministic default
        // (matches the "push up" tie-break used elsewhere for zero-distance
        // contacts and keeps the solver well-defined).
        nx = 0.0f; ny = 0.0f; nz = 1.0f;
    }

    float penetration = r_sum - d;  // negative = separated within margin

    // Contact point on A's surface in the -normal direction (the surface
    // point closest to B). Consistent with how narrow_phase_aabb reports
    // points on the reference face.
    float cx = ax - nx * ra;
    float cy = ay - ny * ra;
    float cz = az - nz * ra;

    out.body_a = id_a;
    out.body_b = id_b;
    out.normal_x = nx;
    out.normal_y = ny;
    out.normal_z = nz;
    out.num_points = 1;
    out.points[0].px = cx;
    out.points[0].py = cy;
    out.points[0].pz = cz;
    out.points[0].penetration = penetration;
    out.points[0].point_id = 0;
    out.is_face_contact = true;
    out.reference_axis = 0;
    out.reference_body = 0;
    return true;
}

bool narrow_phase_sphere_aabb(
    float cx, float cy, float cz, float r,
    const AABB6& box,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out)
{
    // Closest point on the axis-aligned box to the sphere center.
    float qx = std::max(box.min_x, std::min(cx, box.max_x));
    float qy = std::max(box.min_y, std::min(cy, box.max_y));
    float qz = std::max(box.min_z, std::min(cz, box.max_z));

    float dx = cx - qx;
    float dy = cy - qy;
    float dz = cz - qz;
    float d_sq = dx * dx + dy * dy + dz * dz;
    float r_with_margin = r + margin;
    if (d_sq > r_with_margin * r_with_margin) return false;

    float nx, ny, nz;
    float penetration;
    if (d_sq > 1e-12f) {
        float d = std::sqrt(d_sq);
        nx = dx / d; ny = dy / d; nz = dz / d;
        penetration = r - d;
    } else {
        // Sphere center is inside the box: pick the shallowest exit face.
        float pen_xmin = cx - box.min_x;
        float pen_xmax = box.max_x - cx;
        float pen_ymin = cy - box.min_y;
        float pen_ymax = box.max_y - cy;
        float pen_zmin = cz - box.min_z;
        float pen_zmax = box.max_z - cz;
        float min_pen = pen_xmin;
        nx = -1.0f; ny = 0.0f; nz = 0.0f;
        if (pen_xmax < min_pen) { min_pen = pen_xmax; nx =  1.0f; ny = 0.0f; nz = 0.0f; }
        if (pen_ymin < min_pen) { min_pen = pen_ymin; nx = 0.0f; ny = -1.0f; nz = 0.0f; }
        if (pen_ymax < min_pen) { min_pen = pen_ymax; nx = 0.0f; ny =  1.0f; nz = 0.0f; }
        if (pen_zmin < min_pen) { min_pen = pen_zmin; nx = 0.0f; ny = 0.0f; nz = -1.0f; }
        if (pen_zmax < min_pen) { min_pen = pen_zmax; nx = 0.0f; ny = 0.0f; nz =  1.0f; }
        penetration = r + min_pen;
    }

    out.body_a = id_a;
    out.body_b = id_b;
    out.normal_x = nx;
    out.normal_y = ny;
    out.normal_z = nz;
    out.num_points = 1;
    out.points[0].px = qx;
    out.points[0].py = qy;
    out.points[0].pz = qz;
    out.points[0].penetration = penetration;
    out.points[0].point_id = 0;
    out.is_face_contact = true;
    out.reference_axis = 0;  // sphere-box: not meaningful; solver doesn't rely on it
    out.reference_body = 1;  // box is the reference shape
    return true;
}

bool narrow_phase_sphere_obb(
    float cx, float cy, float cz, float r,
    const OBB& box,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out)
{
    // Sphere centre in the box's own frame: project the offset onto each
    // local axis, then clamp to the half extents. That clamped triple IS
    // the closest point, expressed locally.
    const float dx = cx - box.c[0], dy = cy - box.c[1], dz = cz - box.c[2];
    float local[3], clamped[3];
    for (int i = 0; i < 3; ++i) {
        local[i] = dx * box.axis[i][0] + dy * box.axis[i][1] + dz * box.axis[i][2];
        clamped[i] = std::max(-box.half[i], std::min(local[i], box.half[i]));
    }

    // Back to world, by rebuilding from the same axes.
    float qx = box.c[0], qy = box.c[1], qz = box.c[2];
    for (int i = 0; i < 3; ++i) {
        qx += clamped[i] * box.axis[i][0];
        qy += clamped[i] * box.axis[i][1];
        qz += clamped[i] * box.axis[i][2];
    }

    const float ex = cx - qx, ey = cy - qy, ez = cz - qz;
    const float d_sq = ex * ex + ey * ey + ez * ez;
    const float r_with_margin = r + margin;
    if (d_sq > r_with_margin * r_with_margin) return false;

    float nx, ny, nz, penetration;
    if (d_sq > 1e-12f) {
        const float d = std::sqrt(d_sq);
        nx = ex / d; ny = ey / d; nz = ez / d;   // from B toward A (INV-25)
        penetration = r - d;
    } else {
        // Centre inside the box: shallowest exit along a LOCAL axis, then
        // taken to world through that axis. The axis-aligned version picks
        // a world face here; the oriented one must not, or a deeply
        // penetrating sphere is pushed out sideways through the solid.
        int best = 0;
        float min_pen = box.half[0] - std::fabs(local[0]);
        float sign = (local[0] < 0.0f) ? -1.0f : 1.0f;
        for (int i = 1; i < 3; ++i) {
            const float pen = box.half[i] - std::fabs(local[i]);
            if (pen < min_pen) {
                min_pen = pen;
                best = i;
                sign = (local[i] < 0.0f) ? -1.0f : 1.0f;
            }
        }
        nx = sign * box.axis[best][0];
        ny = sign * box.axis[best][1];
        nz = sign * box.axis[best][2];
        penetration = r + min_pen;
    }

    out.body_a = id_a;
    out.body_b = id_b;
    out.normal_x = nx;
    out.normal_y = ny;
    out.normal_z = nz;
    out.num_points = 1;
    out.points[0].px = qx;
    out.points[0].py = qy;
    out.points[0].pz = qz;
    out.points[0].penetration = penetration;
    out.points[0].point_id = 0;
    out.is_face_contact = true;
    out.reference_axis = 0;
    out.reference_body = 1;   // the box is the reference shape
    return true;
}

// ============================================================================
// ORIENTED BOXES: SAT (15 axes) + reference-face clipping
// ============================================================================
// The AABB path above reads width/height/thickness as WORLD extents; that is
// exactly right for the unrotated boxes worldgen compensates by hand (roots,
// tiles) and exactly wrong for anything that carries rotation_x/y/z (blade
// segments, tree branches, tipped debris). This path reads the same extents
// along the particle's OWN axes. Normal convention is unchanged: unit vector
// from B toward A.

namespace {

inline float dot3(const float* a, const float* b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

inline void cross3(const float* a, const float* b, float* out) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

// Projected radius of an oriented box onto a unit axis L.
inline float obb_radius_on(const OBB& o, const float* L) {
    return o.half[0] * std::fabs(dot3(o.axis[0], L)) +
           o.half[1] * std::fabs(dot3(o.axis[1], L)) +
           o.half[2] * std::fabs(dot3(o.axis[2], L));
}

struct ClipV3 { float v[3]; };

// Clip a polygon against the half-space dot(n, x) <= limit.
// General-plane version of the axis-aligned clippers above.
int clip_halfspace(const ClipV3* in, int in_count, ClipV3* out,
                   const float* n, float limit) {
    if (in_count == 0) return 0;
    int out_count = 0;
    for (int i = 0; i < in_count && out_count < MAX_CLIP_VERTICES; i++) {
        int next = (i + 1) % in_count;
        const float di = dot3(in[i].v, n) - limit;
        const float dn = dot3(in[next].v, n) - limit;
        const bool i_inside = di <= 0.0f;
        const bool n_inside = dn <= 0.0f;
        if (i_inside && n_inside) {
            out[out_count++] = in[next];
        } else if (i_inside != n_inside) {
            const float t = di / (di - dn);
            ClipV3 x;
            for (int k = 0; k < 3; ++k)
                x.v[k] = in[i].v[k] + t * (in[next].v[k] - in[i].v[k]);
            out[out_count++] = x;
            if (n_inside && out_count < MAX_CLIP_VERTICES)
                out[out_count++] = in[next];
        }
    }
    return out_count;
}

} // namespace

bool box_particle_is_rotated(const Particle& p) {
    if (p.is_quat_driven) {
        // Unit quaternion: any orientation shows as |w| < 1.
        // 1 - |w| ~ theta^2 / 8; 1e-6 admits ~3 mrad and under as unrotated.
        return 1.0f - std::fabs(p.rotation_q.w) > QUAT_UNROTATED_EPS;
    }
    return std::fabs(p.rotation_x) > BOX_ROTATION_EPS ||
           std::fabs(p.rotation_y) > BOX_ROTATION_EPS ||
           std::fabs(p.rotation_z) > BOX_ROTATION_EPS;
}

OBB obb_of_box_particle(const Particle& p, float z_override) {
    OBB o;
    o.c[0] = p.x; o.c[1] = p.y; o.c[2] = z_override;
    o.half[0] = p.width * 0.5f;
    o.half[1] = p.height * 0.5f;
    o.half[2] = p.thickness * 0.5f;
    const logosphere::Quat q = p.is_quat_driven
        ? p.rotation_q
        : logosphere::Quat::from_euler(p.rotation_x, p.rotation_y, p.rotation_z);
    float m[9];
    q.to_matrix3x3(m);   // row-major; world = m * local
    for (int i = 0; i < 3; ++i) {   // local axis i = column i
        o.axis[i][0] = m[0 + i];
        o.axis[i][1] = m[3 + i];
        o.axis[i][2] = m[6 + i];
    }
    return o;
}

AABB6 aabb_of_obb(const OBB& o) {
    float e[3];
    for (int k = 0; k < 3; ++k)
        e[k] = o.half[0] * std::fabs(o.axis[0][k]) +
               o.half[1] * std::fabs(o.axis[1][k]) +
               o.half[2] * std::fabs(o.axis[2][k]);
    return AABB6{o.c[0] - e[0], o.c[0] + e[0],
                 o.c[1] - e[1], o.c[1] + e[1],
                 o.c[2] - e[2], o.c[2] + e[2]};
}

bool narrow_phase_obb(const OBB& a, const OBB& b,
                      size_t id_a, size_t id_b,
                      float margin,
                      ContactManifold& out) {
    out.num_points = 0;

    // Center offset, B to A. Overlap on a unit axis L is ra + rb - |d.L|;
    // negative down to -margin is a speculative near-contact, below that
    // the axis separates the pair.
    const float d[3] = {a.c[0] - b.c[0], a.c[1] - b.c[1], a.c[2] - b.c[2]};
    auto overlap_on = [&](const float* L) {
        return obb_radius_on(a, L) + obb_radius_on(b, L) - std::fabs(dot3(d, L));
    };

    // ---- 6 face axes ----
    float best_face_overlap = std::numeric_limits<float>::max();
    int best_face_body = -1, best_face_axis = -1;
    for (int body = 0; body < 2; ++body) {
        const OBB& box = (body == 0) ? a : b;
        for (int i = 0; i < 3; ++i) {
            const float ov = overlap_on(box.axis[i]);
            if (ov < -margin) return false;
            if (ov < best_face_overlap) {
                best_face_overlap = ov;
                best_face_body = body;
                best_face_axis = i;
            }
        }
    }

    // ---- 9 edge-cross axes ----
    float best_edge_overlap = std::numeric_limits<float>::max();
    int best_ea = -1, best_eb = -1;
    float best_edge_axis[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float L[3];
            cross3(a.axis[i], b.axis[j], L);
            const float len2 = dot3(L, L);
            if (len2 < 1e-8f) continue;   // near-parallel: face axes cover it
            const float inv = 1.0f / std::sqrt(len2);
            L[0] *= inv; L[1] *= inv; L[2] *= inv;
            const float ov = overlap_on(L);
            if (ov < -margin) return false;
            if (ov < best_edge_overlap) {
                best_edge_overlap = ov;
                best_ea = i; best_eb = j;
                best_edge_axis[0] = L[0];
                best_edge_axis[1] = L[1];
                best_edge_axis[2] = L[2];
            }
        }
    }

    out.body_a = id_a;
    out.body_b = id_b;

    // Prefer face axes: face manifolds carry up to 4 points and are stable
    // frame to frame. An edge axis wins only when clearly smaller (1 mm,
    // the SLOP scale) — sign-safe, unlike a relative factor, because
    // speculative overlaps can be negative.
    // A SEPARATED PAIR MEETS A FACE (night 2026-09-04, journal entry 3).
    // With every overlap negative, 'smallest' means 'most separated', and a
    // cross axis that separates the pair by 20 mm beat the face axis that
    // separated it by 10 mm: a swinging foot part crossing a tile's edge got
    // n = (0, -0.92, 0.40) for one frame and a 1.28 N s speculative shove
    // (test_tile_sticking, physics frame 140). Two boxes approaching from a
    // distance meet on a face; the cross axes are for penetration, where the
    // separating-axis theorem is the rule. SEAM_OBB_FACES_FIRST=0 restores the
    // old choice for A/B.
    static const bool faces_first = [] {
        const char* v = std::getenv("SEAM_OBB_FACES_FIRST");
        return !(v && v[0] == '0');
    }();
    const bool use_edge = (best_ea >= 0) &&
                          (!faces_first || best_face_overlap > 0.0f) &&
                          (best_edge_overlap + EDGE_TOL < best_face_overlap);

    if (use_edge) {
        // ---- edge-edge: single point between the two closest edges ----
        float n[3] = {best_edge_axis[0], best_edge_axis[1], best_edge_axis[2]};
        if (dot3(d, n) < 0.0f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        // n now points from B toward A (the output convention).

        // Supporting edge on A: runs along a.axis[best_ea], displaced toward
        // B (i.e. along -n) on the other two axes. Mirror for B toward A.
        float pa[3] = {a.c[0], a.c[1], a.c[2]};
        for (int k = 0; k < 3; ++k) {
            if (k == best_ea) continue;
            const float s = (dot3(a.axis[k], n) > 0.0f) ? -1.0f : 1.0f;
            for (int q = 0; q < 3; ++q) pa[q] += s * a.half[k] * a.axis[k][q];
        }
        float pb[3] = {b.c[0], b.c[1], b.c[2]};
        for (int k = 0; k < 3; ++k) {
            if (k == best_eb) continue;
            const float s = (dot3(b.axis[k], n) > 0.0f) ? 1.0f : -1.0f;
            for (int q = 0; q < 3; ++q) pb[q] += s * b.half[k] * b.axis[k][q];
        }

        // Closest points of the two edge lines, clamped to the edges.
        const float* e1 = a.axis[best_ea];
        const float* e2 = b.axis[best_eb];
        const float r[3] = {pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2]};
        const float a12 = dot3(e1, e2);
        const float denom = 1.0f - a12 * a12;   // > 1e-8 by the cross check
        float s = (a12 * dot3(e2, r) - dot3(e1, r)) / denom;
        float t = (dot3(e2, r) - a12 * dot3(e1, r)) / denom;
        s = std::max(-a.half[best_ea], std::min(a.half[best_ea], s));
        t = std::max(-b.half[best_eb], std::min(b.half[best_eb], t));

        out.normal_x = n[0]; out.normal_y = n[1]; out.normal_z = n[2];
        out.is_face_contact = false;
        out.reference_body = 0;
        // Dominant world component, for parity with the AABB path's field.
        out.reference_axis =
            (std::fabs(n[0]) >= std::fabs(n[1]) && std::fabs(n[0]) >= std::fabs(n[2])) ? 0 :
            (std::fabs(n[1]) >= std::fabs(n[2])) ? 1 : 2;
        out.num_points = 1;
        ContactPoint& cp = out.points[0];
        cp.px = 0.5f * ((pa[0] + s * e1[0]) + (pb[0] + t * e2[0]));
        cp.py = 0.5f * ((pa[1] + s * e1[1]) + (pb[1] + t * e2[1]));
        cp.pz = 0.5f * ((pa[2] + s * e1[2]) + (pb[2] + t * e2[2]));
        cp.penetration = best_edge_overlap;
        cp.point_id = 0;
        return true;
    }

    // ---- face contact: clip the incident face against the reference face ----
    const OBB& ref = (best_face_body == 0) ? a : b;
    const OBB& inc = (best_face_body == 0) ? b : a;
    const float* u = ref.axis[best_face_axis];

    // Reference face: the one facing the incident box. Its outward normal.
    const float to_inc[3] = {inc.c[0] - ref.c[0], inc.c[1] - ref.c[1],
                             inc.c[2] - ref.c[2]};
    const float s_ref = (dot3(to_inc, u) >= 0.0f) ? 1.0f : -1.0f;
    const float nref[3] = {s_ref * u[0], s_ref * u[1], s_ref * u[2]};

    // Output normal points from B toward A. The reference face's outward
    // normal points toward the OTHER box: away from A when A is reference,
    // toward A when B is.
    const float sign_out = (best_face_body == 0) ? -1.0f : 1.0f;
    out.normal_x = sign_out * nref[0];
    out.normal_y = sign_out * nref[1];
    out.normal_z = sign_out * nref[2];
    out.is_face_contact = true;
    out.reference_body = best_face_body;
    {
        const float ax = std::fabs(out.normal_x), ay = std::fabs(out.normal_y),
                    az = std::fabs(out.normal_z);
        out.reference_axis = (ax >= ay && ax >= az) ? 0 : (ay >= az) ? 1 : 2;
    }

    // Incident face: the face of `inc` most anti-parallel to nref.
    int ii = 0;
    {
        float best_align = -1.0f;
        for (int i = 0; i < 3; ++i) {
            const float al = std::fabs(dot3(inc.axis[i], nref));
            if (al > best_align) { best_align = al; ii = i; }
        }
    }
    const float t_inc = (dot3(inc.axis[ii], nref) > 0.0f) ? -1.0f : 1.0f;
    const int ip = (ii + 1) % 3, iq = (ii + 2) % 3;
    float fc[3];
    for (int k = 0; k < 3; ++k)
        fc[k] = inc.c[k] + t_inc * inc.half[ii] * inc.axis[ii][k];

    ClipV3 buf1[MAX_CLIP_VERTICES];
    ClipV3 buf2[MAX_CLIP_VERTICES];
    {   // incident face quad, wound as a cycle
        const float hp = inc.half[ip], hq = inc.half[iq];
        const float* ap = inc.axis[ip];
        const float* aq = inc.axis[iq];
        const float sp[4] = {+1, -1, -1, +1};
        const float sq[4] = {+1, +1, -1, -1};
        for (int v = 0; v < 4; ++v)
            for (int k = 0; k < 3; ++k)
                buf1[v].v[k] = fc[k] + sp[v] * hp * ap[k] + sq[v] * hq * aq[k];
    }
    int count = 4;

    // Side planes of the reference face: dot(+-axis, x) <= dot(+-axis, c) + half
    const int rp = (best_face_axis + 1) % 3, rq = (best_face_axis + 2) % 3;
    const int side_axes[2] = {rp, rq};
    for (int siv = 0; siv < 2 && count > 0; ++siv) {
        const float* n_side = ref.axis[side_axes[siv]];
        const float dc = dot3(n_side, ref.c);
        const float h = ref.half[side_axes[siv]];
        count = clip_halfspace(buf1, count, buf2, n_side, dc + h);
        const float neg[3] = {-n_side[0], -n_side[1], -n_side[2]};
        count = clip_halfspace(buf2, count, buf1, neg, -(dc - h));
    }

    // Admit clipped points behind (or within margin of) the reference face.
    const float face_d = dot3(nref, ref.c) + ref.half[best_face_axis];
    struct Cand { float v[3]; float depth; };
    Cand cands[MAX_CLIP_VERTICES];
    int n_cand = 0;
    for (int i = 0; i < count; ++i) {
        const float depth = face_d - dot3(nref, buf1[i].v);
        if (depth >= -margin) {
            cands[n_cand].v[0] = buf1[i].v[0];
            cands[n_cand].v[1] = buf1[i].v[1];
            cands[n_cand].v[2] = buf1[i].v[2];
            cands[n_cand].depth = depth;
            n_cand++;
        }
    }

    if (n_cand == 0) {
        // Numerically thinned manifold (grazing pose): fall back to the
        // incident box's deepest vertex along the reference normal.
        float sp[3] = {inc.c[0], inc.c[1], inc.c[2]};
        for (int k = 0; k < 3; ++k) {
            const float s = (dot3(inc.axis[k], nref) > 0.0f) ? -1.0f : 1.0f;
            for (int q = 0; q < 3; ++q) sp[q] += s * inc.half[k] * inc.axis[k][q];
        }
        const float depth = face_d - dot3(nref, sp);
        if (depth < -margin) return false;
        out.num_points = 1;
        out.points[0].px = sp[0];
        out.points[0].py = sp[1];
        out.points[0].pz = sp[2];
        out.points[0].penetration = depth;
        out.points[0].point_id = 0;
        return true;
    }

    // Reduce to 4. Two laws, levered (G-51, MANIFOLD_SPAN=1, default off
    // until the flip ruling):
    //
    // DEFAULT, deepest-4: keeps the 4 deepest of at most 8. In the pose
    // every stack lives in — near-parallel faces whose depths differ by
    // numerical noise across a metre of face — the 4 deepest all sit on
    // the downhill edge of a micro-tilt, so the support centroid lands
    // up to 0.45 m off-centre and the warm start's evenly-split cached
    // support becomes a torque battery with alternating sign as the
    // injection flips the tilt (the G-48 stack pump, witnessed at
    // substep granularity; derivation and numbers in GEDANKEN-51).
    //
    // LEVER, spanning reduction: the reduced set must SPAN the contact
    // polygon. Keep the deepest point (it owns the position error), the
    // point farthest from it, then the points of maximal signed area on
    // each side of that chord — pure argmax over geometry, stable
    // whenever the clip polygon is stable, even when its depth ordering
    // is not.
    // THE FLIP (INV-36, 2026-09-01): the spanning reduction is the shipped
    // default; MANIFOLD_SPAN=0 is the kill switch, never a shipping mode.
    static const bool manifold_span = []{
        const char* e = std::getenv("MANIFOLD_SPAN");
        return !(e && e[0] == '0' && e[1] == '\0');
    }();
    if (n_cand > 4 && manifold_span) {
        int i0 = 0;
        for (int j = 1; j < n_cand; ++j)
            if (cands[j].depth > cands[i0].depth) i0 = j;
        int i1 = -1;
        float best_d2 = -1.0f;
        for (int j = 0; j < n_cand; ++j) {
            if (j == i0) continue;
            const float dx = cands[j].v[0] - cands[i0].v[0];
            const float dy = cands[j].v[1] - cands[i0].v[1];
            const float dz = cands[j].v[2] - cands[i0].v[2];
            const float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 > best_d2) { best_d2 = d2; i1 = j; }
        }
        const float ex = cands[i1].v[0] - cands[i0].v[0];
        const float ey = cands[i1].v[1] - cands[i0].v[1];
        const float ez = cands[i1].v[2] - cands[i0].v[2];
        int i2 = -1, i3 = -1;
        float best_pos = 0.0f, best_neg = 0.0f;
        for (int j = 0; j < n_cand; ++j) {
            if (j == i0 || j == i1) continue;
            const float fx = cands[j].v[0] - cands[i0].v[0];
            const float fy = cands[j].v[1] - cands[i0].v[1];
            const float fz = cands[j].v[2] - cands[i0].v[2];
            // signed area in the face plane: (e x f) . nref
            const float s = (ey*fz - ez*fy) * nref[0]
                          + (ez*fx - ex*fz) * nref[1]
                          + (ex*fy - ey*fx) * nref[2];
            if (s > best_pos) { best_pos = s; i2 = j; }
            if (s < best_neg) { best_neg = s; i3 = j; }
        }
        // A collinear side (no area either way) falls back to the point
        // farthest from the chord's midpoint, so the pick stays
        // deterministic and 4 points always survive.
        for (int* slot : {&i2, &i3}) {
            if (*slot >= 0) continue;
            const float mx = cands[i0].v[0] + 0.5f * ex;
            const float my = cands[i0].v[1] + 0.5f * ey;
            const float mz = cands[i0].v[2] + 0.5f * ez;
            float far_d2 = -1.0f;
            for (int j = 0; j < n_cand; ++j) {
                if (j == i0 || j == i1 || j == i2 || j == i3) continue;
                const float dx = cands[j].v[0] - mx;
                const float dy = cands[j].v[1] - my;
                const float dz = cands[j].v[2] - mz;
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > far_d2) { far_d2 = d2; *slot = j; }
            }
        }
        Cand kept[4] = {cands[i0], cands[i1], cands[i2], cands[i3]};
        for (int i = 0; i < 4; ++i) cands[i] = kept[i];
        n_cand = 4;
    } else if (n_cand > 4) {
        for (int i = 0; i < 4; ++i) {
            int deepest = i;
            for (int j = i + 1; j < n_cand; ++j)
                if (cands[j].depth > cands[deepest].depth) deepest = j;
            const Cand tmp = cands[i];
            cands[i] = cands[deepest];
            cands[deepest] = tmp;
        }
        n_cand = 4;
    }

    out.num_points = n_cand;
    for (int i = 0; i < n_cand; ++i) {
        out.points[i].px = cands[i].v[0];
        out.points[i].py = cands[i].v[1];
        out.points[i].pz = cands[i].v[2];
        out.points[i].penetration = cands[i].depth;
        out.points[i].point_id = i;
    }
    return true;
}

bool narrow_phase_particle_pair(
    const Particle& a, const Particle& b,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out)
{
    bool a_sphere = a.shape == ParticleShape::SPHERE;
    bool b_sphere = b.shape == ParticleShape::SPHERE;
    bool a_box    = a.shape == ParticleShape::BOX;
    bool b_box    = b.shape == ParticleShape::BOX;

    if (a_sphere && b_sphere) {
        return narrow_phase_sphere_sphere(
            a.x, a.y, a.z, a.size * 0.5f,
            b.x, b.y, b.z, b.size * 0.5f,
            id_a, id_b, margin, out);
    }
    if (a_sphere && b_box) {
        // A rotated box is a DIFFERENT SOLID from its world-axis extents,
        // not merely a differently-oriented one. Handing the axis-aligned
        // path a tilted ramp let a sphere fall 1.907 m through the face and
        // rest on the unrotated box's flat top for ever (INV-2, measured in
        // tests/test_ramp_race.cpp). Unrotated boxes keep the raw extents,
        // bit-identical, on purpose.
        if (box_particle_is_rotated(b)) {
            return narrow_phase_sphere_obb(
                a.x, a.y, a.z, a.size * 0.5f,
                obb_of_box_particle(b, b.z),
                id_a, id_b, margin, out);
        }
        return narrow_phase_sphere_aabb(
            a.x, a.y, a.z, a.size * 0.5f,
            aabb_of_box_particle(b),
            id_a, id_b, margin, out);
    }
    if (a_box && b_sphere) {
        // Run with roles swapped (sphere as A), then re-label and flip normal
        // so the caller still sees (body_a = id_a, body_b = id_b).
        bool hit = box_particle_is_rotated(a)
            ? narrow_phase_sphere_obb(
                  b.x, b.y, b.z, b.size * 0.5f,
                  obb_of_box_particle(a, a.z),
                  id_b, id_a, margin, out)
            : narrow_phase_sphere_aabb(
                  b.x, b.y, b.z, b.size * 0.5f,
                  aabb_of_box_particle(a),
                  id_b, id_a, margin, out);
        if (!hit) return false;
        out.body_a = id_a;
        out.body_b = id_b;
        out.normal_x = -out.normal_x;
        out.normal_y = -out.normal_y;
        out.normal_z = -out.normal_z;
        return true;
    }

    // Fallback for ELLIPSOID and any other pair: conservative AABB SAT.
    auto aabb_for = [](const Particle& p) {
        switch (p.shape) {
            case ParticleShape::SPHERE:    return aabb_of_sphere_particle(p);
            case ParticleShape::ELLIPSOID: return aabb_of_ellipsoid_particle(p);
            case ParticleShape::BOX:
            default:                        return aabb_of_box_particle(p);
        }
    };
    return narrow_phase_aabb(aabb_for(a), aabb_for(b), id_a, id_b, margin, out);
}
