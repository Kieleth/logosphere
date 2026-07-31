// Single-pass implementation with inline segment sampling
// Direct processing: depth test → lighting (segmented) → write
#include "logosphere/rendering/surface_rasterizer.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/depth_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "../object_id.h"
#include "logosphere/rendering/shadow_sampling_config.h"
#include "../lighting_config.h"
#include "../lighting_primitives.h"
#include "logosphere/physics/bvh.h"
#include "../engine_metrics.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <atomic>

// Segment sampling constants
static constexpr int SEGMENT_SIZE = Optimizations::SHADOW_SEGMENT_SIZE;
static constexpr float SHADOW_DIFF_THRESHOLD = 0.1f;

// Type aliases for cleaner code
using CollectedPixel = SurfaceRasterizer::CollectedPixel;

// Helper: Write pixel with lighting applied
// Non-static so it can be used from surface_rasterizer.cpp (Phase II-C)
// CONVERGENCE POINT: Both CPU and GPU rendering paths use this function
void write_lit_pixel(
    PixelBuffer& pixel_buffer,
    int x, int y,
    float lighting,  // Lighting in lux
    float particle_r, float particle_g, float particle_b, float particle_a,  // Color values (not pointer!)
    int particle_index,
    SparseObjectMap* object_map)  // Thread-local object map for mouse interaction
{
    static int debug_count = 0;
    static int non_zero_count = 0;

    if (lighting > 0) {
        non_zero_count++;
        if (non_zero_count <= 5) {
            std::cout << "[WRITE_LIT_PIXEL] lighting=" << lighting << " at (" << x << "," << y << ") BEFORE tone_map\n";
        }
    }

    debug_count++;
    if (debug_count == 100000) {
        std::cout << "[WRITE_LIT_PIXEL] Called 100k times, non_zero=" << non_zero_count << "\n";
    }

    // Tone map lux to RGB brightness
    LightingConfig& config = LightingConfig::get();
    int brightness_rgb = config.tone_map_to_rgb(lighting);
    float brightness_factor = brightness_rgb / 255.0f;

    // Apply brightness to particle's base color
    uint8_t r = static_cast<uint8_t>(particle_r * 255.0f * brightness_factor);
    uint8_t g = static_cast<uint8_t>(particle_g * 255.0f * brightness_factor);
    uint8_t b = static_cast<uint8_t>(particle_b * 255.0f * brightness_factor);
    uint8_t a = static_cast<uint8_t>(particle_a * 255);

    if (lighting > 0 && non_zero_count <= 5) {
        std::cout << "[WRITE_LIT_PIXEL] brightness_rgb=" << brightness_rgb
                  << " factor=" << brightness_factor
                  << " RGB=(" << (int)r << "," << (int)g << "," << (int)b << ")\n";
    }

    // Write visual pixel to buffer
    pixel_buffer.set_pixel_with_object(x, y, r, g, b, a, 0);  // PixelBuffer doesn't store IDs

    if (lighting > 0 && non_zero_count <= 5) {
        auto readback = pixel_buffer.get_pixel(x, y);
        std::cout << "[WRITE_LIT_PIXEL] Readback=(" << (int)readback.r << "," << (int)readback.g << "," << (int)readback.b << ")\n";
    }

    // Write object ID to sparse map (for mouse interaction)
    // Both CPU and GPU paths converge here - single source of truth
    if (object_map && particle_index != 0) {
        int encoded_id = ObjectID::encode_particle_surface(particle_index, 0);  // surface_id=0 for now
        object_map->set_object(x, y, encoded_id);

        // DEBUG: Log particle ID writes with count per particle
        static std::atomic<int> particle_write_counts[100] = {};  // Track first 100 particles
        if (particle_index < 100) {
            int count = particle_write_counts[particle_index].fetch_add(1);
            if (count == 0) {  // Log first write for each particle
                std::cout << "[OBJECT ID FIRST] Particle " << particle_index
                          << " (encoded=" << encoded_id << ") at pixel (" << x << "," << y << ")" << std::endl;
            }
        }
    }
}

// Binary search refinement now in shadow_sampling_strategy.h
// (ShadowSampling::Strategy<ShadowSampling::Pattern::SCANLINE_1D>::refine_segment_binary)

