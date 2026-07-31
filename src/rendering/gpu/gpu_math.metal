#ifndef GPU_MATH_METAL
#define GPU_MATH_METAL

#include <metal_stdlib>
#include "gpu_constants.metal"
#include "gpu_types.metal"
using namespace metal;

// =========================================================================
// GPU MATH HELPERS - Single Source of Truth
// =========================================================================
// All mathematical helper functions for GPU compute shaders
// All functions marked 'inline' for zero-overhead abstraction

// =========================================================================
// MÖLLER-TRUMBORE RAY-TRIANGLE INTERSECTION
// =========================================================================
// Returns: true if ray hits triangle BETWEEN surface and light
// Algorithm: Möller-Trumbore (1997) - "Fast, Minimum Storage Ray/Triangle Intersection"
// CRITICAL: Checks max_distance to prevent "mirror shadow" bug
//
// Mirror Shadow Bug (2025-10-06):
// - Without max_distance check, rays extend infinitely past light
// - Hit objects on OPPOSITE side of light (behind light)
// - Created impossible shadows (objects behind light cast shadows in front!)
// - FIX: Only count intersections where RAY_MIN_T < t < max_distance

inline bool ray_intersects_triangle(
    float3 ray_origin,
    float3 ray_direction,
    float max_distance,  // Distance from surface to light source
    float3 v0,
    float3 v1,
    float3 v2)
{
    // Möller-Trumbore algorithm
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(ray_direction, edge2);
    float det = dot(edge1, pvec);

    // Parallel check
    if (abs(det) < RAY_EPSILON) {
        return false;  // Ray parallel to triangle
    }

    float inv_det = 1.0f / det;
    float3 tvec = ray_origin - v0;
    float u = dot(tvec, pvec) * inv_det;

    // u bounds check (barycentric coordinate)
    if (u < 0.0f || u > 1.0f) {
        return false;  // Outside triangle
    }

    float3 qvec = cross(tvec, edge1);
    float v = dot(ray_direction, qvec) * inv_det;

    // v bounds check (barycentric coordinate)
    if (v < 0.0f || u + v > 1.0f) {
        return false;  // Outside triangle
    }

    // t = distance along ray to intersection point
    float t = dot(edge2, qvec) * inv_det;

    // CRITICAL FIX (2025-10-06): Check BOTH min and max distance
    //
    // OLD BUG: return (t > RAY_MIN_T);
    // - Rays extended infinitely, hitting objects PAST the light
    // - Created "mirror shadows" on opposite side of light
    //
    // NEW FIX: return (t > RAY_MIN_T && t < max_distance);
    // - Rays stop at light source (t = max_distance)
    // - Objects past light are ignored (physically correct)
    //
    // CASES:
    // - t < RAY_MIN_T:          Self-intersection (surface shadowing itself) → IGNORE
    // - RAY_MIN_T < t < max_dist: Object between surface and light → SHADOW (TRUE)
    // - t >= max_dist:           Object PAST light (opposite side) → IGNORE (FALSE)
    //
    return (t > RAY_MIN_T && t < max_distance);  // Hit only if between surface and light
}

// =========================================================================
// RAY-AABB INTERSECTION (Branchless Slab Method)
// =========================================================================
// Returns: true if ray intersects axis-aligned bounding box within max_t
// Algorithm: Branchless slab method using float3 SIMD ops (no per-axis loop)
// Used by: BVH traversal to skip entire subtrees
//
// max_t clamps the ray to the light distance, pruning BVH subtrees that
// are entirely past the light source (physically can't cast shadows).
//
// Performance: ~2-3x faster than loop+ternary version on Metal GPU due to
// SIMD vectorization and elimination of branch divergence in SIMD groups.

inline bool ray_intersects_aabb(
    float3 ray_origin,
    float3 ray_direction,
    float3 bbox_min,
    float3 bbox_max,
    float max_t)
{
    // Inverse direction (handles inf correctly for axis-parallel rays)
    float3 inv_dir = 1.0f / ray_direction;

    // Slab entry/exit distances for all 3 axes simultaneously
    float3 t1 = (bbox_min - ray_origin) * inv_dir;
    float3 t2 = (bbox_max - ray_origin) * inv_dir;

    // Per-axis min/max (handles negative direction components)
    float3 tmin3 = min(t1, t2);
    float3 tmax3 = max(t1, t2);

    // Global entry = max of per-axis entries (must enter ALL slabs)
    // Global exit  = min of per-axis exits  (must exit through FIRST slab)
    float tmin = max3(tmin3.x, tmin3.y, tmin3.z);
    float tmax = min3(tmax3.x, tmax3.y, tmax3.z);

    // Clamp: ray starts at 0 (surface), ends at max_t (light)
    tmin = max(tmin, 0.0f);
    tmax = min(tmax, max_t);

    return tmax >= tmin;
}

// Legacy overload without distance clamping (for non-shadow uses)
inline bool ray_intersects_aabb(
    float3 ray_origin,
    float3 ray_direction,
    float3 bbox_min,
    float3 bbox_max)
{
    return ray_intersects_aabb(ray_origin, ray_direction, bbox_min, bbox_max, RAY_MAX_T);
}

// =========================================================================
// CLOSEST-HIT RAY-TRIANGLE INTERSECTION (for indirect lighting)
// =========================================================================
// Returns: hit distance t (negative if no hit)
// Same Möller-Trumbore algorithm but returns t instead of bool
// Used by indirect rays that need the closest surface (not just any-hit)

inline float ray_intersects_triangle_closest(
    float3 ray_origin,
    float3 ray_direction,
    float max_distance,
    float3 v0,
    float3 v1,
    float3 v2)
{
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(ray_direction, edge2);
    float det = dot(edge1, pvec);

    if (abs(det) < RAY_EPSILON) {
        return -1.0f;
    }

    float inv_det = 1.0f / det;
    float3 tvec = ray_origin - v0;
    float u = dot(tvec, pvec) * inv_det;

    if (u < 0.0f || u > 1.0f) {
        return -1.0f;
    }

    float3 qvec = cross(tvec, edge1);
    float v = dot(ray_direction, qvec) * inv_det;

    if (v < 0.0f || u + v > 1.0f) {
        return -1.0f;
    }

    float t = dot(edge2, qvec) * inv_det;

    if (t > RAY_MIN_T && t < max_distance) {
        return t;
    }
    return -1.0f;
}

// =========================================================================
// FUTURE: RASTERIZATION HELPERS (GPU_RENDERING_FULL.md)
// =========================================================================
// When implementing GPU rasterization, add these functions here:
//
// // Edge function for triangle rasterization (2D cross product)
// inline float edge_function(float2 a, float2 b, float2 p) {
//     return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
// }
//
// // Barycentric coordinates for triangle
// inline float3 barycentric_coords(float2 v0, float2 v1, float2 v2, float2 p) {
//     float area = edge_function(v0, v1, v2);
//     float w0 = edge_function(v1, v2, p) / area;
//     float w1 = edge_function(v2, v0, p) / area;
//     float w2 = edge_function(v0, v1, p) / area;
//     return float3(w0, w1, w2);
// }
//
// // Depth interpolation using barycentric coordinates
// inline float interpolate_depth(float3 bary, float3 depths) {
//     return bary.x * depths.x + bary.y * depths.y + bary.z * depths.z;
// }

#endif // GPU_MATH_METAL
