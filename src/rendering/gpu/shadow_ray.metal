// shadow_ray.metal
// GPU compute kernel for ray-triangle intersection (Möller-Trumbore algorithm)
//
// EDUCATIONAL IMPLEMENTATION:
// This is Phase I MVP - single shadow ray, single triangle, no BVH
// Purpose: Learn Metal compute fundamentals, validate GPU pipeline
//
// ALGORITHM: Möller-Trumbore Ray-Triangle Intersection
// Paper: "Fast, Minimum Storage Ray/Triangle Intersection" (1997)
// WHY: Efficient - no plane equations, no 2D projections, direct barycentric coords
// HOW: Solve ray-triangle intersection as a linear system using vector algebra
//
// REFERENCES:
// - https://en.wikipedia.org/wiki/Möller–Trumbore_intersection_algorithm
// - https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-rendering-a-triangle

#include <metal_stdlib>
#include "optimization_flags.metal"
#include "gpu_constants.metal"
#include "gpu_types.metal"
#include "gpu_math.metal"
using namespace metal;

// =========================================================================
// MÖLLER-TRUMBORE ALGORITHM EXPLANATION (For Educational Reference)
// =========================================================================
// See inline comments in ray_intersects_triangle() above
//
// THE FEYNMAN EXPLANATION:
//
// THE PHYSICAL INTUITION (How to "see" this algorithm):
// -------------------------------------------------------
//
// IMAGINE YOU'RE HOLDING A LASER POINTER:
// - The ray is your laser beam: starts at your hand (origin), goes straight (direction)
// - The triangle is a flat piece of paper floating in space with 3 corners (v0, v1, v2)
// - QUESTION: Does your laser beam hit the paper?
//
// THE CLEVER TRICK (Why Möller-Trumbore is beautiful):
// ----------------------------------------------------
//
// Instead of asking "where does the beam hit in 3D space?", we ask:
// "IF the beam hits, what are the coordinates ON THE TRIANGLE?"
//
// Think of the triangle like a map:
// - v0 is the "home base" (origin of our triangle coordinate system)
// - edge1 = (v1 - v0) is the "X axis" of the triangle (pointing from v0 to v1)
// - edge2 = (v2 - v0) is the "Y axis" of the triangle (pointing from v0 to v2)
//
// Any point P on the triangle can be written as:
//   P = v0 + u*edge1 + v*edge2
//
// Where (u, v) are "triangle coordinates" (called barycentric coordinates):
// - u = how far along edge1 (0 at v0, 1 at v1)
// - v = how far along edge2 (0 at v0, 1 at v2)
// - Valid triangle points have: u >= 0, v >= 0, and u + v <= 1
//
// VISUAL EXAMPLE:
//        v2
//        /\
//       /  \      If u=0.3 and v=0.2, we're 30% toward v1 and 20% toward v2
//      /____\     This point is INSIDE the triangle because 0.3 + 0.2 = 0.5 <= 1
//    v0      v1
//
// THE MATH (Setting up the equation):
// -----------------------------------
//
// Ray equation:       P(t) = ray_origin + t * ray_direction
// Triangle equation:  P(u,v) = v0 + u*edge1 + v*edge2
//
// If they intersect, these must be equal:
//   ray_origin + t*ray_direction = v0 + u*edge1 + v*edge2
//
// Rearrange to standard form:
//   t*ray_direction - u*edge1 - v*edge2 = v0 - ray_origin
//
// This is a linear system with 3 unknowns (t, u, v) and 3 equations (X, Y, Z components)
// We can write it as a matrix:
//
//   [ -ray_direction | edge1 | edge2 ] * [t]   [v0 - ray_origin]
//                                         [u] =
//                                         [v]
//
// CRAMER'S RULE (Solving without matrix inversion):
// -------------------------------------------------
//
// Cramer's rule lets us solve this system using determinants and cross products.
// The key insight: cross products give us determinants for free!
//
// Let's define:
//   pvec = cross(ray_direction, edge2)  // Perpendicular to both ray and edge2
//   det = dot(edge1, pvec)               // Determinant of our matrix
//
// WHY THIS WORKS:
// - The determinant tells us if the system has a solution
// - If det ≈ 0, the ray is parallel to the triangle (never intersects OR lies in plane)
// - If det != 0, we can solve for u, v, t using the formula:
//
//   u = dot(tvec, pvec) / det     where tvec = ray_origin - v0
//   v = dot(ray_direction, qvec) / det     where qvec = cross(tvec, edge1)
//   t = dot(edge2, qvec) / det
//
// PHYSICAL MEANING OF EACH VARIABLE:
// ----------------------------------
//
// t = Distance along the ray from origin to intersection point
//     - If t < 0: intersection is BEHIND the ray (we're looking the wrong way)
//     - If t > 0: intersection is in front (valid hit)
//
// u = How far along edge1 (v0 → v1 direction)
//     - If u < 0: we're "to the left" of the triangle
//     - If u > 1: we're "past v1" (outside the triangle)
//
// v = How far along edge2 (v0 → v2 direction)
//     - If v < 0: we're "below" the triangle
//     - If v > 1: we're "past v2" (outside the triangle)
//
// u + v = Total barycentric coordinate
//     - If u + v > 1: we're outside the opposite edge from v0
//     - Triangle interior requires: u >= 0, v >= 0, u + v <= 1
//
// VISUAL CHECK (Debugging with your eyes):
// ----------------------------------------
//
// When debugging, imagine standing at the ray origin looking along ray_direction:
//
// 1. Is det near zero?
//    → Ray is parallel to triangle (glancing blow or perfectly aligned)
//
// 2. Is u negative?
//    → Hit point would be "to the left" of v0-v1 edge
//
// 3. Is v negative?
//    → Hit point would be "below" v0-v2 edge
//
// 4. Is u + v > 1?
//    → Hit point would be "beyond" the v1-v2 edge (opposite side from v0)
//
// 5. Is t negative?
//    → Triangle is behind you (intersection in the past)
//
// 6. All checks pass?
//    → We have a valid hit! The ray pierces the triangle at distance t.
//
// CODE WALKTHROUGH (Following the math above):
// --------------------------------------------
// See inline comments below for step-by-step implementation

