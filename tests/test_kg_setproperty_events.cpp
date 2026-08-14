// KGModule::setProperty event emission test
//
// Verifies that setProperty emits state_changes events to the EventBus
// when the value actually changes, and skips emission for no-op sets
// (writing the same value that's already there). This matters because
// downstream subscribers (e.g. ParticleDynamicsSystem::recompute_capability)
// can be expensive.
//
// Usage:
//   ./build/test_kg_setproperty_events

#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "generated/logosphere_ontology_registry.h"
#include <iostream>
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

void test_setproperty_emits_event_on_change() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    int count = 0;
    bus.state_changes().subscribe(
        [&count](const logosphere::ontology::WorldEvent&) { count++; });

    auto e = kg.createEntity("Humanoid");
    int baseline = count;  // createEntity might emit spawn, not state_change

    kg.setProperty(e, "health", "100");
    ASSERT(count == baseline + 1, "first setProperty emits one event");

    kg.setProperty(e, "health", "50");
    ASSERT(count == baseline + 2, "changed value emits another event");
}

void test_noop_setproperty_skips_emission() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    int count = 0;
    bus.state_changes().subscribe(
        [&count](const logosphere::ontology::WorldEvent&) { count++; });

    auto e = kg.createEntity("Humanoid");

    kg.setProperty(e, "health", "100");
    int after_first = count;

    kg.setProperty(e, "health", "100");  // no-op, same value
    ASSERT(count == after_first, "no-op setProperty does NOT emit");

    kg.setProperty(e, "health", "100");  // still no-op
    ASSERT(count == after_first, "repeated no-op still does not emit");

    kg.setProperty(e, "health", "99");   // actual change
    ASSERT(count == after_first + 1, "real change emits after no-ops");
}

void test_event_carries_target_entity_id() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    std::string captured_target;
    bus.state_changes().subscribe(
        [&captured_target](const logosphere::ontology::WorldEvent& e) {
            if (e.target_entity_id) captured_target = *e.target_entity_id;
        });

    auto e = kg.createEntity("Humanoid");
    // A declared property (Describable.description): setProperty is
    // ontology-gated now (Malleus H1), and this test is about event
    // payloads, not the gate.
    kg.setProperty(e, "description", "gate-checked write");

    ASSERT(!captured_target.empty(), "target_entity_id populated");
    ASSERT(captured_target == std::to_string(e),
           "target_entity_id matches entity");
}

void test_without_event_bus_setproperty_still_works() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    // No event bus wired

    auto e = kg.createEntity("Humanoid");
    kg.setProperty(e, "health", "100");
    ASSERT(kg.getProperty(e, "health") == "100",
           "setProperty works without event bus");

    // Repeated same-value set also works (still a valid API call)
    kg.setProperty(e, "health", "100");
    ASSERT(kg.getProperty(e, "health") == "100",
           "no-op without event bus still works");
}

int main() {
    std::cout << "=== KG setProperty Event Tests ===" << std::endl;

    test_setproperty_emits_event_on_change();
    test_noop_setproperty_skips_emission();
    test_event_carries_target_entity_id();
    test_without_event_bus_setproperty_still_works();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
