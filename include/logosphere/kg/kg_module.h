#ifndef KG_MODULE_H
#define KG_MODULE_H

#include "logosphere/kg/kg_types.h"
#include "logosphere/kg/ontology_registry.h"
#include <memory>
#include <vector>
#include <unordered_map>

// Forward declarations
struct Particle;
namespace logosphere { class EventBus; }

namespace kg {

class KGCore;

/**
 * Main Knowledge Graph Module
 * 
 * This is the primary interface to the KG system.
 * Key design principle: ZERO overhead when disabled.
 * 
 * Usage:
 *   KGModule kg;
 *   kg.setMode(KGMode::MINIMAL);  // Enable basic entity tracking
 *   auto id = kg.createEntity("fire");
 */
class KGModule {
public:
    explicit KGModule(const OntologyRegistry& registry);
    ~KGModule();

    void set_event_bus(logosphere::EventBus* bus) { event_bus_ = bus; }
    logosphere::EventBus* get_event_bus() const { return event_bus_; }

    void extendOntology(const OntologyRegistry& extension);
    const OntologyRegistry& getRegistry() const { return registry_; }

    // === Mode Management ===

    // Set operating mode - can switch at runtime
    void setMode(KGMode mode);
    KGMode getMode() const { return current_mode; }
    
    // Check if a feature is available in current mode
    bool isEnabled() const { return current_mode != KGMode::DISABLED; }
    bool hasSpatial() const { return current_mode >= KGMode::SPATIAL; }
    bool hasTemporal() const { return current_mode >= KGMode::TEMPORAL; }
    bool hasInference() const { return current_mode >= KGMode::INFERENCE; }

    // === World Initialization ===

    // Seed global physics constants into KG
    // TODO[PHYSICS-001]: Load from JSON file (data/physics_constants.json)
    // See docs/dynamics_physics_integration.md "Physics Constants in KG" section
    // Current: Hardcoded values (KISS approach)
    // Future: Zone-based physics with per-zone constants
    EntityID seedPhysicsConstants();

    // === Core Entity Operations (MINIMAL and up) ===

    // Create a new entity of given type
    EntityID createEntity(const std::string& type);

    // Create entity at world position (automatically sets chunk_x, chunk_y properties)
    EntityID createEntityAtPosition(const std::string& type, float world_x, float world_y, float chunk_size = 50.0f);

    // Delete an entity and all its relationships
    void destroyEntity(EntityID id);
    
    // Check if entity exists
    bool exists(EntityID id) const;
    
    // Get entity type
    std::string getType(EntityID id) const;
    
    // === Relationship Operations (MINIMAL and up) ===
    
    // Create relationship between entities
    RelationID createRelation(EntityID from, const std::string& relation, EntityID to);

    // Destroy a specific relation between entities. Returns true if a matching
    // relation was found and removed. Emits RelationEvent(RELATION_REMOVED) if
    // an EventBus is wired.
    bool destroyRelation(EntityID from, const std::string& relation, EntityID to);

    // Query relationships
    std::vector<EntityID> getRelated(EntityID id, const std::string& relation) const;
    // Walk a relation backwards: who points AT id with this type.
    // getRelated(creature, "HAS_PART") lists the parts; this takes a part
    // back to its creature. Needed wherever a system starts from
    // something physical (a particle, a contact) and must reach the
    // entity that owns it. Returns empty for an unrelated or unknown id.
    std::vector<EntityID> getRelatedReverse(EntityID id, const std::string& relation) const;

    // === Property Operations (MINIMAL and up) ===
    
    // Set a property on an entity
    void setProperty(EntityID id, const std::string& key, const PropertyValue& value);
    
    // Get a property from an entity
    PropertyValue getProperty(EntityID id, const std::string& key) const;

    // Distinguish a missing property from a present empty string and
    // remove it explicitly. Seed transactions use this to restore the
    // exact pre-load state when a later operation fails.
    bool hasProperty(EntityID id, const std::string& key) const;
    void removeProperty(EntityID id, const std::string& key);

    // Return all (key, value) pairs on this entity where key starts with prefix.
    // Used for grouped property lookups (e.g., "rule.0.payload." scans all payload keys).
    std::vector<std::pair<std::string, PropertyValue>>
        getPropertiesWithPrefix(EntityID id, const std::string& prefix) const;
    
    // === Stable Particle ID System (MINIMAL and up) ===
    // Two-tier system: KGParticleID (stable) → RenderIndex (temporary)

    // Create stable particle ID and map to render index
    KGParticleID createKGParticle(EntityID entity, RenderIndex initial_render_index);

    // Destroy particle and remove mapping
    void destroyKGParticle(KGParticleID kg_id);

    // Query current render index for KG particle
    RenderIndex getRenderIndex(KGParticleID kg_id) const;

    // Update render index mapping (internal use by ParticleSystem)
    void updateRenderIndex(KGParticleID kg_id, RenderIndex new_index);

    // Called by ParticleSystem when swap-and-pop happens
    void notifyParticleSwap(RenderIndex old_index, RenderIndex new_index);

