#ifndef OPTIMIZATION_FLAGS_H
#define OPTIMIZATION_FLAGS_H

#include <cstddef>  // size_t

// =========================================================================
// ENGINE OPTIMIZATION FLAGS - Single source of truth for all optimizations
// Centralized configuration for all performance optimizations across the engine
//
// IMPORTANT: When changing these flags, rebuild the entire project as they
// affect multiple systems (rendering, camera, lighting, etc.)
// =========================================================================

// NOTE: Shadow sampling pattern selection lives in
// src/rendering/shadow_sampling_config.h so the headless profile does
// not pull in the rendering-only strategy templates.

namespace Optimizations {
    // =========================================================================
    // PHYSICS SYSTEM CONTROL
    // Enable/disable physics for performance benchmarking
    //
    // WHY: Separate rendering performance from physics overhead
    // HOW: PhysicsSystem::update() early-exits if disabled
    // IMPACT: ~40ms savings with 30K kinematic particles (physics-free scenes)
    // USE CASE: Perf tests, rendering benchmarks, static scenes
    // =========================================================================
    constexpr bool ENABLE_PHYSICS = true;  // Disable for pure rendering benchmarks

    // Rendering optimizations
    constexpr bool USE_DEPTH_BUFFER = true;      // Re-enabled after confirming not the cause
    constexpr bool USE_SQUARED_DEPTH = true;     // Re-enabled for performance
    constexpr bool USE_SCANLINE_OPT = true;      // Re-enabled after testing
    constexpr bool USE_EARLY_Z = true;           // Test depth BEFORE shading to eliminate overdraw
    
    // Camera/projection optimizations
    constexpr bool USE_INCREMENTAL_EDGES = true; // Re-enabled - helps with scaling
    constexpr bool USE_FAST_RECT_PATH = true;    // Re-enabled - 3ms improvement
    
    // Lighting optimizations
    constexpr bool USE_DISTANCE_CULLING = true;  // Re-enabled
    constexpr bool USE_SURFACE_CACHE = true;     // Re-enabled - critical 50ms savings!
    
    // =========================================================================
    // TILE-BASED RASTERIZATION (MANDATORY)
    // Processes screen in small tiles that fit in CPU cache for better memory locality
    // Value must be 4, 8, or 16 (power of 2)
    // Scanline rendering has been removed - only tile-based is supported
    // 
    // WHY: Modern CPUs have 32KB L1 cache. Random framebuffer access = cache misses.
    // HOW: Process all pixels in small tile before moving to next tile.
    // IMPACT: 2-5x speedup from better cache usage (measured by Abrash at Valve)
    // 
    // THREADING NOTE (2025-01-22): Changed from 8×8 to 16×16 for better thread granularity
    // - 8×8 = 105K tiles (too fine-grained for threading)
    // - 16×16 = 26K tiles (better work unit size per thread)
    // - 16×16 = 1KB depth buffer per tile (still fits in L1 cache)
    //
    // GPU NOTE (2025-10-04): Tested 16×16 vs 256×256 with GPU shadows
    // - 16×16 = 9.8% SLOWER (21.1 FPS vs 23.4 FPS)
    // - GPU needs LARGE batches (10K+ threads to saturate)
    // - 256×256 = 65K pixels/tile → ~100K shadow rays/batch → good GPU utilization
    // - CPU optimization (small tiles) ≠ GPU optimization (large batches)
    // =========================================================================
    constexpr int TILE_SIZE = 256;  // Optimal for GPU shadow rays (large batches)
    
    // =========================================================================
    // BVH (Bounding Volume Hierarchy) for shadow rays
    // Accelerates shadow ray testing from O(N²) to O(N log N)
    // 
    // WHY: With 20 particles, each pixel tests 20 potential blockers = slow
    // HOW: Hierarchical boxes skip entire groups of non-intersecting particles
    // IMPACT: 20 particles: ~38x faster, 100 particles: ~495x faster (theoretical)
    // =========================================================================
    constexpr bool USE_BVH = true;   // Enabled - 20x speedup measured with 20 particles
    
    // BVH-specific optimizations (only apply when USE_BVH is true)
    // =========================================================================
    
    // FRONT-TO-BACK TRAVERSAL: Visit nearer child nodes first
    // WHY: Can skip far subtree if near subtree contains a hit
    // HOW: Sort children by ray entry distance instead of center distance
    // IMPACT: Expected 20-30% fewer nodes visited
    constexpr bool BVH_FRONT_TO_BACK = true;  // Better child ordering during traversal
    
    // SAH SPLITTING: Use Surface Area Heuristic for better tree balance
    // WHY: Current median split creates unbalanced trees (depth 7 for 39 nodes)
    // HOW: Split to minimize expected traversal cost, not particle count
    // IMPACT: Expected 2-3x speedup from better tree structure
    constexpr bool BVH_USE_SAH = true;  // Use SAH for better tree balance
    
    // SKIP PARTICLE OPTIMIZATION: Don't test source particle in BVH
    // WHY: 77% of leaf tests are wasted on skip_particle_idx
    // ATTEMPTED: Mark subtrees containing skip particle to skip branches
    // RESULT: 2.2x SLOWER! (171ns vs 77ns per ray)
    // PROBLEM: Marking overhead (77 nodes × 2 × 115K rays = 17.8M ops)
    // CONCLUSION: Simple leaf-level skip is sufficient
    constexpr bool BVH_OPTIMIZE_SKIP = false;  // Disabled - overhead exceeds benefit
    
    // SIMD RAY-AABB INTERSECTION: Test both children simultaneously
    // WHY: BVH always tests both children, can do in parallel
    // HOW: Pack 2 AABBs into SIMD registers, test against same ray
    // TESTED: No improvement on Apple Silicon (67ns vs 65.95ns per ray)
    // ISSUE: SIMD overhead exceeds benefit for just 2 AABBs
    constexpr bool BVH_SIMD_DUAL_TEST = false;  // Disabled - no performance gain
    
    // WIDE BVH TREE: Use 8-way tree instead of binary for better SIMD
    // WHY ATTEMPTED: Testing 8 AABBs with SIMD could amortize setup overhead
    // HOW: Each node has up to 8 children, test all with SIMD in parallel
    // 
    // MEASURED RESULTS (39 particles):
    // - Binary BVH: 67ns per ray, 7.1 nodes visited, 10 AABB tests
    
    // ========== MULTI-LIGHT OPTIMIZATIONS ==========
    
    // LIGHT CULLING: Skip lights that can't affect current tile
    // WHY: Each additional light causes linear FPS drop (2 lights = 50% FPS)
    // HOW: Pre-filter lights per tile using sphere-box intersection
    // IMPACT: Expected 2-3x speedup for multi-light scenes
    constexpr bool USE_LIGHT_CULLING = true;
    constexpr float LIGHT_CULL_MARGIN = 1.1f;  // 10% safety margin
    
    // INTERLEAVED LIGHT PROCESSING: Process lights then pixels for cache locality
    // WHY: Current per-pixel light loop causes 30% cache miss rate on BVH traversal
    // HOW: Swap loop order - for(light) for(pixel) instead of for(pixel) for(light)
    // IMPACT: 6x reduction in cache misses, 35% overall speedup for multi-light scenes
    // NOTE: Requires accumulation buffer, trades memory for cache efficiency
    constexpr bool USE_INTERLEAVED_LIGHTS = true;
    
    // RAY BATCHING: Batch multiple shadow rays into single BVH calls
    // Phase 0 results: 1.1% improvement from reduced function call overhead
    constexpr bool USE_RAY_BATCHING = true;  // ENABLED - foundation for SIMD
    
    // SIMD RAY BATCHING: Process batched rays with SIMD instructions
    // ⚠️  WARNING: Experimental feature, requires USE_RAY_BATCHING = true
    // WHY: 4x computational throughput on ray-box tests using SSE/AVX
    // HOW: Process 4 rays simultaneously through BVH traversal
    // EXPECTED: 2-3x speedup on coherent ray packets (rays to same light)
    //
    // NOTE: Wide tree experiment (8-way) was tested and removed (29% slower)
    // Binary tree with SAH is optimal for incoherent shadow rays
    constexpr bool USE_SIMD_RAY_BATCHING = true;   // EXPERIMENTAL - testing performance
    
    // =========================================================================
    // FRUSTUM CULLING
    // Skip particles that are completely outside the camera's view frustum
    // 
    // WHY: No point generating 12 surfaces for a particle that's off-screen
    // HOW: Test particle bounding box against view frustum before surface generation
    // IMPACT: Varies by camera position, typically 10-40% reduction in surfaces
    // =========================================================================
    constexpr bool USE_FRUSTUM_CULLING = true;  // Skip off-screen particles entirely

