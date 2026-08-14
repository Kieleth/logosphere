// Headless test — make_narrative_hint reads crash_* properties off
// the AI entity and produces a one-line narrative the LLM can
// reason about. Catches regressions where:
//   - missing properties don't fall back gracefully
//   - cause names drift in arena.cpp without the formatter knowing
//   - director-wall detection misses the director_origin tag

#include "director/narrative_hint.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// Spin up a tiny KG with the types we need (Cycle for the AI,
// TrailSegment for the killer wall). The test doesn't load the
// real logotron ontology — keeps the test pure-logic and fast.
static kg::OntologyRegistry make_registry() {
    kg::OntologyRegistry r;
    r.addEntityType("Cycle",        "", false);
    r.addEntityType("TrailSegment", "", false);
    return r;
}

struct KGFixture {
    kg::OntologyRegistry registry = make_registry();
    kg::KGModule kg{registry};
    KGFixture() {
        kg.setMode(kg::KGMode::MINIMAL);
    }
};

void empty_when_no_crash_metadata() {
    KGFixture f;
    auto ai = f.kg.createEntity("Cycle");
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(hint.empty(),
        std::string("expected empty hint with no crash data; got '") + hint + "'");
}

void empty_when_invalid_entity() {
    KGFixture f;
    auto hint = logotron::director::make_narrative_hint(
        f.kg, kg::INVALID_ENTITY);
    ASSERT_TRUE(hint.empty(), "INVALID_ENTITY must yield empty hint");
}

void out_of_bounds_message() {
    KGFixture f;
    auto ai = f.kg.createEntity("Cycle");
    f.kg.setProperty(ai, "crash_cause", "out_of_bounds");
    f.kg.setProperty(ai, "crash_x", "40.0");
    f.kg.setProperty(ai, "crash_y", "12.5");
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(contains(hint, "AI ran out of arena"),
        std::string("expected 'ran out of arena'; got '") + hint + "'");
    ASSERT_TRUE(contains(hint, "(40.0, 12.5)"),
        "coords must appear");
}

void self_trail_message_with_age() {
    KGFixture f;
    auto ai = f.kg.createEntity("Cycle");
    f.kg.setProperty(ai, "crash_cause", "sealed_trail_self");
    f.kg.setProperty(ai, "crash_x", "10.7");
    f.kg.setProperty(ai, "crash_y", "23.6");
    f.kg.setProperty(ai, "crash_hit_age", "12.3");
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(contains(hint, "own sealed trail"),
        "expected own-trail narration");
    ASSERT_TRUE(contains(hint, "12.3s old"),
        "expected age annotation");
}

void player_trail_message() {
    KGFixture f;
    auto ai   = f.kg.createEntity("Cycle");
    auto wall = f.kg.createEntity("TrailSegment");
    // Not a director wall (no director_origin tag).
    f.kg.setProperty(ai, "crash_cause", "sealed_trail_other");
    f.kg.setProperty(ai, "crash_x", "4.5");
    f.kg.setProperty(ai, "crash_y", "8.0");
    f.kg.setProperty(ai, "crash_hit_age", "5.1");
    f.kg.setProperty(ai, "crash_hit_entity", std::to_string(wall));
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(contains(hint, "player's sealed trail"),
        std::string("expected player-trail narration; got '") + hint + "'");
}

void director_wall_message() {
    KGFixture f;
    auto ai   = f.kg.createEntity("Cycle");
    auto wall = f.kg.createEntity("TrailSegment");
    f.kg.setProperty(wall, "director_origin", "1");
    f.kg.setProperty(ai, "crash_cause", "sealed_trail_other");
    f.kg.setProperty(ai, "crash_x", "3.5");
    f.kg.setProperty(ai, "crash_y", "12.0");
    f.kg.setProperty(ai, "crash_hit_entity", std::to_string(wall));
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(contains(hint, "director wall"),
        std::string("expected director-wall narration; got '") + hint + "'");
    ASSERT_TRUE(contains(hint, "(3.5, 12.0)"),
        "coords must appear");
}

void head_on_message() {
    KGFixture f;
    auto ai = f.kg.createEntity("Cycle");
    f.kg.setProperty(ai, "crash_cause", "opponent_active_run");
    f.kg.setProperty(ai, "crash_x", "15.2");
    f.kg.setProperty(ai, "crash_y", "18.7");
    auto hint = logotron::director::make_narrative_hint(f.kg, ai);
    ASSERT_TRUE(contains(hint, "rammed player head-on"),
        std::string("expected head-on narration; got '") + hint + "'");
}

int main() {
    std::cout << "=== test_narrative_hint ===" << std::endl;
    TEST(empty_when_no_crash_metadata);
    TEST(empty_when_invalid_entity);
    TEST(out_of_bounds_message);
    TEST(self_trail_message_with_age);
    TEST(player_trail_message);
    TEST(director_wall_message);
    TEST(head_on_message);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