// Helper: Convert CPU shadow rays → GPU shadow rays (Phase II-A integration)
// Non-static so it can be used from surface_rasterizer.cpp (Phase II-C)
void convert_to_gpu_rays(
    const std::vector<ShadowRay>& cpu_rays,
    std::vector<Logosphere::ShadowRay>& gpu_rays)
{
    gpu_rays.resize(cpu_rays.size());
    for (size_t i = 0; i < cpu_rays.size(); i++) {
        const ShadowRay& cpu = cpu_rays[i];
        Logosphere::ShadowRay& gpu = gpu_rays[i];

        // Origin = pixel world position (surface point)
        gpu.origin[0] = cpu.origin_x;
        gpu.origin[1] = cpu.origin_y;
        gpu.origin[2] = cpu.origin_z;

        // Calculate vector from surface to light
        float dx = cpu.target_x - cpu.origin_x;
        float dy = cpu.target_y - cpu.origin_y;
        float dz = cpu.target_z - cpu.origin_z;

        // Distance to light = length of vector (Pythagorean theorem)
        // This is CRITICAL for fixing "mirror shadow" bug - see metal_compute_bridge.h:ShadowRay
        float len = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (len > 0.0001f) {
            // Store actual distance to light (FIX for mirror shadow bug)
            // GPU shader will check: MIN_T < t < max_distance
            // This prevents rays from continuing past the light and hitting objects
            // on the opposite side (which would create impossible shadows)
            gpu.max_distance = len;

            // Direction = normalized vector to light
            gpu.direction[0] = dx / len;
            gpu.direction[1] = dy / len;
            gpu.direction[2] = dz / len;
        } else {
            // Degenerate ray (origin == target, surface is AT the light)
            // This shouldn't happen in practice, but handle gracefully
            gpu.max_distance = 0.0001f;  // Effectively no distance
            gpu.direction[0] = 0;
            gpu.direction[1] = 0;
            gpu.direction[2] = 1;  // Arbitrary direction (won't hit anything)
        }
    }
}

// Particle geometry snapshot for GPU triangle conversion
// Protects from vector reallocation during chunk loading (same pattern as LightSnapshot)
struct ParticleGeometrySnapshot {
    uint32_t particle_id;
    bool is_light_source;
    uint8_t color_r, color_g, color_b, color_a;  // Particle color for indirect GI
    std::vector<Surface> surfaces;
};

