// Rock Entity Activators
//
// Two activators for different rock types:
//
// 1. rock_activator - Static scenery rocks (no physics gluons)
//    - Particles stored directly on entity (not in children)
//    - No gluon connections
//    - Forces at_rest=true
//
// 2. physics_rock_activator - Physics-enabled rocks with gluon constraints
//    - Particles stored on entity with KGGluon records
//    - Returns gluon_requests for chunk system to create constraints
//    - Dynamic physics (at_rest=false, can be pushed/broken)

#include "entity_activators.h"
#include "logosphere/kg/kg_module.h"
#include "core/particle_system.h"
#include "particle.h"
#include <iostream>

ActivationResult rock_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
) {
    ActivationResult result;

    // Get particles directly from rock entity (non-recursive)
    // Unlike vegetation which uses "HAS_PART" relationships,
    // rocks store all particles on the rock entity itself
    auto kg_particles = kg->getEntityKGParticles(entity_id);

    std::cout << "[RockActivator] Entity " << entity_id << " has "
              << kg_particles.size() << " particles (direct)" << std::endl;

    // Load each particle from stored KG data
    for (kg::KGParticleID kg_id : kg_particles) {
        if (kg->hasKGParticleData(kg_id)) {
            Particle p = kg->getKGParticleData(kg_id);

            // DIAGNOSTIC: Print particle data for debugging
            std::cout << "[RockActivator]   KG particle " << kg_id
                      << " pos=(" << p.x << "," << p.y << "," << p.z << ")"
                      << " size=" << p.size
                      << " shape=" << static_cast<int>(p.shape)
                      << " w/h/t=(" << p.width << "," << p.height << "," << p.thickness << ")"
                      << " is_light=" << p.is_light_source
                      << " mass=" << p.GetMass()
                      << std::endl;

            // ⚠️ WARNING: DO NOT SET entity_id! See particle.h MONSTER WARNING!
            // Setting entity_id breaks GPU shadow pipeline (Dec 2024 bug).
            // Object picking is disabled until this is fixed.
            // p.entity_id = entity_id;  // DISABLED - BREAKS SHADOWS!

            // Rocks are static scenery - force at_rest to prevent cascade wake
            // during settling. Stale KG data may have is_at_rest=false.
            p.is_at_rest = true;

            result.particles.push_back(p);
            result.kg_particle_ids.push_back(kg_id);
        } else {
            std::cerr << "[RockActivator] ERROR: KG particle " << kg_id
                      << " has no stored data!" << std::endl;
        }
    }

    return result;
}

// ============================================================================
// PHYSICS ROCK ACTIVATOR - Dynamic rocks with gluon constraints
// ============================================================================

