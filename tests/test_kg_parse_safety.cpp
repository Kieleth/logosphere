// Defensive-parsing regression test.
//
// Malformed KG property values (game typos, schema drift, manual
// edits) used to throw std::invalid_argument from std::stof and kill
// the engine. After the kg_parse refactor, they produce a one-line
// warning to stderr and the engine keeps running with a sensible
// fallback.
//
// Each test below sets a deliberately malformed rule string and
// verifies that compute_from_kg returns normally. Stderr noise is
// expected; we're not asserting the warning text here (that's an
// implementation detail).
//
// Usage:
//   ./build/test_kg_parse_safety

#include "logosphere/capability/capability_profile.h"
#include "logosphere/capability/trigger_registry.h"
#include "logosphere/capability/effect_registry.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include <cstdlib>
#include <iostream>
#include <string>

#include "test_env_portable.h"

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

struct Biped {
    kg::EntityID entity;
    kg::EntityID left_leg;
    kg::EntityID right_leg;
};

static std::unique_ptr<kg::KGModule> make_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->setMode(kg::KGMode::MINIMAL);
    return kg;
}

static Biped make_biped(kg::KGModule& kg) {
    Biped b;
    b.entity = kg.createEntity("Humanoid");
    kg.setProperty(b.entity, "cap.locomotion.expected_count", "2");
    kg.setProperty(b.entity, "cap.locomotion.default_mode", "average");

    b.left_leg = kg.createEntity("Leg");
    kg.setProperty(b.left_leg, "body_part_name", "left_leg");
    kg.setProperty(b.left_leg, "health", "100");
    kg.setProperty(b.left_leg, "max_health", "100");
    kg.setProperty(b.left_leg, "cap_list", "locomotion");
    kg.setProperty(b.left_leg, "cap.locomotion.weight", "1.0");
    kg.createRelation(b.entity, "HAS_PART", b.left_leg);

    b.right_leg = kg.createEntity("Leg");
    kg.setProperty(b.right_leg, "body_part_name", "right_leg");
    kg.setProperty(b.right_leg, "health", "100");
    kg.setProperty(b.right_leg, "max_health", "100");
    kg.setProperty(b.right_leg, "cap_list", "locomotion");
    kg.setProperty(b.right_leg, "cap.locomotion.weight", "1.0");
    kg.createRelation(b.entity, "HAS_PART", b.right_leg);
    return b;
}

void test_malformed_trigger_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:fify");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    // Must not throw — previously would crash with std::invalid_argument
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Rule couldn't fire because threshold was malformed — leg is healthy,
    // both legs contribute, locomotion should be near 1.
    ASSERT(p.locomotion_factor > 0.9f,
           "malformed threshold → rule treated as non-firing, locomotion intact");
}

void test_malformed_effect_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "speed_cap:abc");
    kg->setProperty(b.left_leg, "health", "0");  // actually destroyed

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // speed_cap failed to parse → stays at default 1.0
    ASSERT(p.speed_cap == 1.0f, "malformed speed_cap value left speed_cap untouched");
}

void test_malformed_age_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "created_at", "also_bad");
    kg->setProperty(b.left_leg, "rule.0.trigger", "age_above:notanumber");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor > 0.9f,
           "double-malformed age trigger no-ops cleanly");
}

void test_malformed_health_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "health", "broken");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Malformed health treated as full → both legs contribute fully
    ASSERT(p.locomotion_factor > 0.9f,
           "malformed health falls back to full HP, no crash");
}

void test_malformed_cap_weight_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "cap.locomotion.weight", "one point zero");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor > 0.9f,
           "malformed weight falls back to 1.0, no crash");
}

void test_malformed_cap_modifier_does_not_crash() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_modifier:locomotion:xyz");
    kg->setProperty(b.left_leg, "health", "0");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Rule fires but modifier can't parse factor → no modifier applied.
    // Left leg is destroyed so locomotion drops via aggregation, but the
    // engine doesn't crash.
    ASSERT(p.locomotion_factor >= 0.0f && p.locomotion_factor < 1.0f,
           "malformed cap_modifier factor does not crash, other logic runs");
}

void test_malformed_forge_inputs_fall_back_to_reference() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.entity, "reflexes_ms", "fast");
    kg->setProperty(b.entity, "grit_W", "strong");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Should fall back to reference human: 250ms / 500W
    ASSERT(p.reflexes_ms == 250.0f, "malformed reflexes → reference default");
    ASSERT(p.grit_W == 500.0f, "malformed grit → reference default");
}

void test_well_formed_still_works() {
    auto kg = make_kg();
    auto b = make_biped(*kg);
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:50");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");
    kg->setProperty(b.left_leg, "health", "40");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor < 0.001f,
           "well-formed rule still fires correctly after refactor");
}

int main() {
    std::cout << "=== KG Parse Safety Tests ===" << std::endl;
    std::cout << "(expect warnings on stderr — they're the feature)" << std::endl;

    // This test exercises the READER safety net: kg_parse must warn and
    // continue on malformed stored values. The setProperty gate (Malleus
    // H1) would strictly reject e.g. health="broken" before it ever
    // reached a reader, so run the writes in lenient mode — the layer
    // under test is precisely the defense below the gate (old saves,
    // lenient-mode inventory runs, direct map access).
    test_env::set("KG_GATE_LENIENT", "1");

    test_malformed_trigger_does_not_crash();
    test_malformed_effect_does_not_crash();
    test_malformed_age_does_not_crash();
    test_malformed_health_does_not_crash();
    test_malformed_cap_weight_does_not_crash();
    test_malformed_cap_modifier_does_not_crash();
    test_malformed_forge_inputs_fall_back_to_reference();
    test_well_formed_still_works();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