    // =========================================================================
    // ENTITY-LEVEL FRUSTUM CULLING
    // Pre-cull entire entities before per-particle frustum tests
    //
    // WHY: 87K particles but only ~7K entities = 12x fewer frustum tests
    // HOW: Compute entity AABB from particles, test AABB against frustum
    // IMPACT: ~3-5ms savings by skipping particles of off-screen entities
    // =========================================================================
    constexpr bool USE_ENTITY_FRUSTUM_CULLING = true;  // Pre-cull entities before particles

    // =========================================================================
    // SPATIAL GRID CULLING (Two-Tier Chunk Streaming Phase 1)
    // Use 10m micro-chunk grid instead of iterating all entities
    //
    // WHY: Entity iteration scans 112K entities even if most are off-screen
    // HOW: Spatial grid groups particles by 10m cells, query only visible cells
    // IMPACT: ~100 cell tests instead of 112K entity tests
    // NOTE: Alternative to USE_ENTITY_FRUSTUM_CULLING (use one or the other)
    // =========================================================================
    constexpr bool USE_SPATIAL_GRID_CULLING = false;  // TESTING - compare with entity culling

    // =========================================================================
    // ASYNC CHUNK LOADING (Two-Tier Chunk Streaming Phase 2)
    // Load chunks on background thread to avoid frame hitches
    //
    // WHY: Chunk creation causes 100-500ms stalls when crossing boundaries
    // HOW: Background thread runs create_callback, main thread applies results
    // LIMITATION: Current generators use add_particle() which blocks on lock
    //             For full async, generators should use queue_particle_addition()
    // =========================================================================
    constexpr bool USE_ASYNC_CHUNK_LOADING = true;  // Phase 2 - async chunk loading

    // =========================================================================
    // CAMERA PREDICTION PRE-LOADING (Two-Tier Chunk Streaming Phase 3)
    // Pre-loads chunks in the direction of camera movement
    //
    // WHY: Chunks appear before player reaches them (smoother streaming)
    // HOW: Track camera velocity, predict position 2s ahead, pre-load those chunks
    // REQUIRES: USE_ASYNC_CHUNK_LOADING enabled
    // =========================================================================
    constexpr bool USE_CAMERA_PREDICTION = true;  // Phase 3 - camera prediction

    // =========================================================================
    // HIERARCHICAL CULLING  
    // Test particle bounding box for visibility before generating 12 surfaces
    // 
    // WHY: Avoid generating and testing 12 surfaces if particle is not visible
    // HOW: Do a quick bounds test first, only generate surfaces if visible
    // IMPACT: Reduces surface generation overhead and culling tests
    // =========================================================================
    constexpr bool USE_HIERARCHICAL_CULLING = true;  // Test particle bounds before surfaces
    
    // =========================================================================
    // OCCLUSION CULLING
    // Skip particles that are completely hidden behind other particles
    // 
    // WHY: No point processing particles we can't see
    // HOW: Test particle bounding box corners against depth buffer
    // IMPACT: In dense scenes, 20-50% reduction in surfaces to process
    // NOTE: Uses previous frame's depth buffer for temporal coherence
    // =========================================================================
    constexpr bool USE_OCCLUSION_CULLING = true;  // Skip hidden particles

    // =========================================================================
    // KINEMATIC FAST CULLING OPTIMIZATION
    // =========================================================================
    // WHY: Kinematic particles with no rotation don't need rotation-safe bounding
    //      sphere calculation (6 projections). Simple AABB check suffices (1 projection).
    // HOW: Detect is_kinematic && rotation==0, use simplified frustum test
    // IMPACT: ~80% reduction in projection calls for floor tiles
    // =========================================================================
    constexpr bool USE_KINEMATIC_FAST_CULLING = true;  // Fast path for static floor tiles

    // =========================================================================
    // UV→WORLD CACHE
    // Caches UV to 3D world position conversions (pure function, perfect for caching!)
    // 
    // WHY: UV→World takes 96% of lighting time! Bilinear interpolation per pixel
    // HOW: Hash table caches recent UV→XYZ lookups, reuse for nearby pixels
    // IMPACT: Expected 50-80% cache hit rate for nearby pixels
    // =========================================================================
    constexpr bool USE_UV_CACHE = false;  // FAILED: Actually slower + visual artifacts!
    constexpr size_t UV_CACHE_SIZE = 1024;  // Number of cached entries (power of 2)
    
    // =========================================================================
    // SCANLINE UV COHERENCE
    // Exploits spatial coherence - adjacent pixels have predictable UV values
    // 
    // WHY: Newton-Raphson UV calculation takes 12ms out of 20ms frame time (60%)
    // HOW: For each scanline, compute delta and use addition instead of full bilinear
    // IMPACT: 8.2x speedup for UV→World (16 muls + 9 adds → 3 adds per pixel)
    // =========================================================================
    constexpr bool USE_SCANLINE_COHERENCE = true;  // Testing new optimization
    
    // =========================================================================
    // UV GRADIENT TILES  
    // Pre-computes UV gradients per tile, then uses simple linear interpolation
    // 
    // WHY: Barycentric UV calculation = 9 ops per pixel (3 muls + 6 adds)
    // HOW: Compute du/dx and dv/dy once per tile, then just 2 adds per pixel
    // TESTED: Failed - overhead negates savings (6.85ms → 7.23ms worse)
    // ISSUE: 35% gradient success rate, overhead of computing 3 UV points per surface
    // =========================================================================
    constexpr bool USE_UV_GRADIENT_TILES = false;  // Disabled - not effective
    
    // =========================================================================
    // INCREMENTAL SCALING
    // Optimizes framebuffer scaling by avoiding division operations per pixel
    // 
    // WHY: Scaling from low res (1280x960) to Retina (3200x2400) = 7.68M pixel ops!
    //      Each pixel does expensive division: src_x = x / scale
    // HOW: Pre-compute inverse scale, use incremental addition instead of division
    // IMPACT: ~30% faster scaling for lower resolutions (measured in present_framebuffer)
    // NOTE: This explains why RETINA is faster - no scaling needed at all!
    // =========================================================================
    constexpr bool USE_INCREMENTAL_SCALING = true;  // Enabled - fixes lower res being slower
    
    // =========================================================================
    // FAST NEAREST NEIGHBOR SCALING
    // Uses optimized nearest-neighbor scaling with pre-computed lookup tables
    // 
    // WHY: Even with incremental scaling, we spend 7.5ms scaling 2.2M pixels
    // HOW: Pre-compute source indices for each destination row/column
    // IMPACT: Expected 50% reduction in scaling time
    // =========================================================================
    constexpr bool USE_FAST_NN_SCALING = true;  // Use lookup-based scaling
    
    // =========================================================================
    // WORK STEALING - Fixed chunk size for thread work distribution
    // Threads grab multiple tiles at once to reduce atomic contention
    // 
    // WHY: Each atomic fetch_add() has overhead from cache line bouncing
    // HOW: Grab 4 tiles at once instead of 1, reducing atomics by 75%
    // IMPACT: Expected 5-10% speedup from reduced contention
    // =========================================================================
    constexpr bool USE_WORK_STEALING = true;    // Enabled - no penalty with proper alignment!
    constexpr int WORK_STEAL_CHUNK_SIZE = 16;    // Reduces atomic operations by 94%
    
    // Strided tile access for better cache separation
    // Currently tiles are processed linearly (0,1,2,3...) causing adjacent memory access
    // Strided access spreads threads across screen reducing cache conflicts
    constexpr bool USE_STRIDED_TILE_ACCESS = true;   // Enable strided tile distribution
    constexpr int TILE_ACCESS_STRIDE = 4;            // Tiles between each thread's work
    // With stride=4 and 4 threads:
    // Thread 0: tiles 0, 16, 32, 48...
    // Thread 1: tiles 4, 20, 36, 52...
    // Thread 2: tiles 8, 24, 40, 56...
    // Thread 3: tiles 12, 28, 44, 60...
    
    // =========================================================================
    // PRECISION CONSTANTS - Numerical tolerances for various calculations
    // =========================================================================
    
    // Ray-triangle intersection epsilon (Möller-Trumbore algorithm)
    constexpr float RAY_TRIANGLE_EPSILON = 0.0000001f;  // For parallel ray detection
    
    // Triangle edge epsilon for shadow rays
    // CRITICAL: When splitting quads into triangles, rays can slip through the 
    // diagonal seam between triangles due to floating-point precision.
    // This epsilon slightly expands triangle boundaries to prevent gaps.
    // Too small = shadow artifacts, too large = incorrect shadows
    constexpr float TRIANGLE_EDGE_EPSILON = 0.0001f;  // Positive to contract boundaries slightly
    
    // General ray distance epsilon 
    constexpr float RAY_DISTANCE_EPSILON = 0.001f;  // Minimum ray distance to avoid self-intersection
    
    // =========================================================================
    // SIMPLE THREADING - KISS Implementation (2025-01-26)
    // ONE worker thread for rendering, main thread also helps
    // 
    // WHY: Shadow rays are 89% of frame time and ARE parallelizable
    // HOW: One atomic counter, one worker thread, that's it
    // IMPACT: Expected 1.8x speedup with 2 threads total
    // =========================================================================
    constexpr bool USE_SIMPLE_THREADING = false; // Start disabled for testing
    constexpr int WORKER_THREAD_COUNT = 14;      // Number of persistent worker threads for rasterization (16 cores - 1 main - 1 OS)
    