ActivationResult physics_rock_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
) {
    (void)particle_system;  // Not used directly - particles added by chunk system

    ActivationResult result;

    // Get particles directly from physics_rock entity
    auto kg_particles = kg->getEntityKGParticles(entity_id);

    std::cout << "[PhysicsRockActivator] Entity " << entity_id << " has "
              << kg_particles.size() << " particles" << std::endl;

    // Build a map from KGParticleID to index in result.particles
    // (needed for gluon_requests which use indices)
    std::unordered_map<kg::KGParticleID, size_t> kg_to_index;

    // Load each particle from stored KG data
    for (kg::KGParticleID kg_id : kg_particles) {
        if (kg->hasKGParticleData(kg_id)) {
            Particle p = kg->getKGParticleData(kg_id);

            std::cout << "[PhysicsRockActivator]   KG particle " << kg_id
                      << " pos=(" << p.x << "," << p.y << "," << p.z << ")"
                      << " w/h/t=(" << p.width << "," << p.height << "," << p.thickness << ")"
                      << " mass=" << p.GetMass() << std::endl;

            // Physics rocks are DYNAMIC - don't force at_rest
            // They can be pushed, knocked over, broken apart
            p.is_at_rest = false;

            size_t idx = result.particles.size();
            kg_to_index[kg_id] = idx;

            result.particles.push_back(p);
            result.kg_particle_ids.push_back(kg_id);
        } else {
            std::cerr << "[PhysicsRockActivator] ERROR: KG particle " << kg_id
                      << " has no stored data!" << std::endl;
        }
    }

    // Get gluons for this entity and create gluon requests
    auto kg_gluons = kg->getEntityKGGluons(entity_id);
    std::cout << "[PhysicsRockActivator] Entity " << entity_id << " has "
              << kg_gluons.size() << " gluons" << std::endl;

    for (kg::KGGluonID kg_gluon_id : kg_gluons) {
        auto [kg_particle_a, kg_particle_b] = kg->getKGGluonParticles(kg_gluon_id);

        // Find indices in result.particles
        auto it_a = kg_to_index.find(kg_particle_a);
        auto it_b = kg_to_index.find(kg_particle_b);

        if (it_a != kg_to_index.end() && it_b != kg_to_index.end()) {
            GluonRequest req;
            req.kg_gluon_id = kg_gluon_id;
            req.particle_a_index = it_a->second;
            req.particle_b_index = it_b->second;
            result.gluon_requests.push_back(req);

            std::cout << "[PhysicsRockActivator]   Gluon " << kg_gluon_id
                      << " between particles " << it_a->second << " and " << it_b->second
                      << std::endl;
        } else {
            std::cerr << "[PhysicsRockActivator] ERROR: Gluon " << kg_gluon_id
                      << " references unknown particles!" << std::endl;
        }
    }

    return result;
}

// ============================================================================
// PHYSICS TREE ACTIVATOR - Dynamic trees with gluon constraints
// ============================================================================

ActivationResult physics_tree_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
) {
    (void)particle_system;  // Not used directly - particles added by chunk system

    ActivationResult result;

    // Get particles directly from physics_tree entity
    auto kg_particles = kg->getEntityKGParticles(entity_id);

    std::cout << "[PhysicsTreeActivator] Entity " << entity_id << " has "
              << kg_particles.size() << " particles" << std::endl;

    // Build a map from KGParticleID to index in result.particles
    std::unordered_map<kg::KGParticleID, size_t> kg_to_index;

    // Load each particle from stored KG data
    for (kg::KGParticleID kg_id : kg_particles) {
        if (kg->hasKGParticleData(kg_id)) {
            Particle p = kg->getKGParticleData(kg_id);

            // Physics trees are DYNAMIC - don't force at_rest
            p.is_at_rest = false;

            size_t idx = result.particles.size();
            kg_to_index[kg_id] = idx;

            result.particles.push_back(p);
            result.kg_particle_ids.push_back(kg_id);
        } else {
            std::cerr << "[PhysicsTreeActivator] ERROR: KG particle " << kg_id
                      << " has no stored data!" << std::endl;
        }
    }

    // Get gluons for this entity and create gluon requests
    auto kg_gluons = kg->getEntityKGGluons(entity_id);
    std::cout << "[PhysicsTreeActivator] Entity " << entity_id << " has "
              << kg_gluons.size() << " gluons" << std::endl;

    for (kg::KGGluonID kg_gluon_id : kg_gluons) {
        auto [kg_particle_a, kg_particle_b] = kg->getKGGluonParticles(kg_gluon_id);

        // Find indices in result.particles
        auto it_a = kg_to_index.find(kg_particle_a);
        auto it_b = kg_to_index.find(kg_particle_b);

        if (it_a != kg_to_index.end() && it_b != kg_to_index.end()) {
            GluonRequest req;
            req.kg_gluon_id = kg_gluon_id;
            req.particle_a_index = it_a->second;
            req.particle_b_index = it_b->second;
            result.gluon_requests.push_back(req);
        } else {
            std::cerr << "[PhysicsTreeActivator] ERROR: Gluon " << kg_gluon_id
                      << " references unknown particles!" << std::endl;
        }
    }

    return result;
}
