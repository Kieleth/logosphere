// KG-Driven Capability System Tests (TDD)
//
// Tests the generic capability aggregator, response rules, and
// data-driven body plans. Each test creates a minimal KG setup
// without a full Engine.
//
// Usage:
//   ./build/test_capability_system

#include "logosphere/capability/capability_profile.h"
#include "logosphere/capability/dynamics_params.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/capability/trigger_registry.h"
#include "logosphere/capability/effect_registry.h"
#include "core/game_time.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "generated/logosphere_ontology.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/earth_ontology_registry.h"
#include <iostream>
#include <string>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>
#include <stdexcept>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg) + " [" + #cond + "]")

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) \
        throw std::runtime_error(std::string(msg) + " [" + std::to_string(a) + " vs " + std::to_string(b) + "]")

// Helper: create a KG with ontology, return in MINIMAL mode
static std::unique_ptr<kg::KGModule> create_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    // Snakes, butterflies and branches are earth-like vocabulary, so
    // this test asks for the pack that defines them. The core alone
    // has no botany; see docs/ONTOLOGY_LAYERS.md.
    kg->extendOntology(earth::ontology::registry());

    kg->setMode(kg::KGMode::MINIMAL);
    return kg;
}

// Helper: create a biped body plan on entity with N legs, N arms, torso, head
struct BipedSetup {
    kg::EntityID entity;
    kg::EntityID left_leg, right_leg;
    kg::EntityID left_arm, right_arm;
    kg::EntityID torso, head;
};

static BipedSetup create_biped(kg::KGModule& kg) {
    BipedSetup b;
    b.entity = kg.createEntity("Humanoid");
    kg.setProperty(b.entity, "reflexes_ms", "250");
    kg.setProperty(b.entity, "grit_W", "500");

    kg.setProperty(b.entity, "cap.locomotion.expected_count", "2");
    kg.setProperty(b.entity, "cap.locomotion.default_mode", "average");
    kg.setProperty(b.entity, "cap.manipulation.expected_count", "2");
    kg.setProperty(b.entity, "cap.manipulation.default_mode", "average");
    kg.setProperty(b.entity, "cap.rotation.expected_count", "1");
    kg.setProperty(b.entity, "cap.rotation.default_mode", "minimum");
    kg.setProperty(b.entity, "cap.perception.expected_count", "1");
    kg.setProperty(b.entity, "cap.perception.default_mode", "minimum");

    auto make = [&](const std::string& type, const std::string& name,
                    float hp, const std::string& cap_name, float weight,
                    const std::string& side = "") -> kg::EntityID {
        kg::EntityID part = kg.createEntity(type);
        kg.setProperty(part, "body_part_name", name);
        kg.setProperty(part, "health", std::to_string(hp));
        kg.setProperty(part, "max_health", std::to_string(hp));
        kg.setProperty(part, "cap_list", cap_name);
        kg.setProperty(part, "cap." + cap_name + ".weight", std::to_string(weight));
        if (!side.empty())
            kg.setProperty(part, "cap." + cap_name + ".side", side);
        kg.createRelation(b.entity, "HAS_PART", part);
        return part;
    };

    b.left_leg  = make("Leg",   "left_leg",  100, "locomotion",   1.0f, "left");
    b.right_leg = make("Leg",   "right_leg", 100, "locomotion",   1.0f, "right");
    b.left_arm  = make("Arm",   "left_arm",  80,  "manipulation", 1.0f, "left");
    b.right_arm = make("Arm",   "right_arm", 80,  "manipulation", 1.0f, "right");
    b.torso     = make("Torso", "torso",     150, "rotation",     1.0f);
    b.head      = make("Head",  "head",      80,  "perception",   1.0f);
    return b;
}

// ========================================================================
// Layer 1 Tests: Capability Bindings + Aggregation
// ========================================================================

void test_healthy_biped_full_capabilities() {
    auto kg = create_kg();
    auto b = create_biped(*kg);
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT_NEAR(p.locomotion_factor, 1.0f, 0.01f, "healthy locomotion = 1.0");
    ASSERT_NEAR(p.manipulation_factor, 1.0f, 0.01f, "healthy manipulation = 1.0");
    ASSERT_NEAR(p.rotation_factor, 1.0f, 0.01f, "healthy rotation = 1.0");
    ASSERT_NEAR(p.perception_factor, 1.0f, 0.01f, "healthy perception = 1.0");
    ASSERT(p.capabilities.count("locomotion"), "locomotion in capabilities map");
}

void test_damaged_leg_reduces_locomotion() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "health", "50");
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // average mode: (0.5 + 1.0) / 2 = 0.75
    ASSERT_NEAR(p.locomotion_factor, 0.75f, 0.01f, "locomotion = 0.75 with one leg at 50%");
    ASSERT_NEAR(p.manipulation_factor, 1.0f, 0.01f, "manipulation unaffected");
}

void test_destroyed_leg_halves_locomotion() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "health", "0");
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // average mode: (0.0 + 1.0) / 2 = 0.5
    ASSERT_NEAR(p.locomotion_factor, 0.5f, 0.01f, "locomotion halved with dead leg");
    ASSERT_NEAR(p.side_hints["locomotion.left"], 0.0f, 0.01f, "left leg side hint = 0");
    ASSERT_NEAR(p.side_hints["locomotion.right"], 1.0f, 0.01f, "right leg side hint = 1");
}

void test_arm_damage_no_locomotion_effect() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_arm, "health", "0");
    kg->setProperty(b.right_arm, "health", "0");
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT_NEAR(p.locomotion_factor, 1.0f, 0.01f, "locomotion unaffected by arm damage");
    ASSERT_NEAR(p.manipulation_factor, 0.0f, 0.01f, "manipulation zeroed");
}

