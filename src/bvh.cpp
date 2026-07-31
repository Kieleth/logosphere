#include "logosphere/physics/bvh.h"
#include "particle_geometry_v2.h"
#include "lighting_metrics.h"
#include "frame_metrics.h"
#include "optimization_flags.h"
#include "simd_ray_aabb.h"
#include "surface_cache.h"
#include "lighting_primitives.h"
#include "logosphere/rendering/rasterization_math.h"  // For 3D math primitives
#include <algorithm>
#include <stack>
#include <iostream>
#include <chrono>
#include <limits>
#include <cmath>

// Convert a particle to its bounding box
AABB BVH::particle_to_aabb(const Particle& p) const {
    float half_x, half_y, half_z;

    // Shape-specific AABB. BOX uses explicit per-axis dimensions.
    // SPHERE is isotropic — size is its diameter on every axis.
    // ELLIPSOID is per-axis (width, height, thickness as per-axis
    // diameters). Treating ELLIPSOID like SPHERE (all axes = size/2)
    // produces a loose AABB — tightening it is this slice's fix.
    switch (p.shape) {
        case ParticleShape::SPHERE: {
            float r = p.size * 0.5f;
            half_x = half_y = half_z = r;
            break;
        }
        case ParticleShape::ELLIPSOID: {
            half_x = p.width * 0.5f;
            half_y = p.height * 0.5f;
            half_z = p.thickness * 0.5f;
            break;
        }
        case ParticleShape::BOX:
        default:
            half_x = p.width * 0.5f;
            half_y = p.height * 0.5f;
            half_z = p.thickness * 0.5f;
            break;
    }

    // Add small epsilon to avoid degenerate boxes (1% padding as per risk mitigation)
    float max_dim = std::max({half_x, half_y, half_z});
    float epsilon = max_dim * 0.01f;

    return AABB(
        p.x - half_x - epsilon,
        p.y - half_y - epsilon,
        p.z - half_z - epsilon,
        p.x + half_x + epsilon,
        p.y + half_y + epsilon,
        p.z + half_z + epsilon
    );
}

// Build the BVH from particles
void BVH::build(const std::vector<Particle>& particles) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Clear any existing BVH
    clear();
    
    if (particles.empty()) {
        is_built = false;
        return;
    }
    
    // Create indices for all non-light particles
    // OPTIMIZATION: Also skip particles that are commonly used as ray sources
    // This reduces wasted leaf tests significantly
    particle_indices.clear();
    for (size_t i = 0; i < particles.size(); i++) {
        if (!particles[i].is_light_source) {
            particle_indices.push_back(i);
        }
    }
    
    if (particle_indices.empty()) {
        is_built = false;
        return;
    }
    
    // Reserve space for nodes (worst case: 2N-1 nodes for N particles in binary tree)
    nodes.reserve(2 * particle_indices.size() - 1);

    // Build binary tree with SAH
    root_node_idx = build_recursive(particles, particle_indices, 0, particle_indices.size());

    is_built = true;
    
    // Track build time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    LightingMetrics::get().bvh_build_time = duration.count() / 1000.0;  // Convert to ms
    LightingMetrics::get().bvh_built = true;
    
    // Collect tree statistics
    int total_nodes, leaf_nodes, max_depth;
    get_tree_stats(total_nodes, leaf_nodes, max_depth);

    // DISABLED: Hot-path logging (every frame)
    // std::cout << "[BVH Built] " << particle_indices.size() << " particles -> "
    //           << total_nodes << " nodes (" << leaf_nodes << " leaves), "
    //           << "depth " << max_depth << ", "
    //           << "build time: " << LightingMetrics::get().bvh_build_time << "ms"
    //           << (Optimizations::BVH_USE_SAH ? " (SAH)" : " (median)") << std::endl;
}

