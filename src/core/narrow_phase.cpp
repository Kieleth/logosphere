#include "logosphere/physics/narrow_phase.h"
#include <algorithm>
#include <cmath>
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

static constexpr int MAX_CLIP_VERTICES = 8;  // 4 original + up to 4 from clipping

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
    constexpr float BOUNDARY_EPSILON = 0.002f;
    constexpr float ALIGNED_PENALTY = 1e6f;

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
        return narrow_phase_sphere_aabb(
            a.x, a.y, a.z, a.size * 0.5f,
            aabb_of_box_particle(b),
            id_a, id_b, margin, out);
    }
    if (a_box && b_sphere) {
        // Run with roles swapped (sphere as A), then re-label and flip normal
        // so the caller still sees (body_a = id_a, body_b = id_b).
        bool hit = narrow_phase_sphere_aabb(
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