    // Called by ParticleSystem when particle is actually deleted (after GPU delay)
    void eraseRenderIndexMappings(RenderIndex index);

    // Get all KG particle IDs for an entity
    std::vector<KGParticleID> getEntityKGParticles(EntityID entity) const;

    // Find entity that owns a KG particle
    EntityID getEntityByKGParticle(KGParticleID kg_id) const;

    // Find all entities with particles at the given render index (for UI inspector)
    std::vector<EntityID> getEntitiesByRenderIndex(RenderIndex render_idx) const;

    // Fast O(1) lookup: get owning entity from render index (for collision detection)
    EntityID getEntityByRenderIndex(RenderIndex render_idx) const;

    // Batch form of the above, taking the KG lock once. See KGCore for why:
    // the per-index accessor inside a 14-thread loop measured 275 ns a call.
    void snapshotRenderIndexToEntity(std::vector<EntityID>& out, size_t count) const;

    // Stable KGParticleID currently mapped to a render index
    // (INVALID_KG_PARTICLE_ID if the slot is not KG-backed). The
    // interaction system uses this to bind long-running transformation
    // effects to stable identity when a trigger hands it a raw index.
    KGParticleID getKGParticleByRenderIndex(RenderIndex render_idx) const;

    // Get all KG particles recursively via relationships (for hierarchical entities like humanoids)
    std::vector<KGParticleID> getEntityKGParticlesRecursive(EntityID entity, const std::string& relation = "HAS_PART") const;

    // === Particle Data Storage (for reload) ===

    // Store complete particle data for a KG particle (enables chunk reload)
    void setKGParticleData(KGParticleID kg_id, const Particle& particle_data);

    // Get stored particle data (returns default Particle if not found)
    Particle getKGParticleData(KGParticleID kg_id) const;

    // Check if particle data exists for KG particle
    bool hasKGParticleData(KGParticleID kg_id) const;

    // === Gluon Storage (for physics entity reload) ===
    // Stores gluon constraints between KG particles so they can be recreated on chunk load

    // Create gluon record linking two KG particles
    KGGluonID createKGGluon(EntityID entity, KGParticleID particle_a, KGParticleID particle_b);

    // Store gluon parameters (type, offsets, stiffness, etc.)
    void setKGGluonData(KGGluonID kg_id, const KGGluonData& gluon_data);

    // Get stored gluon data
    KGGluonData getKGGluonData(KGGluonID kg_id) const;

    // Check if gluon data exists
    bool hasKGGluonData(KGGluonID kg_id) const;

    // Get all gluons for an entity
    std::vector<KGGluonID> getEntityKGGluons(EntityID entity) const;

    // Get the two KG particles connected by a gluon
    std::pair<KGParticleID, KGParticleID> getKGGluonParticles(KGGluonID kg_id) const;

    // Destroy a gluon record
    void destroyKGGluon(KGGluonID kg_id);

    // === High-level Helpers ===

    // Create child entity, bind particles, and link to parent in one operation
    EntityID createChildEntityWithParticles(
        EntityID parent_entity,
        const std::string& child_type,
        const std::vector<RenderIndex>& particle_indices,
        const std::string& relation = "HAS_PART"
    );

    // Unload entity: clear KG particle bindings, mark unloaded, return render indices
    // Entity record stays in KG (append-only permaworld)
    UnloadResult unloadEntity(EntityID entity_id);

    // === Query Operations ===

    // Find all entities of a given type
    std::vector<EntityID> findByType(const std::string& type) const;

    // Find all entities with a specific property value
    std::vector<EntityID> findByProperty(const std::string& key, const PropertyValue& value) const;

    // === Combat Support ===

    // Get body part name for a particle (for hit detection)
    // Returns the body_part property from the entity that owns this particle
    // Asserts if particle has no owning entity or entity lacks body_part property
    std::string getParticleBodyPart(EntityID humanoid_entity, RenderIndex particle_render_idx) const;

    // === Physics Queries ===

    // Get total mass of an entity by summing all its particle masses
    // Returns 0.0f if entity not found or disabled
    float getEntityMass(EntityID entity_id) const;

    // === Performance & Debug ===

    // Get memory usage in bytes
    size_t getMemoryUsage() const;
    
    // Get stats for debugging
    struct Stats {
        size_t entity_count = 0;
        size_t relation_count = 0;
        size_t event_count = 0;
        size_t memory_bytes = 0;
        float last_update_ms = 0;
    };
    Stats getStats() const;
    
private:
    OntologyRegistry registry_;
    KGMode current_mode;
    KGConfig config;

    // Core implementation - only allocated when mode != DISABLED
    std::unique_ptr<KGCore> core;

    // Event bus for state change notifications (optional, null = no events)
    logosphere::EventBus* event_bus_ = nullptr;

    // Helper to check if operation is valid in current mode
    bool checkEnabled(const char* operation) const;
};

} // namespace kg

#endif // KG_MODULE_H
