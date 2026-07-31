#ifndef ENTITY_GENERATOR_H
#define ENTITY_GENERATOR_H

#include <algorithm>
#include <vector>
#include <cmath>
#include <iostream>
#include "logosphere/kg/kg_types.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"

// Forward declarations
class Engine;
namespace logosphere { class EventBus; }

// Base class for all procedural entity generators
// Provides common initialization and entity tracking
// Destruction is handled by KG (no special destroy methods needed)
class EntityGenerator {
protected:
    Engine* engine_;
    kg::KGModule* kg_;
    logosphere::EventBus* event_bus_ = nullptr;
    std::vector<kg::EntityID> tracked_entities_;

public:
    EntityGenerator() : engine_(nullptr), kg_(nullptr) {}
    virtual ~EntityGenerator() = default;

    // Initialize with engine and KG references
    virtual void initialize(Engine* engine, kg::KGModule* kg) {
        engine_ = engine;
        kg_ = kg;
    }

    void set_event_bus(logosphere::EventBus* bus) { event_bus_ = bus; }

    // Optional lifecycle hook - called after entity creation
    // Automatically tracks entities for queries and emits SpawnEvent
    virtual void on_entity_created(kg::EntityID entity);

    // Optional lifecycle hook - called when entity destroyed externally
    // Allows generators to clean up tracking
    virtual void on_entity_destroyed(kg::EntityID entity) {
        tracked_entities_.erase(
            std::remove(tracked_entities_.begin(), tracked_entities_.end(), entity),
            tracked_entities_.end()
        );
    }

    // Get all entities created by this generator
    const std::vector<kg::EntityID>& get_tracked_entities() const {
        return tracked_entities_;
    }

    int get_entity_count() const {
        return tracked_entities_.size();
    }

protected:
    // Phase 2.5: Generic constraint creation for all entity types
    // Entities declare structure (A↔B with stiffness X), don't care about physics internals
    // All entities must be chunk-based using KGParticleID
    void create_constraint(kg::EntityID entity_id,
                          kg::KGParticleID particle_a,
                          kg::KGParticleID particle_b,
                          float stiffness) {
        if (!kg_) {
            std::cerr << "[EntityGenerator] Cannot create constraint - KG not initialized!" << std::endl;
            return;
        }

        // Calculate rest distance from current particle positions in KG
        Particle p_a_data = kg_->getKGParticleData(particle_a);
        Particle p_b_data = kg_->getKGParticleData(particle_b);

        float dx = p_b_data.x - p_a_data.x;
        float dy = p_b_data.y - p_a_data.y;
        float dz = p_b_data.z - p_a_data.z;
        float rest_distance = std::sqrt(dx*dx + dy*dy + dz*dz);

        // Create constraint entity in KG
        kg::EntityID constraint_id = kg_->createEntity("Constraint");

        // Store constraint properties (KGParticleID values as strings)
        kg_->setProperty(constraint_id, "entity_id", std::to_string(entity_id));
        kg_->setProperty(constraint_id, "particle_a", std::to_string(particle_a));
        kg_->setProperty(constraint_id, "particle_b", std::to_string(particle_b));
        kg_->setProperty(constraint_id, "rest_distance", std::to_string(rest_distance));
        kg_->setProperty(constraint_id, "base_stiffness", std::to_string(stiffness));

        // Create relation: entity has_constraint constraint
        kg_->createRelation(entity_id, "HAS_CONSTRAINT", constraint_id);
    }

};

#endif // ENTITY_GENERATOR_H