    // =========================================================================
    // MAIN_THREAD_NO_RENDER - Keep main thread free for engine work
    // Main thread handles physics/input/game logic ONLY, no rendering
    // 
    // WHY: Main thread shouldn't block on expensive shadow rays
    // HOW: Only worker threads process tiles, main thread coordinates
    // IMPACT: More responsive input/physics, smoother frame pacing
    // =========================================================================
    constexpr bool MAIN_THREAD_NO_RENDER = true; // Main thread = engine only, worker thread = rendering only
    
    // Legacy parallel implementation (keep disabled)
    constexpr bool USE_PARALLEL_TILES = true;   // RE-ENABLED to investigate existing implementation
    constexpr int PARALLEL_THREAD_COUNT = 4;     // Not used when disabled
    
    // =========================================================================
    // PARALLEL SHADOW RAY BATCHING (2025-01-22)
    // Process shadow rays within each tile in parallel
    // 
    // WHY: Shadow rays are 89% of frame time but computed serially per pixel
    // HOW: Collect all shadow rays for a tile, then process in parallel
    // IMPACT: Expected 3-4x speedup (25 FPS → 75-85 FPS with 40 particles)
    // NOTE: Works best with thread-local BVH copies to avoid cache contention
    // =========================================================================
    constexpr bool USE_SHADOW_RAY_BATCHING = false; // Disabled - doing tile-level threading instead
    constexpr int SHADOW_BATCH_MIN_SIZE = 100;      // Minimum rays to parallelize
    
    // =========================================================================
    // SIMD VECTORIZATION
    // Process multiple pixels simultaneously using CPU vector instructions
    // 
    // WHY: Modern CPUs can process 4-8 operations in parallel (SSE/AVX/NEON)
    // HOW: Edge equations, UV calculations, lighting - all vectorizable
    // IMPACT: 4-8x throughput for vectorized operations
    // NOTE: Falls back to scalar if SIMD not available on platform
    // =========================================================================
    constexpr bool USE_SIMD = true;  // Enable SIMD optimizations where available
    
    // Debug flag to verify SIMD is being used
    constexpr bool DEBUG_SIMD = false;  // Set to true to see SIMD usage messages
    
    // =========================================================================
    // ADAPTIVE SHADOW SAMPLING (2025-09-28)
    // Test shadow rays at strategic points and reuse results in uniform regions
    // 
    // WHY: Shadow rays are 60% of frame time, many pixels have same shadow state
    // HOW: Test corners of 4x4 blocks, reuse if all corners match
    // IMPACT: Expected 25% reduction in shadow rays for typical scenes
    // NOTE: Maintains pixel-perfect accuracy - no approximations
    // =========================================================================
    constexpr bool USE_ADAPTIVE_SHADOWS = false;  // DISABLED - testing performance difference
    constexpr int SHADOW_BLOCK_SIZE = 8;         // Size of blocks (8x8 pixels)
    
    // =========================================================================
    // SHADOW SAMPLING — see src/rendering/shadow_sampling_config.h for the
    // Pattern enum + Scanline1D / Hierarchical2D tuning constants.
    // Only the flags below are referenced outside the rendering TU.
    // =========================================================================

    // Legacy flags (kept for compatibility)
    constexpr bool USE_SHADOW_SEGMENT_SAMPLING = true;
    constexpr int  SHADOW_SEGMENT_SIZE         = 21;  // mirrors Scanline1D::SEGMENT_SIZE
    constexpr bool SEGMENT_DISABLE_INTERPOLATION = true;
    
    // SINGLE PASS SHADOW PROCESSING: Process shadows during rasterization
    // WHY: Two-phase approach breaks surface coherence, creates gaps in scanlines
    // HOW: Shade pixels immediately per surface, maintain scanline continuity
    // EXPECTED: Better segment sampling effectiveness, improved cache locality
    constexpr bool USE_SINGLE_PASS_SHADOWS = true;  // NEW - enable single pass approach
}

// =========================================================================
// NATIVE PIXEL FORMAT OPTIMIZATION (2025-09-29)
// Render directly to platform's native pixel format to eliminate extraction
// 
// WHY: Extracting from EnhancedPixel (8 bytes) to BGRA (4 bytes) takes 8.4ms
// HOW: Use native format (BGRA on macOS) and sparse object map for mouse
// IMPACT: Expected 8.4ms reduction in frame time (76 → 84+ FPS)
// =========================================================================
// Preprocessor macros for compile-time switching (can't use namespace:: in #if)
#define USE_NATIVE_PIXEL_FORMAT 1  // 0 = disabled, 1 = enabled (TESTING PERFORMANCE)
#define NATIVE_PIXEL_SIZE 4        // BGRA/RGBA = 4 bytes per pixel

namespace Optimizations {  // Continue namespace
    
    // =========================================================================
    // GPU COMPUTE (Phase II-A Integration)
    // Move shadow ray calculation from CPU to GPU using Metal compute shaders
    //
    // WHY: CPU is memory-bandwidth limited for ray-BVH intersection (50 GB/s)
    // HOW: Offload shadow rays to GPU (400+ GB/s bandwidth, 10,000+ threads)
    // EXPECTED: 5-10x speedup for shadow-heavy scenes (after BVH optimization)
    //
    // PHASE I: Single shadow ray validation ✅
    // PHASE II-A: Batched rays integrated into renderer (measuring overhead)
    // PHASE II-B: BVH traversal on GPU (add triangles, speedup)
    // PHASE II-C: Single GPU batch per frame (fix overhead)
    //
    // Phase II-C: Single batch per frame (5ms overhead, not 400ms)
    // =========================================================================
    constexpr bool USE_GPU_SHADOW_RAYS = true;  // Phase II-C: Single GPU batch per frame

    // =========================================================================
    // GPU RASTERIZATION (Phase III Integration)
    // Move full rasterization pipeline from CPU to GPU using Metal compute shaders
    //
    // WHY: CPU rasterization is sequential, GPU has 10,000+ parallel threads
    // HOW: Upload surfaces to GPU, rasterize with inline Lambertian lighting
    // CURRENT: Phase A STEP 7 complete - lighting integration working in tests
    // NEXT: Integrate into render pipeline, measure performance vs CPU
    //
    // Phase A: Make It Work (pixel-perfect rendering)
    // Phase B: Make It Good (production code quality)
    // Phase C: Make It Fast (GPU faster than CPU)
    // =========================================================================
    constexpr bool USE_GPU_RASTERIZATION = true;  // Phase A: Enable full GPU rendering

    // =========================================================================
    // DEFERRED RENDERING (3-Pass Architecture)
    // Separate geometry, shadows, and lighting into coherent GPU passes
    //
    // WHY: Forward rendering has 5-10% warp efficiency (thread divergence on BVH)
    // HOW: Pass 1: G-buffer (geometry), Pass 2: shadows (coherent BVH), Pass 3: lighting
    // EXPECTED: 19× faster at 250 particles (10,000ms → 515ms)
    //
    // ROOT CAUSE FIX: Coherent BVH traversal (80-90% warp efficiency vs 5-10%)
    // TRADE-OFF: 181 MB memory overhead for massive performance gain
    //
    // NOTE: Only applies when USE_GPU_RASTERIZATION = true
    // =========================================================================
    // Testing if page fault is actually fixed (user reports no more page faults)
    // Previous issues that were supposedly fixed:
    // - Fixed G-buffer initialization (packed_float3 alignment + clearing)
    // - Fixed shadow_results buffer clearing
    // - Fixed tile binning causing dummy buffer access (tiles_x/y = 0)
    constexpr bool USE_DEFERRED_RENDERING = true;  // TESTING - verifying page fault is resolved

    // =========================================================================
    // TRANSPARENCY (Hybrid Forward+Deferred)
    // Semi-transparent surfaces rendered in a forward pass after deferred lighting
    //
    // WHY: Deferred rendering stores ONE surface per pixel (winner-take-all depth test).
    //      Transparent surfaces behind the winner are lost. This adds a forward pass
    //      that renders transparent geometry with alpha blending onto the lit framebuffer.
    // HOW: Opaque geometry → deferred pipeline (Pass 1-3, unchanged)
    //      Transparent geometry → forward pass (Pass 3.5) with inline lighting + blend
    // COST: ~1-2ms for <100 transparent surfaces (forward lighting is per-fragment)
    //
    // NOTE: Requires USE_GPU_RASTERIZATION = true and USE_DEFERRED_RENDERING = true
    // =========================================================================
    constexpr bool USE_TRANSPARENCY = true;   // Phase 1: Hybrid forward+deferred transparency

