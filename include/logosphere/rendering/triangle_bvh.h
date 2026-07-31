#ifndef TRIANGLE_BVH_H
#define TRIANGLE_BVH_H

#include <vector>
#include "logosphere/physics/bvh.h"  // Reuse AABB

// Forward declaration
namespace Logosphere {
    struct ShadowTriangle;
}

// GPU-optimized BVH node (48 bytes, aligned for GPU)
struct alignas(16) GPUBVHNode {
    float bbox_min[3];   // 12 bytes
    float _pad0;         // 4 bytes (align to 16)
    float bbox_max[3];   // 12 bytes
    float _pad1;         // 4 bytes (align to 16)
    int left_child;      // 4 bytes (-1 if leaf)
    int right_child;     // 4 bytes (-1 if leaf)
    int triangle_idx;    // 4 bytes (leaf: triangle index, internal: -1)
    int _pad2;          // 4 bytes (total: 48)

    GPUBVHNode() : left_child(-1), right_child(-1), triangle_idx(-1) {
        bbox_min[0] = bbox_min[1] = bbox_min[2] = 0;
        bbox_max[0] = bbox_max[1] = bbox_max[2] = 0;
        _pad0 = _pad1 = _pad2 = 0;
    }

    bool is_leaf() const { return triangle_idx >= 0; }
};

// Triangle BVH - GPU-specific, optimized for upload
class TriangleBVH {
private:
    std::vector<GPUBVHNode> nodes_;
    std::vector<int> parent_indices_;  // NEW: Parent pointer for each node (for ancestor refitting)
    int root_idx_;
    bool is_built_;

    // NEW: Triangle storage for incremental mode
    std::vector<Logosphere::ShadowTriangle> triangles_;  // All triangles in BVH
    std::vector<int> triangle_to_leaf_;  // triangle_idx → node_idx mapping

    // Recursive builder
    int build_recursive(const std::vector<AABB>& boxes,
                       std::vector<int>& indices,
                       int start, int end);

    // Reorder nodes to breadth-first layout for better GPU cache locality
    void reorder_to_breadth_first();

    // NEW: Incremental insertion helpers
    int find_best_sibling(const AABB& new_bounds);
    float compute_sah_cost(int node_idx) const;
    void refit_ancestors(int leaf_idx);
    int create_parent_node(int sibling_idx, int new_leaf_idx);
    AABB get_node_aabb(int node_idx) const;

public:
    TriangleBVH() : root_idx_(-1), is_built_(false) {}

    // Build from GPU triangles (batch construction - legacy mode)
    void build(const std::vector<Logosphere::ShadowTriangle>& triangles);

    // Refit BVH AABBs without changing tree structure (10-20x faster than rebuild)
    void refit(const std::vector<Logosphere::ShadowTriangle>& triangles);  // Full refit: all nodes
    void refit();  // Use internal triangle storage (for incremental mode)

    // Selective refit: only dirty triangles + their ancestor chains.
    // dirty_triangle_indices contains indices into the triangles vector.
    // Falls back to full refit if triangle_to_leaf_ mapping is empty.
    void refit_dirty(const std::vector<Logosphere::ShadowTriangle>& triangles,
                     const std::vector<int>& dirty_triangle_indices);

    // NEW: Incremental insertion (for chunk loading)
    void insert(const Logosphere::ShadowTriangle& triangle);
    void insert_batch(const std::vector<Logosphere::ShadowTriangle>& triangles);

    // NEW: Query triangle count (for incremental mode)
    int get_triangle_count() const { return static_cast<int>(triangles_.size()); }

    // Clear
    void clear();

    // Access for GPU upload
    const GPUBVHNode* get_nodes() const { return nodes_.data(); }
    int get_node_count() const { return static_cast<int>(nodes_.size()); }
    const Logosphere::ShadowTriangle* get_triangles() const { return triangles_.data(); }
    bool is_ready() const { return is_built_; }

    // Stats
    int get_depth() const;
};

#endif // TRIANGLE_BVH_H
