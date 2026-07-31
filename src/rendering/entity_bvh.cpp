#include "logosphere/rendering/entity_bvh.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include "../optimization_flags.h"
#include <algorithm>
#include <cmath>
#include <limits>

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

// Compute triangle normal from vertices
void compute_triangle_normal(const Logosphere::ShadowTriangle& tri,
                             float& nx, float& ny, float& nz) {
    // Edge vectors
    float e1_x = tri.v1[0] - tri.v0[0];
    float e1_y = tri.v1[1] - tri.v0[1];
    float e1_z = tri.v1[2] - tri.v0[2];

    float e2_x = tri.v2[0] - tri.v0[0];
    float e2_y = tri.v2[1] - tri.v0[1];
    float e2_z = tri.v2[2] - tri.v0[2];

    // Cross product
    nx = e1_y * e2_z - e1_z * e2_y;
    ny = e1_z * e2_x - e1_x * e2_z;
    nz = e1_x * e2_y - e1_y * e2_x;

    // Normalize
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 0.0001f) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
}

// Get AABB from triangle
AABB triangle_to_aabb(const Logosphere::ShadowTriangle& tri) {
    AABB box;
    box.expand(tri.v0[0], tri.v0[1], tri.v0[2]);
    box.expand(tri.v1[0], tri.v1[1], tri.v1[2]);
    box.expand(tri.v2[0], tri.v2[1], tri.v2[2]);
    return box;
}

}  // anonymous namespace

// =============================================================================
// EntityBVH Implementation
// =============================================================================

void EntityBVH::clear() {
    nodes_.clear();
    parent_indices_.clear();
    groups_.clear();
    triangles_.clear();
    entity_to_leaf_.clear();
    dirty_entities_.clear();
    root_idx_ = -1;
    is_built_ = false;
}

void EntityBVH::build(const std::vector<EntityTriangleData>& entities) {
    clear();

    if (entities.empty()) {
        return;
    }

    // KNOWN DEFECT (2026-07-30 hot-path scan, not yet fixed): this deep-copies
    // every EntityTriangleData, and each carries a std::vector<ShadowTriangle>.
    // At 470k shadow triangles that is ~30 MB of memcpy plus two allocations
    // per entity, every frame, purely so classify_triangles can fill
    // `triangle_directions`. `triangles` is only ever read. The fix is to keep
    // directions in a parallel array and pass `entities` const throughout,
    // which means changing build_recursive and create_directional_groups.
    // Same defect class as the icosphere rebuild and get_world_faces.
    std::vector<EntityTriangleData> processed_entities = entities;
    for (auto& entity : processed_entities) {
        classify_triangles(entity);
    }

    // Create indices for building
    std::vector<int> indices(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }

    // Reserve space for nodes (worst case: 2N-1 nodes)
    nodes_.reserve(2 * entities.size());
    parent_indices_.reserve(2 * entities.size());

    // Build BVH
    root_idx_ = build_recursive(processed_entities, indices, 0, static_cast<int>(indices.size()));

    // Create directional groups and pack triangles
    int group_start = 0;
    int tri_start = 0;

    for (int idx : indices) {
        const auto& entity = processed_entities[idx];

        // Find the leaf node for this entity
        int leaf_idx = -1;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].entity_id == static_cast<int>(entity.entity_id)) {
                leaf_idx = static_cast<int>(i);
                break;
            }
        }

        if (leaf_idx >= 0) {
            create_directional_groups(entity, group_start, tri_start);
            nodes_[leaf_idx].dir_group_start = group_start - nodes_[leaf_idx].dir_group_count;
        }
    }

    is_built_ = true;
}

void EntityBVH::classify_triangles(EntityTriangleData& entity) {
    entity.triangle_directions.resize(entity.triangles.size());

    for (size_t i = 0; i < entity.triangles.size(); ++i) {
        float nx, ny, nz;
        compute_triangle_normal(entity.triangles[i], nx, ny, nz);
        entity.triangle_directions[i] = EntityBVHDirection::classify_normal(nx, ny, nz);
    }
}