kernel void trace_shadow_ray(
    device const Ray* ray [[buffer(0)]],           // Input: Ray to trace
    device const Triangle* triangle [[buffer(1)]], // Input: Triangle to test
    device float* result [[buffer(2)]],            // Output: 0.0=shadow, 1.0=lit
    uint thread_id [[thread_position_in_grid]]     // Thread ID (unused for single ray)
) {
    // Load ray and triangle from device memory
    float3 ray_origin = float3(ray->origin);
    float3 ray_direction = float3(ray->direction);

    float3 v0 = float3(triangle->v0);
    float3 v1 = float3(triangle->v1);
    float3 v2 = float3(triangle->v2);

    // STEP 1: Compute triangle edges (our coordinate system on the triangle)
    // These become the "X" and "Y" axes of the triangle's local space
    float3 edge1 = v1 - v0;  // Edge from v0 to v1 (first basis vector)
    float3 edge2 = v2 - v0;  // Edge from v0 to v2 (second basis vector)

    // STEP 2: Begin Möller-Trumbore algorithm
    // Compute pvec = ray_direction × edge2
    // This vector is perpendicular to BOTH ray_direction and edge2
    // It helps us find the u coordinate (distance along edge1)
    float3 pvec = cross(ray_direction, edge2);

    // STEP 3: Compute determinant
    // det = edge1 · pvec
    // PHYSICAL MEANING: Measures how "non-parallel" the ray is to the triangle
    // - If det ≈ 0: ray is parallel to triangle (never hits or lies in plane)
    // - If det > 0: ray hits front face
    // - If det < 0: ray hits back face
    float det = dot(edge1, pvec);

    // STEP 4: Check if ray is parallel to triangle
    // If determinant is near zero, ray is parallel to triangle plane
    // We test abs(det) to handle both front and back face hits
    if (abs(det) < RAY_EPSILON) {
        result[0] = 1.0f;  // No intersection (miss) - pixel is lit
        return;
    }

    // STEP 5: Compute inverse determinant (used repeatedly below)
    // We'll multiply by this instead of dividing by det (faster)
    float inv_det = 1.0f / det;

    // STEP 6: Calculate u parameter and test bounds
    // tvec = vector from v0 to ray origin
    // u = (tvec · pvec) / det
    // PHYSICAL MEANING: How far along edge1 is the intersection?
    // - u = 0: intersection at v0
    // - u = 1: intersection at v1
    // - u < 0 or u > 1: intersection outside triangle along edge1 direction
    float3 tvec = ray_origin - v0;
    float u = dot(tvec, pvec) * inv_det;

    if (u < 0.0f || u > 1.0f) {
        result[0] = 1.0f;  // Outside triangle bounds - pixel is lit
        return;
    }

    // STEP 7: Calculate v parameter and test bounds
    // qvec = tvec × edge1
    // v = (ray_direction · qvec) / det
    // PHYSICAL MEANING: How far along edge2 is the intersection?
    // - v = 0: intersection at v0
    // - v = 1: intersection at v2
    // - v < 0 or u + v > 1: intersection outside triangle
    float3 qvec = cross(tvec, edge1);
    float v = dot(ray_direction, qvec) * inv_det;

    if (v < 0.0f || u + v > 1.0f) {
        result[0] = 1.0f;  // Outside triangle bounds - pixel is lit
        return;
    }

    // STEP 8: Calculate t parameter (ray distance to intersection)
    // t = (edge2 · qvec) / det
    // PHYSICAL MEANING: How far along the ray is the intersection?
    // - t < 0: intersection is behind ray origin (we're looking away from it)
    // - t = 0: intersection exactly at ray origin (we're ON the triangle)
    // - t > 0: intersection is in front (valid hit)
    float t = dot(edge2, qvec) * inv_det;

    // STEP 9: Check if intersection is in front of ray origin
    // Use small epsilon to avoid self-intersection (ray starting on surface)
    // This prevents a surface from casting shadows on itself due to floating-point errors

    if (t > RAY_MIN_T) {
        // Valid intersection - ray hits triangle
        // The laser pointer beam hit the piece of paper!
        result[0] = 0.0f;  // Pixel is in shadow
    } else {
        // Intersection behind ray origin or too close (self-intersection)
        result[0] = 1.0f;  // Pixel is lit
    }
}

