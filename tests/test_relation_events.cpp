// KGModule relation events test
//
// Verifies that createRelation and destroyRelation emit RelationEvent
// on the relations() EventBus channel with correct source/target/relation_type
// and event_type fields.
//
// Usage:
//   ./build/test_relation_events

#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "generated/logosphere_ontology_registry.h"
#include <iostream>
#include <vector>
#include <string>

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

struct CapturedRelation {
    std::string event_type;
    std::string source_entity_id;
    std::string target_entity_id;
    std::string relation_type;
};

void test_create_relation_emits_event() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    std::vector<CapturedRelation> captured;
    bus.relations().subscribe(
        [&captured](const logosphere::ontology::RelationEvent& e) {
            CapturedRelation c;
            c.event_type = e.event_type;
            if (e.source_entity_id) c.source_entity_id = *e.source_entity_id;
            if (e.target_entity_id) c.target_entity_id = *e.target_entity_id;
            if (e.relation_type) c.relation_type = *e.relation_type;
            captured.push_back(c);
        });

    auto humanoid = kg.createEntity("Humanoid");
    auto leg = kg.createEntity("Leg");
    captured.clear();  // ignore any entity-creation noise

    kg.createRelation(humanoid, "HAS_PART", leg);

    ASSERT(captured.size() == 1, "exactly one relation event");
    ASSERT(captured[0].event_type == "RELATION_CREATED", "event_type = RELATION_CREATED");
    ASSERT(captured[0].source_entity_id == std::to_string(humanoid), "source = humanoid");
    ASSERT(captured[0].target_entity_id == std::to_string(leg), "target = leg");
    ASSERT(captured[0].relation_type == "HAS_PART", "relation_type = HAS_PART");
}

void test_destroy_relation_emits_event() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    auto humanoid = kg.createEntity("Humanoid");
    auto arm = kg.createEntity("Arm");
    kg.createRelation(humanoid, "HAS_PART", arm);

    std::vector<CapturedRelation> captured;
    bus.relations().subscribe(
        [&captured](const logosphere::ontology::RelationEvent& e) {
            CapturedRelation c;
            c.event_type = e.event_type;
            if (e.source_entity_id) c.source_entity_id = *e.source_entity_id;
            if (e.target_entity_id) c.target_entity_id = *e.target_entity_id;
            if (e.relation_type) c.relation_type = *e.relation_type;
            captured.push_back(c);
        });

    bool removed = kg.destroyRelation(humanoid, "HAS_PART", arm);
    ASSERT(removed, "destroyRelation returns true for existing relation");
    ASSERT(captured.size() == 1, "exactly one RELATION_REMOVED event");
    ASSERT(captured[0].event_type == "RELATION_REMOVED", "event_type = RELATION_REMOVED");
    ASSERT(captured[0].source_entity_id == std::to_string(humanoid), "source = humanoid");
    ASSERT(captured[0].target_entity_id == std::to_string(arm), "target = arm");
    ASSERT(captured[0].relation_type == "HAS_PART", "relation_type = HAS_PART");

    // Entity relation gone
    auto parts = kg.getRelated(humanoid, "HAS_PART");
    ASSERT(parts.empty(), "arm no longer in HAS_PART");
}

void test_destroy_nonexistent_relation() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    auto a = kg.createEntity("Humanoid");
    auto b = kg.createEntity("Leg");
    // No relation created

    int count = 0;
    bus.relations().subscribe(
        [&count](const logosphere::ontology::RelationEvent&) { count++; });

    bool removed = kg.destroyRelation(a, "HAS_PART", b);
    ASSERT(!removed, "destroyRelation returns false for missing relation");
    ASSERT(count == 0, "no event emitted for missing relation");
}

void test_without_event_bus_still_works() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    // No event bus

    auto a = kg.createEntity("Humanoid");
    auto b = kg.createEntity("Arm");
    kg.createRelation(a, "HAS_PART", b);
    bool removed = kg.destroyRelation(a, "HAS_PART", b);
    ASSERT(removed, "works without event bus");
}

void test_limb_severed_simulation() {
    // End-to-end: game subscribes to relations and detects "limb lost" via
    // HAS_PART removal, independent of health-based rule triggers.
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    auto monster = kg.createEntity("Humanoid");
    auto left_arm = kg.createEntity("Arm");
    kg.setProperty(left_arm, "body_part_name", "left_arm");
    kg.createRelation(monster, "HAS_PART", left_arm);

    bool limb_lost = false;
    std::string lost_part_name;
    bus.relations().subscribe(
        [&](const logosphere::ontology::RelationEvent& e) {
            if (e.event_type == "RELATION_REMOVED" &&
                e.relation_type && *e.relation_type == "HAS_PART") {
                limb_lost = true;
                if (e.target_entity_id) {
                    auto id = static_cast<kg::EntityID>(std::stoul(*e.target_entity_id));
                    lost_part_name = kg.getProperty(id, "body_part_name");
                }
            }
        });

    // Physics severed the arm — game calls destroyRelation
    kg.destroyRelation(monster, "HAS_PART", left_arm);

    ASSERT(limb_lost, "limb loss detected via relation event");
    ASSERT(lost_part_name == "left_arm",
           "subscriber looked up body_part_name from KG");
}

int main() {
    std::cout << "=== KG Relation Events Tests ===" << std::endl;

    test_create_relation_emits_event();
    test_destroy_relation_emits_event();
    test_destroy_nonexistent_relation();
    test_without_event_bus_still_works();
    test_limb_severed_simulation();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