// Helper: Convert particles to GPU triangles (Phase II-B)
// Each particle becomes 12 triangles (triangle cube geometry)
void convert_particles_to_gpu_triangles(
    const std::vector<ParticleGeometrySnapshot>& particle_snapshots,
    std::vector<Logosphere::ShadowTriangle>& gpu_triangles,
    std::unordered_map<uint32_t, int>& particle_to_triangle_slot,
    std::vector<bool>& triangle_slot_active,
    int& next_free_slot,
    bool& bvh_needs_rebuild)
{
    // =========================================================================
    // BVH REFITTING OPTIMIZATION: Stable Triangle Slot Mapping
    // =========================================================================
    // Instead of clear+rebuild, maintain stable particle_id → triangle_slot mapping.
    // This allows BVH to refit (update AABBs) instead of full rebuild.
    //
    // OLD: gpu_triangles.clear() → invalidates BVH leaf node indices!
    // NEW: Update triangle vertices in-place at stable slots
    // =========================================================================

    // PARTICLE SNAPSHOT PROTECTION:
    // We now receive ParticleGeometrySnapshot instead of raw particle references.
    // This protects from vector reallocation during chunk loading (same as LightSnapshot).
    // The snapshots are created by the caller while holding particle data, then passed here.
    // This function can safely iterate without worrying about particle vector changes.

    const size_t particle_count = particle_snapshots.size();
    if (particle_count == 0) {
        return;  // No particles to process
    }

    // Pre-allocate triangle array based on highest allocated slot
    // (NOT particle_count, because next_free_slot can exceed particle_count with chunk loading/unloading)
    constexpr int TRIANGLES_PER_PARTICLE = 12;
    const size_t min_required_size = next_free_slot + (particle_count * TRIANGLES_PER_PARTICLE);

    if (gpu_triangles.size() < min_required_size) {
        gpu_triangles.resize(min_required_size);
        triangle_slot_active.resize(min_required_size, false);
        bvh_needs_rebuild = true;  // Size changed, need full rebuild
    }

    // Mark all slots as inactive initially (will mark active as we process)
    std::fill(triangle_slot_active.begin(), triangle_slot_active.end(), false);

    // Process each particle snapshot and update triangles in-place
    // Snapshots are stable data - no need for locks
    for (size_t i = 0; i < particle_count; i++) {
        const auto& snapshot = particle_snapshots[i];

        // Skip light sources - they don't cast shadows
        if (snapshot.is_light_source) {
            // Mark particle's slot as inactive (if allocated)
            auto it = particle_to_triangle_slot.find(snapshot.particle_id);
            if (it != particle_to_triangle_slot.end()) {
                int slot = it->second;
                for (int i = 0; i < TRIANGLES_PER_PARTICLE; i++) {
                    triangle_slot_active[slot + i] = false;
                }
            }
            continue;
        }

        // Look up or allocate triangle slot for this particle
        int slot;
        auto it = particle_to_triangle_slot.find(snapshot.particle_id);
        if (it != particle_to_triangle_slot.end()) {
            // Reuse existing slot
            slot = it->second;
        } else {
            // First time seeing this particle - allocate new slot
            //
            // CRITICAL: Use next_free_slot, NOT map.size()!
            // Why: With dynamic chunk loading/unloading, particles get deleted.
            // When deleted, map entries are removed, causing map.size() to shrink.
            // But triangle slots remain allocated in gpu_triangles vector.
            // Using map.size() after deletions can allocate slots that already exist,
            // or worse, allocate slots beyond vector capacity → container overflow!
            //
            // Example: Start with 650 particles (map.size=650, slots 0-7799).
            //          Delete 100 → map.size=550
            //          Add new particle → old code: slot = 550*12 = 6600 (WRONG! Already used)
            //                          → new code: slot = next_free_slot = 7800 (CORRECT!)
            //
            // Alternative considered: Snapshot triangles each frame (like we did for lights).
            // Rejected because: Triangles are 280KB/frame vs lights 100 bytes/frame.
            // Slot tracking maintains BVH refitting optimization (15× faster than rebuild).
            slot = next_free_slot;
            next_free_slot += TRIANGLES_PER_PARTICLE;
            particle_to_triangle_slot[snapshot.particle_id] = slot;
            bvh_needs_rebuild = true;  // New particle, need full rebuild
        }

        // Use pre-computed surfaces from snapshot (no particle vector access!)
        const auto& surfaces = snapshot.surfaces;

        // Update triangles in-place at stable slot
        int tri_idx = 0;
        for (const auto& surface : surfaces) {
            if (surface.vertex_count == 3 && tri_idx < TRIANGLES_PER_PARTICLE) {
                int slot_idx = slot + tri_idx;

                // Update triangle vertices in-place (no push_back!)
                gpu_triangles[slot_idx].v0[0] = surface.vertices[0][0];
                gpu_triangles[slot_idx].v0[1] = surface.vertices[0][1];
                gpu_triangles[slot_idx].v0[2] = surface.vertices[0][2];

                gpu_triangles[slot_idx].v1[0] = surface.vertices[1][0];
                gpu_triangles[slot_idx].v1[1] = surface.vertices[1][1];
                gpu_triangles[slot_idx].v1[2] = surface.vertices[1][2];

                gpu_triangles[slot_idx].v2[0] = surface.vertices[2][0];
                gpu_triangles[slot_idx].v2[1] = surface.vertices[2][1];
                gpu_triangles[slot_idx].v2[2] = surface.vertices[2][2];

                // Store particle color for indirect GI ray hits (BGRA order matches Metal uchar4)
                gpu_triangles[slot_idx].base_color[0] = snapshot.color_b;
                gpu_triangles[slot_idx].base_color[1] = snapshot.color_g;
                gpu_triangles[slot_idx].base_color[2] = snapshot.color_r;
                gpu_triangles[slot_idx].base_color[3] = snapshot.color_a;

                // Mark slot as active
                triangle_slot_active[slot_idx] = true;

                tri_idx++;
            }
        }
    }

    // Note: BVH will now refit (update AABBs) instead of full rebuild
    // unless bvh_needs_rebuild was set due to new particles or size change
}

