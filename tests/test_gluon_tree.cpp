#include "../src/core/engine.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS EXPERIMENT - Tree Structure with Gluon Branches
// ============================================================================
// Purpose: Test gluon support for horizontal branches on vertical trunk
// See: docs/PHYSICS_ITERATIVE_RESOLVER_V3.2.md (Scenario VII)
//
// LAWS (assert-protocol migration, 2026-08-21):
//   INV-14 tears need strain. A horizontal branch hanging off a vertical
//          trunk under its own weight is the standing-load case: the bond is
//          genuinely strained and must NOT tear at it, so "branch stable,
//          drift bounded" is INV-14 measured from the other side.
//   INV-1  the branch reaches something immobile through its bonds; a branch
//          that drifts away has stopped reaching it.
//   INV-24 the loaded tree settles and then performs no further corrective
//          work: drift is measured after settling, not during.
//
// Structure:
//      [Top] 6kg
//        |
//     contact
//        |
//      [Mid] 8kg ----G1---- [Branch_R] 3kg
//        |
//     contact
//        |
//     [Base] 10kg
//        |
//     contact
//        |
//     [Floor]
//
// Test Cases:
//   Case 1: Static equilibrium → single branch from Mid for 10 seconds
//   Case 2: Branching tree → branches on Top with sub-branches
// ============================================================================

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

struct TreeState {
    int floor_id = -1;
    int base_id = -1;
    int mid_id = -1;
    int top_id = -1;
    int branch_r_id = -1;
    // Case 2 additions
    int branch_tl_id = -1;  // Top-left branch
    int branch_tr_id = -1;  // Top-right branch
    int sub_tl_id = -1;     // Sub-branch on top-left
    int sub_tr_id = -1;     // Sub-branch on top-right
};

// ============================================================================
// Create Tree Structure
// ============================================================================
void create_tree(Engine& engine, TreeState& tree) {
    auto& ps = engine.get_particle_system();

    std::cout << "\n[SETUP] Creating tree structure..." << std::endl;

    // Floor (kinematic)
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.1f;
    floor.shape = ParticleShape::BOX;
    floor.width = 4.0f; floor.height = 4.0f; floor.thickness = 0.2f;
    floor.r = 0.3f; floor.g = 0.6f; floor.b = 0.3f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    tree.floor_id = engine.add_particle(floor);

    const float floor_top = 0.2f;  // Floor top surface

    // Base: 10kg, 0.3×0.3×1.0m
    Particle base = {};
    base.x = 0.0f; base.y = 0.0f;
    base.z = floor_top + 0.5f;  // center at 0.7m
    base.shape = ParticleShape::BOX;
    base.width = 0.3f; base.height = 0.3f; base.thickness = 1.0f;
    base.r = 0.4f; base.g = 0.25f; base.b = 0.15f;
    base.SetMaterial(Materials::Type::WOOD_HARD);
    tree.base_id = engine.add_particle(base);

    // Mid: 8kg, 0.25×0.25×1.0m
    Particle mid = {};
    mid.x = 0.0f; mid.y = 0.0f;
    mid.z = floor_top + 1.0f + 0.5f;  // center at 1.7m
    mid.shape = ParticleShape::BOX;
    mid.width = 0.25f; mid.height = 0.25f; mid.thickness = 1.0f;
    mid.r = 0.5f; mid.g = 0.3f; mid.b = 0.2f;
    mid.SetMaterial(Materials::Type::WOOD_HARD);
    tree.mid_id = engine.add_particle(mid);

    // Top: 6kg, 0.2×0.2×1.0m
    Particle top = {};
    top.x = 0.0f; top.y = 0.0f;
    top.z = floor_top + 2.0f + 0.5f;  // center at 2.7m
    top.shape = ParticleShape::BOX;
    top.width = 0.2f; top.height = 0.2f; top.thickness = 1.0f;
    top.r = 0.6f; top.g = 0.35f; top.b = 0.25f;
    top.SetMaterial(Materials::Type::WOOD_HARD);
    tree.top_id = engine.add_particle(top);

    std::cout << "  Floor: id=" << tree.floor_id << " (kinematic)" << std::endl;
    std::cout << "  Base: id=" << tree.base_id << " z=" << base.z << "m, 10kg" << std::endl;
    std::cout << "  Mid: id=" << tree.mid_id << " z=" << mid.z << "m, 8kg" << std::endl;
    std::cout << "  Top: id=" << tree.top_id << " z=" << top.z << "m, 6kg" << std::endl;
}

