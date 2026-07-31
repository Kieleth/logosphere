#ifndef ENTITY_ACTIVATORS_H
#define ENTITY_ACTIVATORS_H

#include "../entity_manager.h"

// Forward declarations of entity activator functions

// Vegetation entity activator - hierarchical loading via has_part relationships
// Used for: trees, grass, bushes, vines - any organic entity with child parts
ActivationResult vegetation_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
);

// Simple entities - cube, light (single particle from properties)
ActivationResult simple_entity_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
);

// Rock entities - multi-particle entities with particles stored directly on entity
// (not in child entities like vegetation)
ActivationResult rock_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
);

// Physics rock entities - dynamic rocks with gluon constraints
// Returns particles AND gluon_requests for physics constraint creation
ActivationResult physics_rock_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
);

// Physics tree entities - dynamic trees with gluon constraints
// Returns particles AND gluon_requests for physics constraint creation
ActivationResult physics_tree_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
);

#endif // ENTITY_ACTIVATORS_H
