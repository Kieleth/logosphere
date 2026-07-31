// test_humanoid_lie_down.cpp
// Visual test for humanoid lie down / stand up animation
// SPACE = toggle lying/standing, ESC = exit

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "../src/core/particle_system.h"  // FloorTileConfig
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "logosphere/kg/kg_module.h"
#include <GLFW/glfw3.h>
#include <iostream>

bool test_humanoid_lie_down(TestContext& /* ctx */) {
    std::cout << "\n=== Humanoid Lie Down Test ===" << std::endl;
    std::cout << "SPACE = toggle lying/standing, ESC = exit" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    if (!is_interactive) {
        std::cout << "SKIPPED: Run with INTERACTIVE=1 for visual test" << std::endl;
        return true;
    }

    // Engine init
    Engine engine;
    EngineConfig config;
    config.mode = EngineMode::Interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Humanoid Lie Down Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Sun - close overhead light for good visibility
    Particle sun = {};
    sun.x = 52.0f; sun.y = 48.0f; sun.z = 8.0f;
    sun.size = 1.0f;
    sun.width = 1.0f; sun.height = 1.0f; sun.thickness = 1.0f;
    sun.SetMaterial(Materials::Type::LIGHT);
    sun.is_light_source = true;
    sun.emission_strength = 50000.0f;
    sun.emission_radius = 100.0f;
    sun.r = 1.0f; sun.g = 0.98f; sun.b = 0.95f; sun.a = 1.0f;
    engine.add_particle(sun);

    // Floor tiles (like logomancers/test_shambler_player_fight)
    FloorTileConfig floor_config;
    floor_config.tile_width = 1.0f;
    floor_config.tile_height = 1.0f;
    floor_config.tile_thickness = 0.3f;
    floor_config.r = 0.25f;
    floor_config.g = 0.35f;
    floor_config.b = 0.15f;
    floor_config.color_variance = 0.15f;

    // Create 10x10 floor tiles centered at (50,50)
    auto floor_tile_ids = ps.create_floor_grid(50.0f, 50.0f, 10, 10, floor_config);
    std::cout << "[FLOOR] Created " << floor_tile_ids.size() << " floor tiles centered at (50,50)" << std::endl;

    // Camera - zoomed in close to humanoid
    auto& camera = engine.get_camera_system();
    camera.set_position(47.0f, 47.0f, 8.0f);
    camera.look_at(50.0f, 50.0f, 0.5f);
    camera.set_camera_deadzone(0.0f);

    // Humanoid
    auto& worldgen = engine.get_worldgen_system();
    auto& humanoid_gen = worldgen.get_humanoid_generator();

    PhysicsHumanoidResult hunter_physics = humanoid_gen.generate_humanoid_physics(
        50.0f, 50.0f, 0.5f,
        -1,
        HumanoidSpec::hunter(),
        false
    );

    int hunter_hips_id = hunter_physics.hips_id;
    std::cout << "Hunter at (50,50,0.5): hips_id=" << hunter_hips_id << std::endl;

    // KG Entity
    kg::EntityID hunter_entity = kg.createEntity("Humanoid");
    kg.setProperty(hunter_entity, "name", "The Hunter");
    for (int body_id : hunter_physics.body_ids) {
        kg.createKGParticle(hunter_entity, static_cast<unsigned>(body_id));
    }

    // Set particles to dynamics-owned
    {
        auto particles = ps.lock_particles_for_write();
        for (int body_id : hunter_physics.body_ids) {
            particles[body_id].owner = ParticleOwner::DYNAMICS;
            particles[body_id].friction = 0.0f;
        }
    }

    // Dynamics registration
    engine.get_humanoid_locomotion().register_humanoid_direct(
        hunter_physics.hips_id,
        hunter_physics.left_leg_ids,
        hunter_physics.right_leg_ids,
        hunter_physics.left_arm_ids,
        hunter_physics.right_arm_ids,
        hunter_physics.torso_ids,
        120.0f,
        800.0f
    );

    // Settle physics
    const float DT = 1.0f / 60.0f;
    for (int i = 0; i < 60; i++) {
        engine.update(DT);
    }
    std::cout << "Physics settled" << std::endl;

    // Main loop
    GLFWwindow* window = static_cast<GLFWwindow*>(engine.get_platform()->get_native_window_handle());
    auto& input = engine.get_input_system();
    auto& dynamics = engine.get_dynamics_system();

    bool is_lying = false;
    bool space_was_pressed = false;

    std::cout << "\nReady. Press SPACE to toggle lying/standing." << std::endl;

    while (!glfwWindowShouldClose(window)) {
        engine.get_platform()->poll_events();

        const auto& state = input.get_input_state();

        // ESC to exit
        if (state.keys[GLFW_KEY_ESCAPE]) {
            break;
        }

        // SPACE to toggle
        bool space_pressed = state.keys[GLFW_KEY_SPACE];
        if (space_pressed && !space_was_pressed) {
            is_lying = !is_lying;
            std::cout << (is_lying ? ">>> LYING DOWN" : ">>> STANDING UP") << std::endl;
            engine.get_humanoid_locomotion().set_lying_down(hunter_hips_id, is_lying);

            // Debug: print particle positions after toggle
            {
                auto particles = ps.lock_particles_for_read();
                const auto& hips = particles.get()[hunter_hips_id];
                const auto& head = particles.get()[hunter_physics.head_id];
                std::cout << "  hips: (" << hips.x << ", " << hips.y << ", " << hips.z << ")" << std::endl;
                std::cout << "  head: (" << head.x << ", " << head.y << ", " << head.z << ")" << std::endl;
            }
        }
        space_was_pressed = space_pressed;

        engine.update(DT);
        engine.render();
        engine.present();
    }

    std::cout << "Test complete." << std::endl;
    return true;
}
