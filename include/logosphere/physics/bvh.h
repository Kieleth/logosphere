#ifndef BVH_H
#define BVH_H

#include <vector>
#include <deque>
#include <limits>
#include "particle.h"

// Axis-Aligned Bounding Box - the fundamental building block
// Represents a 3D box aligned to world axes (no rotation)
// Optimized for cache efficiency - store min/max together for SIMD
struct AABB {
    float min_x, min_y, min_z;  // Minimum corner of the box (12 bytes)
    float max_x, max_y, max_z;  // Maximum corner of the box (12 bytes)
    
    // Default constructor - creates an "inverted" box for easy expansion
    AABB() : 
        min_x(std::numeric_limits<float>::max()),
        min_y(std::numeric_limits<float>::max()),
        min_z(std::numeric_limits<float>::max()),
        max_x(-std::numeric_limits<float>::max()),
        max_y(-std::numeric_limits<float>::max()),
        max_z(-std::numeric_limits<float>::max()) {}
    
    // Constructor from bounds
    AABB(float minx, float miny, float minz, float maxx, float maxy, float maxz) :
        min_x(minx), min_y(miny), min_z(minz),
        max_x(maxx), max_y(maxy), max_z(maxz) {}
    
    // Expand this box to include a point
    void expand(float x, float y, float z) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        min_z = std::min(min_z, z);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
        max_z = std::max(max_z, z);
    }
    
    // Expand this box to include another box
    void expand(const AABB& other) {
        min_x = std::min(min_x, other.min_x);
        min_y = std::min(min_y, other.min_y);
        min_z = std::min(min_z, other.min_z);
        max_x = std::max(max_x, other.max_x);
        max_y = std::max(max_y, other.max_y);
        max_z = std::max(max_z, other.max_z);
    }
    
    // Get center of the box (used for splitting)
    float center_x() const { return (min_x + max_x) * 0.5f; }
    float center_y() const { return (min_y + max_y) * 0.5f; }
    float center_z() const { return (min_z + max_z) * 0.5f; }
    
    // Get the size along each axis
    float width() const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    float depth() const { return max_z - min_z; }
    
    // Get surface area (used for SAH if we add it later)
    float surface_area() const {
        float w = width();
        float h = height();
        float d = depth();
        return 2.0f * (w*h + w*d + h*d);
    }
};

// Shadow Ray for batching - simple structure for batch ray tests
struct ShadowRay {
    float origin_x, origin_y, origin_z;    // Pixel world position
    float target_x, target_y, target_z;    // Light position
    int exclude_particle_id;               // Don't hit source particle
    int pixel_index;                       // Which pixel this ray belongs to
};

// BVH Node - Binary tree structure optimized for cache efficiency
// Compact 36-byte layout fits ~1.77 nodes per 64-byte cache line
// Cache-aligned to prevent false sharing in parallel traversal
struct alignas(64) BVHNode {  // Align to cache line size
    AABB bounds;         // The bounding box for this node (24 bytes)
    int left;            // Index of left child (-1 if leaf) (4 bytes)
    int right;           // Index of right child (-1 if leaf) (4 bytes)
    int particle_id;     // Particle index (only valid for leaf nodes) (4 bytes)

    // Total: 36 bytes (vs 128 bytes before wide tree removal)
    // Note: alignas(64) pads to cache line, but multiple nodes pack together

    // Default constructor
    BVHNode() : left(-1), right(-1), particle_id(-1) {}

    // Check if this is a leaf node
    bool is_leaf() const {
        return particle_id >= 0;  // Leaf nodes have particle IDs
    }
};

// BVH - the main acceleration structure
class BVH {
private:
    std::vector<BVHNode> nodes;           // All nodes in the tree
    std::vector<int> particle_indices;    // Indices of particles (for rebuilding)
    int root_node_idx;                    // Index of root node
    bool is_built;                        // Flag to check if BVH is ready
    
    // Recursive builder for binary tree (returns node index)
    int build_recursive(const std::vector<Particle>& particles,
                       std::vector<int>& indices,
                       int start, int end);
    
public:
    BVH() : root_node_idx(-1), is_built(false) {}

