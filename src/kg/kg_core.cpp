#include "logosphere/kg/kg_core.h"
#include "logosphere/kg/ontology_validator.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>

namespace kg {

KGCore::KGCore(const KGConfig& cfg, const OntologyRegistry& registry)
    : config(cfg), registry_(registry) {
    // Reserve space for expected entities to avoid reallocation.
    // particle_data_store_ stays null; lazy-allocated when first particle
    // data is written (see kg_particle_store.cpp). Headless builds that
    // never touch particle storage pay zero cost here.
    entities.reserve(config.max_entities);
    relations.reserve(config.max_relations);
}

KGCore::~KGCore() = default;

// === Entity Management ===

EntityID KGCore::createEntity(const std::string& type) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Ontology validation: type must exist in registry
    if (!registry_.empty() && !registry_.hasEntityType(type)) {
        std::cerr << "[KG] REJECTED: unknown entity type '" << type
                  << "' (not in ontology)" << std::endl;
        return INVALID_ENTITY;
    }

    if (!registry_.empty() && registry_.isAbstract(type)) {
        std::cerr << "[KG] REJECTED: cannot instantiate abstract type '" << type
                  << "'" << std::endl;
        return INVALID_ENTITY;
    }

    // Check limits
    if (entities.size() >= config.max_entities) {
        std::cerr << "[KG] ERROR: Entity limit reached! size=" << entities.size()
                  << " max=" << config.max_entities << " (type=" << type << ")" << std::endl;
        return INVALID_ENTITY;
    }

    // Generate new ID
    EntityID id = next_entity_id++;

    // Create entity
    Entity& entity = entities[id];
    entity.id = id;
    entity.type = type;

    // Update indices
    addToTypeIndex(id, type);

    return id;
}

EntityID KGCore::createEntityAtPosition(const std::string& type, float world_x, float world_y, float chunk_size) {
    // Create base entity
    EntityID id = createEntity(type);
    if (id == INVALID_ENTITY) {
        return INVALID_ENTITY;
    }

    // Calculate chunk coordinates from world position
    int chunk_x = static_cast<int>(std::floor(world_x / chunk_size));
    int chunk_y = static_cast<int>(std::floor(world_y / chunk_size));

    // Set chunk properties automatically
    setProperty(id, "chunk_x", std::to_string(chunk_x));
    setProperty(id, "chunk_y", std::to_string(chunk_y));

    // Set world position for reference
    setProperty(id, "x", std::to_string(world_x));
    setProperty(id, "y", std::to_string(world_y));

    return id;
}

void KGCore::destroyEntity(EntityID id) {
    auto it = entities.find(id);
    if (it == entities.end()) {
        return;
    }
    
    // Remove from type index
    removeFromTypeIndex(id, it->second.type);
    
    // Remove all relationships involving this entity
    // First, collect relations to remove (can't modify while iterating)
    std::vector<RelationID> to_remove;
    
    // Outgoing relations
    auto out_it = outgoing_relations.find(id);
    if (out_it != outgoing_relations.end()) {
        to_remove.insert(to_remove.end(), out_it->second.begin(), out_it->second.end());
    }
    
    // Incoming relations
    auto in_it = incoming_relations.find(id);
    if (in_it != incoming_relations.end()) {
        to_remove.insert(to_remove.end(), in_it->second.begin(), in_it->second.end());
    }
    
    // Remove all collected relations
    for (RelationID rel_id : to_remove) {
        destroyRelation(rel_id);
    }

    // Remove all KG particles for this entity
    auto kg_it = entity_kg_particles_.find(id);
    if (kg_it != entity_kg_particles_.end()) {
        for (KGParticleID kg_id : kg_it->second) {
            kg_to_render_.erase(kg_id);
            kg_particle_to_entity_.erase(kg_id);
        }
        entity_kg_particles_.erase(kg_it);
    }

    // Finally, remove the entity
    entities.erase(it);
}

bool KGCore::exists(EntityID id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return entities.find(id) != entities.end();
}

const Entity* KGCore::getEntity(EntityID id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = entities.find(id);
    return (it != entities.end()) ? &it->second : nullptr;
}

