// ============================================================================
// PHYSICS TREE ROOTS TEST: Progressive test cases for root attachment
// ============================================================================
// Purpose: Debug root positioning step by step
//
// CASES (SPACE to cycle):
//   Case 0: Slab + 4 cardinal particles (NO gluons) - baseline positioning
//   Case 1: Tree with roots (full test)
//
// Run:
//   INTERACTIVE=1 ./logosphere-tests --test test_physics_tree_roots
//
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>

using PhysicsV4::TURTLE_Z;

// ============================================================================
// CASE 0: Simple slab + 4 cardinal particles (NO gluons)
// ============================================================================
// Just test that we can position particles on the sides of a slab correctly.
// No physics constraints, no trees, just raw positioning.

struct Case0Result {
    int slab_id = -1;
    int north_id = -1;  // +Y
    int south_id = -1;  // -Y
    int east_id = -1;   // +X
    int west_id = -1;   // -X
};

Case0Result setup_case_0(Engine& engine) {
    Case0Result result;

    // Slab dimensions (same as root plate)
    float slab_w = 1.5f;   // X dimension
    float slab_h = 1.5f;   // Y dimension
    float slab_t = 0.25f;  // Z dimension (thickness)
    float slab_z = TURTLE_Z + slab_t * 0.5f;  // Center Z so bottom sits on turtle

    // Cardinal particle dimensions (like roots but simpler - axis aligned)
    float card_w = 0.2f;   // X (cross-section)
    float card_h = 0.2f;   // Y (cross-section)
    float card_len = 1.0f; // Length (will be thickness for horizontal orientation)

    // Create slab (RED)
    Particle slab = {};
    slab.x = 0.0f;
    slab.y = 0.0f;
    slab.z = slab_z;
    slab.shape = ParticleShape::BOX;
    slab.width = slab_w;
    slab.height = slab_h;
    slab.thickness = slab_t;
    slab.r = 1.0f; slab.g = 0.2f; slab.b = 0.2f; slab.a = 1.0f;
    slab.material_density = 800.0f;
    slab.friction = 0.7f;
    result.slab_id = engine.add_particle(slab);

    std::cout << "[CASE 0] Slab id=" << result.slab_id
              << " at z=" << slab_z*1000 << "mm"
              << " size=(" << slab_w << "x" << slab_h << "x" << slab_t << ")\n";

    // Cardinal particles should have their CENTERS at:
    // - Face center of slab + half their length outward
    // - Z at slab center (sides of slab)

    float face_dist = slab_w * 0.5f;  // Distance from slab center to face
    float card_center_dist = face_dist + card_len * 0.5f;  // Distance to cardinal center

    // EAST (+X): particle extends in +X direction
    {
        Particle p = {};
        p.x = card_center_dist;  // Center at face + half length
        p.y = 0.0f;
        p.z = slab_z;  // Same Z as slab center (on side face)
        p.shape = ParticleShape::BOX;
        // For a horizontal particle pointing +X:
        // width = length (X), height = cross-section (Y), thickness = cross-section (Z)
        p.width = card_len;
        p.height = card_h;
        p.thickness = card_w;  // Z dimension is cross-section for horizontal
        p.r = 0.2f; p.g = 0.2f; p.b = 1.0f; p.a = 1.0f;
        p.material_density = 800.0f;
        p.friction = 0.7f;
        result.east_id = engine.add_particle(p);

        std::cout << "[CASE 0] EAST id=" << result.east_id
                  << " center=(" << p.x*1000 << ", " << p.y*1000 << ", " << p.z*1000 << ")mm\n";
    }

    // WEST (-X)
    {
        Particle p = {};
        p.x = -card_center_dist;
        p.y = 0.0f;
        p.z = slab_z;
        p.shape = ParticleShape::BOX;
        p.width = card_len;
        p.height = card_h;
        p.thickness = card_w;
        p.r = 0.2f; p.g = 0.2f; p.b = 1.0f; p.a = 1.0f;
        p.material_density = 800.0f;
        p.friction = 0.7f;
        result.west_id = engine.add_particle(p);

        std::cout << "[CASE 0] WEST id=" << result.west_id
                  << " center=(" << p.x*1000 << ", " << p.y*1000 << ", " << p.z*1000 << ")mm\n";
    }

    // NORTH (+Y)
    {
        Particle p = {};
        p.x = 0.0f;
        p.y = card_center_dist;
        p.z = slab_z;
        p.shape = ParticleShape::BOX;
        // For horizontal particle pointing +Y:
        // width = cross-section (X), height = length (Y), thickness = cross-section (Z)
        p.width = card_w;
        p.height = card_len;
        p.thickness = card_h;
        p.r = 0.2f; p.g = 0.2f; p.b = 1.0f; p.a = 1.0f;
        p.material_density = 800.0f;
        p.friction = 0.7f;
        result.north_id = engine.add_particle(p);

        std::cout << "[CASE 0] NORTH id=" << result.north_id
                  << " center=(" << p.x*1000 << ", " << p.y*1000 << ", " << p.z*1000 << ")mm\n";
    }

    // SOUTH (-Y)
    {
        Particle p = {};
        p.x = 0.0f;
        p.y = -card_center_dist;
        p.z = slab_z;
        p.shape = ParticleShape::BOX;
        p.width = card_w;
        p.height = card_len;
        p.thickness = card_h;
        p.r = 0.2f; p.g = 0.2f; p.b = 1.0f; p.a = 1.0f;
        p.material_density = 800.0f;
        p.friction = 0.7f;
        result.south_id = engine.add_particle(p);

        std::cout << "[CASE 0] SOUTH id=" << result.south_id
                  << " center=(" << p.x*1000 << ", " << p.y*1000 << ", " << p.z*1000 << ")mm\n";
    }

    // Summary
    std::cout << "[CASE 0] Expected: Cardinals at Z=" << slab_z*1000 << "mm (same as slab center)\n";
    std::cout << "[CASE 0] Slab side at X/Y = +/-" << face_dist*1000 << "mm\n";
    std::cout << "[CASE 0] Cardinal inner edge at X/Y = +/-" << face_dist*1000 << "mm (touching slab)\n\n";

    return result;
}

