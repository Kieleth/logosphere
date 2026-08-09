#include "../entity_manager.h"
#include <iostream>
#include <unordered_map>

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
    std::unordered_map<kg::KGParticleID, size_t> kg_to_index;

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

            kg_to_index[kg_id] = result.particles.size();
            result.particles.push_back(p);
            result.kg_particle_ids.push_back(kg_id);
        } else {
            std::cerr << "[VegetationActivator] ERROR: KG particle " << kg_id
                      << " has no stored data!" << std::endl;
        }
    }

    // ========================================================================
    // GLUONS, recursively (issue #47). This activator loaded particles through
    // HAS_PART and then dropped every bond the generator stored: a grass patch
    // materialised as towers of free ultra-thin plates, and on that stack the
    // contact solver DIVERGES (measured 16k -> 4.5e8 impulse in one frame,
    // grass shrapnel at ~100 m/s: the #38 canopy scatter and #42 explosions).
    // Gluons live on each entity in the hierarchy (a blade's bonds are on the
    // blade, not the patch), so collect them with the same HAS_PART walk the
    // particles used. The rock activator has always done this one level deep;
    // vegetation needs the recursive form.
    // ========================================================================
    std::vector<kg::EntityID> to_visit = {entity_id};
    size_t skipped = 0;
    while (!to_visit.empty()) {
        kg::EntityID cur = to_visit.back();
        to_visit.pop_back();
        for (kg::EntityID child : kg->getRelated(cur, "HAS_PART"))
            to_visit.push_back(child);

        for (kg::KGGluonID gid : kg->getEntityKGGluons(cur)) {
            auto pair = kg->getKGGluonParticles(gid);
            auto ia = kg_to_index.find(pair.first);
            auto ib = kg_to_index.find(pair.second);
            if (ia == kg_to_index.end() || ib == kg_to_index.end()) {
                // A bond whose particle did not load. Counted and printed:
                // silently dropping bonds is the defect this block fixes.
                skipped++;
                continue;
            }
            GluonRequest req;
            req.kg_gluon_id = gid;
            req.particle_a_index = ia->second;
            req.particle_b_index = ib->second;
            result.gluon_requests.push_back(req);
        }
    }
    std::cout << "[VegetationActivator] Entity " << entity_id << ": "
              << result.gluon_requests.size() << " gluon requests";
    if (skipped) std::cout << " (" << skipped << " SKIPPED: particle not loaded)";
    std::cout << std::endl;

    return result;
}