Entity* KGCore::getEntity(EntityID id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = entities.find(id);
    return (it != entities.end()) ? &it->second : nullptr;
}

// === Relationship Management ===

RelationID KGCore::createRelation(EntityID from, const std::string& type, EntityID to) {
    // Verify both entities exist
    if (!exists(from) || !exists(to)) {
        return INVALID_RELATION;
    }

    // Ontology validation: relation type must exist
    if (!registry_.empty() && !registry_.hasRelationType(type)) {
        std::cerr << "[KG] REJECTED: unknown relation type '" << type
                  << "' (not in ontology)" << std::endl;
        return INVALID_RELATION;
    }

    // Ontology validation: domain/range
    if (!registry_.empty()) {
        const auto* from_entity = getEntity(from);
        const auto* to_entity = getEntity(to);
        if (from_entity && to_entity &&
            !registry_.isValidRelation(type, from_entity->type, to_entity->type)) {
            std::cerr << "[KG] REJECTED: relation '" << type
                      << "' not valid between '" << from_entity->type
                      << "' and '" << to_entity->type << "'" << std::endl;
            return INVALID_RELATION;
        }
    }

    // Check limits
    if (relations.size() >= config.max_relations) {
        return INVALID_RELATION;
    }
    
    // Generate new ID
    RelationID id = next_relation_id++;
    
    // Create relation
    Relation& rel = relations[id];
    rel.id = id;
    rel.from = from;
    rel.to = to;
    rel.type = type;
    
    // Update indices
    addToRelationIndices(id, from, to);
    
    return id;
}

void KGCore::destroyRelation(RelationID id) {
    auto it = relations.find(id);
    if (it == relations.end()) {
        return;
    }

    // Remove from indices
    removeFromRelationIndices(id, it->second.from, it->second.to);

    // Remove relation
    relations.erase(it);
}

RelationID KGCore::findRelation(EntityID from, const std::string& type, EntityID to) const {
    auto out_it = outgoing_relations.find(from);
    if (out_it == outgoing_relations.end()) return INVALID_RELATION;
    for (RelationID rid : out_it->second) {
        auto rel_it = relations.find(rid);
        if (rel_it == relations.end()) continue;
        const Relation& rel = rel_it->second;
        if (rel.to == to && rel.type == type) return rid;
    }
    return INVALID_RELATION;
}

std::vector<EntityID> KGCore::getRelated(EntityID id, const std::string& relation) const {
    std::vector<EntityID> result;
    
    // Find all outgoing relations from this entity
    auto it = outgoing_relations.find(id);
    if (it == outgoing_relations.end()) {
        return result;
    }
    
    // Filter by relation type
    for (RelationID rel_id : it->second) {
        auto rel_it = relations.find(rel_id);
        if (rel_it != relations.end() && rel_it->second.type == relation) {
            result.push_back(rel_it->second.to);
        }
    }

    return result;
}

// Mirror of getRelated over the incoming index. Deliberately identical in
// shape, so the two stay comparable when either is touched.
std::vector<EntityID> KGCore::getRelatedReverse(EntityID id, const std::string& relation) const {
    std::vector<EntityID> result;

    auto it = incoming_relations.find(id);
    if (it == incoming_relations.end()) {
        return result;
    }

    for (RelationID rel_id : it->second) {
        auto rel_it = relations.find(rel_id);
        if (rel_it != relations.end() && rel_it->second.type == relation) {
            result.push_back(rel_it->second.from);
        }
    }

    return result;
}

// === Property Management ===