// Recursive builder using median split (KISS approach)
int BVH::build_recursive(const std::vector<Particle>& particles,
                        std::vector<int>& indices,
                        int start, int end) {
    // Create a new node
    int node_idx = nodes.size();
    nodes.push_back(BVHNode());
    BVHNode& node = nodes[node_idx];
    
    // Calculate bounds for all particles in this range
    for (int i = start; i < end; i++) {
        AABB particle_box = particle_to_aabb(particles[indices[i]]);
        node.bounds.expand(particle_box);
    }
    
    int count = end - start;
    
    // If only one particle, make it a leaf
    if (count == 1) {
        node.particle_id = indices[start];
        node.left = -1;
        node.right = -1;
        return node_idx;
    }
    
    if (Optimizations::BVH_USE_SAH && count > 8) {  // Only use SAH for larger nodes
        // =========================================================================
        // BUCKET-BASED SAH: Efficient SAH using spatial buckets
        // Avoids expensive sorting, uses spatial subdivision instead
        // =========================================================================
        
        float best_cost = std::numeric_limits<float>::max();
        int best_axis = -1;
        int best_split_idx = start + count / 2;
        
        // Try each axis
        for (int axis = 0; axis < 3; axis++) {
            // Get bounds along this axis
            float axis_min = std::numeric_limits<float>::max();
            float axis_max = -std::numeric_limits<float>::max();
            
            for (int i = start; i < end; i++) {
                float pos = (axis == 0) ? particles[indices[i]].x :
                           (axis == 1) ? particles[indices[i]].y :
                                        particles[indices[i]].z;
                axis_min = std::min(axis_min, pos);
                axis_max = std::max(axis_max, pos);
            }
            
            if (axis_max - axis_min < 0.001f) continue;  // Skip degenerate axis
            
            // Create buckets
            const int NUM_BUCKETS = 12;
            struct Bucket {
                AABB bounds;
                int count = 0;
            } buckets[NUM_BUCKETS];
            
            // Assign particles to buckets
            for (int i = start; i < end; i++) {
                float pos = (axis == 0) ? particles[indices[i]].x :
                           (axis == 1) ? particles[indices[i]].y :
                                        particles[indices[i]].z;
                
                int bucket_idx = std::min(NUM_BUCKETS - 1,
                    (int)((pos - axis_min) / (axis_max - axis_min) * NUM_BUCKETS));
                
                buckets[bucket_idx].count++;
                buckets[bucket_idx].bounds.expand(particle_to_aabb(particles[indices[i]]));
            }
            
            // Accumulate bounds from left and right
            AABB left_bounds[NUM_BUCKETS - 1];
            AABB right_bounds[NUM_BUCKETS - 1];
            int left_count[NUM_BUCKETS - 1];
            int right_count[NUM_BUCKETS - 1];
            
            AABB left_accum;
            int left_sum = 0;
            for (int i = 0; i < NUM_BUCKETS - 1; i++) {
                left_sum += buckets[i].count;
                left_accum.expand(buckets[i].bounds);
                left_bounds[i] = left_accum;
                left_count[i] = left_sum;
            }
            
            AABB right_accum;
            int right_sum = 0;
            for (int i = NUM_BUCKETS - 1; i > 0; i--) {
                right_sum += buckets[i].count;
                right_accum.expand(buckets[i].bounds);
                right_bounds[i - 1] = right_accum;
                right_count[i - 1] = right_sum;
            }
            
            // Find best split bucket
            float parent_area = node.bounds.surface_area();
            for (int i = 0; i < NUM_BUCKETS - 1; i++) {
                if (left_count[i] == 0 || right_count[i] == 0) continue;
                
                float cost = 1.0f +
                    (left_bounds[i].surface_area() / parent_area) * left_count[i] +
                    (right_bounds[i].surface_area() / parent_area) * right_count[i];
                
                // Add balance penalty to prevent deep trees
                float balance = (float)left_count[i] / count;
                if (balance < 0.2f || balance > 0.8f) {
                    cost *= 1.3f;  // 30% penalty for unbalanced splits (20/80)
                }
                if (balance < 0.1f || balance > 0.9f) {
                    cost *= 1.5f;  // 50% penalty for very unbalanced splits (10/90)
                }
                
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    
                    // Find actual split position (first particle in right bucket)
                    float split_pos = axis_min + (axis_max - axis_min) * (i + 1) / NUM_BUCKETS;
                    
                    // Partition particles
                    auto predicate = [&](int idx) {
                        float pos = (axis == 0) ? particles[idx].x :
                                   (axis == 1) ? particles[idx].y :
                                                particles[idx].z;
                        return pos < split_pos;
                    };
                    
                    // This will be the actual split index after partitioning
                    best_split_idx = start + left_count[i];
                }
            }
        }
        
        // Partition based on best split
        if (best_axis >= 0) {
            // We need to actually partition the indices
            if (best_axis == 0) {
                std::nth_element(indices.begin() + start, indices.begin() + best_split_idx,
                    indices.begin() + end, [&particles](int a, int b) { 
                        return particles[a].x < particles[b].x; 
                    });
            } else if (best_axis == 1) {
                std::nth_element(indices.begin() + start, indices.begin() + best_split_idx,
                    indices.begin() + end, [&particles](int a, int b) { 
                        return particles[a].y < particles[b].y; 
                    });
            } else {
                std::nth_element(indices.begin() + start, indices.begin() + best_split_idx,
                    indices.begin() + end, [&particles](int a, int b) { 
                        return particles[a].z < particles[b].z; 
                    });
            }
        }
        
        // Build children with best split
        node.left = build_recursive(particles, indices, start, best_split_idx);
        node.right = build_recursive(particles, indices, best_split_idx, end);
        return node_idx;
        
    } else {
        // =========================================================================
        // BASELINE: Simple median split along longest axis
        // =========================================================================
        int best_split = start + count / 2;
        float x_extent = node.bounds.width();
        float y_extent = node.bounds.height();
        float z_extent = node.bounds.depth();
        
        int split_axis = 0;  // 0=X, 1=Y, 2=Z
        if (y_extent > x_extent) split_axis = 1;
        if (z_extent > std::max(x_extent, y_extent)) split_axis = 2;
        
        if (split_axis == 0) {
            std::nth_element(indices.begin() + start, indices.begin() + best_split, indices.begin() + end,
                [&particles](int a, int b) { return particles[a].x < particles[b].x; });
        } else if (split_axis == 1) {
            std::nth_element(indices.begin() + start, indices.begin() + best_split, indices.begin() + end,
                [&particles](int a, int b) { return particles[a].y < particles[b].y; });
        } else {
            std::nth_element(indices.begin() + start, indices.begin() + best_split, indices.begin() + end,
                [&particles](int a, int b) { return particles[a].z < particles[b].z; });
        }
        
        // Build children recursively
        node.left = build_recursive(particles, indices, start, best_split);
        node.right = build_recursive(particles, indices, best_split, end);
        
        return node_idx;
    }
}

// Clear the BVH
void BVH::clear() {
    nodes.clear();
    particle_indices.clear();
    root_node_idx = -1;
    is_built = false;
}

