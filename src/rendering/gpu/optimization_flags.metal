#ifndef OPTIMIZATION_FLAGS_METAL
#define OPTIMIZATION_FLAGS_METAL

// =========================================================================
// DEPRECATED: Merged into gpu_constants.metal
// =========================================================================
// This file kept for backward compatibility only
// New code should use gpu_constants.metal, gpu_types.metal, gpu_math.metal
//
// Shadow ray batching: number of rays processed per GPU thread
// Higher = more work per thread = better GPU utilization
// Tested values: 1 (slow), 4 (OK), 8 (optimal), 16 (diminishing returns), 32 (too high - GPU hangs)
constant uint GPU_RAYS_PER_BATCH = 16;  // Balanced workload - 32 causes GPU to never complete

// Surface offset along normal to avoid self-intersection in shadow rays.
// Mirror of C++ Optimizations::SHADOW_RAY_NORMAL_OFFSET.
constant float SHADOW_RAY_NORMAL_OFFSET = 0.05f;

#endif // OPTIMIZATION_FLAGS_METAL