// THE PROPERTY DOOR (Malleus H1). createEntity has gated types since the
// registry existed; property writes had no gate at all, so any engine
// system could stamp an undeclared key or a wrong-typed value and the
// KG would silently absorb it — the exact silent-fallback disease the
// 2026-08-13 ruling ordered destroyed. The check is the SAME
// validate_property_write the LLM KGOp path runs (declared-on-type,
// value-type coercion, schema min/max); the two paths cannot drift.
//
// Strict by default: violation prints an actionable message and aborts,
// same as the turtle doors. KG_GATE_LENIENT=1 downgrades to a printed
// violation and lets the write proceed (inventory mode). Lenient
// printing dedups on type.key so a per-frame writer cannot flood the log.
static void kg_property_gate_violation(EntityID id, const std::string& type,
                                       const std::string& key,
                                       const PropertyValue& value,
                                       const std::string& reason) {
    const bool lenient = std::getenv("KG_GATE_LENIENT") != nullptr;
    if (lenient) {
        static std::set<std::string> reported;
        if (!reported.insert(type + "." + key).second) return;
    }
    std::cerr << "[KG GATE VIOLATION] setProperty(entity " << id
              << " [" << type << "], '" << key << "' = '" << value
              << "'): " << reason
              << ". Declare the property in the schema (schema/*.yaml, then"
              << " scripts/generate_ontology.py) or fix the write."
              << (lenient ? " [KG_GATE_LENIENT: write allowed]" : "")
              << std::endl;
    if (!lenient) std::abort();
}

void KGCore::setProperty(EntityID id, const std::string& key, const PropertyValue& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Entity* entity = getEntity(id);
    if (!entity) {
        return;
    }
    // Empty registry = no ontology loaded (same guard createEntity has
    // used since the registry existed): with no TBox there is nothing
    // to validate against.
    if (!registry_.empty()) {
        ValidationResult r =
            validate_property_write(registry_, entity->type, key, value);
        if (!r.ok) {
            kg_property_gate_violation(id, entity->type, key, value, r.reason);
        }
    }
    entity->properties[key] = value;
}

PropertyValue KGCore::getProperty(EntityID id, const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const Entity* entity = getEntity(id);
    if (entity) {
        auto it = entity->properties.find(key);
        if (it != entity->properties.end()) {
            return it->second;
        }
    }
    return "";
}

// === Stable Particle ID System ===

KGParticleID KGCore::createKGParticle(EntityID entity, RenderIndex initial_render_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Assign stable ID that never gets reused
    KGParticleID kg_id = next_kg_particle_id_++;

    // Map to current render index (bidirectional)
    kg_to_render_[kg_id] = initial_render_index;
    render_to_kg_[initial_render_index] = kg_id;

    // Track ownership
    kg_particle_to_entity_[kg_id] = entity;
    entity_kg_particles_[entity].push_back(kg_id);

    return kg_id;
}

void KGCore::destroyKGParticle(KGParticleID kg_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Remove from bidirectional mapping
    auto render_it = kg_to_render_.find(kg_id);
    if (render_it != kg_to_render_.end()) {
        render_to_kg_.erase(render_it->second);  // Remove reverse mapping
        kg_to_render_.erase(render_it);
    }

    // Remove from entity tracking
    auto entity_it = kg_particle_to_entity_.find(kg_id);
    if (entity_it != kg_particle_to_entity_.end()) {
        EntityID entity = entity_it->second;

        // Remove from entity's particle list
        auto& particles = entity_kg_particles_[entity];
        particles.erase(std::remove(particles.begin(), particles.end(), kg_id), particles.end());

        if (particles.empty()) {
            entity_kg_particles_.erase(entity);
        }

        kg_particle_to_entity_.erase(entity_it);
    }
}

RenderIndex KGCore::getRenderIndex(KGParticleID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = kg_to_render_.find(kg_id);
    return (it != kg_to_render_.end()) ? it->second : INVALID_RENDER_INDEX;
}

void KGCore::updateRenderIndex(KGParticleID kg_id, RenderIndex new_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = kg_to_render_.find(kg_id);
    if (it != kg_to_render_.end()) {
        // Remove old reverse mapping
        render_to_kg_.erase(it->second);
        // Update forward and reverse mappings
        it->second = new_index;
        render_to_kg_[new_index] = kg_id;
    }
}

void KGCore::notifyParticleSwap(RenderIndex old_index, RenderIndex new_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // When ParticleSystem does swap-and-pop, particle at old_index moves to new_index
    // Use reverse map for O(1) lookup instead of iterating all entries
    auto rev_it = render_to_kg_.find(old_index);
    if (rev_it != render_to_kg_.end()) {
        KGParticleID kg_id = rev_it->second;
        // Update forward map
        kg_to_render_[kg_id] = new_index;
        // Update reverse map
        render_to_kg_.erase(rev_it);
        render_to_kg_[new_index] = kg_id;
    }
}

