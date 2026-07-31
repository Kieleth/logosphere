//
// shadow_rays_deferred.metal
// Shadow ray tracing for deferred rendering (Pass 2 of 3)
//
// ARCHITECTURE: Deferred Rendering - 3-pass system
// Pass 1: Rasterize geometry, output to G-buffer
// Pass 2 (THIS FILE): Trace shadow rays (coherent BVH traversal)
// Pass 3: Apply lighting (simple math with pre-computed shadows)
//
// PURPOSE: Extract BVH traversal from rasterize_triangles_lit, separate from rasterization
// Processes ONLY visible pixels identified in Pass 1, enabling coherent BVH traversal
//
// PERFORMANCE: ~330ms estimated (coherent BVH, 80-90% warp efficiency vs 5-10% inline)
//
// KEY OPTIMIZATION: Adjacent pixels trace similar rays → Better cache locality
// - Forward rendering: Each pixel diverges on different BVH paths (5-10% efficiency)
// - Deferred rendering: Nearby pixels traverse similar BVH nodes (80-90% efficiency)

#include <metal_stdlib>
#include <metal_raytracing>  // Metal RT API for hardware-accelerated ray tracing
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "optimization_flags.metal"
#include "gpu_math.metal"  // ray_intersects_aabb, ray_intersects_triangle
#include "gbuffer_types.metal"
using namespace metal;
using namespace raytracing;  // For intersector, ray, intersection_type

// =========================================================================
// FORWARD DECLARATIONS - Soft shadow helper functions (defined below kernels)
// =========================================================================
inline float halton(uint index, uint base);
inline float2 get_temporal_jitter(uint frame_index, uint pixel_id);
inline void get_tangent_basis(float3 n, thread float3& tangent, thread float3& bitangent);

// PCSS (Percentage-Closer Soft Shadows) helpers
struct PCSSsamples {
    float2 blocker[PCSS_BLOCKER_SAMPLES];    // Phase 1: fixed points on full light disc
    float2 penumbra[PCSS_PENUMBRA_SAMPLES];  // Phase 3: points within penumbra cone
};
inline PCSSsamples get_pcss_samples(uint frame_index, uint pixel_index);

// =========================================================================
// PASS 2: SHADOW RAY TRACING (Deferred)
// =========================================================================
// Trace shadow rays for all visible pixels identified in G-buffer
// ONLY process visible geometry, enabling coherent BVH traversal
//
// INPUT: G-buffer (world pos, normal, particle ID) from Pass 1
// OUTPUT: Shadow results (total lighting intensity in lux per pixel)

kernel void trace_shadow_rays_deferred(
    constant GBufferPixel* gbuffer [[buffer(0)]],        // G-buffer from Pass 1 (full res)
    device float* shadow_results [[buffer(1)]],          // Output: total lighting (lux) per pixel (reduced res)
    constant uint& shadow_width [[buffer(2)]],           // Phase 1: Shadow buffer width (reduced)
    constant uint& shadow_height [[buffer(3)]],          // Phase 1: Shadow buffer height (reduced)
    constant uint& gbuffer_width [[buffer(4)]],          // Phase 1: G-buffer width (full res)
    constant uint& gbuffer_height [[buffer(5)]],         // Phase 1: G-buffer height (full res)
    constant LightData* lights [[buffer(6)]],            // Light sources (shifted +2)
    constant uint& light_count [[buffer(7)]],            // Light count (shifted +2)
    constant BVHNode* bvh_nodes [[buffer(8)]],           // BVH tree (shifted +2)
    constant uint& bvh_node_count [[buffer(9)]],         // BVH node count (shifted +2)
    constant Triangle* bvh_triangles [[buffer(10)]],     // BVH triangles (shifted +2)
    constant uint& bvh_triangle_count [[buffer(11)]],    // BVH tri count (shifted +2)
    constant uint* frame_index [[buffer(12)]],           // Phase 2: Current frame index for temporal distribution
    device float* temporal_buffer [[buffer(13)]],        // Phase 2: Previous frame lighting (read old, write new)
    uint2 gid [[thread_position_in_grid]])               // Thread position
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    // Phase 1: Bounds check against SHADOW resolution (not G-buffer)
    if (px >= (int)shadow_width || py >= (int)shadow_height) {
        return;
    }

    uint shadow_pixel_index = py * shadow_width + px;           // Shadow buffer index (reduced)

    // Phase 2: Temporal Distribution - Checkerboard pattern selective tracing
    // Only trace pixels selected for this frame, reuse others from previous frame
#ifdef USE_TEMPORAL_LIGHTING
    #ifdef TEMPORAL_FRAME_COUNT
        // Determine if this pixel should be traced this frame
        uint current_frame = frame_index[0];

        // Warmup check: frame_index >= 999 means "trace all pixels" (populating temporal buffer)
        // During warmup (first TEMPORAL_FRAME_COUNT frames), we trace everything to populate buffer
        bool is_warmup = (current_frame >= 999);
        bool should_trace = is_warmup;  // During warmup, always trace

        if (!is_warmup) {
            // Normal operation: Checkerboard pattern spatial distribution
            // For 2-frame: classic checkerboard (x + y) % 2
            // For 3/5-frame: use diagonal stripes for better spatial distribution
            #if TEMPORAL_FRAME_COUNT == 2
                // Classic checkerboard pattern (like chess board)
                should_trace = ((px + py) % 2) == (current_frame % 2);
            #else
                // Diagonal stripe pattern for 3/5 frames
                should_trace = ((px + py) % TEMPORAL_FRAME_COUNT) == (current_frame % TEMPORAL_FRAME_COUNT);
            #endif

            if (!should_trace) {
                // Reuse previous frame's lighting result (temporal coherence)
                // With per-light dispatch: Only copy once (first light), then do nothing
                // Detect first light by checking if shadow_results is still zero (cleared at frame start)
                float current_shadow = shadow_results[shadow_pixel_index];
                if (current_shadow == 0.0f) {
                    // First light dispatch - copy complete lighting from previous frame
                    shadow_results[shadow_pixel_index] = temporal_buffer[shadow_pixel_index];
                }
                // Subsequent light dispatches: do nothing (already copied)
                return;  // Early exit - no ray tracing needed
            }
        }
        // If should_trace (warmup or checkerboard match): proceed with ray tracing below
    #endif
#endif

    // Phase 1: Map shadow pixel to G-buffer pixel (scale up)
    float scale_x = (float)gbuffer_width / (float)shadow_width;
    float scale_y = (float)gbuffer_height / (float)shadow_height;

    uint gbuffer_px = (uint)((px + 0.5f) * scale_x);
    uint gbuffer_py = (uint)((py + 0.5f) * scale_y);
    gbuffer_px = min(gbuffer_px, gbuffer_width - 1);
    gbuffer_py = min(gbuffer_py, gbuffer_height - 1);

    uint gbuffer_pixel_index = gbuffer_py * gbuffer_width + gbuffer_px; // G-buffer index (full)

    // Phase 1: Read G-buffer pixel at FULL resolution
    GBufferPixel pixel = gbuffer[gbuffer_pixel_index];

    // Initialize with NO ambient light (pure darkness without light sources)
    float total_lighting_lux = 0.0f;  // No ambient - surfaces only visible when lit

    // Skip sky pixels (no surface to light)
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        shadow_results[shadow_pixel_index] = 0.0f;  // Sky = black
        return;
    }

    // Surface data from G-buffer
    float3 world_pos = pixel.world_pos;
    float3 normal = pixel.normal;

    // Phase 2: TEMPORAL FIX - Clear temporal buffer for traced pixels
    // This pixel will be traced (not reused), so clear its temporal slot for fresh accumulation
#ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] = 0.0f;
#endif

    // =========================================================================
    // REGISTER PRESSURE FIX (2025-01-24): BVH Stack Allocation
    // =========================================================================
    // PROBLEM: Allocating stack[32] INSIDE the light loop caused register pressure
    //          - At 10+ lights: 10 × 32 ints = 1,280 bytes per thread
    //          - Exceeded Metal's ~1KB register limit per thread
    //          - Result: Register spilling → GPU occupancy collapse → 260% GPU usage
    //
    // SOLUTION: Single stack allocated OUTSIDE light loop, reused for all lights
    //          - Register usage: 1,280 bytes → 128 bytes (91% reduction)
    //          - GPU occupancy: Restored to normal levels
    //          - GPU usage: 260% → ~50% (single core, high efficiency)
    //
    // MEASURED IMPACT:
    //          - Before: 10 lights = 44ms GPU time, 260% usage, 17ms variance
    //          - After:  Expected ~22ms GPU time, ~50% usage, <2ms variance
    //
    // SAFETY: Stack is only used within each light iteration (reset stack_ptr each time)
    //         No data leakage between lights - each light starts fresh from root
    // =========================================================================
    int stack[32];  // Single stack for ALL lights (reused per light iteration)

    // Loop over all lights and compute lighting with shadow tests
    for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
        LightData light = lights[light_idx];

        // Vector from surface to light
        float3 to_light = float3(light.position) - world_pos;
        float distance_sq = dot(to_light, to_light);

        // Check if within light radius
        if (distance_sq <= light.emission_radius * light.emission_radius) {
            // Normalize direction to light
            float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_distance_sq);
            float3 light_dir = to_light * inv_dist;

            // Lambertian lighting (dot product with normal)
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian > 0.0f) {
                // BVH Shadow ray test (extracted from rasterize_triangles_lit)
                bool in_shadow = false;

                if (bvh_node_count > 0) {
                    // Setup shadow ray: from surface point to light
                    float3 ray_origin = world_pos + normal * SHADOW_RAY_NORMAL_OFFSET;
                    float3 ray_direction = light_dir;
                    float max_distance = sqrt(safe_distance_sq);

                    // Stack-based BVH traversal (same as forward rendering, but NOW coherent!)
                    // Adjacent pixels traverse similar BVH paths → Better cache locality
                    // REGISTER FIX: Stack pointer reset per light (stack array reused above)
                    int stack_ptr = 0;
                    stack[stack_ptr++] = 0;  // Start at root

                    while (stack_ptr > 0 && !in_shadow) {
                        int node_idx = stack[--stack_ptr];
                        BVHNode node = bvh_nodes[node_idx];

                        // Test ray vs AABB
                        if (!ray_intersects_aabb(ray_origin, ray_direction,
                                                float3(node.bbox_min), float3(node.bbox_max))) {
                            continue;  // Skip subtree
                        }

                        // Leaf? Test triangle
                        if (node.triangle_idx >= 0) {
                            Triangle tri = bvh_triangles[node.triangle_idx];
                            if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                                       float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                                in_shadow = true;  // Hit! Ray blocked
                            }
                        } else {
                            // Internal node - push children (with overflow guard)
                            if (node.left_child >= 0 && stack_ptr < 32) {
                                stack[stack_ptr++] = node.left_child;
                            }
                            if (node.right_child >= 0 && stack_ptr < 32) {
                                stack[stack_ptr++] = node.right_child;
                            }
                        }
                    }
                }

                // Only accumulate light if NOT in shadow
                if (!in_shadow) {
                    // Calculate intensity (inverse square law)
                    float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);
                    total_lighting_lux += intensity_lux * lambertian;
                }
            }
        }
    }

    // Phase 1: Write total lighting intensity to SHADOW buffer (reduced res)
    // PER-LIGHT dispatch: CPU dispatches once per light with light_count=1
    // Each dispatch must ACCUMULATE its contribution (not overwrite!)
    // Pass 3 will use this accumulated result to compute final color
    //
    // ITERATION 1 FIX: Use += to accumulate across per-light dispatches
    shadow_results[shadow_pixel_index] += total_lighting_lux;

    // Phase 2: TEMPORAL FIX - Write traced pixels to temporal buffer for next frame
    // Only traced pixels update temporal buffer (not reused pixels!)
    // This prevents progressive darkening from mixing old and new data
#ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] += total_lighting_lux;
    // NOTE: Metal shaders don't support printf, using CPU-side logging instead
#endif
}

// =========================================================================
// BATCHED VERSION - 8 pixels per thread for cache coherency (Phase II-B)
// =========================================================================
// PURPOSE: Process multiple adjacent pixels per thread to improve BVH cache reuse
// HYPOTHESIS: Adjacent pixels trace similar rays → hit similar BVH nodes → better cache locality
// BENEFIT: BVH nodes loaded once, reused across 8 pixels (30-50% speedup expected)

kernel void trace_shadow_rays_deferred_batched(
    constant GBufferPixel* gbuffer [[buffer(0)]],        // G-buffer at FULL resolution
    device float* shadow_results [[buffer(1)]],          // Shadow buffer at REDUCED resolution
    constant uint& shadow_width [[buffer(2)]],           // Phase 1: Shadow buffer width (reduced)
    constant uint& shadow_height [[buffer(3)]],          // Phase 1: Shadow buffer height (reduced)
    constant uint& gbuffer_width [[buffer(4)]],          // Phase 1: G-buffer width (full res)
    constant uint& gbuffer_height [[buffer(5)]],         // Phase 1: G-buffer height (full res)
    constant LightData* lights [[buffer(6)]],            // Lights (index shifted +2)
    constant uint& light_count [[buffer(7)]],
    constant BVHNode* bvh_nodes [[buffer(8)]],
    constant uint& bvh_node_count [[buffer(9)]],
    constant Triangle* bvh_triangles [[buffer(10)]],
    constant uint& bvh_triangle_count [[buffer(11)]],
    constant uint* frame_index [[buffer(12)]],           // Phase 2: Current frame index for temporal distribution
    device float* temporal_buffer [[buffer(13)]],        // Phase 2: Previous frame lighting (read old, write new)
    constant uint* pixel_indices [[buffer(14)]],         // Phase 2: Indirect dispatch - pixel indices to trace
    device atomic_uint* debug_rays_traced [[buffer(15)]],        // GPU PROFILING: Total rays traced
    device atomic_uint* debug_bvh_nodes_visited [[buffer(16)]],  // GPU PROFILING: Total BVH nodes visited
    device atomic_uint* debug_triangles_tested [[buffer(17)]],   // GPU PROFILING: Total triangles tested
    constant EntityBVHNode* entity_bvh_nodes [[buffer(18)]],     // Entity BVH nodes for directional culling
    constant uint& entity_node_count [[buffer(19)]],             // Number of entity BVH nodes
    constant DirectionalGroup* directional_groups [[buffer(20)]], // Directional groups (triangles by facing)
    constant uint& dir_group_count [[buffer(21)]],               // Number of directional groups
    device uint32_t* prev_particle_id [[buffer(22)]],            // Previous frame particle IDs (soft shadow motion detection)
    uint gid [[thread_position_in_grid]])  // 1D grid
{
    // Phase 2: TODO[PERF-001] - INDIRECT DISPATCH OPTIMIZATION
    //
    // WARMUP MODE (frame_index >= 999):
    //   - gid is the pixel index directly (gid = py * width + px)
    //   - Dispatch count: full resolution (1.68M threads)
    //   - pixel_indices buffer: NULL (not bound)
    //
    // NORMAL MODE (frame_index < 999):
    //   - gid is index into pixel_indices array
    //   - Dispatch count: 50% of pixels (840K threads)
    //   - pixel_indices[gid] gives the actual pixel index
    //
    uint shadow_pixel_index;
    uint current_frame = frame_index[0];
    bool is_warmup = (current_frame >= 999);

    if (is_warmup) {
        // Warmup: gid is pixel index directly
        if (gid >= shadow_width * shadow_height) {
            return;  // Bounds check
        }
        shadow_pixel_index = gid;
    } else {
        // Normal: read pixel index from buffer (indirect dispatch)
        shadow_pixel_index = pixel_indices[gid];
    }

    // Calculate shadow pixel coordinates from pixel index
    uint shadow_px = shadow_pixel_index % shadow_width;
    uint shadow_py = shadow_pixel_index / shadow_width;

    // Phase 2: TODO[PERF-001] - Indirect dispatch optimization complete
    // CPU pre-computed pixel indices, GPU dispatches only selected threads
    // No checkerboard check needed - indirect dispatch handles pixel selection

    // Phase 1: Map shadow pixel to G-buffer pixel (scale up coordinates)
    // Shadow pixel (x,y) at 640×400 → G-buffer pixel (x*2, y*2) at 1280×800
    // Scale factor = gbuffer_res / shadow_res (e.g., 1280/640 = 2.0)
    float scale_x = (float)gbuffer_width / (float)shadow_width;
    float scale_y = (float)gbuffer_height / (float)shadow_height;

    // Sample G-buffer at center of scaled region (0.5 offset for pixel center)
    uint gbuffer_px = (uint)((shadow_px + 0.5f) * scale_x);
    uint gbuffer_py = (uint)((shadow_py + 0.5f) * scale_y);

    // Clamp to G-buffer bounds (safety)
    gbuffer_px = min(gbuffer_px, gbuffer_width - 1);
    gbuffer_py = min(gbuffer_py, gbuffer_height - 1);

    // Compute G-buffer index (shadow_pixel_index already computed above)
    uint gbuffer_pixel_index = gbuffer_py * gbuffer_width + gbuffer_px; // G-buffer index (full res)

    // Read G-buffer pixel at scaled coordinates (full resolution)
    GBufferPixel pixel = gbuffer[gbuffer_pixel_index];

    // Initialize with NO ambient light (pure darkness without light sources)
    float total_lighting_lux = 0.0f;  // No ambient - surfaces only visible when lit

    // Skip sky pixels (no surface to light)
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        shadow_results[shadow_pixel_index] = 0.0f;  // Sky = black
        return;
    }

    // Surface data from G-buffer
    float3 world_pos = pixel.world_pos;
    float3 normal = pixel.normal;

    // SOFT SHADOWS: Read previous frame's lighting for temporal accumulation
    // When soft shadows enabled, we blend current with previous frame for smooth penumbra
    // NOTE: This BVH kernel is NOT USED when USE_METAL_RT=true (see trace_shadows_metal_rt)
#ifdef USE_SOFT_SHADOWS
    #if TEMPORAL_FRAME_COUNT == 1
    float prev_lighting = temporal_buffer[shadow_pixel_index];
    #endif