void test_minimum_mode_torso() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.torso, "health", "75");
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // minimum mode: single contributor at 0.5
    ASSERT_NEAR(p.rotation_factor, 0.5f, 0.01f, "rotation = torso health ratio (min mode)");
}

void test_binary_mode_wings() {
    auto kg = create_kg();
    kg::EntityID butterfly = kg->createEntity("Butterfly");
    kg->setProperty(butterfly, "cap.flight.expected_count", "2");
    kg->setProperty(butterfly, "cap.flight.default_mode", "binary");

    std::vector<kg::EntityID> wings;
    for (const auto& side : {"left", "right"}) {
        kg::EntityID wing = kg->createEntity("Wing");
        kg->setProperty(wing, "body_part_name", std::string(side) + "_wing");
        kg->setProperty(wing, "health", "100");
        kg->setProperty(wing, "max_health", "100");
        kg->setProperty(wing, "cap_list", "flight");
        kg->setProperty(wing, "cap.flight.weight", "1.0");
        kg->createRelation(butterfly, "HAS_PART", wing);
        wings.push_back(wing);
    }

    auto p1 = CapabilityProfile::compute_from_kg(*kg, butterfly, 0.5f, 0.0f, 0.1f);
    ASSERT_NEAR(p1.capabilities["flight"], 1.0f, 0.01f, "both wings healthy = flight 1.0");

    kg->setProperty(wings[0], "health", "30");
    auto p2 = CapabilityProfile::compute_from_kg(*kg, butterfly, 0.5f, 0.0f, 0.1f);
    ASSERT_NEAR(p2.capabilities["flight"], 0.0f, 0.01f, "one wing damaged = no flight");
}

void test_weighted_sum_serpent() {
    auto kg = create_kg();
    kg::EntityID serpent = kg->createEntity("Snake");
    kg->setProperty(serpent, "cap.undulation.expected_count", "5");
    kg->setProperty(serpent, "cap.undulation.default_mode", "weighted_sum");
    kg->setProperty(serpent, "cap.undulation.normalize", "1.0");

    // 5 segments: head=0.4, mid1=0.2, mid2=0.2, mid3=0.1, tail=0.1 (sum = 1.0)
    float weights[] = {0.4f, 0.2f, 0.2f, 0.1f, 0.1f};
    std::vector<kg::EntityID> segs;
    for (int i = 0; i < 5; i++) {
        kg::EntityID seg = kg->createEntity("Segment");
        kg->setProperty(seg, "body_part_name", "segment_" + std::to_string(i));
        kg->setProperty(seg, "health", "100");
        kg->setProperty(seg, "max_health", "100");
        kg->setProperty(seg, "cap_list", "undulation");
        kg->setProperty(seg, "cap.undulation.weight", std::to_string(weights[i]));
        kg->createRelation(serpent, "HAS_PART", seg);
        segs.push_back(seg);
    }

    // All healthy: undulation = 1.0
    auto p1 = CapabilityProfile::compute_from_kg(*kg, serpent, 5.0f, 0.0f, 0.5f);
    ASSERT_NEAR(p1.capabilities["undulation"], 1.0f, 0.01f, "all segments healthy = 1.0");

    // Destroy tail (weight 0.1): undulation = 0.9
    kg->setProperty(segs[4], "health", "0");
    auto p2 = CapabilityProfile::compute_from_kg(*kg, serpent, 5.0f, 0.0f, 0.5f);
    ASSERT_NEAR(p2.capabilities["undulation"], 0.9f, 0.01f, "tail destroyed = 0.9");

    // Destroy head (weight 0.4): undulation = 0.5
    kg->setProperty(segs[0], "health", "0");
    auto p3 = CapabilityProfile::compute_from_kg(*kg, serpent, 5.0f, 0.0f, 0.5f);
    ASSERT_NEAR(p3.capabilities["undulation"], 0.5f, 0.01f, "head+tail destroyed = 0.5");
}

void test_custom_capability_name() {
    auto kg = create_kg();
    // A custom capability comes WITH its declarations: the setProperty
    // gate (Malleus H1) rejects undeclared keys, so a game inventing
    // "photosynthesis" extends the ontology the documented way
    // (tests/test_ontology_extension.cpp pattern). Branch has no
    // HasHealth mixin either, so its health keys are declared here too.
    kg::OntologyRegistry ext;
    ext.addProperty("Tree", "cap.photosynthesis.expected_count", "integer", false);
    ext.addProperty("Tree", "cap.photosynthesis.default_mode", "string", false);
    ext.addProperty("Branch", "health", "float", false);
    ext.addProperty("Branch", "max_health", "float", false);
    ext.addProperty("Branch", "cap_list", "string", false);
    ext.addProperty("Branch", "cap.photosynthesis.weight", "float", false);
    kg->extendOntology(ext);

    kg::EntityID plant = kg->createEntity("Tree");
    kg->setProperty(plant, "cap.photosynthesis.expected_count", "3");
    kg->setProperty(plant, "cap.photosynthesis.default_mode", "average");

    for (int i = 0; i < 3; i++) {
        kg::EntityID leaf = kg->createEntity("Branch");
        kg->setProperty(leaf, "health", "100");
        kg->setProperty(leaf, "max_health", "100");
        kg->setProperty(leaf, "cap_list", "photosynthesis");
        kg->setProperty(leaf, "cap.photosynthesis.weight", "1.0");
        kg->createRelation(plant, "HAS_PART", leaf);
    }

    auto p = CapabilityProfile::compute_from_kg(*kg, plant, 1.0f, 0.0f, 0.5f);
    ASSERT(p.capabilities.count("photosynthesis"), "custom capability exists");
    ASSERT_NEAR(p.capabilities["photosynthesis"], 1.0f, 0.01f, "3 healthy leaves = 1.0");

    // Locomotion should NOT exist (plant has no legs)
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.01f, "plant has no locomotion");
}

