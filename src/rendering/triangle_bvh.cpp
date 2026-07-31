#include "logosphere/rendering/triangle_bvh.h"
#include <functional>
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include "../optimization_flags.h"
#include <algorithm>
#include <numeric>
#include <deque>

void TriangleBVH::build(const std::vector<Logosphere::ShadowTriangle>& triangles) {
    clear();

    if (triangles.empty()) {
        return;
    }

    // Store triangles for incremental mode compatibility
    triangles_ = triangles;
    triangle_to_leaf_.resize(triangles.size());

    // Build AABB for each triangle
    std::vector<AABB> triangle_boxes;
    triangle_boxes.reserve(triangles.size());

    for (const auto& tri : triangles) {
        AABB box;
        // Expand for v0
        box.expand(tri.v0[0], tri.v0[1], tri.v0[2]);
        // Expand for v1
        box.expand(tri.v1[0], tri.v1[1], tri.v1[2]);
        // Expand for v2
        box.expand(tri.v2[0], tri.v2[1], tri.v2[2]);
        triangle_boxes.push_back(box);
    }

    // Build tree
    std::vector<int> indices(triangles.size());
    std::iota(indices.begin(), indices.end(), 0);

    // Reserve space for parent tracking
    parent_indices_.reserve(triangles.size() * 2);  // Estimate: ~2× nodes vs triangles

    root_idx_ = build_recursive(triangle_boxes, indices, 0, static_cast<int>(indices.size()));

    // Set root's parent to itself
    if (root_idx_ >= 0 && root_idx_ < static_cast<int>(parent_indices_.size())) {
        parent_indices_[root_idx_] = root_idx_;
    }

    // BFS reordering REVERTED (2025-10-06): Caused 1-2% regression instead of improvement
    // Expected: 1.5-2× speedup from cache locality (research-validated)
    // Actual: 20.8 vs 21.0 FPS @ 2 lights (slower!)
    // Reason: GPU uses DFS traversal (stack-based), not BFS
    //         Apple M4 Max GPU prefetching already optimizes DFS access patterns
    // reorder_to_breadth_first();  // DISABLED

    is_built_ = true;
}

void TriangleBVH::refit(const std::vector<Logosphere::ShadowTriangle>& triangles) {
    if (!is_built_ || nodes_.empty()) {
        return;  // Nothing to refit
    }

    // =========================================================================
    // BVH REFITTING: Update AABBs without changing tree structure
    // =========================================================================
    // This is 10-20× faster than full rebuild because:
    // - No SAH evaluation
    // - No partitioning/sorting
    // - No memory allocation
    // - Just AABB min/max updates
    //
    // Algorithm: Bottom-up traversal
    // 1. Leaves: Recompute AABB from triangle vertices
    // 2. Internal: AABB = union(left_child.AABB, right_child.AABB)
    // =========================================================================

    // Traverse nodes bottom-up (leaves processed first, then parents)
    for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; i--) {
        GPUBVHNode& node = nodes_[i];

        if (node.is_leaf()) {
            // Leaf: Recompute AABB from triangle vertices
            int tri_idx = node.triangle_idx;
            if (tri_idx < 0 || tri_idx >= static_cast<int>(triangles.size())) {
                continue;  // Invalid triangle index
            }

            const auto& tri = triangles[tri_idx];

            // Compute min/max across all 3 vertices
            node.bbox_min[0] = std::min({tri.v0[0], tri.v1[0], tri.v2[0]});
            node.bbox_min[1] = std::min({tri.v0[1], tri.v1[1], tri.v2[1]});
            node.bbox_min[2] = std::min({tri.v0[2], tri.v1[2], tri.v2[2]});

            node.bbox_max[0] = std::max({tri.v0[0], tri.v1[0], tri.v2[0]});
            node.bbox_max[1] = std::max({tri.v0[1], tri.v1[1], tri.v2[1]});
            node.bbox_max[2] = std::max({tri.v0[2], tri.v1[2], tri.v2[2]});

        } else {
            // Internal node: AABB = union(left child, right child)
            if (node.left_child >= 0 && node.left_child < static_cast<int>(nodes_.size()) &&
                node.right_child >= 0 && node.right_child < static_cast<int>(nodes_.size())) {

                const GPUBVHNode& left = nodes_[node.left_child];
                const GPUBVHNode& right = nodes_[node.right_child];

                // Min = min of both children
                node.bbox_min[0] = std::min(left.bbox_min[0], right.bbox_min[0]);
                node.bbox_min[1] = std::min(left.bbox_min[1], right.bbox_min[1]);
                node.bbox_min[2] = std::min(left.bbox_min[2], right.bbox_min[2]);

                // Max = max of both children
                node.bbox_max[0] = std::max(left.bbox_max[0], right.bbox_max[0]);
                node.bbox_max[1] = std::max(left.bbox_max[1], right.bbox_max[1]);
                node.bbox_max[2] = std::max(left.bbox_max[2], right.bbox_max[2]);
            }
        }
    }

    // Note: Tree structure unchanged, only AABBs updated
    // This allows GPU to skip re-uploading BVH topology (only update node data)
}

