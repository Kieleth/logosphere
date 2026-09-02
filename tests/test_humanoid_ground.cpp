// test_humanoid_ground.cpp
// Isolated test for humanoid ground support

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

// ============================================================================
// HUMANOID GROUND SUPPORT TEST
// ============================================================================
// Test: Full humanoid on floor tiles with WASD movement.
// Humanoid should stay on ground, not sink.
//
// Run: ./logosphere-tests --test test_humanoid_ground
// Interactive: INTERACTIVE=1 ./logosphere-tests --test test_humanoid_ground
//              Use WASD to move humanoid, ESC to exit
// ============================================================================

bool test_humanoid_ground(TestContext& /* ctx */) {
    std::cout << "\n=== Humanoid Ground Support Test ===" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Humanoid Ground Test - WASD to move, ESC to exit";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // ========================================
    // CREATE SUN (light source)
    // ========================================
    Particle sun = {};
    sun.x = 5.0f; sun.y = -5.0f; sun.z = 10.0f;
    sun.size = 2.0f;
    sun.SetMaterial(Materials::Type::LIGHT);
    sun.is_light_source = true;
    sun.emission_strength = 80000.0f;
    sun.emission_radius = 500.0f;
    sun.r = 1.0f; sun.g = 0.95f; sun.b = 0.9f;
    engine.add_particle(sun);

    // ========================================
    // CREATE FLOOR (large single tile)
    // ========================================
    std::cout << "\n--- Creating Floor ---" << std::endl;

    Particle floor = {};
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.thickness = 0.1f;
    floor.z = 0.05f;  // Center at 0.05, bottom at 0 (on turtle)
    floor.shape = ParticleShape::BOX;
    floor.width = 20.0f;
    floor.height = 20.0f;
    floor.r = 0.5f; floor.g = 0.45f; floor.b = 0.35f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // High mass = effectively immovable
    int floor_id = engine.add_particle(floor);

    float floor_top = floor.z + floor.thickness / 2.0f;
    std::cout << "Floor top at z=" << floor_top << " (id=" << floor_id << ")" << std::endl;

    // Verify floor position and mass after add
    {
        auto particles = ps.lock_particles_for_read();
        const auto& f = particles[floor_id];
        std::cout << "Floor particle AFTER add: z=" << f.z
                  << " owner=" << static_cast<int>(f.owner)
                  << " mass=" << f.GetMass() << " kg"
                  << " inv_mass=" << (1.0f / f.GetMass())
                  << " density=" << f.material_density << std::endl;
    }

    // ========================================
    // CREATE HUMANOID using HumanoidGenerator
    // ========================================
    std::cout << "\n--- Creating Humanoid ---" << std::endl;

    HumanoidGenerator gen;
    gen.initialize(&engine, nullptr);

    PhysicsHumanoidResult humanoid = gen.generate_humanoid_physics(
        0.0f, 0.0f, floor_top,  // Standing ON the floor (INV-37: world_z is the feet's bottom)
        floor_id,               // Use our floor as platform (no extra platform created)
        HumanoidSpec::eva(),    // Use Eva spec
        false                   // No KG support needed
    );

    std::cout << "Humanoid created with " << humanoid.body_ids.size() << " body particles" << std::endl;
    std::cout << "Hips ID: " << humanoid.hips_id << std::endl;

    // Dump INITIAL positions and flags (before settling)
    {
        auto particles = ps.lock_particles_for_read();
        std::cout << "\n--- INITIAL Positions (before settling) ---" << std::endl;
        const char* names[] = {"L_Foot", "L_Shin", "L_Thigh", "R_Foot", "R_Shin", "R_Thigh", "Hips"};
        for (size_t i = 0; i < 7 && i < humanoid.body_ids.size(); i++) {
            int id = humanoid.body_ids[i];
            const auto& p = particles[id];
            std::cout << "  " << names[i] << " (id=" << id << "): pos=(" << p.x << "," << p.y << "," << p.z << ")"
                      << " owner=" << static_cast<int>(p.owner)
                      << " at_rest=" << p.is_at_rest
                      << " friction=" << p.friction
                      << " mass=" << p.GetMass() << "kg" << std::endl;
        }
    }

    // ========================================
    // LET PHYSICS SETTLE
    // ========================================
    std::cout << "\n--- Settling (1 second) ---" << std::endl;
    const float DT = 1.0f / 60.0f;
    for (int i = 0; i < 60; i++) {
        // Debug: Track foot spreading every 5 frames
        if (i % 5 == 0) {
            auto particles = ps.lock_particles_for_read();
            const auto& lf = particles[humanoid.left_leg_ids[0]];   // L_Foot
            const auto& ls = particles[humanoid.left_leg_ids[1]];   // L_Shin
            std::cout << "Frame " << i << ": L_Foot x=" << lf.x << " z=" << lf.z
                      << " rot=" << lf.rotation_z << " vx=" << lf.vx
                      << " | L_Shin x=" << ls.x << " rot=" << ls.rotation_z << std::endl;
        }
        engine.update(DT);
    }
    std::cout << "Settling complete" << std::endl;

    // Debug: Check floor position after settling
    {
        auto particles = ps.lock_particles_for_read();
        std::cout << "Floor particle AFTER settling: z=" << particles[floor_id].z
                  << " owner=" << static_cast<int>(particles[floor_id].owner) << std::endl;

        // Dump ALL humanoid particle positions
        std::cout << "\n--- Humanoid Particle Positions ---" << std::endl;
        const char* part_names[] = {"L_Foot", "L_Shin", "L_Thigh", "R_Foot", "R_Shin", "R_Thigh",
                                     "Hips", "Abdomen", "Chest", "Neck", "Head", "UpperHair",
                                     "BackHair", "L_Ear", "R_Ear", "L_Shoulder", "R_Shoulder",
                                     "L_UpperArm", "L_Forearm", "L_Hand", "R_UpperArm", "R_Forearm", "R_Hand"};
        for (size_t i = 0; i < humanoid.body_ids.size() && i < 23; i++) {
            int id = humanoid.body_ids[i];
            const auto& p = particles[id];
            std::cout << "  " << part_names[i] << " (id=" << id << "): pos=("
                      << p.x << "," << p.y << "," << p.z << ") size=("
                      << p.width << "x" << p.height << "x" << p.thickness << ")" << std::endl;
        }
    }

    // ========================================
    // REGISTER WITH DYNAMICS
    // ========================================
    std::cout << "\n--- Registering with Dynamics ---" << std::endl;

    engine.get_humanoid_locomotion().register_humanoid_direct(
        humanoid.hips_id,
        humanoid.left_leg_ids,
        humanoid.right_leg_ids,
        humanoid.left_arm_ids,
        humanoid.right_arm_ids,
        humanoid.torso_ids
    );

    std::cout << "Humanoid registered with dynamics" << std::endl;

    // Disable friction for volitional movement
    {
        auto particles = ps.lock_particles_for_write();
        for (int body_id : humanoid.body_ids) {
            particles[body_id].friction = 0.0f;
        }
    }

    // ========================================
    // POSITION CAMERA
    // ========================================
    auto& camera = engine.get_camera_system();
    camera.look_at(0.0f, 0.0f, 0.5f);

    // ========================================
    // SIMULATE / INTERACTIVE LOOP
    // ========================================
    const float MOVE_SPEED = 2.0f;

    if (is_interactive) {
        std::cout << "\n--- Interactive Mode ---" << std::endl;
        std::cout << "WASD to move humanoid, ESC to exit" << std::endl;

        auto& input = engine.get_input_system();
        bool running = true;
        int frame = 0;

        while (running) {
            engine.get_platform()->poll_events();
            const auto& state = input.get_input_state();

            if (state.keys[GLFW_KEY_ESCAPE]) {
                running = false;
                break;
            }

            // WASD movement
            float vx = 0.0f, vy = 0.0f;
            if (state.keys[GLFW_KEY_W]) vy += MOVE_SPEED;
            if (state.keys[GLFW_KEY_S]) vy -= MOVE_SPEED;
            if (state.keys[GLFW_KEY_A]) vx -= MOVE_SPEED;
            if (state.keys[GLFW_KEY_D]) vx += MOVE_SPEED;

            bool is_moving = (vx != 0.0f || vy != 0.0f);
            engine.get_humanoid_locomotion().set_target_velocity(humanoid.hips_id, vx, vy);
            engine.get_humanoid_locomotion().set_volitional(humanoid.hips_id, is_moving);

            engine.update(DT);
            engine.render();
            engine.present();

            // Log every 10 frames for first 100, then every 60
            if (frame < 100 ? (frame % 10 == 0) : (frame % 60 == 0)) {
                float hips_x, hips_y, hips_z;
                float floor_x, floor_y, floor_z, floor_vz;
                {
                    auto particles = ps.lock_particles_for_read();
                    hips_x = particles[humanoid.hips_id].x;
                    hips_y = particles[humanoid.hips_id].y;
                    hips_z = particles[humanoid.hips_id].z;
                    floor_x = particles[floor_id].x;
                    floor_y = particles[floor_id].y;
                    floor_z = particles[floor_id].z;
                    floor_vz = particles[floor_id].vz;
                }
                std::cout << "Frame " << frame << ": hips=(" << hips_x << "," << hips_y << "," << hips_z << ")"
                          << " floor=(" << floor_x << "," << floor_y << "," << floor_z << ") vz=" << floor_vz << std::endl;
            }
            frame++;
        }
    } else {
        // Headless mode - simulate walking forward for 2 seconds
        std::cout << "\n--- Simulating Walking (2 seconds) ---" << std::endl;

        // Set walking velocity
        engine.get_humanoid_locomotion().set_target_velocity(humanoid.hips_id, 0.0f, MOVE_SPEED);
        engine.get_humanoid_locomotion().set_volitional(humanoid.hips_id, true);

        for (int frame = 0; frame < 120; frame++) {
            engine.update(DT);

            if (frame % 10 == 0) {
                float hips_y, hips_z;
                float left_foot_z, right_foot_z;
                float floor_z;
                {
                    auto particles = ps.lock_particles_for_read();
                    hips_y = particles[humanoid.hips_id].y;
                    hips_z = particles[humanoid.hips_id].z;
                    left_foot_z = particles[humanoid.left_leg_ids.back()].z;
                    right_foot_z = particles[humanoid.right_leg_ids.back()].z;
                    floor_z = particles[floor_id].z;
                }
                // Show z positions to detect sinking/jumping
                std::cout << "Frame " << frame
                          << ": hips_z=" << hips_z
                          << " feet_z=[" << left_foot_z << "," << right_foot_z << "]"
                          << " floor_z=" << floor_z
                          << " hips_y=" << hips_y
                          << std::endl;
            }
        }

        // Stop and check final position
        engine.get_humanoid_locomotion().set_target_velocity(humanoid.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(humanoid.hips_id, false);
    }

    // ========================================
    // FINAL CHECK
    // ========================================
    std::cout << "\n--- Final State ---" << std::endl;

    float final_hips_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_hips_z = particles[humanoid.hips_id].z;
    }

    std::cout << "Hips z: " << final_hips_z << std::endl;

    // Success if hips stayed above ground
    bool success = (final_hips_z > 0.3f);

    if (success) {
        std::cout << "\n SUCCESS: Humanoid is stable" << std::endl;
    } else {
        std::cout << "\n FAILED: Humanoid sank (hips.z=" << final_hips_z << ")" << std::endl;
    }

    return success;
}