    // =========================================================================
    // GPU RAY BATCHING (Phase II-B - BVH Cache Coherency)
    // Process 8 pixels per thread to improve BVH cache reuse during shadow tracing
    //
    // WHY: BVH cache thrashing causes 2-3× slowdown for later lights (L0: 30ms → L5: 54ms)
    // HOW: Each thread processes 8 consecutive pixels sequentially (vs 1 pixel per thread)
    // HYPOTHESIS: Adjacent pixels trace similar rays → hit similar BVH nodes → cache reuse
    // EXPECTED: 30-50% speedup for later lights (L5: 54ms → 27-35ms)
    //
    // FOUNDATION: test_gpu_ray_batching validated 8 rays/thread pattern
    // RISK: Low (no algorithm changes, just execution pattern)
    // =========================================================================
    constexpr bool USE_GPU_RAY_BATCHING = true;  // ENABLED for testing - Iteration 7J tested: NO performance gain (36.7 vs 36.8 FPS = -0.3%). BVH cache coherence hypothesis REJECTED. Adjacent pixels have LOW ray coherence, single-pass dispatch provides no cache benefits. Batched kernel works correctly but no advantage over per-light dispatch.

    // =========================================================================
    // METAL RAY TRACING (M3+ Hardware Acceleration)
    // Use dedicated RT cores for BVH traversal instead of compute units
    //
    // WHY: RT cores have dedicated silicon for ray-AABB and ray-triangle tests
    //      Measured speedup: 12× for shadow pass (0.3ms vs 3.5ms)
    //
    // HOW: Driver-built acceleration structure replaces software BVH
    //      intersector<triangle_data> API replaces manual traversal
    //
    // A/B TESTING: Set to false to compare compute BVH vs Metal RT
    // =========================================================================
    constexpr bool USE_METAL_RT = false;  // DISABLED: Test if compute BVH fixes DYNAMICS shadows

    // =========================================================================
    // METAL RT ACCELERATION STRUCTURE REBUILD THRESHOLD
    // Skip AS rebuild for small triangle count changes to reduce CPU overhead
    //
    // WHY: AS rebuilds every frame when triangle count changes (even by 1)
    //      Observed: 670 AS rebuilds during movement due to particle streaming
    // HOW: Only rebuild AS when triangle count changes by > threshold %
    //      Small changes use stale AS (minimal visual impact)
    //
    // TRADE-OFF: Stale AS may cause:
    //   - Missed shadows (new triangles not in AS)
    //   - Ghost shadows (removed triangles still in AS) - rare
    //   Visual impact minimal for <5% changes, corrects within frames
    //
    // Set to 0.0f to always rebuild (current behavior)
    // Set to 0.05f (5%) to skip rebuild for typical streaming (recommended)
    // Set to 0.10f (10%) for more aggressive skip (fast movement)
    // =========================================================================
    constexpr float METAL_RT_AS_REBUILD_THRESHOLD = 0.05f;  // Only rebuild AS if triangle count changes >5%

    // =========================================================================
    // SOFT SHADOWS (Contact Hardening via Temporal Jitter)
    // Physically-based soft shadows using distance-aware temporal accumulation
    //
    // WHY: Point lights create unrealistic hard shadow edges
    //      Real lights have physical size → penumbra (soft edges)
    //
    // HOW: Jitter shadow ray direction within light's angular radius
    //      Accumulate over N frames for smooth result
    //      Penumbra width = (light_radius / light_distance) × blocker_distance
    //
    // COST: Zero extra rays per frame (same as hard shadows)
    //       Uses temporal buffer for accumulation
    //
    // VISUAL: Close blockers → hard shadow, far blockers → soft penumbra
    // =========================================================================
    constexpr bool USE_SOFT_SHADOWS = true;              // Master toggle for soft shadows
    constexpr float DEFAULT_LIGHT_RADIUS = 0.3f;         // 30cm default (torch-sized)
    constexpr int SOFT_SHADOW_TEMPORAL_FRAMES = 4;       // Frames to accumulate (4 for fast convergence, Halton-16 cycles independently)
    constexpr int SOFT_SHADOW_MAX_SAMPLES = 64;          // Running average cap (higher = smoother convergence, less per-frame swing)
    constexpr int PCSS_BLOCKER_SAMPLES = 8;               // Phase 1: blocker search (closest-hit)
    constexpr int PCSS_PENUMBRA_SAMPLES = 16;              // Phase 3: penumbra sampling (any-hit)
    constexpr float SHADOW_RAY_NORMAL_OFFSET = 0.01f;   // Surface offset along normal to avoid self-intersection (matches Metal constant; min_distance=0.001f handles self-hit)

    // =========================================================================
    // PENUMBRA MODE - Deterministic penumbra approach selection
    //
    // WHY: Stochastic PCSS requires 24 rays/pixel + temporal accumulation + denoise
    //      to approximate what can be computed geometrically with 1 ray/pixel
    //
    // MODES:
    //   NONE             - Hard shadows only (1 closest-hit ray/pixel/light, binary)
    //   PCSS             - Legacy stochastic PCSS (24 rays, temporal, denoise) [old path]
    //   BLOCKER_MAP      - Approach C: blocker depth neighborhood analysis
    //   BLOCKER_GRADIENT - A+C combined: depth-aware screen gradient
    //
    // When mode != PCSS: temporal accumulation, denoise, and pre-fill blit are bypassed
    // =========================================================================
    // SCREEN_GRADIENT (approach A alone) and SOLID_ANGLE (approach B) were
    // retired 2026-07-29: A was never selected once A+C existed, and B was
    // an enum value that never had a dispatch path at all.
    enum class PenumbraMode {
        NONE,              // Hard shadows (baseline, no penumbra)
        PCSS,              // Legacy stochastic PCSS (unchanged old path)
        BLOCKER_MAP,       // Approach C: blocker depth neighborhood analysis
        BLOCKER_GRADIENT   // A+C combined: depth-aware screen gradient (SHIPPING)
    };
    constexpr PenumbraMode PENUMBRA_MODE = PenumbraMode::BLOCKER_GRADIENT;
    // SHIPPED 2026-07-24: the V-blur walks up to 65 rows per pixel and
    // read particle_id through the 32-byte GBufferPixel stride (a wasted
    // cacheline per tap). The H-blur emits a packed uint id buffer as a
    // second output; the V-blur taps that. Same ids, same comparisons —
    // BYTE-IDENTICAL oracles; retina 19.8 -> 17.1 ms (A-B-A sandwich).
    // MUST mirror PENUMBRA_COMPACT_IDS in gbuffer_types.metal.
    constexpr bool PENUMBRA_COMPACT_IDS = true;

    // =========================================================================
    // GLOBAL ILLUMINATION (Single Bounce) - Legacy ray-based GI
    // Light bouncing off surfaces for indirect lighting
    //
    // WHY: Direct lighting only = harsh shadows, no color bleeding
    //      Real light bounces off surfaces, creating softer illumination
    //
    // HOW: When shadow ray hits blocker, sample blocker's color
    //      Add fraction of light as indirect contribution
    //
    // COST: ~20% overhead (color lookup + bounce calculation)
    // =========================================================================
    constexpr bool USE_GLOBAL_ILLUMINATION = false;      // Master toggle for GI (Phase 2)
    constexpr float GI_BOUNCE_STRENGTH = 0.3f;           // 30% energy transfer per bounce

    // =========================================================================
    // SCREEN-SPACE GI — RETIRED 2026-07-29
    // USE_SSGI / USE_BVH_INDIRECT_GI and their kernels, buffers, denoiser and
    // Pass 3 read are gone (replaced by SSDO Pass 2.7 + DDGI Pass 2.5b/c).
    // Removing them returned 147 MB of GPU memory at 1600x1200 and a
    // ~107 MB/frame CPU memset at retina. Output was byte-identical: the
    // producers had been compile-time disabled and contributed zeros.
    // =========================================================================
    constexpr bool SKIP_SHADOW_DENOISE = false;           // DEBUG: bypass shadow denoise pass (raw shadowResultsBuffer → Pass 3)
    constexpr bool LOG_GPU_DEFERRED_TIMING = false;      // Log Pass 2.5 SSGI timing to console

    // SCREEN-SPACE AMBIENT OCCLUSION (SSAO)
    constexpr bool USE_SSAO = true;                       // Master toggle
    constexpr int SSAO_SAMPLE_COUNT = 16;                 // Hemisphere samples per pixel
    constexpr float SSAO_SCREEN_RADIUS = 32.0f;           // Screen-space radius (pixels)
    constexpr float SSAO_WORLD_RADIUS = 1.0f;             // World-space occlusion/bounce distance (meters)
    constexpr float SSAO_BIAS = 0.01f;                    // Self-occlusion prevention
    constexpr float SSAO_INTENSITY = 1.5f;                // AO darkening strength
    constexpr int SSAO_DENOISE_PASSES = 3;                // A-trous passes (step 1, 2, 4)
    constexpr float SSDO_BOUNCE_STRENGTH = 2000.0f;       // Bounce color multiplier in apply_lighting
    // SHIPPED 2026-07-24: SSDO ping-pong buffers stored as half4 (8 B/px,
    // was 16). Retina 20.8 -> 19.9 ms measured; user-accepted +/-1-LSB
    // epsilon + Eden visual sign-off. Math stays float32 in registers;
    // storage is a plain half4 conversion (see GPU_OPT_LEDGER.md items
    // G2 + G5 — two earlier "fixes" here were falsified after the oracle
    // frame-skip nondeterminism was found and fixed). MUST mirror
    // SSDO_HALF_PRECISION in gbuffer_types.metal.
    constexpr bool SSDO_HALF_PRECISION = true;