// Helper: Calculate lighting for multiple pixels using SIMD batched shadow rays
// Collects all shadow rays (pixels × lights), tests them in batches of 4, returns per-pixel lighting
static void calculate_segment_lighting_batched(
    const std::vector<const Particle*>& lights,
    int segment_count,
    const float* segment_world_x,
    const float* segment_world_y,
    const float* segment_world_z,
    float normal_x, float normal_y, float normal_z,
    int surface_particle_idx,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    Logosphere::MetalComputeBridge* gpu_bridge,  // GPU compute bridge (Phase II-A integration)
    const std::vector<Logosphere::ShadowTriangle>* gpu_triangles,  // Cached GPU triangles (Phase II-B)
    float* out_lighting)  // Output array [segment_count]
{
    static int debug_count = 0;
    if (debug_count++ < 3) {
        std::cout << "[LIGHTING FUNC] gpu_bridge=" << (gpu_bridge ? "OK" : "NULL")
                  << " gpu_triangles=" << (gpu_triangles ? "OK" : "NULL")
                  << " tri_count=" << (gpu_triangles ? gpu_triangles->size() : 0) << "\n";
    }

    LightingConfig& config = LightingConfig::get();

    // Initialize output
    for (int i = 0; i < segment_count; i++) {
        out_lighting[i] = 0.0f;
    }

    // Early exit if no BVH or no lights
    if (!shadow_bvh || !shadow_bvh->is_ready() || lights.empty()) {
        return;
    }

    // Collect all shadow rays for this segment (pixels × lights)
    std::vector<ShadowRay> shadow_rays;
    shadow_rays.reserve(segment_count * lights.size());

    // Track which (pixel, light) each ray corresponds to
    struct RayMapping {
        int pixel_idx;
        int light_idx;
        float base_intensity;  // Intensity before shadow test
    };
    std::vector<RayMapping> ray_mappings;
    ray_mappings.reserve(segment_count * lights.size());

    // Build shadow rays
    for (int pixel_idx = 0; pixel_idx < segment_count; pixel_idx++) {
        float world_x = segment_world_x[pixel_idx];
        float world_y = segment_world_y[pixel_idx];
        float world_z = segment_world_z[pixel_idx];

        for (size_t light_idx = 0; light_idx < lights.size(); light_idx++) {
            const Particle* light = lights[light_idx];
            if (!light->is_light_source) continue;

            // Check distance
            float dx = light->x - world_x;
            float dy = light->y - world_y;
            float dz = light->z - world_z;
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq > light->emission_radius * light->emission_radius) {
                continue;  // Too far
            }

            // Check normal (backface culling)
            float inv_distance = 1.0f / std::sqrt(dist_sq);
            float light_dir_x = dx * inv_distance;
            float light_dir_y = dy * inv_distance;
            float light_dir_z = dz * inv_distance;
            float dot = normal_x * light_dir_x +
                        normal_y * light_dir_y +
                        normal_z * light_dir_z;

            if (dot <= 0.0f) {
                continue;  // Back-facing
            }

            // Calculate intensity (before shadow test)
            float intensity_lux = config.calculate_intensity_at_distance_sq(
                light->emission_strength, dist_sq
            );
            float intensity = intensity_lux * dot;  // Lambertian

            // Add shadow ray to batch
            ShadowRay ray;
            ray.origin_x = world_x;
            ray.origin_y = world_y;
            ray.origin_z = world_z;
            ray.target_x = light->x;
            ray.target_y = light->y;
            ray.target_z = light->z;
            ray.exclude_particle_id = surface_particle_idx;
            ray.pixel_index = pixel_idx;  // Track which pixel

            shadow_rays.push_back(ray);
            ray_mappings.push_back({pixel_idx, (int)light_idx, intensity});
        }
    }

    // Batch test all shadow rays with SIMD
    if (shadow_rays.empty()) return;

    std::vector<bool> blocked;

    // GPU path (Phase II-A integration) or CPU path
    if constexpr (Optimizations::USE_GPU_SHADOW_RAYS) {
        if (gpu_bridge && gpu_triangles && !gpu_triangles->empty()) {
            // GPU PATH
            static std::atomic<int> gpu_call_count{0};
            int call_num = gpu_call_count.fetch_add(1);

            auto start = std::chrono::high_resolution_clock::now();

            // Convert CPU rays → GPU rays
            std::vector<Logosphere::ShadowRay> gpu_rays;
            convert_to_gpu_rays(shadow_rays, gpu_rays);

            auto convert_end = std::chrono::high_resolution_clock::now();

            // Phase II-B: Use cached GPU triangles (converted once per frame)
            // Call GPU
            std::vector<float> gpu_results(gpu_rays.size());
            gpu_bridge->trace_shadow_rays_batched(
                gpu_rays.data(), static_cast<int>(gpu_rays.size()),
                gpu_triangles->data(), static_cast<int>(gpu_triangles->size()),
                gpu_results.data()
            );

            auto gpu_end = std::chrono::high_resolution_clock::now();

            // Convert GPU results → CPU format
            blocked.resize(gpu_results.size());
            for (size_t i = 0; i < gpu_results.size(); i++) {
                blocked[i] = (gpu_results[i] < 0.5f);  // 0.0 = blocked, 1.0 = lit
            }

            auto finish = std::chrono::high_resolution_clock::now();

            if (call_num < 5) {
                float convert_ms = std::chrono::duration<float, std::milli>(convert_end - start).count();
                float gpu_ms = std::chrono::duration<float, std::milli>(gpu_end - convert_end).count();
                float result_ms = std::chrono::duration<float, std::milli>(finish - gpu_end).count();
                std::cout << "[GPU TIMING] rays=" << gpu_rays.size() << " tris=" << gpu_triangles->size()
                          << " convert=" << convert_ms << "ms gpu=" << gpu_ms << "ms result=" << result_ms << "ms\n";
            }
        } else {
            // CPU FALLBACK
            static int warn_count = 0;
            if (warn_count++ < 3) {
                std::cout << "[CPU FALLBACK] gpu_bridge=" << (gpu_bridge ? "OK" : "NULL")
                          << " gpu_triangles=" << (gpu_triangles ? "OK" : "NULL")
                          << " size=" << (gpu_triangles ? gpu_triangles->size() : 0) << "\n";
            }
            blocked = shadow_bvh->trace_shadow_rays_batch_simd(shadow_rays, particles);
        }
    } else {
        // CPU path (existing)
        blocked = shadow_bvh->trace_shadow_rays_batch_simd(shadow_rays, particles);
    }

    // Map results back to per-pixel lighting
    for (size_t i = 0; i < blocked.size(); i++) {
        if (!blocked[i]) {
            // Ray not blocked - add light contribution
            const RayMapping& mapping = ray_mappings[i];
            out_lighting[mapping.pixel_idx] += mapping.base_intensity;
        }
    }
}

