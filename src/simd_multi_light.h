#ifndef SIMD_MULTI_LIGHT_H
#define SIMD_MULTI_LIGHT_H

#include <cmath>
#include <algorithm>

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

// Light structure for batch processing
struct LightData {
    float x, y, z;
    float r, g, b;
    float emission_strength;
    float emission_radius;
};

#ifdef HAS_NEON
// ARM NEON implementation for Apple Silicon
inline void calculate_multi_light_neon(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const LightData* lights, int light_count,
    float& total_r, float& total_g, float& total_b) {
    
    float32x4_t sum_r = vdupq_n_f32(0.0f);
    float32x4_t sum_g = vdupq_n_f32(0.0f);
    float32x4_t sum_b = vdupq_n_f32(0.0f);
    
    float32x4_t px = vdupq_n_f32(point_x);
    float32x4_t py = vdupq_n_f32(point_y);
    float32x4_t pz = vdupq_n_f32(point_z);
    
    float32x4_t nx = vdupq_n_f32(normal_x);
    float32x4_t ny = vdupq_n_f32(normal_y);
    float32x4_t nz = vdupq_n_f32(normal_z);
    
    // Process 4 lights at a time
    for (int i = 0; i < light_count; i += 4) {
        int batch_size = std::min(4, light_count - i);
        
        // Load light positions
        float lx[4] = {0}, ly[4] = {0}, lz[4] = {0};
        float lr[4] = {0}, lg[4] = {0}, lb[4] = {0};
        float strength[4] = {0};
        
        for (int j = 0; j < batch_size; j++) {
            lx[j] = lights[i + j].x;
            ly[j] = lights[i + j].y;
            lz[j] = lights[i + j].z;
            lr[j] = lights[i + j].r;
            lg[j] = lights[i + j].g;
            lb[j] = lights[i + j].b;
            strength[j] = lights[i + j].emission_strength;
        }
        
        float32x4_t light_x = vld1q_f32(lx);
        float32x4_t light_y = vld1q_f32(ly);
        float32x4_t light_z = vld1q_f32(lz);
        
        // Calculate direction vectors
        float32x4_t dx = vsubq_f32(light_x, px);
        float32x4_t dy = vsubq_f32(light_y, py);
        float32x4_t dz = vsubq_f32(light_z, pz);
        
        // Calculate distance squared
        float32x4_t dist_sq = vaddq_f32(
            vmulq_f32(dx, dx),
            vaddq_f32(vmulq_f32(dy, dy), vmulq_f32(dz, dz))
        );
        
        // Calculate 1/distance (using reciprocal square root)
        float32x4_t inv_dist = vrsqrteq_f32(dist_sq);
        
        // Normalize direction
        dx = vmulq_f32(dx, inv_dist);
        dy = vmulq_f32(dy, inv_dist);
        dz = vmulq_f32(dz, inv_dist);
        
        // Dot product with normal (Lambert)
        float32x4_t dot = vaddq_f32(
            vmulq_f32(dx, nx),
            vaddq_f32(vmulq_f32(dy, ny), vmulq_f32(dz, nz))
        );
        
        // Clamp to [0, 1]
        dot = vmaxq_f32(dot, vdupq_n_f32(0.0f));
        
        // Load light colors and strength
        float32x4_t light_r = vld1q_f32(lr);
        float32x4_t light_g = vld1q_f32(lg);
        float32x4_t light_b = vld1q_f32(lb);
        float32x4_t light_strength = vld1q_f32(strength);
        
        // Calculate intensity (inverse square law)
        // intensity = strength / (4π * distance²)
        float32x4_t four_pi = vdupq_n_f32(4.0f * M_PI);
        float32x4_t intensity = vdivq_f32(light_strength, vmulq_f32(four_pi, dist_sq));
        
        // Apply Lambert shading
        intensity = vmulq_f32(intensity, dot);
        
        // Accumulate contributions
        sum_r = vaddq_f32(sum_r, vmulq_f32(light_r, intensity));
        sum_g = vaddq_f32(sum_g, vmulq_f32(light_g, intensity));
        sum_b = vaddq_f32(sum_b, vmulq_f32(light_b, intensity));
    }
    
    // Horizontal sum
    float32x2_t sum_r_low = vadd_f32(vget_low_f32(sum_r), vget_high_f32(sum_r));
    float32x2_t sum_g_low = vadd_f32(vget_low_f32(sum_g), vget_high_f32(sum_g));
    float32x2_t sum_b_low = vadd_f32(vget_low_f32(sum_b), vget_high_f32(sum_b));
    
    total_r = vget_lane_f32(vpadd_f32(sum_r_low, sum_r_low), 0);
    total_g = vget_lane_f32(vpadd_f32(sum_g_low, sum_g_low), 0);
    total_b = vget_lane_f32(vpadd_f32(sum_b_low, sum_b_low), 0);
}
#endif

// Scalar fallback implementation
inline void calculate_multi_light_scalar(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const LightData* lights, int light_count,
    float& total_r, float& total_g, float& total_b) {
    
    total_r = total_g = total_b = 0.0f;
    
    for (int i = 0; i < light_count; i++) {
        const LightData& light = lights[i];
        
        // Direction to light
        float dx = light.x - point_x;
        float dy = light.y - point_y;
        float dz = light.z - point_z;
        
        // Distance
        float dist_sq = dx*dx + dy*dy + dz*dz;
        float dist = std::sqrt(dist_sq);
        
        // Skip if too far
        if (dist > light.emission_radius) continue;
        
        // Normalize direction
        float inv_dist = 1.0f / dist;
        dx *= inv_dist;
        dy *= inv_dist;
        dz *= inv_dist;
        
        // Dot product with normal
        float dot = normal_x * dx + normal_y * dy + normal_z * dz;
        if (dot <= 0.0f) continue;
        
        // Intensity (inverse square law)
        float intensity = light.emission_strength / (4.0f * M_PI * dist_sq);
        intensity *= dot; // Lambert
        
        // Accumulate
        total_r += light.r * intensity;
        total_g += light.g * intensity;
        total_b += light.b * intensity;
    }
}