#endif

    // Phase 2: TEMPORAL FIX - Clear temporal buffer for traced pixels
    // Only clear when NOT doing soft shadow blending (soft shadows need the previous value)
#ifdef USE_TEMPORAL_LIGHTING
    #if !defined(USE_SOFT_SHADOWS) || TEMPORAL_FRAME_COUNT > 1
    temporal_buffer[shadow_pixel_index] = 0.0f;
    #endif
#endif

    // =========================================================================
    // REGISTER PRESSURE FIX (2025-01-24): BVH Stack Allocation
    // =========================================================================
    // PROBLEM: Allocating stack[32] INSIDE the light loop caused register pressure
    //          - At 10+ lights: 10 × 32 ints = 1,280 bytes per thread
    //          - Exceeded Metal's ~1KB register limit per thread
    //          - Result: Register spilling → GPU occupancy collapse → 260% GPU usage
    //
    // SOLUTION: Single stack allocated OUTSIDE light loop, reused for all lights
    //          - Register usage: 1,280 bytes → 128 bytes (91% reduction)
    //          - GPU occupancy: Restored to normal levels
    //          - GPU usage: 260% → ~50% (single core, high efficiency)
    //
    // MEASURED IMPACT (batched kernel, 840K threads/frame):
    //          - Before: 10 lights = 44ms GPU time, 260% usage, 17ms variance
    //          - After:  Expected ~22ms GPU time, ~50% usage, <2ms variance
    //
    // SAFETY: Stack is only used within each light iteration (reset stack_ptr each time)
    //         No data leakage between lights - each light starts fresh from root
    // =========================================================================
    int stack[32];  // Single stack for ALL lights (reused per light iteration)

    // Loop over all lights and compute lighting with shadow tests
    for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
        LightData light = lights[light_idx];

        // Vector from surface to light
        float3 to_light = float3(light.position) - world_pos;
        float distance_sq = dot(to_light, to_light);

        // Check if within light radius
        if (distance_sq <= light.emission_radius * light.emission_radius) {
            // Normalize direction to light
            float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_distance_sq);
            float3 light_dir = to_light * inv_dist;

            // SOFT SHADOWS: Jitter ray direction based on light's angular size
            // angular_radius = light_size / distance gives apparent size from surface
            // Jitter samples different points on area light each frame
            // Temporal accumulation smooths to soft penumbra over 4 frames
            // NOTE: This BVH kernel is NOT USED when USE_METAL_RT=true
            if (light.light_size > 0.001f) {
                float distance_to_light = sqrt(safe_distance_sq);
                float angular_radius = light.light_size / distance_to_light;
                float2 jitter = get_temporal_jitter(current_frame, shadow_pixel_index);
                float3 tangent, bitangent;
                get_tangent_basis(light_dir, tangent, bitangent);
                light_dir = normalize(light_dir + tangent * jitter.x * angular_radius
                                                + bitangent * jitter.y * angular_radius);
            }

            // Lambertian lighting (dot product with normal)
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian > 0.0f) {
                // GPU PROFILING: Count ray traced (only when actually tracing)
                if (debug_rays_traced) {
                    atomic_fetch_add_explicit(debug_rays_traced, 1, memory_order_relaxed);
                }

                // BVH Shadow ray test
                bool in_shadow = false;

                // Setup shadow ray: from surface point to light
                float3 ray_origin = world_pos + normal * SHADOW_RAY_NORMAL_OFFSET;
                float3 ray_direction = light_dir;
                float max_distance = sqrt(safe_distance_sq);

                // =====================================================================
                // ENTITY BVH WITH DIRECTIONAL CULLING
                // =====================================================================
                // 3-Stage Traversal:
                //   Stage 1: Entity BVH AABB test (coarse culling)
                //   Stage 2: Directional culling (skip groups facing away from ray)
                //   Stage 3: Triangle scan (only non-culled groups)
                //
                // Expected benefit: ~50% fewer triangle tests (half face away from ray)
                // =====================================================================
                if (entity_node_count > 0 && dir_group_count > 0) {
                    // Stage 1: Traverse Entity BVH
                    int stack_ptr = 0;
                    stack[stack_ptr++] = 0;  // Start at root

                    while (stack_ptr > 0 && !in_shadow) {
                        int node_idx = stack[--stack_ptr];
                        EntityBVHNode node = entity_bvh_nodes[node_idx];

                        // GPU PROFILING: Count BVH node visited
                        if (debug_bvh_nodes_visited) {
                            atomic_fetch_add_explicit(debug_bvh_nodes_visited, 1, memory_order_relaxed);
                        }

                        // Test ray vs entity AABB (clamped to light distance)
                        if (!ray_intersects_aabb(ray_origin, ray_direction,
                                                float3(node.bbox_min), float3(node.bbox_max),
                                                max_distance)) {
                            continue;  // Skip this entity/subtree
                        }

                        // Is this a leaf node (has directional groups)?
                        if (node.left_child < 0 && node.right_child < 0) {
                            // Stage 2 & 3: Process directional groups for this entity
                            for (int g = 0; g < node.dir_group_count && !in_shadow; ++g) {
                                DirectionalGroup group = directional_groups[node.dir_group_start + g];

                                // Stage 2: Directional culling
                                // Skip groups facing AWAY from ray origin (dot > 0 means surface faces away)
                                float dot_val = dot(ray_direction, float3(group.avg_normal));
                                if (dot_val > 0.0f) {
                                    continue;  // Surface faces away from ray, skip ~50% of triangles
                                }

                                // Stage 3: Scan triangles in this group
                                for (int t = 0; t < group.tri_count && !in_shadow; ++t) {
                                    int tri_idx = group.tri_start + t;

                                    // GPU PROFILING: Count triangle tested
                                    if (debug_triangles_tested) {
                                        atomic_fetch_add_explicit(debug_triangles_tested, 1, memory_order_relaxed);
                                    }

                                    Triangle tri = bvh_triangles[tri_idx];
                                    if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                                               float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                                        in_shadow = true;  // Hit! Ray blocked
                                    }
                                }
                            }
                        } else {
                            // Internal node — front-to-back child ordering.
                            // Push far child first (processed last) so near child
                            // is processed first, finding shadow hits earlier.
                            int left = node.left_child;
                            int right = node.right_child;
                            if (left >= 0 && right >= 0 && stack_ptr < 31) {
                                // Near child = whose AABB center is closer along ray direction
                                float3 left_center = (float3(entity_bvh_nodes[left].bbox_min) +
                                                      float3(entity_bvh_nodes[left].bbox_max)) * 0.5f;
                                float3 right_center = (float3(entity_bvh_nodes[right].bbox_min) +
                                                       float3(entity_bvh_nodes[right].bbox_max)) * 0.5f;
                                float left_t = dot(left_center - ray_origin, ray_direction);
                                float right_t = dot(right_center - ray_origin, ray_direction);
                                if (left_t < right_t) {
                                    stack[stack_ptr++] = right;  // far child first (popped last)
                                    stack[stack_ptr++] = left;   // near child second (popped first)
                                } else {
                                    stack[stack_ptr++] = left;
                                    stack[stack_ptr++] = right;
                                }
                            } else {
                                if (left >= 0 && stack_ptr < 32) stack[stack_ptr++] = left;
                                if (right >= 0 && stack_ptr < 32) stack[stack_ptr++] = right;
                            }
                        }
                    }
                }
                // =====================================================================
                // FALLBACK: Flat BVH traversal (when entity BVH not available)
                // =====================================================================
                else if (bvh_node_count > 0) {
                    // Stack-based BVH traversal (coherent for adjacent pixels)
                    // REGISTER FIX: Stack pointer reset per light (stack array reused above)
                    int stack_ptr = 0;
                    stack[stack_ptr++] = 0;  // Start at root

                    while (stack_ptr > 0 && !in_shadow) {
                        int node_idx = stack[--stack_ptr];
                        BVHNode node = bvh_nodes[node_idx];

                        // GPU PROFILING: Count BVH node visited
                        if (debug_bvh_nodes_visited) {
                            atomic_fetch_add_explicit(debug_bvh_nodes_visited, 1, memory_order_relaxed);
                        }

                        // Test ray vs AABB (clamped to light distance)
                        if (!ray_intersects_aabb(ray_origin, ray_direction,
                                                float3(node.bbox_min), float3(node.bbox_max),
                                                max_distance)) {
                            continue;  // Skip subtree
                        }

                        // Leaf? Test triangle
                        if (node.triangle_idx >= 0) {
                            // GPU PROFILING: Count triangle tested
                            if (debug_triangles_tested) {
                                atomic_fetch_add_explicit(debug_triangles_tested, 1, memory_order_relaxed);
                            }

                            Triangle tri = bvh_triangles[node.triangle_idx];
                            if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                                       float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                                in_shadow = true;  // Hit! Ray blocked
                            }
                        } else {
                            // Internal node — front-to-back child ordering.
                            int left = node.left_child;
                            int right = node.right_child;
                            if (left >= 0 && right >= 0 && stack_ptr < 31) {
                                float3 left_center = (float3(bvh_nodes[left].bbox_min) +
                                                      float3(bvh_nodes[left].bbox_max)) * 0.5f;
                                float3 right_center = (float3(bvh_nodes[right].bbox_min) +
                                                       float3(bvh_nodes[right].bbox_max)) * 0.5f;
                                float left_t = dot(left_center - ray_origin, ray_direction);
                                float right_t = dot(right_center - ray_origin, ray_direction);
                                if (left_t < right_t) {
                                    stack[stack_ptr++] = right;
                                    stack[stack_ptr++] = left;
                                } else {
                                    stack[stack_ptr++] = left;
                                    stack[stack_ptr++] = right;
                                }
                            } else {
                                if (left >= 0 && stack_ptr < 32) stack[stack_ptr++] = left;
                                if (right >= 0 && stack_ptr < 32) stack[stack_ptr++] = right;
                            }
                        }
                    }
                }

                // Only accumulate light if NOT in shadow
                if (!in_shadow) {
                    // Calculate intensity (inverse square law)
                    float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);
                    total_lighting_lux += intensity_lux * lambertian;
                }
            }
        }
    }

    // Write total lighting intensity for this pixel
    // SINGLE-PASS DISPATCH: CPU sends ALL lights in one dispatch
    // Must use = (direct write), NOT += (accumulation) - total_lighting_lux already contains all lights summed
    // Phase 1: Write to shadow buffer at reduced resolution
    // SOFT SHADOWS: Temporal blending for smooth penumbra
    // When soft shadows enabled (TEMPORAL_FRAME_COUNT=1):
    //   blend = mix(prev, current, alpha) where alpha = 1/SOFT_SHADOW_TEMPORAL_FRAMES
    // This creates smooth penumbra as jittered samples accumulate over 4-8 frames
