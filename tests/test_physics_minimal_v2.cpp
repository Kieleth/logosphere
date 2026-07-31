// ============================================================================
// MINIMAL PHYSICS TEST V2: Progressive complexity
// ============================================================================
// Purpose: Test physics settling from baseline (tiles only) to complex (+ objects)
//
// Run:
//   PHASE=1 ./logosphere-tests --test test_physics_minimal_v2  # 16x16x16 tiles (baseline)
//   PHASE=2 ./logosphere-tests --test test_physics_minimal_v2  # + boulder
//   PHASE=3 ./logosphere-tests --test test_physics_minimal_v2  # + tree
//   INTERACTIVE=1 ./logosphere-tests --test test_physics_minimal_v2  # Visual
//
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <map>
#include <string>

using PhysicsV4::TURTLE_Z;

bool test_physics_minimal_v2() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  MINIMAL PHYSICS V2: Progressive Complexity                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Interactive mode
    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    // Engine setup
    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Minimal V2";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    std::cout << "[SETUP] TURTLE_Z = " << TURTLE_Z << "m\n";
    std::cout << "[SETUP] Gravity = -9.8 m/s²\n\n";

    // Tile parameters (16x16x16 = 4096 tiles)
    const int GRID_SIZE = 16;
    const float tile_size = 0.5f;
    const float tile_thickness = 0.1f;
    const float tile_r = 0.4f, tile_g = 0.35f, tile_b = 0.25f;

    std::vector<int> tile_pids;
    std::vector<int> object_pids;  // Boulder or tree particles
    int light_id = -1;

    // Phase selection
    const int MAX_PHASE = 3;
    int test_phase = 1;
    if (!is_interactive) {
        const char* phase_env = std::getenv("PHASE");
        test_phase = phase_env ? std::atoi(phase_env) : 1;
        if (test_phase < 1 || test_phase > MAX_PHASE) test_phase = 1;
    }

    // Initialize generators
    PhysicsTreeGenerator tree_generator;
    tree_generator.initialize(&engine);

    PhysicsRockGenerator rock_generator;
    rock_generator.initialize(&engine);

    // Lambda to create scene for a given phase
    auto create_scene = [&](int phase) {
        // Clear existing
        ps.clear_particles();
        physics.clear_gluons();
        tile_pids.clear();
        object_pids.clear();

        // Add light first
        Particle light = {};
        light.x = 0.0f;
        light.y = -10.0f;
        light.z = 15.0f;
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
        light_id = engine.add_particle(light);

        // Create 16x16x16 tile cube (same for all phases)
        float tile_z = tile_thickness / 2.0f;
        float offset = (GRID_SIZE - 1) * tile_size * 0.5f;

        for (int layer = 0; layer < GRID_SIZE; ++layer) {
            for (int row = 0; row < GRID_SIZE; ++row) {
                for (int col = 0; col < GRID_SIZE; ++col) {
                    Particle tile = {};
                    tile.x = col * tile_size - offset;
                    tile.y = row * tile_size - offset;
                    tile.z = tile_z + layer * tile_thickness;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Color gradient
                    tile.r = 0.2f + (float)col / GRID_SIZE * 0.4f;
                    tile.g = 0.2f + (float)row / GRID_SIZE * 0.4f;
                    tile.b = 0.2f + (float)layer / GRID_SIZE * 0.4f;
                    tile.friction = 0.5f;
                    tile_pids.push_back(engine.add_particle(tile));
                }
            }
        }

        std::string phase_desc;
        if (phase == 1) {
            phase_desc = "PHASE 1: 16x16x16 tiles (baseline)";
            std::cout << "\n[" << phase_desc << "]\n";
        } else if (phase == 2) {
            phase_desc = "PHASE 2: 16x16x16 + BOULDER";
            std::cout << "\n[" << phase_desc << "]\n";

            // Place boulder on top center of cube
            float boulder_x = 0.0f;
            float boulder_y = 0.0f;
            float boulder_z = tile_z + GRID_SIZE * tile_thickness + 0.3f;  // On top + gap

            PhysicsRockSpec spec = PhysicsRockSpec::medium_rock();
            PhysicsRockResult result = rock_generator.generate_rock(
                boulder_x, boulder_y, boulder_z, spec);

            for (int pid : result.box_ids) {
                object_pids.push_back(pid);
            }
            std::cout << "  Boulder: " << result.box_ids.size() << " boxes\n";
        } else if (phase == 3) {
            phase_desc = "PHASE 3: 16x16x16 + TREE";
            std::cout << "\n[" << phase_desc << "]\n";

            // Place tree on top center of cube
            float tree_x = 0.0f;
            float tree_y = 0.0f;
            float tree_z = tile_z + GRID_SIZE * tile_thickness;  // On top

            TreeSpec spec = TreeSpec::sapling();  // Small tree for testing

            PhysicsTreeResult result = tree_generator.generate_tree(
                tree_x, tree_y, tree_z, spec);

            object_pids.push_back(result.trunk_id);
            for (int pid : result.branch_ids) {
                object_pids.push_back(pid);
            }
            std::cout << "  Tree: " << result.total_segments << " segments\n";
        }

        std::cout << "  Total tiles: " << tile_pids.size() << "\n";
        std::cout << "  Object particles: " << object_pids.size() << "\n";
    };

    // Create initial scene
    create_scene(test_phase);

    // Run physics
    const float dt = 1.0f / 60.0f;
    const int max_frames = is_interactive ? 36000 : 1800;  // 10 min or 30s timeout

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  VELOCITY TRACKING                                           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "Frame | MaxVel  | MinVel  | AvgVel  | AtRest\n";
    std::cout << "------+---------+---------+---------+---------\n";

    bool should_quit = false;
    bool space_was_pressed = false;
    bool all_at_rest = false;
    int final_frame = 0;

    for (int frame = 0; frame < max_frames && !should_quit && !all_at_rest; ++frame) {
        engine.update(dt);
        final_frame = frame;

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();

                // Stats for tiles
                float max_vel = 0.0f;
                int at_rest_count = 0;
                for (int pid : tile_pids) {
                    const Particle& p = particles[pid];
                    float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    max_vel = std::max(max_vel, vel);
                    if (p.is_at_rest) at_rest_count++;
                }

                // Stats for objects
                int obj_at_rest = 0;
                float obj_max_vel = 0.0f;
                for (int pid : object_pids) {
                    const Particle& p = particles[pid];
                    float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    obj_max_vel = std::max(obj_max_vel, vel);
                    if (p.is_at_rest) obj_at_rest++;
                }

                std::string phase_str;
                switch (test_phase) {
                    case 1: phase_str = "PHASE 1: 16x16x16 (baseline)"; break;
                    case 2: phase_str = "PHASE 2: + Boulder"; break;
                    case 3: phase_str = "PHASE 3: + Tree"; break;
                    default: phase_str = "PHASE ?"; break;
                }
                ui->draw_text(10, 10, phase_str, 255, 200, 100);
                ui->draw_text(10, 40, "Tiles: " + std::to_string(tile_pids.size()), 200, 200, 200);
                ui->draw_text(10, 70, "Objects: " + std::to_string(object_pids.size()), 200, 200, 200);
                ui->draw_text(10, 100, "Frame: " + std::to_string(frame), 200, 200, 200);
                ui->draw_text(10, 130, "Tile max vel: " + std::to_string(max_vel).substr(0, 8) + " m/s", 200, 200, 200);
                ui->draw_text(10, 160, "Obj max vel: " + std::to_string(obj_max_vel).substr(0, 8) + " m/s", 200, 200, 200);

                std::string tile_rest = std::to_string(at_rest_count) + "/" + std::to_string(tile_pids.size()) + " tiles at rest";
                std::string obj_rest = std::to_string(obj_at_rest) + "/" + std::to_string(object_pids.size()) + " objects at rest";

                int total_rest = at_rest_count + obj_at_rest;
                int total_parts = tile_pids.size() + object_pids.size();
                bool all_at_rest = (total_rest == total_parts);

                if (all_at_rest) {
                    ui->draw_text(10, 200, tile_rest, 100, 255, 100);
                    ui->draw_text(10, 230, obj_rest, 100, 255, 100);
                } else {
                    ui->draw_text(10, 200, tile_rest, 255, 100, 100);
                    ui->draw_text(10, 230, obj_rest, 255, 100, 100);
                }

                ui->draw_text(10, 280, "SPACE: switch phase | ESC: exit", 128, 128, 128);
            }

            engine.present();

            // Input handling
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\n  ESC pressed - exiting" << std::endl;
                should_quit = true;
            }

            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed) {
                test_phase = (test_phase % MAX_PHASE) + 1;
                create_scene(test_phase);
                frame = 0;
            }
            space_was_pressed = space_pressed;
        }

        // Check rest state and log in headless mode
        if (!is_interactive) {
            float max_vel = 0.0f, min_vel = 1000.0f, sum_vel = 0.0f;
            int at_rest_count = 0;
            int total_count = 0;

            auto particles = ps.lock_particles_for_read();

            // Include tiles
            for (int pid : tile_pids) {
                const Particle& p = particles[pid];
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                max_vel = std::max(max_vel, vel);
                min_vel = std::min(min_vel, vel);
                sum_vel += vel;
                if (p.is_at_rest) at_rest_count++;
                total_count++;
            }

            // Include objects
            for (int pid : object_pids) {
                const Particle& p = particles[pid];
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                max_vel = std::max(max_vel, vel);
                min_vel = std::min(min_vel, vel);
                sum_vel += vel;
                if (p.is_at_rest) at_rest_count++;
                total_count++;
            }

            // Check if all at rest - exit early
            if (at_rest_count == total_count) {
                all_at_rest = true;
            }

            // Log every 30 frames (0.5 second)
            if (frame % 30 == 0 || frame < 10 || all_at_rest) {
                float avg_vel = sum_vel / total_count;
                std::cout << std::setw(5) << frame << " | "
                          << std::setw(7) << std::fixed << std::setprecision(4) << max_vel << " | "
                          << std::setw(7) << min_vel << " | "
                          << std::setw(7) << avg_vel << " | "
                          << at_rest_count << "/" << total_count << "\n";
            }
        }
    }

    // Final state
    float elapsed_seconds = final_frame / 60.0f;
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FINAL STATE (frame " << final_frame << " / " << std::fixed << std::setprecision(1) << elapsed_seconds << "s)\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    int total_at_rest = 0;
    int total_particles = tile_pids.size() + object_pids.size();
    float max_final_vel = 0.0f;

    {
        auto particles = ps.lock_particles_for_read();

        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            max_final_vel = std::max(max_final_vel, vel);
            if (p.is_at_rest) total_at_rest++;
        }

        for (int pid : object_pids) {
            const Particle& p = particles[pid];
            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            max_final_vel = std::max(max_final_vel, vel);
            if (p.is_at_rest) total_at_rest++;
        }
    }

    int not_at_rest = total_particles - total_at_rest;

    std::cout << "Tiles: " << tile_pids.size() << "\n";
    std::cout << "Objects: " << object_pids.size() << "\n";
    std::cout << "At rest: " << total_at_rest << "/" << total_particles << "\n";
    std::cout << "NOT at rest: " << not_at_rest << "\n";
    std::cout << "Max velocity: " << max_final_vel << " m/s\n\n";

    // Diagnostic: Show particles not at rest
    if (not_at_rest > 0 && not_at_rest <= 50) {
        std::cout << "=== PARTICLES NOT AT REST ===\n";
        auto particles = ps.lock_particles_for_read();
        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            if (!p.is_at_rest) {
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                std::cout << "P" << pid << ": pos=(" << p.x << "," << p.y << "," << p.z << ") "
                          << "vel=(" << p.vx << "," << p.vy << "," << p.vz << ") |v|=" << vel
                          << " frames_low=" << (int)p.frames_at_rest << "\n";
            }
        }
    } else if (not_at_rest > 50) {
        // Too many - show summary by layer
        std::cout << "=== NOT AT REST BY LAYER (z) ===\n";
        auto particles = ps.lock_particles_for_read();
        std::map<int, int> by_layer;
        std::map<int, float> max_vel_by_layer;
        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            if (!p.is_at_rest) {
                int layer = static_cast<int>(p.z / tile_thickness);
                by_layer[layer]++;
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                if (vel > max_vel_by_layer[layer]) max_vel_by_layer[layer] = vel;
            }
        }
        for (auto& [layer, count] : by_layer) {
            std::cout << "Layer " << layer << " (z~" << (layer * tile_thickness + tile_thickness/2)
                      << "): " << count << " not at rest, max_vel=" << max_vel_by_layer[layer] << "\n";
        }

        // Show first 10 examples
        std::cout << "\n=== FIRST 10 NOT AT REST ===\n";
        int shown = 0;
        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            if (!p.is_at_rest && shown < 10) {
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                std::cout << "P" << pid << ": pos=(" << p.x << "," << p.y << "," << p.z << ") "
                          << "vel=(" << p.vx << "," << p.vy << "," << p.vz << ") |v|=" << vel
                          << " frames_low=" << (int)p.frames_at_rest << "\n";
                shown++;
            }
        }
    }

    // STRICT: ALL particles must be at rest after 5 seconds
    if (not_at_rest == 0) {
        std::cout << "RESULT: PASS - All " << total_particles << " particles at rest\n";
        return true;
    } else {
        std::cout << "RESULT: FAIL - " << not_at_rest << " particles still moving\n";
        return false;
    }
}