// ============================================================================
// MAIN TEST
// ============================================================================

bool test_physics_tree_roots() {
    std::cout << "\n";
    std::cout << "===============================================================\n";
    std::cout << "  PHYSICS TREE ROOTS TEST - SPACE to cycle cases\n";
    std::cout << "===============================================================\n\n";

    // Interactive mode
    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    if (!is_interactive) {
        std::cout << "[MODE] HEADLESS - Set INTERACTIVE=1 for visual test\n";
        std::cout << "[MODE] Running Case 0 only in headless mode\n\n";
    }

    // Engine setup
    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Tree Roots Test - SPACE to cycle";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    std::cout << "[SETUP] TURTLE_Z = " << TURTLE_Z << "m (world floor)\n";
    std::cout << "[SETUP] Gravity = -9.8 m/s^2\n\n";

    // Lights
    Particle light1 = {};
    light1.x = 0.0f; light1.y = -8.0f; light1.z = 12.0f;
    light1.size = 2.0f;
    light1.SetMaterial(Materials::Type::LIGHT);
    light1.is_light_source = true;
    light1.emission_strength = 150000.0f;
    light1.emission_radius = 500.0f;
    light1.r = 1.0f; light1.g = 1.0f; light1.b = 0.95f;
    engine.add_particle(light1);

    Particle light2 = {};
    light2.x = 8.0f; light2.y = 5.0f; light2.z = 8.0f;
    light2.size = 1.5f;
    light2.SetMaterial(Materials::Type::LIGHT);
    light2.is_light_source = true;
    light2.emission_strength = 80000.0f;
    light2.emission_radius = 400.0f;
    light2.r = 0.9f; light2.g = 0.95f; light2.b = 1.0f;
    engine.add_particle(light2);

    // State
    int current_case = 0;
    const int max_cases = 2;  // 0 and 1
    bool space_pressed_last = false;

    // Case 0 data
    Case0Result case0;

    // Case 1 data (tree)
    PhysicsTreeGenerator tree_generator;
    tree_generator.initialize(&engine);
    PhysicsTreeResult case1_tree;

    // Setup initial case
    std::cout << "=== SETTING UP CASE 0 ===\n";
    case0 = setup_case_0(engine);

    // Main loop
    const float dt = 1.0f / 60.0f;
    const int max_frames = is_interactive ? 36000 : 300;  // 10 min or 5s
    bool should_quit = false;

    for (int frame = 0; frame < max_frames && !should_quit; ++frame) {
        engine.update(dt);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();

            // Draw case info
            std::string case_name;
            switch (current_case) {
                case 0: case_name = "CASE 0: Slab + 4 cardinals (NO gluons)"; break;
                case 1: case_name = "CASE 1: Tree with roots"; break;
            }

            ui->draw_text(10, 10, case_name, 255, 200, 100);
            ui->draw_text(10, 40, "Frame: " + std::to_string(frame), 200, 200, 200);
            ui->draw_text(10, 70, "SPACE: next case | ESC: exit", 128, 128, 128);

            // Case-specific info
            if (current_case == 0) {
                auto particles = ps.lock_particles_for_read();
                if (case0.slab_id >= 0 && case0.slab_id < (int)particles.size()) {
                    const auto& slab = particles[case0.slab_id];
                    ui->draw_text(10, 110, "Slab Z: " + std::to_string(slab.z*1000).substr(0,6) + "mm", 200, 200, 200);
                }
                if (case0.east_id >= 0 && case0.east_id < (int)particles.size()) {
                    const auto& east = particles[case0.east_id];
                    ui->draw_text(10, 140, "East Z: " + std::to_string(east.z*1000).substr(0,6) + "mm", 100, 100, 255);
                }
                if (case0.north_id >= 0 && case0.north_id < (int)particles.size()) {
                    const auto& north = particles[case0.north_id];
                    ui->draw_text(10, 170, "North Z: " + std::to_string(north.z*1000).substr(0,6) + "mm", 100, 100, 255);
                }
            }

            engine.present();

            // Input
            const auto& input = engine.get_input_system();

            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\nESC pressed - exiting\n";
                should_quit = true;
            }

            bool space_now = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_now && !space_pressed_last) {
                // Cycle to next case
                current_case = (current_case + 1) % max_cases;
                std::cout << "\n=== SWITCHING TO CASE " << current_case << " ===\n";

                // Clear and setup new case
                ps.clear_particles();

                // Re-add lights
                engine.add_particle(light1);
                engine.add_particle(light2);

                if (current_case == 0) {
                    case0 = setup_case_0(engine);
                } else if (current_case == 1) {
                    std::cout << "[CASE 1] Generating tree with roots...\n";
                    TreeSpec spec = TreeSpec::sapling();
                    case1_tree = tree_generator.generate_tree_with_roots(0.0f, 0.0f, TURTLE_Z, spec);
                    std::cout << "[CASE 1] Tree created: trunk=" << case1_tree.trunk_id
                              << " roots=" << case1_tree.root_ids.size() << "\n";
                }
            }
            space_pressed_last = space_now;
        }

        // Headless: report Case 0, then switch to Case 1
        if (!is_interactive && frame == max_frames - 1) {
            std::cout << "\n[HEADLESS] Case 0 Final positions:\n";
            {
                auto particles = ps.lock_particles_for_read();
                if (case0.slab_id >= 0 && case0.slab_id < (int)particles.size()) {
                    std::cout << "  Slab Z: " << particles[case0.slab_id].z*1000 << "mm\n";
                }
                if (case0.east_id >= 0 && case0.east_id < (int)particles.size()) {
                    std::cout << "  East Z: " << particles[case0.east_id].z*1000 << "mm\n";
                }
                if (case0.north_id >= 0 && case0.north_id < (int)particles.size()) {
                    std::cout << "  North Z: " << particles[case0.north_id].z*1000 << "mm\n";
                }
            }

            // Now test Case 1 (tree with roots)
            std::cout << "\n=== HEADLESS: Testing Case 1 (Tree with roots) ===\n";
            ps.clear_particles();
            engine.add_particle(light1);
            engine.add_particle(light2);

            TreeSpec spec = TreeSpec::sapling();
            case1_tree = tree_generator.generate_tree_with_roots(0.0f, 0.0f, TURTLE_Z, spec);

            std::cout << "[CASE 1] Tree created: trunk=" << case1_tree.trunk_id
                      << " roots=" << case1_tree.root_ids.size() << "\n";

            // Run physics for Case 1
            for (int f2 = 0; f2 < 300; ++f2) {
                engine.update(dt);
            }

            std::cout << "\n[HEADLESS] Case 1 Final positions (after 300 frames):\n";
            {
                auto particles = ps.lock_particles_for_read();
                if (case1_tree.root_plate_id >= 0 && case1_tree.root_plate_id < (int)particles.size()) {
                    const auto& plate = particles[case1_tree.root_plate_id];
                    std::cout << "  Plate Z: " << plate.z*1000 << "mm\n";
                }
                for (size_t i = 0; i < case1_tree.root_ids.size() && i < 4; ++i) {
                    int rid = case1_tree.root_ids[i];
                    if (rid >= 0 && rid < (int)particles.size()) {
                        const auto& r = particles[rid];
                        std::cout << "  Root" << i << " Z: " << r.z*1000 << "mm"
                                  << " (thickness=" << r.thickness*1000 << "mm)\n";
                    }
                }
                if (case1_tree.trunk_id >= 0 && case1_tree.trunk_id < (int)particles.size()) {
                    std::cout << "  Trunk Z: " << particles[case1_tree.trunk_id].z*1000 << "mm\n";
                }
            }
        }
    }

    std::cout << "\n[TEST] Complete\n";
    return true;
}
