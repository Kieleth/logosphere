#include "lighting_primitives.h"
#include "core/bvh_frame_counter.h"  // shared BVH-rebuild tick
#include "particle_geometry_v2.h"  // For Surface struct
#include "lighting_config.h"
#include "lighting_metrics.h"  // For profiling
#include "surface_cache.h"  // For shared surface caching
#include "debug_control.h"  // Centralized debug control
#include "optimization_flags.h"  // Centralized optimization flags
#include "logosphere/physics/bvh.h"  // For BVH acceleration
#include "simd_multi_light.h"  // For multi-light SIMD processing
#include "logosphere/rendering/rasterization_math.h"  // For 3D math primitives
#include <cmath>
#include <iostream>
#include <thread>

namespace LightingPrimitives {

// ============================================================================
// GEOMETRY HELPERS
// ============================================================================

void surface_uv_to_world(
    const Surface& surface,
    float u, float v,
    float& out_x, float& out_y, float& out_z) {
    
    // NO TIMING IN HOT PATH - this is called for every pixel!
    // ScopedTimer timer(LightingMetrics::get().uv_to_world_time);
    
    // =========================================================================
    // CLEAN SEPARATION: Let Surface handle its own geometry
    // =========================================================================
    // Lighting doesn't need to understand UV interpolation or vertices.
    // Surface knows its shape and handles the geometry math.
    surface.get_world_position_at_uv(u, v, out_x, out_y, out_z);
}

void get_surface_center(
    const Surface& surface,
    float& out_x, float& out_y, float& out_z) {
    
    // Center is just UV (0.5, 0.5)
    surface.get_world_position_at_uv(0.5f, 0.5f, out_x, out_y, out_z);
}

// ============================================================================
// RAY TRACING
// ============================================================================

// Helper: Ray-triangle intersection using Möller-Trumbore algorithm
// Returns true if ray intersects triangle, and sets t to the distance along ray
bool ray_triangle_intersection(
    float ray_origin_x, float ray_origin_y, float ray_origin_z,
    float ray_dir_x, float ray_dir_y, float ray_dir_z,
    const float v0[3], const float v1[3], const float v2[3],
    float& out_t) {
    
    const float EPSILON = Optimizations::RAY_TRIANGLE_EPSILON;
    
    // Edge vectors
    float edge1_x = v1[0] - v0[0];
    float edge1_y = v1[1] - v0[1];
    float edge1_z = v1[2] - v0[2];
    
    float edge2_x = v2[0] - v0[0];
    float edge2_y = v2[1] - v0[1];
    float edge2_z = v2[2] - v0[2];
    
    // Cross product of ray direction and edge2
    float h_x = ray_dir_y * edge2_z - ray_dir_z * edge2_y;
    float h_y = ray_dir_z * edge2_x - ray_dir_x * edge2_z;
    float h_z = ray_dir_x * edge2_y - ray_dir_y * edge2_x;
    
    // Dot product of edge1 and h
    float a = edge1_x * h_x + edge1_y * h_y + edge1_z * h_z;
    
    // Ray is parallel to triangle
    if (a > -EPSILON && a < EPSILON) {
        return false;
    }
    
    float f = 1.0f / a;
    
    // Vector from v0 to ray origin
    float s_x = ray_origin_x - v0[0];
    float s_y = ray_origin_y - v0[1];
    float s_z = ray_origin_z - v0[2];
    
    // u parameter (barycentric coordinate)
    float u = f * (s_x * h_x + s_y * h_y + s_z * h_z);
    // Check barycentric bounds without epsilon for debugging
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    
    // Cross product of s and edge1
    float q_x = s_y * edge1_z - s_z * edge1_y;
    float q_y = s_z * edge1_x - s_x * edge1_z;
    float q_z = s_x * edge1_y - s_y * edge1_x;
    
    // v parameter (barycentric coordinate)
    float v = f * (ray_dir_x * q_x + ray_dir_y * q_y + ray_dir_z * q_z);
    // Check barycentric bounds without epsilon for debugging
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    
    // Calculate t (distance along ray)
    float t = f * (edge2_x * q_x + edge2_y * q_y + edge2_z * q_z);
    
    if (t > EPSILON) {
        out_t = t;
        return true;
    }
    
    return false;
}

// Helper: Ray-surface intersection (triangles only now!)
bool ray_quad_intersection(
    float ray_origin_x, float ray_origin_y, float ray_origin_z,
    float ray_dir_x, float ray_dir_y, float ray_dir_z,
    const Surface& surface,
    float& out_t) {
    
    // Surfaces are now triangles only (vertex_count == 3)
    // No more quads after triangle conversion
    
    if (surface.vertex_count < 3) {
        return false;  // Need at least 3 vertices for a triangle
    }
    
#ifdef FORCE_DEBUG_MODE
    // DEBUG: Check if vertices are actually populated
    static int debug_call = 0;
    if (++debug_call <= 20) {
        bool all_zero = true;
        for (int i = 0; i < surface.vertex_count; i++) {
            if (surface.vertices[i][0] != 0 || surface.vertices[i][1] != 0 || surface.vertices[i][2] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            PERF_DEBUG_LOG("[QUAD DEBUG] WARNING: All vertices are zero!");
        }
    }
    
    // DEBUG: Print triangle vertices for first few calls
    if (debug_call <= 10) {
        PERF_DEBUG_PRINT("[QUAD DEBUG] Testing triangle 1: (%f,%f,%f) (%f,%f,%f) (%f,%f,%f)\n",
                         surface.vertices[0][0], surface.vertices[0][1], surface.vertices[0][2],
                         surface.vertices[1][0], surface.vertices[1][1], surface.vertices[1][2],
                         surface.vertices[2][0], surface.vertices[2][1], surface.vertices[2][2]);
    }
#endif
    
    // Test triangles in the surface
    // For quads (4 vertices), test as two triangles: (0,1,2) and (0,2,3)
    // For triangles (3 vertices), test as single triangle: (0,1,2)
    
    float t;
    
    // First triangle: vertices 0, 1, 2
    bool hit = ray_triangle_intersection(
        ray_origin_x, ray_origin_y, ray_origin_z,
        ray_dir_x, ray_dir_y, ray_dir_z,
        surface.vertices[0], surface.vertices[1], surface.vertices[2],
        t
    );
    
    if (hit) {
        out_t = t;
        return true;
    }
    
    // If we have 4 vertices (quad), test second triangle: 0, 2, 3
    if (surface.vertex_count == 4) {
        hit = ray_triangle_intersection(
            ray_origin_x, ray_origin_y, ray_origin_z,
            ray_dir_x, ray_dir_y, ray_dir_z,
            surface.vertices[0], surface.vertices[2], surface.vertices[3],
            t
        );
        
        if (hit) {
            out_t = t;
            return true;
        }
    }
    
    return false;
}

bool is_ray_blocked(
    float from_x, float from_y, float from_z,
    float to_x, float to_y, float to_z,
    const std::vector<Particle>& particles,
    int skip_particle_idx) {
    
    // Note: Profiling is done in is_ray_blocked_bvh if called from there
    // Only profile if called directly (not from BVH wrapper)
    
    // Track that we're using linear search
    LightingMetrics::get().linear_rays_traced++;
    
    COUNT_SHADOW_RAY();
    
    // Calculate ray direction (not normalized)
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float dz = to_z - from_z;
    float max_distance = RasterizationMath::distance_3d(dx, dy, dz);
    
    // Normalize ray direction
    float ray_dir_x = dx / max_distance;
    float ray_dir_y = dy / max_distance;
    float ray_dir_z = dz / max_distance;
    
#ifdef FORCE_DEBUG_MODE
    // DEBUG: Track Eden's shadow rays periodically
    static int debug_counter = 0;
    static int frame_counter = 0;
    debug_counter++;
    if (debug_counter > 1000) {
        debug_counter = 0;
        frame_counter++;
    }
    
    // Debug every 20th "frame" (1000 rays), first 3 rays involving cube
    bool debug_this = false;
    if (frame_counter % 20 == 0 && debug_counter <= 3) {
        // Check if ray involves cube's south face area
        if (skip_particle_idx == 4 || (from_y > 4.5f && from_y < 5.5f)) {
            debug_this = true;
        }
    }
    
    if (debug_this) {
        PERF_DEBUG_PRINT("\n[RAY DEBUG #%d] Checking ray from (%f,%f,%f) to (%f,%f,%f)\n",
                         debug_counter, from_x, from_y, from_z, to_x, to_y, to_z);
        PERF_DEBUG_PRINT("  Distance: %f, Particles to check: %zu, Skip particle: %d\n",
                         max_distance, particles.size(), skip_particle_idx);
    }
#else
    bool debug_this = false;  // Always false in production
#endif
    
    // Feature flag for testing/emergency fallback (default ON for performance)
    // Using centralized optimization flag
    
    
    // Check each particle's surfaces for potential blocking
    for (size_t i = 0; i < particles.size(); i++) {
        const Particle& blocker = particles[i];
        
        // Skip the particle that contains the ray origin
        if ((int)i == skip_particle_idx) continue;
        
        // Light sources don't cast shadows
        if (blocker.is_light_source) continue;
        
        // =========================================================================
        // OPTIMIZATION: Distance-based culling
        // Skip particles that are too far from the ray to possibly intersect
        // A particle can only block if it's within (radius + ray_length) distance
        // =========================================================================
        if (Optimizations::USE_DISTANCE_CULLING) {
            float particle_radius = blocker.size * 0.866f; // Sphere around cube (sqrt(3)/2)
            float dx = blocker.x - from_x;
            float dy = blocker.y - from_y;
            float dz = blocker.z - from_z;
            float dist_squared = dx*dx + dy*dy + dz*dz;
            float max_dist_squared = (max_distance + particle_radius) * (max_distance + particle_radius);
            if (dist_squared > max_dist_squared) continue;  // Too far to block
        }
        
        // =========================================================================
        // OPTIMIZATION: SHADOW RAY SURFACE CACHING
        // WHY: GetSurfaces() was called for every shadow ray (30ms overhead!)
        // HOW: Use shared surface cache instead of regenerating
        // IMPACT: Should eliminate most of the 30ms shadow overhead
        // =========================================================================
        const auto& surfaces = Optimizations::USE_SURFACE_CACHE
            ? SurfaceCache::get().get_surfaces(static_cast<int>(i), blocker)
            : blocker.GetSurfaces();
        
#ifdef FORCE_DEBUG_MODE
        if (debug_this && surfaces.size() > 0) {
            PERF_DEBUG_PRINT("[RAY DEBUG] Checking %zu surfaces for particle %zu\n", surfaces.size(), i);
            // Check first surface vertices
            if (surfaces.size() > 0) {
                const Surface& first = surfaces[0];
                PERF_DEBUG_PRINT("  First surface has %d vertices\n", first.vertex_count);
                if (first.vertex_count > 0) {
                    PERF_DEBUG_PRINT("    Vertex 0: (%f, %f, %f)\n",
                                     first.vertices[0][0], first.vertices[0][1], first.vertices[0][2]);
                }
            }
        }
#endif
        
        
        // Check each surface for ray intersection
        for (const Surface& surface : surfaces) {
            // Check that surface has valid vertices
            if (surface.vertex_count < 3) {
#ifdef FORCE_DEBUG_MODE
                if (debug_this) {
                    PERF_DEBUG_PRINT("[RAY DEBUG] Surface has only %d vertices, skipping\n", surface.vertex_count);
                }
#endif
                continue;
            }
            
#ifdef FORCE_DEBUG_MODE
            // Debug: Check if vertices are actually set (not all zeros)
            if (debug_this) {
                bool all_zero = true;
                for (int v = 0; v < surface.vertex_count; v++) {
                    if (surface.vertices[v][0] != 0 || surface.vertices[v][1] != 0 || surface.vertices[v][2] != 0) {
                        all_zero = false;
                        break;
                    }
                }
                if (all_zero) {
                    PERF_DEBUG_LOG("[RAY DEBUG] WARNING: All vertices are at origin!");
                }
            }
#endif
            
            float t;
            bool hit = ray_quad_intersection(
                from_x, from_y, from_z,
                ray_dir_x, ray_dir_y, ray_dir_z,
                surface,
                t
            );
            
#ifdef FORCE_DEBUG_MODE
            if (debug_this) {
                if (hit) {
                    PERF_DEBUG_PRINT("[RAY DEBUG] Hit surface at t=%f (max_distance=%f)\n", t, max_distance);
                    PERF_DEBUG_PRINT("  Checking if %f > 0.001 && %f < %f\n", t, t, (max_distance - 0.001f));
                    float hit_x = from_x + t * ray_dir_x;
                    float hit_y = from_y + t * ray_dir_y;
                    float hit_z = from_z + t * ray_dir_z;
                    PERF_DEBUG_PRINT("  Hit point: (%f, %f, %f)\n", hit_x, hit_y, hit_z);
                }
            }
#endif
            
            // Check if intersection is within our ray segment
            if (hit && t > Optimizations::RAY_DISTANCE_EPSILON && t < max_distance - Optimizations::RAY_DISTANCE_EPSILON) {
#ifdef FORCE_DEBUG_MODE
                if (debug_this) {
                    PERF_DEBUG_LOG("[RAY DEBUG] Ray BLOCKED by surface!");
                }
#endif
                // Ray hits this surface before reaching the light!
                COUNT_SHADOW_BLOCKED();
                return true;
            }
        }
    }
    
    COUNT_SHADOW_CLEAR();
    return false;  // Clear line of sight
}

// BVH-accelerated version of is_ray_blocked
// Thread-local BVH storage to avoid cache thrashing in parallel rendering
// Each thread gets its own copy of the BVH structure (only ~5KB for 77 nodes)
//
// WHY WE NEED A FRAME COUNTER:
// The BVH object pointer stays the same even when rebuilt (it's a member of ParticleSystem).
// When particles move (like Eva), the BVH is rebuilt in-place with new data, but threads
// can't detect this change by comparing pointers. Without a signal, threads keep using their
// stale clone from frame 1, causing moving particles to not cast shadows properly.
//
// THE SOLUTION:
// 1. Main thread rebuilds BVH → increments frame counter (signal: "BVH has changed!")
// 2. Worker threads check: "Is my clone from frame N-1 but we're now on frame N?"
// 3. If yes → clone the fresh BVH with updated particle positions
// 4. If no → keep using cached clone (saves cloning overhead)
//
// EFFICIENCY:
// - WITHOUT frame counter: Would need to clone on every shadow ray = 500MB copying/frame!
// - WITH frame counter: Clone once per thread per frame = 20KB total (4 threads × 5KB)
struct ThreadLocalBVH {
    BVH local_bvh;
    const BVH* source_bvh = nullptr;
    uint64_t last_frame = UINT64_MAX;  // Initialize to invalid value to force first clone
    
    // Get or create thread-local copy for this frame
    const BVH* get_or_clone(const BVH* source, uint64_t frame_number) {
        if (!source || !source->is_ready()) {
            return source;  // Return original if not ready
        }
        
        // Clone once per frame (BVH is rebuilt each frame when particles move)
        if (frame_number != last_frame || source != source_bvh) {
            std::cout << "[THREAD_BVH] Thread " << std::this_thread::get_id() 
                      << " cloning BVH (frame " << frame_number << " vs last " << last_frame << ")" << std::endl;
            local_bvh = source->clone();
            source_bvh = source;
            last_frame = frame_number;
        }
        
        return &local_bvh;
    }
};

// Thread-local storage - each thread gets its own instance
static thread_local ThreadLocalBVH thread_bvh;

// BVH frame counter relocated to logosphere/core/bvh_frame_counter.{h,cpp}
// so headless test harnesses + ParticleSystem can resolve the symbol
// without dragging the full LightingPrimitives translation unit.
void increment_bvh_frame_counter() {
    logosphere::bvh_frame::increment();
}

bool is_ray_blocked_bvh(
    float from_x, float from_y, float from_z,
    float to_x, float to_y, float to_z,
    const std::vector<Particle>& particles,
    const BVH* bvh,
    int skip_particle_idx) {
    
    // Note: No COUNT_SHADOW_RAY() here to avoid double-counting with batch system
    // Shadow ray counting is done at batch level in test_shadow_rays_batch_simd()
    
    // DEBUG: Track shadow ray tests
    static int shadow_debug_count = 0;
    bool debug_shadow = (shadow_debug_count++ < 50);
    
    if (debug_shadow) {
        printf("[IS_RAY_BLOCKED_BVH] Called, BVH=%p, ready=%d, USE_BVH=%d\n", 
               bvh, bvh ? bvh->is_ready() : 0, Optimizations::USE_BVH);
    }
    
    // If BVH is available and built, use it
    if (Optimizations::USE_BVH && bvh && bvh->is_ready()) {
        LightingMetrics::get().bvh_enabled = true;
        
        // Use thread-local copy if parallel tiles are enabled
        // Each thread clones the BVH once per frame to avoid cache contention
        const BVH* bvh_to_use = bvh;
        if (Optimizations::USE_PARALLEL_TILES) {
            uint64_t current_frame = logosphere::bvh_frame::read();
            bvh_to_use = thread_bvh.get_or_clone(bvh, current_frame);
        }
        
        bool result = bvh_to_use->trace_shadow_ray(from_x, from_y, from_z,
                                     to_x, to_y, to_z,
                                     particles,
                                     skip_particle_idx);
        
        if (debug_shadow) {
            printf("[SHADOW RAY] From (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f), skip %d -> %s\n",
                   from_x, from_y, from_z, to_x, to_y, to_z, skip_particle_idx,
                   result ? "BLOCKED" : "CLEAR");
        }
        
        if (result) {
            COUNT_SHADOW_BLOCKED();
        } else {
            COUNT_SHADOW_CLEAR();
        }
        return result;
    }
    
    // Fall back to linear search
    return is_ray_blocked(from_x, from_y, from_z,
                         to_x, to_y, to_z,
                         particles,
                         skip_particle_idx);
}

// ============================================================================
// LIGHTING CALCULATIONS
// ============================================================================

float calculate_light_contribution(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const Particle& light) {
    
    // Granular profiling - only active during sampling frames
    PROFILE_LIGHT_CALC();
    
    // DEBUG: Track why lighting fails
    static int debug_counter = 0;
    bool debug_this = (debug_counter++ < 20);  // Debug first 20 calls
    
    // Light sources only
    if (!light.is_light_source) {
        if (debug_this) {
            printf("[LIGHT DEBUG] Not a light source\n");
        }
        return 0.0f;
    }
    
    // Calculate distance from point to light
    float dx = light.x - point_x;
    float dy = light.y - point_y;
    float dz = light.z - point_z;
    float distance = RasterizationMath::distance_3d(dx, dy, dz);
    
    if (debug_this) {
        printf("[LIGHT DEBUG] Point (%.1f,%.1f,%.1f) -> Light (%.1f,%.1f,%.1f)\n",
               point_x, point_y, point_z, light.x, light.y, light.z);
        printf("  Distance: %.2f, Emission radius: %.2f\n", 
               distance, light.emission_radius);
    }
    
    // Check if point is within light's emission radius
    if (distance > light.emission_radius) {
        if (debug_this) {
            printf("  REJECTED: Too far (%.2f > %.2f)\n", distance, light.emission_radius);
        }
        return 0.0f;  // Too far away
    }
    
    // Normalize light direction
    float light_dir_x = dx / distance;
    float light_dir_y = dy / distance;
    float light_dir_z = dz / distance;
    
    // Calculate dot product with surface normal (Lambert's cosine law)
    float dot = normal_x * light_dir_x + 
                normal_y * light_dir_y + 
                normal_z * light_dir_z;
    
    if (debug_this) {
        printf("  Normal: (%.2f,%.2f,%.2f), Light dir: (%.2f,%.2f,%.2f)\n",
               normal_x, normal_y, normal_z, light_dir_x, light_dir_y, light_dir_z);
        printf("  Dot product: %.3f\n", dot);
    }
    
    // Surface faces away from light?
    if (dot <= 0.0f) {
        if (debug_this) {
            printf("  REJECTED: Surface faces away (dot=%.3f)\n", dot);
        }
        return 0.0f;
    }
    
    // Calculate intensity using inverse square law
    LightingConfig& config = LightingConfig::get();
    float intensity_lux = config.calculate_intensity_at_distance(
        light.emission_strength, distance);
    
    // Apply Lambert's cosine law
    intensity_lux *= dot;
    
    if (debug_this) {
        printf("  SUCCESS: Intensity = %.2f lux (strength=%.1f)\n", 
               intensity_lux, light.emission_strength);
    }
    
    return intensity_lux;
}

float calculate_point_lighting(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const std::vector<Particle>& particles,
    const std::vector<int>& light_indices,
    int surface_particle_idx) {
    
    // static int call_count = 0;
    // if (++call_count <= 5) {
    //     std::cout << "[calculate_point_lighting #" << call_count << "] Called for point (" 
    //               << point_x << "," << point_y << "," << point_z 
    //               << ") with " << light_indices.size() << " lights" << std::endl;
    // }
    
    float total_intensity = 0.0f;
    
    // Debug output for SIMD activation
    static bool first_call = true;
    if (first_call) {
        std::cout << "[SIMD Debug] Light count: " << light_indices.size() 
                  << ", USE_SIMD: " << Optimizations::USE_SIMD 
                  << ", Will use SIMD: " << (light_indices.size() >= 4 && Optimizations::USE_SIMD) 
                  << std::endl;
        first_call = false;
    }
    
    // SIMD Multi-light optimization with proper shadow testing
    // Process 4 lights simultaneously using SIMD for distance/dot calculations
    // Then test shadows for visible lights only
    if (light_indices.size() >= 4 && Optimizations::USE_SIMD) {
        // Process lights in batches of 4
        for (size_t i = 0; i < light_indices.size(); i += 4) {
            int batch_size = std::min(4, (int)(light_indices.size() - i));
            
            // Prepare batch data
            float light_x[4] = {0}, light_y[4] = {0}, light_z[4] = {0};
            float emission_strength[4] = {0}, emission_radius[4] = {0};
            int light_idx_array[4] = {-1, -1, -1, -1};
            
            for (int j = 0; j < batch_size; j++) {
                int idx = light_indices[i + j];
                if (idx >= 0 && idx < particles.size() && particles[idx].is_light_source) {
                    const Particle& light = particles[idx];
                    light_x[j] = light.x;
                    light_y[j] = light.y;
                    light_z[j] = light.z;
                    emission_strength[j] = light.emission_strength;
                    emission_radius[j] = light.emission_radius;
                    light_idx_array[j] = idx;
                }
            }
            
            // Calculate contributions for 4 lights at once (without shadows)
            float contributions[4];
            SIMD::calculate_light_batch(
                point_x, point_y, point_z,
                normal_x, normal_y, normal_z,
                light_x, light_y, light_z,
                emission_strength, emission_radius,
                contributions
            );
            
            // Now test shadows only for lights that can contribute
            for (int j = 0; j < batch_size; j++) {
                if (contributions[j] > 0.0f && light_idx_array[j] >= 0) {
                    const Particle& light = particles[light_idx_array[j]];
                    
                    // Check shadow
                    bool blocked = is_ray_blocked(
                        point_x, point_y, point_z,
                        light.x, light.y, light.z,
                        particles,
                        surface_particle_idx
                    );
                    
                    if (!blocked) {
                        total_intensity += contributions[j];
                    }
                }
            }
        }
        
        return total_intensity;
    }
    
    // Fallback to scalar processing for <4 lights
    // Check contribution from each light source
    for (int light_idx : light_indices) {
        if (light_idx < 0 || light_idx >= particles.size()) continue;
        
        const Particle& light = particles[light_idx];
        if (!light.is_light_source) continue;  // Sanity check
        
        // Calculate potential contribution from this light
        float contribution = calculate_light_contribution(
            point_x, point_y, point_z,
            normal_x, normal_y, normal_z,
            light
        );
        
        // If light can potentially contribute, check for shadows
        if (contribution > 0.0f) {
            // Check if anything blocks the path from point to light
            bool blocked = is_ray_blocked(
                point_x, point_y, point_z,
                light.x, light.y, light.z,
                particles,
                surface_particle_idx
            );
            
            if (!blocked) {
                // Light reaches this point! Add its contribution.
                total_intensity += contribution;
            }
        }
    }
    
    return total_intensity;
}

// BVH-accelerated version of calculate_point_lighting
float calculate_point_lighting_bvh(
    float point_x, float point_y, float point_z,
    float normal_x, float normal_y, float normal_z,
    const std::vector<Particle>& particles,
    const std::vector<int>& light_indices,
    const BVH* bvh,
    int surface_particle_idx) {
    
    float total_intensity = 0.0f;
    
    // Debug output for SIMD activation
    static bool first_call = true;
    if (first_call) {
        std::cout << "[SIMD Debug BVH] Light count: " << light_indices.size() 
                  << ", USE_SIMD: " << Optimizations::USE_SIMD 
                  << ", Will use SIMD: " << (light_indices.size() >= 4 && Optimizations::USE_SIMD) 
                  << std::endl;
        first_call = false;
    }
    
    // DEBUG: Track first few calls
    static int debug_count = 0;
    bool debug_this = (debug_count++ < 50);
    if (debug_this) {
        printf("[BVH LIGHTING] Point (%.1f,%.1f,%.1f) Normal (%.2f,%.2f,%.2f)\n",
               point_x, point_y, point_z, normal_x, normal_y, normal_z);
        printf("  Light indices: ");
        for (int idx : light_indices) {
            printf("%d ", idx);
        }
        printf("\n  Particle %d, BVH %s\n", surface_particle_idx, bvh ? "available" : "null");
        printf("  SIMD path: %s (size=%zu, USE_SIMD=%d)\n",
               (light_indices.size() >= 4 && Optimizations::USE_SIMD) ? "YES" : "NO",
               light_indices.size(), Optimizations::USE_SIMD);
    }
    
    // SIMD Multi-light optimization with BVH shadow testing
    if (light_indices.size() >= 4 && Optimizations::USE_SIMD) {
        // Process lights in batches of 4
        for (size_t i = 0; i < light_indices.size(); i += 4) {
            int batch_size = std::min(4, (int)(light_indices.size() - i));
            
            // Prepare batch data
            float light_x[4] = {0}, light_y[4] = {0}, light_z[4] = {0};
            float emission_strength[4] = {0}, emission_radius[4] = {0};
            int light_idx_array[4] = {-1, -1, -1, -1};
            
            for (int j = 0; j < batch_size; j++) {
                int idx = light_indices[i + j];
                if (idx >= 0 && idx < particles.size() && particles[idx].is_light_source) {
                    const Particle& light = particles[idx];
                    light_x[j] = light.x;
                    light_y[j] = light.y;
                    light_z[j] = light.z;
                    emission_strength[j] = light.emission_strength;
                    emission_radius[j] = light.emission_radius;
                    light_idx_array[j] = idx;
                }
            }
            
            // Calculate contributions for 4 lights at once (without shadows)
            float contributions[4];
            SIMD::calculate_light_batch(
                point_x, point_y, point_z,
                normal_x, normal_y, normal_z,
                light_x, light_y, light_z,
                emission_strength, emission_radius,
                contributions
            );
            
            // Now test shadows only for lights that can contribute
            for (int j = 0; j < batch_size; j++) {
                if (contributions[j] > 0.0f && light_idx_array[j] >= 0) {
                    const Particle& light = particles[light_idx_array[j]];
                    
                    // Check shadow with BVH
                    bool blocked = is_ray_blocked_bvh(
                        point_x, point_y, point_z,
                        light.x, light.y, light.z,
                        particles,
                        bvh,
                        surface_particle_idx
                    );
                    
                    if (!blocked) {
                        total_intensity += contributions[j];
                    }
                }
            }
        }
        
        return total_intensity;
    }
    
    // Fallback to scalar processing for <4 lights
    // Check contribution from each light source
    for (int light_idx : light_indices) {
        if (light_idx < 0 || light_idx >= particles.size()) continue;
        
        const Particle& light = particles[light_idx];
        if (!light.is_light_source) continue;  // Sanity check
        
        // Calculate potential contribution from this light
        float contribution = calculate_light_contribution(
            point_x, point_y, point_z,
            normal_x, normal_y, normal_z,
            light
        );
        
        
        // If light can potentially contribute, check for shadows using BVH
        if (contribution > 0.0f) {
            // Use BVH-accelerated shadow test
            bool blocked = is_ray_blocked_bvh(
                point_x, point_y, point_z,
                light.x, light.y, light.z,
                particles,
                bvh,
                surface_particle_idx
            );
            
            
            if (!blocked) {
                total_intensity += contribution;
                if (debug_this) {
                    printf("  Light %d contributes %.2f lux (not blocked)\n", light_idx, contribution);
                }
            } else if (debug_this) {
                printf("  Light %d blocked (would be %.2f lux)\n", light_idx, contribution);
            }
        } else if (debug_this) {
            printf("  Light %d no contribution (angle/distance)\n", light_idx);
        }
    }
    
    if (debug_this) {
        printf("  TOTAL: %.2f lux\n", total_intensity);
    }
    
    return total_intensity;
}

// Simple batch wrapper for shadow ray testing (Phase 0)
std::vector<bool> test_shadow_rays_batch(
    const std::vector<float>& from_x,
    const std::vector<float>& from_y,
    const std::vector<float>& from_z,
    const std::vector<float>& to_x,
    const std::vector<float>& to_y,
    const std::vector<float>& to_z,
    const std::vector<int>& skip_particle_ids,
    const std::vector<Particle>& particles,
    const BVH* bvh) {
    
    // Note: No PROFILE_SHADOW_TEST here - this is called as fallback from SIMD version
    // which already has profiling active. Prevents nested timing double-counting.
    
    size_t ray_count = from_x.size();
    std::vector<bool> results(ray_count);
    
    // Simple implementation: just call individual ray tests
    // This reduces function call overhead compared to calling from shader
    for (size_t i = 0; i < ray_count; i++) {
        results[i] = is_ray_blocked_bvh(
            from_x[i], from_y[i], from_z[i],
            to_x[i], to_y[i], to_z[i],
            particles, bvh, skip_particle_ids[i]
        );
    }
    
    return results;
}

// SIMD version of batch shadow ray testing
std::vector<bool> test_shadow_rays_batch_simd(
    const std::vector<float>& from_x,
    const std::vector<float>& from_y,
    const std::vector<float>& from_z,
    const std::vector<float>& to_x,
    const std::vector<float>& to_y,
    const std::vector<float>& to_z,
    const std::vector<int>& skip_particle_ids,
    const std::vector<Particle>& particles,
    const BVH* bvh) {
    
    // NO TIMING IN HOT PATH - Use statistical sampling instead
    // PROFILE_SHADOW_TEST(); // This would time EVERY batch - too much overhead!
    
    // Debug: Log SIMD activation
    static int simd_call_count = 0;
    if (simd_call_count < 3) {
        std::cout << "[SIMD DEBUG] test_shadow_rays_batch_simd called with " 
                  << from_x.size() << " rays, BVH built: " 
                  << (bvh && bvh->is_bvh_built() ? "YES" : "NO") << std::endl;
        simd_call_count++;
    }
    
    // Check if SIMD is enabled and BVH is available
    if (!Optimizations::USE_SIMD_RAY_BATCHING || !bvh || !bvh->is_bvh_built()) {
        std::cout << "[SIMD DEBUG] Falling back to scalar - SIMD:" 
                  << Optimizations::USE_SIMD_RAY_BATCHING 
                  << " BVH:" << (bvh ? "OK" : "NULL") 
                  << " Built:" << (bvh && bvh->is_bvh_built() ? "YES" : "NO") << std::endl;
        // Fallback to scalar version
        return test_shadow_rays_batch(from_x, from_y, from_z, to_x, to_y, to_z,
                                     skip_particle_ids, particles, bvh);
    }
    
    // Convert to ShadowRay format for BVH SIMD interface
    std::vector<ShadowRay> shadow_rays;
    shadow_rays.reserve(from_x.size());
    
    for (size_t i = 0; i < from_x.size(); ++i) {
        shadow_rays.push_back({
            from_x[i], from_y[i], from_z[i],           // Origin
            to_x[i], to_y[i], to_z[i],                 // Target
            skip_particle_ids[i],                      // Exclude ID
            static_cast<int>(i)                        // Pixel index
        });
    }
    
    // Use BVH SIMD batch processing
    return bvh->trace_shadow_rays_batch_simd(shadow_rays, particles);
}

} // namespace LightingPrimitives