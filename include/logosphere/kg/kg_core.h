#ifndef KG_CORE_H
#define KG_CORE_H

#include "logosphere/kg/kg_types.h"
#include "logosphere/kg/ontology_registry.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>

// Forward declaration (Particle is in global namespace)
struct Particle;

namespace kg {

// PIMPL for the particle-data slice of KG state. Defined in
// src/kg/kg_particle_store.cpp, which is the only TU that depends on
// particle.h. Lets kg_core.cpp compile without pulling physics headers
// into callers that do not need them.
struct KGParticleDataStore;

/**
 * Core Knowledge Graph implementation
 * 
 * This class contains the actual graph data structures.
 * Only instantiated when KGMode != DISABLED.
 * 
 * Design principle: Start simple, optimize later.
 */
class KGCore {
public:
    explicit KGCore(const KGConfig& config, const OntologyRegistry& registry);
    ~KGCore();
    
    // === Entity Management ===
    
    EntityID createEntity(const std::string& type);

    // Create entity at world position (automatically sets chunk_x, chunk_y properties)
    EntityID createEntityAtPosition(const std::string& type, float world_x, float world_y, float chunk_size = 50.0f);

    void destroyEntity(EntityID id);
    bool exists(EntityID id) const;
    const Entity* getEntity(EntityID id) const;
    Entity* getEntity(EntityID id);
    
    // === Relationship Management ===
    
    RelationID createRelation(EntityID from, const std::string& type, EntityID to);
    void destroyRelation(RelationID id);
    // Find a relation by (from, type, to). Returns INVALID_RELATION if not found.
    RelationID findRelation(EntityID from, const std::string& type, EntityID to) const;
    std::vector<EntityID> getRelated(EntityID id, const std::string& relation) const;
    // The same query walked backwards: who points AT me with this
    // relation. Answers "which creature owns this body part" from the
    // part, which the forward query cannot do at any price.
    //
    // Reads the incoming_relations index, maintained since relations
    // existed (createRelation appends, destroyRelation and destroyEntity
    // prune) and read by nothing until issue #36. Same cost as the
    // forward query, no new bookkeeping.
    std::vector<EntityID> getRelatedReverse(EntityID id, const std::string& relation) const;

    // === Property Management ===
    
    void setProperty(EntityID id, const std::string& key, const PropertyValue& value);
    PropertyValue getProperty(EntityID id, const std::string& key) const;
    
    // === Stable Particle ID System ===
    // Two-tier system: KGParticleID (stable) → RenderIndex (temporary)

    KGParticleID createKGParticle(EntityID entity, RenderIndex initial_render_index);
    void destroyKGParticle(KGParticleID kg_id);
    RenderIndex getRenderIndex(KGParticleID kg_id) const;
    void updateRenderIndex(KGParticleID kg_id, RenderIndex new_index);
    void notifyParticleSwap(RenderIndex old_index, RenderIndex new_index);

    // Erase all KG mappings pointing to this render index (called when particle actually deleted)
    void eraseRenderIndexMappings(RenderIndex index);
    std::vector<KGParticleID> getEntityKGParticles(EntityID entity) const;
    EntityID getEntityByKGParticle(KGParticleID kg_id) const;
    std::vector<EntityID> getEntitiesByRenderIndex(RenderIndex render_idx) const;  // UI Inspector support
    EntityID getEntityByRenderIndex(RenderIndex render_idx) const;  // Fast O(1) lookup for collision detection

    // Snapshot render index -> owning entity for indices [0, count), taking the
    // lock ONCE. The per-index accessor above locks a recursive_mutex and does
    // two hash lookups; the shadow-triangle pass called it once per particle
    // from 14 worker threads, which measured 275 ns per call under contention
    // against roughly 25 uncontended. Callers needing many indices inside a
    // parallel region should snapshot first, then read lock-free. Unmapped
    // entries are left INVALID_ENTITY, matching the per-index miss behaviour.
    void snapshotRenderIndexToEntity(std::vector<EntityID>& out, size_t count) const;

    KGParticleID getKGParticleByRenderIndex(RenderIndex render_idx) const;  // Stable id for a live render slot

    // Recursive particle collection via relationships (for hierarchical entities)
    std::vector<KGParticleID> getEntityKGParticlesRecursive(EntityID entity, const std::string& relation = "HAS_PART") const;

    // === Particle Data Storage (for reload) ===

    // Store complete particle data for a KG particle (enables chunk reload)
    void setKGParticleData(KGParticleID kg_id, const Particle& particle_data);

    // Get stored particle data (returns default Particle if not found)
    Particle getKGParticleData(KGParticleID kg_id) const;

    // Check if particle data exists for KG particle
    bool hasKGParticleData(KGParticleID kg_id) const;

    // === Gluon Storage (for physics entity reload) ===