// Calculate light contributions for a batch of 4 lights (intensity only, no color)
inline void calculate_light_batch(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const float* light_x, const float* light_y, const float* light_z,
    const float* emission_strength, const float* emission_radius,
    float* out_intensity) {
    
#ifdef HAS_NEON
    // ARM NEON implementation for Apple Silicon
    float32x4_t px = vdupq_n_f32(point_x);
    float32x4_t py = vdupq_n_f32(point_y);
    float32x4_t pz = vdupq_n_f32(point_z);
    
    float32x4_t nx = vdupq_n_f32(normal_x);
    float32x4_t ny = vdupq_n_f32(normal_y);
    float32x4_t nz = vdupq_n_f32(normal_z);
    
    // Load light positions
    float32x4_t lx = vld1q_f32(light_x);
    float32x4_t ly = vld1q_f32(light_y);
    float32x4_t lz = vld1q_f32(light_z);
    
    // Direction vectors
    float32x4_t dx = vsubq_f32(lx, px);
    float32x4_t dy = vsubq_f32(ly, py);
    float32x4_t dz = vsubq_f32(lz, pz);
    
    // Distance squared
    float32x4_t dist_sq = vaddq_f32(
        vmulq_f32(dx, dx),
        vaddq_f32(vmulq_f32(dy, dy), vmulq_f32(dz, dz))
    );
    
    // Inverse distance (using reciprocal square root)
    float32x4_t inv_dist = vrsqrteq_f32(dist_sq);
    
    // Newton-Raphson refinement for accuracy
    float32x4_t three = vdupq_n_f32(3.0f);
    float32x4_t half = vdupq_n_f32(0.5f);
    inv_dist = vmulq_f32(inv_dist, vsubq_f32(three, vmulq_f32(dist_sq, vmulq_f32(inv_dist, inv_dist))));
    inv_dist = vmulq_f32(inv_dist, half);
    
    // Actual distance for radius check
    float32x4_t dist = vmulq_f32(dist_sq, inv_dist);
    
    // Check emission radius
    float32x4_t radius = vld1q_f32(emission_radius);
    uint32x4_t in_range = vcleq_f32(dist, radius);
    
    // Normalize direction
    dx = vmulq_f32(dx, inv_dist);
    dy = vmulq_f32(dy, inv_dist);
    dz = vmulq_f32(dz, inv_dist);
    
    // Dot product with normal (Lambert)
    float32x4_t dot = vaddq_f32(
        vmulq_f32(dx, nx),
        vaddq_f32(vmulq_f32(dy, ny), vmulq_f32(dz, nz))
    );
    
    // Clamp to positive
    dot = vmaxq_f32(dot, vdupq_n_f32(0.0f));
    
    // Intensity (inverse square law)
    float32x4_t strength = vld1q_f32(emission_strength);
    float32x4_t four_pi = vdupq_n_f32(4.0f * M_PI);
    float32x4_t intensity = vdivq_f32(strength, vmulq_f32(four_pi, dist_sq));
    
    // Apply Lambert
    intensity = vmulq_f32(intensity, dot);
    
    // Apply range mask
    intensity = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(intensity), in_range));
    
    // Store results
    vst1q_f32(out_intensity, intensity);
#else
    // Scalar fallback
    for (int i = 0; i < 4; i++) {
        float dx = light_x[i] - point_x;
        float dy = light_y[i] - point_y;
        float dz = light_z[i] - point_z;
        
        float dist_sq = dx*dx + dy*dy + dz*dz;
        float dist = std::sqrt(dist_sq);
        
        if (dist > emission_radius[i]) {
            out_intensity[i] = 0.0f;
            continue;
        }
        
        float inv_dist = 1.0f / dist;
        dx *= inv_dist;
        dy *= inv_dist;
        dz *= inv_dist;
        
        float dot = normal_x * dx + normal_y * dy + normal_z * dz;
        if (dot <= 0.0f) {
            out_intensity[i] = 0.0f;
            continue;
        }
        
        float intensity = emission_strength[i] / (4.0f * M_PI * dist_sq);
        intensity *= dot;
        
        out_intensity[i] = intensity;
    }
#endif
}

// Main dispatch function (kept for compatibility but now less used)
inline void calculate_multi_light(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const LightData* lights, int light_count,
    float& total_r, float& total_g, float& total_b) {
    
    // Use SIMD for 4+ lights
    if (light_count >= 4) {
        #ifdef HAS_NEON
            calculate_multi_light_neon(point_x, point_y, point_z,
                                       normal_x, normal_y, normal_z,
                                       lights, light_count,
                                       total_r, total_g, total_b);
            return;
        #endif
    }
    
    // Fall back to scalar for <4 lights or no SIMD
    calculate_multi_light_scalar(point_x, point_y, point_z,
                                 normal_x, normal_y, normal_z,
                                 lights, light_count,
                                 total_r, total_g, total_b);
}

} // namespace SIMD

#endif // SIMD_MULTI_LIGHT_H