#ifndef SIMD_PIXEL_LIGHTING_H
#define SIMD_PIXEL_LIGHTING_H

#include <cmath>
#include <algorithm>
#include "lighting_config.h"

// Platform detection
#ifdef __ARM_NEON
#include <arm_neon.h>
#define HAS_NEON
#elif defined(__AVX2__)
#include <immintrin.h>
#define HAS_AVX2
#elif defined(__SSE2__)
#include <xmmintrin.h>
#include <emmintrin.h>
#define HAS_SSE2
#endif

namespace SIMD {

// Structure to hold pixel data for batch processing
struct PixelBatch {
    float point_x[4];
    float point_y[4];
    float point_z[4];
    float normal_x[4];
    float normal_y[4];
    float normal_z[4];
    float intensity[4];  // Output
    bool valid[4];       // Which pixels are valid in this batch
};

#ifdef HAS_NEON
// ARM NEON implementation for Apple Silicon - process 4 pixels at once
inline void calculate_light_contribution_batch_neon(
    const PixelBatch& pixels,
    float light_x, float light_y, float light_z,
    float emission_strength, float emission_radius,
    float* out_intensity) {
    
    // Load pixel positions
    float32x4_t px = vld1q_f32(pixels.point_x);
    float32x4_t py = vld1q_f32(pixels.point_y);
    float32x4_t pz = vld1q_f32(pixels.point_z);
    
    // Load normals
    float32x4_t nx = vld1q_f32(pixels.normal_x);
    float32x4_t ny = vld1q_f32(pixels.normal_y);
    float32x4_t nz = vld1q_f32(pixels.normal_z);
    
    // Light position (broadcast to all lanes)
    float32x4_t lx = vdupq_n_f32(light_x);
    float32x4_t ly = vdupq_n_f32(light_y);
    float32x4_t lz = vdupq_n_f32(light_z);
    
    // Calculate direction vectors for 4 pixels
    float32x4_t dx = vsubq_f32(lx, px);
    float32x4_t dy = vsubq_f32(ly, py);
    float32x4_t dz = vsubq_f32(lz, pz);
    
    // Distance squared
    float32x4_t dist_sq = vaddq_f32(
        vmulq_f32(dx, dx),
        vaddq_f32(vmulq_f32(dy, dy), vmulq_f32(dz, dz))
    );
    
    // Distance (using reciprocal square root approximation)
    float32x4_t inv_dist = vrsqrteq_f32(dist_sq);
    
    // Newton-Raphson refinement for better accuracy
    float32x4_t half = vdupq_n_f32(0.5f);
    float32x4_t three = vdupq_n_f32(3.0f);
    inv_dist = vmulq_f32(inv_dist, vsubq_f32(three, vmulq_f32(dist_sq, vmulq_f32(inv_dist, inv_dist))));
    inv_dist = vmulq_f32(inv_dist, half);
    
    // Actual distance for radius check
    float32x4_t dist = vmulq_f32(dist_sq, inv_dist);
    
    // Check emission radius
    float32x4_t max_radius = vdupq_n_f32(emission_radius);
    uint32x4_t in_range = vcleq_f32(dist, max_radius);
    
    // Normalize direction
    dx = vmulq_f32(dx, inv_dist);
    dy = vmulq_f32(dy, inv_dist);
    dz = vmulq_f32(dz, inv_dist);
    
    // Dot product with normals (Lambert)
    float32x4_t dot = vaddq_f32(
        vmulq_f32(dx, nx),
        vaddq_f32(vmulq_f32(dy, ny), vmulq_f32(dz, nz))
    );
    
    // Clamp to positive values (surface facing light)
    dot = vmaxq_f32(dot, vdupq_n_f32(0.0f));
    
    // Calculate intensity using inverse square law
    float32x4_t four_pi = vdupq_n_f32(4.0f * M_PI);
    float32x4_t strength = vdupq_n_f32(emission_strength);
    float32x4_t intensity = vdivq_f32(strength, vmulq_f32(four_pi, dist_sq));
    
    // Apply Lambert shading
    intensity = vmulq_f32(intensity, dot);
    
    // Apply range mask (zero out if beyond emission radius)
    intensity = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(intensity), in_range));
    
    // Store results
    vst1q_f32(out_intensity, intensity);
}

// Process 4 shadow rays in parallel
inline void test_shadow_rays_batch_neon(
    const PixelBatch& pixels,
    float light_x, float light_y, float light_z,
    bool* out_blocked) {
    
    // For now, return false (not blocked) for all pixels
    // This would need BVH integration for actual shadow testing
    // The key is that we can test 4 rays in parallel
    for (int i = 0; i < 4; i++) {
        out_blocked[i] = false; // TODO: Actual shadow test
    }
}
#endif

// Scalar fallback for single pixel
inline float calculate_light_contribution_scalar(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    float light_x, float light_y, float light_z,
    float emission_strength, float emission_radius) {
    
    // Direction to light
    float dx = light_x - point_x;
    float dy = light_y - point_y;
    float dz = light_z - point_z;
    
    // Distance
    float dist_sq = dx*dx + dy*dy + dz*dz;
    float dist = std::sqrt(dist_sq);
    
    // Check emission radius
    if (dist > emission_radius) return 0.0f;
    
    // Normalize direction
    float inv_dist = 1.0f / dist;
    dx *= inv_dist;
    dy *= inv_dist;
    dz *= inv_dist;
    
    // Dot product with normal (Lambert)
    float dot = normal_x * dx + normal_y * dy + normal_z * dz;
    if (dot <= 0.0f) return 0.0f;
    
    // Intensity using inverse square law
    LightingConfig& config = LightingConfig::get();
    float intensity = config.calculate_intensity_at_distance(emission_strength, dist);
    
    // Apply Lambert shading
    intensity *= dot;
    
    return intensity;
}

// Main dispatch function for batch processing
inline void calculate_pixel_batch_lighting(
    const PixelBatch& pixels,
    float light_x, float light_y, float light_z,
    float emission_strength, float emission_radius,
    float* out_intensity) {
    
#ifdef HAS_NEON
    // Process 4 pixels at once with NEON
    calculate_light_contribution_batch_neon(
        pixels, light_x, light_y, light_z,
        emission_strength, emission_radius,
        out_intensity);
#else
    // Fallback to scalar processing
    for (int i = 0; i < 4; i++) {
        if (pixels.valid[i]) {
            out_intensity[i] = calculate_light_contribution_scalar(
                pixels.point_x[i], pixels.point_y[i], pixels.point_z[i],
                pixels.normal_x[i], pixels.normal_y[i], pixels.normal_z[i],
                light_x, light_y, light_z,
                emission_strength, emission_radius);
        } else {
            out_intensity[i] = 0.0f;
        }
    }
#endif
}

} // namespace SIMD

#endif // SIMD_PIXEL_LIGHTING_H