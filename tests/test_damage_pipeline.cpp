// Damage → Body Part Health → Capability Pipeline Test
//
// End-to-end test: damage a body part via DamageSystem, verify that
// CapabilityProfile recomputes and capability factors degrade.
//
// This tests the reactive pipeline:
//   DamageSystem.apply_to_body_part()
//     → KG setProperty("health", reduced)
//       → STATE_CHANGE event
//         → ParticleDynamicsSystem subscriber
//           → recompute_capability()
//             → locomotion_factor drops
//
// Usage:
//   ./build/test_damage_pipeline --no-head

#include "core/engine.h"
#include "logosphere/damage/damage_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/capability/capability_profile.h"
#include "logosphere/capability/dynamics_params.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <string>
#include <stdexcept>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(engine); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg) + " [" + #cond + "]")

// Helper: create a physics humanoid with KG body graph
struct TestHumanoid {
    PhysicsHumanoidResult result;
    kg::EntityID entity_id;
};

static TestHumanoid create_test_humanoid(Engine& engine, float x, float y, float z) {
    auto& gen = engine.get_worldgen_system().get_humanoid_generator();
    auto& kg = engine.get_kg();

    auto result = gen.generate_humanoid_physics(x, y, z, -1, HumanoidSpec::eva(), false);
    result.create_kg_entities(kg, "Humanoid", 250.0f, 500.0f);

    return {result, result.entity_id};
}

// --- Tests ---

void test_body_part_health_in_kg(Engine& engine) {
    auto& kg = engine.get_kg();
    auto h = create_test_humanoid(engine, 0.0f, 0.0f, 0.0f);

    // Verify body parts have health properties
    auto parts = kg.getRelated(h.entity_id, "HAS_PART");
    ASSERT(!parts.empty(), "humanoid has body parts");

    bool found_leg = false;
    for (auto part_id : parts) {
        std::string type = kg.getType(part_id);
        if (type == "Leg") {
            auto hp = kg.getProperty(part_id, "health");
            auto max_hp = kg.getProperty(part_id, "max_health");
            ASSERT(!hp.empty(), "leg has health property");
            ASSERT(!max_hp.empty(), "leg has max_health property");
            ASSERT(std::stof(hp) == 100.0f, "leg starts at 100 HP");
            found_leg = true;
        }
    }
    ASSERT(found_leg, "found a Leg body part");
}

void test_apply_to_body_part_reduces_kg_health(Engine& engine) {
    auto& kg = engine.get_kg();
    auto h = create_test_humanoid(engine, 2.0f, 0.0f, 0.0f);

    DamageSystem damage(nullptr, &engine.get_event_bus());
    damage.set_kg(&kg);
    damage.register_entity(h.entity_id, 500.0f);

    // Damage left leg
    float remaining = damage.apply_to_body_part(h.entity_id, "left_leg", 40.0f, DamageType::Blunt);
    ASSERT(remaining >= 0.0f, "body part found");
    ASSERT(remaining == 60.0f, "leg health reduced to 60");

    // Verify in KG
    auto parts = kg.getRelated(h.entity_id, "HAS_PART");
    for (auto part_id : parts) {
        std::string name = kg.getProperty(part_id, "body_part_name");
        if (name == "left_leg") {
            float hp = std::stof(kg.getProperty(part_id, "health"));
            ASSERT(hp == 60.0f, "KG health matches");
            return;
        }
    }
    ASSERT(false, "left_leg not found in KG");
}

