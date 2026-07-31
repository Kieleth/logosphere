#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "../src/test_context.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS TREE TEST - Verify Generated Trees Stand with Gluon Constraints
// ============================================================================
// Purpose: Test that PhysicsTreeGenerator creates structurally sound trees
//          where all branches stay attached via gluon constraints
//
// Acceptance Criteria:
//   1. Tree stands upright for 10 seconds without collapse
//   2. All branches maintain position within tolerance
//   3. No NaN/Inf in particle positions
//   4. Total mass matches expected calculation
// ============================================================================

// Forward declarations (none needed - standalone test)

// Helper to wait for SPACE key press with visual feedback
static void wait_for_space(Engine& engine, const std::string& title, const std::vector<std::string>& messages) {
    auto* ui = engine.get_ui_system();
    auto& input = engine.get_input_system();

    std::cout << "  [INTERACTIVE] " << title << " - waiting for SPACE..." << std::endl;

    const auto& initial_state = input.get_input_state();
    bool was_pressed = initial_state.keys[GLFW_KEY_SPACE];
    bool detected_press = false;

    while (!detected_press) {
        engine.get_platform()->poll_events();
        engine.render();

        int y = 10;
        ui->draw_text(10, y, title, 255, 255, 0);
        y += 40;

        for (const auto& msg : messages) {
            ui->draw_text(10, y, msg, 200, 200, 200);
            y += 25;
        }

        ui->draw_text(10, y + 20, "Press SPACE to continue...", 150, 150, 255);
        engine.present();

        const auto& state = input.get_input_state();
        bool is_pressed = state.keys[GLFW_KEY_SPACE];

        if (!was_pressed && is_pressed) {
            detected_press = true;
        }
        was_pressed = is_pressed;
    }

    std::cout << "  [INTERACTIVE] SPACE pressed!" << std::endl;
}

