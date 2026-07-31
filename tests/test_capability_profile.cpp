// TDD: CapabilityProfile + DynamicsParams tests
//
// Tests that physical inputs + body graph state correctly derive capability
// factors and dynamics parameters. Replaces test_forge_profile.cpp.

#include "../src/test_context.h"
#include "logosphere/capability/capability_profile.h"
#include "logosphere/capability/dynamics_params.h"
#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/kg/kg_module.h"
#include <cstdio>
#include <cmath>
#include <cassert>

bool test_capability_profile(TestContext& ctx) {
    printf("\n=== CapabilityProfile + DynamicsParams Tests ===\n");
    bool all_pass = true;

    // ================================================================
    // TEST 1: Reference human produces expected baseline dynamics
    // ================================================================
    {
        printf("\n--- Test 1: Reference human baseline ---\n");
        auto cap = CapabilityProfile::compute(250.0f, 500.0f, 75.0f, 0.9f, 1.8f);
        auto d = DynamicsParams::from_capability(cap);

        bool walk_ok = d.max_walk_speed > 1.5f && d.max_walk_speed < 2.5f;
        printf("  walk_speed=%.2f (expect ~2.0): %s\n", d.max_walk_speed, walk_ok ? "OK" : "FAIL");

        bool turn_ok = std::abs(d.walk_turn_rate - 2.0f) < 0.1f;
        printf("  walk_turn=%.2f (expect 2.0): %s\n", d.walk_turn_rate, turn_ok ? "OK" : "FAIL");

        bool dead_ok = std::abs(d.eye_dead_zone - 0.44f) < 0.01f;
        printf("  eye_dead_zone=%.3f (expect 0.44): %s\n", d.eye_dead_zone, dead_ok ? "OK" : "FAIL");

        bool stride_ok = std::abs(d.walk_stride_length - 0.65f) < 0.01f;
        printf("  stride=%.3f (expect 0.65): %s\n", d.walk_stride_length, stride_ok ? "OK" : "FAIL");

        bool pass = walk_ok && turn_ok && dead_ok && stride_ok;
        printf("  Test 1: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 2: Fast reflexes produce tighter dead zones, quicker cadence
    // ================================================================
    {
        printf("\n--- Test 2: Fast reflexes (100ms) ---\n");
        auto fast_cap = CapabilityProfile::compute(100.0f, 500.0f, 75.0f, 0.9f, 1.8f);
        auto ref_cap  = CapabilityProfile::compute(250.0f, 500.0f, 75.0f, 0.9f, 1.8f);
        auto fast = DynamicsParams::from_capability(fast_cap);
        auto ref  = DynamicsParams::from_capability(ref_cap);

        bool dz_ok = fast.eye_dead_zone < ref.eye_dead_zone;
        printf("  eye_dead_zone: fast=%.3f < ref=%.3f: %s\n",
            fast.eye_dead_zone, ref.eye_dead_zone, dz_ok ? "OK" : "FAIL");

        bool cadence_ok = fast.walk_swing_ms < ref.walk_swing_ms;
        printf("  walk_swing_ms: fast=%.0f < ref=%.0f: %s\n",
            fast.walk_swing_ms, ref.walk_swing_ms, cadence_ok ? "OK" : "FAIL");

        bool turn_ok = fast.walk_turn_rate > ref.walk_turn_rate;
        printf("  walk_turn: fast=%.2f > ref=%.2f: %s\n",
            fast.walk_turn_rate, ref.walk_turn_rate, turn_ok ? "OK" : "FAIL");

        bool gaze_ok = fast.gaze_hold_threshold < ref.gaze_hold_threshold;
        printf("  gaze_hold: fast=%.2f < ref=%.2f: %s\n",
            fast.gaze_hold_threshold, ref.gaze_hold_threshold, gaze_ok ? "OK" : "FAIL");

        bool pass = dz_ok && cadence_ok && turn_ok && gaze_ok;
        printf("  Test 2: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 3: Higher grit produces faster locomotion
    // ================================================================
    {
        printf("\n--- Test 3: High grit (2000W) ---\n");
        auto strong_cap = CapabilityProfile::compute(250.0f, 2000.0f, 75.0f, 0.9f, 1.8f);
        auto ref_cap    = CapabilityProfile::compute(250.0f,  500.0f, 75.0f, 0.9f, 1.8f);
        auto strong = DynamicsParams::from_capability(strong_cap);
        auto ref    = DynamicsParams::from_capability(ref_cap);

        bool walk_ok = strong.max_walk_speed > ref.max_walk_speed;
        printf("  walk: strong=%.2f > ref=%.2f: %s\n",
            strong.max_walk_speed, ref.max_walk_speed, walk_ok ? "OK" : "FAIL");

        bool accel_ok = strong.max_acceleration > ref.max_acceleration;
        printf("  accel: strong=%.2f > ref=%.2f: %s\n",
            strong.max_acceleration, ref.max_acceleration, accel_ok ? "OK" : "FAIL");

        bool pass = walk_ok && accel_ok;
        printf("  Test 3: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 4: Longer legs produce longer stride
    // ================================================================
    {
        printf("\n--- Test 4: Longer legs (1.1m vs 0.7m) ---\n");
        auto tall_cap  = CapabilityProfile::compute(250.0f, 500.0f, 75.0f, 1.1f, 2.0f);
        auto short_cap = CapabilityProfile::compute(250.0f, 500.0f, 75.0f, 0.7f, 1.5f);
        auto tall  = DynamicsParams::from_capability(tall_cap);
        auto short_p = DynamicsParams::from_capability(short_cap);

        bool stride_ok = tall.walk_stride_length > short_p.walk_stride_length;
        printf("  stride: tall=%.3f > short=%.3f: %s\n",
            tall.walk_stride_length, short_p.walk_stride_length, stride_ok ? "OK" : "FAIL");

        bool pass = stride_ok;
        printf("  Test 4: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 5: KG body graph - damaged leg reduces locomotion capability
    // ================================================================
    {
        printf("\n--- Test 5: KG body graph - leg damage ---\n");
        auto& engine = ctx.get_engine();
        auto& kg = engine.get_kg();

        auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
        PhysicsHumanoidResult eva = humanoid_gen.generate_humanoid_physics(
            0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
        eva.create_kg_entities(kg, "Humanoid", 180.0f, 800.0f);

        auto healthy_cap = CapabilityProfile::compute_from_kg(kg, eva.entity_id,
            36.5f, 0.9f, 1.7f);
        auto healthy_d = DynamicsParams::from_capability(healthy_cap);

        printf("  Healthy: loco=%.2f walk=%.2f\n",
            healthy_cap.locomotion_factor, healthy_d.max_walk_speed);

        auto parts = kg.getRelated(eva.entity_id, "HAS_PART");
        for (auto part_id : parts) {
            std::string name = kg.getProperty(part_id, "body_part_name");
            if (name == "left_leg") {
                kg.setProperty(part_id, "health", "50.0");
                printf("  Damaged left_leg: health=50/100\n");
                break;
            }
        }

        auto damaged_cap = CapabilityProfile::compute_from_kg(kg, eva.entity_id,
            36.5f, 0.9f, 1.7f);
        auto damaged_d = DynamicsParams::from_capability(damaged_cap);

        printf("  Damaged: loco=%.2f walk=%.2f\n",
            damaged_cap.locomotion_factor, damaged_d.max_walk_speed);

        bool loco_ok = damaged_cap.locomotion_factor < healthy_cap.locomotion_factor;
        printf("  loco_factor reduced: %s\n", loco_ok ? "OK" : "FAIL");

        bool walk_ok = damaged_d.max_walk_speed < healthy_d.max_walk_speed;
        printf("  walk_speed reduced: %s\n", walk_ok ? "OK" : "FAIL");

        bool side_ok = damaged_cap.left_leg_factor < damaged_cap.right_leg_factor;
        printf("  left_leg_factor=%.2f < right=%.2f: %s\n",
            damaged_cap.left_leg_factor, damaged_cap.right_leg_factor, side_ok ? "OK" : "FAIL");

        bool pass = loco_ok && walk_ok && side_ok;
        printf("  Test 5: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 6: Missing arm - manipulation reduced, locomotion unaffected
    // ================================================================
    {
        printf("\n--- Test 6: Missing arm ---\n");
        auto& engine = ctx.get_engine();
        auto& kg = engine.get_kg();

        auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
        PhysicsHumanoidResult eva = humanoid_gen.generate_humanoid_physics(
            5.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
        eva.create_kg_entities(kg, "Humanoid", 250.0f, 500.0f);

        auto parts = kg.getRelated(eva.entity_id, "HAS_PART");
        for (auto part_id : parts) {
            std::string name = kg.getProperty(part_id, "body_part_name");
            if (name == "left_arm") {
                kg.setProperty(part_id, "health", "0.0");
                printf("  Destroyed left_arm: health=0/80\n");
                break;
            }
        }

        auto cap = CapabilityProfile::compute_from_kg(kg, eva.entity_id,
            36.5f, 0.9f, 1.7f);

        bool arm_ok = cap.left_arm_factor == 0.0f;
        printf("  left_arm_factor=%.2f (expect 0): %s\n", cap.left_arm_factor, arm_ok ? "OK" : "FAIL");

        bool manip_ok = cap.manipulation_factor < 1.0f;
        printf("  manipulation=%.2f (expect <1.0): %s\n", cap.manipulation_factor, manip_ok ? "OK" : "FAIL");

        bool loco_ok = cap.locomotion_factor > 0.9f;
        printf("  locomotion=%.2f (expect ~1.0): %s\n", cap.locomotion_factor, loco_ok ? "OK" : "FAIL");

        bool pass = arm_ok && manip_ok && loco_ok;
        printf("  Test 6: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    // ================================================================
    // TEST 7: Animation timing scales with reflexes
    // ================================================================
    {
        printf("\n--- Test 7: Animation timing from DynamicsParams ---\n");
        auto fast_cap = CapabilityProfile::compute(100.0f, 500.0f, 75.0f, 0.9f, 1.8f);
        auto slow_cap = CapabilityProfile::compute(400.0f, 500.0f, 75.0f, 0.9f, 1.8f);
        auto fast = DynamicsParams::from_capability(fast_cap);
        auto slow = DynamicsParams::from_capability(slow_cap);

        bool swing_ok = fast.walk_swing_ms < slow.walk_swing_ms;
        printf("  swing_ms: fast=%.0f < slow=%.0f: %s\n",
            fast.walk_swing_ms, slow.walk_swing_ms, swing_ok ? "OK" : "FAIL");

        bool stab_ok = fast.head_stabilize_walk > slow.head_stabilize_walk;
        printf("  head_stab: fast=%.3f > slow=%.3f: %s\n",
            fast.head_stabilize_walk, slow.head_stabilize_walk, stab_ok ? "OK" : "FAIL");

        bool blend_ok = fast.run_blend_rise_rate > slow.run_blend_rise_rate;
        printf("  blend_rise: fast=%.1f > slow=%.1f: %s\n",
            fast.run_blend_rise_rate, slow.run_blend_rise_rate, blend_ok ? "OK" : "FAIL");

        bool pass = swing_ok && stab_ok && blend_ok;
        printf("  Test 7: %s\n", pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    printf("\n%s: CapabilityProfile + DynamicsParams tests\n", all_pass ? "PASS" : "FAIL");
    return all_pass;
}
