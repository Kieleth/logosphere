#ifndef SIMD_RAY_AABB_WIDE_H
#define SIMD_RAY_AABB_WIDE_H

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

// SIMD-optimized ray-AABB intersection for WIDE BVH traversal
// Tests up to 8 AABBs against ONE ray simultaneously
namespace SIMD {

// Test up to 8 AABBs against 1 ray
// Returns bitmask indicating which boxes were hit (bit i = 1 means box i hit)
// Also fills out_tmins array with entry distances for sorting
inline int test_ray_multi_aabb(
    float ray_origin_x, float ray_origin_y, float ray_origin_z,
    float ray_dir_x, float ray_dir_y, float ray_dir_z,
    const AABB* boxes, int box_count, float max_t,
    float* out_tmins)
{
    // Compute inverse direction once (shared for all boxes)
    float inv_dir_x = (ray_dir_x != 0.0f) ? 1.0f / ray_dir_x : 1e30f;
    float inv_dir_y = (ray_dir_y != 0.0f) ? 1.0f / ray_dir_y : 1e30f;
    float inv_dir_z = (ray_dir_z != 0.0f) ? 1.0f / ray_dir_z : 1e30f;
    
#ifdef HAS_NEON
    // ARM NEON implementation - process 4 at a time
    int hit_mask = 0;
    
    // Process in batches of 4
    for (int batch = 0; batch < 2 && batch * 4 < box_count; batch++) {
        int start = batch * 4;
        int end = std::min(start + 4, box_count);
        
        // Pack bounds for this batch
        float min_x[4], min_y[4], min_z[4];
        float max_x[4], max_y[4], max_z[4];
        
        for (int i = 0; i < 4; i++) {
            if (start + i < end) {
                min_x[i] = boxes[start + i].min_x;
                min_y[i] = boxes[start + i].min_y;
                min_z[i] = boxes[start + i].min_z;
                max_x[i] = boxes[start + i].max_x;
                max_y[i] = boxes[start + i].max_y;
                max_z[i] = boxes[start + i].max_z;
            } else {
                // Pad with invalid box (will not hit)
                min_x[i] = 1e30f;
                min_y[i] = 1e30f;
                min_z[i] = 1e30f;
                max_x[i] = -1e30f;
                max_y[i] = -1e30f;
                max_z[i] = -1e30f;
            }
        }
        
        // Load bounds into NEON registers
        float32x4_t bounds_min_x = vld1q_f32(min_x);
        float32x4_t bounds_min_y = vld1q_f32(min_y);
        float32x4_t bounds_min_z = vld1q_f32(min_z);
        float32x4_t bounds_max_x = vld1q_f32(max_x);
        float32x4_t bounds_max_y = vld1q_f32(max_y);
        float32x4_t bounds_max_z = vld1q_f32(max_z);
        
        // Broadcast ray origin and inverse direction
        float32x4_t orig_x = vdupq_n_f32(ray_origin_x);
        float32x4_t orig_y = vdupq_n_f32(ray_origin_y);
        float32x4_t orig_z = vdupq_n_f32(ray_origin_z);
        float32x4_t inv_x = vdupq_n_f32(inv_dir_x);
        float32x4_t inv_y = vdupq_n_f32(inv_dir_y);
        float32x4_t inv_z = vdupq_n_f32(inv_dir_z);
        
        // Compute t values for X slabs
        float32x4_t t1_x = vmulq_f32(vsubq_f32(bounds_min_x, orig_x), inv_x);
        float32x4_t t2_x = vmulq_f32(vsubq_f32(bounds_max_x, orig_x), inv_x);
        float32x4_t tmin = vminq_f32(t1_x, t2_x);
        float32x4_t tmax = vmaxq_f32(t1_x, t2_x);
        
        // Y slabs
        float32x4_t t1_y = vmulq_f32(vsubq_f32(bounds_min_y, orig_y), inv_y);
        float32x4_t t2_y = vmulq_f32(vsubq_f32(bounds_max_y, orig_y), inv_y);
        tmin = vmaxq_f32(tmin, vminq_f32(t1_y, t2_y));
        tmax = vminq_f32(tmax, vmaxq_f32(t1_y, t2_y));
        
        // Z slabs
        float32x4_t t1_z = vmulq_f32(vsubq_f32(bounds_min_z, orig_z), inv_z);
        float32x4_t t2_z = vmulq_f32(vsubq_f32(bounds_max_z, orig_z), inv_z);
        tmin = vmaxq_f32(tmin, vminq_f32(t1_z, t2_z));
        tmax = vminq_f32(tmax, vmaxq_f32(t1_z, t2_z));
        
        // Store tmin values for sorting
        float tmins[4];
        vst1q_f32(tmins, tmin);
        
        // Check hits: tmax >= tmin && tmin <= max_t && tmax >= 0
        float32x4_t zero = vdupq_n_f32(0.0f);
        float32x4_t max_t_vec = vdupq_n_f32(max_t);
        
        uint32x4_t hit_test = vandq_u32(
            vandq_u32(
                vcgeq_f32(tmax, tmin),
                vcleq_f32(tmin, max_t_vec)
            ),
            vcgeq_f32(tmax, zero)
        );
        
        // Extract hit results
        uint32_t hits[4];
        vst1q_u32(hits, hit_test);
        
        // Set hit mask and store tmins
        for (int i = 0; i < 4 && start + i < end; i++) {
            if (hits[i]) {
                hit_mask |= (1 << (start + i));
                out_tmins[start + i] = tmins[i];
            } else {
                out_tmins[start + i] = 1e30f;
            }
        }
    }
    
    return hit_mask;
    
#else
    // Scalar fallback - test each AABB individually
    int hit_mask = 0;
    
    for (int i = 0; i < box_count; i++) {
        const AABB& box = boxes[i];
        
        // X slabs
        float t1 = (box.min_x - ray_origin_x) * inv_dir_x;
        float t2 = (box.max_x - ray_origin_x) * inv_dir_x;
        float tmin = std::min(t1, t2);
        float tmax = std::max(t1, t2);
        
        // Y slabs
        t1 = (box.min_y - ray_origin_y) * inv_dir_y;
        t2 = (box.max_y - ray_origin_y) * inv_dir_y;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));
        
        // Z slabs
        t1 = (box.min_z - ray_origin_z) * inv_dir_z;
        t2 = (box.max_z - ray_origin_z) * inv_dir_z;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));
        
        // Check hit
        if (tmax >= tmin && tmin <= max_t && tmax >= 0.0f) {
            hit_mask |= (1 << i);
            out_tmins[i] = tmin;
        } else {
            out_tmins[i] = 1e30f;
        }
    }
    
    return hit_mask;
#endif
}

} // namespace SIMD

#endif // SIMD_RAY_AABB_WIDE_H