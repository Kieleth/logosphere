// Visual Damage Pipeline Demo
//
// Humanoid walks for 2 seconds, then takes 4 hits to the left leg
// at 0.75s intervals. Walk speed visibly decreases each hit.
// Press SPACE to reset and replay the sequence.
//
// Usage:
//   ./build/test_damage_visual              # interactive (default)
//   ./build/test_damage_visual --no-head    # headless (auto-run, exit after sequence)

#include "core/engine.h"
#include "logosphere/damage/damage_system.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/capability/capability_profile.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <string>
#include <GLFW/glfw3.h>

// Sequence timing (in frames at 60fps)
static constexpr int WALK_START = 0;
static constexpr int HIT_1 = 120;   // 2.0s: first hit
static constexpr int HIT_2 = 165;   // 2.75s
static constexpr int HIT_3 = 210;   // 3.5s
static constexpr int HIT_4 = 255;   // 4.25s
static constexpr int SEQUENCE_END = 360;  // 6.0s: pause before reset

int main(int argc, char* argv[]) {
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--no-head") headless = true;
    }

    Engine engine;
    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "Damage Pipeline Visual";
    config.show_debug_overlay = true;
    engine.initialize(config);

    auto& dynamics = engine.get_dynamics_system();
    auto& physics = engine.get_physics_system();
    auto& kg = engine.get_kg();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Floor
    Particle floor_p = {};
    floor_p.x = 0.0f; floor_p.y = 0.0f; floor_p.z = 0.05f;
    floor_p.shape = ParticleShape::BOX;
    floor_p.width = 200.0f; floor_p.height = 200.0f; floor_p.thickness = 0.1f;
    floor_p.r = 0.3f; floor_p.g = 0.3f; floor_p.b = 0.3f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    engine.add_particle(floor_p);

    // Light
    Particle light = {};
    light.x = 0.0f; light.y = 0.0f; light.z = 10.0f;
    light.shape = ParticleShape::BOX;
    light.width = 0.2f; light.height = 0.2f; light.thickness = 0.2f;
    light.r = 1.0f; light.g = 0.95f; light.b = 0.9f; light.a = 1.0f;
    light.SetMaterial(Materials::Type::LIGHT);
    light.is_light_source = true;
    light.emission_strength = 600000.0f;
    light.emission_radius = 100.0f;
    engine.add_particle(light);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-8.0f, -8.0f, 8.0f);
    camera.look_at(0.0f, 3.0f, 1.0f);
    camera.set_pixels_per_unit(50.0f);

    // Humanoid
    HumanoidGenerator humanoid_gen;
    humanoid_gen.initialize(&engine, &kg);
    HumanoidSpec spec = HumanoidSpec::hunter();
    auto h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);
    h.create_kg_entities(kg, "Humanoid", 200.0f, 600.0f);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        h.hips_id,
        h.left_leg_ids, h.right_leg_ids,
        h.left_arm_ids, h.right_arm_ids,
        h.torso_ids, 200.0f, 600.0f, h.entity_id
    );
    h.register_joints(&engine.get_humanoid_locomotion());

    // Animation clips
    engine.get_humanoid_locomotion().register_walk_clips(h.hips_id,
        create_fk_walk_step(Side::RIGHT), create_fk_walk_step(Side::LEFT));
    engine.get_humanoid_locomotion().register_run_clips(h.hips_id,
        create_fk_run_step(Side::RIGHT), create_fk_run_step(Side::LEFT));
    engine.get_humanoid_locomotion().register_idle_clip(h.hips_id, create_fk_idle_clip());

    // Damage system
    DamageSystem damage(nullptr, &engine.get_event_bus());
    damage.set_kg(&kg);
    damage.register_entity(h.entity_id, 500.0f);

    // Camera follow
    engine.set_input_target_entity(h.entity_id);
    engine.set_camera_follow_enabled(true);
    engine.set_camera_deadzone(2.0f);

    GLFWwindow* win = static_cast<GLFWwindow*>(engine.get_window_handle());

    // Stabilize physics
    for (int i = 0; i < 5; i++) {
        if (!headless) engine.get_platform()->poll_events();
        engine.update(0.02);
        engine.render();
    }

    std::cout << "\n=== DAMAGE PIPELINE VISUAL ===" << std::endl;
    std::cout << "  Auto-sequence: walk 2s, then 4 hits at 0.75s intervals" << std::endl;
    std::cout << "  Press SPACE to reset and replay" << std::endl;
    std::cout << "  Close window to exit\n" << std::endl;

    // --- Sequence state ---
    int frame = 0;
    float left_leg_hp = 100.0f;
    bool space_was_down = false;
    int run_count = 0;

    auto start_walking = [&]() {
        engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, 0.0f);
        // Set target speed to max_run_speed so clamping by DynamicsParams is visible
        float run_speed = engine.get_humanoid_locomotion().get_max_run_speed(h.hips_id);
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, run_speed);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, true);
    };

    auto do_hit = [&]() {
        if (left_leg_hp <= 0.0f) return;
        left_leg_hp = damage.apply_to_body_part(
            h.entity_id, "left_leg", 25.0f, DamageType::Blunt);
        if (left_leg_hp < 0.0f) left_leg_hp = 0.0f;
        float speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.hips_id);
        std::cout << "  [HIT] left_leg_hp=" << left_leg_hp
                  << " walk_speed=" << speed << std::endl;
    };

    auto reset_sequence = [&]() {
        // Restore leg health in KG
        auto parts = kg.getRelated(h.entity_id, "HAS_PART");
        for (auto part_id : parts) {
            std::string name = kg.getProperty(part_id, "body_part_name");
            if (name == "left_leg") {
                kg.setProperty(part_id, "health", "100.0");
                break;
            }
        }
        left_leg_hp = 100.0f;
        frame = 0;
        run_count++;
        // Re-register entity HP
        damage.unregister_entity(h.entity_id);
        damage.register_entity(h.entity_id, 500.0f);
        float speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.hips_id);
        std::cout << "\n  [RESET #" << run_count << "] walk_speed=" << speed
                  << " left_leg_hp=100" << std::endl;
    };

    float initial_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.hips_id);
    std::cout << "  [START] walk_speed=" << initial_speed << std::endl;
    start_walking();

    while (true) {
        if (headless) {
            if (frame >= SEQUENCE_END) break;
        } else {
            engine.get_platform()->poll_events();
            if (!engine.should_continue()) break;

            // SPACE to reset
            if (win) {
                bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (down && !space_was_down) {
                    reset_sequence();
                    start_walking();
                }
                space_was_down = down;
            }
        }

        // Auto-damage sequence
        if (frame == HIT_1 || frame == HIT_2 || frame == HIT_3 || frame == HIT_4) {
            do_hit();
        }

        // Auto-reset in interactive mode
        if (!headless && frame == SEQUENCE_END) {
            reset_sequence();
            start_walking();
        }

        engine.update(1.0 / 60.0);
        engine.render();
        if (!headless) engine.present();
        frame++;
    }

    float final_speed = engine.get_humanoid_locomotion().get_max_walk_speed(h.hips_id);
    std::cout << "\n  [END] walk_speed=" << final_speed
              << " left_leg_hp=" << left_leg_hp << std::endl;

    if (headless) {
        bool pass = final_speed < initial_speed;
        std::cout << "  " << (pass ? "PASS" : "FAIL")
                  << ": " << initial_speed << " -> " << final_speed << std::endl;
        engine.shutdown();
        return pass ? 0 : 1;
    }

    engine.shutdown();
    return 0;
}
