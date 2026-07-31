#include "../src/simd_edge.h"
#include <iostream>
#include <cmath>
#include <cstring>

// ============================================================================
// TEST: SIMD Edge Equation Evaluation
// ============================================================================
// PURPOSE: Verify SIMD edge calculations match scalar exactly
// WHY: Edge equations determine which pixels are inside triangles
// EXPECTED: Bit-exact match between scalar and SIMD implementations

bool test_simd_edge_equations() {
    std::cout << "\n=== Testing SIMD Edge Equations ===" << std::endl;
    
    // =========================================================================
    // TEST 1: Basic edge equation evaluation
    // =========================================================================
    std::cout << "Test 1: Edge equation evaluation..." << std::endl;
    
    // Triangle vertices (CCW winding)
    float x0 = 100.0f, y0 = 100.0f;
    float x1 = 200.0f, y1 = 100.0f;
    float x2 = 150.0f, y2 = 200.0f;
    
    // Edge 0: from v0 to v1
    float a0 = y1 - y0;  // = 0
    float b0 = x0 - x1;  // = -100
    float c0 = x1*y0 - x0*y1;  // = 0
    
    // Test pixels along scanline y=150
    float pixel_x[8] = {120.0f, 130.0f, 140.0f, 150.0f, 160.0f, 170.0f, 180.0f, 190.0f};
    float pixel_y = 150.0f;
    
    // Scalar reference
    float scalar_results[8];
    SIMD::evaluate_edge_scalar(a0, b0, c0, pixel_x, pixel_y, scalar_results);
    
    // SIMD version
    float simd_results[8];
    SIMD::evaluate_edge(a0, b0, c0, pixel_x, pixel_y, simd_results);
    
    // Compare results
    bool match = true;
    for (int i = 0; i < 8; i++) {
        if (scalar_results[i] != simd_results[i]) {
            std::cout << "  FAIL: Mismatch at pixel " << i 
                      << " scalar=" << scalar_results[i] 
                      << " simd=" << simd_results[i] << std::endl;
            match = false;
        }
    }
    
    if (!match) {
        std::cout << "FAIL: Edge equation evaluation mismatch!" << std::endl;
        return false;
    }
    std::cout << "  ✓ Edge equations match exactly" << std::endl;
    
    // =========================================================================
    // TEST 2: Inside/outside classification
    // =========================================================================
    std::cout << "Test 2: Inside/outside classification..." << std::endl;
    
    // Calculate all three edges
    float edge0[8], edge1[8], edge2[8];
    
    // Edge 0 coefficients (already calculated above)
    SIMD::evaluate_edge(a0, b0, c0, pixel_x, pixel_y, edge0);
    
    // Edge 1: from v1 to v2
    float a1 = y2 - y1;  // = 100
    float b1 = x1 - x2;  // = 50
    float c1 = x2*y1 - x1*y2;  // = -25000
    SIMD::evaluate_edge(a1, b1, c1, pixel_x, pixel_y, edge1);
    
    // Edge 2: from v2 to v0
    float a2 = y0 - y2;  // = -100
    float b2 = x2 - x0;  // = 50
    float c2 = x0*y2 - x2*y0;  // = 5000
    SIMD::evaluate_edge(a2, b2, c2, pixel_x, pixel_y, edge2);
    
    // Test inside/outside
    uint8_t scalar_mask = SIMD::test_inside_scalar(edge0, edge1, edge2);
    uint8_t simd_mask = SIMD::test_inside(edge0, edge1, edge2);
    
    if (scalar_mask != simd_mask) {
        std::cout << "  FAIL: Inside mask mismatch!" << std::endl;
        std::cout << "    Scalar mask: " << std::hex << (int)scalar_mask << std::dec << std::endl;
        std::cout << "    SIMD mask:   " << std::hex << (int)simd_mask << std::dec << std::endl;
        return false;
    }
    std::cout << "  ✓ Inside/outside classification matches" << std::endl;
    std::cout << "  Inside mask: 0x" << std::hex << (int)simd_mask << std::dec
              << " (" << __builtin_popcount(simd_mask) << " pixels inside)" << std::endl;
    
    // =========================================================================
    // TEST 3: Performance comparison
    // =========================================================================
    std::cout << "Test 3: Performance test..." << std::endl;
    
    const int iterations = 1000000;
    
    // Scalar timing
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; iter++) {
        SIMD::evaluate_edge_scalar(a0, b0, c0, pixel_x, pixel_y, scalar_results);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double scalar_time = std::chrono::duration<double, std::milli>(end - start).count();
    
    // SIMD timing
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; iter++) {
        SIMD::evaluate_edge(a0, b0, c0, pixel_x, pixel_y, simd_results);
    }
    end = std::chrono::high_resolution_clock::now();
    double simd_time = std::chrono::duration<double, std::milli>(end - start).count();
    
    std::cout << "  Scalar: " << scalar_time << "ms for " << iterations << " iterations" << std::endl;
    std::cout << "  SIMD:   " << simd_time << "ms for " << iterations << " iterations" << std::endl;
    std::cout << "  Speedup: " << (scalar_time / simd_time) << "x" << std::endl;
    
    // =========================================================================
    // TEST 4: Edge cases
    // =========================================================================
    std::cout << "Test 4: Edge cases..." << std::endl;
    
    // Test with negative values
    float negative_x[8] = {-10.0f, -5.0f, 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f};
    SIMD::evaluate_edge_scalar(a0, b0, c0, negative_x, -50.0f, scalar_results);
    SIMD::evaluate_edge(a0, b0, c0, negative_x, -50.0f, simd_results);
    
    for (int i = 0; i < 8; i++) {
        if (scalar_results[i] != simd_results[i]) {
            std::cout << "  FAIL: Negative value mismatch!" << std::endl;
            return false;
        }
    }
    
    // Test with zero coefficients
    SIMD::evaluate_edge_scalar(0.0f, 1.0f, 0.0f, pixel_x, pixel_y, scalar_results);
    SIMD::evaluate_edge(0.0f, 1.0f, 0.0f, pixel_x, pixel_y, simd_results);
    
    for (int i = 0; i < 8; i++) {
        if (scalar_results[i] != simd_results[i]) {
            std::cout << "  FAIL: Zero coefficient mismatch!" << std::endl;
            return false;
        }
    }
    
    std::cout << "  ✓ Edge cases handled correctly" << std::endl;
    
    // =========================================================================
    // Detect SIMD availability
    // =========================================================================
    std::cout << "\nSIMD implementation used: ";
#ifdef HAS_AVX2
    std::cout << "AVX2 (8 pixels)" << std::endl;
#elif defined(HAS_SSE2)
    std::cout << "SSE2 (4 pixels x2)" << std::endl;
#elif defined(HAS_NEON)
    std::cout << "ARM NEON (4 pixels x2)" << std::endl;
#else
    std::cout << "Scalar fallback" << std::endl;
#endif
    
    std::cout << "✓ SIMD edge equation test passed!" << std::endl;
    return true;
}

// Missing include for chrono
#include <chrono>