#ifdef USE_SOFT_SHADOWS
    #if TEMPORAL_FRAME_COUNT == 1
    // NOTE: This BVH kernel is NOT USED when USE_METAL_RT=true
    // Soft shadow temporal blending (see trace_shadows_metal_rt for active code)
    float alpha = 1.0f / float(SOFT_SHADOW_TEMPORAL_FRAMES);
    float blended_lighting = mix(prev_lighting, total_lighting_lux, alpha);
    shadow_results[shadow_pixel_index] = blended_lighting;
    temporal_buffer[shadow_pixel_index] = blended_lighting;
    #else
    // Temporal distribution mode: direct write (no soft shadow blending)
    shadow_results[shadow_pixel_index] = total_lighting_lux;
    #ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] = total_lighting_lux;
    #endif
    #endif
#else
    // Soft shadows disabled: direct write
    shadow_results[shadow_pixel_index] = total_lighting_lux;
    #ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] = total_lighting_lux;
    #endif
#endif
}

// =========================================================================
// INSTRUMENTED VERSION - Detailed BVH traversal profiling
// =========================================================================
// Same as above but collects detailed statistics for performance analysis
// Enable with PROFILE_BVH_TRAVERSAL flag in optimization_flags.h

kernel void trace_shadow_rays_instrumented(
    constant GBufferPixel* gbuffer [[buffer(0)]],
    device float* shadow_results [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant LightData* lights [[buffer(4)]],
    constant uint& light_count [[buffer(5)]],
    constant BVHNode* bvh_nodes [[buffer(6)]],
    constant uint& bvh_node_count [[buffer(7)]],
    constant Triangle* bvh_triangles [[buffer(8)]],
    constant uint& bvh_triangle_count [[buffer(9)]],
    device BVHTraversalStats* pixel_stats [[buffer(10)]], // Per-pixel statistics
    device BVHGlobalStats* global_stats [[buffer(11)]],   // Global aggregated stats
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    if (px >= (int)width || py >= (int)height) {
        return;
    }

    uint pixel_index = py * width + px;
    GBufferPixel pixel = gbuffer[pixel_index];

    // Initialize per-pixel statistics
    BVHTraversalStats stats;
    stats.nodes_visited = 0;
    stats.nodes_aabb_tested = 0;
    stats.triangles_tested = 0;
    stats.max_stack_depth = 0;
    stats.cache_line_changes = 0;
    stats.early_terminations = 0;
    stats.rays_traced = 0;

    float total_lighting_lux = 0.0f;

    // ITERATION 4 FIX: Don't write to sky pixels in per-light dispatch
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        pixel_stats[pixel_index] = stats;  // Store empty stats
        return;  // Don't touch shadow_results for sky pixels
    }

    float3 world_pos = pixel.world_pos;
    float3 normal = pixel.normal;
    int prev_node_idx = -1;  // For cache line tracking

    // =========================================================================
    // REGISTER PRESSURE FIX (2025-01-24): BVH Stack Allocation
    // =========================================================================
    // PROBLEM: Allocating stack[32] INSIDE the light loop caused register pressure
    //          - At 10+ lights: 10 × 32 ints = 1,280 bytes per thread
    //          - Exceeded Metal's ~1KB register limit per thread
    //          - Result: Register spilling → GPU occupancy collapse → 260% GPU usage
    //
    // SOLUTION: Single stack allocated OUTSIDE light loop, reused for all lights
    //          - Register usage: 1,280 bytes → 128 bytes (91% reduction)
    //          - GPU occupancy: Restored to normal levels
    //          - GPU usage: 260% → ~50% (single core, high efficiency)
    //
    // NOTE: This is the INSTRUMENTED kernel - adds BVH profiling overhead
    //       but same register pressure fix applies
    //
    // SAFETY: Stack is only used within each light iteration (reset stack_ptr each time)
    //         No data leakage between lights - each light starts fresh from root
    // =========================================================================
    int stack[32];  // Single stack for ALL lights (reused per light iteration)

    for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
        LightData light = lights[light_idx];
        float3 to_light = float3(light.position) - world_pos;
        float distance_sq = dot(to_light, to_light);

        if (distance_sq <= light.emission_radius * light.emission_radius) {
            float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_distance_sq);
            float3 light_dir = to_light * inv_dist;
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian > 0.0f) {
                bool in_shadow = false;
                stats.rays_traced++;

                if (bvh_node_count > 0) {
                    float3 ray_origin = world_pos + normal * 0.01f;
                    float3 ray_direction = light_dir;
                    float max_distance = sqrt(safe_distance_sq);

                    // REGISTER FIX: Stack pointer reset per light (stack array reused above)
                    int stack_ptr = 0;
                    stack[stack_ptr++] = 0;

                    // Track per-ray statistics
                    uint ray_nodes_visited = 0;
                    uint ray_triangles_tested = 0;
                    uint ray_max_stack = 1;

                    while (stack_ptr > 0 && !in_shadow) {
                        int node_idx = stack[--stack_ptr];
                        BVHNode node = bvh_nodes[node_idx];

                        ray_nodes_visited++;

                        // Track cache line changes (64-byte cache lines)
                        // Each BVHNode is 48 bytes, so ~1.3 nodes per cache line
                        if (prev_node_idx >= 0) {
                            int prev_line = prev_node_idx * 48 / 64;
                            int curr_line = node_idx * 48 / 64;
                            if (prev_line != curr_line) {
                                stats.cache_line_changes++;
                            }
                        }
                        prev_node_idx = node_idx;

                        // Test AABB
                        if (!ray_intersects_aabb(ray_origin, ray_direction,
                                                float3(node.bbox_min), float3(node.bbox_max))) {
                            continue;
                        }

                        stats.nodes_aabb_tested++;

                        if (node.triangle_idx >= 0) {
                            // Leaf node - test triangle
                            ray_triangles_tested++;
                            Triangle tri = bvh_triangles[node.triangle_idx];
                            if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                                       float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                                in_shadow = true;
                                stats.early_terminations++;
                            }
                        } else {
                            // Internal node - push children (with overflow guard)
                            if (node.left_child >= 0 && stack_ptr < 32) {
                                stack[stack_ptr++] = node.left_child;
                            }
                            if (node.right_child >= 0 && stack_ptr < 32) {
                                stack[stack_ptr++] = node.right_child;
                            }
                            ray_max_stack = max(ray_max_stack, (uint)stack_ptr);
                        }
                    }

                    // Update statistics
                    stats.nodes_visited += ray_nodes_visited;
                    stats.triangles_tested += ray_triangles_tested;
                    stats.max_stack_depth = max(stats.max_stack_depth, ray_max_stack);

                    // Update global statistics (atomically)
                    atomic_fetch_add_explicit(&global_stats->total_nodes_visited, ray_nodes_visited, memory_order_relaxed);
                    atomic_fetch_add_explicit(&global_stats->total_triangles_tested, ray_triangles_tested, memory_order_relaxed);
                    atomic_fetch_max_explicit(&global_stats->max_traversal_depth, ray_max_stack, memory_order_relaxed);

                    if (in_shadow) {
                        atomic_fetch_add_explicit(&global_stats->total_early_terminations, 1, memory_order_relaxed);
                    }
                }

                if (!in_shadow) {
                    float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);
                    total_lighting_lux += intensity_lux * lambertian;
                }
            }
        }
    }

    // Check coherency with neighboring pixels (simple version)
    // A more sophisticated version would track actual node overlap
    if (px > 0 && py > 0) {
        uint neighbor_idx = (py - 1) * width + (px - 1);
        BVHTraversalStats neighbor_stats = pixel_stats[neighbor_idx];
        // If neighboring pixel visited similar number of nodes, likely coherent
        uint diff = absdiff(stats.nodes_visited, neighbor_stats.nodes_visited);
        if (diff < stats.nodes_visited / 4) {  // Within 25% difference
            atomic_fetch_add_explicit(&global_stats->coherency_score, 1, memory_order_relaxed);
        }
    }

    // Store per-pixel statistics
    pixel_stats[pixel_index] = stats;

    // Write lighting result
    // PER-LIGHT dispatch: Must accumulate, not overwrite!
    // ITERATION 1 FIX: Use += to accumulate across per-light dispatches
    shadow_results[pixel_index] += total_lighting_lux;
}

