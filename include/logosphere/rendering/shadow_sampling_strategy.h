#ifndef SHADOW_SAMPLING_STRATEGY_H
#define SHADOW_SAMPLING_STRATEGY_H

#include <vector>
#include <cmath>
#include "particle.h"
#include "logosphere/physics/bvh.h"
#include "lighting_config.h"
#include "lighting_primitives.h"

// =========================================================================
// SHADOW SAMPLING STRATEGY - Flexible Pattern Support
//
// Uses compile-time polymorphism (templates) for zero-overhead abstraction.
// Each pattern specializes the template with pattern-specific logic.
//
// WHY: Current 1D scanline sampling is hardcoded. Future optimizations
//      (2D hierarchical blocks, quad-tree) need flexible patterns.
// HOW: Strategy pattern with template specialization (no virtual calls)
// IMPACT: Zero performance cost, easy pattern switching
// =========================================================================

namespace ShadowSampling {

// Pattern types supported
enum class Pattern {
    SCANLINE_1D,      // Current: horizontal segments with binary search
    HIERARCHICAL_2D,  // Future: 8×8 or 16×16 block testing
    QUADTREE,         // Future: adaptive quad-tree subdivision
    MORTON_ORDER      // Future: cache-friendly Z-order curve
};

// Base template - each pattern provides specialization
// This is never instantiated directly, only specialized versions are used
template<Pattern P>
struct Strategy {
    // Patterns must implement:
    // - Pattern-specific constants (SEGMENT_SIZE, BLOCK_SIZE, etc.)
    // - sample_region() - collect samples and process
    // - Any helper methods needed
};

// =========================================================================
// SCANLINE_1D Strategy - Current Implementation
//
// Processes horizontal scanline segments with:
// - Endpoint testing to detect uniform regions
// - Binary search refinement for shadow edges
// - Linear interpolation for uniform sub-regions
//
// This is the existing algorithm extracted into strategy pattern.
// =========================================================================

template<>
struct Strategy<Pattern::SCANLINE_1D> {
    // Constants (from current implementation)
    static constexpr int SEGMENT_SIZE = 21;  // From optimization_flags.h
    static constexpr float SHADOW_DIFF_THRESHOLD = 0.1f;

    // Binary search refinement for non-uniform segments
    // Recursively tests middle pixels only in regions with lighting transitions
    // Interpolates uniform sub-regions to minimize ray tests
    static void refine_segment_binary(
        int start_idx, int end_idx,
        float left_lighting, float right_lighting,
        float* segment_lighting,  // Output array (indexed by segment position)
        const float* segment_world_x,
        const float* segment_world_y,
        const float* segment_world_z,
        float normal_x, float normal_y, float normal_z,
        int surface_particle_idx,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        int& rays_tested)  // Track rays for metrics
    {
        // Base case: adjacent pixels or single pixel
        if (end_idx - start_idx <= 1) {
            return;
        }

        // Find middle pixel
        int mid_idx = (start_idx + end_idx) / 2;

        // Test middle pixel lighting
        float mid_world_x = segment_world_x[mid_idx];
        float mid_world_y = segment_world_y[mid_idx];
        float mid_world_z = segment_world_z[mid_idx];

        float mid_lighting = 0.0f;
        LightingConfig& config = LightingConfig::get();

        for (const Particle* light : lights) {
            if (!light->is_light_source) continue;

            float dx = light->x - mid_world_x;
            float dy = light->y - mid_world_y;
            float dz = light->z - mid_world_z;
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq > light->emission_radius * light->emission_radius) continue;

            float inv_distance = 1.0f / std::sqrt(dist_sq);
            float light_dir_x = dx * inv_distance;
            float light_dir_y = dy * inv_distance;
            float light_dir_z = dz * inv_distance;
            float dot = normal_x * light_dir_x + normal_y * light_dir_y + normal_z * light_dir_z;

            if (dot <= 0.0f) continue;

            float intensity_lux = config.calculate_intensity_at_distance_sq(light->emission_strength, dist_sq);
            float intensity = intensity_lux * dot;

            bool blocked = LightingPrimitives::is_ray_blocked_bvh(
                mid_world_x, mid_world_y, mid_world_z,
                light->x, light->y, light->z,
                particles, shadow_bvh, surface_particle_idx
            );

            if (!blocked) {
                mid_lighting += intensity;
            }
        }

        segment_lighting[mid_idx] = mid_lighting;
        rays_tested += lights.size();

        // Check which half has lighting transition
        bool left_uniform = std::abs(left_lighting - mid_lighting) < SHADOW_DIFF_THRESHOLD;
        bool right_uniform = std::abs(mid_lighting - right_lighting) < SHADOW_DIFF_THRESHOLD;