// Refit: Update AABBs without rebuilding tree structure
// 10-20× faster than full rebuild because:
// - No SAH evaluation
// - No partitioning/sorting
// - No memory allocation
// - Just AABB min/max updates
void BVH::refit(const std::vector<Particle>& particles) {
    if (!is_built || nodes.empty()) {
        return;  // Nothing to refit
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Bottom-up traversal: leaves first, then parents
    // This works because children always have higher indices than parents
    // (build_recursive creates parents before children, so children are at higher indices)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; i--) {
        BVHNode& node = nodes[i];

        if (node.is_leaf()) {
            // Leaf: Recompute AABB from particle
            int pid = node.particle_id;
            if (pid >= 0 && pid < static_cast<int>(particles.size())) {
                node.bounds = particle_to_aabb(particles[pid]);
            }
        } else {
            // Internal node: AABB = union(left child, right child)
            if (node.left >= 0 && node.left < static_cast<int>(nodes.size()) &&
                node.right >= 0 && node.right < static_cast<int>(nodes.size())) {

                const BVHNode& left = nodes[node.left];
                const BVHNode& right = nodes[node.right];

                // Reset bounds and expand with children
                node.bounds = AABB();
                node.bounds.expand(left.bounds);
                node.bounds.expand(right.bounds);
            }
        }
    }

    // Track refit time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    LightingMetrics::get().bvh_build_time = duration.count() / 1000.0;  // Reuse metric for both
}

// Create a deep copy of this BVH for thread-local use
// This avoids cache thrashing when multiple threads traverse the same BVH
BVH BVH::clone() const {
    BVH copy;
    copy.nodes = nodes;  // Deep copy the vector of nodes
    copy.particle_indices = particle_indices;  // Deep copy the indices
    copy.root_node_idx = root_node_idx;
    copy.is_built = is_built;
    return copy;  // RVO will optimize this
}

// Ray-AABB intersection using the slab method
bool BVH::ray_intersects_aabb(float ray_origin_x, float ray_origin_y, float ray_origin_z,
                              float ray_dir_x, float ray_dir_y, float ray_dir_z,
                              const AABB& box, float max_t) const {
    float dummy_tmin;
    return ray_intersects_aabb_with_dist(ray_origin_x, ray_origin_y, ray_origin_z,
                                         ray_dir_x, ray_dir_y, ray_dir_z,
                                         box, max_t, dummy_tmin);
}

// Ray-AABB intersection with entry distance (for front-to-back ordering)
bool BVH::ray_intersects_aabb_with_dist(float ray_origin_x, float ray_origin_y, float ray_origin_z,
                              float ray_dir_x, float ray_dir_y, float ray_dir_z,
                              const AABB& box, float max_t, float& out_tmin) const {
    // Handle division by zero for ray direction
    float inv_dir_x = (ray_dir_x != 0.0f) ? 1.0f / ray_dir_x : 1e30f;
    float inv_dir_y = (ray_dir_y != 0.0f) ? 1.0f / ray_dir_y : 1e30f;
    float inv_dir_z = (ray_dir_z != 0.0f) ? 1.0f / ray_dir_z : 1e30f;
    
    // X slabs
    float t1 = (box.min_x - ray_origin_x) * inv_dir_x;
    float t2 = (box.max_x - ray_origin_x) * inv_dir_x;
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);
    
    // Y slabs
    t1 = (box.min_y - ray_origin_y) * inv_dir_y;
    t2 = (box.max_y - ray_origin_y) * inv_dir_y;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    
    // Z slabs
    t1 = (box.min_z - ray_origin_z) * inv_dir_z;
    t2 = (box.max_z - ray_origin_z) * inv_dir_z;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    
    // Store entry distance for front-to-back ordering
    out_tmin = tmin;
    
    // Check if ray hits the box
    return tmax >= tmin && tmin <= max_t && tmax >= 0.0f;
}

