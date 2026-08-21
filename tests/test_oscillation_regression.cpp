// RENAMED 2026-08-21 by owner ruling on case 5: test_oscillation_diagnostic
// -> test_oscillation_regression. The name was the last thing still calling it
// a diagnostic after the adjudication ruled it a regression test. Owner's
// word: "if we need to rename, do it." Nothing else changed with the rename:
// the scene, the phases, the band and the born-red verdict are exactly as the
// adjudication left them.
//
// TO-INVESTIGATE (test-protocol migration, 2026-08-21): same defect as
// test_physics_minimal, in the same three-branch shape. The middle branch
// accepts a sustained 0.01-0.1 m/s oscillation on a gluoned tile that nothing
// is touching, prints "damping working", and returns TRUE. INV-24: at steady
// state a scene performs zero corrective work, and a correction firing forever
// on the same body is a perpetual-motion machine (its origin: 1955 firings of
// 290 nm each, five orders of magnitude below this band). INV-19: damping
// exists only where a real dissipation process is modelled, so crediting
// "damping" for a residual velocity names a numerical convenience as physics.
// The file's own stated GOAL is to reproduce a 0.5-1 m/s oscillation, and its
// pass band absorbs a tenth of it. Not weakened here, exit code unchanged;
// ruled together with test_physics_minimal.
//
// ADJUDICATED 2026-08-21 (test-adjudication pass): REPAIRED as a REGRESSION
// TEST, and BORN RED at the default phase.
//
// THE QUESTION PUT TO IT WAS: diagnostic or regression test? A diagnostic
// should have no PASS band at all — it reports and never claims. This file
// reads as an instrument (its name, its "Goal: Reproduce the 0.5-1 m/s
// oscillation", its sixteen phases sweeping an ablation matrix up to 65536
// tiles), so the diagnostic reading is the tempting one.
//
// IT IS THE WRONG ONE, and the reason is mechanical rather than aesthetic.
// The file already carries a band that CAN return false: above 0.1 m/s it
// fails. Demoting it to a pure reporter would take a test that can go red and
// make it unable to, which is a weakening, and weakening is the thing this
// pass exists to undo. Structure decides it too: a deterministic scene, a
// measured final velocity, and a verdict on that measurement is regression
// structure whatever the filename says. So the band stays, and becomes the
// law's.
//
// THE LAW. INV-34 (rest-is-reached, ratified 2026-08-21): a scene with no
// external input settles in bounded time and stays settled, and "sustained
// oscillation of an untouched body is an energy source wearing a steady
// state's clothes, however small its amplitude". INV-24: a settled scene
// performs zero corrective work, its record earned on firings of 290 nm each,
// five orders of magnitude under the band that used to pass here. INV-19:
// damping exists only where a real dissipation process is modelled, so
// "damping working" was naming a numerical convenience as physics. And the
// file's own hypothesis, "gluon constraints fighting turtle contacts", is
// INV-22 stated as a suspicion: exactly one mechanism governs a pair. The
// thing it exists to find was a law violation it was configured to pass.
//
// MEASURED. Default phase (11: 64x64 floor, 16 trees, 32 rocks, 8 fallen
// trees), 5 s: max final speed 0.0817 m/s, inside the old middle branch and
// therefore a green before this change. It is now FAIL, which is the honest
// state. Its sibling defect in test_physics_minimal went the other way: that
// scene settles to exactly 0.0000 m/s today, so the same tightening cost it
// no verdict.
//
// Booked known_open in TEST_AUDIT under the renamed key, with the old name
// and the row's history kept in the row.
// INV-33/34/35 were ratified 2026-08-21 and their records land with the
// physics-TDD item-6 branch; see tests/invariants/INV_PROPOSALS.md.
//
// ============================================================================
// OSCILLATION REGRESSION: gluoned floor tile on the turtle
// ============================================================================
// Test: A 2x2 grid of particles gluoned together (like floor tile), resting on Turtle
// Goal: Reproduce the 0.5-1 m/s oscillation seen in 86k particle world
//
// Run: INTERACTIVE=1 ./logosphere-tests --test test_oscillation_regression
//
// Hypothesis: Oscillation comes from gluon constraints fighting Turtle contacts,
// not from single particle-Turtle interaction.
//
// LAWS: INV-34 (rest-is-reached: an untouched scene settles and stays
// settled), INV-24 (zero corrective work at steady state), INV-22 (exactly one
// mechanism governs a pair — a gluon and a turtle contact fighting over the
// same violation is the hypothesis stated as a law), INV-1, INV-3.
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"  // For NailGluon
#include "logosphere/physics/physics_solver.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstdlib>
#include <vector>
#include <string>

using PhysicsV4::TURTLE_Z;

