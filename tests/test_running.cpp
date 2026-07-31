// Running System Test Suite
//
// Physics-based running: max speeds derived from FORGE attributes (grit_W,
// force_N, mass), not arbitrary constants. Status effects (damage, low HP)
// feed into speed modifiers. Universal — works for any bipedal entity
// (humanoids, shamblers, future creatures).
//
// Speed model:
//   speed_base = sqrt(grit_W / mass)
//   max_walk_speed = speed_base * WALK_EFFICIENCY
//   max_run_speed  = speed_base * RUN_EFFICIENCY
//   max_acceleration = grit_W / mass * ACCEL_FACTOR
//   effective_max = max_run_speed * speed_modifier  (modifier from status)
//
// Tests:
//   1. Mass computation      — mass > 0 after registration
//   2. Default FORGE speeds  — 400W/~75kg → walk ~2.0, run ~4.6 m/s
//   3. High grit speeds      — 2000W → faster walk and run
//   4. Low grit speeds       — 100W → slower walk and run
//   5. Speed cap enforcement  — can't exceed max_run_speed
//   6. Acceleration from grit — higher grit → faster ramp-up
//   7. Leg damage penalty     — 50% leg damage → ~50% speed reduction
//   8. Low HP penalty         — below 25% HP → speed penalty
//   9. Combined penalties     — leg damage + low HP stack correctly
//  10. Recovery               — removing modifier restores full speed
//  11. Shambler-like entity   — different FORGE stats → different limits
//  12. Walk-to-run continuity — speed ramp is smooth (no discrete jump)
//
// Controls (visual mode):
//   SPACE = advance to next test
//   ESC   = exit
//
// Usage:
//   ./build/logomancers/test-running                        # headless
//   INTERACTIVE=1 ./build/logomancers/test-running          # interactive

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "humanoid_validator.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test fixture — same pattern as test_humanoid_movement.cpp
// ---------------------------------------------------------------------------
struct TestFixture {
    Engine engine;
    ParticleSystem* ps = nullptr;
    ParticleDynamicsSystem* dynamics = nullptr;
    PhysicsSystem* physics = nullptr;
    PhysicsHumanoidResult h;
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    bool headless = true;

    // Assertion tracking
    int total_assertions = 0;
    int passed_assertions = 0;
    int failed_assertions = 0;

    bool init(bool headless_, float reflexes = 150.0f, float grit = 600.0f) {
        headless = headless_;
        EngineConfig config;
        config.create_display = !headless;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "Running System Test";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;

        if (engine.initialize(config) != 0) {
            std::cerr << "Engine init failed" << std::endl;
            return false;
        }

        ps = &engine.get_particle_system();
        dynamics = &engine.get_dynamics_system();
        physics = &(engine.get_physics_system());

        physics->add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

        // Floor
        Particle floor = {};
        floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
        floor.shape = ParticleShape::BOX;
        floor.width = 40.0f; floor.height = 40.0f; floor.thickness = 0.1f;
        floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
        floor.SetMaterial(Materials::Type::HEAVY_STATIC);
        engine.add_particle(floor);

        // Lights
        auto add_light = [this](float x, float y, float z, float strength) {
            Particle light = {};
            light.x = x; light.y = y; light.z = z;
            light.shape = ParticleShape::BOX;
            light.width = 0.2f; light.height = 0.2f; light.thickness = 0.2f;
            light.r = 1.0f; light.g = 0.95f; light.b = 0.9f; light.a = 1.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = strength;
            light.emission_radius = 100.0f;
            engine.add_particle(light);
        };
        add_light(0.0f, 0.0f, 10.0f, 600000.0f);
        add_light(0.0f, -4.0f, 3.0f, 400000.0f);
        add_light(3.0f, 0.0f, 3.0f, 300000.0f);

        // Camera
        auto& camera = engine.get_camera_system();
        camera.set_position(-10.0f, -10.0f, 10.0f);
        camera.look_at(0.0f, 3.0f, 1.0f);
        camera.set_pixels_per_unit(40.0f);

        // Humanoid
        auto& kg = engine.get_kg();
        HumanoidGenerator humanoid_gen;
        humanoid_gen.initialize(&engine, &kg);
        HumanoidSpec spec = HumanoidSpec::hunter();
        h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);

        engine.get_humanoid_locomotion().register_humanoid_direct(
            h.hips_id,
            h.left_leg_ids, h.right_leg_ids,
            h.left_arm_ids, h.right_arm_ids,
            h.torso_ids,
            reflexes, grit,
            h.entity_id
        );
        h.register_joints(&engine.get_humanoid_locomotion());

        // Register walk/strafe/turn clips
        auto right_clip = create_fk_walk_step(Side::RIGHT);
        auto left_clip = create_fk_walk_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_walk_clips(h.hips_id, right_clip, left_clip);