    // DDGI (Dynamic Diffuse Global Illumination) — probe-based indirect lighting
    constexpr bool USE_DDGI = true;                       // Master toggle
    constexpr int DDGI_GRID_X = 16;                       // Probes along X
    constexpr int DDGI_GRID_Y = 4;                        // Probes along Y (vertical)
    constexpr int DDGI_GRID_Z = 8;                        // Probes along Z
    constexpr float DDGI_PROBE_SPACING = 2.5f;            // Meters between probes
    constexpr int DDGI_RAYS_PER_PROBE = 64;               // Rays per probe per frame
    constexpr float DDGI_HYSTERESIS = 0.03f;              // 3% new data per frame
    constexpr float DDGI_NORMAL_BIAS = 0.1f;              // Self-intersection avoidance
    constexpr float DDGI_INTENSITY = 3.0f;                // Indirect multiplier
    constexpr int DDGI_OCTAHEDRAL_SIZE = 8;               // 8x8 texels per probe face

    // =========================================================================
    // GPU TILE-BASED BINNING (GPU Rasterization Optimization)
    // Pre-assign triangles to screen tiles on CPU to reduce GPU bandwidth
    //
    // WHY: Naive GPU rasterization tests ALL triangles for EVERY pixel
    //      1,681,600 pixels × 7,200 triangles = 12.1 billion tests/frame
    //      Each test loads ~160 bytes = 1,900 GB/frame (vs 200 GB/s available)
    //
    // HOW: CPU divides screen into tiles, bins triangles to overlapping tiles
    //      GPU kernel only tests triangles assigned to current tile
    //
    // IMPACT: Expected 50-100x speedup (12.1B tests → 100M tests)
    //         Reduces bandwidth from 1,900 GB to ~16 GB per frame
    //
    // NOTE: This is DIFFERENT from CPU TILE_SIZE (256 for cache locality)
    //       GPU binning tiles are smaller for finer-grained culling
    // =========================================================================
    // EXPERIMENT 2026-07-30 (study S9): cache generated surface geometry per
    // particle and reuse it while the particle has not moved. Study S7 showed
    // render cost tracks SURFACES and S8 showed the cost is the per-surface
    // MATH (vertex/face transforms, centre+normal computation) rather than
    // allocation — so the only way to remove it is to not redo it. Most
    // particles in a real scene (Eden terrain) never move.
    //
    // SWAP SAFETY: the cache is keyed by particle index, and particle indices
    // move under swap-and-pop deletion. The entry therefore stores the
    // transform it was built from and is only reused when that still matches
    // the particle at that index — a swapped-in particle fails the check and
    // regenerates. A particle whose transform matches exactly would produce
    // identical geometry anyway.
    // RESULT (study S9): works as designed — 94-99% hit rate, render_collect
    // -24% (Eden) to -36% (static grid). But NO reproducible frame win: an
    // A-B-A on a CPU-bound 8000-particle scene gave 17.98 / 21.05 / 21.12 ms,
    // i.e. the two identical cache-ON runs differ by more than the apparent
    // gain. collect is a small share of the frame and CPU/GPU overlap absorbs
    // it. Default OFF until a scene exists where it crosses the noise floor.
    // Memory caveat: caching sphere geometry costs ~20 KB/particle at
    // subdivision 2 (320 surfaces), so a sphere-heavy scene pays real RAM.
    constexpr bool USE_RENDER_SURFACE_CACHE = false;

    // Reject back-facing surfaces where they are BORN, not one pass later.
    // collect_surfaces built a 144-byte SurfaceData for every surface and
    // cull_surfaces then deleted half of them: Eden 29,556 -> 14,808,
    // the falling-body ramp 469,688 -> 234,892. The test that decides
    // (CameraSystem::passes_backface_culling) needs only the surface centre
    // and normal, both of which exist the instant the surface is generated;
    // the frustum and distance tests beside it are unimplemented stubs that
    // return true. So the construction, the deque insert and the whole
    // remove_if pass over the discarded half were pure waste.
    // Light sources are exempt here exactly as they were in cull_surfaces.
    constexpr bool CULL_SURFACES_AT_GENERATION = true;

    // QUALITY / PERF: icosphere subdivision for SPHERE and ELLIPSOID.
    //   0 =    20 triangles (faceted D20)
    //   1 =    80
    //   2 =   320  (current default)
    //   3 = 1,280
    // Every downstream cost scales with this: surfaces built, SurfaceData
    // written, shadow triangles, BVH input, bytes uploaded. Level 1 is a 4x
    // cut on all of it. Whether it is visible is a judgement call, so it is
    // overridable at runtime without a rebuild:
    //   LOGOSPHERE_SPHERE_LOD=<0..4>
    // Compare with tests/test_sphere_lod_quality.cpp, which renders the same
    // scene at every level and reports the pixel differences between them.
    constexpr int SPHERE_SUBDIVISIONS = 2;

    // PROBE (2026-07-30): the penumbra JFA submits 7 command buffers per frame
    // (1 seed + 6 propagation steps), each one compute encoder with one
    // dispatch. Measured: 18 command buffers per frame, GPU span 23.14 ms to do
    // roughly 6 ms of work (Metal HUD), and the triple-buffer semaphore is not
    // released until the chain ends, so the CPU blocks 11.01 ms on it. If the
    // per-buffer boundary really costs ~1 ms, folding these 7 into 1 buffer with
    // one serially-dispatching encoder removes 6 boundaries for identical
    // output: within a command buffer Metal orders sequential dispatches, which
    // is the same ordering the separate buffers were relying on.
    constexpr bool MERGE_JFA_COMMAND_BUFFERS = true;

    constexpr bool USE_GPU_TILE_BINNING = true;     // Enable tile-based binning
    // SHIPPED 2026-07-24: per-pixel raster walked its tile's triangle
    // list copying the FULL 176-byte TriangleLit and recomputing the
    // screen bbox (6-way min/max) per candidate BEFORE the reject. A
    // precomputed int4 bbox stream (16 B/tri, built in the binning prep
    // from the same floats with the same casts) carries the reject path;
    // the full struct loads only on bbox pass. BYTE-IDENTICAL oracles;
    // retina 17.0 -> 16.7 ms (A-B-A sandwich, median 16.6 = the 60 FPS
    // target). MUST mirror RASTER_BBOX_STREAM in gbuffer_types.metal.
    constexpr bool RASTER_BBOX_STREAM = true;
    constexpr int GPU_BINNING_TILE_SIZE = 64;       // 64x64 tiles. Swept 2026-07-24 (ledger H2): 32 = -0.35 ms GPU retina but 3x CPU binning (0.75 -> 2.3 ms) and slightly worse at 1600 — kept 64. MUST mirror GPU_BINNING_TILE_SIZE_METAL in gbuffer_types.metal.

    // NOTE: GPU shader constants (GPU_RAYS_PER_BATCH, etc.) are in:
    //       src/rendering/gpu/optimization_flags.metal
    //       (Metal shaders cannot include C++ headers)

    // =========================================================================
    // METAL PRESENTATION CONTROL
    // Controls how frames are presented to the display
    //
    // ASYNC_PRESENTATION = false: Use presentsWithTransaction = YES (synchronous)
    //   - Blocks main thread during presentation (~170ms stall observed)
    //   - Guarantees frame completeness but causes CPU/GPU idle time
    //   - Results: 22 FPS with 30% CPU, 12% GPU usage
    //
    // ASYNC_PRESENTATION = true: Use presentsWithTransaction = NO (asynchronous)
    //   - Non-blocking presentation, main thread continues immediately
    //   - Allows CPU/GPU to start next frame while presenting current
    //   - Expected: 60 FPS with better CPU/GPU utilization
    // =========================================================================
    constexpr bool USE_ASYNC_PRESENTATION = true;  // FIX for 170ms present stall

