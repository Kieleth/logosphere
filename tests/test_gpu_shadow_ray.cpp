// test_gpu_shadow_ray.cpp
// Direct GPU validation - no CPU duplication needed
//
// PHILOSOPHY: Test GPU implementation with obvious cases
// - Ray hits triangle center → must return 0.0 (shadow)
// - Ray misses triangle → must return 1.0 (lit)
// - If wrong, debug GPU code (don't create CPU reference)
//
// KISS: Test the thing directly, not a copy of the thing

#include "test_context.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include <iostream>
#include <cmath>

using namespace Logosphere;
// Disambiguate: use GPU version of ShadowRay (not CPU bvh.h version)
using GpuShadowRay = Logosphere::ShadowRay;
using GpuShadowTriangle = Logosphere::ShadowTriangle;

bool test_gpu_shadow_ray(TestContext& ctx) {
    std::cout << "\n=== GPU Shadow Ray - Direct Validation ===" << std::endl;

    // Initialize Metal GPU compute
    MetalComputeBridge bridge;

    if (!bridge.initialize()) {
        std::cerr << "[FAIL] Failed to initialize Metal compute" << std::endl;
        std::cerr << "  This test requires Metal support (macOS 10.11+)" << std::endl;
        std::cerr << "  Skipping GPU test on this platform" << std::endl;
        return true;  // Skip test on non-Metal platforms
    }

    std::cout << "[GPU TEST] Metal initialized successfully" << std::endl;

    bool all_passed = true;

    // =========================================================================
    // TEST 1: Ray hits triangle dead center
    // =========================================================================
    // Triangle: Simple right triangle in XY plane at Z=0
    // v0 = (0, 0, 0)
    // v1 = (1, 0, 0)
    // v2 = (0, 1, 0)
    //
    // Ray: From above (0.25, 0.25, 1) pointing down (0, 0, -1)
    // This ray MUST hit the triangle (barycentric coords u=0.25, v=0.25)
    {
        GpuShadowRay ray(
            0.25f, 0.25f, 1.0f,  // origin: above triangle center
            0.0f,  0.0f, -1.0f   // direction: straight down
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 0.0f) {
            std::cout << "[PASS] Test 1: Ray hits triangle center → 0.0 (shadow)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 1: Expected 0.0 (hit), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 2: Ray misses triangle completely
    // =========================================================================
    // Same triangle, but ray origin is way off to the side
    // Ray: From (10, 10, 1) pointing down
    // This ray MUST miss (outside triangle bounds)
    {
        GpuShadowRay ray(
            10.0f, 10.0f, 1.0f,  // origin: far from triangle
            0.0f,  0.0f, -1.0f   // direction: straight down
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 1.0f) {
            std::cout << "[PASS] Test 2: Ray misses triangle → 1.0 (lit)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 2: Expected 1.0 (miss), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 3: Ray parallel to triangle
    // =========================================================================
    // Ray travels along XY plane, never intersects
    // Ray: From (0.5, 0.5, 0) pointing right (1, 0, 0)
    // Parallel to triangle plane, should miss (det ≈ 0)
    {
        GpuShadowRay ray(
            0.5f, 0.5f, 0.0f,  // origin: in plane
            1.0f, 0.0f, 0.0f   // direction: parallel to plane
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 1.0f) {
            std::cout << "[PASS] Test 3: Ray parallel to triangle → 1.0 (miss)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 3: Expected 1.0 (parallel), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 4: Ray hits triangle edge
    // =========================================================================
    // Ray hits exactly on edge v0-v1
    // Ray: From (0.5, 0, 1) pointing down
    // Should hit (u=0.5, v=0, on edge)
    {
        GpuShadowRay ray(
            0.5f, 0.0f, 1.0f,  // origin: above edge v0-v1
            0.0f, 0.0f, -1.0f  // direction: straight down
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 0.0f) {
            std::cout << "[PASS] Test 4: Ray hits triangle edge → 0.0 (shadow)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 4: Expected 0.0 (edge hit), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 5: Ray behind triangle (t < 0)
    // =========================================================================
    // Ray points away from triangle
    // Ray: From (0.25, 0.25, 1) pointing UP (0, 0, 1)
    // Triangle is behind ray, should miss
    {
        GpuShadowRay ray(
            0.25f, 0.25f, 1.0f,  // origin: above triangle
            0.0f,  0.0f,  1.0f   // direction: pointing up (away from triangle)
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 1.0f) {
            std::cout << "[PASS] Test 5: Ray behind triangle (t<0) → 1.0 (miss)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 5: Expected 1.0 (behind), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 6: Ray hits vertex
    // =========================================================================
    // Ray hits exactly at vertex v0
    // Ray: From (0, 0, 1) pointing down
    // Should hit (u=0, v=0, at v0)
    {
        GpuShadowRay ray(
            0.0f, 0.0f, 1.0f,  // origin: above v0
            0.0f, 0.0f, -1.0f  // direction: straight down
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 0.0f) {
            std::cout << "[PASS] Test 6: Ray hits vertex → 0.0 (shadow)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 6: Expected 0.0 (vertex hit), got " << result << std::endl;
            all_passed = false;
        }
    }

    // =========================================================================
    // TEST 7: Self-intersection prevention
    // =========================================================================
    // Ray starts very close to triangle (simulates self-intersection)
    // Ray: From (0.25, 0.25, 0.0001) pointing down
    // Should miss due to MIN_T epsilon (0.001 in shader)
    {
        GpuShadowRay ray(
            0.25f, 0.25f, 0.0001f,  // origin: very close to triangle
            0.0f,  0.0f, -1.0f      // direction: toward triangle
        );

        GpuShadowTriangle triangle(
            0.0f, 0.0f, 0.0f,  // v0
            1.0f, 0.0f, 0.0f,  // v1
            0.0f, 1.0f, 0.0f   // v2
        );

        float result = bridge.trace_shadow_ray(ray, triangle);

        if (result == 1.0f) {
            std::cout << "[PASS] Test 7: Self-intersection prevention → 1.0 (miss)" << std::endl;
        } else {
            std::cout << "[FAIL] Test 7: Expected 1.0 (too close), got " << result << std::endl;
            all_passed = false;
        }
    }

    if (all_passed) {
        std::cout << "[SUCCESS] All GPU shadow ray tests passed!" << std::endl;
    } else {
        std::cout << "[FAILURE] Some GPU tests failed - check shader implementation" << std::endl;
    }

    return all_passed;
}