// =========================================================================
// SOFT SHADOW HELPERS - Temporal jitter for area lights
// =========================================================================
// Contact hardening soft shadows via temporal accumulation:
//   - Jitter shadow ray direction within light's angular radius
//   - Accumulate results over multiple frames
//   - Penumbra width varies with blocker distance (physically correct)

// Halton sequence for low-discrepancy sampling (2D, base 2 and 3)
// Returns value in [0, 1] for given index
inline float halton(uint index, uint base) {
    float f = 1.0f;
    float r = 0.0f;
    uint i = index;
    while (i > 0) {
        f /= float(base);
        r += f * float(i % base);
        i /= base;
    }
    return r;
}

// =========================================================================
// SOFT SHADOW TEMPORAL JITTER - The key to smooth soft shadows
// =========================================================================
// Returns 2D jitter offset in unit disk for ray direction perturbation
//
// CRITICAL FIX (2026-01-31): All pixels must use SAME jitter per frame
//
// BROKEN (caused fractured shadows):
//   sample_idx = (frame_index + pixel_id * 7) % 64
//   - Each pixel got different jitter direction
//   - Adjacent pixels: one jitters left, one jitters right
//   - Result: Noisy, fractured shadow edges
//
// FIXED (smooth soft shadows):
//   sample_idx = frame_index % 4
//   - All pixels use same jitter direction per frame
//   - Frame 0: all jitter toward sample 0
//   - Frame 1: all jitter toward sample 1
//   - Temporal blend over 4 frames → smooth penumbra
//
// The physics: Area lights create penumbra because different points on
// the light surface see different amounts of the occluder. By sampling
// 4 different points (one per frame) and blending, we approximate the
// integral over the light's surface area.
// =========================================================================
inline float2 get_temporal_jitter(uint frame_index, uint pixel_id) {
    // Frame-coherent: all pixels use same jitter per frame (no per-pixel hash).
    // Per-pixel hash was removed because it caused visible dot artifacts —
    // adjacent pixels getting different jitter directions created speckle noise.
    // With coherent jitter, adjacent penumbra pixels trace similar rays →
    // smooth spatial result, temporal accumulation provides coverage over time.
    (void)pixel_id;  // Unused — kept in signature for API compatibility

    // Pre-computed Halton sequence (bases 2 and 3, indices 1-64)
    const float2 halton64[64] = {
        float2(0.500000f, 0.333333f), float2(0.250000f, 0.666667f), float2(0.750000f, 0.111111f), float2(0.125000f, 0.444444f),
        float2(0.625000f, 0.777778f), float2(0.375000f, 0.222222f), float2(0.875000f, 0.555556f), float2(0.062500f, 0.888889f),
        float2(0.562500f, 0.037037f), float2(0.312500f, 0.370370f), float2(0.812500f, 0.703704f), float2(0.187500f, 0.148148f),
        float2(0.687500f, 0.481481f), float2(0.437500f, 0.814815f), float2(0.937500f, 0.259259f), float2(0.031250f, 0.592593f),
        float2(0.531250f, 0.925926f), float2(0.281250f, 0.074074f), float2(0.781250f, 0.407407f), float2(0.156250f, 0.740741f),
        float2(0.656250f, 0.185185f), float2(0.406250f, 0.518519f), float2(0.906250f, 0.851852f), float2(0.093750f, 0.296296f),
        float2(0.593750f, 0.629630f), float2(0.343750f, 0.962963f), float2(0.843750f, 0.012346f), float2(0.218750f, 0.345679f),
        float2(0.718750f, 0.679012f), float2(0.468750f, 0.123457f), float2(0.968750f, 0.456790f), float2(0.015625f, 0.790123f),
        float2(0.515625f, 0.234568f), float2(0.265625f, 0.567901f), float2(0.765625f, 0.901235f), float2(0.140625f, 0.049383f),
        float2(0.640625f, 0.382716f), float2(0.390625f, 0.716049f), float2(0.890625f, 0.160494f), float2(0.078125f, 0.493827f),
        float2(0.578125f, 0.827160f), float2(0.328125f, 0.271605f), float2(0.828125f, 0.604938f), float2(0.203125f, 0.938272f),
        float2(0.703125f, 0.086420f), float2(0.453125f, 0.419753f), float2(0.953125f, 0.753086f), float2(0.046875f, 0.197531f),
        float2(0.546875f, 0.530864f), float2(0.296875f, 0.864198f), float2(0.796875f, 0.308642f), float2(0.171875f, 0.641975f),
        float2(0.671875f, 0.975309f), float2(0.421875f, 0.024691f), float2(0.921875f, 0.358025f), float2(0.109375f, 0.691358f),
        float2(0.609375f, 0.135802f), float2(0.359375f, 0.469136f), float2(0.859375f, 0.802469f), float2(0.234375f, 0.246914f),
        float2(0.734375f, 0.580247f), float2(0.484375f, 0.913580f), float2(0.984375f, 0.061728f), float2(0.007812f, 0.395062f)
    };
    uint sample_idx = frame_index % 64;
    float2 h = halton64[sample_idx];

    // Map to unit disk via concentric mapping (uniform distribution)
    float r = sqrt(h.x);
    float theta = h.y * 2.0f * M_PI_F;
    return float2(r * cos(theta), r * sin(theta));
}

// Construct orthonormal basis from normal (Frisvad's method)
// Returns tangent and bitangent perpendicular to normal
inline void get_tangent_basis(float3 n, thread float3& tangent, thread float3& bitangent) {
    if (n.z < -0.9999f) {
        tangent = float3(0.0f, -1.0f, 0.0f);
        bitangent = float3(-1.0f, 0.0f, 0.0f);
    } else {
        float a = 1.0f / (1.0f + n.z);
        float b = -n.x * n.y * a;
        tangent = float3(1.0f - n.x * n.x * a, b, -n.x);
        bitangent = float3(b, 1.0f - n.y * n.y * a, -n.y);
    }
}

// =========================================================================
// PCSS SAMPLE GENERATION - Deterministic points on unit disk for PCSS phases
// =========================================================================
// Generates PCSS_BLOCKER_SAMPLES points for blocker search (Phase 1)
// and PCSS_PENUMBRA_SAMPLES points for penumbra sampling (Phase 3).
//
// Frame index rotates samples each frame for temporal accumulation.
// Per-pixel Cranley-Patterson rotation decorrelates sampling noise across
// pixels — same technique as the GI kernel (line ~1496). Each pixel applies
// a unique offset to the Halton values before disk mapping, so neighboring
// pixels don't share the same quantized shadow decisions.
inline PCSSsamples get_pcss_samples(uint frame_index, uint pixel_index) {
    // Pre-computed Halton sequence (bases 2 and 3, indices 1-64)
    const float2 halton64[64] = {
        float2(0.500000f, 0.333333f), float2(0.250000f, 0.666667f), float2(0.750000f, 0.111111f), float2(0.125000f, 0.444444f),
        float2(0.625000f, 0.777778f), float2(0.375000f, 0.222222f), float2(0.875000f, 0.555556f), float2(0.062500f, 0.888889f),
        float2(0.562500f, 0.037037f), float2(0.312500f, 0.370370f), float2(0.812500f, 0.703704f), float2(0.187500f, 0.148148f),
        float2(0.687500f, 0.481481f), float2(0.437500f, 0.814815f), float2(0.937500f, 0.259259f), float2(0.031250f, 0.592593f),
        float2(0.531250f, 0.925926f), float2(0.281250f, 0.074074f), float2(0.781250f, 0.407407f), float2(0.156250f, 0.740741f),
        float2(0.656250f, 0.185185f), float2(0.406250f, 0.518519f), float2(0.906250f, 0.851852f), float2(0.093750f, 0.296296f),
        float2(0.593750f, 0.629630f), float2(0.343750f, 0.962963f), float2(0.843750f, 0.012346f), float2(0.218750f, 0.345679f),
        float2(0.718750f, 0.679012f), float2(0.468750f, 0.123457f), float2(0.968750f, 0.456790f), float2(0.015625f, 0.790123f),
        float2(0.515625f, 0.234568f), float2(0.265625f, 0.567901f), float2(0.765625f, 0.901235f), float2(0.140625f, 0.049383f),
        float2(0.640625f, 0.382716f), float2(0.390625f, 0.716049f), float2(0.890625f, 0.160494f), float2(0.078125f, 0.493827f),
        float2(0.578125f, 0.827160f), float2(0.328125f, 0.271605f), float2(0.828125f, 0.604938f), float2(0.203125f, 0.938272f),
        float2(0.703125f, 0.086420f), float2(0.453125f, 0.419753f), float2(0.953125f, 0.753086f), float2(0.046875f, 0.197531f),
        float2(0.546875f, 0.530864f), float2(0.296875f, 0.864198f), float2(0.796875f, 0.308642f), float2(0.171875f, 0.641975f),
        float2(0.671875f, 0.975309f), float2(0.421875f, 0.024691f), float2(0.921875f, 0.358025f), float2(0.109375f, 0.691358f),
        float2(0.609375f, 0.135802f), float2(0.359375f, 0.469136f), float2(0.859375f, 0.802469f), float2(0.234375f, 0.246914f),
        float2(0.734375f, 0.580247f), float2(0.484375f, 0.913580f), float2(0.984375f, 0.061728f), float2(0.007812f, 0.395062f)
    };

    PCSSsamples samples;

    // Base offset: frame rotation only
    uint base = frame_index * (PCSS_BLOCKER_SAMPLES + PCSS_PENUMBRA_SAMPLES);

    // Per-pixel Cranley-Patterson rotation: golden ratio hash per pixel
    // This gives each pixel a unique [0,1) offset applied to Halton values
    float pixel_hash = fract(float(pixel_index) * 0.6180339887f);

    // Blocker samples: map Halton points to unit disk with per-pixel rotation
    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; i++) {
        float2 h = halton64[(base + i) % 64];
        // Cranley-Patterson: shift and wrap to [0,1)
        h = fract(h + float2(pixel_hash));
        float r = sqrt(h.x);
        float theta = h.y * 2.0f * M_PI_F;
        samples.blocker[i] = float2(r * cos(theta), r * sin(theta));
    }

    // Penumbra samples: offset from blocker samples in Halton sequence
    for (int i = 0; i < PCSS_PENUMBRA_SAMPLES; i++) {
        float2 h = halton64[(base + PCSS_BLOCKER_SAMPLES + i) % 64];
        h = fract(h + float2(pixel_hash));
        float r = sqrt(h.x);
        float theta = h.y * 2.0f * M_PI_F;
        samples.penumbra[i] = float2(r * cos(theta), r * sin(theta));
    }

    return samples;
}