void KGCore::eraseRenderIndexMappings(RenderIndex index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Use reverse map for O(1) lookup
    auto rev_it = render_to_kg_.find(index);
    if (rev_it != render_to_kg_.end()) {
        KGParticleID kg_id = rev_it->second;
        kg_to_render_.erase(kg_id);
        render_to_kg_.erase(rev_it);
        // Verbose logging disabled - uncomment if debugging KG mappings
        // std::cout << "[KG ERASE] Erased mapping for KG particle " << kg_id << " at render index " << index << std::endl;
    }
    // Silently ignore if no mappings found (common during entity cleanup)
}

std::vector<KGParticleID> KGCore::getEntityKGParticles(EntityID entity) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = entity_kg_particles_.find(entity);
    return (it != entity_kg_particles_.end()) ? it->second : std::vector<KGParticleID>{};
}

EntityID KGCore::getEntityByKGParticle(KGParticleID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = kg_particle_to_entity_.find(kg_id);
    return (it != kg_particle_to_entity_.end()) ? it->second : INVALID_ENTITY;
}

KGParticleID KGCore::getKGParticleByRenderIndex(RenderIndex render_idx) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = render_to_kg_.find(render_idx);
    return (it != render_to_kg_.end()) ? it->second : INVALID_KG_PARTICLE_ID;
}

EntityID KGCore::getEntityByRenderIndex(RenderIndex render_idx) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // O(1) lookup via reverse map: render_idx → kg_id → entity
    auto rev_it = render_to_kg_.find(render_idx);
    if (rev_it == render_to_kg_.end()) {
        return INVALID_ENTITY;
    }
    auto entity_it = kg_particle_to_entity_.find(rev_it->second);
    return (entity_it != kg_particle_to_entity_.end()) ? entity_it->second : INVALID_ENTITY;
}

void KGCore::snapshotRenderIndexToEntity(std::vector<EntityID>& out, size_t count) const {
    // Size and zero OUTSIDE the lock: the point of this call is to keep the
    // critical section to one pass, not to hold the mutex across an allocation.
    out.assign(count, INVALID_ENTITY);

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Walk the bound render indices rather than probing [0, count): the map
    // holds only mapped slots, which is far smaller than the particle array.
    for (const auto& kv : render_to_kg_) {
        const size_t idx = static_cast<size_t>(kv.first);
        if (idx >= count) continue;
        auto entity_it = kg_particle_to_entity_.find(kv.second);
        if (entity_it != kg_particle_to_entity_.end()) {
            out[idx] = entity_it->second;
        }
    }
}

// === Queries ===

std::vector<EntityID> KGCore::findByType(const std::string& type) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = type_index.find(type);
    if (it != type_index.end()) {
        return it->second;
    }
    return {};
}

std::vector<EntityID> KGCore::findByProperty(const std::string& key, const PropertyValue& value) const {
    std::vector<EntityID> result;
    
    // For now, linear scan - we'll optimize with property indices later if needed
    for (const auto& [id, entity] : entities) {
        auto prop_it = entity.properties.find(key);
        if (prop_it != entity.properties.end() && prop_it->second == value) {
            result.push_back(id);
        }
    }
    
    return result;
}

// === Stats ===