        auto strafe_r = create_fk_strafe_step(Side::RIGHT, 1.0f);
        auto strafe_l = create_fk_strafe_step(Side::LEFT, 1.0f);
        engine.get_humanoid_locomotion().register_strafe_clips(h.hips_id, strafe_r, strafe_l);

        auto turn_r = create_fk_turn_step(Side::RIGHT, 1.0f);
        auto turn_l = create_fk_turn_step(Side::LEFT, -1.0f);
        engine.get_humanoid_locomotion().register_turn_clips(h.hips_id, turn_r, turn_l);

        entity_id = h.entity_id;

        // Stabilize (3 frames)
        for (int i = 0; i < 3; i++) step(0.02);

        return true;
    }

    void reset() {
        {
            auto view = ps->lock_particles_for_write();
            view[h.hips_id].x = 0.0f;
            view[h.hips_id].y = 0.0f;
        }
        engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
        engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, 0.0f);
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);
        engine.get_humanoid_locomotion().zero_all_velocities(h.hips_id);
        engine.get_humanoid_locomotion().set_speed_modifier(h.hips_id, 1.0f);
        step(); step(); step();
    }

    void step(double dt = 0.02) {
        engine.update(dt);
        if (!headless) engine.render();
    }

    float measure_speed() {
        auto particles = ps->lock_particles_for_read();
        auto& hips = particles[h.hips_id];
        return std::sqrt(hips.vx * hips.vx + hips.vy * hips.vy);
    }

    void check(bool condition, const std::string& msg) {
        total_assertions++;
        if (condition) {
            passed_assertions++;
            printf("  [ASSERT] %s PASS\n", msg.c_str());
        } else {
            failed_assertions++;
            printf("  [ASSERT] %s FAIL\n", msg.c_str());
        }
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    bool headless = true;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--visual" || std::string(argv[i]) == "--interactive")
            headless = false;
    }
    if (getenv("INTERACTIVE") && std::string(getenv("INTERACTIVE")) == "1")
        headless = false;

    printf("========================================\n");
    printf("  RUNNING SYSTEM TEST\n");
    printf("  Physics-Based Locomotion Limits\n");
    printf("  Mode: %s\n", headless ? "Headless" : "Interactive");
    printf("========================================\n");

    int total_pass = 0;
    int total_fail = 0;

    // -----------------------------------------------------------------------
    // Test 1: Mass computation — mass > 0 after registration
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 1: Mass Computation ---\n");
        TestFixture f;
        if (!f.init(headless)) return 1;

        float mass = f.engine.get_humanoid_locomotion().get_entity_mass(f.h.hips_id);
        printf("  Entity mass: %.2f kg\n", mass);

        f.check(mass > 10.0f, "mass > 10 kg (not zero/default): " + std::to_string(mass));
        f.check(mass < 200.0f, "mass < 200 kg (reasonable): " + std::to_string(mass));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 2: Default FORGE speeds — 400W grit → known walk/run limits
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 2: Default FORGE Speed Limits ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        float mass = f.engine.get_humanoid_locomotion().get_entity_mass(f.h.hips_id);
        printf("  FORGE: grit=600W, mass=%.1fkg\n", mass);
        printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", max_walk, max_run);

        f.check(max_walk > 1.5f && max_walk < 3.5f,
                "walk speed in reasonable range [1.5, 3.5]: " + std::to_string(max_walk));
        f.check(max_run > 3.5f && max_run < 8.0f,
                "run speed in reasonable range [3.5, 8.0]: " + std::to_string(max_run));
        f.check(max_run > max_walk,
                "run > walk: " + std::to_string(max_run) + " > " + std::to_string(max_walk));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 3: High grit — 2000W → faster speeds
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 3: High Grit (2000W) → Faster ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 2000.0f)) return 1;

        float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        float mass = f.engine.get_humanoid_locomotion().get_entity_mass(f.h.hips_id);
        printf("  FORGE: grit=2000W, mass=%.1fkg\n", mass);
        printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", max_walk, max_run);

        // With 2000W, should be significantly faster than default 600W
        f.check(max_walk > 3.0f,
                "high-grit walk > 3.0: " + std::to_string(max_walk));
        f.check(max_run > 6.0f,
                "high-grit run > 6.0: " + std::to_string(max_run));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 4: Low grit — 100W → slower speeds
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 4: Low Grit (100W) → Slower ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 100.0f)) return 1;

        float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        float mass = f.engine.get_humanoid_locomotion().get_entity_mass(f.h.hips_id);
        printf("  FORGE: grit=100W, mass=%.1fkg\n", mass);
        printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", max_walk, max_run);

        f.check(max_walk < 1.5f,
                "low-grit walk < 1.5: " + std::to_string(max_walk));
        f.check(max_run < 3.5f,
                "low-grit run < 3.5: " + std::to_string(max_run));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 5: Speed cap enforcement — can't exceed max_run_speed
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 5: Speed Cap Enforcement ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        printf("  max_run=%.2f m/s\n", max_run);

        // Request velocity far above max
        float excessive = max_run * 2.0f;
        f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, excessive);
        f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

        // Run 200 frames to reach steady state
        float peak_speed = 0.0f;
        for (int i = 0; i < 200; i++) {
            f.step();
            float s = f.measure_speed();
            if (s > peak_speed) peak_speed = s;
        }

        printf("  Requested: %.2f m/s, peak achieved: %.2f m/s\n", excessive, peak_speed);

        // Should be capped at max_run (with small tolerance for ramp overshoot)
        f.check(peak_speed <= max_run * 1.05f,
                "peak <= max_run * 1.05: " + std::to_string(peak_speed) +
                " <= " + std::to_string(max_run * 1.05f));
        f.check(peak_speed >= max_run * 0.90f,
                "reached near max: " + std::to_string(peak_speed) +
                " >= " + std::to_string(max_run * 0.90f));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 6: Acceleration scales with grit
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 6: Acceleration From Grit ---\n");

        // Low grit: measure frames to reach 1.5 m/s
        float frames_low, frames_high;
        {
            TestFixture f;
            if (!f.init(headless, 150.0f, 200.0f)) return 1;
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
            f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
            frames_low = 0;
            for (int i = 0; i < 300; i++) {
                f.step();
                frames_low++;
                if (f.measure_speed() >= 1.4f) break;
            }
            printf("  Low grit (200W): reached 1.5 m/s in %.0f frames\n", frames_low);
        }

        // High grit: measure frames to reach 1.5 m/s
        {
            TestFixture f;
            if (!f.init(headless, 150.0f, 2000.0f)) return 1;
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
            f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
            frames_high = 0;
            for (int i = 0; i < 300; i++) {
                f.step();
                frames_high++;
                if (f.measure_speed() >= 1.4f) break;
            }
            printf("  High grit (2000W): reached 1.5 m/s in %.0f frames\n", frames_high);
        }

        // High grit should reach target faster
        printf("  Ratio: low/high = %.1f\n", frames_low / frames_high);

        TestFixture dummy;
        dummy.check(frames_high < frames_low,
                    "high grit accelerates faster: " + std::to_string(frames_high) +
                    " < " + std::to_string(frames_low) + " frames");
        dummy.check(frames_low / frames_high > 1.5f,
                    "at least 1.5x difference: " + std::to_string(frames_low / frames_high));

        total_pass += dummy.passed_assertions;
        total_fail += dummy.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 7: Leg damage penalty — reduces max speed
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 7: Leg Damage → Speed Penalty ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        printf("  Healthy max_run=%.2f m/s\n", max_run);

        // Apply 50% speed modifier (simulating leg damage)
        f.engine.get_humanoid_locomotion().set_speed_modifier(f.h.hips_id, 0.5f);
        float effective = f.engine.get_humanoid_locomotion().get_effective_max_speed(f.h.hips_id);
        printf("  With 50%% modifier: effective_max=%.2f m/s\n", effective);

        f.check(std::abs(effective - max_run * 0.5f) < 0.01f,
                "effective = max * 0.5: " + std::to_string(effective));

        // Verify speed is actually capped when moving
        f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_run);
        f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

        float peak = 0.0f;
        for (int i = 0; i < 200; i++) {
            f.step();
            float s = f.measure_speed();
            if (s > peak) peak = s;
        }
        printf("  Peak speed with modifier: %.2f m/s\n", peak);

        f.check(peak <= effective * 1.05f,
                "capped at effective max: " + std::to_string(peak) +
                " <= " + std::to_string(effective * 1.05f));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 8: Low HP penalty
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 8: Low HP → Speed Penalty ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

        // Apply 70% modifier (simulating low HP: 25% health → 0.7 speed)
        f.engine.get_humanoid_locomotion().set_speed_modifier(f.h.hips_id, 0.7f);
        float effective = f.engine.get_humanoid_locomotion().get_effective_max_speed(f.h.hips_id);
        printf("  Low HP modifier 0.7: effective=%.2f m/s (was %.2f)\n", effective, max_run);

        f.check(std::abs(effective - max_run * 0.7f) < 0.01f,
                "effective = max * 0.7: " + std::to_string(effective));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 9: Combined penalties stack multiplicatively
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 9: Combined Penalties Stack ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

        // Leg damage (0.5) × low HP (0.7) = 0.35 effective
        float combined = 0.5f * 0.7f;
        f.engine.get_humanoid_locomotion().set_speed_modifier(f.h.hips_id, combined);
        float effective = f.engine.get_humanoid_locomotion().get_effective_max_speed(f.h.hips_id);
        printf("  Combined modifier %.2f: effective=%.2f m/s (was %.2f)\n",
               combined, effective, max_run);

        f.check(std::abs(effective - max_run * combined) < 0.01f,
                "stacked modifier correct: " + std::to_string(effective));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 10: Recovery — removing modifier restores speed
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 10: Recovery Restores Speed ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

        // Apply penalty
        f.engine.get_humanoid_locomotion().set_speed_modifier(f.h.hips_id, 0.3f);
        float penalized = f.engine.get_humanoid_locomotion().get_effective_max_speed(f.h.hips_id);

        // Remove penalty
        f.engine.get_humanoid_locomotion().set_speed_modifier(f.h.hips_id, 1.0f);
        float restored = f.engine.get_humanoid_locomotion().get_effective_max_speed(f.h.hips_id);

        printf("  Penalized: %.2f → Restored: %.2f (original: %.2f)\n",
               penalized, restored, max_run);

        f.check(std::abs(restored - max_run) < 0.01f,
                "restored = original: " + std::to_string(restored));
        f.check(penalized < restored,
                "penalized < restored: " + std::to_string(penalized) + " < " + std::to_string(restored));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 11: Shambler-like entity — different FORGE → different limits
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 11: Shambler-Like Entity (Low FORGE) ---\n");
        // Shambler: reflexes=300ms (slow), grit=400W (weak)
        TestFixture f;
        if (!f.init(headless, 300.0f, 400.0f)) return 1;

        float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
        printf("  Shambler: reflexes=300ms, grit=400W\n");
        printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", max_walk, max_run);

        // Compare with default fixture (grit=600W)
        TestFixture f2;
        if (!f2.init(headless, 150.0f, 600.0f)) return 1;
        float player_walk = f2.engine.get_humanoid_locomotion().get_max_walk_speed(f2.h.hips_id);
        float player_run = f2.engine.get_humanoid_locomotion().get_max_run_speed(f2.h.hips_id);
        printf("  Player:   reflexes=150ms, grit=600W\n");
        printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", player_walk, player_run);

        f.check(max_walk < player_walk,
                "shambler walks slower: " + std::to_string(max_walk) +
                " < " + std::to_string(player_walk));
        f.check(max_run < player_run,
                "shambler runs slower: " + std::to_string(max_run) +
                " < " + std::to_string(player_run));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Test 12: Walk-to-run continuity — smooth speed ramp
    // -----------------------------------------------------------------------
    {
        printf("\n--- Test 12: Walk-to-Run Continuity ---\n");
        TestFixture f;
        if (!f.init(headless, 150.0f, 600.0f)) return 1;

        float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

        // Request max run speed
        f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_run);
        f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

        // Track speed over time — should be monotonically increasing (no jumps)
        float prev_speed = 0.0f;
        float max_delta = 0.0f;
        int speed_decreases = 0;
        std::vector<float> speed_samples;

        for (int i = 0; i < 200; i++) {
            f.step();
            float s = f.measure_speed();
            speed_samples.push_back(s);
            float delta = s - prev_speed;
            if (delta < -0.05f) speed_decreases++;  // Allow tiny jitter
            if (std::abs(delta) > max_delta) max_delta = std::abs(delta);
            prev_speed = s;
        }

        printf("  Speed ramp: 0 → %.2f m/s over 200 frames\n", prev_speed);
        printf("  Max frame-to-frame delta: %.3f m/s\n", max_delta);
        printf("  Speed decreases (>0.05): %d\n", speed_decreases);

        f.check(speed_decreases <= 3,
                "smooth ramp (decreases <= 3): " + std::to_string(speed_decreases));
        f.check(max_delta < 1.0f,
                "no speed jumps (delta < 1.0): " + std::to_string(max_delta));
        f.check(prev_speed >= max_run * 0.90f,
                "reached near max: " + std::to_string(prev_speed) +
                " >= " + std::to_string(max_run * 0.90f));

        total_pass += f.passed_assertions;
        total_fail += f.failed_assertions;
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    printf("\n========================================\n");
    printf("  RUNNING SYSTEM TEST COMPLETE\n");
    printf("========================================\n");
    printf("  Passed: %d\n", total_pass);
    printf("  Failed: %d\n", total_fail);
    if (total_fail == 0)
        printf("  ALL ASSERTIONS PASSED\n");
    else
        printf("  *** %d ASSERTIONS FAILED ***\n", total_fail);

    return total_fail > 0 ? 1 : 0;
}
