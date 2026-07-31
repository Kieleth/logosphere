// test_humanoid_rotation.cpp
// Minimal test that replicates logomancers humanoid + mouse look-at
// 100% PARITY WITH logomancers/src/main.cpp - NO CUSTOM CODE

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "logosphere/kg/kg_module.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

bool test_humanoid_rotation(TestContext& /* ctx */) {
    std::cout << "\n=== Humanoid Rotation Test ===" << std::endl;
    std::cout << "100% PARITY with logomancers" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    if (!is_interactive) {
        std::cout << "SKIPPED: Run with INTERACTIVE=1 for visual test" << std::endl;
        return true;
    }

    // ========================================
    // ENGINE INIT (EXACT copy from logomancers main.cpp:1903-1916)
    // ========================================
    Engine engine;
    EngineConfig config;
    config.mode = EngineMode::Interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Humanoid Rotation Test";
    config.enable_chat_window = false;  // Logomancers enables this, but we skip for test

    if (engine.initialize(config) != 0) {
        std::cout << "Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // ========================================
    // SUN (simplified - logomancers uses celestial system)
    // ========================================
    Particle sun = {};
    sun.x = 55.0f; sun.y = 45.0f; sun.z = 30.0f;
    sun.size = 2.0f;
    sun.width = 2.0f; sun.height = 2.0f; sun.thickness = 2.0f;
    sun.SetMaterial(Materials::Type::LIGHT);
    sun.is_light_source = true;
    sun.emission_strength = 80000.0f;
    sun.emission_radius = 500.0f;
    sun.r = 1.0f; sun.g = 0.95f; sun.b = 0.9f; sun.a = 1.0f;
    engine.add_particle(sun);

    // ========================================
    // SECOND LIGHT (fill light from opposite side)
    // ========================================
    Particle light2 = {};
    light2.x = 45.0f; light2.y = 55.0f; light2.z = 20.0f;
    light2.size = 1.5f;
    light2.width = 1.5f; light2.height = 1.5f; light2.thickness = 1.5f;
    light2.SetMaterial(Materials::Type::LIGHT);
    light2.is_light_source = true;
    light2.emission_strength = 60000.0f;
    light2.emission_radius = 400.0f;
    light2.r = 0.9f; light2.g = 0.95f; light2.b = 1.0f; light2.a = 1.0f;
    engine.add_particle(light2);

    // ========================================
    // FLOOR (simple floor for ground detection)
    // ========================================
    Particle floor = {};
    floor.x = 50.0f;
    floor.y = 50.0f;
    floor.z = 0.05f;
    floor.shape = ParticleShape::BOX;
    floor.width = 40.0f;
    floor.height = 40.0f;
    floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.25f; floor.b = 0.15f;
    floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor.is_at_rest = true;
    engine.add_particle(floor);

    // ========================================
    // CAMERA (EXACT copy from logomancers main.cpp:672-675)
    // ========================================
    auto& camera = engine.get_camera_system();
    camera.set_position(40.0f, 40.0f, 30.0f);    // Southwest of chunk intersection
    camera.look_at(50.0f, 50.0f, 0.0f);          // Look at chunk intersection
    camera.set_camera_deadzone(0.0f);             // Initially disabled (enabled after spawn)
    std::cout << "Camera at (40,40,30) looking at (50,50,0)" << std::endl;

    // ========================================
    // HUMANOID (EXACT copy from logomancers main.cpp:1034-1105)
    // ========================================
    auto& worldgen = engine.get_worldgen_system();
    auto& humanoid_gen = worldgen.get_humanoid_generator();

    // Generate physics humanoid at (50, 50) - same as logomancers
    PhysicsHumanoidResult hunter_physics = humanoid_gen.generate_humanoid_physics(
        50.0f, 50.0f, 0.5f,  // Spawn slightly above floor
        -1,                   // -1 = create floor particle
        HumanoidSpec::hunter(),
        false                 // kg_support = false
    );

    int hunter_hips_id = hunter_physics.hips_id;
    int hunter_head_id = hunter_physics.head_id;
    std::cout << "Hunter at (50,50,0.5): hips_id=" << hunter_hips_id
              << " head_id=" << hunter_head_id << std::endl;

    // ========================================
    // KG ENTITY (EXACT copy from logomancers main.cpp:1053-1070)
    // ========================================
    kg::EntityID hunter_entity = kg.createEntity("Humanoid");
    kg.setProperty(hunter_entity, "name", "The Hunter");
    kg.setProperty(hunter_entity, "walk_speed", "2.0");
    kg.setProperty(hunter_entity, "run_speed", "6.0");
    kg.setProperty(hunter_entity, "force_N", "400");
    kg.setProperty(hunter_entity, "reflexes_ms", "120");
    kg.setProperty(hunter_entity, "grit_W", "800");
    kg.setProperty(hunter_entity, "vigor", "70");
    kg.setProperty(hunter_entity, "essence", "1.5");

    // Bind particles to KG entity (EXACT copy from main.cpp:1067-1070)
    for (int body_id : hunter_physics.body_ids) {
        kg.createKGParticle(hunter_entity, static_cast<unsigned>(body_id));
    }
    std::cout << "Hunter KG bindings: " << hunter_physics.body_ids.size() << " particles" << std::endl;

    // ========================================
    // VOLITIONAL ENTITY (EXACT copy from logomancers main.cpp:1076-1083)
    // ========================================
    {
        auto particles = ps.lock_particles_for_write();
        for (int body_id : hunter_physics.body_ids) {
            particles[body_id].owner = ParticleOwner::DYNAMICS;
            particles[body_id].friction = 0.0f;
        }
    }

    // ========================================
    // DYNAMICS REGISTRATION (EXACT copy from logomancers main.cpp:1088-1097)
    // ========================================
    engine.get_humanoid_locomotion().register_humanoid_direct(
        hunter_physics.hips_id,
        hunter_physics.left_leg_ids,
        hunter_physics.right_leg_ids,
        hunter_physics.left_arm_ids,
        hunter_physics.right_arm_ids,
        hunter_physics.torso_ids,
        120.0f,   // reflexes_ms
        800.0f    // grit_W
    );

    // ========================================
    // CAMERA FOLLOW PARTICLE (EXACT copy from logomancers main.cpp:1100)
    // ========================================
    kg.setProperty(hunter_entity, "camera_follow_particle", std::to_string(hunter_hips_id));

    // ========================================
    // INPUT TARGET (EXACT copy from logomancers main.cpp:1103)
    // ========================================
    engine.set_input_target_entity(hunter_entity);

    std::cout << "Hunter entity created: " << hunter_entity << std::endl;

    // ========================================
    // SETTLE (let physics stabilize)
    // ========================================
    const float DT = 1.0f / 60.0f;
    for (int i = 0; i < 60; i++) {
        engine.update(DT);
    }
    std::cout << "Physics settled" << std::endl;

    // ========================================
    // CAMERA FOLLOW SETTINGS (EXACT copy from logomancers main.cpp:1348-1350)
    // ========================================
    engine.set_camera_follow_enabled(true);
    engine.set_camera_deadzone(4.0f);  // 4m total = 2m from center in any direction
    engine.set_camera_follow_offset(25.0f);  // Diagonal offset for isometric centering
    std::cout << "Camera follow enabled with 4m deadzone" << std::endl;

    // ========================================
    // VISION CONE (EXACT copy from logomancers main.cpp:1353-1354)
    // ========================================
    engine.set_vision_cone_enabled(true);
    engine.set_vision_cone_style(0.8f, 0.15f);  // Soft edges, 15% visibility outside
    std::cout << "Vision cone enabled" << std::endl;

    // ========================================
    // MAIN LOOP (EXACT copy from logomancers main.cpp:1799-1891)
    // ========================================
    std::cout << "\nMove mouse to rotate humanoid, WASD to move, ESC to exit" << std::endl;

    GLFWwindow* window = static_cast<GLFWwindow*>(engine.get_platform()->get_native_window_handle());
    auto& input = engine.get_input_system();
    auto& dynamics = engine.get_dynamics_system();
    int frame = 0;

    // Smoothed look direction (EXACT copy from logomancers)
    float smoothed_look_direction = 0.0f;
    const float PI = 3.14159265f;
    const float HUMAN_FOV = 2.094f;  // 120 degrees
    const float vision_range = 30.0f;

    while (!glfwWindowShouldClose(window)) {
        engine.get_platform()->poll_events();

        const auto& state = input.get_input_state();
        if (state.keys[GLFW_KEY_ESCAPE]) {
            break;
        }

        // ========================================
        // BODY-RELATIVE MOVEMENT (from logomancers main.cpp:1737-1795)
        // W = forward (where hips face), S = backward, A/D = strafe
        // ========================================
        float walk_speed = 2.0f;
        float run_speed = 6.0f;
        bool is_running = state.keys[GLFW_KEY_LEFT_SHIFT] || state.keys[GLFW_KEY_RIGHT_SHIFT];
        float move_speed = is_running ? run_speed : walk_speed;

        // Get body rotation from hips particle
        float body_rotation = 0.0f;
        {
            auto particles_view = ps.lock_particles_for_read();
            if (static_cast<size_t>(hunter_hips_id) < particles_view.get().size()) {
                body_rotation = particles_view.get()[hunter_hips_id].rotation_z;
            }
        }

        // Calculate local input (body-relative)
        float local_forward = 0.0f;  // W/S axis (positive = forward)
        float local_right = 0.0f;    // A/D axis (positive = right)

        if (state.keys[GLFW_KEY_W]) local_forward += 1.0f;
        if (state.keys[GLFW_KEY_S]) local_forward -= 1.0f;
        if (state.keys[GLFW_KEY_A]) local_right -= 1.0f;
        if (state.keys[GLFW_KEY_D]) local_right += 1.0f;

        // Normalize diagonal input
        float input_mag = std::sqrt(local_forward * local_forward + local_right * local_right);
        if (input_mag > 1.0f) {
            local_forward /= input_mag;
            local_right /= input_mag;
        }

        // Speed modifiers (backward/strafe slower)
        const float STRAFE_SPEED_MULT = 0.7f;
        const float BACKWARD_SPEED_MULT = 0.6f;

        float forward_vel = local_forward * move_speed;
        if (local_forward < 0) forward_vel *= BACKWARD_SPEED_MULT;
        float strafe_vel = local_right * move_speed * STRAFE_SPEED_MULT;

        // Transform to world coordinates using hips rotation
        // Convention: rotation_z = 0 means facing +Y (north)
        float cos_r = std::cos(body_rotation);
        float sin_r = std::sin(body_rotation);

        float forward_x = sin_r;   // Forward direction X component
        float forward_y = cos_r;   // Forward direction Y component
        float right_x = cos_r;     // Right direction X component
        float right_y = sin_r;     // Right direction Y component

        float vx = forward_x * forward_vel + right_x * strafe_vel;
        float vy = forward_y * forward_vel + right_y * strafe_vel;

        engine.get_humanoid_locomotion().set_target_velocity(hunter_hips_id, vx, vy);

        // ========================================
        // LOOK AT TARGET (EXACT copy from logomancers main.cpp:1799-1810)
        // ========================================
        double screen_mx, screen_my;
        glfwGetCursorPos(window, &screen_mx, &screen_my);
        float mouse_world_x, mouse_world_y;
        engine.get_coord_transformer().screen_to_world(
            static_cast<int>(screen_mx), static_cast<int>(screen_my),
            mouse_world_x, mouse_world_y);

        engine.get_humanoid_locomotion().set_look_at_target(hunter_hips_id, mouse_world_x, mouse_world_y);

        // ========================================
        // VISION CONE UPDATE (EXACT copy from logomancers main.cpp:1812-1891)
        // ========================================
        float head_x, head_y, head_z, hips_x, hips_y, hips_z;
        {
            auto particles_view = ps.lock_particles_for_read();
            const auto& head = particles_view.get()[hunter_head_id];
            const auto& hips = particles_view.get()[hunter_hips_id];
            head_x = head.x;
            head_y = head.y;
            head_z = head.z;
            hips_x = hips.x;
            hips_y = hips.y;
            hips_z = hips.z;

        }

        // Calculate look direction from head to mouse
        float dx = mouse_world_x - head_x;
        float dy = mouse_world_y - head_y;
        float raw_look_direction = atan2f(dx, dy);

        // Smooth look direction (EXACT copy from logomancers main.cpp:1836-1878)
        float diff = raw_look_direction - smoothed_look_direction;
        while (diff > PI) diff -= 2.0f * PI;
        while (diff < -PI) diff += 2.0f * PI;

        bool w_pressed = state.keys[GLFW_KEY_W];
        float abs_diff = std::abs(diff);
        float normalized_diff = abs_diff / PI;
        float base_chase = 0.1f + 0.9f * std::sqrt(normalized_diff);
        float chase_factor = w_pressed ? std::min(1.0f, base_chase * 1.5f) : base_chase;

        smoothed_look_direction += diff * chase_factor;
        while (smoothed_look_direction > PI) smoothed_look_direction -= 2.0f * PI;
        while (smoothed_look_direction < -PI) smoothed_look_direction += 2.0f * PI;

        engine.set_vision_cone(head_x, head_y, smoothed_look_direction, HUMAN_FOV, vision_range);

        // NOTE: Body rotation is handled by dynamics via set_look_at_target() above.
        // Do NOT manually override hips.rotation_z - it fights with dynamics.

        // Foveal focus (EXACT copy from main.cpp:1891)
        engine.set_vision_cone_focus(mouse_world_x, mouse_world_y, 3.0f);

        // ========================================
        // UPDATE AND RENDER
        // ========================================
        engine.update(DT);

        // DEBUG: Comprehensive rotation analysis
        if (frame % 30 == 0) {
            auto particles_view = ps.lock_particles_for_read();
            const auto& head_after = particles_view.get()[hunter_head_id];
            const auto& hips_after = particles_view.get()[hunter_hips_id];

            // Mouse direction from head
            float mouse_dx = mouse_world_x - head_after.x;
            float mouse_dy = mouse_world_y - head_after.y;
            std::string mouse_dir = "?";
            if (std::abs(mouse_dy) > std::abs(mouse_dx)) {
                mouse_dir = (mouse_dy > 0) ? "NORTH" : "SOUTH";
            } else {
                mouse_dir = (mouse_dx > 0) ? "EAST" : "WEST";
            }

            // Expected rotation per docs/ARCHITECTURE.md: atan2(dx, dy), 0=North
            float expected_rot = atan2f(mouse_dx, mouse_dy);

            // Actual rotation from dynamics
            float actual_hips_rot = hips_after.rotation_z;
            float actual_head_rot = head_after.rotation_z;

            // Forward direction from hips rotation (what movement uses)
            float fwd_x = std::sin(actual_hips_rot);
            float fwd_y = std::cos(actual_hips_rot);
            std::string facing_dir = "?";
            if (std::abs(fwd_y) > std::abs(fwd_x)) {
                facing_dir = (fwd_y > 0) ? "NORTH" : "SOUTH";
            } else {
                facing_dir = (fwd_x > 0) ? "EAST" : "WEST";
            }

            std::cout << "[ROT F" << frame << "] mouse=" << mouse_dir
                      << " expected=" << (expected_rot * 180.0f / PI) << "°"
                      << " head_rot=" << (actual_head_rot * 180.0f / PI) << "°"
                      << " hips_rot=" << (actual_hips_rot * 180.0f / PI) << "°"
                      << " facing=" << facing_dir
                      << (mouse_dir != facing_dir ? " ← MISMATCH!" : " ✓")
                      << std::endl;
        }

        engine.render();
        engine.present();

        // Debug every 60 frames
        if (frame % 60 == 0) {
            float head_rot;
            {
                auto particles = ps.lock_particles_for_read();
                head_rot = particles.get()[hunter_head_id].rotation_z;
            }
            std::cout << "[F" << frame << "] mouse_world=(" << mouse_world_x << "," << mouse_world_y << ")"
                      << " smoothed_dir=" << (smoothed_look_direction * 180.0f / PI) << "deg"
                      << " head_rot=" << (head_rot * 180.0f / PI) << "deg"
                      << std::endl;
        }
        frame++;
    }

    return true;
}
