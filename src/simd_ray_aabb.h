#ifndef SIMD_RAY_AABB_H
#define SIMD_RAY_AABB_H

#include "logosphere/physics/bvh.h"
#include <algorithm>
#include <cmath>

// Platform detection and SIMD headers
#ifdef __ARM_NEON
    #include <arm_neon.h>
    #define HAS_NEON 1
#elif defined(__AVX2__)
    #include <immintrin.h>
    #define HAS_AVX2 1
#elif defined(__SSE2__)
    #include <emmintrin.h>
    #define HAS_SSE2 1
#endif

// SIMD-optimized ray-AABB intersection for BVH traversal
// Tests TWO AABBs against ONE ray simultaneously
namespace SIMD {

// Main entry point - automatically selects best implementation
// Returns bitmask: bit 0 = left hit, bit 1 = right hit
inline int test_ray_dual_aabb(
    float ray_origin_x, float ray_origin_y, float ray_origin_z,
    float ray_dir_x, float ray_dir_y, float ray_dir_z,
    const AABB& box_left, const AABB& box_right,
    float max_t,
    float& out_tmin_left, float& out_tmin_right)
{
#ifdef HAS_NEON
    // ARM NEON implementation (Apple Silicon)
    // NEON processes 4 floats at a time
    
    // Compute inverse direction once (shared for both boxes)
    float inv_dir_x = (ray_dir_x != 0.0f) ? 1.0f / ray_dir_x : 1e30f;
    float inv_dir_y = (ray_dir_y != 0.0f) ? 1.0f / ray_dir_y : 1e30f;
    float inv_dir_z = (ray_dir_z != 0.0f) ? 1.0f / ray_dir_z : 1e30f;
    
    // Pack bounds for X axis: [left_min_x, right_min_x, left_max_x, right_max_x]
    float32x4_t bounds_x = {box_left.min_x, box_right.min_x, box_left.max_x, box_right.max_x};
    float32x4_t origin_x = vdupq_n_f32(ray_origin_x);
    float32x4_t inv_x = vdupq_n_f32(inv_dir_x);
    
    // t = (bound - origin) * inv_dir
    float32x4_t t_x = vmulq_f32(vsubq_f32(bounds_x, origin_x), inv_x);
    
    // Same for Y axis
    float32x4_t bounds_y = {box_left.min_y, box_right.min_y, box_left.max_y, box_right.max_y};
    float32x4_t origin_y = vdupq_n_f32(ray_origin_y);
    float32x4_t inv_y = vdupq_n_f32(inv_dir_y);
    float32x4_t t_y = vmulq_f32(vsubq_f32(bounds_y, origin_y), inv_y);
    
    // Same for Z axis
    float32x4_t bounds_z = {box_left.min_z, box_right.min_z, box_left.max_z, box_right.max_z};
    float32x4_t origin_z = vdupq_n_f32(ray_origin_z);
    float32x4_t inv_z = vdupq_n_f32(inv_dir_z);
    float32x4_t t_z = vmulq_f32(vsubq_f32(bounds_z, origin_z), inv_z);
    
    // Extract t values
    float tx[4], ty[4], tz[4];
    vst1q_f32(tx, t_x);
    vst1q_f32(ty, t_y);
    vst1q_f32(tz, t_z);
    
    // Process left box (indices 0 and 2)
    float tmin_left = std::min(tx[0], tx[2]);  // min/max for X
    float tmax_left = std::max(tx[0], tx[2]);
    
    float ty_min = std::min(ty[0], ty[2]);     // min/max for Y
    float ty_max = std::max(ty[0], ty[2]);
    tmin_left = std::max(tmin_left, ty_min);
    tmax_left = std::min(tmax_left, ty_max);
    
    float tz_min = std::min(tz[0], tz[2]);     // min/max for Z
    float tz_max = std::max(tz[0], tz[2]);
    tmin_left = std::max(tmin_left, tz_min);
    tmax_left = std::min(tmax_left, tz_max);
    
    // Process right box (indices 1 and 3)
    float tmin_right = std::min(tx[1], tx[3]);  // min/max for X
    float tmax_right = std::max(tx[1], tx[3]);
    
    ty_min = std::min(ty[1], ty[3]);            // min/max for Y
    ty_max = std::max(ty[1], ty[3]);
    tmin_right = std::max(tmin_right, ty_min);
    tmax_right = std::min(tmax_right, ty_max);
    
    tz_min = std::min(tz[1], tz[3]);            // min/max for Z
    tz_max = std::max(tz[1], tz[3]);
    tmin_right = std::max(tmin_right, tz_min);
    tmax_right = std::min(tmax_right, tz_max);
    
    // Store entry distances for front-to-back ordering
    out_tmin_left = tmin_left;
    out_tmin_right = tmin_right;
    
    // Check hits
    int result = 0;
    if (tmax_left >= tmin_left && tmin_left <= max_t && tmax_left >= 0.0f) {
        result |= 1;  // Left hit
    }
    if (tmax_right >= tmin_right && tmin_right <= max_t && tmax_right >= 0.0f) {
        result |= 2;  // Right hit
    }
    
    return result;
    
#elif defined(HAS_AVX2)
    // AVX2 implementation (x86-64)
    // Similar to NEON but using AVX2 intrinsics
    // ... (implementation would go here if needed)
    // For now, fall back to scalar
    goto scalar_fallback;
    
#else
scalar_fallback:
    // Scalar fallback - just do two separate tests
    // This ensures the code compiles and runs on all platforms
    
    // Compute inverse direction once
    float inv_dir_x = (ray_dir_x != 0.0f) ? 1.0f / ray_dir_x : 1e30f;
    float inv_dir_y = (ray_dir_y != 0.0f) ? 1.0f / ray_dir_y : 1e30f;
    float inv_dir_z = (ray_dir_z != 0.0f) ? 1.0f / ray_dir_z : 1e30f;
    
    // Test left box
    float t1 = (box_left.min_x - ray_origin_x) * inv_dir_x;
    float t2 = (box_left.max_x - ray_origin_x) * inv_dir_x;
    float tmin_left = std::min(t1, t2);
    float tmax_left = std::max(t1, t2);
    
    t1 = (box_left.min_y - ray_origin_y) * inv_dir_y;
    t2 = (box_left.max_y - ray_origin_y) * inv_dir_y;
    tmin_left = std::max(tmin_left, std::min(t1, t2));
    tmax_left = std::min(tmax_left, std::max(t1, t2));
    
    t1 = (box_left.min_z - ray_origin_z) * inv_dir_z;
    t2 = (box_left.max_z - ray_origin_z) * inv_dir_z;
    tmin_left = std::max(tmin_left, std::min(t1, t2));
    tmax_left = std::min(tmax_left, std::max(t1, t2));
    
    out_tmin_left = tmin_left;
    
    // Test right box
    t1 = (box_right.min_x - ray_origin_x) * inv_dir_x;
    t2 = (box_right.max_x - ray_origin_x) * inv_dir_x;
    float tmin_right = std::min(t1, t2);
    float tmax_right = std::max(t1, t2);
    
    t1 = (box_right.min_y - ray_origin_y) * inv_dir_y;
    t2 = (box_right.max_y - ray_origin_y) * inv_dir_y;
    tmin_right = std::max(tmin_right, std::min(t1, t2));
    tmax_right = std::min(tmax_right, std::max(t1, t2));
    
    t1 = (box_right.min_z - ray_origin_z) * inv_dir_z;
    t2 = (box_right.max_z - ray_origin_z) * inv_dir_z;
    tmin_right = std::max(tmin_right, std::min(t1, t2));
    tmax_right = std::min(tmax_right, std::max(t1, t2));
    
    out_tmin_right = tmin_right;
    
    // Check hits
    int result = 0;
    if (tmax_left >= tmin_left && tmin_left <= max_t && tmax_left >= 0.0f) {
        result |= 1;  // Left hit
    }
    if (tmax_right >= tmin_right && tmin_right <= max_t && tmax_right >= 0.0f) {
        result |= 2;  // Right hit
    }
    
    return result;
#endif
}

} // namespace SIMD

#endif // SIMD_RAY_AABB_H