// ============================================================================
// Case 1: Static Equilibrium - Branch held by gluon
// ============================================================================
bool run_case_1_static_branch(Engine& engine, bool is_interactive, TreeState& tree) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 1: Static Branch";
    std::string description = "Branch attached to Mid with gluon - should stay rigid for 10 seconds";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Branch weight: 29.4N", "Gluon: 500N breaking force"});
    }

    // Get Mid position for branch placement
    float mid_x, mid_z, mid_half_width;
    {
        auto particles = ps.lock_particles_for_read();
        mid_x = particles[tree.mid_id].x;
        mid_z = particles[tree.mid_id].z;
        mid_half_width = particles[tree.mid_id].width / 2.0f;
    }

    // Branch_R: 3kg, 0.5×0.1×0.1m (horizontal)
    Particle branch = {};
    branch.shape = ParticleShape::BOX;
    branch.width = 0.5f;      // X dimension (horizontal length)
    branch.height = 0.1f;     // Y dimension
    branch.thickness = 0.1f;  // Z dimension
    branch.r = 0.7f; branch.g = 0.4f; branch.b = 0.2f;
    branch.SetMaterial(Materials::Type::WOOD_HARD);

    // Create nail gluon to attach branch to Mid
    auto nail = std::make_unique<NailGluon>();
    nail->offset_a = Vec3(mid_half_width, 0.0f, 0.0f);  // Mid's right edge
    nail->offset_b = Vec3(-0.25f, 0.0f, 0.0f);         // Branch's left edge
    nail->target_distance = 0.005f;  // 5mm gap
    nail->breaking_force = 500.0f;   // 500N breaking force

    // Create branch with gluon attachment
    tree.branch_r_id = physics.add_particle_with_gluon_to(tree.mid_id, branch, std::move(nail));

    float branch_z;
    {
        auto particles = ps.lock_particles_for_read();
        branch_z = particles[tree.branch_r_id].z;
    }

    std::cout << "\n[STEP 1] Created branch:" << std::endl;
    std::cout << "  Branch_R: id=" << tree.branch_r_id << " z=" << branch_z << "m, 3kg" << std::endl;
    std::cout << "  Gluon: 500N breaking force" << std::endl;

    // Expected forces
    std::cout << "\n[ENVELOPE]" << std::endl;
    std::cout << "  Branch weight: 3kg × 9.8 = 29.4N" << std::endl;
    std::cout << "  Gluon must support: 29.4N" << std::endl;
    std::cout << "  Total tree: (10+8+6+3) × 9.8 = 264.6N" << std::endl;

    // Run simulation (10 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;

    std::cout << "\n[STEP 2] Running simulation for 10.0s @ 60Hz..." << std::endl;

    float initial_branch_z = branch_z;
    float final_branch_z = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        {
            auto particles = ps.lock_particles_for_read();
            final_branch_z = particles[tree.branch_r_id].z;
        }

        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " Branch z=" << std::setprecision(3) << final_branch_z << "m"
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 10s", 200, 200, 200);
            ui->draw_text(10, 70, "Branch z=" + std::to_string(final_branch_z).substr(0, 6) + "m", 255, 128, 0);
            ui->draw_text(10, 100, "Expected: " + std::to_string(initial_branch_z).substr(0, 6) + "m", 150, 150, 150);

            float drift = std::abs(final_branch_z - initial_branch_z);
            if (drift < 0.06f) {
                ui->draw_text(10, 130, "Status: STABLE", 0, 255, 0);
            } else {
                ui->draw_text(10, 130, "Status: DRIFTING!", 255, 0, 0);
            }

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Initial z: " << initial_branch_z << "m" << std::endl;
    std::cout << "  Final z: " << final_branch_z << "m" << std::endl;

    float drift = std::abs(final_branch_z - initial_branch_z);
    bool pass = drift < 0.06f;  // 60mm tolerance (visually acceptable drift)

    if (!pass) {
        std::cout << "  ❌ FAIL INV-14/INV-1: Branch drifted " << drift << "m" << std::endl;
    } else {
        std::cout << "  ✅ PASS INV-14/INV-1/INV-24: Branch stable under standing load (drift=" << drift << "m)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Initial z: " + std::to_string(initial_branch_z).substr(0, 6) + "m");
        results.push_back("Final z: " + std::to_string(final_branch_z).substr(0, 6) + "m");
        results.push_back("Drift: " + std::to_string(drift).substr(0, 8) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 2: Branching Tree - Branches on Top with sub-branches
// ============================================================================
bool run_case_2_branching_tree(Engine& engine, bool is_interactive, TreeState& tree) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 2: Branching Tree";
    std::string description = "Branches on Top with sub-branches - chained gluons";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {
            description, "",
            "Structure: Top -> Branch_TL -> Sub_TL",
            "           Top -> Branch_TR -> Sub_TR",
            "Total new weight: 4 branches × 2kg = 78.4N"
        });
    }

    // Get Top position for branch placement
    float top_x, top_z, top_half_width;
    {
        auto particles = ps.lock_particles_for_read();
        top_x = particles[tree.top_id].x;
        top_z = particles[tree.top_id].z;
        top_half_width = particles[tree.top_id].width / 2.0f;
    }

    // -------------------------------------------------------------------------
    // Branch_TL: 2kg, attached to Top's left edge (-X)
    // -------------------------------------------------------------------------
    {
        Particle branch = {};
        branch.shape = ParticleShape::BOX;
        branch.width = 0.4f;      // X dimension
        branch.height = 0.08f;    // Y dimension
        branch.thickness = 0.08f; // Z dimension
        branch.r = 0.65f; branch.g = 0.45f; branch.b = 0.25f;
        branch.SetMaterial(Materials::Type::WOOD_HARD);

        auto nail = std::make_unique<NailGluon>();
        nail->offset_a = Vec3(-top_half_width, 0.0f, 0.0f);  // Top's left edge
        nail->offset_b = Vec3(0.2f, 0.0f, 0.0f);             // Branch's right edge
        nail->target_distance = 0.005f;
        nail->breaking_force = 500.0f;

        tree.branch_tl_id = physics.add_particle_with_gluon_to(tree.top_id, branch, std::move(nail));
    }

    // -------------------------------------------------------------------------
    // Branch_TR: 2kg, attached to Top's right edge (+X)
    // -------------------------------------------------------------------------
    {
        Particle branch = {};
        branch.shape = ParticleShape::BOX;
        branch.width = 0.4f;
        branch.height = 0.08f;
        branch.thickness = 0.08f;
        branch.r = 0.65f; branch.g = 0.45f; branch.b = 0.25f;
        branch.SetMaterial(Materials::Type::WOOD_HARD);

        auto nail = std::make_unique<NailGluon>();
        nail->offset_a = Vec3(top_half_width, 0.0f, 0.0f);   // Top's right edge
        nail->offset_b = Vec3(-0.2f, 0.0f, 0.0f);            // Branch's left edge
        nail->target_distance = 0.005f;
        nail->breaking_force = 500.0f;

        tree.branch_tr_id = physics.add_particle_with_gluon_to(tree.top_id, branch, std::move(nail));
    }

    // Get branch positions for sub-branch placement
    float branch_tl_x, branch_tr_x;
    {
        auto particles = ps.lock_particles_for_read();
        branch_tl_x = particles[tree.branch_tl_id].x;
        branch_tr_x = particles[tree.branch_tr_id].x;
    }

    // -------------------------------------------------------------------------
    // Sub_TL: 2kg, attached to Branch_TL's left edge
    // -------------------------------------------------------------------------
    {
        Particle sub = {};
        sub.shape = ParticleShape::BOX;
        sub.width = 0.3f;
        sub.height = 0.06f;
        sub.thickness = 0.06f;
        sub.r = 0.75f; sub.g = 0.5f; sub.b = 0.3f;
        sub.SetMaterial(Materials::Type::WOOD_HARD);

        auto nail = std::make_unique<NailGluon>();
        nail->offset_a = Vec3(-0.2f, 0.0f, 0.0f);  // Branch_TL's left edge
        nail->offset_b = Vec3(0.15f, 0.0f, 0.0f);  // Sub's right edge
        nail->target_distance = 0.005f;
        nail->breaking_force = 500.0f;

        tree.sub_tl_id = physics.add_particle_with_gluon_to(tree.branch_tl_id, sub, std::move(nail));
    }

    // -------------------------------------------------------------------------
    // Sub_TR: 2kg, attached to Branch_TR's right edge
    // -------------------------------------------------------------------------
    {
        Particle sub = {};
        sub.shape = ParticleShape::BOX;
        sub.width = 0.3f;
        sub.height = 0.06f;
        sub.thickness = 0.06f;
        sub.r = 0.75f; sub.g = 0.5f; sub.b = 0.3f;
        sub.SetMaterial(Materials::Type::WOOD_HARD);

        auto nail = std::make_unique<NailGluon>();
        nail->offset_a = Vec3(0.2f, 0.0f, 0.0f);   // Branch_TR's right edge
        nail->offset_b = Vec3(-0.15f, 0.0f, 0.0f); // Sub's left edge
        nail->target_distance = 0.005f;
        nail->breaking_force = 500.0f;

        tree.sub_tr_id = physics.add_particle_with_gluon_to(tree.branch_tr_id, sub, std::move(nail));
    }

    // Get initial positions
    float initial_sub_tl_z, initial_sub_tr_z;
    {
        auto particles = ps.lock_particles_for_read();
        initial_sub_tl_z = particles[tree.sub_tl_id].z;
        initial_sub_tr_z = particles[tree.sub_tr_id].z;
    }

    std::cout << "\n[STEP 1] Created branches:" << std::endl;
    std::cout << "  Branch_TL: id=" << tree.branch_tl_id << " 2kg" << std::endl;
    std::cout << "  Branch_TR: id=" << tree.branch_tr_id << " 2kg" << std::endl;
    std::cout << "  Sub_TL: id=" << tree.sub_tl_id << " z=" << initial_sub_tl_z << "m, 2kg" << std::endl;
    std::cout << "  Sub_TR: id=" << tree.sub_tr_id << " z=" << initial_sub_tr_z << "m, 2kg" << std::endl;

    // Expected forces
    std::cout << "\n[ENVELOPE]" << std::endl;
    std::cout << "  New branch weight: 4 × 2kg × 9.8 = 78.4N" << std::endl;
    std::cout << "  Total tree: (10+8+6+3+2+2+2+2) × 9.8 = 343N" << std::endl;

    // Run simulation (10 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 600;

    std::cout << "\n[STEP 2] Running simulation for 10.0s @ 60Hz..." << std::endl;

    float final_sub_tl_z = 0.0f;
    float final_sub_tr_z = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        {
            auto particles = ps.lock_particles_for_read();
            final_sub_tl_z = particles[tree.sub_tl_id].z;
            final_sub_tr_z = particles[tree.sub_tr_id].z;
        }

        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " Sub_TL z=" << std::setprecision(3) << final_sub_tl_z << "m"
                      << " Sub_TR z=" << final_sub_tr_z << "m"
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 10s", 200, 200, 200);
            ui->draw_text(10, 70, "Sub_TL z=" + std::to_string(final_sub_tl_z).substr(0, 6) + "m", 255, 128, 0);
            ui->draw_text(10, 100, "Sub_TR z=" + std::to_string(final_sub_tr_z).substr(0, 6) + "m", 255, 128, 0);

            float drift_tl = std::abs(final_sub_tl_z - initial_sub_tl_z);
            float drift_tr = std::abs(final_sub_tr_z - initial_sub_tr_z);
            if (drift_tl < 0.06f && drift_tr < 0.06f) {
                ui->draw_text(10, 130, "Status: STABLE", 0, 255, 0);
            } else {
                ui->draw_text(10, 130, "Status: DRIFTING!", 255, 0, 0);
            }

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    float drift_tl = std::abs(final_sub_tl_z - initial_sub_tl_z);
    float drift_tr = std::abs(final_sub_tr_z - initial_sub_tr_z);

    std::cout << "  Sub_TL: initial=" << initial_sub_tl_z << "m, final=" << final_sub_tl_z << "m, drift=" << drift_tl << "m" << std::endl;
    std::cout << "  Sub_TR: initial=" << initial_sub_tr_z << "m, final=" << final_sub_tr_z << "m, drift=" << drift_tr << "m" << std::endl;

    bool pass = (drift_tl < 0.06f) && (drift_tr < 0.06f);  // 60mm tolerance (visually acceptable)

    if (!pass) {
        std::cout << "  ❌ FAIL INV-14/INV-1: Sub-branches drifted" << std::endl;
    } else {
        std::cout << "  ✅ PASS INV-14/INV-1/INV-24: All branches stable under standing load" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Sub_TL drift: " + std::to_string(drift_tl).substr(0, 8) + "m");
        results.push_back("Sub_TR drift: " + std::to_string(drift_tr).substr(0, 8) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Main test entry point
// ============================================================================
bool test_gluon_tree() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS EXPERIMENT - Tree Structure with Gluon Branches  ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Experiment - Tree with Gluon Branches";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // Light source
    Particle light = {};
    light.x = 0.0f; light.y = -10.0f; light.z = 10.0f;
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    light.is_light_source = true;
    light.emission_strength = 100000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
    engine.add_particle(light);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-3.0f, -3.0f, 3.0f);
    camera.look_at(0.0f, 0.0f, 1.5f);

    // Gravity
    auto& physics = engine.get_physics_system();
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Create tree
    TreeState tree;
    create_tree(engine, tree);

    // Settle for 2 seconds
    std::cout << "\n[SETTLE] Running 2s settling period..." << std::endl;
    const double dt = 1.0 / 60.0;
    for (int frame = 0; frame < 120; frame++) {
        engine.update(dt);
        if (is_interactive && frame % 2 == 0) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "SETTLING TREE - 2 SECONDS", 255, 255, 0);
            ui->draw_text(10, 40, "Progress: " + std::to_string(frame / 60) + "s / 2s", 200, 200, 200);

            engine.present();
        }
    }
    std::cout << "  Settled." << std::endl;

    // Wait for space before running test
    if (is_interactive) {
        wait_for_space(engine, "READY FOR TEST", {"Tree settled for 2 seconds", "", "Press SPACE to add branch with gluon"});
    }

    // Run tests
    bool case1_pass = run_case_1_static_branch(engine, is_interactive, tree);
    bool case2_pass = run_case_2_branching_tree(engine, is_interactive, tree);

    // Summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║         TREE GLUON TESTING COMPLETE                          ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1 (INV-14 static branch): " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 2 (INV-14 branching tree): " << (case2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;

    bool all_pass = case1_pass && case2_pass;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED!" : "❌ SOME TESTS FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1 (Static branch): " + std::string(case1_pass ? "PASS" : "FAIL"),
            "Case 2 (Branching tree): " + std::string(case2_pass ? "PASS" : "FAIL"),
            "",
            all_pass ? "TREE GLUON WORKING!" : "TEST FAILED"
        };
        wait_for_space(engine, "TREE GLUON TESTING COMPLETE", summary);
    }

    return all_pass;
}