// =========================================================================
// MULTI-TRIANGLE SHADOW RAY - Phase I-B
// =========================================================================
// Test a single ray against multiple triangles (linear search, no BVH)
//
// PURPOSE: Educational step before BVH implementation
// - Understand GPU array upload patterns
// - Measure O(N) baseline performance
// - Early-exit optimization (return on first hit)
//
// ALGORITHM: Loop through all triangles, return 0.0 on first hit
// PERFORMANCE: O(N) where N = triangle count
// - 10 triangles: ~0.5ms
// - 100 triangles: ~5ms
// - 1000+ triangles: BVH required!

kernel void trace_shadow_ray_multi_triangle(
    device const Ray* ray [[buffer(0)]],              // Input: Single ray to trace
    device const Triangle* triangles [[buffer(1)]],   // Input: Array of N triangles
    constant uint& triangle_count [[buffer(2)]],      // Input: How many triangles
    device float* result [[buffer(3)]],               // Output: 0.0=shadow, 1.0=lit
    uint thread_id [[thread_position_in_grid]]        // Thread ID (unused for single ray)
) {
    // Load ray from device memory (single ray for backward compatibility)
    float3 ray_origin = float3(ray->origin);
    float3 ray_direction = float3(ray->direction);

    // Test ray against all triangles (early exit on first hit)
    for (uint tri_idx = 0; tri_idx < triangle_count; tri_idx++) {
        // Load triangle vertices
        float3 v0 = float3(triangles[tri_idx].v0);
        float3 v1 = float3(triangles[tri_idx].v1);
        float3 v2 = float3(triangles[tri_idx].v2);

        // Möller-Trumbore algorithm (same as single-triangle kernel)
        float3 edge1 = v1 - v0;
        float3 edge2 = v2 - v0;
        float3 pvec = cross(ray_direction, edge2);
        float det = dot(edge1, pvec);

        // Parallel check
        if (abs(det) < RAY_EPSILON) {
            continue;  // Skip this triangle, try next
        }

        float inv_det = 1.0f / det;
        float3 tvec = ray_origin - v0;
        float u = dot(tvec, pvec) * inv_det;

        // u bounds check
        if (u < 0.0f || u > 1.0f) {
            continue;
        }

        float3 qvec = cross(tvec, edge1);
        float v = dot(ray_direction, qvec) * inv_det;

        // v bounds check
        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }

        float t = dot(edge2, qvec) * inv_det;

        // Distance check (with self-intersection prevention)
        if (t > RAY_MIN_T) {
            // HIT! Ray blocked by this triangle
            // Early exit: no need to test remaining triangles
            result[0] = 0.0f;  // Shadow
            return;
        }
    }

    // Tested all triangles, no hits
    result[0] = 1.0f;  // Lit (no occlusion)
}