// =========================================================================
// METAL RAY TRACING VERSION - Hardware-accelerated shadow rays (M3+ only)
// =========================================================================
// Uses Metal RT API to leverage dedicated RT cores for BVH traversal
// REPLACES: Manual BVH traversal (~100 lines) with hardware intersection (~5 lines)
//
// SOFT SHADOWS: Temporal jitter within light's angular radius
//   - angular_radius = light_size / distance_to_light
//   - Jitter direction by angular_radius, accumulate over frames
//   - Zero extra cost per frame (same ray count as hard shadows)
//
// REQUIREMENTS:
//   - macOS 14.0+ (Sonoma)
//   - Apple Silicon M3+ (MTLGPUFamilyApple9)
//   - Acceleration structure built from triangles
//
// EXPECTED SPEEDUP: 5-10× vs software BVH (RT cores vs compute units)
//
// ARCHITECTURE:
//   - Acceleration structure: Driver-built optimized BVH
//   - intersector<>: Hardware-accelerated ray-primitive test
//   - accept_any_intersection(true): Shadow ray optimization (first hit = done)

// =========================================================================
// DETERMINISTIC SHADOW KERNEL (Metal RT)
// =========================================================================
// 1 closest-hit ray per pixel per light. No stochastic sampling, no temporal
// accumulation, no denoise. Outputs hard shadow lux + blocker distance.
//
// Used by all PenumbraMode values except PCSS:
//   NONE:             Hard shadows only (shadow_results = hard lux)
//   SOLID_ANGLE:      In-kernel analytical penumbra via triangle solid angle
//   SCREEN_GRADIENT:  Hard shadows + blocker_distance for post-process gradient
//   BLOCKER_MAP:      Hard shadows + blocker_distance for post-process analysis
//   BLOCKER_GRADIENT: Hard shadows + blocker_distance for combined A+C post-process
//
// Van Oosterom-Strackee formula for solid angle of a triangle from a point:
//   Omega = 2 * atan2(|dot(a, cross(b,c))|, |a||b||c| + dot(a,b)|c| + dot(b,c)|a| + dot(c,a)|b|)
// where a,b,c = unit vectors from point to triangle vertices
inline float compute_triangle_solid_angle(float3 P, float3 v0, float3 v1, float3 v2) {
    float3 a = v0 - P;
    float3 b = v1 - P;
    float3 c = v2 - P;
    float la = length(a);
    float lb = length(b);
    float lc = length(c);
    // Degenerate cases: point is at or very near a vertex
    if (la < 1e-6f || lb < 1e-6f || lc < 1e-6f) return 0.0f;
    a /= la; b /= lb; c /= lc;
    float num = abs(dot(a, cross(b, c)));
    float den = 1.0f + dot(a, b) + dot(b, c) + dot(c, a);
    if (den <= 0.0f) return 0.0f;  // Triangle wraps > hemisphere — clamp
    return 2.0f * atan2(num, den);
}

kernel void trace_shadows_deterministic(
    primitive_acceleration_structure accel [[buffer(0)]],
    constant GBufferPixel* gbuffer [[buffer(1)]],
    device float* shadow_results [[buffer(2)]],             // Output: total lighting lux (scalar per pixel)
    constant uint& shadow_width [[buffer(3)]],
    constant uint& shadow_height [[buffer(4)]],
    constant uint& gbuffer_width [[buffer(5)]],
    constant uint& gbuffer_height [[buffer(6)]],
    constant LightData* lights [[buffer(7)]],
    constant uint& light_count [[buffer(8)]],
    device float* blocker_distance [[buffer(9)]],           // Output: blocker distance from dominant blocked light
    constant Triangle* shadow_triangles [[buffer(10)]],     // Triangle buffer (for solid angle lookup)
    constant uint& triangle_count [[buffer(11)]],           // Triangle count
    device float4* light_color [[buffer(12)]],              // Output: per-channel color ratio (RGB, 0-1)
    uint gid [[thread_position_in_grid]])
{
    // Bounds check
    if (gid >= shadow_width * shadow_height) return;

    uint shadow_pixel_index = gid;
    uint shadow_px = shadow_pixel_index % shadow_width;
    uint shadow_py = shadow_pixel_index / shadow_width;

    // Map to G-buffer coordinates
    float scale_x = (float)gbuffer_width / (float)shadow_width;
    float scale_y = (float)gbuffer_height / (float)shadow_height;
    uint gbuffer_px = min((uint)((shadow_px + 0.5f) * scale_x), gbuffer_width - 1);
    uint gbuffer_py = min((uint)((shadow_py + 0.5f) * scale_y), gbuffer_height - 1);
    uint gbuffer_pixel_index = gbuffer_py * gbuffer_width + gbuffer_px;

    GBufferPixel pixel = gbuffer[gbuffer_pixel_index];

    // Skip sky pixels
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        shadow_results[shadow_pixel_index] = 0.0f;
        blocker_distance[shadow_pixel_index] = 0.0f;
        light_color[shadow_pixel_index] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        return;
    }

    float3 world_pos = pixel.world_pos;
    float3 normal = pixel.normal;
    float total_lighting_lux = 0.0f;
    float3 total_lighting_rgb = float3(0.0f);

    // Track blocker distance from the light whose blocked contribution would be highest
    float max_blocked_potential_lux = 0.0f;
    float dominant_blocker_dist = 0.0f;

    // Setup intersector — ALWAYS closest-hit for blocker distance
    intersector<triangle_data> shadow_intersector;
    shadow_intersector.accept_any_intersection(false);

    for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
        LightData light = lights[light_idx];

        float3 to_light = float3(light.position) - world_pos;
        float distance_sq = dot(to_light, to_light);

        if (distance_sq <= light.emission_radius * light.emission_radius) {
            float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_distance_sq);
            float3 light_dir = to_light * inv_dist;
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian > 0.0f) {
                float3 ray_origin = world_pos + normal * SHADOW_RAY_NORMAL_OFFSET;
                float dist_to_light = sqrt(safe_distance_sq);
                float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);

                float potential_lux = intensity_lux * lambertian;

                // Smooth fade at emission_radius boundary (last 20%)
                float radius_ratio = dist_to_light / light.emission_radius;
                float radius_fade = (radius_ratio > 0.8f) ? (1.0f - radius_ratio) * 5.0f : 1.0f;

                // Single closest-hit ray to light center
                ray shadow_ray;
                shadow_ray.origin = ray_origin;
                shadow_ray.direction = light_dir;
                shadow_ray.min_distance = 0.001f;
                shadow_ray.max_distance = dist_to_light;

                auto result = shadow_intersector.intersect(shadow_ray, accel);

                if (result.type == intersection_type::none) {
                    // Fully lit by this light (with radius fade)
                    float faded_lux = potential_lux * radius_fade;
                    total_lighting_lux += faded_lux;
                    total_lighting_rgb += faded_lux * float3(light.emission_color);
                } else {
                    // Blocked — record blocker distance for penumbra post-processing
                    float blocker_dist = result.distance;

                    // Track dominant blocker (highest potential contribution that was blocked)
                    if (potential_lux > max_blocked_potential_lux) {
                        max_blocked_potential_lux = potential_lux;
                        dominant_blocker_dist = blocker_dist;
                    }

                    // Approach B: Solid angle penumbra (computed in-kernel)
                    // Only active when PENUMBRA_MODE == SOLID_ANGLE (3)
#if PENUMBRA_MODE == 3
                    if (light.light_size > 0.001f) {
                        // Look up the blocking triangle vertices via primitive_id
                        uint tri_id = result.primitive_id;
                        if (tri_id < triangle_count) {
                            Triangle tri = shadow_triangles[tri_id];
                            float3 v0 = float3(tri.v0);
                            float3 v1 = float3(tri.v1);
                            float3 v2 = float3(tri.v2);

                            // Solid angle of blocking triangle as seen from this pixel
                            float tri_solid_angle = compute_triangle_solid_angle(world_pos, v0, v1, v2);

                            // Solid angle of the light disc as seen from this pixel
                            // Omega_light = pi * (r/d)^2 for a disc at distance d with radius r
                            float light_angular_radius = light.light_size / dist_to_light;
                            float light_solid_angle = M_PI_F * light_angular_radius * light_angular_radius;

                            // Shadow value: fraction of light NOT blocked
                            // Clamp: triangle can't block more than the entire light
                            float occlusion = clamp(tri_solid_angle / light_solid_angle, 0.0f, 1.0f);
                            float shadow_value = 1.0f - occlusion;

                            total_lighting_lux += potential_lux * shadow_value;
                            total_lighting_rgb += potential_lux * shadow_value * float3(light.emission_color);
                        }
                        // else: invalid tri_id, treat as fully blocked (0 contribution)
                    }
#endif
                    // For non-SOLID_ANGLE modes: fully blocked, 0 contribution
                }
            }
        }
    }

    // Direct write — no temporal accumulation, no blending
    shadow_results[shadow_pixel_index] = total_lighting_lux;
    blocker_distance[shadow_pixel_index] = dominant_blocker_dist;

    // Store color ratio: per-channel / total, so denoise/penumbra on the scalar
    // automatically preserves color proportions. White light → (1,1,1).
    if (total_lighting_lux > 0.001f) {
        light_color[shadow_pixel_index] = float4(total_lighting_rgb / total_lighting_lux, 0.0f);
    } else {
        light_color[shadow_pixel_index] = float4(1.0f, 1.0f, 1.0f, 0.0f);
    }
}