    // The leaf bound of one particle: oriented for a rotated BOX, exact for
    // every shape, padded 1%. Public because the creation door queries with
    // the SAME bound the tree was built from — a query box computed a second
    // way is a second geometry, and the two would eventually disagree.
    AABB particle_to_aabb(const Particle& p) const;

    // Build the BVH from particles
    void build(const std::vector<Particle>& particles);

    // Refit: Update AABBs without rebuilding tree structure (10-20× faster than rebuild)
    // Use when particles move but count is unchanged
    void refit(const std::vector<Particle>& particles);

    // Clear the BVH
    void clear();
    
    // Check if built
    bool is_ready() const { return is_built; }
    
    // Create a deep copy of this BVH (for thread-local copies)
    // This copies the tree structure but not the particle data
    BVH clone() const;
    
    // Trace a shadow ray (returns true if blocked)
    bool trace_shadow_ray(float from_x, float from_y, float from_z,
                         float to_x, float to_y, float to_z,
                         const std::vector<Particle>& particles,
                         int skip_particle_id = -1) const;

    // Trace a shadow ray with multiple exclusion IDs (for creature vision)
    bool trace_shadow_ray_exclude(float from_x, float from_y, float from_z,
                                  float to_x, float to_y, float to_z,
                                  const std::vector<Particle>& particles,
                                  const std::vector<int>& skip_particle_ids) const;

    // Cast a ray and return the particle_id of the first hit (-1 if nothing hit)
    // Used for vision cone: cast rays, collect what they hit
    int trace_ray_hit(float origin_x, float origin_y, float origin_z,
                      float dir_x, float dir_y, float dir_z,
                      float max_distance,
                      const std::vector<Particle>& particles,
                      const std::vector<int>& skip_particle_ids) const;

    // Batch trace multiple shadow rays (returns true if blocked for each ray)
    std::vector<bool> trace_shadow_rays_batch(const std::vector<ShadowRay>& rays,
                                              const std::vector<Particle>& particles) const;
    
    // SIMD batch trace multiple shadow rays (4 rays at a time)
    // ⚠️  Requires USE_SIMD_RAY_BATCHING flag enabled
    std::vector<bool> trace_shadow_rays_batch_simd(const std::vector<ShadowRay>& rays,
                                                   const std::vector<Particle>& particles) const;
    
    // Query all particles that overlap with a given AABB (for collision detection)
    // Returns particle indices that might collide with the query region
    void query_aabb(const AABB& query_box,
                    const std::vector<Particle>& particles,
                    std::vector<int>& out_particle_indices) const;

    // Get stats for debugging
    int get_node_count() const { return nodes.size(); }
    int get_depth() const;
    int get_leaf_count() const;
    void get_tree_stats(int& total_nodes, int& leaf_nodes, int& max_depth) const;
    bool is_bvh_built() const { return is_built; }
    
private:
    // AABB-AABB overlap test (for collision detection)
    bool aabb_overlaps_aabb(const AABB& a, const AABB& b) const;

    // Recursive helper for query_aabb
    void query_aabb_recursive(int node_index, const AABB& query_box,
                             const std::vector<Particle>& particles,
                             std::vector<int>& out_particle_indices) const;

    // Ray-AABB intersection test
    bool ray_intersects_aabb(float ray_origin_x, float ray_origin_y, float ray_origin_z,
                            float ray_dir_x, float ray_dir_y, float ray_dir_z,
                            const AABB& box, float max_t) const;
    
    // Ray-AABB intersection with entry distance
    bool ray_intersects_aabb_with_dist(float ray_origin_x, float ray_origin_y, float ray_origin_z,
                            float ray_dir_x, float ray_dir_y, float ray_dir_z,
                            const AABB& box, float max_t, float& out_tmin) const;
    
    // (The old #ifdef __SSE__ 4-ray SIMD batch helpers were deleted:
    //  nothing called them and they referenced a BVH node layout that no
    //  longer exists — they could not have compiled on x86 since that
    //  layout changed. The portable traversal above is the only path.)
};

#endif // BVH_H