// =========================================================================
// PARALLEL SHADOW RAYS - Phase I-C
// =========================================================================
// Trace multiple rays in parallel - one GPU thread per ray
//
// PURPOSE: Demonstrate GPU parallelism - the key advantage!
// - Each thread is independent (no synchronization needed)
// - N rays tested simultaneously (true parallelism)
// - Typical use: 64 rays (8×8 tile) or 256 rays (16×16 tile)
//
// ALGORITHM: Each thread:
// 1. Uses thread_id to select its ray from array
// 2. Tests ray against all triangles (linear search)
// 3. Writes result to results[thread_id]
//
// PERFORMANCE: O(N) per ray, but all rays run in parallel!
// - 64 rays × 100 triangles = 6400 tests, but only ~5ms total (not 64×5ms!)
// - GPU parallelism: 10,000+ threads can run simultaneously
// - SIMD groups: 32 threads execute together (divergence matters!)
//
// PHYSICAL INTUITION:
// - CPU: Like one person checking 64 rays sequentially (slow)
// - GPU: Like 64 people each checking their own ray simultaneously (fast!)

kernel void trace_shadow_rays_parallel(
    device const Ray* rays [[buffer(0)]],             // Input: Array of N rays
    device const Triangle* triangles [[buffer(1)]],   // Input: Array of M triangles
    constant uint& ray_count [[buffer(2)]],           // Input: How many rays
    constant uint& triangle_count [[buffer(3)]],      // Input: How many triangles
    device float* results [[buffer(4)]],              // Output: N results (0.0=shadow, 1.0=lit)
    uint thread_id [[thread_position_in_grid]]        // Thread ID (which ray am I?)
) {
    // Bounds check: Only process valid rays
    // (GPU might dispatch more threads than rays due to threadgroup sizing)
    if (thread_id >= ray_count) {
        return;  // Extra thread, do nothing
    }

    // Each thread gets its own ray from the array
    float3 ray_origin = float3(rays[thread_id].origin);
    float3 ray_direction = float3(rays[thread_id].direction);

    // Test this ray against all triangles (early exit on first hit)
    for (uint tri_idx = 0; tri_idx < triangle_count; tri_idx++) {
        // Load triangle vertices
        float3 v0 = float3(triangles[tri_idx].v0);
        float3 v1 = float3(triangles[tri_idx].v1);
        float3 v2 = float3(triangles[tri_idx].v2);

        // Möller-Trumbore algorithm (same as other kernels)
        float3 edge1 = v1 - v0;
        float3 edge2 = v2 - v0;
        float3 pvec = cross(ray_direction, edge2);
        float det = dot(edge1, pvec);

        // Parallel check
        if (abs(det) < RAY_EPSILON) {
            continue;
        }

        float inv_det = 1.0f / det;
        float3 tvec = ray_origin - v0;
        float u = dot(tvec, pvec) * inv_det;

        if (u < 0.0f || u > 1.0f) {
            continue;
        }

        float3 qvec = cross(tvec, edge1);
        float v = dot(ray_direction, qvec) * inv_det;

        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }

        float t = dot(edge2, qvec) * inv_det;

        if (t > RAY_MIN_T) {
            // HIT! Ray blocked by this triangle
            // Write to THIS THREAD's result slot
            results[thread_id] = 0.0f;  // Shadow
            return;  // Early exit
        }
    }

    // Tested all triangles, no hits
    // Write to THIS THREAD's result slot
    results[thread_id] = 1.0f;  // Lit (no occlusion)
}

