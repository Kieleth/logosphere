// Journal renderer tests: compact per-entry lines and state-delta
// folding (first prev -> last value, net no-ops dropped, numeric-ish
// entity ordering). Includes the end-to-end path: KGModule
// setProperty -> state_changes journal -> reader -> rendered block.
//
// Usage:
//   ./build/test_journal_render

#include "logosphere/events/event_bus.h"
#include "logosphere/events/journal_render.h"
#include "logosphere/kg/kg_module.h"
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

using logosphere::EventLog;
namespace onto = logosphere::ontology;

namespace {

onto::WorldEvent state_change(const std::string& target,
                              const std::string& prop,
                              const std::string& value,
                              const std::string& prev) {
    onto::WorldEvent e;
    e.event_type = "STATE_CHANGE";
    e.target_entity_id = target;
    e.payload_keys = {"property", "value", "prev"};
    e.payload_values = {prop, value, prev};
    return e;
}

}  // namespace

void test_compact_render() {
    EventLog<onto::WorldEvent> log;
    log.advance_frame(3, 1.5);
    auto e = state_change("7", "max_speed", "12", "8");
    e.source_entity_id = "3";
    log.emit(e);

    auto out = logosphere::render_journal_compact(log.collect_since(0));
    ASSERT(out.find("[0]") != std::string::npos, "seq stamped");
    ASSERT(out.find("t=1.5") != std::string::npos, "game time stamped");
    ASSERT(out.find("STATE_CHANGE") != std::string::npos, "event type");
    ASSERT(out.find("target=7") != std::string::npos, "target id");
    ASSERT(out.find("source=3") != std::string::npos, "source id");
    ASSERT(out.find("property=max_speed") != std::string::npos, "payload");
}

void test_delta_folding() {
    EventLog<onto::WorldEvent> log;
    log.emit(state_change("7", "max_speed", "10", "8"));
    log.emit(state_change("7", "max_speed", "12", "10"));
    log.emit(state_change("7", "hue", "0.5", ""));

    auto out = logosphere::render_state_deltas(log.collect_since(0));
    ASSERT(out.find("7 max_speed: 8 -> 12") != std::string::npos,
           "folds to first prev -> last value");
    ASSERT(out.find("10 ->") == std::string::npos,
           "intermediate value dropped");
    ASSERT(out.find("7 hue: (unset) -> 0.5") != std::string::npos,
           "fresh property shows (unset)");
}

void test_net_noop_dropped_and_ordering() {
    EventLog<onto::WorldEvent> log;
    log.emit(state_change("10", "b_prop", "2", "1"));
    log.emit(state_change("7", "a_prop", "back", "orig"));
    log.emit(state_change("7", "a_prop", "orig", "back"));  // round trip

    auto out = logosphere::render_state_deltas(log.collect_since(0));
    ASSERT(out.find("a_prop") == std::string::npos,
           "net no-op round trip dropped");
    ASSERT(out.find("10 b_prop: 1 -> 2") != std::string::npos,
           "real delta kept");

    // Numeric-ish ordering: entity 7 before entity 10.
    EventLog<onto::WorldEvent> log2;
    log2.emit(state_change("10", "p", "x", "y"));
    log2.emit(state_change("7", "p", "x", "y"));
    auto out2 = logosphere::render_state_deltas(log2.collect_since(0));
    ASSERT(out2.find("7 p:") < out2.find("10 p:"),
           "entity 7 renders before entity 10");
}

void test_non_state_change_ignored() {
    EventLog<onto::WorldEvent> log;
    onto::WorldEvent other;
    other.event_type = "SOMETHING_ELSE";
    other.target_entity_id = "5";
    log.emit(other);

    ASSERT(logosphere::render_state_deltas(log.collect_since(0)).empty(),
           "non-STATE_CHANGE entries ignored by delta renderer");
    ASSERT(!logosphere::render_journal_compact(log.collect_since(0)).empty(),
           "compact renderer shows everything");
}

void test_end_to_end_kg_to_block() {
    logosphere::EventBus bus;
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.set_event_bus(&bus);

    auto reader = bus.state_changes().create_reader();
    auto e = kg.createEntity("Rock");
    kg.setProperty(e, "hardness", "3");
    kg.setProperty(e, "hardness", "9");
    kg.setProperty(e, "hardness", "9");   // no-op set: no event

    auto entries = reader.drain_entries();
    auto out = logosphere::render_state_deltas(entries);
    auto expected = std::to_string(e) + " hardness: (unset) -> 9";
    ASSERT(out.find(expected) != std::string::npos,
           "KG mutations render as a since-cursor delta block (got: " +
               out + ")");
    ASSERT(reader.drain_entries().empty(), "cursor consumed");
}

int main() {
    std::cout << "=== Journal Renderer Tests ===" << std::endl;
    test_compact_render();
    test_delta_folding();
    test_net_noop_dropped_and_ordering();
    test_non_state_change_ignored();
    test_end_to_end_kg_to_block();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
