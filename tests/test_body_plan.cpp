// Body plan template tests
//
// Verifies the declare_biped/serpent/winged/quadruped helpers set the
// correct capability properties on entities, and that create_capability_part
// builds body part entities with the expected KG bindings.
//
// Usage:
//   ./build/test_body_plan

#include "logosphere/capability/body_plan.h"
#include "logosphere/capability/capability_profile.h"
#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/earth_ontology_registry.h"
#include <iostream>
#include <cassert>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

static std::unique_ptr<kg::KGModule> make_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    // Snakes, butterflies and branches are earth-like vocabulary, so
    // this test asks for the pack that defines them. The core alone
    // has no botany; see docs/ONTOLOGY_LAYERS.md.
    kg->extendOntology(earth::ontology::registry());

    kg->setMode(kg::KGMode::MINIMAL);
    return kg;
}

void test_declare_biped_sets_capabilities() {
    auto kg = make_kg();
    auto entity = kg->createEntity("Humanoid");
    body_plan::declare_biped(*kg, entity);

    ASSERT(kg->getProperty(entity, "cap.locomotion.expected_count") == "2",
           "biped locomotion expected=2");
    ASSERT(kg->getProperty(entity, "cap.locomotion.default_mode") == "average",
           "biped locomotion mode=average");
    ASSERT(kg->getProperty(entity, "cap.manipulation.expected_count") == "2",
           "biped manipulation expected=2");
    ASSERT(kg->getProperty(entity, "cap.rotation.default_mode") == "minimum",
           "biped rotation mode=minimum");
    ASSERT(kg->getProperty(entity, "cap.perception.default_mode") == "minimum",
           "biped perception mode=minimum");
}

void test_declare_serpent_sets_segment_count() {
    auto kg = make_kg();
    auto entity = kg->createEntity("Snake");
    body_plan::declare_serpent(*kg, entity, /*segment_count=*/20);

    ASSERT(kg->getProperty(entity, "cap.undulation.expected_count") == "20",
           "serpent undulation expected=20");
    ASSERT(kg->getProperty(entity, "cap.undulation.default_mode") == "weighted_sum",
           "serpent undulation mode=weighted_sum");
    ASSERT(kg->getProperty(entity, "cap.undulation.normalize") == "1.0",
           "serpent undulation normalize=1.0");
}

void test_declare_winged_computes_flight_parts() {
    auto kg = make_kg();
    auto entity = kg->createEntity("Butterfly");
    body_plan::declare_winged(*kg, entity, /*wing_pairs=*/2);
    // flight_parts = wing_pairs * 2 + 1 (thorax) = 5

    ASSERT(kg->getProperty(entity, "cap.flight.expected_count") == "5",
           "winged(2 pairs) flight expected=5");
    ASSERT(kg->getProperty(entity, "cap.flight.default_mode") == "binary",
           "winged flight mode=binary");
}

void test_declare_quadruped_four_legs() {
    auto kg = make_kg();
    auto entity = kg->createEntity("Humanoid");  // abuse humanoid as a quadruped for test
    body_plan::declare_quadruped(*kg, entity);

    ASSERT(kg->getProperty(entity, "cap.locomotion.expected_count") == "4",
           "quadruped locomotion expected=4");
    ASSERT(kg->getProperty(entity, "cap.locomotion.default_mode") == "average",
           "quadruped locomotion mode=average");
}

void test_create_capability_part_links_and_binds() {
    auto kg = make_kg();
    auto entity = kg->createEntity("Humanoid");
    body_plan::declare_biped(*kg, entity);

    auto left_leg = body_plan::create_capability_part(
        *kg, entity, "Leg", "left_leg", 100.0f, "locomotion", 1.0f, "left");

    ASSERT(kg->exists(left_leg), "body part entity created");
    ASSERT(kg->getType(left_leg) == "Leg", "body part type correct");
    ASSERT(kg->getProperty(left_leg, "body_part_name") == "left_leg",
           "body part name set");
    ASSERT(kg->getProperty(left_leg, "health") == "100.000000",
           "health set");
    ASSERT(kg->getProperty(left_leg, "max_health") == "100.000000",
           "max_health set");
    ASSERT(kg->getProperty(left_leg, "cap_list") == "locomotion",
           "cap_list has locomotion");
    ASSERT(kg->getProperty(left_leg, "cap.locomotion.weight") == "1.000000",
           "capability weight set");
    ASSERT(kg->getProperty(left_leg, "cap.locomotion.side") == "left",
           "side hint set");

    // Relation: Humanoid HAS_PART Leg
    auto parts = kg->getRelated(entity, "HAS_PART");
    bool found = false;
    for (auto p : parts) if (p == left_leg) { found = true; break; }
    ASSERT(found, "HAS_PART relation created");
}

void test_biped_with_parts_aggregates_locomotion() {
    auto kg = make_kg();
    auto humanoid = kg->createEntity("Humanoid");
    body_plan::declare_biped(*kg, humanoid);

    body_plan::create_capability_part(*kg, humanoid, "Leg", "left_leg",  100.0f, "locomotion", 1.0f, "left");
    body_plan::create_capability_part(*kg, humanoid, "Leg", "right_leg", 100.0f, "locomotion", 1.0f, "right");

    auto cap = CapabilityProfile::compute_from_kg(*kg, humanoid, 75.0f, 0.9f, 1.8f);
    ASSERT(cap.locomotion_factor > 0.99f, "fully healthy biped has locomotion=1.0");

    // Damage one leg to 50%
    auto parts = kg->getRelated(humanoid, "HAS_PART");
    for (auto p : parts) {
        if (kg->getProperty(p, "body_part_name") == "left_leg") {
            kg->setProperty(p, "health", "50");
            break;
        }
    }

    auto damaged = CapabilityProfile::compute_from_kg(*kg, humanoid, 75.0f, 0.9f, 1.8f);
    ASSERT(damaged.locomotion_factor < cap.locomotion_factor,
           "damaged biped has reduced locomotion");
    ASSERT(damaged.locomotion_factor > 0.7f && damaged.locomotion_factor < 0.8f,
           "average mode gives ~0.75 with one leg at 50%");
}

int main() {
    std::cout << "=== Body Plan Template Tests ===" << std::endl;

    test_declare_biped_sets_capabilities();
    test_declare_serpent_sets_segment_count();
    test_declare_winged_computes_flight_parts();
    test_declare_quadruped_four_legs();
    test_create_capability_part_links_and_binds();
    test_biped_with_parts_aggregates_locomotion();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
