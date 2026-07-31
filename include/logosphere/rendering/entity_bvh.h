#ifndef ENTITY_BVH_H
#define ENTITY_BVH_H

#include <vector>
#include <unordered_map>
#include "logosphere/physics/bvh.h"  // Reuse AABB
#include "logosphere/kg/kg_types.h"  // For EntityID

// Forward declaration
namespace Logosphere {
    struct ShadowTriangle;
}

// =============================================================================
// ENTITY BVH - Two-level hierarchy with directional culling
// =============================================================================
//
// Level 1: Entity BVH - coarse culling by entity bounding boxes
// Level 2: DirectionalGroups - triangles grouped by surface normal
// Level 3: Triangle scan within non-culled groups
//
// OPTIMIZATION INSIGHT:
// Particles are fully opaque/solid. A ray hitting an entity from one side
// CANNOT reach surfaces facing the opposite direction (they're occluded by
// the entity's own geometry). This allows ~50% triangle culling per entity.
//
// =============================================================================

// 6 canonical directions for grouping triangles by facing
namespace EntityBVHDirection {
    constexpr int POS_X = 0;  // +X (East)
    constexpr int NEG_X = 1;  // -X (West)
    constexpr int POS_Y = 2;  // +Y (North)
    constexpr int NEG_Y = 3;  // -Y (South)
    constexpr int POS_Z = 4;  // +Z (Up)
    constexpr int NEG_Z = 5;  // -Z (Down)
    constexpr int COUNT = 6;

    // Canonical normal vectors (unit vectors)
    constexpr float NORMALS[6][3] = {
        { 1.0f,  0.0f,  0.0f},  // POS_X
        {-1.0f,  0.0f,  0.0f},  // NEG_X
        { 0.0f,  1.0f,  0.0f},  // POS_Y
        { 0.0f, -1.0f,  0.0f},  // NEG_Y
        { 0.0f,  0.0f,  1.0f},  // POS_Z
        { 0.0f,  0.0f, -1.0f}   // NEG_Z
    };

    // Get the canonical direction index for a given normal
    // Returns the direction that best matches the normal (highest dot product)
    inline int classify_normal(float nx, float ny, float nz) {
        float best_dot = -2.0f;
        int best_dir = 0;
        for (int i = 0; i < COUNT; ++i) {
            float dot = nx * NORMALS[i][0] + ny * NORMALS[i][1] + nz * NORMALS[i][2];
            if (dot > best_dot) {
                best_dot = dot;
                best_dir = i;
            }
        }
        return best_dir;
    }
}

// =============================================================================
// GPU-Compatible Structures (must match Metal shader layout)
// =============================================================================

// DirectionalGroup: triangles grouped by facing direction (32 bytes)
// Used for directional culling - skip groups facing away from ray
struct alignas(16) DirectionalGroup {
    float avg_normal[3];     // 12 bytes - average normal of this group
    int tri_start;           // 4 bytes - index into triangle array
    int tri_count;           // 4 bytes - triangles in this group
    int _padding[3];         // 12 bytes - alignment to 32 bytes

    DirectionalGroup() : avg_normal{0,0,0}, tri_start(0), tri_count(0), _padding{0,0,0} {}

    DirectionalGroup(float nx, float ny, float nz, int start, int count)
        : avg_normal{nx, ny, nz}, tri_start(start), tri_count(count), _padding{0,0,0} {}
};
static_assert(sizeof(DirectionalGroup) == 32, "DirectionalGroup must be 32 bytes for GPU");

// EntityBVHNode: BVH node for entity-level culling (48 bytes)
// Leaf nodes point to DirectionalGroups for fine-grained culling
struct alignas(16) EntityBVHNode {
    float bbox_min[3];       // 12 bytes - entity AABB minimum
    int left_child;          // 4 bytes - left child index (-1 if leaf)
    float bbox_max[3];       // 12 bytes - entity AABB maximum
    int right_child;         // 4 bytes - right child index (-1 if leaf)
    int entity_id;           // 4 bytes - KG EntityID (for skip checks)
    int dir_group_start;     // 4 bytes - index into DirectionalGroup array
    int dir_group_count;     // 4 bytes - number of directional groups (typically 6)
    int _padding;            // 4 bytes - alignment to 48 bytes

    EntityBVHNode()
        : bbox_min{0,0,0}, left_child(-1), bbox_max{0,0,0}, right_child(-1),
          entity_id(0), dir_group_start(0), dir_group_count(0), _padding(0) {}

    bool is_leaf() const { return dir_group_count > 0; }

    void set_bounds(const AABB& box) {
        bbox_min[0] = box.min_x; bbox_min[1] = box.min_y; bbox_min[2] = box.min_z;
        bbox_max[0] = box.max_x; bbox_max[1] = box.max_y; bbox_max[2] = box.max_z;
    }