// Trace a shadow ray through the BVH
bool BVH::trace_shadow_ray(float from_x, float from_y, float from_z,
                           float to_x, float to_y, float to_z,
                           const std::vector<Particle>& particles,
                           int skip_particle_id) const {
    if (!is_built || root_node_idx < 0) {
        return false;  // No BVH, no blocking
    }
    
    // Note: Per-ray timing disabled per PERFORMANCE_RESEARCH.md - use batch-level timing instead
    // PROFILE_BVH_TRAVERSE(); // Would accumulate 875K+ individual timings, creating measurement contamination
    
    // Track that we're using BVH
    LightingMetrics::get().bvh_rays_traced++;
    COUNT_SHADOW_RAY();  // Use LightingMetrics counter
    FRAME_COUNT_BVH;
    
    static int trace_debug = 0;
    bool debug_this = (trace_debug++ < 30);
    
    // Calculate ray direction and length
    float ray_dir_x = to_x - from_x;
    float ray_dir_y = to_y - from_y;
    float ray_dir_z = to_z - from_z;
    float ray_length = RasterizationMath::distance_3d(ray_dir_x, ray_dir_y, ray_dir_z);
    
    if (debug_this) {
        // printf("[BVH TRACE] Ray from (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f), skip=%d, length=%.1f\n",
        //        from_x, from_y, from_z, to_x, to_y, to_z, skip_particle_id, ray_length);
        // printf("  BVH has %zu nodes, root=%d\n", nodes.size(), root_node_idx);
    }
    
    if (ray_length < 0.001f) {
        return false;  // Too short to block
    }
    
    // Normalize direction
    ray_dir_x /= ray_length;
    ray_dir_y /= ray_length;
    ray_dir_z /= ray_length;
    
    // Stack-based traversal (no recursion)
    // OPTIMIZATION: Use fixed-size array instead of std::stack for better cache locality
    // With BVH depth 7, we need at most 14 stack entries (2^7 = 128 nodes)
    int node_stack[32];  // Fixed array in L1 cache
    int stack_ptr = 0;
    node_stack[stack_ptr++] = root_node_idx;
    
    // Track max stack depth for metrics
    int current_stack_depth = 1;
    LightingMetrics::get().bvh_max_stack_depth = std::max(
        LightingMetrics::get().bvh_max_stack_depth, current_stack_depth);
    
    
    // CRITICAL FOR FRONT-TO-BACK: Track if we found a blocker
    // This enables early termination of traversal
    bool found_blocker = false;
    
    while (stack_ptr > 0 && !found_blocker) {
        int node_idx = node_stack[--stack_ptr];
        current_stack_depth--;
        
        const BVHNode& node = nodes[node_idx];
        LightingMetrics::get().bvh_nodes_visited++;
        
        // OPTIMIZATION: Skip redundant AABB tests in front-to-back mode
        // With front-to-back, we only push nodes that already passed the hit test
        // Without it, we need to test each node from the stack
        bool node_hit = true;
        
        if (Optimizations::BVH_FRONT_TO_BACK) {
            // Only test the root node (first iteration)
            // All others were tested before being pushed
            if (node_idx == root_node_idx) {
                LightingMetrics::get().bvh_aabb_tests++;
                node_hit = ray_intersects_aabb(from_x, from_y, from_z,
                                         ray_dir_x, ray_dir_y, ray_dir_z,
                                         node.bounds, ray_length);
                
                if (node_hit) {
                    LightingMetrics::get().bvh_aabb_hits++;
                }
            }
            // else: node was already tested when pushed, skip redundant test
        } else {
            // Baseline mode: must test every node from stack
            LightingMetrics::get().bvh_aabb_tests++;
            node_hit = ray_intersects_aabb(from_x, from_y, from_z,
                                     ray_dir_x, ray_dir_y, ray_dir_z,
                                     node.bounds, ray_length);
            
            if (node_hit) {
                LightingMetrics::get().bvh_aabb_hits++;
            }
        }
        
        if (!node_hit) {
            continue;  // Skip this entire subtree
        }
        
        // If it's a leaf, check the actual particle
        if (node.is_leaf()) {
            LightingMetrics::get().bvh_leaf_tests++;
            int particle_id = node.particle_id;
            
            static int leaf_debug = 0;
            if (leaf_debug++ < 20) {
                // printf("[BVH LEAF] Testing particle %d for blocking\n", particle_id);
            }
            
            // Skip the particle we're checking from
            if (particle_id == skip_particle_id) {
                LightingMetrics::get().bvh_empty_leaf_hits++;
                continue;
            }
            
            const Particle& particle = particles[particle_id];
            
            // =========================================================================
            // FIXED[BVH-001]: Implemented precise ray-surface intersection
            // 
            // Now using actual surface geometry for accurate shadow calculations
            // This allows partial shadows where objects only partially occlude
            // =========================================================================
            
            // First do a quick AABB check to see if we need to test surfaces
            AABB particle_box = particle_to_aabb(particle);
            bool aabb_hit = ray_intersects_aabb(from_x, from_y, from_z,
                                               ray_dir_x, ray_dir_y, ray_dir_z,
                                               particle_box, ray_length);
            
            if (!aabb_hit) {
                continue; // Ray definitely misses this particle
            }
            
            // AABB hit, now do precise surface intersection
            bool hits = false;
            const auto& surfaces = particle.GetSurfaces();
            
            // DEBUG: Track what we're testing
            // Debug logging disabled for performance testing
            // static int debug_precise = 0;
            // if (debug_precise++ < 50) {
            //     printf("[BVH PRECISE] Testing particle %d with %zu surfaces, ray: (%.2f,%.2f,%.2f) -> dir(%.2f,%.2f,%.2f) len=%.2f\n",
            //            particle_id, surfaces.size(), from_x, from_y, from_z, 
            //            ray_dir_x, ray_dir_y, ray_dir_z, ray_length);
            //     printf("  Particle at (%.1f,%.1f,%.1f) size %.1f, skip_id=%d\n",
            //            particle.x, particle.y, particle.z, particle.size, skip_particle_id);
            // }
            
            // Test each surface (triangle or quad) of the particle
            for (const auto& surface : surfaces) {
                // For quads, we'll test both triangles
                int num_triangles = (surface.vertex_count == 4) ? 2 : 1;
                
                for (int tri = 0; tri < num_triangles; tri++) {
                    // Ray-triangle intersection test
                    float t1 = 0, t2 = 0, t3 = 0; // Barycentric coordinates
                    float t = 0; // Distance along ray
                    
                    // Get triangle vertices
                    // For quads: triangle 0 = (0,1,2), triangle 1 = (0,2,3)
                    const float* v0;
                    const float* v1;
                    const float* v2;
                    
                    if (tri == 0) {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[1];
                        v2 = surface.vertices[2];
                    } else {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[2];
                        v2 = surface.vertices[3];
                    }
                
                // Edge vectors
                float e1_x = v1[0] - v0[0];
                float e1_y = v1[1] - v0[1];
                float e1_z = v1[2] - v0[2];
                
                float e2_x = v2[0] - v0[0];
                float e2_y = v2[1] - v0[1];
                float e2_z = v2[2] - v0[2];
                
                // Cross product of ray direction and edge2
                float p_x = ray_dir_y * e2_z - ray_dir_z * e2_y;
                float p_y = ray_dir_z * e2_x - ray_dir_x * e2_z;
                float p_z = ray_dir_x * e2_y - ray_dir_y * e2_x;
                
                // Determinant
                float det = e1_x * p_x + e1_y * p_y + e1_z * p_z;
                
                // If determinant is near zero, ray is parallel to triangle
                if (std::abs(det) < 0.000001f) {
                    continue;
                }
                
                float inv_det = 1.0f / det;
                
                // Distance from v0 to ray origin
                float t_x = from_x - v0[0];
                float t_y = from_y - v0[1];
                float t_z = from_z - v0[2];
                
                // First barycentric coordinate
                float u = (t_x * p_x + t_y * p_y + t_z * p_z) * inv_det;
                if (u < 0.0f || u > 1.0f) {
                    continue;
                }
                
                // Cross product of t and edge1
                float q_x = t_y * e1_z - t_z * e1_y;
                float q_y = t_z * e1_x - t_x * e1_z;
                float q_z = t_x * e1_y - t_y * e1_x;
                
                // Second barycentric coordinate
                float v = (ray_dir_x * q_x + ray_dir_y * q_y + ray_dir_z * q_z) * inv_det;
                if (v < 0.0f || u + v > 1.0f) {
                    continue;
                }
                
                // Distance along ray to intersection point
                t = (e2_x * q_x + e2_y * q_y + e2_z * q_z) * inv_det;
                
                // Check if intersection is within ray segment
                if (t > 0.0f && t < ray_length) {
                    static int hit_debug = 0;
                    if (hit_debug++ < 20) {
                        // printf("[BVH HIT] Particle %d surface hit at t=%.3f (ray len=%.3f)\n",
                        //        particle_id, t, ray_length);
                    }
                    hits = true;
                    break; // Found a hit, no need to test other triangles
                }
                } // End triangle loop
                
                if (hits) break; // Found a hit, no need to test other surfaces
            }
            
            
            if (hits) {
                // DEBUG: Report which particle blocks the ray
                // Debug logging disabled for performance testing
                // static int block_debug_count = 0;
                // if (block_debug_count++ < 50) {
                //     printf("[BVH BLOCKER] Particle %d at (%.1f,%.1f,%.1f) blocks ray (skip was %d)\n", 
                //            particle_id, particle.x, particle.y, particle.z, skip_particle_id);
                //     printf("  Ray was from (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
                //            from_x, from_y, from_z, to_x, to_y, to_z);
                // }
                
                // EARLY EXIT: Found a blocker!
                // With front-to-back traversal, we can stop immediately
                if (Optimizations::BVH_FRONT_TO_BACK) {
                    found_blocker = true;  // This will exit the while loop
                }
                COUNT_SHADOW_BLOCKED();
                return true;  // Ray is blocked (conservative approximation)
            }
        } else {
            // Interior node - traverse binary tree children
            const BVHNode& left_child = nodes[node.left];
            const BVHNode& right_child = nodes[node.right];
            
            // Update stack depth tracking
            current_stack_depth += 2;  // Adding two children
            LightingMetrics::get().bvh_max_stack_depth = std::max(
                LightingMetrics::get().bvh_max_stack_depth, current_stack_depth);
            
            if (Optimizations::BVH_FRONT_TO_BACK) {
                // =========================================================================
                // OPTIMIZATION: Front-to-back traversal for shadow rays with early exit
                // 
                // GOAL: Find ANY blocker as quickly as possible (not the closest)
                // 
                // STRATEGY:
                // 1. Compute entry distance for both children (reuse parent's hit test)
                // 2. Process nearer child first - higher chance of finding blocker early
                // 3. If nearer child blocks, skip far child entirely (early exit)
                // 4. Only test far child if near child was clear
                // 
                // WHY IT HELPS:
                // - 23% of shadow rays are blocked (from profiling)
                // - Processing near-to-far increases chance of early termination
                // - Saves traversing far subtree when near subtree blocks
                // 
                // CRITICAL: We already know at least one child hits (parent hit)
                //           So we can optimize by only computing distances
                // =========================================================================
                
                // Compute entry distances for both children
                // Note: Parent already hit, so at least one child must hit
                float left_tmin = std::numeric_limits<float>::max();
                float right_tmin = std::numeric_limits<float>::max();
                
                bool left_hit, right_hit;
                
                // Use SIMD to test both children simultaneously if enabled
                if (Optimizations::BVH_SIMD_DUAL_TEST) {
                    int hit_mask = SIMD::test_ray_dual_aabb(
                        from_x, from_y, from_z,
                        ray_dir_x, ray_dir_y, ray_dir_z,
                        left_child.bounds, right_child.bounds,
                        ray_length,
                        left_tmin, right_tmin);
                    
                    left_hit = (hit_mask & 1) != 0;
                    right_hit = (hit_mask & 2) != 0;
                } else {
                    // Fallback to scalar tests
                    left_hit = ray_intersects_aabb_with_dist(from_x, from_y, from_z,
                                                            ray_dir_x, ray_dir_y, ray_dir_z,
                                                            left_child.bounds, ray_length, left_tmin);
                    
                    right_hit = ray_intersects_aabb_with_dist(from_x, from_y, from_z,
                                                             ray_dir_x, ray_dir_y, ray_dir_z,
                                                             right_child.bounds, ray_length, right_tmin);
                }
                
                // Update metrics
                LightingMetrics::get().bvh_aabb_tests += 2;
                if (left_hit) LightingMetrics::get().bvh_aabb_hits++;
                if (right_hit) LightingMetrics::get().bvh_aabb_hits++;
                
                // Push children in far-to-near order (stack processes in reverse)
                // This ensures we process nearer child first
                if (left_hit && right_hit) {
                    // Both hit - order by entry distance
                    if (left_tmin < right_tmin) {
                        node_stack[stack_ptr++] = node.right;  // Push far first
                        node_stack[stack_ptr++] = node.left;   // Near on top (processed first)
                    } else {
                        node_stack[stack_ptr++] = node.left;   // Push far first  
                        node_stack[stack_ptr++] = node.right;  // Near on top (processed first)
                    }
                } else if (left_hit) {
                    // Only left hit
                    node_stack[stack_ptr++] = node.left;
                    current_stack_depth--;  // Only added one child (not two)
                } else if (right_hit) {
                    // Only right hit
                    node_stack[stack_ptr++] = node.right;
                    current_stack_depth--;  // Only added one child (not two)
                } else {
                    // Neither hit (shouldn't happen if parent hit, but be safe)
                    current_stack_depth -= 2;  // No children added
                }
            } else {
                // =========================================================================
                // BASELINE: Simple distance-to-center heuristic
                // =========================================================================
                float left_dist_sq = 
                    (left_child.bounds.center_x() - from_x) * (left_child.bounds.center_x() - from_x) +
                    (left_child.bounds.center_y() - from_y) * (left_child.bounds.center_y() - from_y) +
                    (left_child.bounds.center_z() - from_z) * (left_child.bounds.center_z() - from_z);
                
                float right_dist_sq = 
                    (right_child.bounds.center_x() - from_x) * (right_child.bounds.center_x() - from_x) +
                    (right_child.bounds.center_y() - from_y) * (right_child.bounds.center_y() - from_y) +
                    (right_child.bounds.center_z() - from_z) * (right_child.bounds.center_z() - from_z);
                
                if (left_dist_sq > right_dist_sq) {
                    node_stack[stack_ptr++] = node.left;
                    node_stack[stack_ptr++] = node.right;
                } else {
                    node_stack[stack_ptr++] = node.right;
                    node_stack[stack_ptr++] = node.left;
                }
            }
        }
    }
    
    COUNT_SHADOW_CLEAR();
    return false;  // Ray reached the light
}