        // Recurse on non-uniform halves
        if (!left_uniform && mid_idx - start_idx > 1) {
            refine_segment_binary(
                start_idx, mid_idx, left_lighting, mid_lighting,
                segment_lighting, segment_world_x, segment_world_y, segment_world_z,
                normal_x, normal_y, normal_z, surface_particle_idx,
                lights, particles, shadow_bvh, rays_tested
            );
        }

        if (!right_uniform && end_idx - mid_idx > 1) {
            refine_segment_binary(
                mid_idx, end_idx, mid_lighting, right_lighting,
                segment_lighting, segment_world_x, segment_world_y, segment_world_z,
                normal_x, normal_y, normal_z, surface_particle_idx,
                lights, particles, shadow_bvh, rays_tested
            );
        }

        // Interpolate uniform sub-regions (pixels we didn't test)
        // Left half
        if (left_uniform) {
            for (int i = start_idx + 1; i < mid_idx; i++) {
                float t = (float)(i - start_idx) / (float)(mid_idx - start_idx);
                segment_lighting[i] = left_lighting * (1.0f - t) + mid_lighting * t;
            }
        }

        // Right half
        if (right_uniform) {
            for (int i = mid_idx + 1; i < end_idx; i++) {
                float t = (float)(i - mid_idx) / (float)(end_idx - mid_idx);
                segment_lighting[i] = mid_lighting * (1.0f - t) + right_lighting * t;
            }
        }
    }
};

// =========================================================================
// HIERARCHICAL_2D Strategy - Simple 7×7 Block Testing
//
// Minimal implementation: test 4 corners, interpolate if uniform, brute-force if not
// No recursion, no subdivision, no corner reuse - pure simplicity
//
// Purpose: Measure whether block testing helps at all compared to scanline
// =========================================================================

template<>
struct Strategy<Pattern::HIERARCHICAL_2D> {
    static constexpr float UNIFORMITY_THRESHOLD = 0.1f;

    // Test lighting for a single pixel
    static float test_pixel_lighting(
        float world_x, float world_y, float world_z,
        float normal_x, float normal_y, float normal_z,
        int surface_particle_idx,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh)
    {
        float total_lighting = 0.0f;
        LightingConfig& config = LightingConfig::get();

        for (const Particle* light : lights) {
            if (!light->is_light_source) continue;

            float dx = light->x - world_x;
            float dy = light->y - world_y;
            float dz = light->z - world_z;
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq > light->emission_radius * light->emission_radius) continue;

            float inv_distance = 1.0f / std::sqrt(dist_sq);
            float light_dir_x = dx * inv_distance;
            float light_dir_y = dy * inv_distance;
            float light_dir_z = dz * inv_distance;
            float dot = normal_x * light_dir_x + normal_y * light_dir_y + normal_z * light_dir_z;

            if (dot <= 0.0f) continue;

            float intensity_lux = config.calculate_intensity_at_distance_sq(light->emission_strength, dist_sq);
            float intensity = intensity_lux * dot;

            bool blocked = LightingPrimitives::is_ray_blocked_bvh(
                world_x, world_y, world_z,
                light->x, light->y, light->z,
                particles, shadow_bvh, surface_particle_idx
            );

            if (!blocked) {
                total_lighting += intensity;
            }
        }

