#ifndef GPU_CONSTANTS_METAL
#define GPU_CONSTANTS_METAL

// =========================================================================
// GPU CONSTANTS - Single Source of Truth
// =========================================================================
// All constants used across Metal GPU shaders
// Must match C++ constants in optimization_flags.h and lighting_config.h

// =========================================================================
// RAY-TRIANGLE INTERSECTION CONSTANTS
// =========================================================================
// From optimization_flags.h:RAY_TRIANGLE_EPSILON
constant float RAY_EPSILON = 0.0000001f;      // Möller-Trumbore parallel ray detection

// From optimization_flags.h:RAY_DISTANCE_EPSILON
constant float RAY_MIN_T = 0.001f;            // Self-intersection prevention (surface shadowing itself)

// Default maximum ray distance (replaced by per-ray max_distance in shadow rays)
constant float RAY_MAX_T = 1000.0f;           // Used in ray_intersects_aabb()

// =========================================================================
// PHYSICS CONSTANTS
// =========================================================================
// From lighting_config.h
constant float FOUR_PI = 12.566370f;          // 4π for solid angle calculations (sphere)
constant float MIN_DISTANCE_SQ = 0.01f;       // 0.1² minimum distance squared (avoid 1/0 singularity)

// =========================================================================
// GPU TUNING PARAMETERS
// =========================================================================
// NOTE: GPU_RAYS_PER_BATCH still in optimization_flags.metal
// Will be moved here in STEP 10 (Consolidate optimization_flags.metal)

#endif // GPU_CONSTANTS_METAL