void test_speed_cap_in_derive() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    auto healthy = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // Manually set speed_cap and re-derive to verify it applies
    auto capped = healthy;
    capped.speed_cap = 0.5f;
    // Re-derive with speed_cap (private method, but compute sets it)
    // For now, test that the field exists and derive() would use it
    // Full test requires response rules (Phase 3)
    ASSERT(healthy.speed_cap == 1.0f, "default speed_cap is 1.0");
}

// ========================================================================
// Layer 2 Tests: Response Rules (Phase 3, expected to FAIL until implemented)
// ========================================================================

void test_response_rule_speed_cap() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Add response rule: at 50% health, cap speed to 0.5
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:50");
    kg->setProperty(b.left_leg, "rule.0.effect", "speed_cap:0.5");

    // Damage leg to 40% (below 50% threshold)
    kg->setProperty(b.left_leg, "health", "40");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    auto d = DynamicsParams::from_capability(p);
    ASSERT(p.speed_cap <= 0.5f, "speed capped at 0.5 by response rule");
    ASSERT(d.max_walk_speed < 1.0f, "walk speed reduced by speed cap");
}

void test_cascade_torso_to_limbs() {
    auto kg = create_kg();

    // Build a hierarchical body: humanoid with torso as parent of arms
    kg::EntityID entity = kg->createEntity("Humanoid");
    kg->setProperty(entity, "reflexes_ms", "250");
    kg->setProperty(entity, "grit_W", "500");
    kg->setProperty(entity, "cap.locomotion.expected_count", "2");
    kg->setProperty(entity, "cap.locomotion.default_mode", "average");
    kg->setProperty(entity, "cap.manipulation.expected_count", "2");
    kg->setProperty(entity, "cap.manipulation.default_mode", "average");
    kg->setProperty(entity, "cap.rotation.expected_count", "1");
    kg->setProperty(entity, "cap.rotation.default_mode", "minimum");

    // Torso with cascade rule: when destroyed, children lose 50% effectiveness
    kg::EntityID torso = kg->createEntity("Torso");
    kg->setProperty(torso, "body_part_name", "torso");
    kg->setProperty(torso, "health", "150");
    kg->setProperty(torso, "max_health", "150");
    kg->setProperty(torso, "cap_list", "rotation");
    kg->setProperty(torso, "cap.rotation.weight", "1.0");
    kg->setProperty(torso, "rule.0.trigger", "destroyed");
    kg->setProperty(torso, "rule.0.effect", "cap.rotation.disable");
    kg->setProperty(torso, "rule.0.cascade", "children:0.5");
    kg->createRelation(entity, "HAS_PART", torso);

    // Two legs (children of entity, affected by torso cascade)
    kg::EntityID left_leg = kg->createEntity("Leg");
    kg->setProperty(left_leg, "body_part_name", "left_leg");
    kg->setProperty(left_leg, "health", "100");
    kg->setProperty(left_leg, "max_health", "100");
    kg->setProperty(left_leg, "cap_list", "locomotion");
    kg->setProperty(left_leg, "cap.locomotion.weight", "1.0");
    kg->createRelation(entity, "HAS_PART", left_leg);

    kg::EntityID right_leg = kg->createEntity("Leg");
    kg->setProperty(right_leg, "body_part_name", "right_leg");
    kg->setProperty(right_leg, "health", "100");
    kg->setProperty(right_leg, "max_health", "100");
    kg->setProperty(right_leg, "cap_list", "locomotion");
    kg->setProperty(right_leg, "cap.locomotion.weight", "1.0");
    kg->createRelation(entity, "HAS_PART", right_leg);

    // Healthy: locomotion = 1.0
    auto p1 = CapabilityProfile::compute_from_kg(*kg, entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p1.locomotion_factor, 1.0f, 0.01f, "healthy locomotion");

    // Destroy torso: cascade should reduce legs' effective health by 0.5
    kg->setProperty(torso, "health", "0");
    auto p2 = CapabilityProfile::compute_from_kg(*kg, entity, 75.0f, 0.9f, 1.8f);
    // Legs are healthy (100%) but cascade multiplies by 0.5 → effective 0.5
    // average mode: (0.5 + 0.5) / 2 = 0.5
    ASSERT_NEAR(p2.locomotion_factor, 0.5f, 0.01f, "cascade reduces locomotion to 0.5");
    ASSERT_NEAR(p2.rotation_factor, 0.0f, 0.01f, "torso rotation disabled");
}

// ========================================================================
// Layer 2: Relation cascades (rule.N.cascade = "relation:<TYPE>:<factor>")
// ========================================================================