    AABB get_bounds() const {
        return AABB(bbox_min[0], bbox_min[1], bbox_min[2],
                    bbox_max[0], bbox_max[1], bbox_max[2]);
    }
};
static_assert(sizeof(EntityBVHNode) == 48, "EntityBVHNode must be 48 bytes for GPU");

// =============================================================================
// CPU-side helper structures
// =============================================================================

// Per-entity data for building the BVH
struct EntityTriangleData {
    kg::EntityID entity_id;
    AABB bbox;
    std::vector<Logosphere::ShadowTriangle> triangles;
    std::vector<int> triangle_directions;  // Which direction each triangle belongs to
    bool is_dynamic;  // Needs per-frame AABB update

    EntityTriangleData() : entity_id(kg::INVALID_ENTITY), is_dynamic(false) {}
};

// =============================================================================
// EntityBVH Class
// =============================================================================

class EntityBVH {
public:
    EntityBVH() : root_idx_(-1), is_built_(false) {}

    // Build from entity data
    // entities: vector of entity data with triangles
    void build(const std::vector<EntityTriangleData>& entities);

    // Refit entity AABB without rebuilding tree structure
    // Call when entity moves but triangle count unchanged
    void refit_entity(kg::EntityID entity_id, const AABB& new_bbox);

    // Refit all dirty entities
    void refit_dirty_entities();

    // Mark entity as dirty (needs AABB update)
    void mark_dirty(kg::EntityID entity_id);

    // Insert new entity (for chunk loading)
    void insert_entity(const EntityTriangleData& entity);

    // Remove entity (for chunk unloading)
    void remove_entity(kg::EntityID entity_id);

    // Clear everything
    void clear();

    // Trace shadow ray with entity-level culling and directional culling
    // Returns true if ray is blocked
    bool trace_shadow_ray(float from_x, float from_y, float from_z,
                          float to_x, float to_y, float to_z,
                          kg::EntityID skip_entity_id = kg::INVALID_ENTITY) const;

    // Trace shadow ray with multiple exclusions
    bool trace_shadow_ray_exclude(float from_x, float from_y, float from_z,
                                  float to_x, float to_y, float to_z,
                                  const std::vector<kg::EntityID>& skip_entities) const;

    // Access for GPU upload
    const EntityBVHNode* get_nodes() const { return nodes_.data(); }
    int get_node_count() const { return static_cast<int>(nodes_.size()); }

    const DirectionalGroup* get_groups() const { return groups_.data(); }
    int get_group_count() const { return static_cast<int>(groups_.size()); }

    const Logosphere::ShadowTriangle* get_triangles() const { return triangles_.data(); }
    int get_triangle_count() const { return static_cast<int>(triangles_.size()); }

    bool is_ready() const { return is_built_; }

    // Stats
    int get_depth() const;
    void get_stats(int& entity_count, int& total_triangles, int& max_depth) const;

private:
    // BVH nodes
    std::vector<EntityBVHNode> nodes_;
    std::vector<int> parent_indices_;  // Parent for each node (for ancestor refitting)
    int root_idx_;
    bool is_built_;

    // Directional groups (all entities' groups concatenated)
    std::vector<DirectionalGroup> groups_;

    // All triangles (sorted by entity, then by direction group)
    std::vector<Logosphere::ShadowTriangle> triangles_;

    // Entity tracking
    std::unordered_map<kg::EntityID, int> entity_to_leaf_;  // entity_id → node index
    std::vector<kg::EntityID> dirty_entities_;  // Entities needing AABB update

    // Build helpers
    // `directions` is parallel to `entities`: directions[i][t] is the facing
    // bucket of entities[i].triangles[t]. It is kept alongside rather than
    // inside EntityTriangleData so build() never has to copy the entities,
    // each of which owns a std::vector<ShadowTriangle> (~30 MB a frame at
    // Eden scale).
    int build_recursive(const std::vector<EntityTriangleData>& entities,
                        const std::vector<std::vector<int>>& directions,
                        std::vector<int>& indices, int start, int end);
    void classify_triangles(const EntityTriangleData& entity,
                            std::vector<int>& out_directions) const;
    void create_directional_groups(const EntityTriangleData& entity,
                                   const std::vector<int>& directions,
                                   int& group_start, int& tri_start);
    void refit_ancestors(int leaf_idx);

    // Ray intersection helpers
    bool ray_intersects_aabb(float ox, float oy, float oz,
                             float dx, float dy, float dz,
                             const float* bbox_min, const float* bbox_max,
                             float max_t) const;
    bool ray_intersects_triangle(float ox, float oy, float oz,
                                 float dx, float dy, float dz,
                                 float max_t,
                                 const Logosphere::ShadowTriangle& tri) const;
};

#endif // ENTITY_BVH_H