void EntityBVH::create_directional_groups(const EntityTriangleData& entity,
                                          int& group_start_out, int& tri_start_out) {
    // Count triangles per direction
    int counts[EntityBVHDirection::COUNT] = {0};
    for (int dir : entity.triangle_directions) {
        counts[dir]++;
    }

    // Compute average normal per group and create groups
    for (int dir = 0; dir < EntityBVHDirection::COUNT; ++dir) {
        if (counts[dir] > 0) {
            // Accumulate normals for this direction
            float sum_nx = 0, sum_ny = 0, sum_nz = 0;
            for (size_t i = 0; i < entity.triangles.size(); ++i) {
                if (entity.triangle_directions[i] == dir) {
                    float nx, ny, nz;
                    compute_triangle_normal(entity.triangles[i], nx, ny, nz);
                    sum_nx += nx;
                    sum_ny += ny;
                    sum_nz += nz;
                }
            }

            // Normalize average
            float len = std::sqrt(sum_nx*sum_nx + sum_ny*sum_ny + sum_nz*sum_nz);
            if (len > 0.0001f) {
                sum_nx /= len;
                sum_ny /= len;
                sum_nz /= len;
            }

            // Create group
            DirectionalGroup group(sum_nx, sum_ny, sum_nz,
                                   tri_start_out, counts[dir]);
            groups_.push_back(group);
            group_start_out++;

            // Copy triangles for this direction
            for (size_t i = 0; i < entity.triangles.size(); ++i) {
                if (entity.triangle_directions[i] == dir) {
                    triangles_.push_back(entity.triangles[i]);
                    tri_start_out++;
                }
            }
        }
    }
}

