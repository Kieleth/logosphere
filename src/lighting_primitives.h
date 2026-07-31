#ifndef LIGHTING_PRIMITIVES_H
#define LIGHTING_PRIMITIVES_H

#include <vector>
#include <deque>
#include "particle.h"

// Forward declaration for BVH integration
class BVH;

// LightingPrimitives - Reusable building blocks for lighting calculations
//
// These are the core algorithms extracted from SimplePixelToLight that can be
// reused by any lighting strategy. They handle the physics and math of lighting
// without any assumptions about how often they're called (per-surface vs per-pixel).
//
// Design philosophy:
// - Each function does ONE thing well
// - No side effects, pure functions where possible
// - Clear inputs and outputs
// - Reusable across different lighting strategies

namespace LightingPrimitives {
    
    // Signal to worker threads that BVH has been rebuilt
    // Call this after update_bvh() to ensure threads refresh their local copies
    void increment_bvh_frame_counter();
    
    // ============================================================================
    // GEOMETRY HELPERS - Convert between coordinate systems
    // ============================================================================
    
    // Calculate the 3D world position from surface UV coordinates
    // UV coordinates are [0,1] where (0,0) is bottom-left, (1,1) is top-right
    // Uses bilinear interpolation between the 4 corners
    void surface_uv_to_world(
        const Surface& surface,
        float u, float v,
        float& out_x, float& out_y, float& out_z
    );
    
    // Get the center point of a surface (convenience function)
    // Equivalent to surface_uv_to_world with u=0.5, v=0.5
    void get_surface_center(
        const Surface& surface,
        float& out_x, float& out_y, float& out_z
    );
    
    // ============================================================================
    // RAY INTERSECTION PRIMITIVES - Low-level geometric tests
    // ============================================================================

    // Ray-triangle intersection using Möller-Trumbore algorithm
    // Returns true if ray intersects triangle, with distance in out_t
    // out_t: Distance along ray to intersection point (ray_origin + t * ray_dir)
    bool ray_triangle_intersection(
        float ray_origin_x, float ray_origin_y, float ray_origin_z,
        float ray_dir_x, float ray_dir_y, float ray_dir_z,
        const float v0[3], const float v1[3], const float v2[3],
        float& out_t
    );

    // Ray-surface intersection (handles triangles and quads)
    // Surfaces with 3 vertices: tests single triangle
    // Surfaces with 4 vertices: tests as two triangles (0,1,2) and (0,2,3)
    // Returns true if ray intersects, with distance in out_t
    bool ray_quad_intersection(
        float ray_origin_x, float ray_origin_y, float ray_origin_z,
        float ray_dir_x, float ray_dir_y, float ray_dir_z,
        const Surface& surface,
        float& out_t
    );

    // ============================================================================
    // RAY TRACING - Check if light can reach a point
    // ============================================================================

    // Check if a ray from point A to point B is blocked by any particle
    // Returns true if blocked, false if clear line of sight
    // skip_particle_idx: Don't check this particle (typically the one containing point A)
    bool is_ray_blocked(
        float from_x, float from_y, float from_z,  // Ray origin
        float to_x, float to_y, float to_z,        // Ray destination
        const std::vector<Particle>& particles,
        int skip_particle_idx = -1                 // Particle to skip (can't block itself)
    );
    
    // BVH-accelerated version of is_ray_blocked
    // Uses Bounding Volume Hierarchy for O(log N) ray testing
    // Falls back to linear search if BVH is null or not built
    bool is_ray_blocked_bvh(
        float from_x, float from_y, float from_z,  // Ray origin
        float to_x, float to_y, float to_z,        // Ray destination
        const std::vector<Particle>& particles,
        const BVH* bvh,                            // BVH acceleration structure (optional)
        int skip_particle_idx = -1                 // Particle to skip
    );
    
    // Simple batch wrapper for shadow ray testing (Phase 0)
    // Collects multiple shadow rays and processes them as a batch
    std::vector<bool> test_shadow_rays_batch(
        const std::vector<float>& from_x,          // Ray origins X
        const std::vector<float>& from_y,          // Ray origins Y  
        const std::vector<float>& from_z,          // Ray origins Z
        const std::vector<float>& to_x,            // Ray targets X
        const std::vector<float>& to_y,            // Ray targets Y
        const std::vector<float>& to_z,            // Ray targets Z
        const std::vector<int>& skip_particle_ids, // Particles to skip
        const std::vector<Particle>& particles,
        const BVH* bvh
    );
    
    // SIMD batch wrapper for shadow ray testing 
    // ⚠️  Requires USE_SIMD_RAY_BATCHING flag enabled
    // Processes rays 4 at a time using SIMD instructions
    std::vector<bool> test_shadow_rays_batch_simd(
        const std::vector<float>& from_x,          // Ray origins X
        const std::vector<float>& from_y,          // Ray origins Y  
        const std::vector<float>& from_z,          // Ray origins Z
        const std::vector<float>& to_x,            // Ray targets X
        const std::vector<float>& to_y,            // Ray targets Y
        const std::vector<float>& to_z,            // Ray targets Z
        const std::vector<int>& skip_particle_ids, // Particles to skip
        const std::vector<Particle>& particles,
        const BVH* bvh
    );
    
    // ============================================================================
    // LIGHTING CALCULATIONS - Physics of light
    // ============================================================================
    
    // Calculate how much light reaches a point from a single light source
    // Takes into account:
    // - Distance (inverse square law)
    // - Surface normal (Lambert's cosine law)
    // - Light emission radius
    // Returns intensity in lux, or 0 if light can't reach the point
    float calculate_light_contribution(
        float point_x, float point_y, float point_z,        // Point being lit
        float normal_x, float normal_y, float normal_z,     // Surface normal at point
        const Particle& light                               // Light source
    );
    
    // Calculate total lighting at a point from all light sources
    // This combines contributions from multiple lights and checks for shadows
    // Returns total intensity in lux
    float calculate_point_lighting(
        float point_x, float point_y, float point_z,        // Point being lit
        float normal_x, float normal_y, float normal_z,     // Surface normal
        const std::vector<Particle>& particles,             // All particles (for shadow checks)
        const std::vector<int>& light_indices,              // Which particles are lights
        int surface_particle_idx = -1                       // Particle containing this point
    );
    
    // BVH-accelerated version of calculate_point_lighting
    float calculate_point_lighting_bvh(
        float point_x, float point_y, float point_z,        // Point being lit
        float normal_x, float normal_y, float normal_z,     // Surface normal
        const std::vector<Particle>& particles,             // All particles (for shadow checks)
        const std::vector<int>& light_indices,              // Which particles are lights
        const BVH* bvh,                                     // BVH acceleration structure
        int surface_particle_idx = -1                       // Particle containing this point
    );
    
    // ============================================================================
    // HELPER FUNCTIONS - Common calculations
    // ============================================================================
    
    // Calculate distance between two 3D points
    inline float distance_3d(
        float x1, float y1, float z1,
        float x2, float y2, float z2
    ) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float dz = z2 - z1;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }
    
    // Normalize a 3D vector in place
    inline void normalize_3d(float& x, float& y, float& z) {
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }
    }
    
    // Dot product of two 3D vectors
    inline float dot_3d(
        float x1, float y1, float z1,
        float x2, float y2, float z2
    ) {
        return x1*x2 + y1*y2 + z1*z2;
    }
}

#endif // LIGHTING_PRIMITIVES_H