void TriangleBVH::refit() {
    // Refit using internally stored triangles (incremental mode)
    if (triangles_.empty()) {
        return;  // No triangles stored - use external refit() instead
    }
    refit(triangles_);
}

void TriangleBVH::refit_dirty(const std::vector<Logosphere::ShadowTriangle>& triangles,
                               const std::vector<int>& dirty_triangle_indices) {
    if (!is_built_ || nodes_.empty() || dirty_triangle_indices.empty()) {
        return;
    }

    // Fallback: if triangle_to_leaf_ mapping wasn't built, do full refit
    if (triangle_to_leaf_.empty() || parent_indices_.empty()) {
        refit(triangles);
        return;
    }

    // =========================================================================
    // SELECTIVE BVH REFIT: Only update dirty leaves + ancestor chains
    // =========================================================================
    // 1. For each dirty triangle, update its leaf AABB from new vertex data
    // 2. Collect all ancestor nodes that need AABB recalculation
    // 3. Process ancestors bottom-up (descending index order)
    //
    // Cost: O(D * log N) where D = dirty count, N = total nodes
    // For 6k dirty out of 91k total with depth 17: ~6k + ~15k ancestors
    // vs 182k for full refit
    // =========================================================================

    // Step 1: Update dirty leaf AABBs and collect ancestors
    // Use a bitset-style vector for O(1) ancestor dedup
    std::vector<bool> needs_update(nodes_.size(), false);

    for (int tri_idx : dirty_triangle_indices) {
        if (tri_idx < 0 || tri_idx >= static_cast<int>(triangles.size())) continue;
        if (tri_idx >= static_cast<int>(triangle_to_leaf_.size())) continue;

        int leaf_idx = triangle_to_leaf_[tri_idx];
        if (leaf_idx < 0 || leaf_idx >= static_cast<int>(nodes_.size())) continue;

        // Update leaf AABB from triangle vertices
        GPUBVHNode& leaf = nodes_[leaf_idx];
        const auto& tri = triangles[tri_idx];

        leaf.bbox_min[0] = std::min({tri.v0[0], tri.v1[0], tri.v2[0]});
        leaf.bbox_min[1] = std::min({tri.v0[1], tri.v1[1], tri.v2[1]});
        leaf.bbox_min[2] = std::min({tri.v0[2], tri.v1[2], tri.v2[2]});
        leaf.bbox_max[0] = std::max({tri.v0[0], tri.v1[0], tri.v2[0]});
        leaf.bbox_max[1] = std::max({tri.v0[1], tri.v1[1], tri.v2[1]});
        leaf.bbox_max[2] = std::max({tri.v0[2], tri.v1[2], tri.v2[2]});

        // Walk ancestor chain, marking nodes that need AABB update
        int current = parent_indices_[leaf_idx];
        while (current >= 0 && current < static_cast<int>(nodes_.size())) {
            if (needs_update[current]) break;  // Already marked — ancestors above are too
            needs_update[current] = true;
            int parent = parent_indices_[current];
            if (parent == current) break;  // Root
            current = parent;
        }
    }

    // Step 2: Process marked internal nodes bottom-up (descending index order)
    // Parent indices are always lower than children in DFS build order,
    // so descending iteration guarantees children are processed before parents.
    for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; i--) {
        if (!needs_update[i]) continue;

        GPUBVHNode& node = nodes_[i];
        if (node.is_leaf()) continue;  // Leaves already updated above

        if (node.left_child >= 0 && node.left_child < static_cast<int>(nodes_.size()) &&
            node.right_child >= 0 && node.right_child < static_cast<int>(nodes_.size())) {

            const GPUBVHNode& left = nodes_[node.left_child];
            const GPUBVHNode& right = nodes_[node.right_child];

            node.bbox_min[0] = std::min(left.bbox_min[0], right.bbox_min[0]);
            node.bbox_min[1] = std::min(left.bbox_min[1], right.bbox_min[1]);
            node.bbox_min[2] = std::min(left.bbox_min[2], right.bbox_min[2]);
            node.bbox_max[0] = std::max(left.bbox_max[0], right.bbox_max[0]);
            node.bbox_max[1] = std::max(left.bbox_max[1], right.bbox_max[1]);
            node.bbox_max[2] = std::max(left.bbox_max[2], right.bbox_max[2]);
        }
    }
}