// Trace a shadow ray with multiple exclusion IDs (for creature vision)
// This is used when a creature needs to exclude all its body particles from occlusion checks
bool BVH::trace_shadow_ray_exclude(float from_x, float from_y, float from_z,
                                   float to_x, float to_y, float to_z,
                                   const std::vector<Particle>& particles,
                                   const std::vector<int>& skip_particle_ids) const {
    if (!is_built || root_node_idx < 0) {
        return false;  // No BVH, no blocking
    }

    // Build a helper for fast exclusion check
    auto is_excluded = [&skip_particle_ids](int id) {
        return std::find(skip_particle_ids.begin(), skip_particle_ids.end(), id) != skip_particle_ids.end();
    };

    // Calculate ray direction and length
    float ray_dir_x = to_x - from_x;
    float ray_dir_y = to_y - from_y;
    float ray_dir_z = to_z - from_z;
    float ray_length = RasterizationMath::distance_3d(ray_dir_x, ray_dir_y, ray_dir_z);

    if (ray_length < 0.001f) {
        return false;  // Too short to block
    }

    // Normalize direction
    ray_dir_x /= ray_length;
    ray_dir_y /= ray_length;
    ray_dir_z /= ray_length;

    // Stack-based traversal
    int node_stack[32];
    int stack_ptr = 0;
    node_stack[stack_ptr++] = root_node_idx;

    while (stack_ptr > 0) {
        int node_idx = node_stack[--stack_ptr];
        const BVHNode& node = nodes[node_idx];

        // Test root AABB
        bool node_hit = ray_intersects_aabb(from_x, from_y, from_z,
                                           ray_dir_x, ray_dir_y, ray_dir_z,
                                           node.bounds, ray_length);

        if (!node_hit) {
            continue;  // Skip this entire subtree
        }

        // If it's a leaf, check the actual particle
        if (node.is_leaf()) {
            int particle_id = node.particle_id;

            // Skip any particle in the exclusion list (body parts)
            if (is_excluded(particle_id)) {
                continue;
            }

            const Particle& particle = particles[particle_id];

            // Quick AABB check
            AABB particle_box = particle_to_aabb(particle);
            bool aabb_hit = ray_intersects_aabb(from_x, from_y, from_z,
                                               ray_dir_x, ray_dir_y, ray_dir_z,
                                               particle_box, ray_length);

            if (!aabb_hit) {
                continue;
            }

            // Precise surface intersection
            bool hits = false;
            const auto& surfaces = particle.GetSurfaces();

            for (const auto& surface : surfaces) {
                int num_triangles = (surface.vertex_count == 4) ? 2 : 1;

                for (int tri = 0; tri < num_triangles && !hits; tri++) {
                    const float* v0;
                    const float* v1;
                    const float* v2;

                    if (tri == 0) {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[1];
                        v2 = surface.vertices[2];
                    } else {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[2];
                        v2 = surface.vertices[3];
                    }

                    // Möller–Trumbore ray-triangle intersection
                    float e1_x = v1[0] - v0[0], e1_y = v1[1] - v0[1], e1_z = v1[2] - v0[2];
                    float e2_x = v2[0] - v0[0], e2_y = v2[1] - v0[1], e2_z = v2[2] - v0[2];

                    float h_x = ray_dir_y * e2_z - ray_dir_z * e2_y;
                    float h_y = ray_dir_z * e2_x - ray_dir_x * e2_z;
                    float h_z = ray_dir_x * e2_y - ray_dir_y * e2_x;

                    float a = e1_x * h_x + e1_y * h_y + e1_z * h_z;
                    if (std::abs(a) < 1e-6f) continue;  // Parallel

                    float f = 1.0f / a;
                    float s_x = from_x - v0[0], s_y = from_y - v0[1], s_z = from_z - v0[2];

                    float u = f * (s_x * h_x + s_y * h_y + s_z * h_z);
                    if (u < 0.0f || u > 1.0f) continue;

                    float q_x = s_y * e1_z - s_z * e1_y;
                    float q_y = s_z * e1_x - s_x * e1_z;
                    float q_z = s_x * e1_y - s_y * e1_x;

                    float v = f * (ray_dir_x * q_x + ray_dir_y * q_y + ray_dir_z * q_z);
                    if (v < 0.0f || u + v > 1.0f) continue;

                    float t = f * (e2_x * q_x + e2_y * q_y + e2_z * q_z);
                    if (t > 0.001f && t < ray_length) {
                        hits = true;
                    }
                }

                if (hits) break;
            }

            if (hits) {
                return true;  // Found blocker
            }
        } else {
            // Internal node - push children
            if (node.left >= 0) node_stack[stack_ptr++] = node.left;
            if (node.right >= 0) node_stack[stack_ptr++] = node.right;
        }
    }

    return false;  // Ray reached target
}