bool test_oscillation_regression() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  OSCILLATION REGRESSION: Gluoned Tile on Turtle              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Oscillation Diagnostic";
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

    // Floor tile settings (same as logomancers FloorGenerator)
    const float tile_size = 0.5f;        // 0.5m tiles
    const float tile_thickness = 0.1f;   // 10cm thick
    const float tile_r = 0.4f, tile_g = 0.35f, tile_b = 0.25f;  // Earth brown

    std::vector<int> pids;
    int light_id = -1;
    // Phase selection: interactive starts at 1, headless uses PHASE env var or defaults to 11
    int test_phase = 1;
    if (!is_interactive) {
        const char* phase_env = std::getenv("PHASE");
        test_phase = phase_env ? std::atoi(phase_env) : 11;
        if (test_phase < 1 || test_phase > 16) test_phase = 11;
    }

    // Initialize tree generator
    PhysicsTreeGenerator tree_generator;
    tree_generator.initialize(&engine);

    // Initialize rock generator
    PhysicsRockGenerator rock_generator;
    rock_generator.initialize(&engine);

    // Lambda to create tiles
    auto create_tiles = [&](int phase) {
        // Clear existing
        ps.clear_particles();
        physics.clear_gluons();
        pids.clear();

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

        if (phase == 1) {
            // Single tile
            std::cout << "\n[PHASE 1] Creating single tile on Turtle\n";
            Particle tile = {};
            tile.x = 0.0f;
            tile.y = 0.0f;
            tile.z = tile_thickness / 2.0f;
            tile.shape = ParticleShape::BOX;
            tile.width = tile_size;
            tile.height = tile_size;
            tile.thickness = tile_thickness;
            tile.SetMaterial(Materials::Type::BRICK);
            tile.r = tile_r; tile.g = tile_g; tile.b = tile_b;
            tile.friction = 0.5f;
            pids.push_back(engine.add_particle(tile));
        } else if (phase == 2) {
            // 2x2 grid of tiles
            std::cout << "\n[PHASE 2] Creating 2x2 floor tiles on Turtle\n";
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    Particle tile = {};
                    tile.x = (col - 0.5f) * tile_size;  // Center grid at origin
                    tile.y = (row - 0.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    tile.r = tile_r + (col * 0.05f);  // Slight color variation
                    tile.g = tile_g;
                    tile.b = tile_b + (row * 0.05f);
                    tile.friction = 0.5f;
                    pids.push_back(engine.add_particle(tile));
                }
            }
        } else if (phase == 3) {
            // 16x16 grid of tiles (256 tiles) - NO gluons
            std::cout << "\n[PHASE 3] Creating 16x16 floor tiles (256) on Turtle - NO GLUONS\n";
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    Particle tile = {};
                    tile.x = (col - 7.5f) * tile_size;  // Center grid at origin
                    tile.y = (row - 7.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Checkerboard-ish color variation
                    float var = ((row + col) % 2) * 0.05f;
                    tile.r = tile_r + var;
                    tile.g = tile_g;
                    tile.b = tile_b + var;
                    tile.friction = 0.5f;
                    pids.push_back(engine.add_particle(tile));
                }
            }
        } else if (phase == 4) {
            // 16x16 grid of tiles (256 tiles) - NO floor gluons (tiles rest independently)
            // This tests the hypothesis that floor gluons cause oscillation propagation
            std::cout << "\n[PHASE 4] Creating 16x16 floor tiles (256) on Turtle - NO FLOOR GLUONS\n";

            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    Particle tile = {};
                    tile.x = (col - 7.5f) * tile_size;
                    tile.y = (row - 7.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Blue tint to distinguish from phase 3
                    tile.r = tile_r * 0.8f;
                    tile.g = tile_g * 0.9f;
                    tile.b = tile_b + 0.2f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                }
            }
            std::cout << "  NO floor gluons - tiles rest independently on Turtle\n";
        } else if (phase == 5) {
            // PHASE 5: 16x16 tiles (NO floor gluons) + tree in center
            // Tree is gluoned to its base tile, but floor tiles are independent
            std::cout << "\n[PHASE 5] Creating 16x16 floor (no gluons) + TREE in center\n";

            // First create all tiles - NO gluons between them
            std::vector<std::vector<int>> grid(16, std::vector<int>(16));
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    Particle tile = {};
                    tile.x = (col - 7.5f) * tile_size;
                    tile.y = (row - 7.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Green tint for tree phase
                    tile.r = tile_r * 0.7f;
                    tile.g = tile_g + 0.1f;
                    tile.b = tile_b * 0.7f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  NO floor gluons - tiles rest independently on Turtle\n";

            // Create tree in center (on tile at grid[8][8])
            int center_tile_id = grid[8][8];

            // Get the ACTUAL position of the center tile
            float tree_x, tree_y;
            {
                auto particles_read = ps.lock_particles_for_read();
                const Particle& center_tile = particles_read[center_tile_id];
                tree_x = center_tile.x;
                tree_y = center_tile.y;
            }

            TreeSpec spec;
            spec.height = 3.0f;           // 3m tall tree
            spec.trunk_diameter = 0.3f;   // 30cm trunk
            spec.branch_depth = 3;        // Moderate branching
            spec.branches_per_split = 2;
            spec.length_ratio = 0.7f;
            spec.branch_angle = 45.0f;
            spec.random_seed = 12345;
            spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
            spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

            PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                tree_x, tree_y, tile_thickness,  // Tree position AT the tile's location
                center_tile_id,                   // Floor tile to attach to
                spec
            );

            // Add tree particles to our tracking list
            if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
            for (int id : tree.branch_ids) pids.push_back(id);
            for (int id : tree.leaf_ids) pids.push_back(id);

            std::cout << "  Created tree: " << tree.total_segments << " segments, "
                      << tree.leaf_ids.size() << " leaves\n";

            // DEBUG: Print tree particle positions
            {
                auto particles_read = ps.lock_particles_for_read();
                std::cout << "\n[TREE_DEBUG] Trunk particle " << tree.trunk_id << ":\n";
                const Particle& trunk = particles_read[tree.trunk_id];
                std::cout << "  pos=(" << trunk.x << ", " << trunk.y << ", " << trunk.z << ")\n";
                std::cout << "  mass=" << trunk.GetMass() << " shape=" << (int)trunk.shape << "\n";
                std::cout << "  thickness=" << trunk.thickness << " (height)\n";

                // Check center tile position
                const Particle& center_tile = particles_read[center_tile_id];
                std::cout << "[TREE_DEBUG] Center tile " << center_tile_id << ":\n";
                std::cout << "  pos=(" << center_tile.x << ", " << center_tile.y << ", " << center_tile.z << ")\n";
                std::cout << "  top surface z=" << (center_tile.z + center_tile.thickness/2.0f) << "\n";
            }
        } else if (phase == 6 || phase == 7 || phase == 8) {
            // PHASE 6/7/8: 32x32 tiles with varying layers + 4 trees
            // NO floor gluons - tiles rest independently, only trees are gluoned
            // Phase 6: 1 layer (1024 tiles)
            // Phase 7: 4 layers (4096 tiles)
            // Phase 8: 16 layers (16384 tiles)
            const int LAYERS = (phase == 6) ? 1 : (phase == 7) ? 4 : 16;
            std::cout << "\n[PHASE " << phase << "] Creating 32x32x" << LAYERS
                      << " floor (NO gluons) (" << (32*32*LAYERS) << " tiles) + 4 TREES\n";

            // Create all tiles - LAYERS of 32x32 (NO GLUONS between tiles)
            // grid[layer][row][col]
            std::vector<std::vector<std::vector<int>>> grid(LAYERS,
                std::vector<std::vector<int>>(32, std::vector<int>(32)));

            for (int layer = 0; layer < LAYERS; ++layer) {
                float layer_z = tile_thickness / 2.0f + layer * tile_thickness;
                // Color gradient per layer
                float layer_brightness = 0.6f + 0.4f * ((float)layer / std::max(1, LAYERS - 1));

                for (int row = 0; row < 32; ++row) {
                    for (int col = 0; col < 32; ++col) {
                        Particle tile = {};
                        tile.x = (col - 15.5f) * tile_size;
                        tile.y = (row - 15.5f) * tile_size;
                        tile.z = layer_z;
                        tile.shape = ParticleShape::BOX;
                        tile.width = tile_size;
                        tile.height = tile_size;
                        tile.thickness = tile_thickness;
                        tile.SetMaterial(Materials::Type::BRICK);
                        // Color by phase: purple(6), cyan(7), orange(8)
                        if (phase == 6) {
                            tile.r = (tile_r + 0.1f) * layer_brightness;
                            tile.g = tile_g * 0.7f * layer_brightness;
                            tile.b = (tile_b + 0.3f) * layer_brightness;
                        } else if (phase == 7) {
                            tile.r = tile_r * 0.5f * layer_brightness;
                            tile.g = (tile_g + 0.2f) * layer_brightness;
                            tile.b = (tile_b + 0.4f) * layer_brightness;
                        } else {
                            tile.r = (tile_r + 0.3f) * layer_brightness;
                            tile.g = (tile_g + 0.1f) * layer_brightness;
                            tile.b = tile_b * 0.5f * layer_brightness;
                        }
                        tile.friction = 0.5f;
                        int pid = engine.add_particle(tile);
                        pids.push_back(pid);
                        grid[layer][row][col] = pid;
                    }
                }
            }
            std::cout << "  NO floor gluons - tiles rest independently on Turtle\n";

            // EXPERIMENT: Settling period before adding trees
            // Hypothesis: Tree 4's tile falls through stack because gluon pulls it
            // down before tile-tile contacts stabilize.
            if (LAYERS > 1) {
                const int SETTLING_FRAMES = 60;
                std::cout << "  [EXPERIMENT] Settling tiles for " << SETTLING_FRAMES << " frames...\n";
                for (int f = 0; f < SETTLING_FRAMES; ++f) {
                    engine.update(1.0 / 60.0);
                }
            }

            // Create 4 trees on the TOP layer
            int top_layer = LAYERS - 1;
            float top_z = tile_thickness * LAYERS;
            int tree_positions[4][2] = {{8, 8}, {8, 24}, {24, 8}, {24, 24}};
            int tree_seeds[4] = {12345, 23456, 34567, 45678};

            for (int t = 0; t < 4; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[top_layer][row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = tree_seeds[t];
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, top_z,
                    tile_id,
                    spec
                );

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);

                std::cout << "  Tree " << (t+1) << ": " << tree.total_segments << " segments, "
                          << tree.leaf_ids.size() << " leaves\n";
            }
        } else if (phase == 9) {
            // PHASE 9: 128x128 single layer (16384 tiles) + 16 trees spread out
            // NO floor gluons - tiles rest independently on Turtle
            std::cout << "\n[PHASE 9] Creating 128x128x1 floor (NO gluons) (16384 tiles) + 16 TREES spread\n";

            // Create all tiles - 128x128 single layer (NO GLUONS)
            std::vector<std::vector<int>> grid(128, std::vector<int>(128));

            for (int row = 0; row < 128; ++row) {
                for (int col = 0; col < 128; ++col) {
                    Particle tile = {};
                    tile.x = (col - 63.5f) * tile_size;
                    tile.y = (row - 63.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Yellow-green tint for phase 9
                    tile.r = tile_r + 0.2f;
                    tile.g = tile_g + 0.3f;
                    tile.b = tile_b * 0.3f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  NO floor gluons - tiles rest independently on Turtle\n";

            // Create 16 trees in a 4x4 grid pattern
            // Positions spread across the 128x128 floor
            int tree_positions[16][2] = {
                {16, 16}, {16, 48}, {16, 80}, {16, 112},
                {48, 16}, {48, 48}, {48, 80}, {48, 112},
                {80, 16}, {80, 48}, {80, 80}, {80, 112},
                {112, 16}, {112, 48}, {112, 80}, {112, 112}
            };

            for (int t = 0; t < 16; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = 10000 + t * 1111;
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, tile_thickness,
                    tile_id,
                    spec
                );

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);

                std::cout << "  Tree " << (t+1) << ": " << tree.total_segments << " segments\n";
            }
        } else if (phase == 10) {
            // PHASE 10: 256x256 single layer (65536 tiles) + 64 trees (4x phase 9)
            // NO floor gluons - tiles rest independently on Turtle
            std::cout << "\n[PHASE 10] Creating 256x256x1 floor (NO gluons) (65536 tiles) + 64 TREES\n";

            // Create all tiles - 256x256 single layer (NO GLUONS)
            std::vector<std::vector<int>> grid(256, std::vector<int>(256));

            for (int row = 0; row < 256; ++row) {
                for (int col = 0; col < 256; ++col) {
                    Particle tile = {};
                    tile.x = (col - 127.5f) * tile_size;
                    tile.y = (row - 127.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    // Red tint for phase 10
                    tile.r = tile_r + 0.4f;
                    tile.g = tile_g * 0.5f;
                    tile.b = tile_b * 0.3f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  Created " << (256*256) << " tiles\n";
            std::cout << "  NO floor gluons - tiles rest independently on Turtle\n";

            // Create 64 trees in an 8x8 grid pattern
            int tree_count = 0;
            for (int tr = 0; tr < 8; ++tr) {
                for (int tc = 0; tc < 8; ++tc) {
                    int row = 16 + tr * 32;  // Spread across 256 tiles
                    int col = 16 + tc * 32;
                    int tile_id = grid[row][col];

                    float tree_x, tree_y;
                    {
                        auto particles_read = ps.lock_particles_for_read();
                        const Particle& tile = particles_read[tile_id];
                        tree_x = tile.x;
                        tree_y = tile.y;
                    }

                    TreeSpec spec;
                    spec.height = 3.0f;
                    spec.trunk_diameter = 0.3f;
                    spec.branch_depth = 3;
                    spec.branches_per_split = 2;
                    spec.length_ratio = 0.7f;
                    spec.branch_angle = 45.0f;
                    spec.random_seed = 20000 + tree_count * 777;
                    spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                    spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                    PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                        tree_x, tree_y, tile_thickness,
                        tile_id,
                        spec
                    );

                    if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                    for (int id : tree.branch_ids) pids.push_back(id);
                    for (int id : tree.leaf_ids) pids.push_back(id);
                    tree_count++;
                }
            }
            std::cout << "  Created " << tree_count << " trees\n";
        } else if (phase == 11) {
            // PHASE 11: Like logomancers - 64x64 floor + trees + rocks + fallen trees
            // Tests full scene complexity with mixed entity types
            std::cout << "\n[PHASE 11] Creating 64x64 floor + 16 TREES + 32 ROCKS + 8 FALLEN TREES\n";

            // Create 64x64 floor tiles (NO gluons)
            std::vector<std::vector<int>> grid(64, std::vector<int>(64));
            for (int row = 0; row < 64; ++row) {
                for (int col = 0; col < 64; ++col) {
                    Particle tile = {};
                    tile.x = (col - 31.5f) * tile_size;
                    tile.y = (row - 31.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::DIRT);
                    // Earth tones
                    tile.r = tile_r + 0.1f;
                    tile.g = tile_g;
                    tile.b = tile_b * 0.8f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  Created " << (64*64) << " tiles (NO gluons)\n";

            // Create 16 trees in 4x4 pattern
            int tree_count = 0;
            int tree_positions[16][2] = {
                {8, 8}, {8, 24}, {8, 40}, {8, 56},
                {24, 8}, {24, 24}, {24, 40}, {24, 56},
                {40, 8}, {40, 24}, {40, 40}, {40, 56},
                {56, 8}, {56, 24}, {56, 40}, {56, 56}
            };
            for (int t = 0; t < 16; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = 30000 + t * 333;
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, tile_thickness,
                    tile_id,
                    spec
                );

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);
                tree_count++;
            }
            std::cout << "  Created " << tree_count << " trees\n";

            // Create 32 rocks of various sizes scattered around
            int rock_count = 0;
            for (int r = 0; r < 32; ++r) {
                // Scatter rocks across floor
                int row = 4 + (r % 8) * 7 + (r / 8);
                int col = 4 + (r / 8) * 15 + (r % 4);
                if (row >= 64) row = 63;
                if (col >= 64) col = 63;

                float rock_x, rock_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[grid[row][col]];
                    rock_x = tile.x + (r % 3 - 1) * 0.1f;  // Small offset
                    rock_y = tile.y + (r / 3 % 3 - 1) * 0.1f;
                }

                // Alternate rock sizes
                PhysicsRockSpec spec;
                if (r % 4 == 0) {
                    spec = PhysicsRockSpec::large_boulder();
                } else if (r % 4 == 1) {
                    spec = PhysicsRockSpec::medium_rock();
                } else if (r % 4 == 2) {
                    spec = PhysicsRockSpec::small_rock();
                } else {
                    spec = PhysicsRockSpec::pebble();
                }

                PhysicsRockResult rock = rock_generator.generate_rock(
                    rock_x, rock_y, tile_thickness + spec.size / 2.0f,
                    spec
                );

                if (rock.core_id >= 0) pids.push_back(rock.core_id);
                for (int id : rock.box_ids) {
                    if (id != rock.core_id) pids.push_back(id);
                }
                rock_count++;
            }
            std::cout << "  Created " << rock_count << " rocks\n";

            // Create 8 fallen trees (horizontal logs on ground)
            int fallen_count = 0;
            int fallen_positions[8][2] = {
                {16, 16}, {16, 48}, {32, 8}, {32, 56},
                {48, 16}, {48, 48}, {12, 32}, {52, 32}
            };
            for (int f = 0; f < 8; ++f) {
                int row = fallen_positions[f][0];
                int col = fallen_positions[f][1];

                float log_x, log_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[grid[row][col]];
                    log_x = tile.x;
                    log_y = tile.y;
                }

                // Create fallen log as a horizontal capsule (approximated with box)
                float log_length = 1.5f + (f % 3) * 0.5f;  // 1.5-2.5m
                float log_diameter = 0.2f + (f % 2) * 0.1f;  // 0.2-0.3m
                float rotation = (f * 45.0f) * 3.14159f / 180.0f;  // Vary rotation

                Particle log_p = {};
                log_p.shape = ParticleShape::BOX;
                log_p.x = log_x;
                log_p.y = log_y;
                log_p.z = tile_thickness + log_diameter / 2.0f;
                log_p.width = log_length;
                log_p.height = log_diameter;
                log_p.thickness = log_diameter;
                log_p.rotation_z = rotation;
                log_p.SetMaterial(Materials::Type::WOOD_HARD);
                log_p.r = 0.3f + (f % 3) * 0.05f;
                log_p.g = 0.2f;
                log_p.b = 0.1f;
                log_p.friction = 0.6f;
                pids.push_back(engine.add_particle(log_p));
                fallen_count++;
            }
            std::cout << "  Created " << fallen_count << " fallen trees\n";
        } else if (phase == 12) {
            // PHASE 12: Same as phase 11 but NO rocks, NO fallen trees
            // Isolates whether rocks cause oscillation
            std::cout << "\n[PHASE 12] Creating 64x64 floor + 16 TREES (NO rocks, NO fallen trees)\n";

            // Create 64x64 floor tiles (NO gluons) - same as phase 11
            std::vector<std::vector<int>> grid(64, std::vector<int>(64));
            for (int row = 0; row < 64; ++row) {
                for (int col = 0; col < 64; ++col) {
                    Particle tile = {};
                    tile.x = (col - 31.5f) * tile_size;
                    tile.y = (row - 31.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::DIRT);
                    tile.r = tile_r + 0.15f;  // Slightly different color to distinguish
                    tile.g = tile_g + 0.1f;
                    tile.b = tile_b * 0.6f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  Created " << (64*64) << " tiles (NO gluons)\n";

            // Create 16 trees in 4x4 pattern - same positions as phase 11
            int tree_count = 0;
            int tree_positions[16][2] = {
                {8, 8}, {8, 24}, {8, 40}, {8, 56},
                {24, 8}, {24, 24}, {24, 40}, {24, 56},
                {40, 8}, {40, 24}, {40, 40}, {40, 56},
                {56, 8}, {56, 24}, {56, 40}, {56, 56}
            };
            for (int t = 0; t < 16; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = 30000 + t * 333;  // Same seeds as phase 11
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, tile_thickness,
                    tile_id,
                    spec
                );

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);
                tree_count++;
            }
            std::cout << "  Created " << tree_count << " trees (NO rocks, NO fallen trees)\n";
        } else if (phase == 13) {
            // PHASE 13: 64x64 + 16 trees + 32 rocks (NO fallen trees)
            // Tests if rocks alone cause oscillation
            std::cout << "\n[PHASE 13] Creating 64x64 floor + 16 TREES + 32 ROCKS (NO fallen trees)\n";

            std::vector<std::vector<int>> grid(64, std::vector<int>(64));
            for (int row = 0; row < 64; ++row) {
                for (int col = 0; col < 64; ++col) {
                    Particle tile = {};
                    tile.x = (col - 31.5f) * tile_size;
                    tile.y = (row - 31.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::DIRT);
                    tile.r = tile_r + 0.2f;
                    tile.g = tile_g * 0.8f;
                    tile.b = tile_b * 0.5f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  Created " << (64*64) << " tiles\n";

            // 16 trees
            int tree_count = 0;
            int tree_positions[16][2] = {
                {8, 8}, {8, 24}, {8, 40}, {8, 56},
                {24, 8}, {24, 24}, {24, 40}, {24, 56},
                {40, 8}, {40, 24}, {40, 40}, {40, 56},
                {56, 8}, {56, 24}, {56, 40}, {56, 56}
            };
            for (int t = 0; t < 16; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = 30000 + t * 333;
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, tile_thickness, tile_id, spec);

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);
                tree_count++;
            }
            std::cout << "  Created " << tree_count << " trees\n";

            // 32 rocks (same as phase 11)
            int rock_count = 0;
            for (int r = 0; r < 32; ++r) {
                int row = 4 + (r % 8) * 7 + (r / 8);
                int col = 4 + (r / 8) * 15 + (r % 4);
                if (row >= 64) row = 63;
                if (col >= 64) col = 63;

                float rock_x, rock_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[grid[row][col]];
                    rock_x = tile.x + (r % 3 - 1) * 0.1f;
                    rock_y = tile.y + (r / 3 % 3 - 1) * 0.1f;
                }

                PhysicsRockSpec spec;
                if (r % 4 == 0) spec = PhysicsRockSpec::large_boulder();
                else if (r % 4 == 1) spec = PhysicsRockSpec::medium_rock();
                else if (r % 4 == 2) spec = PhysicsRockSpec::small_rock();
                else spec = PhysicsRockSpec::pebble();

                PhysicsRockResult rock = rock_generator.generate_rock(
                    rock_x, rock_y, tile_thickness + spec.size / 2.0f, spec);

                if (rock.core_id >= 0) pids.push_back(rock.core_id);
                for (int id : rock.box_ids) {
                    if (id != rock.core_id) pids.push_back(id);
                }
                rock_count++;
            }
            std::cout << "  Created " << rock_count << " rocks (NO fallen trees)\n";
        } else if (phase == 14) {
            // PHASE 14: 64x64 + 16 trees + 8 fallen trees (NO rocks)
            // Tests if fallen trees alone cause oscillation
            std::cout << "\n[PHASE 14] Creating 64x64 floor + 16 TREES + 8 FALLEN TREES (NO rocks)\n";

            std::vector<std::vector<int>> grid(64, std::vector<int>(64));
            for (int row = 0; row < 64; ++row) {
                for (int col = 0; col < 64; ++col) {
                    Particle tile = {};
                    tile.x = (col - 31.5f) * tile_size;
                    tile.y = (row - 31.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::DIRT);
                    tile.r = tile_r;
                    tile.g = tile_g + 0.2f;
                    tile.b = tile_b * 0.4f;
                    tile.friction = 0.5f;
                    int pid = engine.add_particle(tile);
                    pids.push_back(pid);
                    grid[row][col] = pid;
                }
            }
            std::cout << "  Created " << (64*64) << " tiles\n";

            // 16 trees
            int tree_count = 0;
            int tree_positions[16][2] = {
                {8, 8}, {8, 24}, {8, 40}, {8, 56},
                {24, 8}, {24, 24}, {24, 40}, {24, 56},
                {40, 8}, {40, 24}, {40, 40}, {40, 56},
                {56, 8}, {56, 24}, {56, 40}, {56, 56}
            };
            for (int t = 0; t < 16; ++t) {
                int row = tree_positions[t][0];
                int col = tree_positions[t][1];
                int tile_id = grid[row][col];

                float tree_x, tree_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[tile_id];
                    tree_x = tile.x;
                    tree_y = tile.y;
                }

                TreeSpec spec;
                spec.height = 3.0f;
                spec.trunk_diameter = 0.3f;
                spec.branch_depth = 3;
                spec.branches_per_split = 2;
                spec.length_ratio = 0.7f;
                spec.branch_angle = 45.0f;
                spec.random_seed = 30000 + t * 333;
                spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
                spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

                PhysicsTreeResult tree = tree_generator.generate_tree_on_floor(
                    tree_x, tree_y, tile_thickness, tile_id, spec);

                if (tree.trunk_id >= 0) pids.push_back(tree.trunk_id);
                for (int id : tree.branch_ids) pids.push_back(id);
                for (int id : tree.leaf_ids) pids.push_back(id);
                tree_count++;
            }
            std::cout << "  Created " << tree_count << " trees\n";

            // 8 fallen trees (same as phase 11)
            int fallen_count = 0;
            int fallen_positions[8][2] = {
                {16, 16}, {16, 48}, {32, 8}, {32, 56},
                {48, 16}, {48, 48}, {12, 32}, {52, 32}
            };
            for (int f = 0; f < 8; ++f) {
                int row = fallen_positions[f][0];
                int col = fallen_positions[f][1];

                float log_x, log_y;
                {
                    auto particles_read = ps.lock_particles_for_read();
                    const Particle& tile = particles_read[grid[row][col]];
                    log_x = tile.x;
                    log_y = tile.y;
                }

                float log_length = 1.5f + (f % 3) * 0.5f;
                float log_diameter = 0.2f + (f % 2) * 0.1f;
                float rotation = (f * 45.0f) * 3.14159f / 180.0f;

                Particle log_p = {};
                log_p.shape = ParticleShape::BOX;
                log_p.x = log_x;
                log_p.y = log_y;
                log_p.z = tile_thickness + log_diameter / 2.0f;
                log_p.width = log_length;
                log_p.height = log_diameter;
                log_p.thickness = log_diameter;
                log_p.rotation_z = rotation;
                log_p.SetMaterial(Materials::Type::WOOD_HARD);
                log_p.r = 0.3f + (f % 3) * 0.05f;
                log_p.g = 0.2f;
                log_p.b = 0.1f;
                log_p.friction = 0.6f;
                pids.push_back(engine.add_particle(log_p));
                fallen_count++;
            }
            std::cout << "  Created " << fallen_count << " fallen trees (NO rocks)\n";
        } else if (phase == 15) {
            // PHASE 15: Just tiles + 1 pebble (smallest rock)
            // Minimal rock test case
            std::cout << "\n[PHASE 15] Creating 16x16 floor + 1 PEBBLE (minimal rock)\n";

            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    Particle tile = {};
                    tile.x = (col - 7.5f) * tile_size;
                    tile.y = (row - 7.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    tile.r = tile_r; tile.g = tile_g; tile.b = tile_b;
                    tile.friction = 0.5f;
                    pids.push_back(engine.add_particle(tile));
                }
            }
            std::cout << "  Created 256 tiles\n";

            // 1 pebble in center
            PhysicsRockSpec spec = PhysicsRockSpec::pebble();
            PhysicsRockResult rock = rock_generator.generate_rock(
                0.0f, 0.0f, tile_thickness + spec.size / 2.0f, spec);

            if (rock.core_id >= 0) pids.push_back(rock.core_id);
            for (int id : rock.box_ids) {
                if (id != rock.core_id) pids.push_back(id);
            }
            std::cout << "  Created 1 pebble (" << rock.total_boxes << " boxes)\n";
        } else if (phase == 16) {
            // PHASE 16: Just tiles + 1 boulder (largest rock)
            std::cout << "\n[PHASE 16] Creating 16x16 floor + 1 BOULDER (large rock)\n";

            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    Particle tile = {};
                    tile.x = (col - 7.5f) * tile_size;
                    tile.y = (row - 7.5f) * tile_size;
                    tile.z = tile_thickness / 2.0f;
                    tile.shape = ParticleShape::BOX;
                    tile.width = tile_size;
                    tile.height = tile_size;
                    tile.thickness = tile_thickness;
                    tile.SetMaterial(Materials::Type::BRICK);
                    tile.r = tile_r; tile.g = tile_g; tile.b = tile_b;
                    tile.friction = 0.5f;
                    pids.push_back(engine.add_particle(tile));
                }
            }
            std::cout << "  Created 256 tiles\n";

            // 1 large boulder in center
            PhysicsRockSpec spec = PhysicsRockSpec::large_boulder();
            PhysicsRockResult rock = rock_generator.generate_rock(
                0.0f, 0.0f, tile_thickness + spec.size / 2.0f, spec);

            if (rock.core_id >= 0) pids.push_back(rock.core_id);
            for (int id : rock.box_ids) {
                if (id != rock.core_id) pids.push_back(id);
            }
            std::cout << "  Created 1 boulder (" << rock.total_boxes << " boxes)\n";
        }
        std::cout << "  Created " << pids.size() << " particle(s) total\n";
    };

    // Create initial tiles
    create_tiles(test_phase);

    // Run physics
    const float dt = 1.0f / 60.0f;
    const int total_frames = is_interactive ? 36000 : 600;  // 10 min or 10s

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

            // Draw UI overlay with particle state
            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();

                // Calculate stats for all tiles
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

                // First tile details
                const Particle& p = particles[pids[0]];

                std::string phase_str = (test_phase == 1) ? "PHASE 1: Single Tile" :
                                        (test_phase == 2) ? "PHASE 2: Four Tiles (2x2)" :
                                        (test_phase == 3) ? "PHASE 3: 256 Tiles (no gluons)" :
                                        (test_phase == 4) ? "PHASE 4: 256 Tiles (no gluons)" :
                                        (test_phase == 5) ? "PHASE 5: 256 Tiles + TREE" :
                                        (test_phase == 6) ? "PHASE 6: 1024x1 + 4 TREES" :
                                        (test_phase == 7) ? "PHASE 7: 1024x4 + 4 TREES" :
                                        (test_phase == 8) ? "PHASE 8: 1024x16 + 4 TREES" :
                                        (test_phase == 9) ? "PHASE 9: 16384x1 + 16 TREES" :
                                        (test_phase == 10) ? "PHASE 10: 65536x1 + 64 TREES" :
                                        (test_phase == 11) ? "PHASE 11: 4096 + TREES+ROCKS+LOGS" :
                                        (test_phase == 12) ? "PHASE 12: 4096 + 16 TREES (no rocks)" :
                                        (test_phase == 13) ? "PHASE 13: 4096 + 16 TREES + 32 ROCKS" :
                                        (test_phase == 14) ? "PHASE 14: 4096 + 16 TREES + 8 LOGS" :
                                        (test_phase == 15) ? "PHASE 15: 256 + 1 PEBBLE" :
                                                             "PHASE 16: 256 + 1 BOULDER";
                ui->draw_text(10, 10, phase_str, 255, 200, 100);
                ui->draw_text(10, 40, "Tiles: " + std::to_string(pids.size()), 200, 200, 200);
                ui->draw_text(10, 70, "Avg z: " + std::to_string(avg_z).substr(0, 6) + "m", 200, 200, 200);
                ui->draw_text(10, 100, "Max velocity: " + std::to_string(max_vel).substr(0, 6) + " m/s", 200, 200, 200);
                ui->draw_text(10, 130, "vz[0]=" + std::to_string(p.vz).substr(0, 8), 150, 150, 150);

                std::string rest_str = std::to_string(at_rest_count) + "/" + std::to_string(pids.size()) + " at rest";
                if (at_rest_count == (int)pids.size()) {
                    ui->draw_text(10, 160, rest_str, 100, 255, 100);
                } else {
                    ui->draw_text(10, 160, rest_str, 255, 100, 100);
                }

                ui->draw_text(10, 200, "SPACE: switch phase | ESC: exit", 128, 128, 128);
            }

            engine.present();

            // Check for input
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\n  ESC pressed - exiting" << std::endl;
                should_quit = true;
            }

            // SPACE to switch phases (1 -> 2 -> ... -> 16 -> 1)
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed) {
                test_phase = (test_phase % 16) + 1;  // Cycle 1 -> 2 -> ... -> 16 -> 1
                create_tiles(test_phase);
                frame = 0;  // Reset frame counter
            }
            space_was_pressed = space_pressed;
        }

        // Log every 30 frames (0.5 second)
        if (frame % 30 == 0 || frame < 10) {
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

    // Final detailed state
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FINAL STATE @ t=5s                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    auto particles = ps.lock_particles_for_read();
    for (int pid : pids) {
        const Particle& p = particles[pid];
        float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
        std::cout << "Particle " << pid << ": pos=(" << p.x << ", " << p.y << ", " << p.z << ") "
                  << "vel=" << vel << " m/s "
                  << (p.is_at_rest ? "[AT REST]" : "[ACTIVE]") << "\n";
    }

    // THE BAND IS THE LAW'S, not a tenth of the phenomenon this file exists to
    // reproduce. Same shape as its sibling test_physics_minimal, and for the
    // same reason: INV-34 (an untouched scene settles and stays settled),
    // INV-24 (zero corrective work at steady state), with INV-22 as the
    // suspected mechanism (a gluon row and a turtle contact governing one
    // pair). is_at_rest is the engine's own quietness verdict, priced in
    // extremity speed and gated on non-growth per G-44; the speed bound is the
    // residual a reader can check without trusting that predicate.
    float final_max_vel = 0.0f;
    int not_at_rest = 0;
    for (int pid : pids) {
        const Particle& p = particles[pid];
        float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
        final_max_vel = std::max(final_max_vel, vel);
        if (!p.is_at_rest) ++not_at_rest;
    }

    const bool all_at_rest = (not_at_rest == 0);
    const bool speed_ok    = (final_max_vel < 0.01f);

    std::cout << "\n";
    std::cout << "  [measure] max final speed " << final_max_vel << " m/s"
              << "   at rest " << (pids.size() - (size_t)not_at_rest) << "/"
              << pids.size() << "\n";

    if (all_at_rest && speed_ok) {
        std::cout << "RESULT: PASS INV-24/INV-34: the tile reached rest and "
                     "stayed there (max " << final_max_vel << " m/s)\n";
        return true;
    }
    if (!all_at_rest) {
        std::cout << "RESULT: FAIL INV-34: " << not_at_rest << " of " << pids.size()
                  << " bodies never reached rest on a scene nobody is touching\n";
    }
    if (!speed_ok) {
        std::cout << "RESULT: FAIL INV-34/INV-24/INV-22: sustained " << final_max_vel
                  << " m/s on a scene nobody is touching. This is the "
                     "oscillation the file was written to reproduce, at a tenth "
                     "of the amplitude it went looking for, and the old middle "
                     "branch passed it as 'damping working'.\n";
    }
    return false;
}