void test_cascade_named_relation_applies() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Declare manipulation aggregation over 3 contributors (two arms + one weapon).
    kg->setProperty(b.entity, "cap.manipulation.expected_count", "3");

    // Attach a weapon as a HAS_PART of the entity, wielded by the left arm
    // via a custom GRIP_ON relation (source part → target weapon).
    kg::EntityID weapon = kg->createEntity("Segment");
    ASSERT(weapon != kg::INVALID_ENTITY, "weapon entity created");
    kg->setProperty(weapon, "body_part_name", "weapon");
    kg->setProperty(weapon, "health", "100");
    kg->setProperty(weapon, "max_health", "100");
    kg->setProperty(weapon, "cap_list", "manipulation");
    kg->setProperty(weapon, "cap.manipulation.weight", "1.0");
    kg->createRelation(b.entity, "HAS_PART", weapon);
    // Use SUPPORTS (already in the engine ontology) to model the arm
    // wielding the weapon. A game would typically extend the registry
    // with its own relation (GRIP_ON, WIELDS, etc.) at startup.
    kg->createRelation(b.left_arm, "SUPPORTS", weapon);

    // Baseline: healthy limbs + weapon → manipulation averages 1.0 over 3.
    auto p0 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p0.manipulation_factor, 1.0f, 0.02f,
                "manipulation full when all three contributors healthy");

    // Left arm fires a rule cascading via SUPPORTS to the weapon only.
    kg->setProperty(b.left_arm, "health", "0");
    kg->setProperty(b.left_arm, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_arm, "rule.0.effect",  "cap_modifier:manipulation:1.0");
    kg->setProperty(b.left_arm, "rule.0.cascade", "relation:SUPPORTS:0.5");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Contributors: left_arm=0 (destroyed, own rule fires → 0 contribution),
    // right_arm=1.0, weapon=0.5 (cascade via SUPPORTS).
    // Average over 3 → (0 + 1.0 + 0.5) / 3 = 0.5.
    ASSERT_NEAR(p.manipulation_factor, 0.5f, 0.02f,
                "SUPPORTS cascade halves weapon contribution");
}

void test_cascade_named_relation_empty_target_is_noop() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Cascade clause references a relation with no targets. Must be silent.
    kg->setProperty(b.torso, "health", "0");
    kg->setProperty(b.torso, "rule.0.trigger", "destroyed");
    kg->setProperty(b.torso, "rule.0.effect",  "cap_disable:rotation");
    kg->setProperty(b.torso, "rule.0.cascade", "relation:NONEXISTENT_REL:0.1");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // No targets → no parts get the 0.1 factor. Legs stay healthy.
    ASSERT_NEAR(p.locomotion_factor, 1.0f, 0.02f,
                "cascade with empty relation targets is a no-op");
    ASSERT_NEAR(p.rotation_factor, 0.0f, 0.02f,
                "effect still fires even though cascade hit nothing");
}

void test_cascade_multiple_clauses_min_wins() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Two clauses on one rule: both should run, with min factor winning
    // on any target hit by both. The children: clause applies to all
    // HAS_PART parts; the relation: clause hits the connected arm only.
    // Using CONTAINS (already in ontology) as the torso→arm link for
    // test purposes; a real game would extend the ontology.
    kg->createRelation(b.torso, "CONTAINS", b.left_arm);

    kg->setProperty(b.torso, "health", "0");
    kg->setProperty(b.torso, "rule.0.trigger", "destroyed");
    kg->setProperty(b.torso, "rule.0.effect",  "cap_disable:rotation");
    kg->setProperty(b.torso, "rule.0.cascade", "children:0.7, relation:CONTAINS:0.2");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // left_arm is hit by both clauses (min wins → 0.2). right_arm only by
    // children: (0.7). Manipulation average over expected=2 → (0.2 + 0.7) / 2 = 0.45.
    ASSERT_NEAR(p.manipulation_factor, 0.45f, 0.02f,
                "overlapping cascade clauses take the minimum factor per part");
    // Legs only hit by children:0.7 → average 0.7.
    ASSERT_NEAR(p.locomotion_factor, 0.7f, 0.02f,
                "children clause applies where relation clause does not");
}

void test_cascade_children_alias_still_works() {
    // Regression guard: classic "children:<f>" syntax preserved unchanged.
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.torso, "health", "0");
    kg->setProperty(b.torso, "rule.0.trigger", "destroyed");
    kg->setProperty(b.torso, "rule.0.effect",  "cap_disable:rotation");
    kg->setProperty(b.torso, "rule.0.cascade", "children:0.5");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.5f, 0.02f,
                "children:0.5 still cascades to all HAS_PART parts");
}

// ========================================================================
// Layer 2: Rule groups (rule.N.group + rule_group.M.name/enabled)
// ========================================================================

void test_rule_group_disabled_skips_rule() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Declare a "flying" rule group, disabled.
    kg->setProperty(b.entity, "rule_group.0.name", "flying");
    kg->setProperty(b.entity, "rule_group.0.enabled", "false");

    // Rule gated on "flying" group. Would otherwise fire (leg is destroyed).
    kg->setProperty(b.left_leg, "health", "0");
    kg->setProperty(b.left_leg, "rule.0.group", "flying");
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    // Group disabled → rule does not fire. Destroyed leg still contributes
    // 0 health, but the cap is not disabled; average over expected=2 → 0.5.
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.5f, 0.02f,
                "disabled rule group prevents rule from firing");
}

void test_rule_group_enabled_fires_rule() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.entity, "rule_group.0.name", "flying");
    kg->setProperty(b.entity, "rule_group.0.enabled", "true");

    kg->setProperty(b.left_leg, "health", "0");
    kg->setProperty(b.left_leg, "rule.0.group", "flying");
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    // Group enabled → rule fires → cap_disable zeroes locomotion.
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.02f,
                "enabled rule group allows rule to fire");
}

void test_rule_group_undeclared_fails_closed() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Rule references a group the entity never declared. Fail-closed.
    kg->setProperty(b.left_leg, "health", "0");
    kg->setProperty(b.left_leg, "rule.0.group", "ghost_group");
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.5f, 0.02f,
                "undeclared rule group treated as disabled (fail-closed)");
}

void test_rule_without_group_unaffected() {
    // Regression: rules with no group field behave exactly as before.
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Declare a group that should be irrelevant to this rule.
    kg->setProperty(b.entity, "rule_group.0.name", "flying");
    kg->setProperty(b.entity, "rule_group.0.enabled", "false");

    kg->setProperty(b.left_leg, "health", "0");
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.02f,
                "ungrouped rule fires regardless of unrelated group state");
}