// Cast a ray and return the particle_id of the first hit (-1 if nothing)
// Used for vision cone: cast rays across FOV, collect what each hits
int BVH::trace_ray_hit(float origin_x, float origin_y, float origin_z,
                       float dir_x, float dir_y, float dir_z,
                       float max_distance,
                       const std::vector<Particle>& particles,
                       const std::vector<int>& skip_particle_ids) const {
    if (!is_built || root_node_idx < 0) {
        return -1;  // No BVH, no hits
    }

    // Normalize direction (caller may not have done this)
    float dir_len = RasterizationMath::distance_3d(dir_x, dir_y, dir_z);
    if (dir_len < 0.001f) {
        return -1;  // Invalid direction
    }
    dir_x /= dir_len;
    dir_y /= dir_len;
    dir_z /= dir_len;

    // Track closest hit
    float closest_t = max_distance;
    int closest_particle_id = -1;

    // Build exclusion check helper
    auto is_excluded = [&skip_particle_ids](int id) {
        return std::find(skip_particle_ids.begin(), skip_particle_ids.end(), id) != skip_particle_ids.end();
    };

    // Stack-based BVH traversal
    int node_stack[32];
    int stack_ptr = 0;
    node_stack[stack_ptr++] = root_node_idx;

    while (stack_ptr > 0) {
        int node_idx = node_stack[--stack_ptr];
        const BVHNode& node = nodes[node_idx];

        // Test AABB intersection
        bool node_hit = ray_intersects_aabb(origin_x, origin_y, origin_z,
                                           dir_x, dir_y, dir_z,
                                           node.bounds, closest_t);

        if (!node_hit) {
            continue;  // Skip subtree
        }

        if (node.is_leaf()) {
            int particle_id = node.particle_id;

            // Skip excluded particles (body parts)
            if (is_excluded(particle_id)) {
                continue;
            }

            const Particle& particle = particles[particle_id];

            // Quick AABB check
            AABB particle_box = particle_to_aabb(particle);
            bool aabb_hit = ray_intersects_aabb(origin_x, origin_y, origin_z,
                                               dir_x, dir_y, dir_z,
                                               particle_box, closest_t);

            if (!aabb_hit) {
                continue;
            }

            // Precise surface intersection - find closest hit
            const auto& surfaces = particle.GetSurfaces();

            for (const auto& surface : surfaces) {
                int num_triangles = (surface.vertex_count == 4) ? 2 : 1;

                for (int tri = 0; tri < num_triangles; tri++) {
                    const float* v0;
                    const float* v1;
                    const float* v2;

                    if (tri == 0) {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[1];
                        v2 = surface.vertices[2];
                    } else {
                        v0 = surface.vertices[0];
                        v1 = surface.vertices[2];
                        v2 = surface.vertices[3];
                    }

                    // Möller–Trumbore ray-triangle intersection
                    float e1_x = v1[0] - v0[0], e1_y = v1[1] - v0[1], e1_z = v1[2] - v0[2];
                    float e2_x = v2[0] - v0[0], e2_y = v2[1] - v0[1], e2_z = v2[2] - v0[2];

                    float h_x = dir_y * e2_z - dir_z * e2_y;
                    float h_y = dir_z * e2_x - dir_x * e2_z;
                    float h_z = dir_x * e2_y - dir_y * e2_x;

                    float a = e1_x * h_x + e1_y * h_y + e1_z * h_z;
                    if (std::abs(a) < 1e-6f) continue;  // Parallel

                    float f = 1.0f / a;
                    float s_x = origin_x - v0[0], s_y = origin_y - v0[1], s_z = origin_z - v0[2];

                    float u = f * (s_x * h_x + s_y * h_y + s_z * h_z);
                    if (u < 0.0f || u > 1.0f) continue;

                    float q_x = s_y * e1_z - s_z * e1_y;
                    float q_y = s_z * e1_x - s_x * e1_z;
                    float q_z = s_x * e1_y - s_y * e1_x;

                    float v = f * (dir_x * q_x + dir_y * q_y + dir_z * q_z);
                    if (v < 0.0f || u + v > 1.0f) continue;

                    float t = f * (e2_x * q_x + e2_y * q_y + e2_z * q_z);
                    if (t > 0.001f && t < closest_t) {
                        closest_t = t;
                        closest_particle_id = particle_id;
                    }
                }
            }
        } else {
            // Internal node - push children
            if (node.left >= 0) node_stack[stack_ptr++] = node.left;
            if (node.right >= 0) node_stack[stack_ptr++] = node.right;
        }
    }

    return closest_particle_id;
}

