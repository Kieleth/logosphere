#pragma once

// ============================================================================
// SIMD Edge Equation Evaluation
// ============================================================================
// PURPOSE: Accelerate edge equation tests for triangle rasterization
// TARGET: Lines 364-366 in camera_system.cpp
// EXPECTED: 8x throughput for edge tests (process 8 pixels simultaneously)
// ============================================================================

#include <cstdint>

// Platform detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define HAS_SSE2
    #include <emmintrin.h>  // SSE2
    #ifdef __AVX2__
        #define HAS_AVX2
        #include <immintrin.h>  // AVX2
    #endif
#elif defined(__ARM_NEON)
    #define HAS_NEON
    #include <arm_neon.h>
#endif

namespace SIMD {

// ============================================================================
// Scalar fallback (always available)
// ============================================================================
inline void evaluate_edge_scalar(
    float a, float b, float c,  // Edge equation coefficients: ax + by + c = 0
    const float* pixel_x,        // Array of 8 x coordinates
    float pixel_y,               // Single y coordinate (same for horizontal span)
    float* results)              // Output: 8 edge values
{
    for (int i = 0; i < 8; i++) {
        results[i] = a * pixel_x[i] + b * pixel_y + c;
    }
}

// Test if all 8 pixels are inside (all edge values >= 0)
inline uint8_t test_inside_scalar(
    const float* edge0,
    const float* edge1,
    const float* edge2)
{
    uint8_t mask = 0;
    for (int i = 0; i < 8; i++) {
        if (edge0[i] >= 0 && edge1[i] >= 0 && edge2[i] >= 0) {
            mask |= (1 << i);
        }
    }
    return mask;  // Bit i set if pixel i is inside
}

#ifdef HAS_AVX2
// ============================================================================
// AVX2 implementation (8 pixels at once)
// ============================================================================
inline void evaluate_edge_avx2(
    float a, float b, float c,
    const float* pixel_x,
    float pixel_y,
    float* results)
{
    __m256 va = _mm256_set1_ps(a);
    __m256 vb = _mm256_set1_ps(b);
    __m256 vc = _mm256_set1_ps(c);
    __m256 vy = _mm256_set1_ps(pixel_y);
    __m256 vx = _mm256_loadu_ps(pixel_x);  // Load 8 x values
    
    // edge = a*x + b*y + c
    __m256 edge = _mm256_fmadd_ps(va, vx, _mm256_fmadd_ps(vb, vy, vc));
    _mm256_storeu_ps(results, edge);
}

inline uint8_t test_inside_avx2(
    const float* edge0,
    const float* edge1,
    const float* edge2)
{
    __m256 e0 = _mm256_loadu_ps(edge0);
    __m256 e1 = _mm256_loadu_ps(edge1);
    __m256 e2 = _mm256_loadu_ps(edge2);
    __m256 zero = _mm256_setzero_ps();
    
    // Test if >= 0
    __m256 mask0 = _mm256_cmp_ps(e0, zero, _CMP_GE_OQ);
    __m256 mask1 = _mm256_cmp_ps(e1, zero, _CMP_GE_OQ);
    __m256 mask2 = _mm256_cmp_ps(e2, zero, _CMP_GE_OQ);
    
    // All three must be >= 0
    __m256 inside = _mm256_and_ps(mask0, _mm256_and_ps(mask1, mask2));
    
    // Convert to bit mask
    return _mm256_movemask_ps(inside);
}
#endif

#ifdef HAS_SSE2
// ============================================================================
// SSE2 implementation (4 pixels at once, process twice)
// ============================================================================
inline void evaluate_edge_sse2(
    float a, float b, float c,
    const float* pixel_x,
    float pixel_y,
    float* results)
{
    __m128 va = _mm_set1_ps(a);
    __m128 vb = _mm_set1_ps(b);
    __m128 vc = _mm_set1_ps(c);
    __m128 vy = _mm_set1_ps(pixel_y);
    
    // First 4 pixels
    __m128 vx0 = _mm_loadu_ps(pixel_x);
    __m128 edge0 = _mm_add_ps(_mm_mul_ps(va, vx0), _mm_add_ps(_mm_mul_ps(vb, vy), vc));
    _mm_storeu_ps(results, edge0);
    
    // Next 4 pixels
    __m128 vx1 = _mm_loadu_ps(pixel_x + 4);
    __m128 edge1 = _mm_add_ps(_mm_mul_ps(va, vx1), _mm_add_ps(_mm_mul_ps(vb, vy), vc));
    _mm_storeu_ps(results + 4, edge1);
}

inline uint8_t test_inside_sse2(
    const float* edge0,
    const float* edge1,
    const float* edge2)
{
    __m128 zero = _mm_setzero_ps();
    
    // First 4 pixels
    __m128 e0_0 = _mm_loadu_ps(edge0);
    __m128 e1_0 = _mm_loadu_ps(edge1);
    __m128 e2_0 = _mm_loadu_ps(edge2);
    __m128 mask0_0 = _mm_cmpge_ps(e0_0, zero);
    __m128 mask1_0 = _mm_cmpge_ps(e1_0, zero);
    __m128 mask2_0 = _mm_cmpge_ps(e2_0, zero);
    __m128 inside_0 = _mm_and_ps(mask0_0, _mm_and_ps(mask1_0, mask2_0));
    uint8_t bits_0 = _mm_movemask_ps(inside_0);
    
    // Next 4 pixels
    __m128 e0_1 = _mm_loadu_ps(edge0 + 4);
    __m128 e1_1 = _mm_loadu_ps(edge1 + 4);
    __m128 e2_1 = _mm_loadu_ps(edge2 + 4);
    __m128 mask0_1 = _mm_cmpge_ps(e0_1, zero);
    __m128 mask1_1 = _mm_cmpge_ps(e1_1, zero);
    __m128 mask2_1 = _mm_cmpge_ps(e2_1, zero);
    __m128 inside_1 = _mm_and_ps(mask0_1, _mm_and_ps(mask1_1, mask2_1));
    uint8_t bits_1 = _mm_movemask_ps(inside_1);
    
    return bits_0 | (bits_1 << 4);
}
#endif

#ifdef HAS_NEON
// ============================================================================
// ARM NEON implementation (4 pixels at once, process twice)
// ============================================================================
inline void evaluate_edge_neon(
    float a, float b, float c,
    const float* pixel_x,
    float pixel_y,
    float* results)
{
    float32x4_t va = vdupq_n_f32(a);
    float32x4_t vb = vdupq_n_f32(b);
    float32x4_t vc = vdupq_n_f32(c);
    float32x4_t vy = vdupq_n_f32(pixel_y);
    
    // First 4 pixels
    float32x4_t vx0 = vld1q_f32(pixel_x);
    float32x4_t edge0 = vmlaq_f32(vc, va, vx0);  // c + a*x
    edge0 = vmlaq_f32(edge0, vb, vy);            // + b*y
    vst1q_f32(results, edge0);
    
    // Next 4 pixels
    float32x4_t vx1 = vld1q_f32(pixel_x + 4);
    float32x4_t edge1 = vmlaq_f32(vc, va, vx1);
    edge1 = vmlaq_f32(edge1, vb, vy);
    vst1q_f32(results + 4, edge1);
}

inline uint8_t test_inside_neon(
    const float* edge0,
    const float* edge1,
    const float* edge2)
{
    float32x4_t zero = vdupq_n_f32(0.0f);
    uint8_t mask = 0;
    
    // First 4 pixels
    float32x4_t e0_0 = vld1q_f32(edge0);
    float32x4_t e1_0 = vld1q_f32(edge1);
    float32x4_t e2_0 = vld1q_f32(edge2);
    uint32x4_t mask0_0 = vcgeq_f32(e0_0, zero);
    uint32x4_t mask1_0 = vcgeq_f32(e1_0, zero);
    uint32x4_t mask2_0 = vcgeq_f32(e2_0, zero);
    uint32x4_t inside_0 = vandq_u32(mask0_0, vandq_u32(mask1_0, mask2_0));
    
    // Extract bits (NEON doesn't have movemask equivalent)
    if (vgetq_lane_u32(inside_0, 0)) mask |= 0x01;
    if (vgetq_lane_u32(inside_0, 1)) mask |= 0x02;
    if (vgetq_lane_u32(inside_0, 2)) mask |= 0x04;
    if (vgetq_lane_u32(inside_0, 3)) mask |= 0x08;
    
    // Next 4 pixels
    float32x4_t e0_1 = vld1q_f32(edge0 + 4);
    float32x4_t e1_1 = vld1q_f32(edge1 + 4);
    float32x4_t e2_1 = vld1q_f32(edge2 + 4);
    uint32x4_t mask0_1 = vcgeq_f32(e0_1, zero);
    uint32x4_t mask1_1 = vcgeq_f32(e1_1, zero);
    uint32x4_t mask2_1 = vcgeq_f32(e2_1, zero);
    uint32x4_t inside_1 = vandq_u32(mask0_1, vandq_u32(mask1_1, mask2_1));
    
    if (vgetq_lane_u32(inside_1, 0)) mask |= 0x10;
    if (vgetq_lane_u32(inside_1, 1)) mask |= 0x20;
    if (vgetq_lane_u32(inside_1, 2)) mask |= 0x40;
    if (vgetq_lane_u32(inside_1, 3)) mask |= 0x80;
    
    return mask;
}
#endif

// ============================================================================
// Unified interface - selects best available implementation
// ============================================================================
inline void evaluate_edge(
    float a, float b, float c,
    const float* pixel_x,
    float pixel_y,
    float* results)
{
#ifdef HAS_AVX2
    evaluate_edge_avx2(a, b, c, pixel_x, pixel_y, results);
#elif defined(HAS_SSE2)
    evaluate_edge_sse2(a, b, c, pixel_x, pixel_y, results);
#elif defined(HAS_NEON)
    evaluate_edge_neon(a, b, c, pixel_x, pixel_y, results);
#else
    evaluate_edge_scalar(a, b, c, pixel_x, pixel_y, results);
#endif
}

inline uint8_t test_inside(
    const float* edge0,
    const float* edge1,
    const float* edge2)
{
#ifdef HAS_AVX2
    return test_inside_avx2(edge0, edge1, edge2);
#elif defined(HAS_SSE2)
    return test_inside_sse2(edge0, edge1, edge2);
#elif defined(HAS_NEON)
    return test_inside_neon(edge0, edge1, edge2);
#else
    return test_inside_scalar(edge0, edge1, edge2);
#endif
}

} // namespace SIMD