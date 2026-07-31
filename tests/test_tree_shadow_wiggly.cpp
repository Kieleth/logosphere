#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// TREE SHADOW WIGGLY TEST
// ============================================================================
// Purpose: Investigate why shadows change wildly even when trees are stable
// Setup: Full floor grid, complex oak tree, sunset light position
//
// Run: INTERACTIVE=1 ./logosphere-tests --test test_tree_shadow_wiggly
// ============================================================================

// Track shadow changes by sampling lit pixels on floor
struct ShadowSample {
    int frame;
    float tree_max_velocity;  // Tree movement
    float shadow_change;      // Change in floor lighting (0-1)
    int lit_pixels;           // Number of lit floor pixels
};

bool test_tree_shadow_wiggly() {
    std::cout << "\n======================================================" << std::endl;
    std::cout << "  TREE SHADOW WIGGLY TEST                              " << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "\nPurpose: Investigate shadow instability with stable trees" << std::endl;
    std::cout << "Setup: Full floor, oak tree depth=5, sunset light" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    // Create engine
    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Tree Shadow Wiggly Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "  Engine init failed!" << std::endl;
        return false;
    }

    // Add gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    auto& ps = engine.get_particle_system();

    // =========================================================================
    // STEP 1: Create floor grid (manual grid for reliable coverage)
    // =========================================================================
    std::cout << "\n[STEP 1] Creating floor grid..." << std::endl;

    // Create a large floor grid manually (50x50 tiles, 2m each = 100x100m area)
    // This ensures we have floor at known positions for tree placement
    const float tile_size = 2.0f;
    const int grid_half = 25;  // -25 to +25 tiles = 51x51 grid
    int floor_count = 0;

    for (int gx = -grid_half; gx <= grid_half; gx++) {
        for (int gy = -grid_half; gy <= grid_half; gy++) {
            float x = gx * tile_size;
            float y = gy * tile_size;

            Particle floor_tile = {};
            floor_tile.x = x;
            floor_tile.y = y;
            floor_tile.z = 0.0f;
            floor_tile.size = tile_size;
            floor_tile.width = tile_size;       // X dimension
            floor_tile.height = tile_size;      // Y dimension
            floor_tile.thickness = 0.1f;        // Z dimension (thin floor)
            floor_tile.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
            // Earth tone color
            floor_tile.r = 0.35f;
            floor_tile.g = 0.25f;
            floor_tile.b = 0.15f;
            engine.add_particle(floor_tile);
            floor_count++;
        }
    }

    std::cout << "  Created " << floor_count << " floor tiles ("
              << (grid_half*2+1) << "x" << (grid_half*2+1) << " grid, "
              << (grid_half*2+1)*tile_size << "m x " << (grid_half*2+1)*tile_size << "m)" << std::endl;

    // =========================================================================
    // STEP 2: Create sunset light (like logomancers December 8th 16:30)
    // =========================================================================
    std::cout << "\n[STEP 2] Creating sunset light..." << std::endl;

    // Sunset position: low on horizon, far away
    // From logomancers: distance=200, inclination=45deg
    // At sunset (phase ~0.75), the sun is near horizon
    // Position: (-100, -100, 15) gives low grazing angle
    Particle sun_light = {};
    sun_light.x = -100.0f;
    sun_light.y = -100.0f;
    sun_light.z = 15.0f;    // Low elevation for sunset
    sun_light.size = 5.0f;
    sun_light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible, sun doesn't fall
    sun_light.is_light_source = true;
    sun_light.emission_strength = 100000000.0f;  // Sun brightness
    sun_light.emission_radius = 500.0f;
    // Sunset colors (warm orange)
    sun_light.r = 1.0f;
    sun_light.g = 0.6f;
    sun_light.b = 0.3f;
    int sun_id = engine.add_particle(sun_light);

    std::cout << "  Sun light at (" << sun_light.x << ", " << sun_light.y << ", " << sun_light.z << ")" << std::endl;
    std::cout << "  Sun ID: " << sun_id << std::endl;

    // =========================================================================
    // STEP 3: Create oak trees (6 total - 1 at center, 5 closer to sun)
    // =========================================================================
    std::cout << "\n[STEP 3] Creating oak trees..." << std::endl;

    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    TreeSpec spec = TreeSpec::oak();

    // Store all trees for velocity tracking
    std::vector<PhysicsTreeResult> all_trees;

    // Tree positions: center + 5 closer to sun at (-100, -100, 15)
    struct TreePos { float x, y; int seed; };
    std::vector<TreePos> tree_positions = {
        {  0.0f,   0.0f, 42},   // Center tree
        {-20.0f, -20.0f, 43},   // Closer to sun
        {-30.0f, -15.0f, 44},
        {-15.0f, -30.0f, 45},
        {-25.0f, -25.0f, 46},
        {-35.0f, -35.0f, 47},   // Closest to sun
    };

    int total_segments = 0;
    int total_leaves = 0;

    for (const auto& pos : tree_positions) {
        spec.random_seed = pos.seed;
        PhysicsTreeResult tree = generator.generate_tree(pos.x, pos.y, 0.2f, spec);

        if (tree.trunk_id >= 0) {
            std::cout << "  Tree at (" << pos.x << ", " << pos.y << "): "
                      << tree.total_segments << " segments, "
                      << tree.leaf_ids.size() << " leaves" << std::endl;
            total_segments += tree.total_segments;
            total_leaves += tree.leaf_ids.size();
            all_trees.push_back(tree);
        } else {
            std::cout << "  Tree at (" << pos.x << ", " << pos.y << "): FAILED" << std::endl;
        }
    }

    std::cout << "  Total: " << all_trees.size() << " trees, "
              << total_segments << " segments, "
              << total_leaves << " leaves" << std::endl;

    // =========================================================================
    // STEP 4: Setup camera to see all trees and shadows
    // =========================================================================
    auto& camera = engine.get_camera_system();
    // Position camera to see trees between origin and sun
    camera.set_position(-50.0f, -50.0f, 35.0f);
    camera.look_at(-15.0f, -15.0f, 5.0f);  // Look at center of tree cluster

    // =========================================================================
    // STEP 5: Settle physics (3 seconds)
    // =========================================================================
    std::cout << "\n[STEP 4] Settling physics (3s)..." << std::flush;

    const double dt = 1.0 / 60.0;
    for (int frame = 0; frame < 180; frame++) {
        engine.update(dt);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "SETTLING: " + std::to_string(frame / 60) + "s / 3s", 255, 255, 0);
            engine.present();
        }
    }
    std::cout << " done" << std::endl;

    // =========================================================================
    // STEP 6: Measure shadow stability (5 seconds)
    // =========================================================================
    std::cout << "\n[STEP 5] Measuring shadow stability (5s)..." << std::endl;

    std::vector<ShadowSample> samples;
    float max_tree_velocity = 0.0f;
    int wiggly_shadow_frames = 0;
    const float SHADOW_CHANGE_THRESHOLD = 0.01f;  // 1% change considered wiggly
    const float TREE_VELOCITY_THRESHOLD = 0.005f; // 5mm/s considered still

    // Sample every 10 frames for 300 frames
    for (int frame = 0; frame < 300; frame++) {
        engine.update(dt);

        if (frame % 10 == 0) {
            ShadowSample sample;
            sample.frame = frame;

            // Measure tree velocity across all trees
            float frame_max_v = 0.0f;
            {
                auto particles = ps.lock_particles_for_read();
                for (const auto& tree : all_trees) {
                    for (int id : tree.branch_ids) {
                        if (id >= 0 && id < static_cast<int>(particles.size())) {
                            const Particle& p = particles[id];
                            float v = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                            frame_max_v = std::max(frame_max_v, v);
                        }
                    }
                    for (int id : tree.leaf_ids) {
                        if (id >= 0 && id < static_cast<int>(particles.size())) {
                            const Particle& p = particles[id];
                            float v = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                            frame_max_v = std::max(frame_max_v, v);
                        }
                    }
                }
            }
            sample.tree_max_velocity = frame_max_v;
            max_tree_velocity = std::max(max_tree_velocity, frame_max_v);

            // Note: Shadow change measurement would require access to framebuffer
            // For now, we detect shadow wiggly by checking if tree is still but
            // BVH is being rebuilt (mark_bvh_dirty triggers)
            sample.shadow_change = 0.0f;  // Placeholder
            sample.lit_pixels = 0;        // Placeholder

            samples.push_back(sample);

            // Count frames where tree is still but we expect shadow issues
            if (frame_max_v < TREE_VELOCITY_THRESHOLD) {
                // Tree is basically still - shadows shouldn't change
                // In visual mode, user can observe if shadows are stable
            }
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            float t = (frame / 60.0f);
            ui->draw_text(10, 10, "MEASURING SHADOWS", 255, 200, 100);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0, 4) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Tree vel: " + std::to_string(max_tree_velocity * 1000).substr(0, 6) + " mm/s", 200, 200, 200);
            ui->draw_text(10, 100, "Watch shadows for wiggly behavior!", 255, 100, 100);

            engine.present();

            // Check for ESC
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\n  ESC pressed - exiting early" << std::endl;
                break;
            }
        }
    }

    // =========================================================================
    // STEP 7: Analysis
    // =========================================================================
    std::cout << "\n[STEP 6] Analysis:" << std::endl;
    std::cout << "  Max tree velocity: " << std::fixed << std::setprecision(4)
              << (max_tree_velocity * 1000.0f) << " mm/s" << std::endl;

    bool tree_is_stable = max_tree_velocity < 0.100f;  // 100mm/s threshold (includes leaf sway)
    std::cout << "  Tree stability: " << (tree_is_stable ? "STABLE" : "UNSTABLE") << std::endl;

    if (is_interactive) {
        std::cout << "\n  Visual inspection required!" << std::endl;
        std::cout << "  Did you observe:" << std::endl;
        std::cout << "    - Shadows flickering/dancing?" << std::endl;
        std::cout << "    - Shadow edges moving when tree is still?" << std::endl;
        std::cout << "    - Sudden shadow jumps?" << std::endl;

        // Wait for user input
        std::cout << "\n  Press SPACE to confirm pass, ESC to fail..." << std::endl;
        auto& input = engine.get_input_system();
        bool space_was_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
        bool esc_was_pressed = input.get_input_state().keys[GLFW_KEY_ESCAPE];

        while (true) {
            engine.get_platform()->poll_events();
            engine.update(dt);
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "VISUAL INSPECTION", 255, 255, 0);
            ui->draw_text(10, 40, "Tree stable: " + std::string(tree_is_stable ? "YES" : "NO"),
                         tree_is_stable ? 0 : 255, tree_is_stable ? 255 : 0, 0);
            ui->draw_text(10, 70, "SPACE=shadows OK, ESC=shadows BAD", 200, 200, 200);

            engine.present();

            const auto& state = input.get_input_state();
            if (!space_was_pressed && state.keys[GLFW_KEY_SPACE]) {
                std::cout << "  User confirmed: SHADOWS OK" << std::endl;
                return tree_is_stable;  // Pass if tree is stable
            }
            if (!esc_was_pressed && state.keys[GLFW_KEY_ESCAPE]) {
                std::cout << "  User confirmed: SHADOWS BAD" << std::endl;
                return false;  // Fail - shadow wiggly observed
            }
            space_was_pressed = state.keys[GLFW_KEY_SPACE];
            esc_was_pressed = state.keys[GLFW_KEY_ESCAPE];
        }
    }

    // Headless mode: just check tree stability
    std::cout << "\n  [HEADLESS] Cannot measure shadow stability without rendering" << std::endl;
    std::cout << "  [HEADLESS] Run with INTERACTIVE=1 for visual inspection" << std::endl;

    if (tree_is_stable) {
        std::cout << "\n  PASS (tree stable - shadow test requires visual)" << std::endl;
    } else {
        std::cout << "\n  FAIL (tree unstable)" << std::endl;
    }

    return tree_is_stable;
}
