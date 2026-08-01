// Ontology layering: universe core, setting packs, game extensions.
//
// The core describes what the ENGINE understands - bodies, materials,
// joints, events, solver authority - and nothing about any particular
// world. Astronomy, trees, soil: those are settings, and a world only
// gains them when a game asks.
//
// The contract has two halves, and the second is the one that makes
// it real:
//
//   1. a pack's vocabulary is available once loaded
//   2. it is NOT available otherwise, and asking for it fails loudly
//      rather than half-working
//
// Without (2) a "pack" is only documentation. createEntity already
// rejects unknown types, so the enforcement was free; what this test
// holds is that the core genuinely stopped carrying the setting.
//
// Usage:
//   ./build/test_ontology_packs

#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/space_ontology_registry.h"

#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

// The core knows engine concepts and no astronomy.
void test_core_carries_no_setting() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    // Engine concepts stay in the core: these are what the physics,
    // damage and event systems are built on.
    CHECK(kg.createEntity("Wall") != kg::INVALID_ENTITY,
          "a body is a core concept");
    CHECK(kg.createEntity("Constraint") != kg::INVALID_ENTITY,
          "so is a constraint");

    // Astronomy is not. Rejected, loudly - the [KG] REJECTED lines
    // above this are the point, not noise.
    CHECK(kg.createEntity("Planet") == kg::INVALID_ENTITY,
          "a core-only world has no Planet — if this passes, the "
          "setting leaked back into the core");
    CHECK(kg.createEntity("Sky") == kg::INVALID_ENTITY,
          "nor a Sky");
    CHECK(kg.createEntity("CelestialBody") == kg::INVALID_ENTITY,
          "nor any celestial body");
}

// Asking for the pack grants exactly that vocabulary.
void test_pack_grants_its_vocabulary() {
    kg::KGModule kg(space::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    CHECK(kg.createEntity("Planet") != kg::INVALID_ENTITY,
          "the space pack brings Planet");
    CHECK(kg.createEntity("Sky") != kg::INVALID_ENTITY,
          "and Sky");
    CHECK(kg.createEntity("CelestialBody") != kg::INVALID_ENTITY,
          "and CelestialBody");

    // A pack sits ON the core rather than replacing it: everything
    // the engine understands is still there.
    CHECK(kg.createEntity("Wall") != kg::INVALID_ENTITY,
          "and the core underneath it is intact");
}

// A pack loaded after the fact reaches an already-running world, which
// is what lets a game add a setting when the human asks for one.
void test_a_pack_can_be_added_to_a_running_world() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    CHECK(kg.createEntity("Planet") == kg::INVALID_ENTITY,
          "no sky before the pack");

    kg.extendOntology(space::ontology::registry());

    CHECK(kg.createEntity("Planet") != kg::INVALID_ENTITY,
          "and a sky after it — a setting can arrive mid-world");
    CHECK(kg.createEntity("Wall") != kg::INVALID_ENTITY,
          "without disturbing what was already there");
}

// Pack properties are validated like any other: a pack is real
// ontology, not a bag of loose strings.
void test_pack_properties_are_validated() {
    kg::KGModule kg(space::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    auto planet = kg.createEntity("Planet");
    CHECK(planet != kg::INVALID_ENTITY, "planet exists");

    kg.setProperty(planet, "planet_radius", "4");
    CHECK(kg.getProperty(planet, "planet_radius") == "4",
          "a pack slot round-trips");
}

}  // namespace

int main() {
    std::cout << "Ontology packs (core / setting / game)" << std::endl;
    std::cout << "note: [KG] REJECTED lines below are EXPECTED — they "
                 "are the contract being enforced." << std::endl;
    test_core_carries_no_setting();
    test_pack_grants_its_vocabulary();
    test_a_pack_can_be_added_to_a_running_world();
    test_pack_properties_are_validated();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
