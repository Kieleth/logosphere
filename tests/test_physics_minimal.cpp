// ADJUDICATED 2026-08-21 (test-adjudication pass): REPAIRED. The middle PASS
// branch is gone.
//
// It accepted a sustained oscillation of up to 0.1 m/s on a stack of tiles
// that nothing is touching, printed "damping working", and returned true.
// INV-34 (rest-is-reached, ratified 2026-08-21) is exactly this law: a scene
// with no external input settles in bounded time and stays settled, and
// "sustained oscillation of an untouched body is an energy source wearing a
// steady state's clothes, however small its amplitude". INV-34's own mechanism
// field names this test as the second known violation of it. INV-24 says a
// settled scene performs ZERO corrective work (its record was earned on
// firings of 290 nm each, five orders of magnitude under that band), and
// INV-19 forbids crediting damping for a residual velocity, which is naming a
// numerical convenience as a physical process.
//
// The band is now the sibling's. test_physics_minimal_v2 runs the same scene
// shape and demands `not_at_rest == 0` — every body carrying the engine's own
// quietness verdict — and the two files disagreed about one law. They no
// longer do: this file requires BOTH that every tile reads is_at_rest and that
// the maximum final speed is under the 0.01 m/s band it already called "at
// rest, no oscillation". The 0.01-0.1 m/s range now FAILS.
//
// MEASURED, before and after: the tile scene settles honestly today. PHASE 1,
// 8 and 9 all end with every tile at rest and a maximum final speed of exactly
// 0.0000 m/s, so tightening cost no verdict anywhere it was run. The middle
// branch was dead code protecting a defect that the G-44 sleep work had
// already removed from this scene. It is not dead for the gluoned tile:
// test_oscillation_diagnostic, which carried the identical three-branch
// verdict, measures 0.0817 m/s and is red as of the same pass.
//
// INV-33/34/35 were ratified 2026-08-21 and their records land with the
// physics-TDD item-6 branch; see tests/invariants/INV_PROPOSALS.md.
//
// ============================================================================
// MINIMAL PHYSICS TEST: Understanding the fundamentals
// ============================================================================
// Purpose: Observe physics behavior with the simplest possible scenarios.
// No trees, no rocks, no gluons - just tiles on Turtle.
//
// LAWS: INV-1 (the turtle holds), INV-2 (tiles do not sink into each other),
// INV-24 (the stack reaches a fixed point and performs no further corrective
// work) and INV-34 (rest-is-reached: an untouched scene settles and stays
// settled).
//
// Run:
//   PHASE=1 ./logosphere-tests --test test_physics_minimal  # 1 tile
//   PHASE=2 ./logosphere-tests --test test_physics_minimal  # 4 tiles (2x2)
//   PHASE=3 ./logosphere-tests --test test_physics_minimal  # 2 stacked tiles
//   PHASE=4 ./logosphere-tests --test test_physics_minimal  # 3 stacked tiles
//   PHASE=5 ./logosphere-tests --test test_physics_minimal  # 4 stacked tiles (column)
//   PHASE=6 ./logosphere-tests --test test_physics_minimal  # 2x2 base, 2 layers (8 tiles)
//   PHASE=7 ./logosphere-tests --test test_physics_minimal  # 2x2 base, 3 layers (12 tiles)
//   PHASE=8 ./logosphere-tests --test test_physics_minimal  # 2x2 base, 4 layers (16 tiles)
//   PHASE=9 ./logosphere-tests --test test_physics_minimal  # 3x3x3 cube (27 tiles)
//   PHASE=10 ./logosphere-tests --test test_physics_minimal # 4x4x4 cube (64 tiles)
//   ... up to PHASE=22 for 16x16x16 cube (4096 tiles)
//   INTERACTIVE=1 ./logosphere-tests --test test_physics_minimal  # Visual (SPACE to cycle)
//
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/physics_solver.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>

using PhysicsV4::TURTLE_Z;

