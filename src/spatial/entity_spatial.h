// Entity Spatial Query - Engine-layer spatial queries bridging ParticleSystem and KG
//
// Provides entity-aware spatial queries that game systems (combat, AI, vision) can use
// instead of manually coordinating between ParticleSystem (positions) and KG (entity ownership).
//
// Design: Thin query layer that combines:
//   - ParticleSystem: actual positions, velocities, dimensions
//   - KG: entity ownership, body_part properties, hierarchies

#ifndef LOGOSPHERE_ENTITY_SPATIAL_H
#define LOGOSPHERE_ENTITY_SPATIAL_H

#include "logosphere/kg/kg_types.h"
#include <vector>
#include <string>
#include <cmath>

// Forward declarations
class ParticleSystem;
namespace kg { class KGModule; }

namespace spatial {

// Axis-aligned bounding box
struct AABB {
    float min_x = 0.0f, min_y = 0.0f, min_z = 0.0f;
    float max_x = 0.0f, max_y = 0.0f, max_z = 0.0f;

    // Check if this AABB overlaps with another
    bool overlaps(const AABB& other) const {
        return !(max_x < other.min_x || min_x > other.max_x ||
                 max_y < other.min_y || min_y > other.max_y ||
                 max_z < other.min_z || min_z > other.max_z);
    }

    // Check if a point is inside this AABB
    bool contains_point(float x, float y, float z) const {
        return x >= min_x && x <= max_x &&
               y >= min_y && y <= max_y &&
               z >= min_z && z <= max_z;
    }

    // Get AABB center
    void center(float& cx, float& cy, float& cz) const {
        cx = (min_x + max_x) * 0.5f;
        cy = (min_y + max_y) * 0.5f;
        cz = (min_z + max_z) * 0.5f;
    }
};

// Particle data with entity context
struct ParticleData {
    int render_idx = -1;
    float x = 0.0f, y = 0.0f, z = 0.0f;               // Position
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;            // Velocity
    float width = 0.0f, height = 0.0f, thickness = 0.0f;
    std::string body_part;                            // From KG (empty if not set)
    kg::EntityID owner = kg::INVALID_ENTITY;          // Owning entity

    // Get AABB for this single particle
    AABB get_aabb() const {
        AABB box;
        box.min_x = x - width * 0.5f;
        box.max_x = x + width * 0.5f;
        box.min_y = y - height * 0.5f;
        box.max_y = y + height * 0.5f;
        box.min_z = z - thickness * 0.5f;
        box.max_z = z + thickness * 0.5f;
        return box;
    }
};

// Entity spatial query interface - bridges ParticleSystem and KG
class EntitySpatialQuery {
public:
    EntitySpatialQuery(ParticleSystem& ps, kg::KGModule& kg);

    // === Single Entity Queries ===

    // Get entity center (average of all particle positions)
    // Returns false if entity has no particles
    bool get_entity_center(kg::EntityID entity, float& x, float& y, float& z);

    // Get entity bounding box (aggregated from all particles)
    AABB get_entity_bbox(kg::EntityID entity);

    // Get all particles belonging to entity (recursive through has_part relations)
    std::vector<ParticleData> get_entity_particles(kg::EntityID entity);

    // Get specific body part position
    // Returns false if body part not found
    bool get_body_part_position(kg::EntityID entity, const std::string& part,
                                float& x, float& y, float& z);

    // Get specific body part full data
    // Returns false if body part not found
    bool get_body_part_data(kg::EntityID entity, const std::string& part,
                            ParticleData& out_data);

    // === Two-Entity Queries ===

    // Distance between entity centers
    float distance_between_entities(kg::EntityID a, kg::EntityID b);

    // Do entity bounding boxes overlap?
    bool entities_bbox_overlap(kg::EntityID a, kg::EntityID b);

    // === Particle-Level Queries ===

    // Get particle data by render index
    ParticleData get_particle_data(int render_idx);

    // Distance between two particle centers
    float distance_between_particles(int render_idx_a, int render_idx_b);

    // AABB overlap check between two particles
    bool particles_aabb_overlap(int render_idx_a, int render_idx_b);

private:
    ParticleSystem& ps_;
    kg::KGModule& kg_;
};

}  // namespace spatial

#endif // LOGOSPHERE_ENTITY_SPATIAL_H