int EntityBVH::build_recursive(const std::vector<EntityTriangleData>& entities,
                               std::vector<int>& indices, int start, int end) {
    int node_idx = static_cast<int>(nodes_.size());
    nodes_.push_back(EntityBVHNode());
    parent_indices_.push_back(-1);  // Will be set by parent

    EntityBVHNode& node = nodes_[node_idx];

    // Calculate bounds for all entities in this range
    AABB bounds;
    for (int i = start; i < end; ++i) {
        bounds.expand(entities[indices[i]].bbox);
    }
    node.set_bounds(bounds);

    int count = end - start;

    // If only one entity, make it a leaf
    if (count == 1) {
        const auto& entity = entities[indices[start]];
        node.entity_id = static_cast<int>(entity.entity_id);
        node.left_child = -1;
        node.right_child = -1;

        // Count directional groups (will be filled in during group creation)
        int group_count = 0;
        for (int dir = 0; dir < EntityBVHDirection::COUNT; ++dir) {
            bool has_dir = false;
            for (int d : entity.triangle_directions) {
                if (d == dir) { has_dir = true; break; }
            }
            if (has_dir) group_count++;
        }
        node.dir_group_count = group_count;

        // Track entity → leaf mapping
        entity_to_leaf_[entity.entity_id] = node_idx;

        return node_idx;
    }

    // SAH-based splitting (simplified for entities)
    if (Optimizations::BVH_USE_SAH && count > 4) {
        float best_cost = std::numeric_limits<float>::max();
        int best_axis = -1;
        int best_split = start + count / 2;

        for (int axis = 0; axis < 3; ++axis) {
            // Get bounds along this axis
            float axis_min = std::numeric_limits<float>::max();
            float axis_max = -std::numeric_limits<float>::max();

            for (int i = start; i < end; ++i) {
                const auto& bbox = entities[indices[i]].bbox;
                float center = (axis == 0) ? (bbox.min_x + bbox.max_x) * 0.5f :
                               (axis == 1) ? (bbox.min_y + bbox.max_y) * 0.5f :
                                             (bbox.min_z + bbox.max_z) * 0.5f;
                axis_min = std::min(axis_min, center);
                axis_max = std::max(axis_max, center);
            }

            if (axis_max - axis_min < 0.001f) continue;

            // Simple bucket-based SAH
            const int NUM_BUCKETS = 8;
            struct Bucket {
                AABB bounds;
                int count = 0;
            } buckets[NUM_BUCKETS];

            for (int i = start; i < end; ++i) {
                const auto& bbox = entities[indices[i]].bbox;
                float center = (axis == 0) ? (bbox.min_x + bbox.max_x) * 0.5f :
                               (axis == 1) ? (bbox.min_y + bbox.max_y) * 0.5f :
                                             (bbox.min_z + bbox.max_z) * 0.5f;

                int bucket_idx = std::min(NUM_BUCKETS - 1,
                    static_cast<int>((center - axis_min) / (axis_max - axis_min) * NUM_BUCKETS));

                buckets[bucket_idx].count++;
                buckets[bucket_idx].bounds.expand(bbox);
            }

            // Evaluate splits
            for (int split = 1; split < NUM_BUCKETS; ++split) {
                AABB left_bounds, right_bounds;
                int left_count = 0, right_count = 0;

                for (int b = 0; b < split; ++b) {
                    left_bounds.expand(buckets[b].bounds);
                    left_count += buckets[b].count;
                }
                for (int b = split; b < NUM_BUCKETS; ++b) {
                    right_bounds.expand(buckets[b].bounds);
                    right_count += buckets[b].count;
                }

                if (left_count == 0 || right_count == 0) continue;

                float parent_area = bounds.surface_area();
                float cost = 1.0f +
                    (left_bounds.surface_area() / parent_area) * left_count +
                    (right_bounds.surface_area() / parent_area) * right_count;

                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;

                    // Calculate actual split position
                    float split_pos = axis_min + (axis_max - axis_min) * split / NUM_BUCKETS;

                    // Count elements on left
                    int left_n = 0;
                    for (int i = start; i < end; ++i) {
                        const auto& bbox = entities[indices[i]].bbox;
                        float center = (axis == 0) ? (bbox.min_x + bbox.max_x) * 0.5f :
                                       (axis == 1) ? (bbox.min_y + bbox.max_y) * 0.5f :
                                                     (bbox.min_z + bbox.max_z) * 0.5f;
                        if (center < split_pos) left_n++;
                    }
                    best_split = start + std::max(1, std::min(count - 1, left_n));
                }
            }
        }

        // Partition based on best axis
        if (best_axis >= 0) {
            if (best_axis == 0) {
                std::nth_element(indices.begin() + start, indices.begin() + best_split,
                    indices.begin() + end, [&entities](int a, int b) {
                        float ca = (entities[a].bbox.min_x + entities[a].bbox.max_x) * 0.5f;
                        float cb = (entities[b].bbox.min_x + entities[b].bbox.max_x) * 0.5f;
                        return ca < cb;
                    });
            } else if (best_axis == 1) {
                std::nth_element(indices.begin() + start, indices.begin() + best_split,
                    indices.begin() + end, [&entities](int a, int b) {
                        float ca = (entities[a].bbox.min_y + entities[a].bbox.max_y) * 0.5f;
                        float cb = (entities[b].bbox.min_y + entities[b].bbox.max_y) * 0.5f;
                        return ca < cb;
                    });
            } else {
                std::nth_element(indices.begin() + start, indices.begin() + best_split,
                    indices.begin() + end, [&entities](int a, int b) {
                        float ca = (entities[a].bbox.min_z + entities[a].bbox.max_z) * 0.5f;
                        float cb = (entities[b].bbox.min_z + entities[b].bbox.max_z) * 0.5f;
                        return ca < cb;
                    });
            }

            // Build children
            node.left_child = build_recursive(entities, indices, start, best_split);
            parent_indices_[node.left_child] = node_idx;

            node.right_child = build_recursive(entities, indices, best_split, end);
            parent_indices_[node.right_child] = node_idx;

            return node_idx;
        }
    }

    // Fallback: median split along longest axis
    int split = start + count / 2;
    float x_ext = bounds.width();
    float y_ext = bounds.height();
    float z_ext = bounds.depth();

    int axis = 0;
    if (y_ext > x_ext) axis = 1;
    if (z_ext > std::max(x_ext, y_ext)) axis = 2;

    if (axis == 0) {
        std::nth_element(indices.begin() + start, indices.begin() + split,
            indices.begin() + end, [&entities](int a, int b) {
                return (entities[a].bbox.min_x + entities[a].bbox.max_x) <
                       (entities[b].bbox.min_x + entities[b].bbox.max_x);
            });
    } else if (axis == 1) {
        std::nth_element(indices.begin() + start, indices.begin() + split,
            indices.begin() + end, [&entities](int a, int b) {
                return (entities[a].bbox.min_y + entities[a].bbox.max_y) <
                       (entities[b].bbox.min_y + entities[b].bbox.max_y);
            });
    } else {
        std::nth_element(indices.begin() + start, indices.begin() + split,
            indices.begin() + end, [&entities](int a, int b) {
                return (entities[a].bbox.min_z + entities[a].bbox.max_z) <
                       (entities[b].bbox.min_z + entities[b].bbox.max_z);
            });
    }

    node.left_child = build_recursive(entities, indices, start, split);
    parent_indices_[node.left_child] = node_idx;

    node.right_child = build_recursive(entities, indices, split, end);
    parent_indices_[node.right_child] = node_idx;

    return node_idx;
}

// =============================================================================
// Ray Intersection
// =============================================================================

