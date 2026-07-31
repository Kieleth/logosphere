#include "../entity_manager.h"
#include <iostream>

/**
 * Vegetation Entity Activator
 *
 * Handles hierarchical vegetation entities:
 *   - Trees: tree_root → branches → leaves
 *   - Grass: grass_patch → grass blades
 *   - Bushes: bush_root → branches → leaves
 *   - Vines: vine_root → segments
 *
 * Uses recursive loading to collect all particles from the hierarchy.
 * Particle data is stored in KG during creation for reload.
 */
ActivationResult vegetation_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
) {
    ActivationResult result;

    // Use recursive loading for hierarchical vegetation
    // Follows "HAS_PART" relationships to get all child entities
    auto kg_particles = kg->getEntityKGParticlesRecursive(entity_id, "HAS_PART");

    std::cout << "[VegetationActivator] Entity " << entity_id << " has "
              << kg_particles.size() << " particles (recursive)" << std::endl;

    // Load each particle from stored KG data
    for (kg::KGParticleID kg_id : kg_particles) {
        if (kg->hasKGParticleData(kg_id)) {
            Particle p = kg->getKGParticleData(kg_id);

            // ⚠️ WARNING: DO NOT SET entity_id! See particle.h MONSTER WARNING!
            // Setting entity_id breaks GPU shadow pipeline (Dec 2024 bug).
            // Object picking is disabled until this is fixed.
            // p.entity_id = entity_id;  // DISABLED - BREAKS SHADOWS!

            // Trees are static scenery - force at_rest to prevent cascade wake
            // during settling. Stale KG data may have is_at_rest=false.
            p.is_at_rest = true;

            result.particles.push_back(p);
            result.kg_particle_ids.push_back(kg_id);
        } else {
            std::cerr << "[VegetationActivator] ERROR: KG particle " << kg_id
                      << " has no stored data!" << std::endl;
        }
    }

    return result;
}