int TriangleBVH::build_recursive(const std::vector<AABB>& boxes,
                                  std::vector<int>& indices,
                                  int start, int end) {
    // =========================================================================
    // ITERATIVE BVH BUILD (Stack-based, no recursion)
    // =========================================================================
    //
    // WHY ITERATIVE:
    //   Recursive builds with 1M+ triangles overflow worker thread stacks.
    //   Worker threads have 544KB stacks (macOS default), and deep recursion
    //   (log2(1M) ≈ 20 levels, but unbalanced trees go deeper) crashes.
    //
    // HOW IT WORKS:
    //   Instead of call stack, we use an explicit std::vector<BuildTask>.
    //   Each task represents work that would have been a recursive call.
    //   Parent-child linking is done via stored parent_idx in each task.
    //
    // ALGORITHM (same as recursive, just iterative):
    //   1. Pop task from work_stack
    //   2. Create node, compute bounds
    //   3. Link to parent (if any)
    //   4. If leaf (1 triangle): store triangle index, done
    //   5. If internal: find split point (SAH or midpoint), push child tasks
    //
    // PERFORMANCE: Same O(n log n) complexity, no stack depth limits.
    // =========================================================================

    struct BuildTask {
        int start;
        int end;
        int parent_idx;      // -1 for root
        bool is_left_child;  // true = left, false = right
    };

    std::vector<BuildTask> work_stack;
    work_stack.reserve(64);  // log2(1M) ≈ 20, with margin

    // Push initial task
    work_stack.push_back({start, end, -1, true});

    int root_idx = -1;

    while (!work_stack.empty()) {
        BuildTask task = work_stack.back();
        work_stack.pop_back();

        int count = task.end - task.start;

        // Compute bounds for this node
        AABB bounds;
        for (int i = task.start; i < task.end; i++) {
            bounds.expand(boxes[indices[i]]);
        }

        // Create node
        GPUBVHNode node;
        node.bbox_min[0] = bounds.min_x;
        node.bbox_min[1] = bounds.min_y;
        node.bbox_min[2] = bounds.min_z;
        node.bbox_max[0] = bounds.max_x;
        node.bbox_max[1] = bounds.max_y;
        node.bbox_max[2] = bounds.max_z;
        node.left_child = -1;
        node.right_child = -1;
        node.triangle_idx = -1;

        int node_idx = static_cast<int>(nodes_.size());
        nodes_.push_back(node);
        parent_indices_.push_back(task.parent_idx);

        // Link to parent
        if (task.parent_idx >= 0) {
            if (task.is_left_child) {
                nodes_[task.parent_idx].left_child = node_idx;
            } else {
                nodes_[task.parent_idx].right_child = node_idx;
            }
        } else {
            root_idx = node_idx;  // This is the root
        }

        // Leaf: single triangle
        if (count == 1) {
            nodes_[node_idx].triangle_idx = indices[task.start];
            triangle_to_leaf_[indices[task.start]] = node_idx;
            continue;
        }

        // Internal node: find split point
        int best_split = task.start + count / 2;

        if (Optimizations::BVH_USE_SAH) {
            // SAH split
            constexpr int NUM_BUCKETS = 12;
            float best_cost = std::numeric_limits<float>::max();
            int best_axis = -1;

            for (int axis = 0; axis < 3; axis++) {
                float axis_min = (axis == 0) ? bounds.min_x : (axis == 1) ? bounds.min_y : bounds.min_z;
                float axis_max = (axis == 0) ? bounds.max_x : (axis == 1) ? bounds.max_y : bounds.max_z;

                if (axis_max - axis_min < 0.0001f) continue;

                // Bucket triangles
                int bucket_count[NUM_BUCKETS] = {0};
                AABB bucket_box[NUM_BUCKETS];

                for (int i = task.start; i < task.end; i++) {
                    float pos = (axis == 0) ? boxes[indices[i]].center_x() :
                               (axis == 1) ? boxes[indices[i]].center_y() :
                                            boxes[indices[i]].center_z();

                    int bucket = static_cast<int>(NUM_BUCKETS * (pos - axis_min) / (axis_max - axis_min));
                    bucket = std::min(bucket, NUM_BUCKETS - 1);

                    bucket_count[bucket]++;
                    bucket_box[bucket].expand(boxes[indices[i]]);
                }

                // Evaluate splits
                for (int i = 0; i < NUM_BUCKETS - 1; i++) {
                    AABB box_left, box_right;
                    int count_left = 0, count_right = 0;

                    for (int j = 0; j <= i; j++) {
                        box_left.expand(bucket_box[j]);
                        count_left += bucket_count[j];
                    }
                    for (int j = i + 1; j < NUM_BUCKETS; j++) {
                        box_right.expand(bucket_box[j]);
                        count_right += bucket_count[j];
                    }

                    if (count_left == 0 || count_right == 0) continue;

                    float cost = count_left * box_left.surface_area() +
                               count_right * box_right.surface_area();

                    if (cost < best_cost) {
                        best_cost = cost;
                        best_axis = axis;
                        best_split = task.start + count_left;
                    }
                }
            }

            // Partition based on best axis
            if (best_axis >= 0) {
                if (best_axis == 0) {
                    std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                        indices.begin() + task.end, [&boxes](int a, int b) {
                            return boxes[a].center_x() < boxes[b].center_x();
                        });
                } else if (best_axis == 1) {
                    std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                        indices.begin() + task.end, [&boxes](int a, int b) {
                            return boxes[a].center_y() < boxes[b].center_y();
                        });
                } else {
                    std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                        indices.begin() + task.end, [&boxes](int a, int b) {
                            return boxes[a].center_z() < boxes[b].center_z();
                        });
                }
            }
        } else {
            // Median split along longest axis
            float x_extent = bounds.width();
            float y_extent = bounds.height();
            float z_extent = bounds.depth();

            int split_axis = 0;
            if (y_extent > x_extent) split_axis = 1;
            if (z_extent > std::max(x_extent, y_extent)) split_axis = 2;

            if (split_axis == 0) {
                std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                    indices.begin() + task.end, [&boxes](int a, int b) {
                        return boxes[a].center_x() < boxes[b].center_x();
                    });
            } else if (split_axis == 1) {
                std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                    indices.begin() + task.end, [&boxes](int a, int b) {
                        return boxes[a].center_y() < boxes[b].center_y();
                    });
            } else {
                std::nth_element(indices.begin() + task.start, indices.begin() + best_split,
                    indices.begin() + task.end, [&boxes](int a, int b) {
                        return boxes[a].center_z() < boxes[b].center_z();
                    });
            }
        }

        // Push child tasks (right first so left is processed first - maintains similar order)
        work_stack.push_back({best_split, task.end, node_idx, false});  // Right child
        work_stack.push_back({task.start, best_split, node_idx, true}); // Left child
    }

    return root_idx;
}