// =========================================================================
// BATCHED SHADOW RAYS - Phase II-A
// =========================================================================
// Process multiple rays per GPU thread (8 rays/thread)
// Foundation for BVH traversal optimization
//
// PURPOSE: Learn multiple-rays-per-thread pattern (Phase II foundation)
// - Each thread processes RAYS_PER_BATCH (8) rays sequentially
// - More work per thread → better GPU utilization
// - Foundation for BVH optimization (load BVH nodes once, test 8 rays)
//
// ALGORITHM: Each thread:
// 1. Calculate which rays it owns: [thread_id*8, thread_id*8+7]
// 2. For each ray in batch:
//    a. Test against all triangles (linear search, NO BVH yet)
//    b. Write result to results[ray_index]
// 3. Next thread processes next 8 rays
//
// DIFFERENCE FROM PARALLEL:
// - Parallel: 64 rays → 64 GPU threads (1 ray/thread)
// - Batched: 64 rays → 8 GPU threads (8 rays/thread)
// - Fewer threads, more work per thread
//
// WHY THIS MATTERS:
// - Phase II-B will add BVH traversal
// - BVH nodes loaded once per batch (8 rays share BVH path)
// - Spatially coherent rays (from same tile) → similar BVH paths
// - Foundation for packet traversal (Phase II-C)
//
// PERFORMANCE (Phase II-A, linear search):
// - Same as parallel (no BVH benefit yet)
// - Validates batching pattern works correctly
// - Baseline for Phase II-B BVH speedup measurement

kernel void trace_shadow_rays_batched(
    device const Ray* rays [[buffer(0)]],             // Input: Array of N rays
    device const Triangle* triangles [[buffer(1)]],   // Input: Array of M triangles
    constant uint& ray_count [[buffer(2)]],           // Input: Total rays (not batches!)
    constant uint& triangle_count [[buffer(3)]],      // Input: How many triangles
    device float* results [[buffer(4)]],              // Output: N results (0.0=shadow, 1.0=lit)
    uint thread_id [[thread_position_in_grid]]        // Thread ID (which batch am I?)
) {
    // Each thread processes GPU_RAYS_PER_BATCH rays (from optimization_flags.metal)
    uint ray_start = thread_id * GPU_RAYS_PER_BATCH;
    uint ray_end = min(ray_start + GPU_RAYS_PER_BATCH, ray_count);  // Handle partial batch

    // Process each ray in this thread's batch
    for (uint ray_idx = ray_start; ray_idx < ray_end; ray_idx++) {
        // Load this ray
        float3 ray_origin = float3(rays[ray_idx].origin);
        float3 ray_direction = float3(rays[ray_idx].direction);

        // Test this ray against all triangles (early exit on first hit)
        bool hit = false;
        for (uint tri_idx = 0; tri_idx < triangle_count && !hit; tri_idx++) {
            // Load triangle vertices
            float3 v0 = float3(triangles[tri_idx].v0);
            float3 v1 = float3(triangles[tri_idx].v1);
            float3 v2 = float3(triangles[tri_idx].v2);

            // Möller-Trumbore algorithm (same as other kernels)
            float3 edge1 = v1 - v0;
            float3 edge2 = v2 - v0;
            float3 pvec = cross(ray_direction, edge2);
            float det = dot(edge1, pvec);

            // Parallel check
            if (abs(det) < RAY_EPSILON) {
                continue;
            }

            float inv_det = 1.0f / det;
            float3 tvec = ray_origin - v0;
            float u = dot(tvec, pvec) * inv_det;

            if (u < 0.0f || u > 1.0f) {
                continue;
            }

            float3 qvec = cross(tvec, edge1);
            float v = dot(ray_direction, qvec) * inv_det;

            if (v < 0.0f || u + v > 1.0f) {
                continue;
            }

            float t = dot(edge2, qvec) * inv_det;

            if (t > RAY_MIN_T) {
                // HIT! Ray blocked by this triangle
                hit = true;  // Exit inner loop
            }
        }

        // Write result for this ray
        results[ray_idx] = hit ? 0.0f : 1.0f;
    }

    // Thread done - processed all rays in batch
}