size_t KGCore::getMemoryUsage() const {
    size_t total = 0;
    
    // Entities
    total += sizeof(entities);
    total += entities.size() * (sizeof(EntityID) + sizeof(Entity));
    
    // Add property memory
    for (const auto& [id, entity] : entities) {
        for (const auto& [key, value] : entity.properties) {
            total += key.capacity() + value.capacity();
        }
    }

    // KG particle system memory
    total += kg_to_render_.size() * (sizeof(KGParticleID) + sizeof(RenderIndex));
    total += kg_particle_to_entity_.size() * (sizeof(KGParticleID) + sizeof(EntityID));
    for (const auto& [entity_id, kg_particles] : entity_kg_particles_) {
        total += sizeof(EntityID) + kg_particles.capacity() * sizeof(KGParticleID);
    }
    
    // Relations
    total += sizeof(relations);
    total += relations.size() * (sizeof(RelationID) + sizeof(Relation));
    
    // Indices
    total += sizeof(type_index);
    for (const auto& [type, ids] : type_index) {
        total += type.capacity();
        total += ids.size() * sizeof(EntityID);
    }
    
    total += sizeof(outgoing_relations) + outgoing_relations.size() * sizeof(EntityID);
    total += sizeof(incoming_relations) + incoming_relations.size() * sizeof(EntityID);
    
    return total;
}

// === Helper Methods ===

void KGCore::addToTypeIndex(EntityID id, const std::string& type) {
    type_index[type].push_back(id);
}

void KGCore::removeFromTypeIndex(EntityID id, const std::string& type) {
    auto it = type_index.find(type);
    if (it != type_index.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
        
        // Clean up empty vectors
        if (vec.empty()) {
            type_index.erase(it);
        }
    }
}

void KGCore::addToRelationIndices(RelationID rel_id, EntityID from, EntityID to) {
    outgoing_relations[from].push_back(rel_id);
    incoming_relations[to].push_back(rel_id);
}

void KGCore::removeFromRelationIndices(RelationID rel_id, EntityID from, EntityID to) {
    // Remove from outgoing
    auto out_it = outgoing_relations.find(from);
    if (out_it != outgoing_relations.end()) {
        auto& vec = out_it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), rel_id), vec.end());
        if (vec.empty()) {
            outgoing_relations.erase(out_it);
        }
    }
    
    // Remove from incoming
    auto in_it = incoming_relations.find(to);
    if (in_it != incoming_relations.end()) {
        auto& vec = in_it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), rel_id), vec.end());
        if (vec.empty()) {
            incoming_relations.erase(in_it);
        }
    }
}

std::vector<EntityID> KGCore::getEntitiesByRenderIndex(RenderIndex render_idx) const {
    std::vector<EntityID> entities;
    std::unordered_set<EntityID> seen;  // Track unique entities

    // Search through all KG particles for ones with this render index
    for (const auto& [kg_id, idx] : kg_to_render_) {
        if (idx == render_idx) {
            // Found a match - get the entity
            auto entity_it = kg_particle_to_entity_.find(kg_id);
            if (entity_it != kg_particle_to_entity_.end()) {
                EntityID entity = entity_it->second;
                // Only add if not already added (ensures uniqueness)
                if (seen.find(entity) == seen.end()) {
                    entities.push_back(entity);
                    seen.insert(entity);
                }
            }
        }
    }

    return entities;
}

std::vector<KGParticleID> KGCore::getEntityKGParticlesRecursive(EntityID entity, const std::string& relation) const {
    std::vector<KGParticleID> all_particles;
    std::unordered_set<EntityID> visited;  // Prevent infinite loops in cyclic graphs

    // Helper lambda for recursive traversal
    std::function<void(EntityID)> collect_recursive = [&](EntityID current_entity) {
        // Prevent cycles
        if (visited.find(current_entity) != visited.end()) {
            return;
        }
        visited.insert(current_entity);

        // Collect direct particles from this entity
        auto direct_particles = getEntityKGParticles(current_entity);
        all_particles.insert(all_particles.end(), direct_particles.begin(), direct_particles.end());

        // Recursively collect from related entities
        auto related = getRelated(current_entity, relation);
        for (EntityID related_entity : related) {
            collect_recursive(related_entity);
        }
    };

    collect_recursive(entity);
    return all_particles;
}

EntityID KGCore::createChildEntityWithParticles(
    EntityID parent_entity,
    const std::string& child_type,
    const std::vector<RenderIndex>& particle_indices,
    const std::string& relation
) {
    // Create child entity
    EntityID child_entity = createEntity(child_type);

    // Bind all particles to child
    for (RenderIndex render_idx : particle_indices) {
        createKGParticle(child_entity, render_idx);
    }

    // Link parent to child
    createRelation(parent_entity, relation, child_entity);

    return child_entity;
}