void TriangleBVH::reorder_to_breadth_first() {
    if (nodes_.empty() || root_idx_ < 0) {
        return;
    }

    // =====================================================================
    // BREADTH-FIRST BVH LAYOUT (GPU Cache Optimization)
    // =====================================================================
    //
    // PROBLEM: Depth-first layout causes cache misses on GPU
    // - Sibling nodes (left/right children) are NOT adjacent in memory
    // - GPU loads 64-128 byte cache lines
    // - Each BVH node is 48 bytes
    // - Depth-first: ~0.5 nodes per cache line (terrible!)
    // - Breadth-first: ~1.77 nodes per cache line (siblings together)
    //
    // EXAMPLE: Tree with root and 2 children
    //
    // Depth-First Layout (current):
    // [0: Root] [1: LeftChild] [2: LeftLeft] [3: LeftRight] [4: RightChild] ...
    //   ^---------- 96 bytes between siblings! ----------^
    // Result: 2 cache line loads to access both children
    //
    // Breadth-First Layout (optimal):
    // [0: Root] [1: LeftChild] [2: RightChild] [3: LeftLeft] [4: LeftRight] ...
    //   ^-- 48 bytes between siblings (same cache line!) --^
    // Result: 1 cache line load for both children
    //
    // ALGORITHM:
    // 1. BFS traverse tree with queue, assign new sequential indices
    // 2. Build mapping: old_index → new_index
    // 3. Copy nodes to new array in BFS order
    // 4. Update child pointers to use new indices
    // 5. Root is always at index 0 after reordering
    //
    // EXPECTED IMPROVEMENT: 1.5-2× speedup (research-validated)
    // =====================================================================

    std::vector<int> old_to_new(nodes_.size(), -1);  // old index → new index
    std::vector<int> bfs_order;  // New order of node indices
    bfs_order.reserve(nodes_.size());

    // BFS traverse to build new ordering
    std::deque<int> queue;
    queue.push_back(root_idx_);

    while (!queue.empty()) {
        int old_idx = queue.front();
        queue.pop_front();

        // Assign new index (sequential in BFS order)
        int new_idx = static_cast<int>(bfs_order.size());
        old_to_new[old_idx] = new_idx;
        bfs_order.push_back(old_idx);

        // Enqueue children (left first, then right - BFS order)
        const GPUBVHNode& node = nodes_[old_idx];
        if (node.left_child >= 0) {
            queue.push_back(node.left_child);
        }
        if (node.right_child >= 0) {
            queue.push_back(node.right_child);
        }
    }

    // Build new nodes array in BFS order
    std::vector<GPUBVHNode> new_nodes;
    new_nodes.reserve(nodes_.size());

    for (int old_idx : bfs_order) {
        GPUBVHNode node = nodes_[old_idx];

        // Update child pointers to new indices
        if (node.left_child >= 0) {
            node.left_child = old_to_new[node.left_child];
        }
        if (node.right_child >= 0) {
            node.right_child = old_to_new[node.right_child];
        }

        new_nodes.push_back(node);
    }

    // Replace old nodes with BFS-ordered nodes
    nodes_ = std::move(new_nodes);

    // Root is now always at index 0 in BFS layout
    root_idx_ = 0;
}