kernel void trace_shadows_metal_rt(
    primitive_acceleration_structure accel [[buffer(0)]],   // Driver-built acceleration structure
    constant GBufferPixel* gbuffer [[buffer(1)]],           // G-buffer from Pass 1
    device float* shadow_results [[buffer(2)]],             // Output: lighting per pixel
    constant uint& shadow_width [[buffer(3)]],              // Shadow buffer width
    constant uint& shadow_height [[buffer(4)]],             // Shadow buffer height
    constant uint& gbuffer_width [[buffer(5)]],             // G-buffer width (full res)
    constant uint& gbuffer_height [[buffer(6)]],            // G-buffer height (full res)
    constant LightData* lights [[buffer(7)]],               // Light sources
    constant uint& light_count [[buffer(8)]],               // Number of lights
    constant uint* frame_index [[buffer(9)]],               // Frame index for temporal
    device float* temporal_buffer [[buffer(10)]],           // Temporal buffer
    constant uint* pixel_indices [[buffer(11)]],            // Indirect dispatch indices
    device uint32_t* prev_particle_id [[buffer(12)]],       // Motion detection: prev frame particle IDs
    constant float2& camera_delta [[buffer(13)]],           // Screen-space camera movement for reprojection
    device uint32_t* sample_count [[buffer(14)]],           // Running average sample count (convergence)
    uint gid [[thread_position_in_grid]])
{
    // Pixel selection (same as batched kernel)
    uint shadow_pixel_index;
    uint current_frame = frame_index[0];
    bool is_warmup = (current_frame >= 999);

    if (is_warmup) {
        if (gid >= shadow_width * shadow_height) {
            return;
        }
        shadow_pixel_index = gid;
    } else {
        shadow_pixel_index = pixel_indices[gid];
    }

    uint shadow_px = shadow_pixel_index % shadow_width;
    uint shadow_py = shadow_pixel_index / shadow_width;

    // Map to G-buffer coordinates
    float scale_x = (float)gbuffer_width / (float)shadow_width;
    float scale_y = (float)gbuffer_height / (float)shadow_height;
    uint gbuffer_px = min((uint)((shadow_px + 0.5f) * scale_x), gbuffer_width - 1);
    uint gbuffer_py = min((uint)((shadow_py + 0.5f) * scale_y), gbuffer_height - 1);
    uint gbuffer_pixel_index = gbuffer_py * gbuffer_width + gbuffer_px;

    GBufferPixel pixel = gbuffer[gbuffer_pixel_index];

    // Skip sky pixels
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        shadow_results[shadow_pixel_index] = 0.0f;
        return;
    }

    float3 world_pos = pixel.world_pos;
    float3 normal = pixel.normal;
    float total_lighting_lux = 0.0f;

    // =========================================================================
    // SOFT SHADOW TEMPORAL ACCUMULATION - Read previous frame's value
    // =========================================================================
    // When soft shadows enabled and tracing all pixels each frame (TEMPORAL_FRAME_COUNT=1),
    // we blend with previous frame's value for smooth penumbra over time.
    // Alpha = 1/SOFT_SHADOW_TEMPORAL_FRAMES (e.g., 0.25 for 4-frame accumulation)
    //
    // MOTION DETECTION WITH TEMPORAL REPROJECTION:
    // Problem: When camera moves, comparing prev_particle_id[screen_pixel] with current
    // particle_id fails because the same screen pixel now shows a different world position.
    // Solution: Reproject - "where was this world position on screen LAST frame?"
    //   curr_world_pos → prev_camera_transform → prev_screen_pos → sample prev_particle_id
    // If particle_id matches: camera moved but same surface → blend (no aura)
    // If particle_id differs: object moved or newly visible → no blend (prevents ghosting)
    bool motion_detected = false;
#ifdef USE_SOFT_SHADOWS
    #if TEMPORAL_FRAME_COUNT == 1
    // Reproject FIRST: where was this world position on screen LAST frame?
    int prev_px = (int)shadow_px - (int)round(camera_delta.x);
    int prev_py = (int)shadow_py - (int)round(camera_delta.y);

    // Read BOTH prev_lighting AND prev_particle_id from reprojected position
    float prev_lighting = 0.0f;
    uint32_t prev_pid = GBUFFER_SKY_ID;
    if (prev_px >= 0 && prev_px < (int)shadow_width &&
        prev_py >= 0 && prev_py < (int)shadow_height) {
        uint prev_idx = prev_py * shadow_width + prev_px;
        prev_pid = prev_particle_id[prev_idx];
        prev_lighting = temporal_buffer[prev_idx];  // FIX: read from reprojected position
    }
    uint32_t curr_pid = pixel.particle_id;
    motion_detected = (prev_pid != curr_pid);
    #endif
#endif

#ifdef USE_TEMPORAL_LIGHTING
    #if !defined(USE_SOFT_SHADOWS) || TEMPORAL_FRAME_COUNT > 1
    temporal_buffer[shadow_pixel_index] = 0.0f;
    #endif