    // =========================================================================
    // GPU BUFFER SLOTS (Triple-Buffering / Multi-Buffering)
    // Number of in-flight command buffers for async CPU/GPU overlap
    //
    // WHY: More slots = less CPU blocking when GPU falls behind
    // PATTERN: CPU submits frame N while GPU processes frames N-1, N-2, N-3
    // TESTED:
    //   - 2 slots: Good at 6-9 lights, CPU starvation at 10+ lights (5-16ms semaphore blocking)
    //   - 3 slots: Eliminates CPU blocking at 10-11 lights (measured 2025-01-24)
    // TRADE-OFF: More slots = more memory (~20MB per slot)
    // IMPACT: At 10+ lights, GPU Pass 2 = 34-42ms → CPU exhausts 2 slots → blocks
    //         With 3 slots, CPU never blocks → FPS limited by GPU only (~26-28 FPS expected)
    // ROOT CAUSE ANALYSIS: See docs/performance/GPU_260_PERCENT_ROOT_CAUSE.md
    // =========================================================================
    constexpr int GPU_BUFFER_SLOTS = 2;  // Reduced from 3 to 2 (2025-03-07): Save ~250MB GPU memory to prevent kernel panics. Tradeoff: CPU may block at 10+ lights.

    // =========================================================================
    // ASYNC GPU_PREP (2025-01-24)
    // Overlap CPU GPU_PREP with GPU execution to break FPS cap
    //
    // CURRENT BOTTLENECK (measured @ 9 lights):
    // - Frame time: 42.2ms = 14.1ms CPU prep + 31.5ms GPU (serial)
    // - Theoretical max: 1000 / (14.1 + 31.5) = 21.9 FPS ✓ matches observed
    //
    // ASYNC STRATEGY:
    // - While GPU renders Frame N (31.5ms), CPU preps Frame N+1 async (14.1ms)
    // - Triple-buffered prep data (prep_buffer_index, render_buffer_index)
    // - Expected: 1000 / 31.5 = 31.7 FPS (GPU time limited, not CPU+GPU serial)
    //
    // SAFETY: Falls back to sync if prep not ready when GPU finishes
    // =========================================================================
    constexpr bool USE_ASYNC_GPU_PREP = false;  // DISABLED: Test DYNAMICS shadow with sync prep

    // =========================================================================
    // GPU BUFFER STORAGE MODES (QW1 - Quick Win #1)
    // Use MTLResourceStorageModePrivate for GPU-only buffers
    //
    // WHY: GPU-only buffers (G-buffer, shadow results, depth) don't need CPU access
    // IMPACT: 134.4 MB freed from shared CPU/GPU memory (2 slots × 67.2 MB)
    // APPLE BEST PRACTICE: "GPU-only resources should use private storage mode"
    // EXPECTED: 2-5% improvement from reduced memory contention
    //
    // DISABLED: Use MTLResourceStorageModeShared (CPU+GPU accessible, wastes unified memory)
    // ENABLED: Use MTLResourceStorageModePrivate for G-buffer, shadow_results, depth_buffer
    // =========================================================================
    constexpr bool USE_GPU_PRIVATE_BUFFERS = false;  // QW1 DISABLED: Degrades min FPS by 4.2% (15.9 vs 16.6)

    // =========================================================================
    // RT ACCELERATION-STRUCTURE REFIT (2026-07 GPU campaign, item C)
    // The Metal AS was rebuilt from scratch over the full shadow-triangle
    // set every frame: measured 1.8 ms/frame GPU at retina (+ ~1 ms CPU
    // triangle regen) regardless of how little moved. With this flag the
    // AS is built once with Refit usage and REFITTED each frame while the
    // triangle count is unchanged (typical frame: things move, topology
    // does not); a full rebuild runs on count change and periodically
    // (every AS_FULL_REBUILD_INTERVAL frames) to keep tree quality from
    // degrading under sustained motion. Traversal results are exact
    // either way — refit changes build cost, never hit results (pixel
    // oracle gates the A/B). GPU_OPT_LEDGER.md entry C.
    // =========================================================================
    constexpr bool USE_RT_AS_REFIT = true;   // SHIPPED 2026-07-23: 1.85 -> 0.27 ms/frame; user-accepted +/-1-LSB epsilon on <=0.03% of pixels
    constexpr int  AS_FULL_REBUILD_INTERVAL = 240;  // frames between forced full rebuilds under refit

    // =========================================================================
    // GPU THREAD CONFIGURATION (QW3 - Quick Win #3)
    // Thread group size for Metal compute shaders - PER-PASS CONTROL
    //
    // WHY: Different passes have different computational complexity
    // BACKGROUND: Was using 8×8 = 64 threads (only 10% GPU utilization)
    // OPTIONS:
    //   - 8×8 = 64 threads (conservative, poor GPU utilization)
    //   - 16×16 = 256 threads (good balance, 4× more threads)
    //   - 32×32 = 1024 threads (maximum for Apple Silicon)
    //
    // QW3 RESULTS: Uniform 16×16 all passes = +1.5% avg FPS vs baseline 32×32
    // - Hypothesis (register spilling causing 10-25% gain) was INCORRECT
    // - Actual benefit: Consistent +1.5-1.8% average FPS improvement
    //
    // QW3 MINI-SPIKE: Test mixed thread group sizes
    // - THEORY: Heavy passes (shadow/BVH) benefit from smaller groups (less register pressure)
    // - THEORY: Light passes benefit from larger groups (better GPU utilization)
    // - TESTING: 32×32 for light passes, 16×16 for shadow pass
    // =========================================================================

    // Clear operations (simple memory fill)
    constexpr int GPU_THREADS_CLEAR = 32;  // RESET to pre-QW3 baseline

    // Forward rendering (geometry + BVH + lighting in one pass)
    constexpr int GPU_THREADS_FORWARD = 32;  // RESET to pre-QW3 baseline

    // Deferred Pass 1: G-buffer generation (geometry only, no BVH)
    constexpr int GPU_THREADS_GBUFFER = 16;  // QW3 RE-ENABLED: 16×16 thread groups (+1.5% vs 32×32)

    // Deferred Pass 2: Shadow rays (BVH traversal - THE BOTTLENECK)
    constexpr int GPU_THREADS_SHADOW = 16;  // QW3 RE-ENABLED: 16×16 thread groups (+1.8% min FPS)

    // Deferred Pass 3: Lighting (combine G-buffer + shadows)
    constexpr int GPU_THREADS_LIGHTING = 16;  // QW3 RE-ENABLED: 16×16 thread groups (+1.5% avg FPS)

    // =========================================================================
    // GPU BLIT TRANSFERS (QW4 - Quick Win #4)
    // Use GPU blit encoder to offload CPU→GPU data transfers
    //
    // WHY: 42.7 MB memcpy per frame (31.3 MB triangles + 1.92 MB BVH + 9.4 MB BVH triangles)
    // HOW: CPU writes to staging buffers (Shared), GPU blits to Private buffers
    // EXPECTED: Offload ~11.3 MB to GPU, save ~100-200μs CPU overhead
    //
    // IMPLEMENTATION:
    // - Staging buffers (Shared): CPU writes via memcpy
    // - GPU buffers (Private): Blit encoder copies staging → GPU
    // - Compute shaders: Read from GPU buffers
    // =========================================================================
    constexpr bool USE_GPU_BLIT_TRANSFERS = false;  // QW4 DISABLED: Degrades min FPS by 5.4% (15.7 vs 16.6)

    // =========================================================================
    // SPATIAL SORTING (BVH Cache Coherence Optimization)
    // Sort particles by 3D spatial position before GPU upload to improve cache locality
    //
    // WHY: BVH cache thrashing in Pass 2 (Shadow Rays) causes 2× frame time variance
    //      Adjacent pixels hit spatially-distant particles → different BVH paths → cache misses
    //      Current: Particles uploaded in creation order (random spatial distribution)
    //      Problem: 30-40 FPS avg with microdips to 17-19 FPS (stuttering)
    //
    // HOW: Sort particles using Morton codes (Z-order curve) for 3D spatial locality
    //      Morton code interleaves X/Y/Z bits to create space-filling curve
    //      Nearby particles in 3D space → nearby positions in sorted array
    //      GPU threads processing adjacent pixels → hit nearby particles → same BVH nodes in L1 cache
    //
    // EXPECTED: Min FPS improvement: 17 → 28-30 FPS (1.65-1.76× improvement)
    //           Reduces frame time variance from 2.06× to ~1.3×
    //           Eliminates microdip stuttering
    //
    // IMPLEMENTATION:
    // - Compute Morton code for each particle (3D position → 30-bit Morton code)
    // - Sort particle indices by Morton code (not particles themselves)
    // - Generate shadow triangles using sorted order
    // - Pure CPU operation, no GPU code changes
    // - Zero risk: sorting doesn't change rendering, only improves cache patterns
    //
    // REFERENCE: See GPU_DEFERRED_PIPELINE_ANALYSIS.md section "BVH CACHE COHERENCE"
    // =========================================================================
    constexpr bool USE_SPATIAL_SORTING = false;  // DISABLED - Tested, no improvement (hypothesis rejected)