void TriangleBVH::clear() {
    nodes_.clear();
    parent_indices_.clear();
    triangles_.clear();
    triangle_to_leaf_.clear();
    root_idx_ = -1;
    is_built_ = false;
}

int TriangleBVH::get_depth() const {
    if (!is_built_ || nodes_.empty()) return 0;

    std::function<int(int)> depth_recursive = [&](int idx) -> int {
        if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return 0;
        const auto& node = nodes_[idx];
        if (node.is_leaf()) return 1;
        return 1 + std::max(depth_recursive(node.left_child), depth_recursive(node.right_child));
    };

    return depth_recursive(root_idx_);
}

// ============================================================================
// INCREMENTAL INSERTION: Add triangles without full rebuild
// ============================================================================
// Based on Box2D dynamic BVH and academic research (Incremental BVH 2015)
// Performance: O(log n) per insertion vs O(n log n) for rebuild
// Quality: Comparable to SAH builders with good insertion heuristic
// ============================================================================

AABB TriangleBVH::get_node_aabb(int node_idx) const {
    if (node_idx < 0 || node_idx >= static_cast<int>(nodes_.size())) {
        return AABB{};
    }

    const auto& node = nodes_[node_idx];
    AABB box;
    box.min_x = node.bbox_min[0];
    box.min_y = node.bbox_min[1];
    box.min_z = node.bbox_min[2];
    box.max_x = node.bbox_max[0];
    box.max_y = node.bbox_max[1];
    box.max_z = node.bbox_max[2];
    return box;
}