#endif

    // =========================================================================
    // METAL RT INTERSECTOR SETUP
    // =========================================================================
    // intersector<triangle_data>: Hardware-accelerated ray-triangle intersection
    // accept_any_intersection mode is set per-phase inside the light loop:
    //   Phase 1 (blocker search): false — need closest hit distance
    //   Phase 3 (penumbra sample): true — any hit = occluded (fast)
    intersector<triangle_data> shadow_intersector;

    // Loop over all lights
    for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
        LightData light = lights[light_idx];

        float3 to_light = float3(light.position) - world_pos;
        float distance_sq = dot(to_light, to_light);

        if (distance_sq <= light.emission_radius * light.emission_radius) {
            float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_distance_sq);
            float3 light_dir = to_light * inv_dist;
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian > 0.0f) {
                float3 ray_origin = world_pos + normal * SHADOW_RAY_NORMAL_OFFSET;
                float dist_to_light = sqrt(safe_distance_sq);
                float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);

                // Point lights (light_size == 0): single hard shadow ray (unchanged)
                if (light.light_size <= 0.0f) {
                    shadow_intersector.accept_any_intersection(true);
                    ray shadow_ray;
                    shadow_ray.origin = ray_origin;
                    shadow_ray.direction = light_dir;
                    shadow_ray.min_distance = 0.001f;
                    shadow_ray.max_distance = dist_to_light;

                    auto result = shadow_intersector.intersect(shadow_ray, accel);
                    if (result.type == intersection_type::none) {
                        total_lighting_lux += intensity_lux * lambertian;
                    }
                } else {
                    // ==========================================================
                    // PCSS: 3-Phase Percentage-Closer Soft Shadows
                    // ==========================================================
                    // Phase 1: Blocker search (4 closest-hit rays on full light disc)
                    // Phase 2: Penumbra estimation (pure math, no rays)
                    // Phase 3: Penumbra sampling (8 any-hit rays within penumbra cone)
                    //
                    // Result: continuous shadow_value per frame (not binary).
                    // Temporal accumulation still runs but converges much faster.

                    float angular_radius = light.light_size / dist_to_light;
                    float3 tangent, bitangent;
                    get_tangent_basis(light_dir, tangent, bitangent);

                    PCSSsamples samples = get_pcss_samples(current_frame, shadow_pixel_index);

                    // ---- Phase 1: Blocker search (closest-hit) ----
                    shadow_intersector.accept_any_intersection(false);
                    int blocker_hits = 0;
                    float blocker_dist_sum = 0.0f;

                    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; i++) {
                        float3 dir = normalize(light_dir
                            + tangent * samples.blocker[i].x * angular_radius
                            + bitangent * samples.blocker[i].y * angular_radius);

                        ray shadow_ray;
                        shadow_ray.origin = ray_origin;
                        shadow_ray.direction = dir;
                        shadow_ray.min_distance = 0.001f;
                        shadow_ray.max_distance = dist_to_light * 1.1f;

                        auto result = shadow_intersector.intersect(shadow_ray, accel);
                        if (result.type != intersection_type::none) {
                            blocker_hits++;
                            blocker_dist_sum += result.distance;
                        }
                    }

                    float shadow_value;

                    if (blocker_hits == 0) {
                        // Fully lit: no blockers found — geometrically certain
                        shadow_value = 1.0f;
                    } else {
                        // ---- Phase 2: Penumbra estimation (pure math) ----
                        float avg_blocker_dist = blocker_dist_sum / float(blocker_hits);
                        float penumbra_ratio = (dist_to_light - avg_blocker_dist) / dist_to_light;
                        float penumbra_width = clamp(penumbra_ratio * light.light_size, 0.0f, light.light_size);
                        float penumbra_angular = penumbra_width / dist_to_light;

                        // ---- Phase 3: Penumbra sampling (any-hit) ----
                        // Always run even when all blockers hit — the penumbra cone
                        // may still find lit paths, avoiding the hard boundary that
                        // caused frame-to-frame dancing at the shadow edge.
                        shadow_intersector.accept_any_intersection(true);
                        int lit_count = 0;

                        for (int i = 0; i < PCSS_PENUMBRA_SAMPLES; i++) {
                            float3 dir = normalize(light_dir
                                + tangent * samples.penumbra[i].x * penumbra_angular
                                + bitangent * samples.penumbra[i].y * penumbra_angular);

                            ray shadow_ray;
                            shadow_ray.origin = ray_origin;
                            shadow_ray.direction = dir;
                            shadow_ray.min_distance = 0.001f;
                            shadow_ray.max_distance = dist_to_light * 1.1f;

                            auto result = shadow_intersector.intersect(shadow_ray, accel);
                            if (result.type == intersection_type::none) {
                                lit_count++;
                            }
                        }

                        shadow_value = float(lit_count) / float(PCSS_PENUMBRA_SAMPLES);
                    }

                    total_lighting_lux += intensity_lux * lambertian * shadow_value;
                }
            }
        }
    }

    // =========================================================================
    // SOFT SHADOW TEMPORAL BLENDING - Accumulate over frames
    // =========================================================================
    // Motion-aware temporal blending for soft shadows:
    // - When surface changed (motion_detected): use direct write (no ghosting/aura)
    // - When surface stable: blend with previous frame for smooth penumbra
    //
    // Alpha = 1/SOFT_SHADOW_TEMPORAL_FRAMES (e.g., 0.25 for 4-frame accumulation)
#ifdef USE_SOFT_SHADOWS
    #if TEMPORAL_FRAME_COUNT == 1
    // Capped running average: weight decreases as 1/N until N reaches cap, then stays at 1/cap.
    // No freeze — low-alpha EMA continues to wash out self-intersection variance (grey spots).
    // At cap=64, max per-frame shift ≈ 1.6 lux → ~1 brightness unit (invisible after denoise).
    if (motion_detected) {
        shadow_results[shadow_pixel_index] = total_lighting_lux;
        temporal_buffer[shadow_pixel_index] = total_lighting_lux;
        sample_count[shadow_pixel_index] = 1;
    } else {
        uint count = min(sample_count[shadow_pixel_index] + 1, (uint)SOFT_SHADOW_MAX_SAMPLES);
        float weight = 1.0f / float(count);
        float blended = mix(prev_lighting, total_lighting_lux, weight);
        shadow_results[shadow_pixel_index] = blended;
        temporal_buffer[shadow_pixel_index] = blended;
        sample_count[shadow_pixel_index] = count;
    }
    prev_particle_id[shadow_pixel_index] = curr_pid;
    #else
    // Temporal distribution mode: direct write (no soft shadow blending)
    shadow_results[shadow_pixel_index] = total_lighting_lux;
    #ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] = total_lighting_lux;
    #endif
    #endif
#else
    // Soft shadows disabled: direct write
    shadow_results[shadow_pixel_index] = total_lighting_lux;
    #ifdef USE_TEMPORAL_LIGHTING
    temporal_buffer[shadow_pixel_index] = total_lighting_lux;
    #endif
#endif
}
// =========================================================================
// (RETIRED 2026-07-29) compute_ssgi, compute_indirect_rays,
// denoise_gi_buffer, denoise_gi_atrous lived here — the screen-space and
// BVH-indirect GI path, replaced by SSDO (Pass 2.7) + DDGI (Pass 2.5b/c).
// Both producers had been compile-time disabled and contributed zeros.
// =========================================================================


// =========================================================================
// PASS 2.05: Shadow buffer spatial denoiser
// 5x5 cross-bilateral filter on shadow lux values (before tone mapping).
// Edge-aware: preserves hard shadow boundaries using normal, world-position,
// and particle_id from the G-buffer. Only smooths noisy penumbra pixels.
// =========================================================================
kernel void denoise_shadow_buffer(
    device const float* shadow_results [[buffer(0)]],       // Input: raw shadow lux (shadow res)
    device float* shadow_results_denoised [[buffer(1)]],    // Output: denoised lux (shadow res)
    device const GBufferPixel* gbuffer [[buffer(2)]],       // G-buffer (full res) for edge detection
    constant uint& shadow_width [[buffer(3)]],              // Shadow buffer width (reduced)
    constant uint& shadow_height [[buffer(4)]],             // Shadow buffer height (reduced)
    constant uint& gbuffer_width [[buffer(5)]],             // G-buffer width (full)
    constant uint& gbuffer_height [[buffer(6)]],            // G-buffer height (full)
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    if (px >= (int)shadow_width || py >= (int)shadow_height) {
        return;
    }

    uint idx = py * shadow_width + px;

    // Map shadow pixel to G-buffer pixel (scale up)
    float scale_x = (float)gbuffer_width / (float)shadow_width;
    float scale_y = (float)gbuffer_height / (float)shadow_height;
    uint gbuf_px = (uint)((px + 0.5f) * scale_x);
    uint gbuf_py = (uint)((py + 0.5f) * scale_y);
    gbuf_px = min(gbuf_px, gbuffer_width - 1);
    gbuf_py = min(gbuf_py, gbuffer_height - 1);
    uint gbuf_idx = gbuf_py * gbuffer_width + gbuf_px;

    GBufferPixel center_pixel = gbuffer[gbuf_idx];

    // Sky pixels: pass through as 0
    if (center_pixel.particle_id == GBUFFER_SKY_ID) {
        shadow_results_denoised[idx] = 0.0f;
        return;
    }

    float3 center_normal = normalize(float3(center_pixel.normal));
    float3 center_pos = float3(center_pixel.world_pos);
    float center_lux = shadow_results[idx];

    float sum = center_lux;
    float weight = 1.0f;

    // 5x5 neighborhood
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;

            int nx = px + dx;
            int ny = py + dy;
            if (nx < 0 || nx >= (int)shadow_width || ny < 0 || ny >= (int)shadow_height) continue;

            // Map neighbor shadow pixel to G-buffer
            uint n_gbuf_px = min((uint)((nx + 0.5f) * scale_x), gbuffer_width - 1);
            uint n_gbuf_py = min((uint)((ny + 0.5f) * scale_y), gbuffer_height - 1);
            uint n_gbuf_idx = n_gbuf_py * gbuffer_width + n_gbuf_px;
            GBufferPixel neighbor = gbuffer[n_gbuf_idx];

            // Hard edge: different object
            if (neighbor.particle_id == GBUFFER_SKY_ID) continue;
            if (neighbor.particle_id != center_pixel.particle_id) continue;

            // Normal weight: exponential falloff across different surface orientations
            float3 n_normal = normalize(float3(neighbor.normal));
            float ndot = max(0.0f, dot(center_normal, n_normal));
            float w_normal = pow(ndot, 32.0f);

            // Position weight: prevent averaging across geometry gaps
            float3 n_pos = float3(neighbor.world_pos);
            float dist = distance(center_pos, n_pos);
            float w_pos = exp(-dist / 0.5f);

            float w = w_normal * w_pos;
            if (w < 0.001f) continue;

            uint n_idx = ny * shadow_width + nx;
            sum += shadow_results[n_idx] * w;
            weight += w;
        }
    }

    shadow_results_denoised[idx] = sum / weight;
}
