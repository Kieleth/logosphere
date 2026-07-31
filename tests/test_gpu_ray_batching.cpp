// test_gpu_ray_batching.cpp
// Phase II-A: Validate multiple-rays-per-thread pattern

#include "../src/test_context.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include <iostream>
#include <cmath>

using namespace Logosphere;
// Disambiguate: use GPU version of ShadowRay/ShadowTriangle (not CPU bvh.h version)
using GpuShadowRay = Logosphere::ShadowRay;
using GpuShadowTriangle = Logosphere::ShadowTriangle;

// =========================================================================
// TEST: GPU Ray Batching (8 rays per thread)
// =========================================================================
// PURPOSE: Validate batched kernel produces same results as parallel kernel
// PATTERN: Each GPU thread processes 8 rays sequentially
// FOUNDATION: For Phase II-B (BVH traversal)

bool test_gpu_ray_batching(TestContext& ctx) {
    std::cout << "\n=== GPU Ray Batching Test (Phase II-A) ===" << std::endl;

    MetalComputeBridge gpu;
    if (!gpu.initialize()) {
        std::cerr << "❌ FAIL: GPU not available" << std::endl;
        return false;
    }

    std::cout << "✅ GPU initialized" << std::endl;

    // =====================================================================
    // TEST 1: Single batch (8 rays) - basic validation
    // =====================================================================
    {
        std::cout << "\nTest 1: Single batch (8 rays)..." << std::endl;

        // Create 8 rays pointing in different directions
        GpuShadowRay rays[8];
        rays[0] = GpuShadowRay(0, 0, 0,  1, 0, 0);  // +X
        rays[1] = GpuShadowRay(0, 0, 0, -1, 0, 0);  // -X
        rays[2] = GpuShadowRay(0, 0, 0,  0, 1, 0);  // +Y
        rays[3] = GpuShadowRay(0, 0, 0,  0,-1, 0);  // -Y
        rays[4] = GpuShadowRay(0, 0, 0,  0, 0, 1);  // +Z
        rays[5] = GpuShadowRay(0, 0, 0,  0, 0,-1);  // -Z
        rays[6] = GpuShadowRay(0, 0, 0,  1, 1, 0);  // Diagonal
        rays[7] = GpuShadowRay(0, 0, 0,  1, 0, 1);  // Diagonal

        // Triangle blocking +X rays (at x=5, y=[-1,1], z=[-1,1])
        GpuShadowTriangle triangle(
            5, -1, -1,
            5,  1, -1,
            5,  0,  1
        );

        // Run batched kernel
        float results_batched[8];
        gpu.trace_shadow_rays_batched(rays, 8, &triangle, 1, results_batched);

        // Run parallel kernel (reference)
        float results_parallel[8];
        gpu.trace_shadow_rays_parallel(rays, 8, &triangle, 1, results_parallel);

        // Compare results
        bool all_match = true;
        for (int i = 0; i < 8; i++) {
            if (std::abs(results_batched[i] - results_parallel[i]) > 0.001f) {
                std::cerr << "❌ Ray " << i << ": batched=" << results_batched[i]
                          << " vs parallel=" << results_parallel[i] << std::endl;
                all_match = false;
            }
        }

        if (!all_match) {
            std::cerr << "❌ TEST 1 FAILED: Results don't match" << std::endl;
            return false;
        }

        std::cout << "✅ Test 1 passed: Batched matches parallel for 8 rays" << std::endl;
    }

    // =====================================================================
    // TEST 2: Multiple batches (64 rays = 8 batches)
    // =====================================================================
    {
        std::cout << "\nTest 2: Multiple batches (64 rays = 8 batches)..." << std::endl;

        // Create 64 rays (8×8 tile pattern)
        const int NUM_RAYS = 64;
        GpuShadowRay rays[NUM_RAYS];

        for (int i = 0; i < NUM_RAYS; i++) {
            // Spread rays in a grid pattern
            float angle = (i / float(NUM_RAYS)) * 6.28f;  // 0 to 2π
            rays[i] = GpuShadowRay(
                0, 0, 0,
                std::cos(angle), std::sin(angle), 0.5f
            );
        }

        // Triangle blocking half the rays
        GpuShadowTriangle triangle(
            2, -1, 0,
            2,  1, 0,
            2,  0, 1
        );

        // Run batched kernel
        float results_batched[NUM_RAYS];
        gpu.trace_shadow_rays_batched(rays, NUM_RAYS, &triangle, 1, results_batched);

        // Run parallel kernel (reference)
        float results_parallel[NUM_RAYS];
        gpu.trace_shadow_rays_parallel(rays, NUM_RAYS, &triangle, 1, results_parallel);

        // Compare results
        bool all_match = true;
        int num_shadow_batched = 0;
        int num_shadow_parallel = 0;

        for (int i = 0; i < NUM_RAYS; i++) {
            if (results_batched[i] < 0.5f) num_shadow_batched++;
            if (results_parallel[i] < 0.5f) num_shadow_parallel++;

            if (std::abs(results_batched[i] - results_parallel[i]) > 0.001f) {
                std::cerr << "❌ Ray " << i << ": batched=" << results_batched[i]
                          << " vs parallel=" << results_parallel[i] << std::endl;
                all_match = false;
            }
        }

        if (!all_match) {
            std::cerr << "❌ TEST 2 FAILED: Results don't match" << std::endl;
            return false;
        }

        std::cout << "✅ Test 2 passed: Batched matches parallel for 64 rays" << std::endl;
        std::cout << "   Shadow pixels: batched=" << num_shadow_batched
                  << " parallel=" << num_shadow_parallel << std::endl;
    }

    // =====================================================================
    // TEST 3: Partial batch (13 rays = 1 full batch + 5 rays)
    // =====================================================================
    {
        std::cout << "\nTest 3: Partial batch (13 rays)..." << std::endl;

        const int NUM_RAYS = 13;
        GpuShadowRay rays[NUM_RAYS];

        for (int i = 0; i < NUM_RAYS; i++) {
            float angle = (i / float(NUM_RAYS)) * 6.28f;
            rays[i] = GpuShadowRay(
                0, 0, 0,
                std::cos(angle), std::sin(angle), 0.2f
            );
        }

        // Triangle blocking some rays
        GpuShadowTriangle triangle(
            1, -0.5f, 0,
            1,  0.5f, 0,
            1,  0,    0.5f
        );

        // Run batched kernel
        float results_batched[NUM_RAYS];
        gpu.trace_shadow_rays_batched(rays, NUM_RAYS, &triangle, 1, results_batched);

        // Run parallel kernel (reference)
        float results_parallel[NUM_RAYS];
        gpu.trace_shadow_rays_parallel(rays, NUM_RAYS, &triangle, 1, results_parallel);

        // Compare results
        bool all_match = true;
        for (int i = 0; i < NUM_RAYS; i++) {
            if (std::abs(results_batched[i] - results_parallel[i]) > 0.001f) {
                std::cerr << "❌ Ray " << i << ": batched=" << results_batched[i]
                          << " vs parallel=" << results_parallel[i] << std::endl;
                all_match = false;
            }
        }

        if (!all_match) {
            std::cerr << "❌ TEST 3 FAILED: Partial batch handling incorrect" << std::endl;
            return false;
        }

        std::cout << "✅ Test 3 passed: Partial batch (13 rays) handled correctly" << std::endl;
    }

    // =====================================================================
    // ALL TESTS PASSED
    // =====================================================================
    std::cout << "\n✅ ALL GPU RAY BATCHING TESTS PASSED" << std::endl;
    std::cout << "   Phase II-A MVP: Multiple-rays-per-thread pattern validated" << std::endl;
    std::cout << "   Ready for Phase II-B: BVH integration" << std::endl;

    return true;
}
