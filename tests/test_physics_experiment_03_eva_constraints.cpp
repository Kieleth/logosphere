#include "../src/core/engine.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>

// ============================================================================
// PHYSICS EXPERIMENT 03 - Eva Body Parts + Constraints (STRUCTURAL INTEGRITY)
// ============================================================================
// Purpose: Test structural integrity system from docs/entity_structural_integrity.md
// Strategy: Build Eva entity INCREMENTALLY, testing joint constraints at each step
//
// Constraints model JOINTS (ankle, knee, hip, shoulder, elbow, etc.)
// NOT arbitrary links between separate body parts
//
// Test Cases (incremental, additive):
//   Case 1: 2 feet on floor (NO constraints)
//           → Test: Feet don't penetrate floor, stand independently
//
//   Case 2: Keep feet + Add 2 shins + 2 constraints (foot→shin per leg)
//           → Test: Ankle joints maintain connection, legs don't collapse
//
//   Case 3: Keep all + Add 2 thighs + 2 constraints (shin→thigh per leg)
//           → Test: Knee joints work, full legs maintain structure
//
//   ... etc for hips, abdomen, chest, shoulders, arms, neck, head
//
// Expected per case:
//   - NO PENETRATION (z >= floor_surface for all particles)
//   - CONSTRAINTS MAINTAINED (distance error < 10% rest_distance)
//   - NO COLLAPSE (structure doesn't fall apart)
//   - NO INFINITE STRETCH (constraints don't break)
//
// See: docs/entity_structural_integrity.md
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
    std::cout.flush();
}

// ============================================================================
// Persistent State: Floor and body parts stay across test cases
// ============================================================================
struct BodyPartIDs {
    std::vector<int> floor_ids;
    int left_foot_id = -1;
    int right_foot_id = -1;
    int left_shin_id = -1;
    int right_shin_id = -1;
    // Future: thighs, hips, abdomen, chest, etc.
};

// ============================================================================
// Case 1: Just Feet (No Constraints)
// ============================================================================
bool run_case_1_feet_only(Engine& engine, bool is_interactive, BodyPartIDs& body) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 1: Feet Only (No Constraints)";
    std::string description = "2 feet particles on floor - test collision only";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Floor at z=0, Feet at z=0.3m"});
    }

    // Create floor (3x3 grid, persists for all test cases)
    std::cout << "\n[STEP 1] Creating floor tiles..." << std::endl;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            Particle tile = {};
            tile.x = x * 2.0f;
            tile.y = y * 2.0f;
            tile.z = 0.1f;  // Floor surface at z=0
            tile.shape = ParticleShape::BOX;
            tile.width = 2.0f;
            tile.height = 2.0f;
            tile.thickness = 0.2f;
            tile.r = 0.3f; tile.g = 0.6f; tile.b = 0.3f;
            tile.SetMaterial(Materials::Type::LIGHT);  // Kinematic
            tile.material_density = 1000.0f;
            body.floor_ids.push_back(engine.add_particle(tile));
        }
    }
    std::cout << "  Floor tiles: " << body.floor_ids.size() << " (kinematic)" << std::endl;

    // Create feet (persist for all test cases)
    std::cout << "\n[STEP 2] Creating feet..." << std::endl;

    Particle left_foot = {};
    left_foot.x = -0.2f;  // Left of center
    left_foot.y = 0.0f;
    left_foot.z = 0.3f;   // Start above floor
    left_foot.shape = ParticleShape::BOX;
    left_foot.size = 0.2f;
    left_foot.r = 1.0f; left_foot.g = 0.0f; left_foot.b = 0.0f;  // RED
    left_foot.SetMaterial(Materials::Type::FLESH);
    left_foot.material_density = 1000.0f;
    body.left_foot_id = engine.add_particle(left_foot);

    Particle right_foot = {};
    right_foot.x = 0.2f;  // Right of center
    right_foot.y = 0.0f;
    right_foot.z = 0.3f;
    right_foot.shape = ParticleShape::BOX;
    right_foot.size = 0.2f;
    right_foot.r = 0.0f; right_foot.g = 0.0f; right_foot.b = 1.0f;  // BLUE
    right_foot.SetMaterial(Materials::Type::FLESH);
    right_foot.material_density = 1000.0f;
    body.right_foot_id = engine.add_particle(right_foot);

    std::cout << "  Left foot (RED): id=" << body.left_foot_id << " mass=1kg" << std::endl;
    std::cout << "  Right foot (BLUE): id=" << body.right_foot_id << " mass=1kg" << std::endl;

    // Run simulation (3 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 180;
    const double total_time = 3.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float left_z, right_z;
        {
            auto particles = ps.lock_particles_for_read();
            left_z = particles[body.left_foot_id].z;
            right_z = particles[body.right_foot_id].z;
        }

        float min_z = std::min(left_z, right_z);
        if (min_z < min_z_observed) min_z_observed = min_z;

        if (min_z < -0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Left=" << std::setw(6) << left_z << "m"
                      << " Right=" << std::setw(6) << right_z << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Left (RED) z=" + std::to_string(left_z).substr(0,6) + "m", 255, 0, 0);
            ui->draw_text(10, 90, "Right (BLUE) z=" + std::to_string(right_z).substr(0,6) + "m", 0, 0, 255);

            if (penetration_detected) {
                ui->draw_text(10, 130, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 130, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest Z: " << min_z_observed << "m (floor surface at z=0)" << std::endl;

    bool pass = !penetration_detected && min_z_observed >= -0.05f;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Feet penetrated floor" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Feet standing on floor without penetration" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Lowest Z: " + std::to_string(min_z_observed).substr(0,6) + "m");
        results.push_back(penetration_detected ? "RESULT: FAIL" : "RESULT: PASS");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    // DON'T delete particles - they persist for next test case
    return pass;
}

// ============================================================================
// Main test entry point
// ============================================================================
bool test_physics_experiment_03_eva_constraints() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  PHYSICS EXPERIMENT 03 - Eva Body Parts + Constraints      ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview (Incremental):" << std::endl;
    std::cout << "  Case 1: Feet only (no constraints) → test collision" << std::endl;
    std::cout << "  (Future: Case 2 adds shins + ankle constraints)" << std::endl;

    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Experiment 03 - Eva Body Parts + Constraints";

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // Setup: Light source
    std::cout << "\n[SETUP] Creating light..." << std::endl;
    Particle light = {};
    light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);
    light.is_light_source = true;
    light.emission_strength = 50000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f; light.a = 1.0f;
    engine.add_particle(light);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-5.0f, -5.0f, 3.0f);
    camera.look_at(0.0f, 0.0f, 0.0f);

    // Physics: Gravity
    std::cout << "\n[PHYSICS] Configuring gravity..." << std::endl;
    auto& physics = engine.get_physics_system();
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));
    std::cout << "  Gravity: (0, 0, -9.8) m/s²" << std::endl;

    // Persistent body parts (stay across test cases)
    BodyPartIDs body;

    // Run test cases (particles persist between cases)
    bool case1_pass = run_case_1_feet_only(engine, is_interactive, body);

    // Final summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║              TEST COMPLETED                                  ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1: " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "\n" << (case1_pass ? "✅ TEST PASSED!" : "❌ TEST FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1 (Feet): " + std::string(case1_pass ? "PASS" : "FAIL"),
            "",
            case1_pass ? "TEST PASSED!" : "TEST FAILED"
        };
        wait_for_space(engine, "TEST COMPLETED", summary);
    }

    return case1_pass;
}