// Batch trace multiple shadow rays - simple implementation for Phase 0
std::vector<bool> BVH::trace_shadow_rays_batch(const std::vector<ShadowRay>& rays,
                                               const std::vector<Particle>& particles) const {
    std::vector<bool> results;
    results.reserve(rays.size());
    
    for (const auto& ray : rays) {
        bool is_blocked = trace_shadow_ray(
            ray.origin_x, ray.origin_y, ray.origin_z,
            ray.target_x, ray.target_y, ray.target_z,
            particles, ray.exclude_particle_id
        );
        results.push_back(is_blocked);
    }
    
    return results;
}

// Batched shadow-ray entry. The old #ifdef __SSE__ arm packed rays four
// at a time into the SIMD helpers deleted above; those referenced a BVH
// node layout that no longer exists, so the SSE arm never compiled on
// x86 and every shipping build (arm64) already took this scalar path.
std::vector<bool> BVH::trace_shadow_rays_batch_simd(const std::vector<ShadowRay>& rays,
                                                    const std::vector<Particle>& particles) const {
    return trace_shadow_rays_batch(rays, particles);
}

// Get tree depth for debugging
int BVH::get_depth() const {
    if (!is_built || nodes.empty()) return 0;
    
    int max_depth = 0;
    std::stack<std::pair<int, int>> stack;  // node_idx, depth
    stack.push({root_node_idx, 1});
    
    while (!stack.empty()) {
        auto [node_idx, depth] = stack.top();
        stack.pop();
        
        max_depth = std::max(max_depth, depth);
        
        const BVHNode& node = nodes[node_idx];
        if (!node.is_leaf()) {
            stack.push({node.left, depth + 1});
            stack.push({node.right, depth + 1});
        }
    }
    
    return max_depth;
}