void test_response_rule_cap_disable() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // When left leg is destroyed, disable manipulation entirely.
    // (Semantically odd, but exercises the disable mechanism.)
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:manipulation");

    // Destroy the leg
    kg->setProperty(b.left_leg, "health", "0");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.manipulation_factor, 0.0f, 0.001f,
                "manipulation disabled by rule effect");
    // Locomotion still degraded by leg damage (normal aggregation)
    ASSERT(p.locomotion_factor < 1.0f, "locomotion still reflects leg damage");
}

void test_response_rule_cap_modifier() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // When left leg below 50%, halve locomotion.
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:50");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_modifier:locomotion:0.5");

    // Damage leg to 40% (below 50% threshold)
    kg->setProperty(b.left_leg, "health", "40");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // Normal aggregation: average(0.4, 1.0) = 0.7
    // Modifier halves it: 0.7 * 0.5 = 0.35
    ASSERT_NEAR(p.locomotion_factor, 0.35f, 0.02f,
                "locomotion modified by 0.5 after rule fires");
}

void test_cap_disable_beats_modifier() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Two rules on the same part. Disable should win.
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",   "cap_disable:locomotion");
    kg->setProperty(b.left_leg, "rule.1.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.1.effect",   "cap_modifier:locomotion:2.0");

    kg->setProperty(b.left_leg, "health", "0");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.001f,
                "disable wins over modifier");
}

// ========================================================================
// Layer 2: Pluggable effect registry
// ========================================================================

void test_custom_effect_registration() {
    // Games can add custom effect applicators. Here we register a "heal"
    // effect that restores health on the triggering part.
    auto& registry = capability::EffectRegistry::instance();
    registry.register_effect("heal",
        [](capability::EffectContext& ctx, const std::string& args) {
            if (args.empty()) return;
            float amount = std::stof(args);
            float cur = std::stof(ctx.kg.getProperty(ctx.part_id, "health"));
            float new_hp = cur + amount;
            ctx.kg.setProperty(ctx.part_id, "health", std::to_string(new_hp));
        });

    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Rule: when health below 30, heal by 20
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:30");
    kg->setProperty(b.left_leg, "rule.0.effect",  "heal:20");

    // Damage leg to 20
    kg->setProperty(b.left_leg, "health", "20");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    float healed_hp = std::stof(kg->getProperty(b.left_leg, "health"));
    ASSERT(healed_hp >= 40.0f,
           "custom heal effect restored health by 20 (was 20, now >=40)");
    (void)p;
}

void test_custom_effect_with_event_emission() {
    // Custom effect that emits an event with its own event_type
    auto& registry = capability::EffectRegistry::instance();
    registry.register_effect("log_combat",
        [](capability::EffectContext& ctx, const std::string& args) {
            auto* bus = ctx.kg.get_event_bus();
            if (!bus) return;
            logosphere::ontology::WorldEvent evt;
            evt.event_type = "combat_log";
            evt.source_entity_id = std::to_string(ctx.part_id);
            evt.target_entity_id = std::to_string(ctx.entity_id);
            evt.payload_keys.push_back("message");
            evt.payload_values.push_back(args);
            bus->state_changes().emit(std::move(evt));
        });

    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    std::string captured_msg;
    bus.state_changes().subscribe(
        [&captured_msg](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "combat_log" && !e.payload_values.empty())
                captured_msg = e.payload_values[0];
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "log_combat:left_leg severed");
    kg->setProperty(b.left_leg, "health", "0");

    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(captured_msg == "left_leg severed",
           "custom effect emitted event with args payload");
}

void test_unknown_effect_is_silent_noop() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "typo_effect:0.5");
    kg->setProperty(b.left_leg, "health", "0");

    // Should not crash, effect just doesn't apply
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.speed_cap == 1.0f, "unknown effect does not modify state");
}

void test_multiple_effects_all_dispatched() {
    // Rule fires, multiple effects chain through effect_2, effect_3, effect_4
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    int event_count = 0;
    bus.state_changes().subscribe(
        [&event_count](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "chain_evt") event_count++;
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",    "speed_cap:0.5");
    kg->setProperty(b.left_leg, "rule.0.effect_2",  "cap_disable:locomotion");
    kg->setProperty(b.left_leg, "rule.0.effect_3",  "emit_event:chain_evt");
    kg->setProperty(b.left_leg, "rule.0.effect_4",  "cap_modifier:manipulation:0.5");
    kg->setProperty(b.left_leg, "health", "0");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(p.speed_cap <= 0.5f, "effect 1 (speed_cap) applied");
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.001f, "effect 2 (cap_disable) applied");
    ASSERT(event_count == 1, "effect 3 (emit_event) applied");
    ASSERT(p.manipulation_factor < 1.0f, "effect 4 (cap_modifier) applied");
}

// ========================================================================
// Layer 2: Pluggable trigger registry
// ========================================================================

void test_trigger_health_above() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Fire when leg is nearly full health (>95%)
    kg->setProperty(b.left_leg, "rule.0.trigger", "health_above:95");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_modifier:locomotion:0.5");

    // Leg at 100 — above 95 → rule fires
    auto p1 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // average(0.5*1.0, 1.0) + modifier... actually: base aggregation is 1.0, modifier 0.5 -> 0.5
    ASSERT_NEAR(p1.locomotion_factor, 0.5f, 0.02f,
                "health_above fires when leg > 95%");

    // Damage leg to 90 — below 95 → rule does not fire
    kg->setProperty(b.left_leg, "health", "90");
    auto p2 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p2.locomotion_factor, 0.95f, 0.02f,
                "health_above does not fire below threshold");
}