    KGGluonID createKGGluon(EntityID entity, KGParticleID particle_a, KGParticleID particle_b);
    void setKGGluonData(KGGluonID kg_id, const KGGluonData& gluon_data);
    KGGluonData getKGGluonData(KGGluonID kg_id) const;
    bool hasKGGluonData(KGGluonID kg_id) const;
    std::vector<KGGluonID> getEntityKGGluons(EntityID entity) const;
    std::pair<KGParticleID, KGParticleID> getKGGluonParticles(KGGluonID kg_id) const;
    void destroyKGGluon(KGGluonID kg_id);

    // === High-level Helpers ===

    // Create child entity, bind particles, and link to parent in one operation
    // Common pattern for hierarchical entities (e.g., humanoid body parts, tree branches)
    EntityID createChildEntityWithParticles(
        EntityID parent_entity,
        const std::string& child_type,
        const std::vector<RenderIndex>& particle_indices,
        const std::string& relation = "HAS_PART"
    );

    // Unload entity for permaworld: clear KG particle bindings, mark unloaded, return render indices
    // Entity record stays in KG (append-only). Returns render indices for ParticleSystem deletion.
    UnloadResult unloadEntity(EntityID entity_id);

    // === Queries ===
    
    std::vector<EntityID> findByType(const std::string& type) const;
    std::vector<EntityID> findByProperty(const std::string& key, const PropertyValue& value) const;
    
    // === Stats ===
    
    size_t getEntityCount() const { return entities.size(); }
    size_t getRelationCount() const { return relations.size(); }
    size_t getMemoryUsage() const;
    
private:
    KGConfig config;
    const OntologyRegistry& registry_;

    // === Core Data Structures ===
    
    // Entity storage - using unordered_map for O(1) lookup
    std::unordered_map<EntityID, Entity> entities;
    
    // Relationship storage
    std::unordered_map<RelationID, Relation> relations;
    
    // === Indices for Fast Queries ===
    
    // Type index: type -> list of entities
    std::unordered_map<std::string, std::vector<EntityID>> type_index;
    
    // Relationship index: entity -> outgoing relations
    std::unordered_map<EntityID, std::vector<RelationID>> outgoing_relations;
    
    // Relationship index: entity -> incoming relations
    std::unordered_map<EntityID, std::vector<RelationID>> incoming_relations;
    
    // === Stable Particle ID Mapping ===
    // Two-tier system for permaworld support:
    // - KGParticleID: Stable, never reused, survives save/load
    // - RenderIndex: Temporary, changes during swap-and-pop

    std::unordered_map<KGParticleID, RenderIndex> kg_to_render_;          // KG ID → current render index (ONLY loaded particles)
    std::unordered_map<RenderIndex, KGParticleID> render_to_kg_;          // Render index → KG ID (reverse lookup for collision detection)
    std::unordered_set<KGParticleID> unloaded_particles_;                 // Historical record of unloaded particles (permaworld)
    std::unordered_map<KGParticleID, EntityID> kg_particle_to_entity_;    // KG ID → owning entity
    std::unordered_map<EntityID, std::vector<KGParticleID>> entity_kg_particles_;  // Entity → KG IDs

    // === Particle Data Storage (for reload) ===
    // Concrete definition lives in src/kg/kg_particle_store.cpp (PIMPL).
    // shared_ptr (not unique_ptr) because its destructor is type-erased
    // via the control block, so kg_core.cpp can default-destruct a
    // KGCore without needing the complete type of KGParticleDataStore.
    // The store is lazily created on first particle-data write.
    std::shared_ptr<KGParticleDataStore> particle_data_store_;

    // === Gluon Storage (for physics entity reload) ===
    // Stores gluon constraints so they can be recreated when chunks reload
    struct KGGluonRecord {
        EntityID entity;
        KGParticleID particle_a;
        KGParticleID particle_b;
    };
    std::unordered_map<KGGluonID, KGGluonRecord> kg_gluon_records_;
    std::unordered_map<KGGluonID, KGGluonData> kg_gluon_data_;
    std::unordered_map<EntityID, std::vector<KGGluonID>> entity_kg_gluons_;

    // === ID Generation ===

    EntityID next_entity_id = 1;  // Start at 1, 0 is INVALID
    RelationID next_relation_id = 1;
    KGParticleID next_kg_particle_id_ = 1;  // CRITICAL: Never resets, must persist across save/load
    KGGluonID next_kg_gluon_id_ = 1;

    // === Thread Safety ===
    // Protects all data structures from concurrent access during chunk loading/unloading
    // Using recursive_mutex because methods like getEntityKGParticlesRecursive call other protected methods
    mutable std::recursive_mutex mutex_;

    // === Helper Methods ===

    void addToTypeIndex(EntityID id, const std::string& type);
    void removeFromTypeIndex(EntityID id, const std::string& type);
    void addToRelationIndices(RelationID rel_id, EntityID from, EntityID to);
    void removeFromRelationIndices(RelationID rel_id, EntityID from, EntityID to);
};

} // namespace kg

#endif // KG_CORE_H