// Entity Spatial Query - Implementation
//
// Bridges ParticleSystem and KG for entity-aware spatial queries

#include "entity_spatial.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"

namespace spatial {

EntitySpatialQuery::EntitySpatialQuery(ParticleSystem& ps, kg::KGModule& kg)
    : ps_(ps), kg_(kg) {}

ParticleData EntitySpatialQuery::get_particle_data(int render_idx) {
    ParticleData data;
    data.render_idx = render_idx;

    if (render_idx < 0) return data;

    // Get position from ParticleSystem
    auto particles = ps_.lock_particles_for_read();
    if (render_idx >= static_cast<int>(particles.size())) return data;

    const auto& p = particles.get()[render_idx];
    data.x = p.x;
    data.y = p.y;
    data.z = p.z;
    data.vx = p.vx;
    data.vy = p.vy;
    data.vz = p.vz;
    data.width = p.width;
    data.height = p.height;
    data.thickness = p.thickness;

    // Get entity and body_part from KG
    data.owner = kg_.getEntityByRenderIndex(render_idx);
    if (data.owner != kg::INVALID_ENTITY) {
        data.body_part = kg_.getParticleBodyPart(data.owner, render_idx);
    }

    return data;
}

std::vector<ParticleData> EntitySpatialQuery::get_entity_particles(kg::EntityID entity) {
    std::vector<ParticleData> result;

    if (entity == kg::INVALID_ENTITY) return result;

    // Get all KG particles for entity (recursive for humanoids via has_part)
    auto kg_particles = kg_.getEntityKGParticlesRecursive(entity, "HAS_PART");

    // Lock ParticleSystem ONCE for efficiency
    auto particles = ps_.lock_particles_for_read();

    for (auto kg_id : kg_particles) {
        int render_idx = kg_.getRenderIndex(kg_id);
        if (render_idx < 0 || render_idx >= static_cast<int>(particles.size())) continue;

        const auto& p = particles.get()[render_idx];
        ParticleData data;
        data.render_idx = render_idx;
        data.x = p.x;
        data.y = p.y;
        data.z = p.z;
        data.vx = p.vx;
        data.vy = p.vy;
        data.vz = p.vz;
        data.width = p.width;
        data.height = p.height;
        data.thickness = p.thickness;
        data.owner = entity;
        data.body_part = kg_.getParticleBodyPart(entity, render_idx);

        result.push_back(data);
    }

    return result;
}

bool EntitySpatialQuery::get_entity_center(kg::EntityID entity, float& x, float& y, float& z) {
    auto particles = get_entity_particles(entity);
    if (particles.empty()) return false;

    x = y = z = 0.0f;
    for (const auto& p : particles) {
        x += p.x;
        y += p.y;
        z += p.z;
    }

    float count = static_cast<float>(particles.size());
    x /= count;
    y /= count;
    z /= count;

    return true;
}

AABB EntitySpatialQuery::get_entity_bbox(kg::EntityID entity) {
    auto particles = get_entity_particles(entity);
    if (particles.empty()) return AABB{0, 0, 0, 0, 0, 0};

    // Initialize with first particle
    AABB bbox;
    const auto& first = particles[0];
    bbox.min_x = first.x - first.width * 0.5f;
    bbox.max_x = first.x + first.width * 0.5f;
    bbox.min_y = first.y - first.height * 0.5f;
    bbox.max_y = first.y + first.height * 0.5f;
    bbox.min_z = first.z - first.thickness * 0.5f;
    bbox.max_z = first.z + first.thickness * 0.5f;

    // Expand for remaining particles
    for (size_t i = 1; i < particles.size(); ++i) {
        const auto& p = particles[i];
        bbox.min_x = std::min(bbox.min_x, p.x - p.width * 0.5f);
        bbox.max_x = std::max(bbox.max_x, p.x + p.width * 0.5f);
        bbox.min_y = std::min(bbox.min_y, p.y - p.height * 0.5f);
        bbox.max_y = std::max(bbox.max_y, p.y + p.height * 0.5f);
        bbox.min_z = std::min(bbox.min_z, p.z - p.thickness * 0.5f);
        bbox.max_z = std::max(bbox.max_z, p.z + p.thickness * 0.5f);
    }

    return bbox;
}

bool EntitySpatialQuery::get_body_part_position(kg::EntityID entity, const std::string& part,
                                                 float& x, float& y, float& z) {
    auto particles = get_entity_particles(entity);

    for (const auto& p : particles) {
        if (p.body_part == part) {
            x = p.x;
            y = p.y;
            z = p.z;
            return true;
        }
    }

    return false;
}

bool EntitySpatialQuery::get_body_part_data(kg::EntityID entity, const std::string& part,
                                             ParticleData& out_data) {
    auto particles = get_entity_particles(entity);

    for (const auto& p : particles) {
        if (p.body_part == part) {
            out_data = p;
            return true;
        }
    }

    return false;
}

float EntitySpatialQuery::distance_between_entities(kg::EntityID a, kg::EntityID b) {
    float ax, ay, az, bx, by, bz;

    if (!get_entity_center(a, ax, ay, az)) return -1.0f;
    if (!get_entity_center(b, bx, by, bz)) return -1.0f;

    float dx = bx - ax;
    float dy = by - ay;
    float dz = bz - az;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool EntitySpatialQuery::entities_bbox_overlap(kg::EntityID a, kg::EntityID b) {
    AABB bbox_a = get_entity_bbox(a);
    AABB bbox_b = get_entity_bbox(b);

    return bbox_a.overlaps(bbox_b);
}

float EntitySpatialQuery::distance_between_particles(int render_idx_a, int render_idx_b) {
    auto pa = get_particle_data(render_idx_a);
    auto pb = get_particle_data(render_idx_b);

    if (pa.render_idx < 0 || pb.render_idx < 0) return -1.0f;

    float dx = pb.x - pa.x;
    float dy = pb.y - pa.y;
    float dz = pb.z - pa.z;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool EntitySpatialQuery::particles_aabb_overlap(int render_idx_a, int render_idx_b) {
    auto pa = get_particle_data(render_idx_a);
    auto pb = get_particle_data(render_idx_b);

    if (pa.render_idx < 0 || pb.render_idx < 0) return false;

    AABB bbox_a = pa.get_aabb();
    AABB bbox_b = pb.get_aabb();

    return bbox_a.overlaps(bbox_b);
}

}  // namespace spatial