void test_capability_profile_reads_damaged_health(Engine& engine) {
    auto& kg = engine.get_kg();
    auto h = create_test_humanoid(engine, 4.0f, 0.0f, 0.0f);

    // Healthy profile first
    auto healthy_cap = CapabilityProfile::compute_from_kg(kg, h.entity_id, 75.0f, 0.9f, 1.8f);
    auto healthy_d   = DynamicsParams::from_capability(healthy_cap);
    ASSERT(healthy_cap.locomotion_factor > 0.9f, "healthy locomotion near 1.0");

    // Damage left leg to 0 in KG
    auto parts = kg.getRelated(h.entity_id, "HAS_PART");
    for (auto part_id : parts) {
        std::string name = kg.getProperty(part_id, "body_part_name");
        if (name == "left_leg") {
            kg.setProperty(part_id, "health", "0.0");
            break;
        }
    }

    // Recompute
    auto damaged_cap = CapabilityProfile::compute_from_kg(kg, h.entity_id, 75.0f, 0.9f, 1.8f);
    auto damaged_d   = DynamicsParams::from_capability(damaged_cap);
    ASSERT(damaged_cap.locomotion_factor < healthy_cap.locomotion_factor,
           "locomotion degraded after leg damage");
    ASSERT(damaged_cap.left_leg_factor == 0.0f, "left leg factor is 0");
    ASSERT(damaged_d.max_walk_speed < healthy_d.max_walk_speed,
           "walk speed reduced");
}

void test_damage_pipeline_end_to_end(Engine& engine) {
    auto& kg = engine.get_kg();
    auto h = create_test_humanoid(engine, 6.0f, 0.0f, 0.0f);

    // Register humanoid in dynamics so the event subscriber can find it
    auto& dynamics = engine.get_dynamics_system();
    engine.get_humanoid_locomotion().register_humanoid_direct(
        h.result.hips_id,
        h.result.left_leg_ids, h.result.right_leg_ids,
        h.result.left_arm_ids, h.result.right_arm_ids,
        h.result.torso_ids, 250.0f, 500.0f, h.entity_id
    );

    // Get initial walk speed
    float initial_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.result.hips_id);
    ASSERT(initial_speed > 0.0f, "initial walk speed > 0");

    // Set up damage system with KG and event bus
    DamageSystem damage(nullptr, &engine.get_event_bus());
    damage.set_kg(&kg);
    damage.register_entity(h.entity_id, 500.0f);

    // Damage left leg severely
    damage.apply_to_body_part(h.entity_id, "left_leg", 100.0f, DamageType::Slash);

    // The STATE_CHANGE event fires synchronously (signal tier),
    // so recompute_forge() has already been called

    // Verify walk speed dropped
    float damaged_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.result.hips_id);
    ASSERT(damaged_speed < initial_speed,
           "walk speed dropped after leg damage");

    std::cout << "(speed: " << initial_speed << " -> " << damaged_speed << ") ";
}

void test_arm_damage_no_locomotion_effect(Engine& engine) {
    auto& kg = engine.get_kg();
    auto h = create_test_humanoid(engine, 8.0f, 0.0f, 0.0f);

    auto& dynamics = engine.get_dynamics_system();
    engine.get_humanoid_locomotion().register_humanoid_direct(
        h.result.hips_id,
        h.result.left_leg_ids, h.result.right_leg_ids,
        h.result.left_arm_ids, h.result.right_arm_ids,
        h.result.torso_ids, 250.0f, 500.0f, h.entity_id
    );

    float initial_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.result.hips_id);

    DamageSystem damage(nullptr, &engine.get_event_bus());
    damage.set_kg(&kg);
    damage.register_entity(h.entity_id, 500.0f);

    // Destroy both arms
    damage.apply_to_body_part(h.entity_id, "left_arm", 80.0f, DamageType::Slash);
    damage.apply_to_body_part(h.entity_id, "right_arm", 80.0f, DamageType::Slash);

    float after_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.result.hips_id);

    // Arms don't affect locomotion
    ASSERT(std::abs(after_speed - initial_speed) < 0.01f,
           "arm damage doesn't affect walk speed");
}

// --- Main ---

int main(int argc, char* argv[]) {
    // Parse --no-head for headless mode
    bool headless = true;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--no-head") headless = true;
    }

    Engine engine;
    EngineConfig config;
    config.create_display = !headless;
    engine.initialize(config);

    std::cout << "=== Damage Pipeline Tests ===" << std::endl;

    TEST(body_part_health_in_kg);
    TEST(apply_to_body_part_reduces_kg_health);
    TEST(capability_profile_reads_damaged_health);
    TEST(damage_pipeline_end_to_end);
    TEST(arm_damage_no_locomotion_effect);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;

    engine.shutdown();
    return tests_failed > 0 ? 1 : 0;
}
