#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_core.h"
#include "logosphere/events/event_bus.h"
#include <cassert>
#include <iostream>
#include <utility>
#include <string>

namespace kg {

KGModule::KGModule(const OntologyRegistry& registry)
    : registry_(registry)
    , current_mode(KGMode::DISABLED)
    , core(nullptr) {
}

KGModule::~KGModule() {
}

void KGModule::extendOntology(const OntologyRegistry& extension) {
    registry_.extend(extension);
}

void KGModule::setMode(KGMode mode) {
    if (mode == current_mode) {
        return;  // No change needed
    }
    
    current_mode = mode;
    
    if (mode == KGMode::DISABLED) {
        // Free all memory when disabled
        core.reset();
        std::cout << "[KG] Mode set to DISABLED - all memory freed\n";
    } else {
        if (!core) {
            // Allocate core on first enable, with ontology registry
            core = std::make_unique<KGCore>(config, registry_);
            std::cout << "[KG] Mode set to " << static_cast<int>(mode)
                      << " - KG core initialized\n";

            // Seed world physics constants automatically
            // This happens once when KG is first enabled
            seedPhysicsConstants();
            std::cout << "[KG] Physics constants seeded\n";
        }
        // In future, we might need to enable/disable features based on mode
        // For now, all non-DISABLED modes get full core functionality
    }
}

bool KGModule::checkEnabled(const char* operation) const {
    if (!isEnabled()) {
        // Silent fail when disabled - no overhead, no spam
        return false;
    }
    return true;
}

// === Entity Operations ===

EntityID KGModule::createEntity(const std::string& type) {
    if (!checkEnabled("createEntity")) {
        return INVALID_ENTITY;
    }
    return core->createEntity(type);
}

EntityID KGModule::createEntityAtPosition(const std::string& type, float world_x, float world_y, float chunk_size) {
    if (!checkEnabled("createEntityAtPosition")) {
        return INVALID_ENTITY;
    }
    return core->createEntityAtPosition(type, world_x, world_y, chunk_size);
}

void KGModule::destroyEntity(EntityID id) {
    if (!checkEnabled("destroyEntity")) {
        return;
    }
    // Entity removal is bookkeeping, NOT a death. Emitting DeathEvent
    // here flooded the deaths journal with trail/UI teardown noise
    // (hundreds per second in Logotron) and drowned real deaths.
    // Deaths are emitted by the systems that mean them (DamageSystem,
    // game derez logic). Removed 2026-07-29 with the journal upgrade;
    // no consumer ever read these.
    core->destroyEntity(id);
}

bool KGModule::exists(EntityID id) const {
    if (!checkEnabled("exists")) {
        return false;
    }
    return core->exists(id);
}

std::string KGModule::getType(EntityID id) const {
    if (!checkEnabled("getType")) {
        return "";
    }
    const Entity* entity = core->getEntity(id);
    return entity ? entity->type : "";
}

// === Relationship Operations ===

RelationID KGModule::createRelation(EntityID from, const std::string& relation, EntityID to) {
    if (!checkEnabled("createRelation")) {
        return INVALID_RELATION;
    }
    auto result = core->createRelation(from, relation, to);
    if (event_bus_ && result != INVALID_RELATION) {
        logosphere::ontology::RelationEvent evt;
        evt.source_entity_id = std::to_string(from);
        evt.target_entity_id = std::to_string(to);
        evt.relation_type = relation;
        evt.event_type = "RELATION_CREATED";
        event_bus_->relations().emit(std::move(evt));
    }
    return result;
}

bool KGModule::destroyRelation(EntityID from, const std::string& relation, EntityID to) {
    if (!checkEnabled("destroyRelation")) {
        return false;
    }
    RelationID id = core->findRelation(from, relation, to);
    if (id == INVALID_RELATION) return false;
    core->destroyRelation(id);
    if (event_bus_) {
        logosphere::ontology::RelationEvent evt;
        evt.source_entity_id = std::to_string(from);
        evt.target_entity_id = std::to_string(to);
        evt.relation_type = relation;
        evt.event_type = "RELATION_REMOVED";
        event_bus_->relations().emit(std::move(evt));
    }
    return true;
}

std::vector<EntityID> KGModule::getRelated(EntityID id, const std::string& relation) const {
    if (!checkEnabled("getRelated")) {
        return {};
    }
    return core->getRelated(id, relation);
}

std::vector<EntityID> KGModule::getRelatedReverse(EntityID id, const std::string& relation) const {
    if (!checkEnabled("getRelatedReverse")) {
        return {};
    }
    return core->getRelatedReverse(id, relation);
}

// === Property Operations ===

void KGModule::setProperty(EntityID id, const std::string& key, const PropertyValue& value) {
    if (!checkEnabled("setProperty")) {
        return;
    }
    // Skip emission (and the write) when the value is unchanged — no-op sets
    // were spamming state_changes and triggering expensive subscribers like
    // ParticleDynamicsSystem::recompute_capability().
    std::string prev_value;
    if (event_bus_) {
        const PropertyValue& prev = core->getProperty(id, key);
        if (prev == value) return;
        prev_value = prev;
    }
    core->setProperty(id, key, value);
    if (event_bus_) {
        logosphere::ontology::WorldEvent evt;
        evt.target_entity_id = std::to_string(id);
        evt.event_type = "STATE_CHANGE";
        // The delta payload is what makes the state_changes JOURNAL
        // consumable: render_state_deltas (journal_render.h) folds
        // these into "prop: prev -> value" blocks for meta-agents.
        evt.payload_keys = {"property", "value", "prev"};
        evt.payload_values = {key, value, prev_value};
        event_bus_->state_changes().emit(std::move(evt));
    }
}

PropertyValue KGModule::getProperty(EntityID id, const std::string& key) const {
    if (!checkEnabled("getProperty")) {
        return "";
    }
    return core->getProperty(id, key);
}

std::vector<std::pair<std::string, PropertyValue>>
KGModule::getPropertiesWithPrefix(EntityID id, const std::string& prefix) const {
    std::vector<std::pair<std::string, PropertyValue>> result;
    if (!checkEnabled("getPropertiesWithPrefix")) return result;
    const Entity* entity = core->getEntity(id);
    if (!entity) return result;
    for (const auto& [key, value] : entity->properties) {
        if (key.size() >= prefix.size() &&
            key.compare(0, prefix.size(), prefix) == 0) {
            result.emplace_back(key, value);
        }
    }
    return result;
}

// === Stable Particle ID System ===

KGParticleID KGModule::createKGParticle(EntityID entity, RenderIndex initial_render_index) {
    if (!checkEnabled("createKGParticle")) {
        return INVALID_KG_PARTICLE_ID;
    }
    return core->createKGParticle(entity, initial_render_index);
}

void KGModule::destroyKGParticle(KGParticleID kg_id) {
    if (!checkEnabled("destroyKGParticle")) {
        return;
    }
    core->destroyKGParticle(kg_id);
}

RenderIndex KGModule::getRenderIndex(KGParticleID kg_id) const {
    if (!checkEnabled("getRenderIndex")) {
        return INVALID_RENDER_INDEX;
    }
    return core->getRenderIndex(kg_id);
}

void KGModule::updateRenderIndex(KGParticleID kg_id, RenderIndex new_index) {
    if (!checkEnabled("updateRenderIndex")) {
        return;
    }
    core->updateRenderIndex(kg_id, new_index);
}

void KGModule::notifyParticleSwap(RenderIndex old_index, RenderIndex new_index) {
    if (!checkEnabled("notifyParticleSwap")) {
        return;
    }
    core->notifyParticleSwap(old_index, new_index);
}

void KGModule::eraseRenderIndexMappings(RenderIndex index) {
    if (!checkEnabled("eraseRenderIndexMappings")) {
        return;
    }
    core->eraseRenderIndexMappings(index);
}

std::vector<KGParticleID> KGModule::getEntityKGParticles(EntityID entity) const {
    if (!checkEnabled("getEntityKGParticles")) {
        return {};
    }
    return core->getEntityKGParticles(entity);
}

EntityID KGModule::getEntityByKGParticle(KGParticleID kg_id) const {
    if (!checkEnabled("getEntityByKGParticle")) {
        return INVALID_ENTITY;
    }
    return core->getEntityByKGParticle(kg_id);
}

std::vector<EntityID> KGModule::getEntitiesByRenderIndex(RenderIndex render_idx) const {
    if (!checkEnabled("getEntitiesByRenderIndex")) {
        return {};
    }
    return core->getEntitiesByRenderIndex(render_idx);
}

EntityID KGModule::getEntityByRenderIndex(RenderIndex render_idx) const {
    if (!checkEnabled("getEntityByRenderIndex")) {
        return INVALID_ENTITY;
    }
    return core->getEntityByRenderIndex(render_idx);
}

void KGModule::snapshotRenderIndexToEntity(std::vector<EntityID>& out, size_t count) const {
    if (!checkEnabled("snapshotRenderIndexToEntity")) {
        out.assign(count, INVALID_ENTITY);
        return;
    }
    core->snapshotRenderIndexToEntity(out, count);
}

KGParticleID KGModule::getKGParticleByRenderIndex(RenderIndex render_idx) const {
    if (!checkEnabled("getKGParticleByRenderIndex")) {
        return INVALID_KG_PARTICLE_ID;
    }
    return core->getKGParticleByRenderIndex(render_idx);
}

std::vector<KGParticleID> KGModule::getEntityKGParticlesRecursive(EntityID entity, const std::string& relation) const {
    if (!checkEnabled("getEntityKGParticlesRecursive")) {
        return {};
    }
    return core->getEntityKGParticlesRecursive(entity, relation);
}

EntityID KGModule::createChildEntityWithParticles(
    EntityID parent_entity,
    const std::string& child_type,
    const std::vector<RenderIndex>& particle_indices,
    const std::string& relation
) {
    if (!checkEnabled("createChildEntityWithParticles")) {
        return INVALID_ENTITY;
    }
    return core->createChildEntityWithParticles(parent_entity, child_type, particle_indices, relation);
}

UnloadResult KGModule::unloadEntity(EntityID entity_id) {
    if (!checkEnabled("unloadEntity")) {
        return {};
    }
    return core->unloadEntity(entity_id);
}

// === Query Operations ===

std::vector<EntityID> KGModule::findByType(const std::string& type) const {
    if (!checkEnabled("findByType")) {
        return {};
    }
    return core->findByType(type);
}

std::vector<EntityID> KGModule::findByProperty(const std::string& key, const PropertyValue& value) const {
    if (!checkEnabled("findByProperty")) {
        return {};
    }
    return core->findByProperty(key, value);
}

// Physics queries (getEntityMass) live in src/kg/kg_particle_store.cpp
// so this TU does not need particle.h.

// === Performance & Debug ===

size_t KGModule::getMemoryUsage() const {
    if (!core) {
        return 0;  // Zero overhead when disabled!
    }
    return core->getMemoryUsage();
}

KGModule::Stats KGModule::getStats() const {
    Stats stats;
    if (core) {
        stats.entity_count = core->getEntityCount();
        stats.relation_count = core->getRelationCount();
        stats.memory_bytes = core->getMemoryUsage();
    }
    return stats;
}

// Particle data storage (for reload) lives in src/kg/kg_particle_store.cpp.

// === Gluon Storage ===

KGGluonID KGModule::createKGGluon(EntityID entity, KGParticleID particle_a, KGParticleID particle_b) {
    if (!checkEnabled("createKGGluon")) {
        return INVALID_KG_GLUON_ID;
    }
    return core->createKGGluon(entity, particle_a, particle_b);
}

void KGModule::setKGGluonData(KGGluonID kg_id, const KGGluonData& gluon_data) {
    if (!checkEnabled("setKGGluonData")) {
        return;
    }
    core->setKGGluonData(kg_id, gluon_data);
}

KGGluonData KGModule::getKGGluonData(KGGluonID kg_id) const {
    if (!checkEnabled("getKGGluonData")) {
        return KGGluonData{};
    }
    return core->getKGGluonData(kg_id);
}

bool KGModule::hasKGGluonData(KGGluonID kg_id) const {
    if (!checkEnabled("hasKGGluonData")) {
        return false;
    }
    return core->hasKGGluonData(kg_id);
}

std::vector<KGGluonID> KGModule::getEntityKGGluons(EntityID entity) const {
    if (!checkEnabled("getEntityKGGluons")) {
        return {};
    }
    return core->getEntityKGGluons(entity);
}

std::pair<KGParticleID, KGParticleID> KGModule::getKGGluonParticles(KGGluonID kg_id) const {
    if (!checkEnabled("getKGGluonParticles")) {
        return {INVALID_KG_PARTICLE_ID, INVALID_KG_PARTICLE_ID};
    }
    return core->getKGGluonParticles(kg_id);
}

void KGModule::destroyKGGluon(KGGluonID kg_id) {
    if (!checkEnabled("destroyKGGluon")) {
        return;
    }
    core->destroyKGGluon(kg_id);
}

EntityID KGModule::seedPhysicsConstants() {
    // TODO[PHYSICS-001]: Load from data/physics_constants.json instead of hardcoding
    // See docs/dynamics_physics_integration.md "Physics Constants in KG" section
    //
    // Future: Zone-based physics (moon zone, underwater zone, etc.)
    // For now: Single global physics_constants entity (KISS)

    if (!checkEnabled("seedPhysicsConstants")) {
        return INVALID_ENTITY;
    }

    // Create physics constants entity
    EntityID physics_id = createEntity("PhysicsConstants");

    // Seed Earth-standard physics (SI units)
    setProperty(physics_id, "gravity_z", "-9.8");         // m/s² (downward)
    setProperty(physics_id, "air_resistance", "0.0");     // Coefficient (none for now)
    setProperty(physics_id, "default_friction", "0.5");   // Coefficient

    return physics_id;
}

// === Combat Support ===

std::string KGModule::getParticleBodyPart(EntityID humanoid_entity, RenderIndex particle_render_idx) const {
    (void)humanoid_entity;  // Reserved for future: validate particle belongs to humanoid

    if (!checkEnabled("getParticleBodyPart")) {
        assert(false && "KG disabled but getParticleBodyPart called");
        return "";
    }

    // Get the entity that owns this particle
    EntityID particle_entity = getEntityByRenderIndex(particle_render_idx);
    assert(particle_entity != INVALID_ENTITY && "Particle has no owning entity");

    // Entity should have body_part property set by its creator (humanoid_generator, etc.)
    std::string body_part = getProperty(particle_entity, "body_part");
    assert(!body_part.empty() && "Entity missing body_part property - fix generator to set it");

    return body_part;
}

} // namespace kg