bool EntityBVH::ray_intersects_aabb(float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    const float* bbox_min, const float* bbox_max,
                                    float max_t) const {
    float inv_dx = (dx != 0.0f) ? 1.0f / dx : 1e30f;
    float inv_dy = (dy != 0.0f) ? 1.0f / dy : 1e30f;
    float inv_dz = (dz != 0.0f) ? 1.0f / dz : 1e30f;

    float t1 = (bbox_min[0] - ox) * inv_dx;
    float t2 = (bbox_max[0] - ox) * inv_dx;
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);

    t1 = (bbox_min[1] - oy) * inv_dy;
    t2 = (bbox_max[1] - oy) * inv_dy;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));

    t1 = (bbox_min[2] - oz) * inv_dz;
    t2 = (bbox_max[2] - oz) * inv_dz;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));

    return tmax >= tmin && tmin <= max_t && tmax >= 0.0f;
}

bool EntityBVH::ray_intersects_triangle(float ox, float oy, float oz,
                                        float dx, float dy, float dz,
                                        float max_t,
                                        const Logosphere::ShadowTriangle& tri) const {
    // Moller-Trumbore algorithm
    float e1_x = tri.v1[0] - tri.v0[0];
    float e1_y = tri.v1[1] - tri.v0[1];
    float e1_z = tri.v1[2] - tri.v0[2];

    float e2_x = tri.v2[0] - tri.v0[0];
    float e2_y = tri.v2[1] - tri.v0[1];
    float e2_z = tri.v2[2] - tri.v0[2];

    float p_x = dy * e2_z - dz * e2_y;
    float p_y = dz * e2_x - dx * e2_z;
    float p_z = dx * e2_y - dy * e2_x;

    float det = e1_x * p_x + e1_y * p_y + e1_z * p_z;
    if (std::abs(det) < 0.000001f) return false;

    float inv_det = 1.0f / det;

    float t_x = ox - tri.v0[0];
    float t_y = oy - tri.v0[1];
    float t_z = oz - tri.v0[2];

    float u = (t_x * p_x + t_y * p_y + t_z * p_z) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;

    float q_x = t_y * e1_z - t_z * e1_y;
    float q_y = t_z * e1_x - t_x * e1_z;
    float q_z = t_x * e1_y - t_y * e1_x;

    float v = (dx * q_x + dy * q_y + dz * q_z) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = (e2_x * q_x + e2_y * q_y + e2_z * q_z) * inv_det;
    return t > 0.0001f && t < max_t;
}

bool EntityBVH::trace_shadow_ray(float from_x, float from_y, float from_z,
                                 float to_x, float to_y, float to_z,
                                 kg::EntityID skip_entity_id) const {
    if (!is_built_ || root_idx_ < 0) return false;

    // Compute ray direction and length
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float dz = to_z - from_z;
    float ray_length = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (ray_length < 0.001f) return false;

    // Normalize
    dx /= ray_length;
    dy /= ray_length;
    dz /= ray_length;

    // Stack-based traversal
    int stack[32];
    int stack_ptr = 0;
    stack[stack_ptr++] = root_idx_;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const EntityBVHNode& node = nodes_[node_idx];

        // Test entity AABB
        if (!ray_intersects_aabb(from_x, from_y, from_z, dx, dy, dz,
                                 node.bbox_min, node.bbox_max, ray_length)) {
            continue;
        }

        if (node.is_leaf()) {
            // Skip self
            if (static_cast<kg::EntityID>(node.entity_id) == skip_entity_id) {
                continue;
            }

            // Directional culling + triangle scan
            for (int g = 0; g < node.dir_group_count; ++g) {
                const DirectionalGroup& group = groups_[node.dir_group_start + g];

                // Directional culling: skip surfaces facing away from ray origin
                // dot(ray_dir, surface_normal) > 0 means surface faces away
                float dot = dx * group.avg_normal[0] +
                            dy * group.avg_normal[1] +
                            dz * group.avg_normal[2];

                if (dot > 0.0f) {
                    continue;  // Surface faces away, skip group
                }

                // Test triangles in this group
                for (int t = 0; t < group.tri_count; ++t) {
                    const auto& tri = triangles_[group.tri_start + t];
                    if (ray_intersects_triangle(from_x, from_y, from_z,
                                                dx, dy, dz, ray_length, tri)) {
                        return true;  // Blocked!
                    }
                }
            }
        } else {
            // Internal node: push children
            if (node.left_child >= 0) stack[stack_ptr++] = node.left_child;
            if (node.right_child >= 0) stack[stack_ptr++] = node.right_child;
        }
    }

    return false;
}