// =========================================================================
// BVH TRAVERSAL KERNEL - Phase II-B
// =========================================================================
// O(log N) BVH traversal vs O(N) linear search
// Expected: ~11 BVH nodes vs 1464 triangles (122× fewer tests)

kernel void trace_shadow_rays_batched_bvh(
    device const Ray* rays [[buffer(0)]],
    device const BVHNode* bvh [[buffer(1)]],          // NEW: BVH tree
    device const Triangle* triangles [[buffer(2)]],
    constant uint& ray_count [[buffer(3)]],
    device float* results [[buffer(4)]],
    uint thread_id [[thread_position_in_grid]])
{
    // Each thread processes GPU_RAYS_PER_BATCH rays (from optimization_flags.metal)
    uint ray_start = thread_id * GPU_RAYS_PER_BATCH;
    uint ray_end = min(ray_start + GPU_RAYS_PER_BATCH, ray_count);

    // Process each ray in batch
    for (uint ray_idx = ray_start; ray_idx < ray_end; ray_idx++) {
        float3 ray_origin = float3(rays[ray_idx].origin);
        float3 ray_direction = float3(rays[ray_idx].direction);
        float max_distance = rays[ray_idx].max_distance;  // CRITICAL: Distance to light

        // Stack for iterative BVH traversal (32 levels max)
        int stack[32];
        int stack_ptr = 0;
        stack[stack_ptr++] = 0;  // Start at root

        bool hit = false;

        // Traverse BVH
        while (stack_ptr > 0 && !hit) {
            int node_idx = stack[--stack_ptr];
            BVHNode node = bvh[node_idx];

            // Test ray vs AABB
            if (!ray_intersects_aabb(ray_origin, ray_direction,
                                    float3(node.bbox_min), float3(node.bbox_max))) {
                continue;  // Skip subtree
            }

            // Leaf? Test triangle
            if (node.triangle_idx >= 0) {
                Triangle tri = triangles[node.triangle_idx];
                // CRITICAL: Pass max_distance to prevent "mirror shadow" bug
                if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                           float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                    hit = true;  // Early exit
                }
            } else {
                // Internal node - push children
                if (node.left_child >= 0 && stack_ptr < 32) {
                    stack[stack_ptr++] = node.left_child;
                }
                if (node.right_child >= 0 && stack_ptr < 32) {
                    stack[stack_ptr++] = node.right_child;
                }
            }
        }

        results[ray_idx] = hit ? 0.0f : 1.0f;
    }
}

// =========================================================================
// DEBUGGING TIPS:
// =========================================================================
// If shadows look wrong, check these common issues:
//
// 1. MISSING SHADOWS (false negatives):
//    - EPSILON too large → parallel rays rejected incorrectly
//    - MIN_T too large → close hits rejected as self-intersection
//    - Back-face culling → only testing abs(det), might need det > EPSILON
//
// 2. INCORRECT SHADOWS (false positives):
//    - EPSILON too small → floating-point errors cause spurious hits
//    - MIN_T too small → surface shadows itself
//    - Triangle winding order → vertices v0,v1,v2 might be clockwise instead of CCW
//
// 3. PERFORMANCE ISSUES:
//    - Single-triangle kernel: O(1) per ray-triangle test (very fast!)
//    - Multi-triangle kernel: O(N) linear search (slow for large N)
//    - Bottleneck will be testing MANY triangles → need BVH (Phase II)
//    - GPU divergence if many rays miss → batch coherent rays together
// =========================================================================