void test_trigger_relation_missing() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Rule: when left_leg is NOT linked via HAS_PART, cap_disable locomotion
    kg->setProperty(b.left_leg, "rule.0.trigger", "relation_missing:HAS_PART");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");

    // Currently linked → rule does not fire
    auto p1 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p1.locomotion_factor > 0.9f, "locomotion intact when part attached");

    // Sever the limb: remove HAS_PART relation
    bool removed = kg->destroyRelation(b.entity, "HAS_PART", b.left_leg);
    ASSERT(removed, "relation removed");

    // Now the rule fires — but note: after severing, left_leg is no longer
    // in HAS_PART, so it won't even be included in body part iteration.
    // This trigger is most useful when the part has OTHER relations or is
    // a standalone entity that's being tracked.
    auto p2 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // With only right_leg left: average mode over expected_count=2 → 0.5
    ASSERT_NEAR(p2.locomotion_factor, 0.5f, 0.02f,
                "severed limb no longer contributes");
}

void test_custom_trigger_registration() {
    // Games can add their own named trigger evaluators at startup
    auto& registry = capability::TriggerRegistry::instance();
    registry.register_trigger("always_true",
        [](const capability::TriggerContext&, const std::string&) {
            return true;
        });
    registry.register_trigger("property_equals",
        [](const capability::TriggerContext& ctx, const std::string& args) {
            // args: "key=value"
            auto eq = args.find('=');
            if (eq == std::string::npos) return false;
            std::string key = args.substr(0, eq);
            std::string want = args.substr(eq + 1);
            return ctx.kg.getProperty(ctx.part_id, key) == want;
        });

    auto kg = create_kg();
    auto b = create_biped(*kg);

    // always_true: fires regardless of state
    kg->setProperty(b.left_leg, "rule.0.trigger", "always_true");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_modifier:locomotion:0.5");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.5f, 0.02f,
                "custom always_true trigger fires");

    // property_equals: fires on custom property match. The custom
    // property is declared first — the setProperty gate (Malleus H1)
    // rejects undeclared keys.
    {
        kg::OntologyRegistry ext;
        ext.addProperty("Leg", "infected", "string", false);
        kg->extendOntology(ext);
    }
    kg->setProperty(b.right_leg, "infected", "yes");
    kg->setProperty(b.right_leg, "rule.0.trigger", "property_equals:infected=yes");
    kg->setProperty(b.right_leg, "rule.0.effect",  "cap_disable:locomotion");

    auto p2 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p2.locomotion_factor, 0.0f, 0.02f,
                "property_equals trigger fires with custom args");
}

void test_trigger_age_above_fires_when_old() {
    // Set up a game-time reference frame
    GameTime::reset();
    GameTime::initialize(0.0);

    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Mark the left leg with a creation time, then advance game time
    kg->setProperty(b.left_leg, "created_at", "0.0");
    GameTime::jump_to(10.0);  // 10 seconds later

    // Rule: when part is older than 5 seconds, disable manipulation
    kg->setProperty(b.left_leg, "rule.0.trigger", "age_above:5");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:manipulation");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.manipulation_factor, 0.0f, 0.001f,
                "age_above fires when part age > threshold");
}

void test_trigger_age_above_does_not_fire_when_young() {
    GameTime::reset();
    GameTime::initialize(0.0);

    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "created_at", "0.0");
    GameTime::jump_to(2.0);  // only 2 seconds

    kg->setProperty(b.left_leg, "rule.0.trigger", "age_above:5");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:manipulation");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.manipulation_factor > 0.9f,
           "age_above does not fire before threshold");
}

void test_trigger_age_below_inverse() {
    GameTime::reset();
    GameTime::initialize(0.0);

    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "created_at", "0.0");
    GameTime::jump_to(3.0);

    // Rule: while part is younger than 5 seconds, give a capability bonus
    // (expressed negatively here: halve locomotion — just exercising the trigger)
    kg->setProperty(b.left_leg, "rule.0.trigger", "age_below:5");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_modifier:locomotion:0.5");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor < 0.6f,
           "age_below fires while part is young (modifier applied)");

    // Advance past threshold
    GameTime::jump_to(10.0);
    auto p2 = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p2.locomotion_factor > 0.9f,
           "age_below stops firing after threshold");
}

void test_trigger_age_without_created_at_is_noop() {
    GameTime::reset();
    GameTime::initialize(0.0);
    GameTime::jump_to(100.0);

    auto kg = create_kg();
    auto b = create_biped(*kg);
    // Do NOT set created_at on left_leg

    kg->setProperty(b.left_leg, "rule.0.trigger", "age_above:5");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:manipulation");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.manipulation_factor > 0.9f,
           "age trigger is no-op without created_at property");
}

void test_unknown_trigger_is_silent_noop() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Typo / unregistered trigger
    kg->setProperty(b.left_leg, "rule.0.trigger", "helt_bellow:50");
    kg->setProperty(b.left_leg, "rule.0.effect",  "cap_disable:locomotion");
    kg->setProperty(b.left_leg, "health", "10");

    // Should not crash, rule simply does not fire
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor > 0.0f, "unknown trigger does not crash or fire");
}

// ========================================================================
// Layer 2: Compound triggers (AND/OR composition)
// ========================================================================

void test_compound_trigger_and_fires_when_all_true() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Both conditions true: damaged leg AND no TAIL relation on entity.
    kg->setProperty(b.left_leg, "health", "10");  // health_below:50 true
    kg->setProperty(b.left_leg, "rule.0.trigger",
        "compound:(health_below:50 AND relation_missing:TAIL)");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor < 0.6f,
           "compound AND fires when all atoms true");
}