bool EntityBVH::trace_shadow_ray_exclude(float from_x, float from_y, float from_z,
                                         float to_x, float to_y, float to_z,
                                         const std::vector<kg::EntityID>& skip_entities) const {
    if (!is_built_ || root_idx_ < 0) return false;

    // Helper to check if entity should be skipped
    auto should_skip = [&skip_entities](kg::EntityID id) {
        return std::find(skip_entities.begin(), skip_entities.end(), id) != skip_entities.end();
    };

    // Compute ray direction and length
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float dz = to_z - from_z;
    float ray_length = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (ray_length < 0.001f) return false;

    dx /= ray_length;
    dy /= ray_length;
    dz /= ray_length;

    int stack[32];
    int stack_ptr = 0;
    stack[stack_ptr++] = root_idx_;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const EntityBVHNode& node = nodes_[node_idx];

        if (!ray_intersects_aabb(from_x, from_y, from_z, dx, dy, dz,
                                 node.bbox_min, node.bbox_max, ray_length)) {
            continue;
        }

        if (node.is_leaf()) {
            if (should_skip(static_cast<kg::EntityID>(node.entity_id))) {
                continue;
            }

            for (int g = 0; g < node.dir_group_count; ++g) {
                const DirectionalGroup& group = groups_[node.dir_group_start + g];

                float dot = dx * group.avg_normal[0] +
                            dy * group.avg_normal[1] +
                            dz * group.avg_normal[2];

                if (dot > 0.0f) continue;

                for (int t = 0; t < group.tri_count; ++t) {
                    if (ray_intersects_triangle(from_x, from_y, from_z,
                                                dx, dy, dz, ray_length,
                                                triangles_[group.tri_start + t])) {
                        return true;
                    }
                }
            }
        } else {
            if (node.left_child >= 0) stack[stack_ptr++] = node.left_child;
            if (node.right_child >= 0) stack[stack_ptr++] = node.right_child;
        }
    }

    return false;
}

// =============================================================================
// Dynamic Updates
// =============================================================================

void EntityBVH::mark_dirty(kg::EntityID entity_id) {
    dirty_entities_.push_back(entity_id);
}

void EntityBVH::refit_entity(kg::EntityID entity_id, const AABB& new_bbox) {
    auto it = entity_to_leaf_.find(entity_id);
    if (it == entity_to_leaf_.end()) return;

    int leaf_idx = it->second;
    nodes_[leaf_idx].set_bounds(new_bbox);
    refit_ancestors(leaf_idx);
}

void EntityBVH::refit_ancestors(int leaf_idx) {
    int current = parent_indices_[leaf_idx];
    while (current >= 0) {
        EntityBVHNode& node = nodes_[current];

        // Recompute bounds from children
        AABB new_bounds;
        if (node.left_child >= 0) {
            new_bounds.expand(nodes_[node.left_child].get_bounds());
        }
        if (node.right_child >= 0) {
            new_bounds.expand(nodes_[node.right_child].get_bounds());
        }
        node.set_bounds(new_bounds);

        current = parent_indices_[current];
    }
}

void EntityBVH::refit_dirty_entities() {
    // This requires external AABB computation - placeholder for now
    dirty_entities_.clear();
}

void EntityBVH::insert_entity(const EntityTriangleData& entity) {
    // For now, just rebuild - incremental insertion is complex
    // TODO: Implement proper SAH-based incremental insertion
    (void)entity;
}

void EntityBVH::remove_entity(kg::EntityID entity_id) {
    // For now, just mark as removed - actual cleanup on next rebuild
    entity_to_leaf_.erase(entity_id);
}

// =============================================================================
// Stats
// =============================================================================

int EntityBVH::get_depth() const {
    if (!is_built_ || root_idx_ < 0) return 0;

    int max_depth = 0;
    int stack[64];
    int depths[64];
    int stack_ptr = 0;

    stack[stack_ptr] = root_idx_;
    depths[stack_ptr] = 1;
    stack_ptr++;

    while (stack_ptr > 0) {
        stack_ptr--;
        int node_idx = stack[stack_ptr];
        int depth = depths[stack_ptr];

        max_depth = std::max(max_depth, depth);

        const EntityBVHNode& node = nodes_[node_idx];
        if (!node.is_leaf()) {
            if (node.left_child >= 0) {
                stack[stack_ptr] = node.left_child;
                depths[stack_ptr] = depth + 1;
                stack_ptr++;
            }
            if (node.right_child >= 0) {
                stack[stack_ptr] = node.right_child;
                depths[stack_ptr] = depth + 1;
                stack_ptr++;
            }
        }
    }

    return max_depth;
}

void EntityBVH::get_stats(int& entity_count, int& total_triangles, int& max_depth) const {
    entity_count = static_cast<int>(entity_to_leaf_.size());
    total_triangles = static_cast<int>(triangles_.size());
    max_depth = get_depth();
}
