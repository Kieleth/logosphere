// test_humanoid_ground_multitile.cpp
// Isolated test for humanoid ground support with MULTIPLE floor tiles
// This mimics the Logomancers setup where the floor is made of many small tiles

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
#include <vector>

// ============================================================================
// HUMANOID GROUND SUPPORT TEST - MULTI-TILE VERSION
// ============================================================================
// Test: Full humanoid on MULTIPLE floor tiles with WASD movement.
// This tests the realistic case where humanoid walks across tile boundaries.
// Humanoid should stay on ground, not sink between tiles.
//
// Run: ./logosphere-tests --test test_humanoid_ground_multitile
// Interactive: INTERACTIVE=1 ./logosphere-tests --test test_humanoid_ground_multitile
//              Use WASD to move humanoid, ESC to exit
// ============================================================================

bool test_humanoid_ground_multitile(TestContext& /* ctx */) {
    std::cout << "\n=== Humanoid Ground Support Test (Multi-Tile) ===" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Humanoid Multi-Tile Test - WASD to move, ESC to exit";
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
    // CREATE FLOOR TILES (grid of small tiles)
    // ========================================
    std::cout << "\n--- Creating Floor Tiles ---" << std::endl;

    const float TILE_SIZE = 2.0f;      // Each tile is 2m x 2m (like Logomancers)
    const float TILE_THICKNESS = 0.1f;
    const int GRID_SIZE = 9;           // 9x9 grid = 81 tiles covering 18m x 18m
    const float GRID_OFFSET = -((GRID_SIZE - 1) * TILE_SIZE) / 2.0f;  // Center the grid

    std::vector<int> tile_ids;
    int tiles_created = 0;

    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            Particle tile = {};
            tile.x = GRID_OFFSET + col * TILE_SIZE;
            tile.y = GRID_OFFSET + row * TILE_SIZE;
            tile.z = TILE_THICKNESS / 2.0f;  // Center at 0.05, bottom at 0 (on turtle)
            tile.shape = ParticleShape::BOX;
            tile.width = TILE_SIZE;
            tile.height = TILE_SIZE;
            tile.thickness = TILE_THICKNESS;

            // Alternate colors for checkerboard pattern
            if ((row + col) % 2 == 0) {
                tile.r = 0.5f; tile.g = 0.45f; tile.b = 0.35f;
            } else {
                tile.r = 0.4f; tile.g = 0.35f; tile.b = 0.25f;
            }
            tile.a = 1.0f;

            tile.SetMaterial(Materials::Type::HEAVY_STATIC);
            tile.is_at_rest = true;  // Like create_floor_grid - immovable floor
            int tile_id = engine.add_particle(tile);
            tile_ids.push_back(tile_id);
            tiles_created++;
        }
    }

    std::cout << "Created " << tiles_created << " floor tiles (" << GRID_SIZE << "x" << GRID_SIZE << " grid)" << std::endl;
    std::cout << "Tile size: " << TILE_SIZE << "m x " << TILE_SIZE << "m" << std::endl;
    std::cout << "Floor coverage: " << (GRID_SIZE * TILE_SIZE) << "m x " << (GRID_SIZE * TILE_SIZE) << "m" << std::endl;

    // Use center tile as reference for humanoid generator
    int center_tile_id = tile_ids[(GRID_SIZE * GRID_SIZE) / 2];

    // ========================================
    // CREATE HUMANOID using HumanoidGenerator
    // ========================================
    std::cout << "\n--- Creating Humanoid ---" << std::endl;

    HumanoidGenerator gen;
    gen.initialize(&engine, nullptr);

    PhysicsHumanoidResult humanoid = gen.generate_humanoid_physics(
        0.0f, 0.0f, TILE_THICKNESS,  // Standing ON the tiles (INV-37: world_z is the feet's bottom)
        center_tile_id,         // Use center tile as platform reference
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

    // Debug: Check positions after settling
    {
        auto particles = ps.lock_particles_for_read();

        // Check a few tiles
        std::cout << "\n--- Sample Tile Positions After Settling ---" << std::endl;
        for (int i = 0; i < 3 && i < (int)tile_ids.size(); i++) {
            const auto& t = particles[tile_ids[i]];
            std::cout << "  Tile " << i << " (id=" << tile_ids[i] << "): z=" << t.z << std::endl;
        }

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
        std::cout << "WASD to move humanoid across tile boundaries, ESC to exit" << std::endl;

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
                float max_tile_z = 0.0f;
                int tiles_moved = 0;
                {
                    auto particles = ps.lock_particles_for_read();
                    hips_x = particles[humanoid.hips_id].x;
                    hips_y = particles[humanoid.hips_id].y;
                    hips_z = particles[humanoid.hips_id].z;

                    // Check tile positions
                    for (int tile_id : tile_ids) {
                        float tz = particles[tile_id].z;
                        if (tz > max_tile_z) max_tile_z = tz;
                        if (std::abs(tz - 0.05f) > 0.01f) tiles_moved++;
                    }
                }
                std::cout << "Frame " << frame << ": hips=(" << hips_x << "," << hips_y << "," << hips_z << ")";
                if (tiles_moved > 0) {
                    std::cout << " TILES MOVED: " << tiles_moved << " max_z=" << max_tile_z;
                }
                std::cout << std::endl;
            }
            frame++;
        }
    } else {
        // Headless mode - simulate walking forward across multiple tiles
        std::cout << "\n--- Simulating Walking Across Tiles (3 seconds) ---" << std::endl;

        // Set walking velocity
        engine.get_humanoid_locomotion().set_target_velocity(humanoid.hips_id, 0.0f, MOVE_SPEED);
        engine.get_humanoid_locomotion().set_volitional(humanoid.hips_id, true);

        // Walk for 3 seconds (180 frames) to cross multiple tiles
        for (int frame = 0; frame < 180; frame++) {
            engine.update(DT);

            if (frame % 20 == 0) {
                float hips_y, hips_z;
                float left_foot_x, left_foot_z;
                float right_foot_x, right_foot_z;
                {
                    auto particles = ps.lock_particles_for_read();
                    hips_y = particles[humanoid.hips_id].y;
                    hips_z = particles[humanoid.hips_id].z;
                    left_foot_x = particles[humanoid.left_leg_ids[0]].x;
                    left_foot_z = particles[humanoid.left_leg_ids[0]].z;
                    right_foot_x = particles[humanoid.right_leg_ids[0]].x;
                    right_foot_z = particles[humanoid.right_leg_ids[0]].z;
                }
                // Show positions - humanoid should walk across multiple tiles
                std::cout << "Frame " << frame
                          << ": hips_y=" << hips_y << " hips_z=" << hips_z
                          << " L_foot=(" << left_foot_x << "," << left_foot_z << ")"
                          << " R_foot=(" << right_foot_x << "," << right_foot_z << ")"
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

    float final_hips_y, final_hips_z;
    float final_left_foot_x, final_right_foot_x;
    {
        auto particles = ps.lock_particles_for_read();
        final_hips_y = particles[humanoid.hips_id].y;
        final_hips_z = particles[humanoid.hips_id].z;
        final_left_foot_x = particles[humanoid.left_leg_ids[0]].x;
        final_right_foot_x = particles[humanoid.right_leg_ids[0]].x;
    }

    std::cout << "Hips: y=" << final_hips_y << " z=" << final_hips_z << std::endl;
    std::cout << "Feet x: L=" << final_left_foot_x << " R=" << final_right_foot_x << std::endl;

    // Calculate tiles crossed
    float distance_walked = final_hips_y;  // Started at y=0
    int tiles_crossed = static_cast<int>(distance_walked / TILE_SIZE);
    std::cout << "Distance walked: " << distance_walked << "m (" << tiles_crossed << " tiles crossed)" << std::endl;

    // Success criteria:
    // 1. Hips stayed above ground (didn't sink)
    // 2. Feet didn't spread too far apart (lateral stability)
    bool hips_stable = (final_hips_z > 0.3f);
    float foot_spread = std::abs(final_right_foot_x - final_left_foot_x);
    bool feet_stable = (foot_spread < 0.5f);  // Feet shouldn't be more than 0.5m apart

    bool success = hips_stable && feet_stable;

    if (success) {
        std::cout << "\n SUCCESS: Humanoid is stable across multiple tiles" << std::endl;
    } else {
        std::cout << "\n FAILED: ";
        if (!hips_stable) std::cout << "Humanoid sank (hips.z=" << final_hips_z << ") ";
        if (!feet_stable) std::cout << "Feet spread too far (spread=" << foot_spread << "m)";
        std::cout << std::endl;
    }

    return success;
}