bool test_physics_minimal() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  MINIMAL PHYSICS TEST: Tiles on Turtle                       ║\n";
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
    config.window_title = "Minimal Physics Test";
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

    // Tile parameters
    const float tile_size = 0.5f;
    const float tile_thickness = 0.1f;
    const float tile_r = 0.4f, tile_g = 0.35f, tile_b = 0.25f;

    std::vector<int> pids;
    int light_id = -1;

    // Phase selection (22 phases total: 1-8 original, 9-22 for NxNxN cubes)
    const int MAX_PHASE = 22;
    int test_phase = 1;
    if (!is_interactive) {
        const char* phase_env = std::getenv("PHASE");
        test_phase = phase_env ? std::atoi(phase_env) : 1;
        if (test_phase < 1 || test_phase > MAX_PHASE) test_phase = 1;
    }

    // Lambda to create scene for a given phase
    auto create_scene = [&](int phase) {
        // Clear existing
        ps.clear_particles();
        physics.clear_gluons();
        pids.clear();

        // Add light first (bright, far away)
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

        float tile_z = tile_thickness / 2.0f;

        if (phase == 1) {
            std::cout << "\n[PHASE 1] Single tile on Turtle\n";
            Particle tile = {};
            tile.x = 0.0f;
            tile.y = 0.0f;
            tile.z = tile_z;
            tile.shape = ParticleShape::BOX;
            tile.width = tile_size;
            tile.height = tile_size;
            tile.thickness = tile_thickness;
            tile.SetMaterial(Materials::Type::BRICK);
            tile.r = tile_r; tile.g = tile_g; tile.b = tile_b;
            tile.friction = 0.5f;
            pids.push_back(engine.add_particle(tile));
        } else if (phase == 2) {
            std::cout << "\n[PHASE 2] Four tiles (2x2) on Turtle\n";
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    Particle tile = {};
                    tile.x = (col - 0.5f) * tile_size;
                    tile.y = (row - 0.5f) * tile_size;
                    tile.z = tile_z;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    tile.r = tile_r + (col * 0.05f);
                    tile.g = tile_g;
                    tile.b = tile_b + (row * 0.05f);
                    tile.friction = 0.5f;
                    pids.push_back(engine.add_particle(tile));
                }
            }
        } else if (phase == 3) {
            std::cout << "\n[PHASE 3] Two stacked tiles (column of 2)\n";
            for (int layer = 0; layer < 2; ++layer) {
                Particle tile = {};
                tile.x = 0.0f;
                tile.y = 0.0f;
                tile.z = tile_z + layer * tile_thickness;
                tile.shape = ParticleShape::BOX;
                tile.width = tile_size;
                tile.height = tile_size;
                tile.thickness = tile_thickness;
                tile.SetMaterial(Materials::Type::BRICK);
                tile.r = 0.3f + layer * 0.15f;
                tile.g = 0.25f + layer * 0.1f;
                tile.b = 0.2f + layer * 0.1f;
                tile.friction = 0.5f;
                pids.push_back(engine.add_particle(tile));
            }
        } else if (phase == 4) {
            std::cout << "\n[PHASE 4] Three stacked tiles (column of 3)\n";
            for (int layer = 0; layer < 3; ++layer) {
                Particle tile = {};
                tile.x = 0.0f;
                tile.y = 0.0f;
                tile.z = tile_z + layer * tile_thickness;
                tile.shape = ParticleShape::BOX;
                tile.width = tile_size;
                tile.height = tile_size;
                tile.thickness = tile_thickness;
                tile.SetMaterial(Materials::Type::BRICK);
                tile.r = 0.3f + layer * 0.1f;
                tile.g = 0.25f + layer * 0.08f;
                tile.b = 0.2f + layer * 0.08f;
                tile.friction = 0.5f;
                pids.push_back(engine.add_particle(tile));
            }
        } else if (phase == 5) {
            std::cout << "\n[PHASE 5] Four stacked tiles (column of 4)\n";
            for (int layer = 0; layer < 4; ++layer) {
                Particle tile = {};
                tile.x = 0.0f;
                tile.y = 0.0f;
                tile.z = tile_z + layer * tile_thickness;
                tile.shape = ParticleShape::BOX;
                tile.width = tile_size;
                tile.height = tile_size;
                tile.thickness = tile_thickness;
                tile.SetMaterial(Materials::Type::BRICK);
                tile.r = 0.3f + layer * 0.075f;
                tile.g = 0.25f + layer * 0.06f;
                tile.b = 0.2f + layer * 0.06f;
                tile.friction = 0.5f;
                pids.push_back(engine.add_particle(tile));
            }
        } else if (phase == 6) {
            std::cout << "\n[PHASE 6] 2x2 base, 2 layers (8 tiles)\n";
            for (int layer = 0; layer < 2; ++layer) {
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        Particle tile = {};
                        tile.x = (col - 0.5f) * tile_size;
                        tile.y = (row - 0.5f) * tile_size;
                        tile.z = tile_z + layer * tile_thickness;
                        tile.shape = ParticleShape::BOX;
                        tile.width = tile_size;
                        tile.height = tile_size;
                        tile.thickness = tile_thickness;
                        tile.SetMaterial(Materials::Type::BRICK);
                        tile.r = 0.3f + layer * 0.15f + col * 0.05f;
                        tile.g = 0.25f + layer * 0.1f;
                        tile.b = 0.2f + layer * 0.1f + row * 0.05f;
                        tile.friction = 0.5f;
                        pids.push_back(engine.add_particle(tile));
                    }
                }
            }
        } else if (phase == 7) {
            std::cout << "\n[PHASE 7] 2x2 base, 3 layers (12 tiles)\n";
            for (int layer = 0; layer < 3; ++layer) {
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        Particle tile = {};
                        tile.x = (col - 0.5f) * tile_size;
                        tile.y = (row - 0.5f) * tile_size;
                        tile.z = tile_z + layer * tile_thickness;
                        tile.shape = ParticleShape::BOX;
                        tile.width = tile_size;
                        tile.height = tile_size;
                        tile.thickness = tile_thickness;
                        tile.SetMaterial(Materials::Type::BRICK);
                        tile.r = 0.3f + layer * 0.1f + col * 0.05f;
                        tile.g = 0.25f + layer * 0.08f;
                        tile.b = 0.2f + layer * 0.08f + row * 0.05f;
                        tile.friction = 0.5f;
                        pids.push_back(engine.add_particle(tile));
                    }
                }
            }
        } else if (phase == 8) {
            std::cout << "\n[PHASE 8] 2x2 base, 4 layers (16 tiles)\n";
            for (int layer = 0; layer < 4; ++layer) {
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        Particle tile = {};
                        tile.x = (col - 0.5f) * tile_size;
                        tile.y = (row - 0.5f) * tile_size;
                        tile.z = tile_z + layer * tile_thickness;
                        tile.shape = ParticleShape::BOX;
                        tile.width = tile_size;
                        tile.height = tile_size;
                        tile.thickness = tile_thickness;
                        tile.SetMaterial(Materials::Type::BRICK);
                        tile.r = 0.3f + layer * 0.075f + col * 0.05f;
                        tile.g = 0.25f + layer * 0.06f;
                        tile.b = 0.2f + layer * 0.06f + row * 0.05f;
                        tile.friction = 0.5f;
                        pids.push_back(engine.add_particle(tile));
                    }
                }
            }
        } else if (phase >= 9 && phase <= 22) {
            // NxNxN cube phases: phase 9 = 3x3x3, phase 10 = 4x4x4, ..., phase 22 = 16x16x16
            int n = phase - 6;  // phase 9 → n=3, phase 22 → n=16
            int total = n * n * n;
            std::cout << "\n[PHASE " << phase << "] " << n << "x" << n << "x" << n
                      << " cube (" << total << " tiles)\n";

            float offset = (n - 1) * tile_size * 0.5f;  // Center the cube

            for (int layer = 0; layer < n; ++layer) {
                for (int row = 0; row < n; ++row) {
                    for (int col = 0; col < n; ++col) {
                        Particle tile = {};
                        tile.x = col * tile_size - offset;
                        tile.y = row * tile_size - offset;
                        tile.z = tile_z + layer * tile_thickness;
                        tile.shape = ParticleShape::BOX;
                        tile.width = tile_size;
                        tile.height = tile_size;
                        tile.thickness = tile_thickness;
                        tile.SetMaterial(Materials::Type::BRICK);
                        // Color gradient based on position
                        tile.r = 0.2f + (float)col / n * 0.4f;
                        tile.g = 0.2f + (float)row / n * 0.4f;
                        tile.b = 0.2f + (float)layer / n * 0.4f;
                        tile.friction = 0.5f;
                        pids.push_back(engine.add_particle(tile));
                    }
                }
            }
        }
        std::cout << "  Created " << pids.size() << " particle(s)\n";
    };

    // Create initial scene
    create_scene(test_phase);

    // Run physics
    const float dt = 1.0f / 60.0f;
    const int total_frames = is_interactive ? 36000 : 300;  // 10 min or 5s

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  VELOCITY TRACKING (SPACE to switch phases, ESC to exit)     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    bool should_quit = false;
    bool space_was_pressed = false;

    for (int frame = 0; frame < total_frames && !should_quit; ++frame) {
        engine.update(dt);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            // Draw UI overlay
            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();

                float max_vel = 0.0f;
                float avg_z = 0.0f;
                int at_rest_count = 0;
                for (int pid : pids) {
                    const Particle& p = particles[pid];
                    float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    max_vel = std::max(max_vel, vel);
                    avg_z += p.z;
                    if (p.is_at_rest) at_rest_count++;
                }
                avg_z /= pids.size();

                std::string phase_str;
                switch (test_phase) {
                    case 1: phase_str = "PHASE 1: Single Tile"; break;
                    case 2: phase_str = "PHASE 2: 2x2 (4 tiles)"; break;
                    case 3: phase_str = "PHASE 3: Column of 2"; break;
                    case 4: phase_str = "PHASE 4: Column of 3"; break;
                    case 5: phase_str = "PHASE 5: Column of 4"; break;
                    case 6: phase_str = "PHASE 6: 2x2 x 2 layers (8)"; break;
                    case 7: phase_str = "PHASE 7: 2x2 x 3 layers (12)"; break;
                    case 8: phase_str = "PHASE 8: 2x2 x 4 layers (16)"; break;
                    default: phase_str = "PHASE ?"; break;
                }
                ui->draw_text(10, 10, phase_str, 255, 200, 100);
                ui->draw_text(10, 40, "Tiles: " + std::to_string(pids.size()), 200, 200, 200);
                ui->draw_text(10, 70, "Frame: " + std::to_string(frame), 200, 200, 200);
                ui->draw_text(10, 100, "Avg z: " + std::to_string(avg_z).substr(0, 6) + "m", 200, 200, 200);
                ui->draw_text(10, 130, "Max velocity: " + std::to_string(max_vel).substr(0, 6) + " m/s", 200, 200, 200);

                // Per-particle details
                int y = 170;
                for (int pid : pids) {
                    const Particle& p = particles[pid];
                    std::string info = "P" + std::to_string(pid) + ": z=" + std::to_string(p.z).substr(0, 7) +
                                       " vz=" + std::to_string(p.vz).substr(0, 8);
                    ui->draw_text(10, y, info, 150, 150, 150);
                    y += 25;
                }

                std::string rest_str = std::to_string(at_rest_count) + "/" + std::to_string(pids.size()) + " at rest";
                if (at_rest_count == (int)pids.size()) {
                    ui->draw_text(10, y + 10, rest_str, 100, 255, 100);
                } else {
                    ui->draw_text(10, y + 10, rest_str, 255, 100, 100);
                }

                ui->draw_text(10, y + 50, "SPACE: switch phase | ESC: exit", 128, 128, 128);
            }

            engine.present();

            // Check for input
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\n  ESC pressed - exiting" << std::endl;
                should_quit = true;
            }

            // SPACE to switch phases (1 -> 2 -> ... -> 8 -> 1)
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed) {
                test_phase = (test_phase % MAX_PHASE) + 1;
                create_scene(test_phase);
                frame = 0;  // Reset frame counter
            }
            space_was_pressed = space_pressed;
        }

        // Log every 30 frames (0.5 second) in headless mode
        if (!is_interactive && (frame % 30 == 0 || frame < 10)) {
            float max_vel = 0.0f, min_vel = 1000.0f, sum_vel = 0.0f;
            float max_z = -1000.0f, min_z = 1000.0f;
            int at_rest_count = 0;

            auto particles = ps.lock_particles_for_read();
            for (int pid : pids) {
                const Particle& p = particles[pid];
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                max_vel = std::max(max_vel, vel);
                min_vel = std::min(min_vel, vel);
                sum_vel += vel;
                max_z = std::max(max_z, p.z);
                min_z = std::min(min_z, p.z);
                if (p.is_at_rest) at_rest_count++;
            }

            float avg_vel = sum_vel / pids.size();
            std::cout << std::setw(5) << frame << " | "
                      << std::setw(7) << max_vel << " | "
                      << std::setw(7) << min_vel << " | "
                      << std::setw(7) << avg_vel << " | "
                      << std::setw(7) << max_z << " | "
                      << std::setw(7) << min_z << " | "
                      << at_rest_count << "/" << pids.size() << "\n";
        }
    }

    // Final state
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FINAL STATE                                                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    auto particles = ps.lock_particles_for_read();
    float max_final_vel = 0.0f;
    int not_at_rest = 0;
    for (int pid : pids) {
        const Particle& p = particles[pid];
        float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
        max_final_vel = std::max(max_final_vel, vel);
        if (!p.is_at_rest) ++not_at_rest;
        std::cout << "Particle " << pid << ": pos=(" << p.x << ", " << p.y << ", " << p.z << ") "
                  << "vel=" << vel << " m/s "
                  << (p.is_at_rest ? "[AT REST]" : "[ACTIVE]") << "\n";
    }

    // THE BAND IS THE SIBLING'S. test_physics_minimal_v2 runs the same scene
    // shape and demands not_at_rest == 0; this file used to accept 0.01-0.1 m/s
    // of sustained oscillation as "damping working". Two files, one law, two
    // answers. INV-34 (rest-is-reached) is that law: an untouched scene settles
    // and stays settled, and a sustained oscillation is an energy source
    // however small its amplitude. Both halves are required, because they say
    // different things: is_at_rest is the engine's own quietness verdict
    // (sustained non-growing quietness over REST_FRAMES_REQUIRED, priced in
    // extremity speed per G-44), and the speed bound is the measured residual
    // a reader can check without trusting that predicate.
    const bool all_at_rest = (not_at_rest == 0);
    const bool speed_ok    = (max_final_vel < 0.01f);

    std::cout << "\n";
    std::cout << "  [measure] max final speed " << max_final_vel << " m/s"
              << "   at rest " << (pids.size() - (size_t)not_at_rest) << "/"
              << pids.size() << "\n";

    if (all_at_rest && speed_ok) {
        std::cout << "RESULT: PASS INV-24/INV-34: every tile reached rest and "
                     "stayed there (max " << max_final_vel << " m/s)\n";
        return true;
    }
    if (!all_at_rest) {
        std::cout << "RESULT: FAIL INV-34: " << not_at_rest << " of " << pids.size()
                  << " tiles never reached rest on a scene nobody is touching\n";
    }
    if (!speed_ok) {
        std::cout << "RESULT: FAIL INV-34/INV-24: sustained " << max_final_vel
                  << " m/s on a scene nobody is touching. INV-34: an untouched "
                     "scene settles and stays settled, and a sustained "
                     "oscillation is an energy source however small its "
                     "amplitude. INV-24: a settled scene performs zero "
                     "corrective work. INV-19 forbids crediting damping for the "
                     "residual, which is what the removed middle branch did.\n";
    }
    return false;
}
