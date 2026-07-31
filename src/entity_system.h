#ifndef ENTITY_SYSTEM_H
#define ENTITY_SYSTEM_H

#include <vector>
#include <unordered_map>
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"

// Forward declaration
class ParticleDynamicsSystem;

// EDUCATIONAL NOTE: Entity System
//
// An entity is a group of particles that move and behave together.
// For example, Eva is an entity made of 3 particles (head, torso, legs).
// When we move Eva, all three particles move together maintaining their
// relative positions.
//
// This is the simplest possible implementation - just tracking which
// particles belong together and moving them as a unit.

class EntitySystem {
public:
    EntitySystem(ParticleSystem& particle_system, kg::KGModule* kg_module = nullptr,
                 ParticleDynamicsSystem* dynamics_system = nullptr)
        : particle_system_(particle_system), kg_module_(kg_module),
          dynamics_system_(dynamics_system) {}
    
    // DEPRECATED: Direct position manipulation fights physics constraints
    // Use setEntityVelocity() instead - see docs/PHYSICS_AND_ANIMATIONS.md
    void moveEntity(kg::EntityID entity_id, float dx, float dy, float dz);

    // PHYSICS-DRIVEN: Set velocity on all entity particles (m/s)
    // Physics will integrate velocity → position, gluons maintain structure
    void setEntityVelocity(kg::EntityID entity_id, float vx, float vy, float vz);

    // Move a particle (or its entity if it belongs to one)
    void moveParticleOrEntity(int particle_id, float dx, float dy, float dz);
    
    // Check if a particle belongs to an entity
    bool isPartOfEntity(int particle_id) const;
    
    // Get the entity that owns a particle (or INVALID_ENTITY if none)
    kg::EntityID getEntityForParticle(int particle_id) const;

    // Remove an entity completely:
    // 1. Unregisters from dynamics system (if humanoid)
    // 2. Removes all particles (handles swap-and-pop correctly)
    // 3. Removes entity from KG (if destroy_kg_entity = true)
    // Returns number of particles removed
    int removeEntity(kg::EntityID entity_id, bool destroy_kg_entity = true);

private:
    ParticleSystem& particle_system_;
    kg::KGModule* kg_module_;  // Optional KG integration
    ParticleDynamicsSystem* dynamics_system_;  // Optional, for humanoid cleanup
};

#endif // ENTITY_SYSTEM_H