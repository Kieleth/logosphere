#ifndef SIMD_UV_H
#define SIMD_UV_H

#include <cstdint>

// Platform detection for SIMD support
#if defined(__ARM_NEON) || defined(__aarch64__)
    #define HAS_NEON
    #include <arm_neon.h>
#elif defined(__AVX2__)
    #define HAS_AVX2
    #include <immintrin.h>
#elif defined(__SSE2__)
    #define HAS_SSE2
    #include <emmintrin.h>
#endif

namespace SIMD {

// Compute barycentric UV coordinates for multiple pixels at once
// Returns a bitmask of which pixels have valid UV coordinates
uint8_t compute_uv_batch(
    const float* pixel_x,      // Array of 8 x coordinates
    float pixel_y,             // Same y for all pixels
    const float tri_x[3],      // Triangle vertex x coords
    const float tri_y[3],      // Triangle vertex y coords
    float* out_u,              // Output u coordinates (8 values)
    float* out_v               // Output v coordinates (8 values)
);

// Scalar fallback implementation
inline uint8_t compute_uv_batch_scalar(
    const float* pixel_x,
    float pixel_y,
    const float tri_x[3],
    const float tri_y[3],
    float* out_u,
    float* out_v)
{
    uint8_t valid_mask = 0;
    
    // Precompute triangle-specific values (same for all pixels)
    float x0 = tri_x[0], y0 = tri_y[0];
    float x1 = tri_x[1], y1 = tri_y[1];
    float x2 = tri_x[2], y2 = tri_y[2];
    
    // Vectors from vertex 0 to 1 and 2
    float v0x = x1 - x0, v0y = y1 - y0;
    float v1x = x2 - x0, v1y = y2 - y0;
    
    // Triangle-specific dot products
    float d00 = v0x * v0x + v0y * v0y;
    float d01 = v0x * v1x + v0y * v1y;
    float d11 = v1x * v1x + v1y * v1y;
    
    // Compute denominator once
    float denom = d00 * d11 - d01 * d01;
    if (denom < 0.001f && denom > -0.001f) {
        // Degenerate triangle - all pixels get center UV
        for (int i = 0; i < 8; ++i) {
            out_u[i] = 0.5f;
            out_v[i] = 0.5f;
            valid_mask |= (1 << i);
        }
        return valid_mask;
    }
    
    float inv_denom = 1.0f / denom;
    
    // Process each pixel
    for (int i = 0; i < 8; ++i) {
        // Vector from vertex 0 to pixel
        float v2x = pixel_x[i] - x0;
        float v2y = pixel_y - y0;
        
        // Pixel-specific dot products
        float d20 = v2x * v0x + v2y * v0y;
        float d21 = v2x * v1x + v2y * v1y;
        
        // Barycentric coordinates
        out_u[i] = (d11 * d20 - d01 * d21) * inv_denom;
        out_v[i] = (d00 * d21 - d01 * d20) * inv_denom;
        
        valid_mask |= (1 << i);
    }
    
    return valid_mask;
}

#ifdef HAS_NEON
// ARM NEON implementation (processes 4 pixels at a time, twice)
inline uint8_t compute_uv_batch_neon(
    const float* pixel_x,
    float pixel_y,
    const float tri_x[3],
    const float tri_y[3],
    float* out_u,
    float* out_v)
{
    // Triangle setup (scalar - computed once)
    float x0 = tri_x[0], y0 = tri_y[0];
    float x1 = tri_x[1], y1 = tri_y[1];
    float x2 = tri_x[2], y2 = tri_y[2];
    
    float v0x = x1 - x0, v0y = y1 - y0;
    float v1x = x2 - x0, v1y = y2 - y0;
    
    float d00 = v0x * v0x + v0y * v0y;
    float d01 = v0x * v1x + v0y * v1y;
    float d11 = v1x * v1x + v1y * v1y;
    
    float denom = d00 * d11 - d01 * d01;
    if (denom < 0.001f && denom > -0.001f) {
        // Degenerate triangle
        float32x4_t half = vdupq_n_f32(0.5f);
        vst1q_f32(out_u, half);
        vst1q_f32(out_u + 4, half);
        vst1q_f32(out_v, half);
        vst1q_f32(out_v + 4, half);
        return 0xFF;
    }
    
    float inv_denom = 1.0f / denom;
    
    // Broadcast triangle values to SIMD registers
    float32x4_t vx0 = vdupq_n_f32(x0);
    float32x4_t vy0 = vdupq_n_f32(y0);
    float32x4_t vv0x = vdupq_n_f32(v0x);
    float32x4_t vv0y = vdupq_n_f32(v0y);
    float32x4_t vv1x = vdupq_n_f32(v1x);
    float32x4_t vv1y = vdupq_n_f32(v1y);
    float32x4_t vd00 = vdupq_n_f32(d00);
    float32x4_t vd01 = vdupq_n_f32(d01);
    float32x4_t vd11 = vdupq_n_f32(d11);
    float32x4_t vinv_denom = vdupq_n_f32(inv_denom);
    float32x4_t vpixel_y = vdupq_n_f32(pixel_y);
    
    // Process first 4 pixels
    float32x4_t vpx0 = vld1q_f32(pixel_x);
    float32x4_t vv2x0 = vsubq_f32(vpx0, vx0);
    float32x4_t vv2y0 = vsubq_f32(vpixel_y, vy0);
    
    // Dot products: d20 = v2x * v0x + v2y * v0y
    float32x4_t vd20_0 = vmlaq_f32(vmulq_f32(vv2x0, vv0x), vv2y0, vv0y);
    float32x4_t vd21_0 = vmlaq_f32(vmulq_f32(vv2x0, vv1x), vv2y0, vv1y);
    
    // Barycentric: u = (d11 * d20 - d01 * d21) * inv_denom
    float32x4_t vu0 = vmulq_f32(vmlsq_f32(vmulq_f32(vd11, vd20_0), vd01, vd21_0), vinv_denom);
    float32x4_t vv0 = vmulq_f32(vmlsq_f32(vmulq_f32(vd00, vd21_0), vd01, vd20_0), vinv_denom);
    
    vst1q_f32(out_u, vu0);
    vst1q_f32(out_v, vv0);
    
    // Process second 4 pixels
    float32x4_t vpx1 = vld1q_f32(pixel_x + 4);
    float32x4_t vv2x1 = vsubq_f32(vpx1, vx0);
    float32x4_t vv2y1 = vsubq_f32(vpixel_y, vy0);
    
    float32x4_t vd20_1 = vmlaq_f32(vmulq_f32(vv2x1, vv0x), vv2y1, vv0y);
    float32x4_t vd21_1 = vmlaq_f32(vmulq_f32(vv2x1, vv1x), vv2y1, vv1y);
    
    float32x4_t vu1 = vmulq_f32(vmlsq_f32(vmulq_f32(vd11, vd20_1), vd01, vd21_1), vinv_denom);
    float32x4_t vv1 = vmulq_f32(vmlsq_f32(vmulq_f32(vd00, vd21_1), vd01, vd20_1), vinv_denom);
    
    vst1q_f32(out_u + 4, vu1);
    vst1q_f32(out_v + 4, vv1);
    
    return 0xFF; // All pixels valid
}
#endif

#ifdef HAS_AVX2
// AVX2 implementation (processes all 8 pixels at once)
inline uint8_t compute_uv_batch_avx2(
    const float* pixel_x,
    float pixel_y,
    const float tri_x[3],
    const float tri_y[3],
    float* out_u,
    float* out_v)
{
    // Triangle setup (scalar - computed once)
    float x0 = tri_x[0], y0 = tri_y[0];
    float x1 = tri_x[1], y1 = tri_y[1];
    float x2 = tri_x[2], y2 = tri_y[2];
    
    float v0x = x1 - x0, v0y = y1 - y0;
    float v1x = x2 - x0, v1y = y2 - y0;
    
    float d00 = v0x * v0x + v0y * v0y;
    float d01 = v0x * v1x + v0y * v1y;
    float d11 = v1x * v1x + v1y * v1y;
    
    float denom = d00 * d11 - d01 * d01;
    if (denom < 0.001f && denom > -0.001f) {
        // Degenerate triangle
        __m256 half = _mm256_set1_ps(0.5f);
        _mm256_storeu_ps(out_u, half);
        _mm256_storeu_ps(out_v, half);
        return 0xFF;
    }
    
    float inv_denom = 1.0f / denom;
    
    // Broadcast triangle values
    __m256 vx0 = _mm256_set1_ps(x0);
    __m256 vy0 = _mm256_set1_ps(y0);
    __m256 vv0x = _mm256_set1_ps(v0x);
    __m256 vv0y = _mm256_set1_ps(v0y);
    __m256 vv1x = _mm256_set1_ps(v1x);
    __m256 vv1y = _mm256_set1_ps(v1y);
    __m256 vd00 = _mm256_set1_ps(d00);
    __m256 vd01 = _mm256_set1_ps(d01);
    __m256 vd11 = _mm256_set1_ps(d11);
    __m256 vinv_denom = _mm256_set1_ps(inv_denom);
    __m256 vpixel_y = _mm256_set1_ps(pixel_y);
    
    // Load pixel x coordinates
    __m256 vpx = _mm256_loadu_ps(pixel_x);
    
    // Vector from vertex 0 to pixels
    __m256 vv2x = _mm256_sub_ps(vpx, vx0);
    __m256 vv2y = _mm256_sub_ps(vpixel_y, vy0);
    
    // Dot products
    __m256 vd20 = _mm256_fmadd_ps(vv2x, vv0x, _mm256_mul_ps(vv2y, vv0y));
    __m256 vd21 = _mm256_fmadd_ps(vv2x, vv1x, _mm256_mul_ps(vv2y, vv1y));
    
    // Barycentric coordinates
    __m256 vu = _mm256_mul_ps(_mm256_fnmadd_ps(vd01, vd21, _mm256_mul_ps(vd11, vd20)), vinv_denom);
    __m256 vv = _mm256_mul_ps(_mm256_fnmadd_ps(vd01, vd20, _mm256_mul_ps(vd00, vd21)), vinv_denom);
    
    _mm256_storeu_ps(out_u, vu);
    _mm256_storeu_ps(out_v, vv);
    
    return 0xFF; // All pixels valid
}
#endif

#ifdef HAS_SSE2
// SSE2 implementation (processes 4 pixels at a time, twice)
inline uint8_t compute_uv_batch_sse2(
    const float* pixel_x,
    float pixel_y,
    const float tri_x[3],
    const float tri_y[3],
    float* out_u,
    float* out_v)
{
    // Triangle setup (scalar - computed once)
    float x0 = tri_x[0], y0 = tri_y[0];
    float x1 = tri_x[1], y1 = tri_y[1];
    float x2 = tri_x[2], y2 = tri_y[2];
    
    float v0x = x1 - x0, v0y = y1 - y0;
    float v1x = x2 - x0, v1y = y2 - y0;
    
    float d00 = v0x * v0x + v0y * v0y;
    float d01 = v0x * v1x + v0y * v1y;
    float d11 = v1x * v1x + v1y * v1y;
    
    float denom = d00 * d11 - d01 * d01;
    if (denom < 0.001f && denom > -0.001f) {
        // Degenerate triangle
        __m128 half = _mm_set1_ps(0.5f);
        _mm_storeu_ps(out_u, half);
        _mm_storeu_ps(out_u + 4, half);
        _mm_storeu_ps(out_v, half);
        _mm_storeu_ps(out_v + 4, half);
        return 0xFF;
    }
    
    float inv_denom = 1.0f / denom;
    
    // Broadcast triangle values
    __m128 vx0 = _mm_set1_ps(x0);
    __m128 vy0 = _mm_set1_ps(y0);
    __m128 vv0x = _mm_set1_ps(v0x);
    __m128 vv0y = _mm_set1_ps(v0y);
    __m128 vv1x = _mm_set1_ps(v1x);
    __m128 vv1y = _mm_set1_ps(v1y);
    __m128 vd00 = _mm_set1_ps(d00);
    __m128 vd01 = _mm_set1_ps(d01);
    __m128 vd11 = _mm_set1_ps(d11);
    __m128 vinv_denom = _mm_set1_ps(inv_denom);
    __m128 vpixel_y = _mm_set1_ps(pixel_y);
    
    // Process first 4 pixels
    __m128 vpx0 = _mm_loadu_ps(pixel_x);
    __m128 vv2x0 = _mm_sub_ps(vpx0, vx0);
    __m128 vv2y0 = _mm_sub_ps(vpixel_y, vy0);
    
    __m128 vd20_0 = _mm_add_ps(_mm_mul_ps(vv2x0, vv0x), _mm_mul_ps(vv2y0, vv0y));
    __m128 vd21_0 = _mm_add_ps(_mm_mul_ps(vv2x0, vv1x), _mm_mul_ps(vv2y0, vv1y));
    
    __m128 vu0 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(vd11, vd20_0), _mm_mul_ps(vd01, vd21_0)), vinv_denom);
    __m128 vv0 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(vd00, vd21_0), _mm_mul_ps(vd01, vd20_0)), vinv_denom);
    
    _mm_storeu_ps(out_u, vu0);
    _mm_storeu_ps(out_v, vv0);
    
    // Process second 4 pixels
    __m128 vpx1 = _mm_loadu_ps(pixel_x + 4);
    __m128 vv2x1 = _mm_sub_ps(vpx1, vx0);
    __m128 vv2y1 = _mm_sub_ps(vpixel_y, vy0);
    
    __m128 vd20_1 = _mm_add_ps(_mm_mul_ps(vv2x1, vv0x), _mm_mul_ps(vv2y1, vv0y));
    __m128 vd21_1 = _mm_add_ps(_mm_mul_ps(vv2x1, vv1x), _mm_mul_ps(vv2y1, vv1y));
    
    __m128 vu1 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(vd11, vd20_1), _mm_mul_ps(vd01, vd21_1)), vinv_denom);
    __m128 vv1 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(vd00, vd21_1), _mm_mul_ps(vd01, vd20_1)), vinv_denom);
    
    _mm_storeu_ps(out_u + 4, vu1);
    _mm_storeu_ps(out_v + 4, vv1);
    
    return 0xFF; // All pixels valid
}
#endif

// Main dispatch function
inline uint8_t compute_uv_batch(
    const float* pixel_x,
    float pixel_y,
    const float tri_x[3],
    const float tri_y[3],
    float* out_u,
    float* out_v)
{
#ifdef HAS_AVX2
    return compute_uv_batch_avx2(pixel_x, pixel_y, tri_x, tri_y, out_u, out_v);
#elif defined(HAS_SSE2)
    return compute_uv_batch_sse2(pixel_x, pixel_y, tri_x, tri_y, out_u, out_v);
#elif defined(HAS_NEON)
    return compute_uv_batch_neon(pixel_x, pixel_y, tri_x, tri_y, out_u, out_v);
#else
    return compute_uv_batch_scalar(pixel_x, pixel_y, tri_x, tri_y, out_u, out_v);
#endif
}

} // namespace SIMD

#endif // SIMD_UV_H