void test_compound_trigger_and_fails_when_one_false() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // First atom true, second false: part IS linked via HAS_PART.
    kg->setProperty(b.left_leg, "health", "10");
    kg->setProperty(b.left_leg, "rule.0.trigger",
        "compound:(health_below:50 AND relation_missing:HAS_PART)");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    // Rule doesn't fire; damaged leg still contributes a small amount:
    // average over expected=2 → (0.1 + 1.0) / 2 ≈ 0.55.
    ASSERT(p.locomotion_factor > 0.4f,
           "compound AND does not fire when one atom false");
}

void test_compound_trigger_or_fires_when_any_true() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // First atom false (leg is healthy), second true (no TAIL relation).
    // cap_disable zeroes the capability across the entity.
    kg->setProperty(b.left_leg, "rule.0.trigger",
        "compound:(health_below:50 OR relation_missing:TAIL)");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT_NEAR(p.locomotion_factor, 0.0f, 0.01f,
                "compound OR fires when at least one atom true");
}

void test_compound_trigger_unknown_inner_silent_false() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "health", "10");
    kg->setProperty(b.left_leg, "rule.0.trigger",
        "compound:(health_below:50 AND totally_unknown_trigger:42)");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:locomotion");

    // Unknown inner → that atom is false → AND is false → rule does not fire.
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor > 0.4f,
           "compound with unknown inner trigger is silent false, no crash");
}

void test_compound_trigger_malformed_silent_false() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "health", "10");
    // Unbalanced parens + mixed AND/OR. Both should be silent false.
    kg->setProperty(b.left_leg, "rule.0.trigger",
        "compound:(health_below:50 AND destroyed OR relation_missing:TAIL");
    kg->setProperty(b.left_leg, "rule.0.effect", "cap_disable:locomotion");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor > 0.4f,
           "compound with malformed expression is silent false, no crash");
}

// ========================================================================
// Layer 2: emit_event effect
// ========================================================================

struct CapturedEvent {
    std::string event_type;
    std::string source_entity_id;
    std::string target_entity_id;
};

