// Damage system → ParticleSystem bridge.
//
// `DamageSystem::apply_to_particle` is the only surface that requires
// the spatial query (to resolve particle → owning entity). Splitting
// it into its own TU means the rest of damage_system.cpp compiles
// without entity_spatial.h, which transitively pulls in ParticleSystem
// and physics headers.
//
// Excluded from headless-core builds; included in the full engine.

#include "logosphere/damage/damage_system.h"
#include "spatial/entity_spatial.h"
#include "logosphere/kg/kg_types.h"
#include <iostream>

float DamageSystem::apply_to_particle(int particle_id, float damage, DamageType type) {
    if (!spatial_) {
        std::cerr << "[DamageSystem] No spatial query set, can't resolve particle" << std::endl;
        return -1.0f;
    }

    auto data = spatial_->get_particle_data(particle_id);
    if (data.owner == kg::INVALID_ENTITY) {
        std::cerr << "[DamageSystem] WARN: particle " << particle_id
                  << " has no KG entity owner - damage not applied" << std::endl;
        return -1.0f;
    }

    return apply(data.owner, damage, type);
}