// Single-pass rasterization with immediate shading and inline segment sampling
void SurfaceRasterizer::rasterize_tile_single_pass(
    const TileBin& tile_bin,
    int tile_x, int tile_y,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    CameraSystem& camera_system,
    const std::vector<const Particle*>& lights,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    const std::vector<Logosphere::ShadowTriangle>* gpu_triangles,  // Cached GPU triangles (Phase II-B)
    int render_width, int render_height,
    float cam_x, float cam_y, float cam_z,
    SparseObjectMap* object_map,  // Thread-local object map for mouse interaction
    EngineMetrics* metrics)
{
    // Publish the active camera for calculate_pixel_depth so this path
    // also uses projection-aware depth (see surface_rasterizer.cpp).
    active_camera_system_ = &camera_system;

    // Calculate tile boundaries
    int tile_screen_x = tile_x * tile_size_;
    int tile_screen_y = tile_y * tile_size_;
    int tile_screen_x_end = std::min(tile_screen_x + tile_size_, render_width);
    int tile_screen_y_end = std::min(tile_screen_y + tile_size_, render_height);
    int tile_width = tile_screen_x_end - tile_screen_x;
    int tile_height = tile_screen_y_end - tile_screen_y;

    // Statistics for monitoring
    int rays_tested = 0;

    // Per-tile operation counters (thread-local, no atomics needed)
    uint64_t tile_pixels_edge_tested = 0;
    uint64_t tile_pixels_inside = 0;
    uint64_t tile_pixels_depth_tested = 0;
    uint64_t tile_pixels_depth_passed = 0;


    // Process each surface in this tile
    for (const auto* surf_data_ptr : tile_bin.surfaces) {
        if (!surf_data_ptr) continue;

        const SurfaceData& surf_data = *surf_data_ptr;
        const auto& surface = surf_data.surface;

        // Project triangle
        const float (&vertices)[3][3] = (const float (&)[3][3])surface.vertices;
        auto projected = camera_system.project_triangle(vertices);

        // Process only pixels within this tile
        int y_start = std::max(tile_screen_y, projected.min_y);
        int y_end = std::min(tile_screen_y_end - 1, projected.max_y);

        // Compile-time pattern selection: SCANLINE_1D vs HIERARCHICAL_2D
        if constexpr (Optimizations::SHADOW_SAMPLING_PATTERN == ShadowSampling::Pattern::HIERARCHICAL_2D) {
            // HIERARCHICAL_2D: Process tile in 13×13 blocks
            using Hierarchical2D = ShadowSampling::Strategy<ShadowSampling::Pattern::HIERARCHICAL_2D>;
            constexpr int BLOCK_SIZE = 13;

            // Process tile in BLOCK_SIZE×BLOCK_SIZE blocks
            for (int block_y = y_start; block_y <= y_end; block_y += BLOCK_SIZE) {
                for (int block_x = tile_screen_x; block_x < tile_screen_x_end; block_x += BLOCK_SIZE) {
                    int bx_end = std::min(block_x + BLOCK_SIZE - 1, tile_screen_x_end - 1);
                    int by_end = std::min(block_y + BLOCK_SIZE - 1, y_end);

                    // Collect valid pixels in this block
                    float lighting_grid[BLOCK_SIZE][BLOCK_SIZE];
                    float world_pos_grid[BLOCK_SIZE][BLOCK_SIZE][3];
                    for (int i = 0; i < BLOCK_SIZE; i++) {
                        for (int j = 0; j < BLOCK_SIZE; j++) {
                            lighting_grid[i][j] = -1.0f;  // Invalid marker
                        }
                    }

                    for (int y = block_y; y <= by_end; y++) {
                        for (int x = block_x; x <= bx_end; x++) {
                            tile_pixels_edge_tested++;

                            float u, v;
                            bool pixel_inside = camera_system.pixel_to_triangle_uv(x, y, projected, u, v);

                            if (pixel_inside) {
                                tile_pixels_inside++;
                                tile_pixels_depth_tested++;

                                float depth = calculate_pixel_depth(u, v, vertices, cam_x, cam_y, cam_z);
                                if (depth_buffer.test_and_set(x, y, depth, Optimizations::USE_SQUARED_DEPTH)) {
                                    tile_pixels_depth_passed++;

                                    float w = 1.0f - u - v;
                                    float world_x = vertices[0][0] * w + vertices[1][0] * u + vertices[2][0] * v;
                                    float world_y = vertices[0][1] * w + vertices[1][1] * u + vertices[2][1] * v;
                                    float world_z = vertices[0][2] * w + vertices[1][2] * u + vertices[2][2] * v;

                                    int local_y = y - block_y;
                                    int local_x = x - block_x;
                                    world_pos_grid[local_y][local_x][0] = world_x;
                                    world_pos_grid[local_y][local_x][1] = world_y;
                                    world_pos_grid[local_y][local_x][2] = world_z;
                                    lighting_grid[local_y][local_x] = 0.0f;  // Valid pixel
                                }
                            }
                        }
                    }

                    // Process this 13×13 block
                    Hierarchical2D::process_block_13x13(
                        lighting_grid, world_pos_grid,
                        surface.nx, surface.ny, surface.nz,
                        surf_data.particle_index,
                        lights, particles, shadow_bvh,
                        rays_tested
                    );

                    // Write to framebuffer
                    for (int y = block_y; y <= by_end; y++) {
                        for (int x = block_x; x <= bx_end; x++) {
                            int local_y = y - block_y;
                            int local_x = x - block_x;
                            if (lighting_grid[local_y][local_x] >= 0.0f) {
                                write_lit_pixel(pixel_buffer, x, y,
                                    lighting_grid[local_y][local_x],
                                    surf_data.particle_r, surf_data.particle_g, surf_data.particle_b, surf_data.particle_a,
                                    surf_data.particle_index, object_map);
                            }
                        }
                    }
                }
            }
        } else if constexpr (Optimizations::SHADOW_SAMPLING_PATTERN == ShadowSampling::Pattern::SCANLINE_1D) {
        // Process scanlines for this surface with inline segment sampling
        for (int y = y_start; y <= y_end; ++y) {
            int x_start = std::max(tile_screen_x, projected.min_x);
            int x_end = std::min(tile_screen_x_end - 1, projected.max_x);

            // Fixed-size buffers for one segment (max SEGMENT_SIZE pixels)
            int segment_x[SEGMENT_SIZE];
            float segment_world_x[SEGMENT_SIZE];
            float segment_world_y[SEGMENT_SIZE];
            float segment_world_z[SEGMENT_SIZE];
            int segment_count = 0;

            // Process pixels left to right
            for (int x = x_start; x <= x_end; ++x) {
                // PROFILING: Edge test
                tile_pixels_edge_tested++;

                float u, v;
                bool pixel_inside = camera_system.pixel_to_triangle_uv(x, y, projected, u, v);

                bool pixel_passed = false;
                if (pixel_inside) {
                    // PROFILING: Passed edge test
                    tile_pixels_inside++;

                    // PROFILING: Depth calculation
                    tile_pixels_depth_tested++;

                    // Depth test
                    float depth = calculate_pixel_depth(u, v, vertices, cam_x, cam_y, cam_z);
                    if (depth_buffer.test_and_set(x, y, depth, Optimizations::USE_SQUARED_DEPTH)) {
                        pixel_passed = true;

                        // PROFILING: Passed depth test (pixel drawn)
                        tile_pixels_depth_passed++;

                        // Calculate world position
                        float w = 1.0f - u - v;
                        float world_x = vertices[0][0] * w + vertices[1][0] * u + vertices[2][0] * v;
                        float world_y = vertices[0][1] * w + vertices[1][1] * u + vertices[2][1] * v;
                        float world_z = vertices[0][2] * w + vertices[1][2] * u + vertices[2][2] * v;

                        // Add to current segment
                        segment_x[segment_count] = x;
                        segment_world_x[segment_count] = world_x;
                        segment_world_y[segment_count] = world_y;
                        segment_world_z[segment_count] = world_z;
                        segment_count++;
                    }
                }

                // Process segment when: full, gap detected, or scanline end
                bool segment_full = (segment_count == SEGMENT_SIZE);
                bool scanline_end = (x == x_end);
                bool gap_detected = (segment_count > 0 && !pixel_passed);  // Had pixels, but this one failed

                if (segment_full || scanline_end || gap_detected) {
                    if (segment_count > 0) {  // Only process if we have pixels

                    // Phase II-C: GPU path collects pixels, CPU path processes immediately
                    if constexpr (Optimizations::USE_GPU_SHADOW_RAYS) {
                        // GPU PATH: Just collect pixel data (no lighting yet)
                        for (int i = 0; i < segment_count; i++) {
                            CollectedPixel pixel;
                            pixel.screen_x = segment_x[i];
                            pixel.screen_y = y;
                            pixel.world_x = segment_world_x[i];
                            pixel.world_y = segment_world_y[i];
                            pixel.world_z = segment_world_z[i];
                            pixel.normal_x = surface.nx;
                            pixel.normal_y = surface.ny;
                            pixel.normal_z = surface.nz;
                            pixel.particle_r = surf_data.particle_r;
                            pixel.particle_g = surf_data.particle_g;
                            pixel.particle_b = surf_data.particle_b;
                            pixel.particle_a = surf_data.particle_a;
                            pixel.particle_index = surf_data.particle_index;
                            pixel.lighting = 0.0f;  // Will be filled by GPU

                            // Thread-safe insertion into frame collection
                            {
                                std::lock_guard<std::mutex> lock(frame_pixels_mutex_);
                                current_frame().pixels.push_back(pixel);
                            }
                        }
                    } else {
                        // CPU PATH: Immediate lighting calculation (existing code)

                    // SIMD BATCHING: Test segment endpoints first
                    // Build temporary arrays for endpoint testing
                    float endpoint_world_x[2];
                    float endpoint_world_y[2];
                    float endpoint_world_z[2];
                    float endpoint_lighting[2];
                    int endpoint_count = (segment_count > 1) ? 2 : 1;

                    endpoint_world_x[0] = segment_world_x[0];
                    endpoint_world_y[0] = segment_world_y[0];
                    endpoint_world_z[0] = segment_world_z[0];

                    if (segment_count > 1) {
                        endpoint_world_x[1] = segment_world_x[segment_count-1];
                        endpoint_world_y[1] = segment_world_y[segment_count-1];
                        endpoint_world_z[1] = segment_world_z[segment_count-1];
                    }

                    // Batch test endpoints with SIMD
                    calculate_segment_lighting_batched(
                        lights, endpoint_count,
                        endpoint_world_x, endpoint_world_y, endpoint_world_z,
                        surface.nx, surface.ny, surface.nz,
                        surf_data.particle_index,
                        particles, shadow_bvh,
                        gpu_bridge_,  // Pass GPU bridge (Phase II-A)
                        gpu_triangles,  // Cached GPU triangles (Phase II-B)
                        endpoint_lighting
                    );
                    rays_tested += endpoint_count * lights.size();  // Approximate ray count

                    float first_lighting = endpoint_lighting[0];
                    float last_lighting_seg = (segment_count > 1) ? endpoint_lighting[1] : first_lighting;

                    // Check if segment is uniform
                    bool is_uniform = std::abs(first_lighting - last_lighting_seg) < SHADOW_DIFF_THRESHOLD;

                    if (is_uniform && segment_count > 2) {
                        // Uniform segment: interpolate lighting for all pixels
                        for (int i = 0; i < segment_count; i++) {
                            float t = (segment_count > 1) ? (float)i / (float)(segment_count - 1) : 0.0f;
                            float lighting = first_lighting * (1.0f - t) + last_lighting_seg * t;
                            write_lit_pixel(pixel_buffer, segment_x[i], y, lighting,
                                          surf_data.particle_r, surf_data.particle_g, surf_data.particle_b, surf_data.particle_a,
                                          surf_data.particle_index, object_map);
                        }
                    } else {
                        // Non-uniform segment: Use binary search to find shadow edges
                        // This dramatically reduces rays by only testing transition regions
                        float segment_lighting[SEGMENT_SIZE];

                        if (segment_count <= 2) {
                            // Small segment: use endpoint results directly
                            segment_lighting[0] = first_lighting;
                            if (segment_count > 1) {
                                segment_lighting[1] = last_lighting_seg;
                            }
                        } else {
                            // Larger segment with lighting transition
                            // Set endpoints (already tested)
                            segment_lighting[0] = first_lighting;
                            segment_lighting[segment_count-1] = last_lighting_seg;

                            // Binary search to find shadow edge using strategy pattern
                            // Only tests log2(N) pixels instead of all N pixels!
                            using Scanline1D = ShadowSampling::Strategy<ShadowSampling::Pattern::SCANLINE_1D>;
                            Scanline1D::refine_segment_binary(
                                0, segment_count - 1,  // Start and end indices
                                first_lighting, last_lighting_seg,  // Endpoint lighting
                                segment_lighting,  // Output array
                                segment_world_x, segment_world_y, segment_world_z,
                                surface.nx, surface.ny, surface.nz,
                                surf_data.particle_index,
                                lights, particles, shadow_bvh,
                                rays_tested  // Tracks actual rays used
                            );
                        }

                        for (int i = 0; i < segment_count; i++) {
                            write_lit_pixel(pixel_buffer, segment_x[i], y, segment_lighting[i],
                                          surf_data.particle_r, surf_data.particle_g, surf_data.particle_b, surf_data.particle_a,
                                          surf_data.particle_index, object_map);
                        }
                    }

                    } // end CPU path else block

                    // Reset segment
                    segment_count = 0;
                    }
                }
            }
        }
        } // end if constexpr (SCANLINE_1D)
    }

    // Accumulate statistics for frame-level reporting
    // Aggregate tile counters to global metrics (thread-safe with atomics)
    if (metrics) {
        // Use reinterpret_cast to treat uint64_t as atomic for lock-free adds
        __atomic_fetch_add(&metrics->pixels_edge_tested, tile_pixels_edge_tested, __ATOMIC_RELAXED);
        __atomic_fetch_add(&metrics->pixels_inside, tile_pixels_inside, __ATOMIC_RELAXED);
        __atomic_fetch_add(&metrics->pixels_depth_tested, tile_pixels_depth_tested, __ATOMIC_RELAXED);
        __atomic_fetch_add(&metrics->pixels_depth_passed, tile_pixels_depth_passed, __ATOMIC_RELAXED);

        // Update shadow ray count
        __atomic_fetch_add(reinterpret_cast<uint64_t*>(&metrics->light_ray_count), rays_tested, __ATOMIC_RELAXED);
    }
}