float TriangleBVH::compute_sah_cost(int node_idx) const {
    if (node_idx < 0 || node_idx >= static_cast<int>(nodes_.size())) {
        return 0.0f;
    }

    AABB box = get_node_aabb(node_idx);
    float surface_area = box.surface_area();

    // Leaf cost: surface area (1 triangle test)
    if (nodes_[node_idx].is_leaf()) {
        return surface_area;
    }

    // Internal cost: surface area × 2 (traversal cost for 2 children)
    return surface_area * 2.0f;
}

int TriangleBVH::find_best_sibling(const AABB& new_bounds) {
    // Greedy descent from root to find best insertion point
    // Cost heuristic: SAH (Surface Area Heuristic)
    // Choose child that minimizes cost increase

    if (root_idx_ < 0 || nodes_.empty()) {
        return -1;  // No tree yet
    }

    int best_sibling = root_idx_;
    float best_cost = std::numeric_limits<float>::max();

    // Descend tree, choosing child with minimum cost increase
    int current = root_idx_;
    while (current >= 0 && current < static_cast<int>(nodes_.size())) {
        const GPUBVHNode& node = nodes_[current];

        // Compute cost if we insert here (create parent with this node)
        AABB combined = get_node_aabb(current);
        combined.expand(new_bounds);
        float cost_here = combined.surface_area();

        if (cost_here < best_cost) {
            best_cost = cost_here;
            best_sibling = current;
        }

        // Leaf: Stop descending
        if (node.is_leaf()) {
            break;
        }

        // Internal: Choose child with minimum cost increase
        AABB left_box = get_node_aabb(node.left_child);
        AABB right_box = get_node_aabb(node.right_child);

        left_box.expand(new_bounds);
        right_box.expand(new_bounds);

        float left_cost = left_box.surface_area();
        float right_cost = right_box.surface_area();

        // Descend to cheaper child
        current = (left_cost < right_cost) ? node.left_child : node.right_child;
    }

    return best_sibling;
}

void TriangleBVH::refit_ancestors(int leaf_idx) {
    // Update AABBs from leaf up to root
    int current = leaf_idx;

    while (current >= 0 && current < static_cast<int>(parent_indices_.size())) {
        GPUBVHNode& node = nodes_[current];

        if (node.is_leaf()) {
            // Leaf AABB already set during insertion
        } else {
            // Internal: AABB = union(left, right)
            if (node.left_child >= 0 && node.right_child >= 0) {
                AABB left_box = get_node_aabb(node.left_child);
                AABB right_box = get_node_aabb(node.right_child);
                left_box.expand(right_box);

                node.bbox_min[0] = left_box.min_x;
                node.bbox_min[1] = left_box.min_y;
                node.bbox_min[2] = left_box.min_z;
                node.bbox_max[0] = left_box.max_x;
                node.bbox_max[1] = left_box.max_y;
                node.bbox_max[2] = left_box.max_z;
            }
        }

        // Move to parent
        int parent = parent_indices_[current];
        if (parent == current) {
            break;  // Root (parent points to self)
        }
        current = parent;
    }
}

