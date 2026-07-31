#include "logosphere/rendering/vision_cone_occlusion.h"

#include <cmath>

namespace logosphere::rendering {

namespace {

// Ray-vs-line-segment intersection. Solves for the ray parameter
// t > 0 at the first hit; returns +∞ (huge value) if no hit.
//
// Ray:     P + t * D,         t ∈ [0, ∞)
// Segment: A + s * (B - A),   s ∈ [0, 1]
//
// Equation: P + t*D = A + s*(B-A)
//   t*Dx - s*(Bx-Ax) = Ax - Px
//   t*Dy - s*(By-Ay) = Ay - Py
//
// Solved with Cramer's rule. Parallel rays (det ≈ 0) miss.
float ray_to_segment(float px, float py, float dx, float dy,
                     float ax, float ay, float bx, float by) {
    constexpr float kHuge   = 1e30f;
    constexpr float kEps    = 1e-9f;
    constexpr float kMinT   = 1e-4f;  // skip hits at the ray origin
    float ex = bx - ax;
    float ey = by - ay;
    float det = dx * (-ey) - dy * (-ex);  // = dy*ex - dx*ey
    if (std::fabs(det) < kEps) return kHuge;
    float rx = ax - px;
    float ry = ay - py;
    float t = (rx * (-ey) - ry * (-ex)) / det;  // = (ry*ex - rx*ey)/det
    float s = (dx *   ry  - dy *   rx) / det;
    if (t < kMinT)            return kHuge;
    if (s < 0.0f || s > 1.0f) return kHuge;
    return t;
}

// Is point P inside the oriented bounding box that represents
// `seg` thickened by half_thick? Catches the "viewer is on/inside
// a wall" case so we don't return the far-side OBB exit as a
// phantom forward occluder.
bool point_inside_thick_segment(float px, float py,
                                const OccluderSegment& seg) {
    float tx = seg.bx - seg.ax;
    float ty = seg.by - seg.ay;
    float len_sq = tx * tx + ty * ty;
    if (len_sq < 1e-12f) {
        // Degenerate segment — treat as a disc of radius half_thick.
        float dxp = px - seg.ax;
        float dyp = py - seg.ay;
        return (dxp * dxp + dyp * dyp) <= seg.half_thick * seg.half_thick;
    }
    // Project (P - A) onto (B - A) / |B - A|² to find the parametric
    // position of P's perpendicular projection onto the segment line.
    float u = ((px - seg.ax) * tx + (py - seg.ay) * ty) / len_sq;
    if (u < 0.0f || u > 1.0f) return false;  // perpendicular foot is past either end
    // Perpendicular distance from P to the segment line.
    float cx = seg.ax + u * tx;
    float cy = seg.ay + u * ty;
    float dxp = px - cx;
    float dyp = py - cy;
    return (dxp * dxp + dyp * dyp) <= seg.half_thick * seg.half_thick;
}

// Build the four edges of the oriented bounding box that represents
// `seg` thickened by half_thick perpendicularly, then return the
// minimum ray-t across those edges. Generic — works for any
// orientation, not just axis-aligned segments.
//
// Returns +∞ if the ray's ORIGIN is already inside the OBB. Without
// this guard the ray would hit the OBB's far-side edge and report a
// spurious "occluder X meters in front" — exactly the bug the
// `wall_through_viewer_is_not_zero_distance` test catches (the
// player riding along their own just-laid trail).
float ray_to_thick_segment(float px, float py, float dx, float dy,
                           const OccluderSegment& seg) {
    constexpr float kHuge = 1e30f;

    if (point_inside_thick_segment(px, py, seg)) return kHuge;

    float tx = seg.bx - seg.ax;
    float ty = seg.by - seg.ay;
    float len = std::sqrt(tx * tx + ty * ty);
    if (len < 1e-6f) return kHuge;

    // Normalized perpendicular (engine convention: rotate +90° CW
    // around +Z viewed from above is (-ty, tx) / len; sign doesn't
    // affect the OBB since we offset both ways).
    float nx = -ty / len;
    float ny =  tx / len;
    float ox = nx * seg.half_thick;
    float oy = ny * seg.half_thick;

    // Four corners of the OBB.
    float p0x = seg.ax + ox, p0y = seg.ay + oy;  // A + n
    float p1x = seg.bx + ox, p1y = seg.by + oy;  // B + n
    float p2x = seg.bx - ox, p2y = seg.by - oy;  // B - n
    float p3x = seg.ax - ox, p3y = seg.ay - oy;  // A - n

    float t = kHuge;
    auto consider = [&](float ax, float ay, float bx, float by) {
        float u = ray_to_segment(px, py, dx, dy, ax, ay, bx, by);
        if (u < t) t = u;
    };
    consider(p0x, p0y, p1x, p1y);   // top edge
    consider(p1x, p1y, p2x, p2y);   // right cap
    consider(p2x, p2y, p3x, p3y);   // bottom edge
    consider(p3x, p3y, p0x, p0y);   // left cap

    return t;
}

} // namespace

void compute_vision_cone_occlusion(
    const OccluderSegment* segments, int segment_count,
    float viewer_x, float viewer_y,
    float look_direction, float half_fov, float range,
    float* out_distances, int bin_count)
{
    if (!out_distances || bin_count <= 0) return;
    if (range <= 0.0f) {
        for (int i = 0; i < bin_count; ++i) out_distances[i] = 0.0f;
        return;
    }

    const float bin_arc = (2.0f * half_fov) / static_cast<float>(bin_count);
    const float arc_start = look_direction - half_fov;

    for (int i = 0; i < bin_count; ++i) {
        // Bin center direction. Engine convention: yaw=0 is +Y, so
        // direction vector is (sin, cos) of the bin angle.
        float ang = arc_start + (static_cast<float>(i) + 0.5f) * bin_arc;
        float dx  = std::sin(ang);
        float dy  = std::cos(ang);

        float best_t = 1.0f;  // 1.0 = ray reaches its full `range`
        for (int s = 0; s < segment_count; ++s) {
            // ray_to_thick_segment returns t in WORLD units (not
            // ray-parametric u in [0,1]) because our ray direction
            // is a unit vector — so t is distance directly.
            float t = ray_to_thick_segment(viewer_x, viewer_y,
                                           dx, dy, segments[s]);
            if (t < best_t * range) {
                best_t = t / range;
            }
        }
        // best_t * range = distance to nearest hit (or `range` if none).
        out_distances[i] = best_t * range;
    }
}

} // namespace logosphere::rendering