    // =========================================================================
    // PARALLEL SURFACE COLLECTION
    // Parallelize particle-to-surface conversion across CPU cores
    //
    // WHY: With 32K particles, collect_surfaces takes 36.80ms (73% of frame time!)
    //      This starves the GPU - only 10.53ms GPU work vs 36.80ms CPU prep
    // HOW: Split particles across threads, each generates surfaces independently
    // EXPECTED: 4-8× speedup with 14 threads (36.80ms → 4-9ms)
    // IMPACT: 10 FPS → 50-66 FPS, GPU utilization increases as CPU feeds faster
    // =========================================================================
    constexpr bool USE_PARALLEL_SURFACE_COLLECTION = true;  // Parallelize collect_surfaces

    // =========================================================================
    // GEOMETRY CACHING (Phase 1.1 - COLLECT_SURFACES_BOTTLENECK.md)
    // Cache rotated vertices for identical (size, rotation) combinations
    //
    // WHY: collect_surfaces takes 12.2ms at 32K particles (40% of frame time)
    //      Most particles share size=1.0, rotation=(0,0,0) - expected 95% cache hit rate
    // HOW: Thread-local hash map caches rotated vertices, only translate position on hits
    // EXPECTED: 2-3× speedup (12.2ms → 4-6ms) by eliminating 168 ops per cache hit
    // RESULT: 100% hit rate achieved, but no FPS gain (GPU-bound)
    // =========================================================================
    constexpr bool USE_GEOMETRY_CACHE = true;  // Phase 1.1 COMPLETE - maintains CPU headroom

    // =========================================================================
    // SURFACE ALLOCATION BATCHING (Phase 1.3 - COLLECT_SURFACES_BOTTLENECK.md)
    // Pre-allocate thread-local surface storage to avoid repeated reallocations
    //
    // WHY: Growing vectors/deques trigger reallocations during surface generation
    //      Each particle generates 12 triangles, causing frequent memory allocation
    // HOW: Use std::vector with reserve() to pre-allocate estimated size
    //      Estimate: (particles_per_thread × 12 triangles × 80% pass culling)
    // EXPECTED: Minor improvement (memory allocation overhead reduction)
    // VALUE: Maintains CPU headroom for future features
    // =========================================================================
    constexpr bool USE_SURFACE_ALLOC_BATCHING = true;  // Phase 1.3 - pre-allocate surface vectors

    // =========================================================================
    // SHADOW DISTANCE CULLING
    // Skip shadow triangle generation for particles beyond a radius from camera
    //
    // WHY: With 90K floor tiles, generating 1M shadow triangles takes 600ms/frame
    // HOW: Only generate shadow triangles for particles within SHADOW_CULL_RADIUS
    //      Distant particles fade to black via GPU shader (distance fade)
    // IMPACT: Expected 20× FPS boost (1M triangles → 50K triangles)
    //
    // VISUAL: Creates "darkness beyond the campfire" effect
    // - 0 to SHADOW_CULL_RADIUS: Full shadow rays + accurate lighting
    // - RADIUS to RADIUS+FADE: Smooth fade from accurate → black
    // - Beyond: Black (no shadow computation)
    // =========================================================================
    // FIXED 2026-01-03: Now uses look-at target instead of camera position
    constexpr bool USE_SHADOW_DISTANCE_CULLING = true;   // ENABLED - reduces shadow triangles
    constexpr float SHADOW_CULL_RADIUS = 30.0f;          // Only cast shadows within 30m
    constexpr float SHADOW_FADE_START = 20.0f;            // GPU: begin shadow fade at 20m
    constexpr float SHADOW_FADE_END = 40.0f;              // GPU: complete fade at 40m (smoothstep)

    // =========================================================================
    // SHADOW QUALITY SETTINGS (Phase 1 - Resolution Scaling)
    // Render shadows at reduced resolution for performance vs quality tradeoff
    //
    // WHY: Shadow rays are 60-70% of GPU time, reducing pixels = fewer rays
    // HOW: Render shadow pass at lower resolution, upscale to full resolution
    // IMPACT: Expected 4× speedup at 0.5 scale (0.5 × 0.5 = 0.25 pixels)
    //
    // REFERENCE: GPU_QUALITY_SETTINGS.md, PHASE_1_SHADOW_RESOLUTION.md
    // =========================================================================

    // Shadow resolution scale (1.0 = full res, 0.5 = half res, 0.25 = quarter res)
    // Shadow dimensions are calculated at RUNTIME in GPURasterizer::initialize()
    // based on actual framebuffer size (window-dependent, not compile-time constant)
    // FULL RES by decision 2026-07-17: no quality-reducing optimizations
    // until truly blocked (see GPU_PIPELINE_AUDIT_2026-07.md, deferred
    // ledger). Measured: 0.5 lifts retina-native from 44.9 to 63.2 FPS
    // but quantizes hard shadow edges into 2-4 px staircase treads —
    // test_shadow_edge_quantization catches it (red at 0.5, green here).
    // If ever needed, the ledger path is an edge-aware (depth/id-weighted)
    // shadow upsample in apply_lighting_deferred with that same test as
    // the acceptance gate at 0.5.
    constexpr float SHADOW_RESOLUTION_SCALE = 1.0f;

    // Upscaling smoothing mode
    enum ShadowSmoothingMode {
        SMOOTHING_NONE,       // No smoothing (blocky, fast - testing only)
        SMOOTHING_BILINEAR,   // 2×2 bilinear interpolation (balanced - recommended)
        SMOOTHING_GAUSSIAN_3  // 3×3 Gaussian blur (smooth, slower - future)
    };
    constexpr ShadowSmoothingMode SHADOW_SMOOTHING = ShadowSmoothingMode::SMOOTHING_BILINEAR;

    // =========================================================================
    // TEMPORAL DISTRIBUTION (Phase 2 - Checkerboard Rendering)
    // Distribute lighting calculations across multiple frames for performance
    //
    // WHY: Reduces rays per frame by tracing different pixels each frame
    // HOW: Checkerboard pattern - trace subset of pixels this frame, reuse rest from previous
    // IMPACT: 50-80% ray reduction depending on frame count
    // TRADE-OFF: Ghosting on camera movement (acceptable per user preference)
    // NOTE: "Lighting" not "Shadows" - Pass 2 computes lighting+shadows together as lux values
    //
    // REFERENCE: GPU_QUALITY_SETTINGS.md, PHASE_2_TEMPORAL_DISTRIBUTION.md
    // =========================================================================

    // Enable temporal distribution
    constexpr bool USE_TEMPORAL_LIGHTING = true;

    // Frame distribution count (2, 3, 4, or 5 frames)
    // 2 = 50% reduction → ~44 FPS (simple checkerboard, minimal ghosting)
    // 3 = 66% reduction → ~66 FPS (diagonal stripes)
    // 4 = 75% reduction → ~88 FPS (diagonal stripes, good performance/quality balance)
    // 5 = 80% reduction → ~110 FPS (maximum performance, more ghosting)
    constexpr int TEMPORAL_FRAME_COUNT = 1;  // TESTING: Disabled (was 3) - compute all pixels every frame

    // =========================================================================
    // BVH QUALITY OPTIMIZATION (Option 4C from GPU_PASS2_OPTIMIZATION.md)
    // Periodic BVH rebuild to maintain traversal quality as particles move
    //
    // WHY: BVH degrades over time - refit updates AABBs but tree topology is frozen
    //      Measured: ~85 nodes/ray (degraded) vs ~50 nodes/ray (optimal fresh BVH)
    // HOW: Rebuild periodically (time-based) or when quality degrades (quality-based)
    // IMPACT: Expected +10-18% FPS from improved BVH quality (70-100% fewer traversals)
    // COST: 234ms rebuild masked by async GPU_PREP (zero frame time impact)
    //
    // REFERENCE: BVH_REFIT_TUNING_DESIGN.md
    // =========================================================================

    // Master switch - disable entire optimization if needed
    constexpr bool USE_BVH_QUALITY_REBUILD = false;  // DISABLED: 269ms rebuild NOT masked by async prep → 286ms frame stall → min FPS drops to 3.5

    // Rebuild strategy selection
    enum class BVHRebuildStrategy {
        NEVER,           // Always refit (current behavior - quality degrades)
        TIME_BASED,      // Rebuild every N frames (Phase 1 - simple, zero overhead)
        QUALITY_BASED,   // Rebuild when quality metric exceeds threshold (Phase 2 - optimal)
        BOTH             // Use both time-based AND quality-based conditions
    };
    constexpr BVHRebuildStrategy BVH_REBUILD_STRATEGY = BVHRebuildStrategy::TIME_BASED;

    // Time-based rebuild parameters (Phase 1)
    constexpr size_t BVH_REBUILD_INTERVAL = 600;  // Frames (20 seconds @ 30 FPS)