        return total_lighting;
    }

    // Process 13×13 block: test 4 corners → if different test cross → fill 4 quadrants
    static void process_block_13x13(
        float lighting_grid[13][13],
        float world_pos_grid[13][13][3],
        float normal_x, float normal_y, float normal_z,
        int surface_particle_idx,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        int& rays_tested)
    {
        // Step 1: Test 4 corners (0,0) (0,12) (12,0) (12,12)
        float corners[4];
        corners[0] = (lighting_grid[0][0] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[0][0][0], world_pos_grid[0][0][1], world_pos_grid[0][0][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        corners[1] = (lighting_grid[0][12] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[0][12][0], world_pos_grid[0][12][1], world_pos_grid[0][12][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        corners[2] = (lighting_grid[12][0] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[12][0][0], world_pos_grid[12][0][1], world_pos_grid[12][0][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        corners[3] = (lighting_grid[12][12] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[12][12][0], world_pos_grid[12][12][1], world_pos_grid[12][12][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        rays_tested += 4 * lights.size();

        // Step 2: Check if all corners match
        bool all_match = true;
        if (corners[0] >= 0.0f && corners[1] >= 0.0f && corners[2] >= 0.0f && corners[3] >= 0.0f) {
            float max_diff = std::max({
                std::abs(corners[0] - corners[1]), std::abs(corners[0] - corners[2]),
                std::abs(corners[0] - corners[3]), std::abs(corners[1] - corners[2]),
                std::abs(corners[1] - corners[3]), std::abs(corners[2] - corners[3])
            });
            all_match = (max_diff < UNIFORMITY_THRESHOLD);
        } else {
            all_match = false;
        }

        // Step 3: If all match, fill entire block and done
        if (all_match) {
            for (int y = 0; y < 13; y++) {
                for (int x = 0; x < 13; x++) {
                    if (lighting_grid[y][x] >= 0.0f) {
                        float u = x / 12.0f;
                        float v = y / 12.0f;
                        float top = corners[0] * (1.0f - u) + corners[1] * u;
                        float bottom = corners[2] * (1.0f - u) + corners[3] * u;
                        lighting_grid[y][x] = top * (1.0f - v) + bottom * v;
                    }
                }
            }
            return;
        }

        // Step 4: Different - test cross (center + 4 edges)
        float cross[5];  // center, top, right, bottom, left
        cross[0] = (lighting_grid[6][6] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[6][6][0], world_pos_grid[6][6][1], world_pos_grid[6][6][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        cross[1] = (lighting_grid[0][6] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[0][6][0], world_pos_grid[0][6][1], world_pos_grid[0][6][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        cross[2] = (lighting_grid[6][12] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[6][12][0], world_pos_grid[6][12][1], world_pos_grid[6][12][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        cross[3] = (lighting_grid[12][6] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[12][6][0], world_pos_grid[12][6][1], world_pos_grid[12][6][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        cross[4] = (lighting_grid[6][0] >= 0.0f) ?
            test_pixel_lighting(world_pos_grid[6][0][0], world_pos_grid[6][0][1], world_pos_grid[6][0][2],
                normal_x, normal_y, normal_z, surface_particle_idx, lights, particles, shadow_bvh) : -1.0f;

        rays_tested += 5 * lights.size();

        // Step 5: Process 4 quadrants (6×6 each)
        // TL: corners[0], cross[1], cross[4], cross[0]
        process_quadrant_6x6(0, 0, corners[0], cross[1], cross[4], cross[0],
            lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
            surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

        // TR: cross[1], corners[1], cross[0], cross[2]
        process_quadrant_6x6(0, 6, cross[1], corners[1], cross[0], cross[2],
            lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
            surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

        // BL: cross[4], cross[0], corners[2], cross[3]
        process_quadrant_6x6(6, 0, cross[4], cross[0], corners[2], cross[3],
            lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
            surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

        // BR: cross[0], cross[2], cross[3], corners[3]
        process_quadrant_6x6(6, 6, cross[0], cross[2], cross[3], corners[3],
            lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
            surface_particle_idx, lights, particles, shadow_bvh, rays_tested);
    }

    // Process one 7×7 quadrant (with second level subdivision)
    static void process_quadrant_6x6(
        int y_offset, int x_offset,
        float tl, float tr, float bl, float br,
        float lighting_grid[13][13],
        float world_pos_grid[13][13][3],
        float normal_x, float normal_y, float normal_z,
        int surface_particle_idx,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        int& rays_tested)
    {
        // Check if all 4 corners match
        bool all_valid = (tl >= 0.0f && tr >= 0.0f && bl >= 0.0f && br >= 0.0f);
        bool all_match = false;
        if (all_valid) {
            float max_diff = std::max({
                std::abs(tl - tr), std::abs(tl - bl), std::abs(tl - br),
                std::abs(tr - bl), std::abs(tr - br), std::abs(bl - br)
            });
            all_match = (max_diff < UNIFORMITY_THRESHOLD);
        }

        if (all_match) {
            // Fill entire 7×7 region
            for (int y = 0; y <= 6; y++) {
                for (int x = 0; x <= 6; x++) {
                    int gy = y_offset + y;
                    int gx = x_offset + x;
                    if (lighting_grid[gy][gx] >= 0.0f) {
                        float u = x / 6.0f;
                        float v = y / 6.0f;
                        float top = tl * (1.0f - u) + tr * u;
                        float bottom = bl * (1.0f - u) + br * u;
                        lighting_grid[gy][gx] = top * (1.0f - v) + bottom * v;
                    }
                }
            }
        } else {
            // NON-UNIFORM: Subdivide again - test inner cross
            float inner[5];  // center, top, right, bottom, left

            inner[0] = (lighting_grid[y_offset + 3][x_offset + 3] >= 0.0f) ?
                test_pixel_lighting(world_pos_grid[y_offset + 3][x_offset + 3][0],
                    world_pos_grid[y_offset + 3][x_offset + 3][1],
                    world_pos_grid[y_offset + 3][x_offset + 3][2],
                    normal_x, normal_y, normal_z, surface_particle_idx,
                    lights, particles, shadow_bvh) : -1.0f;

            inner[1] = (lighting_grid[y_offset][x_offset + 3] >= 0.0f) ?
                test_pixel_lighting(world_pos_grid[y_offset][x_offset + 3][0],
                    world_pos_grid[y_offset][x_offset + 3][1],
                    world_pos_grid[y_offset][x_offset + 3][2],
                    normal_x, normal_y, normal_z, surface_particle_idx,
                    lights, particles, shadow_bvh) : -1.0f;

            inner[2] = (lighting_grid[y_offset + 3][x_offset + 6] >= 0.0f) ?
                test_pixel_lighting(world_pos_grid[y_offset + 3][x_offset + 6][0],
                    world_pos_grid[y_offset + 3][x_offset + 6][1],
                    world_pos_grid[y_offset + 3][x_offset + 6][2],
                    normal_x, normal_y, normal_z, surface_particle_idx,
                    lights, particles, shadow_bvh) : -1.0f;

            inner[3] = (lighting_grid[y_offset + 6][x_offset + 3] >= 0.0f) ?
                test_pixel_lighting(world_pos_grid[y_offset + 6][x_offset + 3][0],
                    world_pos_grid[y_offset + 6][x_offset + 3][1],
                    world_pos_grid[y_offset + 6][x_offset + 3][2],
                    normal_x, normal_y, normal_z, surface_particle_idx,
                    lights, particles, shadow_bvh) : -1.0f;

            inner[4] = (lighting_grid[y_offset + 3][x_offset] >= 0.0f) ?
                test_pixel_lighting(world_pos_grid[y_offset + 3][x_offset][0],
                    world_pos_grid[y_offset + 3][x_offset][1],
                    world_pos_grid[y_offset + 3][x_offset][2],
                    normal_x, normal_y, normal_z, surface_particle_idx,
                    lights, particles, shadow_bvh) : -1.0f;

            rays_tested += 5 * lights.size();

            // Now process 4 sub-quadrants (3×3 each)
            process_subquadrant_3x3(y_offset, x_offset, tl, inner[1], inner[4], inner[0],
                lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
                surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

            process_subquadrant_3x3(y_offset, x_offset + 3, inner[1], tr, inner[0], inner[2],
                lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
                surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

            process_subquadrant_3x3(y_offset + 3, x_offset, inner[4], inner[0], bl, inner[3],
                lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
                surface_particle_idx, lights, particles, shadow_bvh, rays_tested);

            process_subquadrant_3x3(y_offset + 3, x_offset + 3, inner[0], inner[2], inner[3], br,
                lighting_grid, world_pos_grid, normal_x, normal_y, normal_z,
                surface_particle_idx, lights, particles, shadow_bvh, rays_tested);
        }
    }

    // Process one 3×3 or 4×4 sub-quadrant (final level - fill or brute force)
    static void process_subquadrant_3x3(
        int y_offset, int x_offset,
        float tl, float tr, float bl, float br,
        float lighting_grid[13][13],
        float world_pos_grid[13][13][3],
        float normal_x, float normal_y, float normal_z,
        int surface_particle_idx,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        int& rays_tested)
    {
        // Check if corners match
        bool all_valid = (tl >= 0.0f && tr >= 0.0f && bl >= 0.0f && br >= 0.0f);
        bool all_match = false;
        if (all_valid) {
            float max_diff = std::max({
                std::abs(tl - tr), std::abs(tl - bl), std::abs(tl - br),
                std::abs(tr - bl), std::abs(tr - br), std::abs(bl - br)
            });
            all_match = (max_diff < UNIFORMITY_THRESHOLD);
        }

        if (all_match) {
            // Fill 4×4 region (to cover from offset to offset+3)
            for (int y = 0; y <= 3; y++) {
                for (int x = 0; x <= 3; x++) {
                    int gy = y_offset + y;
                    int gx = x_offset + x;
                    if (gy < 13 && gx < 13 && lighting_grid[gy][gx] >= 0.0f) {
                        float u = x / 3.0f;
                        float v = y / 3.0f;
                        float top = tl * (1.0f - u) + tr * u;
                        float bottom = bl * (1.0f - u) + br * u;
                        lighting_grid[gy][gx] = top * (1.0f - v) + bottom * v;
                    }
                }
            }
        } else {
            // Brute force this small region
            for (int y = 0; y <= 3; y++) {
                for (int x = 0; x <= 3; x++) {
                    int gy = y_offset + y;
                    int gx = x_offset + x;
                    if (gy < 13 && gx < 13 && lighting_grid[gy][gx] >= 0.0f) {
                        lighting_grid[gy][gx] = test_pixel_lighting(
                            world_pos_grid[gy][gx][0], world_pos_grid[gy][gx][1],
                            world_pos_grid[gy][gx][2], normal_x, normal_y, normal_z,
                            surface_particle_idx, lights, particles, shadow_bvh);
                        rays_tested += lights.size();
                    }
                }
            }
        }
    }
};

} // namespace ShadowSampling

#endif // SHADOW_SAMPLING_STRATEGY_H