UnloadResult KGCore::unloadEntity(EntityID entity_id) {
    UnloadResult result;

    if (!exists(entity_id)) {
        return result;  // Entity doesn't exist
    }

    // Get all KG particles recursively (includes child entities via relationships)
    auto kg_particles = getEntityKGParticlesRecursive(entity_id);

    // Collect render indices
    std::unordered_set<RenderIndex> unique_indices;  // Prevent duplicates
    size_t duplicate_count = 0;
    for (KGParticleID kg_id : kg_particles) {
        RenderIndex render_idx = getRenderIndex(kg_id);
        if (render_idx != INVALID_RENDER_INDEX) {
            auto [it, inserted] = unique_indices.insert(render_idx);
            if (!inserted) {
                duplicate_count++;
                std::cerr << "[KG UNLOAD ERROR] Duplicate render index " << render_idx
                          << " for KG particle " << kg_id << " in entity " << entity_id << std::endl;
            }
        }
    }

    if (duplicate_count > 0) {
        std::cerr << "[KG UNLOAD ERROR] Found " << duplicate_count
                  << " duplicate render indices in entity " << entity_id << std::endl;
    }

    // Populate result
    result.render_indices.assign(unique_indices.begin(), unique_indices.end());
    result.kg_particle_ids = kg_particles;  // Return all KG particles for tracking

    // CRITICAL: Invalidate render indices immediately
    // This prevents reload from thinking particles are still active
    for (KGParticleID kg_id : kg_particles) {
        unloaded_particles_.insert(kg_id);    // Add to historical record
        kg_to_render_[kg_id] = INVALID_RENDER_INDEX;  // Mark as unloaded
    }

    return result;
}

// Particle data storage for reload — see src/kg/kg_particle_store.cpp
// (split out so this TU does not need particle.h).

// === Gluon Storage Implementation ===

KGGluonID KGCore::createKGGluon(EntityID entity, KGParticleID particle_a, KGParticleID particle_b) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    KGGluonID kg_id = next_kg_gluon_id_++;

    KGGluonRecord record;
    record.entity = entity;
    record.particle_a = particle_a;
    record.particle_b = particle_b;

    kg_gluon_records_[kg_id] = record;
    entity_kg_gluons_[entity].push_back(kg_id);

    return kg_id;
}

void KGCore::setKGGluonData(KGGluonID kg_id, const KGGluonData& gluon_data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    kg_gluon_data_[kg_id] = gluon_data;
}

KGGluonData KGCore::getKGGluonData(KGGluonID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = kg_gluon_data_.find(kg_id);
    if (it != kg_gluon_data_.end()) {
        return it->second;
    }
    return KGGluonData{};
}

bool KGCore::hasKGGluonData(KGGluonID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return kg_gluon_data_.find(kg_id) != kg_gluon_data_.end();
}

std::vector<KGGluonID> KGCore::getEntityKGGluons(EntityID entity) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = entity_kg_gluons_.find(entity);
    if (it != entity_kg_gluons_.end()) {
        return it->second;
    }
    return {};
}

std::pair<KGParticleID, KGParticleID> KGCore::getKGGluonParticles(KGGluonID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = kg_gluon_records_.find(kg_id);
    if (it != kg_gluon_records_.end()) {
        return {it->second.particle_a, it->second.particle_b};
    }
    return {INVALID_KG_PARTICLE_ID, INVALID_KG_PARTICLE_ID};
}

void KGCore::destroyKGGluon(KGGluonID kg_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = kg_gluon_records_.find(kg_id);
    if (it != kg_gluon_records_.end()) {
        EntityID entity = it->second.entity;

        // Remove from entity's gluon list
        auto& gluons = entity_kg_gluons_[entity];
        gluons.erase(std::remove(gluons.begin(), gluons.end(), kg_id), gluons.end());

        // Remove records
        kg_gluon_records_.erase(kg_id);
        kg_gluon_data_.erase(kg_id);
    }
}

} // namespace kg