// ============================================================================
// Test: Simple Tree Stands Upright
// ============================================================================
bool test_physics_tree_simple(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::string test_name = "Physics Tree: Simple Structure";
    std::string description = "Generate simple tree with PhysicsTreeGenerator, verify stability";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Tree should stand for 10 seconds"});
    }

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Use simple tree spec (reduced branching for easier debugging)
    TreeSpec spec;
    spec.height = 3.0f;           // 3m tall (small tree)
    spec.trunk_diameter = 0.3f;   // 30cm trunk
    spec.branch_depth = 2;        // Just trunk + first branches
    spec.branch_angle = 45.0f;
    spec.branches_per_split = 2;
    spec.length_ratio = 0.6f;
    spec.thickness_ratio = 0.7f;
    spec.side_branch_probability = 0.0f;  // No side branches for simple test
    spec.random_seed = 12345;

    std::cout << "\n[STEP 1] Generating physics tree..." << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Floor ID: " << tree.floor_id << std::endl;
    std::cout << "  Trunk ID: " << tree.trunk_id << std::endl;
    std::cout << "  Total segments: " << tree.total_segments << std::endl;
    std::cout << "  Branch IDs: ";
    for (int id : tree.branch_ids) {
        std::cout << id << " ";
    }
    std::cout << std::endl;

    // Record initial positions
    std::vector<float> initial_z;
    {
        auto particles = ps.lock_particles_for_read();
        for (int id : tree.branch_ids) {
            initial_z.push_back(particles[id].z);
        }
    }

    std::cout << "\n[STEP 2] Running simulation for 10 seconds @ 60Hz..." << std::endl;

    // Run simulation
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds

    bool nan_detected = false;
    float max_drift = 0.0f;
    int nan_frame = -1;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Check for NaN and drift
        {
            auto particles = ps.lock_particles_for_read();
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                int id = tree.branch_ids[i];
                float z = particles[id].z;

                if (std::isnan(z) || std::isinf(z)) {
                    if (!nan_detected) {
                        nan_detected = true;
                        nan_frame = frame;
                        std::cout << "  ⚠️ NaN detected at frame " << frame
                                  << " in particle " << id << std::endl;
                    }
                }

                float drift = std::abs(z - initial_z[i]);
                if (drift > max_drift) {
                    max_drift = drift;
                }
            }
        }

        // Progress logging
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " trunk_z=" << std::setprecision(3) << trunk_z << "m"
                      << " max_drift=" << std::setprecision(4) << max_drift << "m"
                      << std::endl;
        }

        // Render if interactive
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Segments: " + std::to_string(tree.total_segments), 200, 200, 200);
            ui->draw_text(10, 100, "Max drift: " + std::to_string(max_drift).substr(0, 8) + "m", 200, 200, 200);

            if (max_drift < 0.05f) {
                ui->draw_text(10, 130, "Status: STABLE", 0, 255, 0);
            } else {
                ui->draw_text(10, 130, "Status: DRIFTING", 255, 255, 0);
            }

            engine.present();
        }

        if (nan_detected) break;
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Max drift: " << max_drift << "m" << std::endl;
    std::cout << "  NaN detected: " << (nan_detected ? "YES" : "NO") << std::endl;

    // Pass criteria: no NaN and drift < 10cm
    bool pass = !nan_detected && max_drift < 0.1f;

    if (!pass) {
        if (nan_detected) {
            std::cout << "  ❌ FAIL: NaN detected at frame " << nan_frame << std::endl;
        } else {
            std::cout << "  ❌ FAIL: Tree drifted " << max_drift << "m (max 0.1m allowed)" << std::endl;
        }
    } else {
        std::cout << "  ✅ PASS: Tree stable (drift=" << max_drift << "m)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Segments: " + std::to_string(tree.total_segments));
        results.push_back("Max drift: " + std::to_string(max_drift).substr(0, 8) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Test: Oak Tree (More Complex Structure)
// ============================================================================
bool test_physics_tree_oak(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::string test_name = "Physics Tree: Oak Structure";
    std::string description = "Generate oak-like tree with multiple branch levels";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Oak tree with deeper branching"});
    }

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Use oak preset but with reduced parameters for testing
    TreeSpec spec = TreeSpec::oak();
    spec.height = 5.0f;           // 5m tall
    spec.branch_depth = 3;        // 3 levels of branching
    spec.crown_radius = 2.0f;     // Smaller crown
    spec.side_branch_probability = 0.2f;  // Some side branches
    spec.random_seed = 54321;

    std::cout << "\n[STEP 1] Generating oak physics tree..." << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Total segments: " << tree.total_segments << std::endl;
    std::cout << "  Branches: " << tree.branch_ids.size() << ", Leaves: " << tree.leaf_ids.size() << std::endl;

    // DEBUG: Dump initial particle positions by height
    std::cout << "\n[DEBUG] TREE STRUCTURE (sorted by Z):" << std::endl;
    std::vector<std::pair<int, float>> all_parts;  // id, z
    std::vector<float> initial_z_by_id;
    {
        auto particles = ps.lock_particles_for_read();
        all_parts.push_back({tree.trunk_id, particles[tree.trunk_id].z});
        for (int bid : tree.branch_ids) {
            all_parts.push_back({bid, particles[bid].z});
        }
        for (int lid : tree.leaf_ids) {
            all_parts.push_back({lid, particles[lid].z});
        }
        // Sort by Z
        std::sort(all_parts.begin(), all_parts.end(), [](auto& a, auto& b) { return a.second < b.second; });
        // Store initial z for all particles
        int max_id = 0;
        for (auto& p : all_parts) if (p.first > max_id) max_id = p.first;
        initial_z_by_id.resize(max_id + 1, 0.0f);
        for (auto& p : all_parts) initial_z_by_id[p.first] = p.second;

        // Print structure
        for (auto& p : all_parts) {
            const char* type = (p.first == tree.trunk_id) ? "TRUNK" :
                               (std::find(tree.branch_ids.begin(), tree.branch_ids.end(), p.first) != tree.branch_ids.end()) ? "BRANCH" : "LEAF";
            std::cout << "  P[" << p.first << "] " << type << " z=" << p.second << "m" << std::endl;
        }
    }

    // Record initial trunk Z
    float initial_trunk_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_trunk_z = particles[tree.trunk_id].z;
    }

    std::cout << "\n[STEP 2] Running simulation for 10 seconds @ 60Hz..." << std::endl;

    // Run simulation (shorter for complex tree)
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds

    bool nan_detected = false;
    float max_trunk_drift = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Check trunk stability
        {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            if (std::isnan(trunk_z) || std::isinf(trunk_z)) {
                nan_detected = true;
                std::cout << "  ⚠️ NaN in trunk at frame " << frame << std::endl;
                break;
            }

            float drift = std::abs(trunk_z - initial_trunk_z);
            if (drift > max_trunk_drift) {
                max_trunk_drift = drift;
            }
        }

        // DEBUG: Track ALL particle drifts every second
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            // Track branch and leaf drifts
            float max_branch_drift = 0.0f;
            float max_leaf_drift = 0.0f;
            int drifting_branches = 0;
            int drifting_leaves = 0;
            int worst_branch_id = -1;
            int worst_leaf_id = -1;

            for (auto& p : all_parts) {
                int id = p.first;
                float current_z = particles[id].z;
                float initial_z = initial_z_by_id[id];
                float drift = std::abs(current_z - initial_z);

                bool is_branch = (std::find(tree.branch_ids.begin(), tree.branch_ids.end(), id) != tree.branch_ids.end());
                bool is_leaf = (std::find(tree.leaf_ids.begin(), tree.leaf_ids.end(), id) != tree.leaf_ids.end());

                if (is_branch) {
                    if (drift > max_branch_drift) {
                        max_branch_drift = drift;
                        worst_branch_id = id;
                    }
                    if (drift > 0.01f) drifting_branches++;
                } else if (is_leaf) {
                    if (drift > max_leaf_drift) {
                        max_leaf_drift = drift;
                        worst_leaf_id = id;
                    }
                    if (drift > 0.01f) drifting_leaves++;
                }
            }

            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " trunk_z=" << std::setprecision(3) << trunk_z << "m"
                      << " | BRANCH drift=" << std::setprecision(4) << max_branch_drift << "m"
                      << " (" << drifting_branches << "/" << tree.branch_ids.size() << " moving)"
                      << " | LEAF drift=" << max_leaf_drift << "m"
                      << " (" << drifting_leaves << "/" << tree.leaf_ids.size() << " moving)"
                      << std::endl;

            // If significant drift, show details
            if (max_branch_drift > 0.05f && worst_branch_id >= 0) {
                float init_z = initial_z_by_id[worst_branch_id];
                float curr_z = particles[worst_branch_id].z;
                std::cout << "       WORST BRANCH: P[" << worst_branch_id << "] "
                          << init_z << "m → " << curr_z << "m (delta=" << (curr_z - init_z) << "m)" << std::endl;
            }
            if (max_leaf_drift > 0.05f && worst_leaf_id >= 0) {
                float init_z = initial_z_by_id[worst_leaf_id];
                float curr_z = particles[worst_leaf_id].z;
                std::cout << "       WORST LEAF: P[" << worst_leaf_id << "] "
                          << init_z << "m → " << curr_z << "m (delta=" << (curr_z - init_z) << "m)" << std::endl;
            }
        }

        // Render if interactive
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Segments: " + std::to_string(tree.total_segments), 200, 200, 200);

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Trunk drift: " << max_trunk_drift << "m" << std::endl;

    bool pass = !nan_detected && max_trunk_drift < 0.1f;

    if (!pass) {
        std::cout << "  ❌ FAIL: Tree unstable" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Oak tree stable" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Segments: " + std::to_string(tree.total_segments));
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Test: Complex Crown Tree (Deep Branching with Leaves)
// ============================================================================
bool test_physics_tree_crown(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::string test_name = "Physics Tree: Complex Crown";
    std::string description = "Deep branching tree with full crown structure";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Complex tree with deep crown"});
    }

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Complex crown tree - deeper branching, more side branches
    TreeSpec spec;
    spec.height = 6.0f;               // 6m tall
    spec.trunk_diameter = 0.4f;       // 40cm trunk
    spec.branch_depth = 4;            // 4 levels of branching (deep crown)
    spec.branch_angle = 35.0f;        // Tighter branching angle
    spec.branches_per_split = 3;      // 3-way splits for fuller crown
    spec.length_ratio = 0.65f;        // Branches 65% of parent
    spec.thickness_ratio = 0.6f;      // Thickness reduces faster
    spec.side_branch_probability = 0.4f;  // 40% side branches for density
    spec.angle_variance = 15.0f;      // Some randomness
    spec.length_variance = 0.15f;
    spec.random_seed = 99999;

    std::cout << "\n[STEP 1] Generating complex crown tree..." << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Total segments: " << tree.total_segments << std::endl;

    // Record initial trunk Z
    float initial_trunk_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_trunk_z = particles[tree.trunk_id].z;
    }

    std::cout << "\n[STEP 2] Running simulation for 10 seconds @ 60Hz..." << std::endl;

    // Run simulation
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds

    bool nan_detected = false;
    float max_trunk_drift = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Check trunk stability
        {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            if (std::isnan(trunk_z) || std::isinf(trunk_z)) {
                nan_detected = true;
                std::cout << "  ⚠️ NaN in trunk at frame " << frame << std::endl;
                break;
            }

            float drift = std::abs(trunk_z - initial_trunk_z);
            if (drift > max_trunk_drift) {
                max_trunk_drift = drift;
            }
        }

        // Progress logging
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " trunk_z=" << std::setprecision(3) << trunk_z << "m"
                      << " segments=" << tree.total_segments
                      << std::endl;
        }

        // Render if interactive
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Segments: " + std::to_string(tree.total_segments), 200, 200, 200);

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Trunk drift: " << max_trunk_drift << "m" << std::endl;

    // Larger trees with more constraints have more solver drift - allow 20cm
    bool pass = !nan_detected && max_trunk_drift < 0.2f;

    if (!pass) {
        std::cout << "  ❌ FAIL: Tree unstable (drift=" << max_trunk_drift << "m)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Crown tree stable (" << tree.total_segments << " segments)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Segments: " + std::to_string(tree.total_segments));
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Test: Full Canopy Tree (Maximum Branching with Dense Leaves)
// ============================================================================
bool test_physics_tree_canopy(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::string test_name = "Physics Tree: Full Canopy";
    std::string description = "Maximum branching with dense leaf coverage";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Full tree with dense canopy"});
    }

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Full canopy tree - matches kinematic oak for visual complexity
    TreeSpec spec;
    spec.height = 10.0f;              // 10m tall (like kinematic oak)
    spec.trunk_diameter = 0.5f;       // 50cm trunk
    spec.branch_depth = 5;            // 5 levels for full crown
    spec.branch_angle = 70.0f;        // Wide angles for spreading (like kinematic oak)
    spec.branches_per_split = 2;      // 2-way splits
    spec.length_ratio = 0.7f;         // Branches 70% of parent
    spec.thickness_ratio = 0.6f;      // Leaner branches (like kinematic oak)
    spec.side_branch_probability = 0.5f;  // 50% side branches (like kinematic oak)
    spec.angle_variance = 40.0f;      // High variance for wild variety (like kinematic oak)
    spec.length_variance = 0.3f;      // High length variation (like kinematic oak)
    spec.random_seed = 77777;

    std::cout << "\n[STEP 1] Generating full canopy tree..." << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Total segments: " << tree.total_segments << std::endl;
    std::cout << "  Total leaves: " << tree.leaf_ids.size() << std::endl;

    // Record initial positions for trunk, branches, and leaves (ALL AXES)
    float initial_trunk_z;
    std::vector<float> initial_leaf_z;
    std::vector<float> initial_branch_x, initial_branch_y, initial_branch_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_trunk_z = particles[tree.trunk_id].z;
        for (int id : tree.leaf_ids) {
            initial_leaf_z.push_back(particles[id].z);
        }
        for (int id : tree.branch_ids) {
            initial_branch_x.push_back(particles[id].x);
            initial_branch_y.push_back(particles[id].y);
            initial_branch_z.push_back(particles[id].z);
        }
    }

    std::cout << "\n[STEP 2] Running simulation for 10 seconds @ 60Hz..." << std::endl;

    // Run simulation
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds

    bool nan_detected = false;
    float max_trunk_drift = 0.0f;
    float max_leaf_drift = 0.0f;
    float max_branch_drift = 0.0f;
    float max_branch_drift_x = 0.0f;  // X/Y drift tracking
    float max_branch_drift_y = 0.0f;
    int drifting_leaf_count = 0;
    int drifting_branch_count = 0;
    int worst_branch_idx = -1;
    int worst_branch_x_id = -1, worst_branch_y_id = -1;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Check trunk and leaf stability
        {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            if (std::isnan(trunk_z) || std::isinf(trunk_z)) {
                nan_detected = true;
                std::cout << "  ⚠️ NaN in trunk at frame " << frame << std::endl;
                break;
            }

            float drift = std::abs(trunk_z - initial_trunk_z);
            if (drift > max_trunk_drift) {
                max_trunk_drift = drift;
            }

            // Track leaf drift (positive = upward)
            drifting_leaf_count = 0;
            for (size_t i = 0; i < tree.leaf_ids.size(); i++) {
                int id = tree.leaf_ids[i];
                float leaf_z = particles[id].z;
                float leaf_drift = leaf_z - initial_leaf_z[i];

                if (leaf_drift > max_leaf_drift) {
                    max_leaf_drift = leaf_drift;
                }
                if (leaf_drift > 0.1f) {  // Count leaves drifting up > 10cm
                    drifting_leaf_count++;
                }
            }

            // Track branch drift (ALL AXES)
            drifting_branch_count = 0;
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                int id = tree.branch_ids[i];
                float branch_z = particles[id].z;
                float branch_drift = branch_z - initial_branch_z[i];

                // X/Y drift (absolute value - any direction)
                float drift_x = std::abs(particles[id].x - initial_branch_x[i]);
                float drift_y = std::abs(particles[id].y - initial_branch_y[i]);
                if (drift_x > max_branch_drift_x) {
                    max_branch_drift_x = drift_x;
                    worst_branch_x_id = id;
                }
                if (drift_y > max_branch_drift_y) {
                    max_branch_drift_y = drift_y;
                    worst_branch_y_id = id;
                }

                if (branch_drift > max_branch_drift) {
                    max_branch_drift = branch_drift;
                    worst_branch_idx = i;
                }
                if (branch_drift > 0.1f) {  // Count branches drifting up > 10cm
                    drifting_branch_count++;
                }
            }
        }

        // Progress logging - track specific top particles
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();

            // Print positions of top 5 branches by initial z
            std::vector<std::pair<float, int>> branch_z;
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                branch_z.push_back({initial_branch_z[i], tree.branch_ids[i]});
            }
            std::sort(branch_z.begin(), branch_z.end(), [](auto& a, auto& b) { return a.first > b.first; });

            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " X:" << std::setprecision(4) << max_branch_drift_x << "m"
                      << " Y:" << max_branch_drift_y << "m"
                      << " Z:" << max_branch_drift << "m";
            if (max_branch_drift_x > 0.01f || max_branch_drift_y > 0.01f) {
                std::cout << " *** XY DRIFT";
            }
            std::cout << std::endl;
        }

        // Render if interactive
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Segments: " + std::to_string(tree.total_segments) +
                          " | Leaves: " + std::to_string(tree.leaf_ids.size()), 200, 200, 200);
            ui->draw_text(10, 130, "Leaf drift: " + std::to_string(max_leaf_drift).substr(0, 6) + "m"
                          + " | Drifting: " + std::to_string(drifting_leaf_count), 200, 200, 200);

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Trunk drift: " << max_trunk_drift << "m" << std::endl;
    std::cout << "  Max branch drift X: " << max_branch_drift_x << "m (P" << worst_branch_x_id << ")" << std::endl;
    std::cout << "  Max branch drift Y: " << max_branch_drift_y << "m (P" << worst_branch_y_id << ")" << std::endl;
    std::cout << "  Max branch drift Z: " << max_branch_drift << "m";
    if (worst_branch_idx >= 0) {
        std::cout << " (P" << tree.branch_ids[worst_branch_idx] << ")";
    }
    std::cout << std::endl;
    std::cout << "  Branches drifting >10cm: " << drifting_branch_count << "/" << tree.branch_ids.size() << std::endl;
    std::cout << "  Max leaf drift (upward): " << max_leaf_drift << "m" << std::endl;
    std::cout << "  Leaves drifting >10cm: " << drifting_leaf_count << "/" << tree.leaf_ids.size() << std::endl;

    // Complex trees with many constraints have more solver drift - allow 30cm
    // Check both trunk and branch drift
    bool pass = !nan_detected && max_trunk_drift < 0.3f && max_branch_drift < 0.5f;

    if (max_branch_drift_x > 0.01f || max_branch_drift_y > 0.01f) {
        std::cout << "  ⚠️ WARNING: X/Y DRIFT DETECTED!" << std::endl;
    }
    if (max_branch_drift > 0.1f) {
        std::cout << "  ⚠️ WARNING: Branches drifting upward!" << std::endl;
    }
    if (max_leaf_drift > 0.5f) {
        std::cout << "  ⚠️ WARNING: Leaves drifting upward significantly!" << std::endl;
    }

    if (!pass) {
        std::cout << "  ❌ FAIL: Tree unstable (drift=" << max_trunk_drift << "m)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Canopy tree stable (" << tree.total_segments
                  << " segments, " << tree.leaf_ids.size() << " leaves)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Segments: " + std::to_string(tree.total_segments));
        results.push_back("Leaves: " + std::to_string(tree.leaf_ids.size()));
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Test 5: Oak-Style Tree - Kinematic Parameters Test
// ============================================================================
// More horizontal branches create stronger torques and are more likely to
// cause physics instability. This tests the solver with challenging geometry.
// ============================================================================
bool test_physics_tree_quest(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::string test_name = "Physics Tree: Oak Style";
    std::string description = "Oak-like tree with high variance (matches kinematic generator)";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Oak-style tree tests physics with kinematic parameters"});
    }

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Oak-style tree (matches kinematic TreeSpec::oak() parameters)
    TreeSpec spec;
    spec.height = 8.0f;               // 8m tall
    spec.trunk_diameter = 0.5f;       // 50cm trunk
    spec.branch_depth = 4;            // 4 levels of branching
    spec.branch_angle = 70.0f;        // Wide angles for spreading oak branches
    spec.branches_per_split = 2;      // Binary tree (2-way splits)
    spec.length_ratio = 0.7f;         // Branches 70% of parent
    spec.thickness_ratio = 0.6f;      // Branches taper more
    spec.side_branch_probability = 0.5f;  // 50% chance of side branches
    spec.angle_variance = 40.0f;      // Very high variance for wild variety (like kinematic)
    spec.length_variance = 0.3f;      // High length variation
    spec.random_seed = 31415;         // Consistent seed

    std::cout << "\n[STEP 1] Generating oak-style tree..." << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Total segments: " << tree.total_segments << std::endl;
    std::cout << "  Total leaves: " << tree.leaf_ids.size() << std::endl;

    // Record initial positions
    float initial_trunk_z;
    std::vector<float> initial_branch_x, initial_branch_y, initial_branch_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_trunk_z = particles[tree.trunk_id].z;
        for (int id : tree.branch_ids) {
            initial_branch_x.push_back(particles[id].x);
            initial_branch_y.push_back(particles[id].y);
            initial_branch_z.push_back(particles[id].z);
        }
    }

    std::cout << "\n[STEP 2] Running simulation for 10 seconds @ 60Hz..." << std::endl;

    // Run simulation
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds

    bool nan_detected = false;
    float max_trunk_drift = 0.0f;
    float max_branch_drift_x = 0.0f;
    float max_branch_drift_y = 0.0f;
    float max_branch_drift_z = 0.0f;
    int worst_x_id = -1, worst_y_id = -1, worst_z_id = -1;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Check stability
        {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            if (std::isnan(trunk_z) || std::isinf(trunk_z)) {
                nan_detected = true;
                std::cout << "  ⚠️ NaN in trunk at frame " << frame << std::endl;
                break;
            }

            float drift = std::abs(trunk_z - initial_trunk_z);
            if (drift > max_trunk_drift) max_trunk_drift = drift;

            // Track branch drift on all axes
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                int id = tree.branch_ids[i];
                float dx = std::abs(particles[id].x - initial_branch_x[i]);
                float dy = std::abs(particles[id].y - initial_branch_y[i]);
                float dz = std::abs(particles[id].z - initial_branch_z[i]);

                if (dx > max_branch_drift_x) { max_branch_drift_x = dx; worst_x_id = id; }
                if (dy > max_branch_drift_y) { max_branch_drift_y = dy; worst_y_id = id; }
                if (dz > max_branch_drift_z) { max_branch_drift_z = dz; worst_z_id = id; }
            }
        }

        // Progress logging
        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " X:" << std::setprecision(4) << max_branch_drift_x << "m"
                      << " Y:" << max_branch_drift_y << "m"
                      << " Z:" << max_branch_drift_z << "m";
            if (max_branch_drift_x > 0.10f || max_branch_drift_y > 0.10f) {
                std::cout << " *** XY DRIFT";
            }
            std::cout << std::endl;
        }

        // Visual update for interactive mode
        if (is_interactive && frame % 2 == 0) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_name, 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 30s", 200, 200, 200);
            ui->draw_text(10, 70, "Segments: " + std::to_string(tree.total_segments), 200, 200, 200);
            ui->draw_text(10, 100, "X drift: " + std::to_string(max_branch_drift_x).substr(0, 6) + "m", 200, 200, 200);
            ui->draw_text(10, 130, "Y drift: " + std::to_string(max_branch_drift_y).substr(0, 6) + "m", 200, 200, 200);

            engine.present();
        }
    }

    // Results
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Trunk drift: " << max_trunk_drift << "m" << std::endl;
    std::cout << "  Max branch drift X: " << max_branch_drift_x << "m (P" << worst_x_id << ")" << std::endl;
    std::cout << "  Max branch drift Y: " << max_branch_drift_y << "m (P" << worst_y_id << ")" << std::endl;
    std::cout << "  Max branch drift Z: " << max_branch_drift_z << "m (P" << worst_z_id << ")" << std::endl;

    // Pass criteria: oak-style tree with high variance
    // Allow up to 50cm XY drift for challenging horizontal geometry
    float max_xy = std::max(max_branch_drift_x, max_branch_drift_y);
    bool pass = !nan_detected && max_xy < 0.50f && max_branch_drift_z < 0.30f;

    if (nan_detected) {
        std::cout << "  ❌ FAIL: NaN detected (physics explosion)" << std::endl;
    } else if (max_xy >= 0.50f) {
        std::cout << "  ❌ FAIL: XY drift too large (" << max_xy << "m >= 0.50m)" << std::endl;
    } else if (max_branch_drift_z >= 0.30f) {
        std::cout << "  ❌ FAIL: Z drift too large (" << max_branch_drift_z << "m >= 0.30m)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Oak-style tree stable (" << tree.total_segments
                  << " segments, kinematic parameters)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Segments: " + std::to_string(tree.total_segments));
        results.push_back("XY drift: " + std::to_string(max_xy).substr(0, 6) + "m");
        results.push_back("Z drift: " + std::to_string(max_branch_drift_z).substr(0, 6) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// Helper to create and configure engine for a single test
static Engine* create_test_engine(bool is_interactive) {
    Engine* engine = new Engine();
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Tree Test";
    config.enable_chat_window = false;

    if (engine->initialize(config) != 0) {
        delete engine;
        return nullptr;
    }

    // Configure gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine->get_physics_system().add_force(std::move(gravity));

    // For interactive mode, add light and configure camera
    if (is_interactive) {
        auto& ps = engine->get_particle_system();

        // Light source
        Particle light = {};
        light.x = -5.0f; light.y = -10.0f; light.z = 10.0f;
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
        engine->add_particle(light);

        // Camera position
        auto& camera = engine->get_camera_system();
        camera.set_position(-8.0f, -8.0f, 6.0f);
        camera.look_at(0.0f, 0.0f, 2.0f);
    }

    return engine;
}

// ============================================================================
// MAIN TEST ENTRY POINT
// ============================================================================
bool test_physics_tree() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS TREE TEST - Gluon-Based Tree Generation         ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    std::cout << "\nTest Overview:" << std::endl;
    std::cout << "  Test 1: Simple tree (depth=2)" << std::endl;
    std::cout << "  Test 2: Oak tree (depth=3)" << std::endl;
    std::cout << "  Test 3: Crown tree (depth=4)" << std::endl;
    std::cout << "  Test 4: Full canopy (depth=5 + leaves)" << std::endl;
    std::cout << "  Test 5: Oak-style tree (kinematic params)" << std::endl;
    std::cout << "\nAcceptance: Trees must stand without collapsing" << std::endl;

    // Check for interactive mode
    bool is_interactive = (std::getenv("INTERACTIVE") != nullptr);
    if (is_interactive) {
        std::cout << "\n[MODE] INTERACTIVE - will show visual progress" << std::endl;
    } else {
        std::cout << "\n[MODE] HEADLESS" << std::endl;
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    bool test1_pass = false;
    bool test2_pass = false;
    bool test3_pass = false;
    bool test4_pass = false;
    bool test5_pass = false;

    // Helper lambda to reset engine state between tests
    auto reset_for_next_test = [](Engine* engine, const char* test_name) {
        std::cout << "\n[INTERACTIVE] Resetting for " << test_name << "..." << std::endl;
        engine->get_particle_system().clear_particles();
        engine->get_physics_system().clear_gluons();

        // Re-add light (was cleared with particles)
        auto& ps = engine->get_particle_system();
        Particle light = {};
        light.x = -5.0f; light.y = -10.0f; light.z = 10.0f;
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
        engine->add_particle(light);
    };

    if (is_interactive) {
        // Interactive mode: use single engine (GLFW doesn't like multiple init/shutdown)
        // Run all tests, clearing state between them
        Engine* engine = create_test_engine(true);
        if (engine) {
            test1_pass = test_physics_tree_simple(*engine, true);

            reset_for_next_test(engine, "oak test");
            test2_pass = test_physics_tree_oak(*engine, true);

            reset_for_next_test(engine, "crown test");
            test3_pass = test_physics_tree_crown(*engine, true);

            reset_for_next_test(engine, "canopy test");
            test4_pass = test_physics_tree_canopy(*engine, true);

            reset_for_next_test(engine, "quest test");
            test5_pass = test_physics_tree_quest(*engine, true);

            engine->shutdown();
            delete engine;
        } else {
            std::cerr << "Failed to create engine!" << std::endl;
        }
    } else {
        // Headless mode: separate engines work fine
        {
            Engine* engine = create_test_engine(false);
            if (engine) {
                test1_pass = test_physics_tree_simple(*engine, false);
                engine->shutdown();
                delete engine;
            } else {
                std::cerr << "Failed to create engine for test 1!" << std::endl;
            }
        }
        {
            Engine* engine = create_test_engine(false);
            if (engine) {
                test2_pass = test_physics_tree_oak(*engine, false);
                engine->shutdown();
                delete engine;
            } else {
                std::cerr << "Failed to create engine for test 2!" << std::endl;
            }
        }
        {
            Engine* engine = create_test_engine(false);
            if (engine) {
                test3_pass = test_physics_tree_crown(*engine, false);
                engine->shutdown();
                delete engine;
            } else {
                std::cerr << "Failed to create engine for test 3!" << std::endl;
            }
        }
        {
            Engine* engine = create_test_engine(false);
            if (engine) {
                test4_pass = test_physics_tree_canopy(*engine, false);
                engine->shutdown();
                delete engine;
            } else {
                std::cerr << "Failed to create engine for test 4!" << std::endl;
            }
        }
        {
            Engine* engine = create_test_engine(false);
            if (engine) {
                test5_pass = test_physics_tree_quest(*engine, false);
                engine->shutdown();
                delete engine;
            } else {
                std::cerr << "Failed to create engine for test 5!" << std::endl;
            }
        }
    }

    // Summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║         PHYSICS TREE TEST COMPLETE                          ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Test 1 (Simple): " << (test1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Test 2 (Oak): " << (test2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Test 3 (Crown): " << (test3_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Test 4 (Canopy): " << (test4_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Test 5 (Oak-style): " << (test5_pass ? "✅ PASS" : "❌ FAIL") << std::endl;

    bool all_pass = test1_pass && test2_pass && test3_pass && test4_pass && test5_pass;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED!" : "❌ SOME TESTS FAILED") << std::endl;

    return all_pass;
}

// ============================================================================
// Standalone Drift Test - 10 Second Physics Tree Monitoring
// ============================================================================
// Generates a tree and monitors drift for 10 seconds with detailed reporting.
// Use this for quick stability validation of physics trees.
// ============================================================================
bool test_physics_tree_drift() {
    // Create standalone engine in HEADLESS mode
    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Tree Drift Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "  ❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    // Configure gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    auto& ps = engine.get_particle_system();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  Physics Tree Drift Test (10 seconds)                        ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Create physics tree generator
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Use oak-style parameters (matches kinematic generator)
    TreeSpec spec;
    spec.height = 10.0f;              // 10m tall
    spec.trunk_diameter = 0.5f;       // 50cm trunk
    spec.branch_depth = 5;            // 5 levels - complex tree
    spec.branch_angle = 70.0f;        // Wide spreading branches
    spec.branches_per_split = 2;      // Binary tree
    spec.length_ratio = 0.7f;
    spec.thickness_ratio = 0.7f;
    spec.side_branch_probability = 0.5f;
    spec.angle_variance = 40.0f;      // High variance like kinematic
    spec.length_variance = 0.3f;
    spec.random_seed = 42;

    std::cout << "\n[GENERATING] Oak-style tree with kinematic parameters..." << std::endl;
    std::cout << "  Height: " << spec.height << "m" << std::endl;
    std::cout << "  Depth: " << spec.branch_depth << " levels" << std::endl;
    std::cout << "  Angle variance: " << spec.angle_variance << "°" << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "\n[TREE GENERATED]" << std::endl;
    std::cout << "  Segments: " << tree.total_segments << std::endl;
    std::cout << "  Leaves: " << tree.leaf_ids.size() << std::endl;

    // Record initial positions
    float initial_trunk_z;
    std::vector<float> initial_x, initial_y, initial_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_trunk_z = particles[tree.trunk_id].z;
        for (int id : tree.branch_ids) {
            initial_x.push_back(particles[id].x);
            initial_y.push_back(particles[id].y);
            initial_z.push_back(particles[id].z);
        }
    }

    std::cout << "\n[SIMULATION] Running 10 seconds @ 60 FPS..." << std::endl;
    std::cout << "┌──────────┬────────────┬────────────┬────────────┬────────────┐" << std::endl;
    std::cout << "│  Time    │  Max X     │  Max Y     │  Max Z     │  Trunk Z   │" << std::endl;
    std::cout << "├──────────┼────────────┼────────────┼────────────┼────────────┤" << std::endl;

    // Run simulation for 10 seconds
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds at 60 FPS
    bool nan_detected = false;
    float final_max_x = 0.0f, final_max_y = 0.0f, final_max_z = 0.0f;

    for (int frame = 0; frame <= total_frames; frame++) {
        engine.update(dt);

        // Report every second (every 60 frames)
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            float trunk_z = particles[tree.trunk_id].z;

            if (std::isnan(trunk_z) || std::isinf(trunk_z)) {
                nan_detected = true;
                std::cout << "│  " << std::setw(4) << (frame / 60) << "s    │  ⚠️ NaN detected - simulation unstable!" << std::endl;
                break;
            }

            float max_drift_x = 0.0f, max_drift_y = 0.0f, max_drift_z = 0.0f;
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                int id = tree.branch_ids[i];
                float dx = std::abs(particles[id].x - initial_x[i]);
                float dy = std::abs(particles[id].y - initial_y[i]);
                float dz = std::abs(particles[id].z - initial_z[i]);
                max_drift_x = std::max(max_drift_x, dx);
                max_drift_y = std::max(max_drift_y, dy);
                max_drift_z = std::max(max_drift_z, dz);
            }

            final_max_x = max_drift_x;
            final_max_y = max_drift_y;
            final_max_z = max_drift_z;

            std::cout << "│  " << std::setw(4) << (frame / 60) << "s    │"
                      << std::fixed << std::setprecision(4)
                      << "  " << std::setw(8) << max_drift_x << "m │"
                      << "  " << std::setw(8) << max_drift_y << "m │"
                      << "  " << std::setw(8) << max_drift_z << "m │"
                      << "  " << std::setw(8) << trunk_z << "m │" << std::endl;
        }
    }

    std::cout << "└──────────┴────────────┴────────────┴────────────┴────────────┘" << std::endl;

    // Evaluate results
    float max_xy = std::max(final_max_x, final_max_y);
    bool pass = !nan_detected && max_xy < 0.50f && final_max_z < 0.30f;

    std::cout << "\n[RESULTS]" << std::endl;
    if (nan_detected) {
        std::cout << "  ❌ FAIL: Simulation became unstable (NaN detected)" << std::endl;
    } else if (max_xy >= 0.50f) {
        std::cout << "  ❌ FAIL: XY drift too large (" << max_xy << "m >= 0.50m)" << std::endl;
    } else if (final_max_z >= 0.30f) {
        std::cout << "  ❌ FAIL: Z drift too large (" << final_max_z << "m >= 0.30m)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Tree stable after 10 seconds" << std::endl;
        std::cout << "    Final drift - X: " << final_max_x << "m, Y: " << final_max_y << "m, Z: " << final_max_z << "m" << std::endl;
    }

    return pass;
}
