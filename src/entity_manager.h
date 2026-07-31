#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include "logosphere/kg/kg_module.h"
#include "particle.h"
#include "core/particle_system.h"
#include <vector>
#include <functional>
#include <unordered_map>

// Forward declarations
class PhysicsSystem;
namespace logosphere { class EventBus; }

// Gluon request - indices into particles vector (resolved to render indices by EntityManager)
struct GluonRequest {
    kg::KGGluonID kg_gluon_id;  // KG ID to look up gluon data
    size_t particle_a_index;    // Index into ActivationResult::particles
    size_t particle_b_index;    // Index into ActivationResult::particles
};

// Entity activation result
struct ActivationResult {
    std::vector<Particle> particles;              // Particles to create
    std::vector<kg::KGParticleID> kg_particle_ids;  // For tracking
    std::vector<GluonRequest> gluon_requests;     // Gluons to create after particles
};

// Entity factory function type
// Takes: KG module, entity ID, particle system
// Returns: Particles to create and their KG IDs
using EntityActivator = std::function<ActivationResult(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
)>;

/**
 * EntityManager - Type-based entity activation system
 *
 * Replaces hardcoded type checking in generators with polymorphic dispatch.
 * Each entity type registers an activator function that knows how to:
 * - Read entity state from KG
 * - Build particles for rendering
 * - Return particle data and KG bindings
 *
 * Benefits:
 * - No hardcoded type checks in generators
 * - Adding new entity type = register activator function
 * - Unknown types log warning instead of crashing
 * - Entity logic isolated in activator functions
 */
class EntityManager {
public:
    EntityManager(kg::KGModule* kg, ParticleSystem& particle_system,
                  PhysicsSystem* physics = nullptr, logosphere::EventBus* event_bus = nullptr);

    void set_event_bus(logosphere::EventBus* bus) { event_bus_ = bus; }

    // Register entity type handler
    void register_type(const std::string& type, EntityActivator activator);

    // Activate entity (creates particles and gluons, returns KG particle IDs)
    ActivationResult activate_entity(kg::EntityID entity_id);

    // Deactivate entity (removes particles, clears KG mappings)
    void deactivate_entity(kg::EntityID entity_id);

    // Create gluon from KG data between two render indices
    // Used by ChunkSystem after particles are created
    void create_gluon_from_kg(kg::KGGluonID kg_gluon_id, int render_a, int render_b);

private:
    kg::KGModule* kg_;
    ParticleSystem& particle_system_;
    PhysicsSystem* physics_;
    logosphere::EventBus* event_bus_ = nullptr;
    std::unordered_map<std::string, EntityActivator> type_registry_;
};

#endif // ENTITY_MANAGER_H