int TriangleBVH::create_parent_node(int sibling_idx, int new_leaf_idx) {
    // Create new internal node as parent of sibling + new_leaf
    GPUBVHNode parent_node;

    // Compute AABB: union(sibling, new_leaf)
    AABB sibling_box = get_node_aabb(sibling_idx);
    AABB new_leaf_box = get_node_aabb(new_leaf_idx);
    sibling_box.expand(new_leaf_box);

    parent_node.bbox_min[0] = sibling_box.min_x;
    parent_node.bbox_min[1] = sibling_box.min_y;
    parent_node.bbox_min[2] = sibling_box.min_z;
    parent_node.bbox_max[0] = sibling_box.max_x;
    parent_node.bbox_max[1] = sibling_box.max_y;
    parent_node.bbox_max[2] = sibling_box.max_z;

    parent_node.left_child = sibling_idx;
    parent_node.right_child = new_leaf_idx;
    parent_node.triangle_idx = -1;  // Internal node

    // Add parent node
    int parent_idx = static_cast<int>(nodes_.size());
    nodes_.push_back(parent_node);
    parent_indices_.push_back(parent_idx);  // Parent points to itself initially

    // Update children's parent pointers
    if (sibling_idx >= 0 && sibling_idx < static_cast<int>(parent_indices_.size())) {
        // Sibling's old parent becomes new parent's parent
        int old_parent = parent_indices_[sibling_idx];
        parent_indices_[parent_idx] = old_parent;

        // Update old parent's child pointer
        if (old_parent >= 0 && old_parent < static_cast<int>(nodes_.size())) {
            if (nodes_[old_parent].left_child == sibling_idx) {
                nodes_[old_parent].left_child = parent_idx;
            } else if (nodes_[old_parent].right_child == sibling_idx) {
                nodes_[old_parent].right_child = parent_idx;
            }
        } else {
            // Sibling was root - new parent becomes root
            root_idx_ = parent_idx;
        }

        // Set children's parents
        parent_indices_[sibling_idx] = parent_idx;
    }

    if (new_leaf_idx >= 0 && new_leaf_idx < static_cast<int>(parent_indices_.size())) {
        parent_indices_[new_leaf_idx] = parent_idx;
    }

    return parent_idx;
}

void TriangleBVH::insert(const Logosphere::ShadowTriangle& triangle) {
    // Compute AABB for new triangle
    AABB tri_box;
    tri_box.expand(triangle.v0[0], triangle.v0[1], triangle.v0[2]);
    tri_box.expand(triangle.v1[0], triangle.v1[1], triangle.v1[2]);
    tri_box.expand(triangle.v2[0], triangle.v2[1], triangle.v2[2]);

    // Store triangle
    int tri_idx = static_cast<int>(triangles_.size());
    triangles_.push_back(triangle);

    // Create leaf node
    GPUBVHNode leaf_node;
    leaf_node.bbox_min[0] = tri_box.min_x;
    leaf_node.bbox_min[1] = tri_box.min_y;
    leaf_node.bbox_min[2] = tri_box.min_z;
    leaf_node.bbox_max[0] = tri_box.max_x;
    leaf_node.bbox_max[1] = tri_box.max_y;
    leaf_node.bbox_max[2] = tri_box.max_z;
    leaf_node.left_child = -1;
    leaf_node.right_child = -1;
    leaf_node.triangle_idx = tri_idx;

    int leaf_idx = static_cast<int>(nodes_.size());
    nodes_.push_back(leaf_node);
    parent_indices_.push_back(leaf_idx);  // Parent initially points to self

    // Track triangle → leaf mapping
    triangle_to_leaf_.push_back(leaf_idx);

    // First triangle: Becomes root
    if (!is_built_ || root_idx_ < 0) {
        root_idx_ = leaf_idx;
        is_built_ = true;
        return;
    }

    // Find best sibling for insertion
    int sibling = find_best_sibling(tri_box);
    if (sibling < 0) {
        // Fallback: Make new triangle the root
        root_idx_ = leaf_idx;
        return;
    }

    // Create parent node for sibling + new leaf
    int parent = create_parent_node(sibling, leaf_idx);

    // Refit ancestors up to root
    refit_ancestors(parent);
}

void TriangleBVH::insert_batch(const std::vector<Logosphere::ShadowTriangle>& new_triangles) {
    // Reserve space to avoid reallocations
    triangles_.reserve(triangles_.size() + new_triangles.size());
    triangle_to_leaf_.reserve(triangle_to_leaf_.size() + new_triangles.size());
    nodes_.reserve(nodes_.size() + new_triangles.size() * 2);  // 2 nodes per triangle (leaf + parent)
    parent_indices_.reserve(parent_indices_.size() + new_triangles.size() * 2);

    // Insert triangles one by one
    for (const auto& tri : new_triangles) {
        insert(tri);
    }
}