    // QUALITY LEVER: minimum frames between shadow-BVH full rebuilds.
    //
    // A refit cannot place NEW triangles (no leaves exist for them), so a scene
    // that spawns geometry must rebuild. The trigger for that is a relative
    // one (>1% of the current triangle count), which a steady spawn rate
    // outruns while the scene is still small: measured on the falling-bodies
    // ramp, 180 new shadow triangles a frame against a 1% threshold meant a
    // FULL REBUILD EVERY FRAME between 839 and 2,039 bodies. 81 frames at up
    // to 97 ms each, 13% of the whole run, then it self-corrected once 1% of
    // the count exceeded the growth rate. That is the classic "runs fine, gets
    // choppy, then recovers" report.
    //
    // Rebuild cost is linear and honest (~0.6 us/triangle); a refit of 629,552
    // triangles is 1.66 ms against 97 ms to rebuild 160,000. The cost is not
    // the builder, it is rebuilding every frame.
    //
    // This is the knob a player would move. Higher = smoother, at the price of
    // newly spawned bodies casting no shadow for up to N-1 frames:
    //   1  every change rebuilds        highest fidelity, the old behaviour
    //   4  ~66 ms of shadowless spawn   balanced
    //   8  ~133 ms                      smooth
    //   16 ~266 ms                      performance
    // A large jump (see SHADOW_BVH_FORCE_REBUILD_FRACTION) always rebuilds
    // immediately regardless, so scene loads and teleports are never stale.
    // Override at runtime with LOGOSPHERE_BVH_REBUILD_FRAMES=<n>.
    constexpr size_t SHADOW_BVH_MIN_REBUILD_FRAMES = 1;  // default = old behaviour

    // Change big enough to bypass the interval above and rebuild now, as a
    // fraction of the live triangle count. Guards teleports and scene swaps,
    // where a deferred rebuild would leave shadows attached to nothing.
    constexpr float SHADOW_BVH_FORCE_REBUILD_FRACTION = 0.25f;

    // Quality-based rebuild parameters (Phase 2)
    constexpr float BVH_QUALITY_THRESHOLD = 1.5f;          // Rebuild if surface area > 1.5× initial
    constexpr size_t BVH_QUALITY_CHECK_INTERVAL = 60;     // Check quality every N frames (2 seconds @ 30 FPS)

    // =========================================================================
    // BVH REBUILD THRESHOLD (Tolerance for Triangle Count Changes)
    // Skip full rebuild for small particle changes, use refit instead
    //
    // WHY: Full BVH rebuild of 1M triangles takes 580ms, killing FPS during chunk load/unload
    //      Refit only updates AABBs (~10ms) without restructuring tree
    // HOW: Only trigger full rebuild if triangle count changes by > BVH_REBUILD_THRESHOLD %
    //      Small changes (chunk load/unload) use fast refit instead
    // IMPACT: Eliminates 580ms stalls during normal gameplay
    // TRADE-OFF: Slightly degraded BVH quality for small changes (acceptable)
    //
    // Set to 0.0f to always rebuild (old behavior, causes stalls)
    // Set to 0.05f (5%) to skip rebuild for typical chunk load/unload (recommended)
    // Set to 1.0f (100%) to never rebuild (refit only, may degrade over time)
    // =========================================================================
    constexpr bool USE_BVH_REBUILD_THRESHOLD = false;      // DISABLED: causes shadow flickering (BVH indices mismatch triangle buffer)
    constexpr float BVH_REBUILD_THRESHOLD = 0.05f;         // Only rebuild if triangle count changes >5%

    // =========================================================================
    // PROFILING CONTROL
    // Based on lessons from PERFORMANCE_RESEARCH.md:
    // - Never profile in shipping builds (zero overhead)
    // - Use statistical sampling (profile 1 in 60 frames)
    // - Count operations, don't time individual calls
    // =========================================================================
    constexpr bool ENABLE_PROFILING = true;  // ENABLED - diagnosing 11 FPS issue
    constexpr int PROFILE_SAMPLE_RATE = 60;   // Sample every N frames (10 for testing, 60 for production)

    // Verbose per-frame logs for debugging (ASYNC_PREP, CALLBACK_TIMING, PRESENT_STALL, GPU_RASTERIZER)
    // Set to false for clean output during normal operation
    constexpr bool ENABLE_VERBOSE_FRAME_LOGS = false;  // Disable chatty per-frame performance logs

    // Hierarchical profiling levels (following industry best practices)
    enum ProfileLevel {
        PROFILE_NONE = 0,      // Ship build - zero overhead
        PROFILE_FRAME = 1,     // Frame time only (minimal overhead)
        PROFILE_SYSTEMS = 2,   // Major systems (render, shadow, lighting)
        PROFILE_BATCHES = 3,   // Per-batch timing (statistical sampling)
        PROFILE_DETAILED = 4   // Full instrumentation (debug only)
    };
    constexpr ProfileLevel PROFILE_LEVEL = PROFILE_SYSTEMS;  // Current profiling depth

    // Detailed component profiling (can be enabled individually for diagnosis)
    // UI overlay refresh cap. present() runs on every main-loop iteration
    // so the HUD stays live when the GPU is slow (FPS-independent UI,
    // the UI overlay design notes), but each present fills a
    // fresh drawable — uncapped, a 200 Hz main loop would upload far more
    // than a 60 FPS game does. 120 Hz bounds it to display-refresh-class
    // traffic. Presents triggered by a NEW GPU frame are never capped.
    constexpr double UI_PRESENT_MAX_HZ = 120.0;

    constexpr double STALL_FRAME_THRESHOLD_MS = 50.0;  // [STALL-FRAME] attribution print threshold (lower to ~35 for tail hunts)
    constexpr double PHYSICS_SPIKE_THRESHOLD_MS = 12.0; // [PHYSICS-SPIKE] ~3x the 4.3ms live median; catches outliers that hide under the frame threshold
    constexpr bool PROFILE_TILE_BINNING = true;  // Log tile binning time every PROFILE_SAMPLE_RATE frames
    constexpr bool PROFILE_GPU_COMMAND_ENCODING = false;  // Profile GPU command buffer encoding - MEASURED: only 0.1-0.2ms, NOT the bottleneck!
    constexpr bool PROFILE_BVH_TRAVERSAL = true;  // Detailed BVH traversal statistics (nodes visited, cache patterns, coherency)

    // Batch sampling for statistical profiling (no clock in hot paths)
    constexpr int BATCH_SAMPLE_RATE = 100;  // Sample 1 in 100 batches (1% overhead)
    constexpr bool TRACK_MEMORY_ALLOCS = true;  // Track per-frame memory allocations
    constexpr bool TRACK_SIMD_COHERENCE = true;  // Measure ray packet coherence

    // =========================================================================
    // PER-PASS COMMAND BUFFERS: production structure, no flag (2026-07-23).
    // ENABLE_PER_PASS_GPU_TIMING removed — it never toggled timing; it
    // selected a frozen pre-deterministic renderer (per-light temporal
    // shadow kernel, full-screen software-BVH walks) measured 4x slower
    // with different pixels. That branch is deleted per C-116. Timing
    // handlers remain, sampled via GPU_PROFILE_SAMPLE_RATE (near-free).
    // RCA: the GPU optimization ledger item E.
    // =========================================================================

    // =========================================================================
    // GPU TIMESTAMP PROFILING (Metal Counter Sample Buffers)
    // Use hardware GPU timestamps to measure actual GPU execution time per pass
    //
    // WHY: Find actual bottleneck in deferred pipeline after temporal optimization
    //      50% thread reduction → only 14% FPS gain suggests Pass 2 not the bottleneck
    // HOW: MTLCounterSampleBuffer records GPU timestamps around each pass
    //      Statistical sampling every 60 frames (zero overhead 98.3% of time)
    // EXPECTED: Identify which pass dominates: G-buffer, Blit, Shadow, or Apply
    //
    // OVERHEAD: <0.1% (sampling 1 in 60 frames, reading timestamps async)
    // PRINCIPLE: Following PERFORMANCE_RESEARCH.md - statistical sampling
    // REFERENCE: Apple Metal Best Practices - GPU Performance Counters
    // =========================================================================
    constexpr bool ENABLE_GPU_TIMESTAMP_PROFILING = true;   // ENABLED for Metal RT comparison
    constexpr int GPU_PROFILE_SAMPLE_RATE = 60;            // Sample every N frames (60 = 1.7% overhead)

    // =========================================================================
    // TEMPORAL BLIT DISABLE (A/B Testing for Blit Cost Measurement)
    // Temporarily disable temporal pre-fill blit to measure its exact cost
    //
    // WHY: Suspect blit overhead might be eating performance gains from indirect dispatch
    //      Blit copies 6.7 MB/frame (temporal_buffer → shadow_results)
    // HOW: Conditional compilation - can toggle without modifying core logic
    // WARNING: When enabled, rendering will be DARK (non-traced pixels = zero)
    //
    // USAGE: Set to true, rebuild, measure GPU times, compare to baseline
    // EXPECTED: If blit is the problem, Pass 2 time will drop significantly
    // =========================================================================
    constexpr bool DISABLE_TEMPORAL_BLIT = false;  // A/B TEST COMPLETE: Blit cost measured (0.01ms), re-enabled for correct rendering
}

#endif // OPTIMIZATION_FLAGS_H