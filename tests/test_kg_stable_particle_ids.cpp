// Tests must assert in every build type. Release passes -DNDEBUG,
// which turns assert() into a no-op and made this suite green by
// vacuity (and abort where asserts carried side effects). Undef
// BEFORE any include so <cassert> re-expands assert() for real.
#undef NDEBUG

#include "test_harness.h"
#include "test_context.h"
#include "logosphere/kg/kg_core.h"
#include "logosphere/kg/ontology_registry.h"
#include <cassert>
#include <iostream>

// Test the stable KG particle ID system
bool test_kg_stable_particle_ids(TestContext& ctx) {
    kg::KGConfig config;
    kg::OntologyRegistry empty_registry;  // empty = no validation (test mode)
    kg::KGCore kg(config, empty_registry);

    // Create a test entity
    kg::EntityID entity = kg.createEntity("test_entity");
    assert(entity != kg::INVALID_ENTITY);

    // Test 1: Create KG particles with different render indices
    kg::KGParticleID kg_id1 = kg.createKGParticle(entity, 0);
    kg::KGParticleID kg_id2 = kg.createKGParticle(entity, 1);
    kg::KGParticleID kg_id3 = kg.createKGParticle(entity, 2);

    // Verify IDs are unique and start from 1
    assert(kg_id1 > 0 && "KG particle IDs start from 1");
    assert(kg_id2 > 0 && "KG particle IDs start from 1");
    assert(kg_id3 > 0 && "KG particle IDs start from 1");
    assert(kg_id1 != kg_id2 && "KG particle IDs are unique");
    assert(kg_id2 != kg_id3 && "KG particle IDs are unique");
    assert(kg_id1 != kg_id3 && "KG particle IDs are unique");

    // Test 2: Verify render index mapping
    assert(kg.getRenderIndex(kg_id1) == 0);
    assert(kg.getRenderIndex(kg_id2) == 1);
    assert(kg.getRenderIndex(kg_id3) == 2);

    // Test 3: Update render indices (simulating swap-and-pop)
    kg.updateRenderIndex(kg_id1, 100);
    kg.updateRenderIndex(kg_id2, 200);

    assert(kg.getRenderIndex(kg_id1) == 100);
    assert(kg.getRenderIndex(kg_id2) == 200);
    assert(kg.getRenderIndex(kg_id3) == 2);  // Unchanged

    // Test 4: Verify entity-to-particles mapping
    auto kg_particles = kg.getEntityKGParticles(entity);
    assert(kg_particles.size() == 3);

    // Verify all KG IDs are present
    bool found1 = false, found2 = false, found3 = false;
    for (kg::KGParticleID kg_id : kg_particles) {
        if (kg_id == kg_id1) found1 = true;
        if (kg_id == kg_id2) found2 = true;
        if (kg_id == kg_id3) found3 = true;
    }
    assert(found1 && found2 && found3 && "All KG IDs found in entity");

    // Test 5: Verify particle-to-entity mapping
    assert(kg.getEntityByKGParticle(kg_id1) == entity);
    assert(kg.getEntityByKGParticle(kg_id2) == entity);
    assert(kg.getEntityByKGParticle(kg_id3) == entity);

    // Test 6: Test getEntitiesByRenderIndex (UI inspector support)
    auto entities_at_100 = kg.getEntitiesByRenderIndex(100);
    assert(entities_at_100.size() == 1);
    assert(entities_at_100[0] == entity);

    auto entities_at_200 = kg.getEntitiesByRenderIndex(200);
    assert(entities_at_200.size() == 1);
    assert(entities_at_200[0] == entity);

    auto entities_at_2 = kg.getEntitiesByRenderIndex(2);
    assert(entities_at_2.size() == 1);
    assert(entities_at_2[0] == entity);

    // Test 7: Test notifyParticleSwap (critical for swap-and-pop)
    // Simulate: particle at index 200 is removed, last particle (index 2) moves to 200
    kg.notifyParticleSwap(2, 200);  // old_index=2 moves to new_index=200

    // Now kg_id3 should map to render index 200 (was moved)
    assert(kg.getRenderIndex(kg_id3) == 200);
    // kg_id2 should still map to 200 (was already there)
    assert(kg.getRenderIndex(kg_id2) == 200);

    // Test 8: Destroy KG particle
    kg.destroyKGParticle(kg_id1);

    assert(kg.getRenderIndex(kg_id1) == kg::INVALID_RENDER_INDEX);
    assert(kg.getEntityByKGParticle(kg_id1) == kg::INVALID_ENTITY);

    // Entity should now have 2 particles
    kg_particles = kg.getEntityKGParticles(entity);
    assert(kg_particles.size() == 2);

    // Test 9: Destroy entity should clean up all KG particles
    kg.destroyEntity(entity);

    assert(kg.getRenderIndex(kg_id2) == kg::INVALID_RENDER_INDEX);
    assert(kg.getRenderIndex(kg_id3) == kg::INVALID_RENDER_INDEX);
    assert(kg.getEntityKGParticles(entity).empty());

    std::cout << "[TEST] Stable KG particle IDs: PASS" << std::endl;
    return true;
}
