// Consumer smoke test: exercises the installed logosphere::core the
// way a game would — knowledge graph, ontology registry, event bus.
// Built ONLY against the installed package (see CMakeLists.txt), so
// a pass proves the export/install surface is complete.

#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "generated/logosphere_ontology_registry.h"
#include <iostream>

int main() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    int state_changes = 0;
    bus.state_changes().subscribe(
        [&state_changes](const logosphere::ontology::WorldEvent&) {
            state_changes++;
        });

    auto e = kg.createEntity("Humanoid");
    int baseline = state_changes;
    kg.setProperty(e, "health", "100");
    kg.setProperty(e, "health", "50");

    std::cout << "consumer-smoke: entity=" << e
              << " state_changes=" << (state_changes - baseline)
              << " (expected 2)" << std::endl;

    if (state_changes - baseline != 2) {
        std::cerr << "FAIL: expected 2 state-change events, got "
                  << (state_changes - baseline) << std::endl;
        return 1;
    }
    std::cout << "consumer-smoke: PASS" << std::endl;
    return 0;
}