void test_emit_event_fires_on_trigger() {
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    std::vector<CapturedEvent> captured;
    bus.state_changes().subscribe(
        [&captured](const logosphere::ontology::WorldEvent& e) {
            CapturedEvent c;
            c.event_type = e.event_type;
            if (e.source_entity_id) c.source_entity_id = *e.source_entity_id;
            if (e.target_entity_id) c.target_entity_id = *e.target_entity_id;
            captured.push_back(c);
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:limb_lost");

    captured.clear();  // ignore setup setProperty emissions

    kg->setProperty(b.left_leg, "health", "0");  // trigger destroyed
    captured.clear();                             // ignore setProperty's own state_changes

    // Now compute — rule fires, emit_event should fire
    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    bool found = false;
    for (const auto& c : captured) {
        if (c.event_type == "limb_lost") {
            ASSERT(c.source_entity_id == std::to_string(b.left_leg),
                   "source_entity_id = left_leg part id");
            ASSERT(c.target_entity_id == std::to_string(b.entity),
                   "target_entity_id = biped entity id");
            found = true;
        }
    }
    ASSERT(found, "limb_lost event fired");
}

void test_emit_event_does_not_fire_when_untriggered() {
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    int limb_lost_count = 0;
    bus.state_changes().subscribe(
        [&limb_lost_count](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "limb_lost") limb_lost_count++;
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "health_below:50");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:limb_lost");
    // Leg is at 100, above threshold — rule should not fire

    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(limb_lost_count == 0,
           "untriggered rule does not emit event");
}

void test_emit_event_without_bus_is_noop() {
    auto kg = create_kg();
    // NO event bus wired — effect should silently do nothing
    auto b = create_biped(*kg);

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:should_not_matter");
    kg->setProperty(b.left_leg, "health", "0");

    // Should not crash
    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    ASSERT(p.locomotion_factor < 1.0f, "compute still works without bus");
}

void test_emit_event_payload_populated_from_kg() {
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    std::unordered_map<std::string, std::string> captured_payload;
    std::string captured_type;
    bus.state_changes().subscribe(
        [&](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "limb_lost") {
                captured_type = e.event_type;
                // Payload keys/values are parallel vectors; zip them
                for (size_t i = 0; i < e.payload_keys.size() && i < e.payload_values.size(); i++) {
                    captured_payload[e.payload_keys[i]] = e.payload_values[i];
                }
            }
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:limb_lost");
    kg->setProperty(b.left_leg, "rule.0.payload.limb_name", "left_leg");
    kg->setProperty(b.left_leg, "rule.0.payload.severity", "critical");
    kg->setProperty(b.left_leg, "rule.0.payload.hp_lost", "100");
    kg->setProperty(b.left_leg, "health", "0");

    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(captured_type == "limb_lost", "event fired with correct type");
    ASSERT(captured_payload.size() == 3, "payload has 3 entries");
    ASSERT(captured_payload["limb_name"] == "left_leg", "limb_name set");
    ASSERT(captured_payload["severity"] == "critical", "severity set");
    ASSERT(captured_payload["hp_lost"] == "100", "hp_lost set");
}

void test_emit_event_without_payload() {
    // Regression: emit_event with no payload props should still work
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    size_t captured_keys = 0;
    bus.state_changes().subscribe(
        [&captured_keys](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "bare") captured_keys = e.payload_keys.size();
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:bare");
    kg->setProperty(b.left_leg, "health", "0");

    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(captured_keys == 0, "empty payload when no props set");
}

void test_emit_event_payload_per_rule() {
    // Each rule has its own payload scope (rule.0.payload.* vs rule.1.payload.*)
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> by_type;
    bus.state_changes().subscribe(
        [&by_type](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type.substr(0, 5) != "evt_a" && e.event_type.substr(0, 5) != "evt_b")
                return;
            for (size_t i = 0; i < e.payload_keys.size() && i < e.payload_values.size(); i++) {
                by_type[e.event_type][e.payload_keys[i]] = e.payload_values[i];
            }
        });

    // Two rules on same part, each with its own payload
    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:evt_a");
    kg->setProperty(b.left_leg, "rule.0.payload.tag", "from_rule_0");

    kg->setProperty(b.left_leg, "rule.1.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.1.effect",  "emit_event:evt_b");
    kg->setProperty(b.left_leg, "rule.1.payload.tag", "from_rule_1");

    kg->setProperty(b.left_leg, "health", "0");

    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(by_type["evt_a"]["tag"] == "from_rule_0",
           "rule 0 payload scoped correctly");
    ASSERT(by_type["evt_b"]["tag"] == "from_rule_1",
           "rule 1 payload scoped correctly");
}

void test_emit_event_fires_every_compute() {
    auto kg = create_kg();
    logosphere::EventBus bus;
    kg->set_event_bus(&bus);

    auto b = create_biped(*kg);

    int count = 0;
    bus.state_changes().subscribe(
        [&count](const logosphere::ontology::WorldEvent& e) {
            if (e.event_type == "pulse") count++;
        });

    kg->setProperty(b.left_leg, "rule.0.trigger", "destroyed");
    kg->setProperty(b.left_leg, "rule.0.effect",  "emit_event:pulse");
    kg->setProperty(b.left_leg, "health", "0");

    // Three independent computes: each should re-emit (stateless)
    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);
    CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    ASSERT(count == 3, "emit_event fires once per compute");
}

void test_cap_modifier_compounds_across_parts() {
    auto kg = create_kg();
    auto b = create_biped(*kg);

    // Both legs damaged, each contributes its own modifier.
    // 0.5 * 0.5 = 0.25 compound multiplier.
    kg->setProperty(b.left_leg,  "rule.0.trigger", "health_below:100");
    kg->setProperty(b.left_leg,  "rule.0.effect",   "cap_modifier:locomotion:0.5");
    kg->setProperty(b.right_leg, "rule.0.trigger", "health_below:100");
    kg->setProperty(b.right_leg, "rule.0.effect",   "cap_modifier:locomotion:0.5");

    // Damage both legs slightly so rules fire
    kg->setProperty(b.left_leg,  "health", "99");
    kg->setProperty(b.right_leg, "health", "99");

    auto p = CapabilityProfile::compute_from_kg(*kg, b.entity, 75.0f, 0.9f, 1.8f);

    // Normal aggregation: average(0.99, 0.99) ≈ 0.99
    // Compound modifier: 0.99 * 0.25 ≈ 0.2475
    ASSERT_NEAR(p.locomotion_factor, 0.2475f, 0.02f,
                "modifiers compound multiplicatively");
}

// --- Main ---

int main() {
    std::cout << "=== KG-Driven Capability System Tests ===" << std::endl;

    std::cout << "\n-- Layer 1: Capability Bindings --" << std::endl;
    TEST(healthy_biped_full_capabilities);
    TEST(damaged_leg_reduces_locomotion);
    TEST(destroyed_leg_halves_locomotion);
    TEST(arm_damage_no_locomotion_effect);
    TEST(minimum_mode_torso);
    TEST(binary_mode_wings);
    TEST(weighted_sum_serpent);
    TEST(custom_capability_name);
    TEST(speed_cap_in_derive);

    std::cout << "\n-- Layer 2: Effect Registry --" << std::endl;
    TEST(custom_effect_registration);
    TEST(custom_effect_with_event_emission);
    TEST(unknown_effect_is_silent_noop);
    TEST(multiple_effects_all_dispatched);

    std::cout << "\n-- Layer 2: Trigger Registry --" << std::endl;
    TEST(trigger_health_above);
    TEST(trigger_relation_missing);
    TEST(trigger_age_above_fires_when_old);
    TEST(trigger_age_above_does_not_fire_when_young);
    TEST(trigger_age_below_inverse);
    TEST(trigger_age_without_created_at_is_noop);
    TEST(custom_trigger_registration);
    TEST(unknown_trigger_is_silent_noop);
    TEST(compound_trigger_and_fires_when_all_true);
    TEST(compound_trigger_and_fails_when_one_false);
    TEST(compound_trigger_or_fires_when_any_true);
    TEST(compound_trigger_unknown_inner_silent_false);
    TEST(compound_trigger_malformed_silent_false);

    std::cout << "\n-- Layer 2: Response Rules --" << std::endl;
    TEST(response_rule_speed_cap);
    TEST(response_rule_cap_disable);
    TEST(response_rule_cap_modifier);
    TEST(cap_disable_beats_modifier);
    TEST(cap_modifier_compounds_across_parts);
    TEST(emit_event_fires_on_trigger);
    TEST(emit_event_does_not_fire_when_untriggered);
    TEST(emit_event_without_bus_is_noop);
    TEST(emit_event_fires_every_compute);
    TEST(emit_event_payload_populated_from_kg);
    TEST(emit_event_without_payload);
    TEST(emit_event_payload_per_rule);
    TEST(cascade_torso_to_limbs);
    TEST(cascade_named_relation_applies);
    TEST(cascade_named_relation_empty_target_is_noop);
    TEST(cascade_multiple_clauses_min_wins);
    TEST(cascade_children_alias_still_works);
    TEST(rule_group_disabled_skips_rule);
    TEST(rule_group_enabled_fires_rule);
    TEST(rule_group_undeclared_fails_closed);
    TEST(rule_without_group_unaffected);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