// Get leaf count
int BVH::get_leaf_count() const {
    if (!is_built || nodes.empty()) return 0;
    
    int leaf_count = 0;
    for (const auto& node : nodes) {
        if (node.is_leaf()) {
            leaf_count++;
        }
    }
    return leaf_count;
}

// Get comprehensive tree stats
void BVH::get_tree_stats(int& total_nodes, int& leaf_nodes, int& max_depth) const {
    total_nodes = nodes.size();
    leaf_nodes = get_leaf_count();
    max_depth = get_depth();

    // Store in global metrics for profiling
    LightingMetrics::get().bvh_total_nodes = total_nodes;
    LightingMetrics::get().bvh_leaf_nodes = leaf_nodes;
    LightingMetrics::get().bvh_max_depth = max_depth;
}

// =========================================================================
// COLLISION DETECTION - AABB overlap queries for physics
// =========================================================================

// AABB-AABB overlap test (separating axis theorem)
bool BVH::aabb_overlaps_aabb(const AABB& a, const AABB& b) const {
    // No overlap if separated on ANY axis
    if (a.max_x < b.min_x || a.min_x > b.max_x) return false;
    if (a.max_y < b.min_y || a.min_y > b.max_y) return false;
    if (a.max_z < b.min_z || a.min_z > b.max_z) return false;

    // Overlapping on all axes = collision!
    return true;
}

// Recursive traversal to find all particles overlapping with query AABB
void BVH::query_aabb_recursive(int node_index, const AABB& query_box,
                               const std::vector<Particle>& particles,
                               std::vector<int>& out_particle_indices) const {
    if (node_index < 0 || node_index >= static_cast<int>(nodes.size())) {
        return;
    }

    const BVHNode& node = nodes[node_index];

    // Early out: query box doesn't overlap with this node's bounds
    if (!aabb_overlaps_aabb(query_box, node.bounds)) {
        return;
    }

    // Leaf node: add particle to results
    if (node.is_leaf()) {
        if (node.particle_id >= 0 && node.particle_id < static_cast<int>(particles.size())) {
            out_particle_indices.push_back(node.particle_id);
        }
        return;
    }

    // Internal node: recursively check children
    if (node.left >= 0) {
        query_aabb_recursive(node.left, query_box, particles, out_particle_indices);
    }
    if (node.right >= 0) {
        query_aabb_recursive(node.right, query_box, particles, out_particle_indices);
    }
}

// Public interface: query all particles overlapping with query AABB
void BVH::query_aabb(const AABB& query_box,
                     const std::vector<Particle>& particles,
                     std::vector<int>& out_particle_indices) const {
    if (!is_built || root_node_idx < 0) {
        return;
    }

    out_particle_indices.clear();
    query_aabb_recursive(root_node_idx, query_box, particles, out_particle_indices);
}

// =========================================================================
// (Deleted: the #ifdef __SSE__ 4-ray SIMD batch helpers. Uncalled, and
//  they referenced a BVH node layout (start_idx/count) that no longer
//  exists, so they could not compile on any x86 build. The portable
//  traversal is the only ray path.)
// =========================================================================
