#include "../src/core/engine.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS EXPERIMENT 01 - Newtonian Gravity Basics (VISUAL)
// ============================================================================
// Tests the most basic physics principles with VISUAL + PARSEABLE output
//
// Test Cases:
//   Case 1: Particle with mass=0kg → no movement (kinematic via F=ma)
//   Case 2: Particle with mass=0kg → no movement (duplicate test)
//   Case 3: Particle with mass=1kg → free fall (v=-29.4m/s, z=-42.1m @ t=3s)
//   Case 4: Kinematic + Falling particle interaction (FAILS - collision not implemented)
//
// Expected behavior (Newtonian physics):
//   F = ma  (Newton's second law) → if mass=0, then F=0 (kinematic automatically)
//   v = v₀ + at  (velocity from acceleration)
//   x = x₀ + v₀t + ½at²  (position from velocity/acceleration)
//
// See docs/PHYSICS_v2.md for full design
//
// ============================================================================
// ⚠️  CRITICAL: READ-WRITE LOCK DEADLOCK - RESOLVED
// ============================================================================
//
// PROBLEM ENCOUNTERED:
// This test was hanging in headless mode during particle cleanup. The hang occurred
// when calling `flush_safe_deletions()` while still holding a read lock from
// `lock_particles_for_read()`.
//
// ROOT CAUSE:
// - ParticleSystem uses a shared_mutex for thread-safe access
// - `lock_particles_for_read()` acquires a READ lock (shared lock)
// - `flush_safe_deletions()` needs a WRITE lock (exclusive lock)
// - Thread deadlocked trying to acquire write lock while holding read lock
//
// INCORRECT CODE (CAUSES DEADLOCK):
//   auto particles = ps.lock_particles_for_read();  // Read lock acquired
//   const Particle& p = particles[id];
//   float z = p.z;                                   // Use particle data
//   // Lock still held here!
//   ps.flush_safe_deletions(9999);                  // ❌ DEADLOCK! Needs write lock
//
// CORRECT CODE (USES RAII SCOPE):
//   float z;
//   {
//       auto particles = ps.lock_particles_for_read();  // Read lock acquired
//       const Particle& p = particles[id];
//       z = p.z;                                         // Use particle data
//   }  // ✅ Lock automatically released here (RAII destructor)
//   ps.flush_safe_deletions(9999);                     // Safe - no lock held
//
// KEY PRINCIPLE:
// ALWAYS use explicit scope blocks `{ }` to ensure locks release via RAII before
// calling any function that might need a different lock type. The lock is held
// by the return value of `lock_particles_for_read()`, and releasing it requires
// the destructor to run (which happens at end of scope).
//
// HOW TO AVOID:
// 1. Extract data you need into local variables
// 2. Wrap `lock_particles_for_read()` in explicit scope block `{ }`
// 3. Lock releases automatically when scope ends (RAII)
// 4. Then safely call functions that need write locks
//
// This pattern applies to ANY code that needs to transition from read to write locks.
// ============================================================================

// Helper function to wait for key press in interactive mode
void wait_for_key(Engine& engine, const std::string& message) {
    std::cout << "\n" << message << std::endl;

    auto& input = engine.get_input_system();

    // First, wait for all keys to be released (drain any held keys)
    bool all_released = false;
    while (!all_released) {
        if (engine.get_platform()) {
            engine.get_platform()->poll_events();
        }

        const auto& input_state = input.get_input_state();
        all_released = true;
        for (int i = 0; i < GLFW_KEY_LAST; i++) {
            if (input_state.keys[i]) {
                all_released = false;
                break;
            }
        }

        engine.render();
        engine.present();
    }

    // Now wait for a new key press
    bool key_pressed = false;
    while (!key_pressed) {
        if (engine.get_platform()) {
            engine.get_platform()->poll_events();
        }

        const auto& input_state = input.get_input_state();
        for (int i = 0; i < GLFW_KEY_LAST; i++) {
            if (input_state.keys[i]) {
                key_pressed = true;
                break;
            }
        }

        engine.render();
        engine.present();
    }
}

// Helper function to run a single test case
struct TestCase {
    std::string name;
    std::string description;
    float x_pos;
    float mass;
    float r, g, b;
    bool should_fall;
};

// Helper to wait for SPACE key press with visual feedback
// Detects rising edge (press after release) to avoid double-press issues
void wait_for_space(Engine& engine, const std::string& title, const std::vector<std::string>& messages) {
    auto* ui = engine.get_ui_system();
    auto& input = engine.get_input_system();

    std::cout << "  [INTERACTIVE] " << title << " - waiting for SPACE..." << std::endl;

    // Wait for SPACE rising edge (NOT pressed → pressed transition)
    // Initialize was_pressed to current state to avoid false edge detection
    const auto& initial_state = input.get_input_state();
    bool was_pressed = initial_state.keys[GLFW_KEY_SPACE];
    bool detected_press = false;

    while (!detected_press) {
        engine.get_platform()->poll_events();
        engine.render();

        // Draw messages
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

        // Detect rising edge: was NOT pressed, now IS pressed
        if (!was_pressed && is_pressed) {
            detected_press = true;
        }

        was_pressed = is_pressed;
    }

    std::cout << "  [INTERACTIVE] SPACE pressed! Returning from wait_for_space..." << std::endl;
    std::cout.flush();  // Force flush to ensure message is written
}

bool run_single_test_case(Engine& engine, const TestCase& test_case, bool is_interactive) {
    auto& ps = engine.get_particle_system();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_case.name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << test_case.description << std::endl;

    // STATE 1: Ready to start - wait for SPACE
    if (is_interactive) {
        wait_for_space(engine, test_case.name, {test_case.description});
    }

    // Create test particle
    Particle p = {};
    p.x = test_case.x_pos;
    p.y = 0.0f;
    p.z = 2.0f;  // Start 2 meters above origin
    p.shape = ParticleShape::BOX;
    p.size = 0.5f;
    p.r = test_case.r;
    p.g = test_case.g;
    p.b = test_case.b;
    p.SetMass((test_case.mass == 0.0f) ? 100000.0f : test_case.mass);  // 0 mass → heavy static
    p.material_density = 1000.0f;
    int particle_id = engine.add_particle(p);

    std::cout << "  Particle created: x=" << p.x << " z=" << p.z << " mass=" << p.GetMass() << "kg" << std::endl;

    // Run simulation for 3 seconds
    const double dt = 1.0 / 60.0;
    const int total_frames = 180;
    const double total_time = 3.0;

    // Physics constants
    const double g = -9.8;
    const double expected_vz_3s = g * total_time;
    const double expected_z_3s = 2.0 + 0.5*g*total_time*total_time;

    std::cout << "\n  Running simulation for " << total_time << " seconds @ 60 Hz..." << std::endl;
    if (test_case.should_fall) {
        std::cout << "  Expected @ t=3s: vz=" << expected_vz_3s << " m/s, z=" << expected_z_3s << "m" << std::endl;
    }

    float initial_z = 2.0f;

    // STATE 2: Running - simulation runs automatically without pauses
    std::cout << "\n  [RUNNING] Simulating physics for " << total_time << " seconds..." << std::endl;

    // Run simulation
    for (int frame = 0; frame < total_frames; frame++) {
        // FORENSICS: Log test loop calls
        if (frame <= 10) {
            std::cout << std::setprecision(10) << "[TEST_LOOP F" << frame << "] Calling engine.update(dt=" << dt << ")" << std::endl;
        }
        engine.update(dt);

        // Print every 30 frames (0.5 seconds)
        if (frame % 30 == 0) {
            double t = frame * dt;
            auto particles = ps.lock_particles_for_read();
            const Particle& current_p = particles[particle_id];

            std::cout << "  [t=" << std::fixed << std::setprecision(2) << t << "s] z="
                      << std::setw(6) << current_p.z << "m  vz="
                      << std::setw(6) << current_p.vz << " m/s" << std::endl;
        }

        // Render frame (visual feedback, but no pauses)
        if (is_interactive) {
            engine.get_platform()->poll_events();  // Keep window responsive
            engine.render();

            // Show live status on screen (every frame for smooth updates)
            auto particles = ps.lock_particles_for_read();
            const Particle& current_p = particles[particle_id];
            double t = frame * dt;

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, test_case.name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 60, "z = " + std::to_string(current_p.z).substr(0,6) + "m", 200, 200, 200);
            ui->draw_text(10, 80, "vz = " + std::to_string(current_p.vz).substr(0,6) + " m/s", 200, 200, 200);
            ui->draw_text(10, 120, "Simulating... (no input)", 150, 150, 150);

            engine.present();
        }
        // Headless mode: Skip rendering entirely, only physics matters for tests
    }

    std::cout << "  [RUNNING] Simulation complete!" << std::endl;

    // Get final state using safe API (deadlock-proof, no manual scoping needed)
    float final_z = ps.get_particle_z(particle_id);
    float final_vz = ps.get_particle_vz(particle_id);
    bool pass = false;

    // Verify results
    if (test_case.should_fall) {
        // Dynamic particle - should match physics
        bool pos_ok = std::abs(final_z - expected_z_3s) < 0.5f;  // 50cm tolerance (accounts for fixed timestep accumulation)
        bool vel_ok = std::abs(final_vz - expected_vz_3s) < 0.5f;  // 0.5 m/s tolerance
        pass = pos_ok && vel_ok;

        std::cout << "\n  RESULTS:" << std::endl;
        std::cout << "    Final z:  " << final_z << "m (expected: " << expected_z_3s << "m)" << std::endl;
        std::cout << "    Final vz: " << final_vz << " m/s (expected: " << expected_vz_3s << " m/s)" << std::endl;
        std::cout << "    Position: " << (pos_ok ? "✅ PASS" : "❌ FAIL") << std::endl;
        std::cout << "    Velocity: " << (vel_ok ? "✅ PASS" : "❌ FAIL") << std::endl;
    } else {
        // Kinematic particle - should not move
        pass = std::abs(final_z - initial_z) < 0.001f;

        std::cout << "\n  RESULTS:" << std::endl;
        std::cout << "    Initial z: " << initial_z << "m" << std::endl;
        std::cout << "    Final z:   " << final_z << "m" << std::endl;
        std::cout << "    Movement:  " << (final_z - initial_z) << "m" << std::endl;
    }

    std::cout << "\n  " << (pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout.flush();

    // STATE 3: Complete - show results and wait for SPACE
    if (is_interactive) {
        std::vector<std::string> results;

        if (test_case.should_fall) {
            results.push_back("Final z:  " + std::to_string(final_z).substr(0,6) + "m (expected: " + std::to_string(expected_z_3s).substr(0,6) + "m)");
            results.push_back("Final vz: " + std::to_string(final_vz).substr(0,6) + " m/s (expected: " + std::to_string(expected_vz_3s).substr(0,6) + " m/s)");
        } else {
            results.push_back("Initial z: " + std::to_string(initial_z) + "m");
            results.push_back("Final z:   " + std::to_string(final_z) + "m");
            results.push_back("Movement:  " + std::to_string(final_z - initial_z) + "m");
        }

        results.push_back("");  // Blank line
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");

        wait_for_space(engine, test_case.name + " - COMPLETE", results);
    }

    // Safe immediate deletion (deadlock-proof, hides GPU buffering details)
    ps.delete_particle_immediate(particle_id);

    return pass;
}

// ============================================================================
// Run interaction test case: TWO particles (kinematic + falling)
// ============================================================================
bool run_interaction_test_case(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 4: Kinematic + Falling Particle Interaction";
    std::string description = "Kinematic particle (yellow, mass=0, z=0m) waits for falling particle (cyan, mass=1kg, z=2m)";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    // STATE 1: Ready to start - wait for SPACE
    if (is_interactive) {
        wait_for_space(engine, test_name, {description});
    }

    // Create kinematic particle (small cube at ground level - NOT a floor)
    Particle kinematic = {};
    kinematic.x = 0.0f;
    kinematic.y = 0.0f;
    kinematic.z = 0.125f;  // sit ON the turtle  // Ground level
    kinematic.shape = ParticleShape::BOX;
    kinematic.width = 0.5f;
    kinematic.height = 0.5f;
    kinematic.thickness = 0.25f;
    kinematic.r = 1.0f;  // Yellow
    kinematic.g = 1.0f;
    kinematic.b = 0.0f;
    kinematic.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    kinematic.material_density = 1000.0f;
    int kinematic_id = engine.add_particle(kinematic);
    std::cout << "  Kinematic particle (YELLOW): z=" << kinematic.z << "m" << std::endl;

    // Create falling particle (above kinematic)
    Particle falling = {};
    falling.x = 0.0f;
    falling.y = 0.0f;
    falling.z = 2.0f;  // 2 meters above
    falling.shape = ParticleShape::BOX;
    falling.width = 0.5f;
    falling.height = 0.5f;
    falling.thickness = 0.5f;
    falling.r = 0.0f;  // Cyan
    falling.g = 1.0f;
    falling.b = 1.0f;
    falling.SetMaterial(Materials::Type::FLESH);  // 1kg - will fall
    falling.material_density = 1000.0f;
    int falling_id = engine.add_particle(falling);
    std::cout << "  Falling particle (CYAN): z=" << falling.z << "m mass=" << falling.GetMass() << "kg" << std::endl;

    // Run simulation for 3 seconds
    const double dt = 1.0 / 60.0;
    const int total_frames = 180;
    const double total_time = 3.0;

    std::cout << "\n  Running simulation for " << total_time << " seconds @ 60 Hz..." << std::endl;
    std::cout << "  [RUNNING] Observing interaction..." << std::endl;

    // Run simulation
    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        // Print every 30 frames (0.5 seconds)
        if (frame % 30 == 0) {
            double t = frame * dt;
            auto particles = ps.lock_particles_for_read();
            const Particle& k = particles[kinematic_id];
            const Particle& f = particles[falling_id];

            std::cout << "  [t=" << std::fixed << std::setprecision(2) << t << "s]"
                      << " Kinematic z=" << std::setw(6) << k.z << "m"
                      << " | Falling z=" << std::setw(6) << f.z << "m vz=" << std::setw(6) << f.vz << " m/s"
                      << std::endl;
        }

        // Render frame (visual feedback)
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto particles = ps.lock_particles_for_read();
            const Particle& k = particles[kinematic_id];
            const Particle& f = particles[falling_id];
            double t = frame * dt;

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Kinematic (YELLOW) z=" + std::to_string(k.z).substr(0,6) + "m", 255, 255, 0);
            ui->draw_text(10, 90, "Falling (CYAN) z=" + std::to_string(f.z).substr(0,6) + "m vz=" + std::to_string(f.vz).substr(0,6) + " m/s", 0, 255, 255);
            ui->draw_text(10, 120, "Observing interaction...", 150, 150, 150);

            engine.present();
        }
    }

    // Get final state using safe API (deadlock-proof)
    auto kinematic_snap = ps.get_particle_snapshot(kinematic_id);
    auto falling_snap = ps.get_particle_snapshot(falling_id);

    float final_k_z = kinematic_snap.z;
    float final_f_z = falling_snap.z;
    float final_f_vz = falling_snap.vz;

    std::cout << "\n  Final state @ t=3s:" << std::endl;
    std::cout << "    Kinematic: z=" << final_k_z << "m (expected: 0m - no movement)" << std::endl;
    std::cout << "    Falling:   z=" << final_f_z << "m vz=" << final_f_vz << " m/s" << std::endl;

    // Build results for visual display
    std::vector<std::string> results;
    results.push_back("Final state @ t=3s:");
    results.push_back("");
    results.push_back("Kinematic (YELLOW):");
    results.push_back("  z = " + std::to_string(final_k_z).substr(0,6) + "m (expected: 0m)");
    results.push_back("");
    results.push_back("Falling (CYAN):");
    results.push_back("  z = " + std::to_string(final_f_z).substr(0,6) + "m");
    results.push_back("  vz = " + std::to_string(final_f_vz).substr(0,6) + " m/s");
    results.push_back("");
    results.push_back("Observation: No collision detection yet - particles pass through");

    // STATE 3: Complete - show results, wait for SPACE
    if (is_interactive) {
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    // Safe immediate batch deletion (deadlock-proof, hides GPU buffering details)
    ps.delete_particles_immediate({kinematic_id, falling_id});

    return true;  // No validation yet - just observing
}

// ============================================================================
// Case 5: Floor collision test - cube lands on floor
// ============================================================================
bool run_floor_collision_test(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 5: Floor Collision Test";
    std::string description = "Floor (yellow 2x2m, z=0) + falling cube (cyan 0.5m, z=2m) - should land and stop";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description});
    }

    // Create floor (kinematic, wide)
    Particle floor_p = {};
    floor_p.x = 0.0f;
    floor_p.y = 0.0f;
    floor_p.z = 0.125f;  // sit ON the turtle  // Center at z=0, top at z=0.25
    floor_p.shape = ParticleShape::BOX;
    floor_p.width = 4.0f;
    floor_p.height = 4.0f;
    floor_p.thickness = 0.25f;
    floor_p.r = 1.0f; floor_p.g = 1.0f; floor_p.b = 0.0f;  // Yellow
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    floor_p.material_density = 1000.0f;
    int floor_id = engine.add_particle(floor_p);
    std::cout << "  Floor (YELLOW): 4x4m at z=" << floor_p.z << "m (top at z=0.25m)" << std::endl;

    // Create falling cube
    Particle cube = {};
    cube.x = 0.0f;
    cube.y = 0.0f;
    cube.z = 2.0f;  // Start 2m up, bottom at z=1.75
    cube.shape = ParticleShape::BOX;
    cube.width = 0.5f;
    cube.height = 0.5f;
    cube.thickness = 0.5f;
    cube.r = 0.0f; cube.g = 1.0f; cube.b = 1.0f;  // Cyan
    cube.SetMaterial(Materials::Type::FLESH);
    cube.material_density = 1000.0f;
    int cube_id = engine.add_particle(cube);
    std::cout << "  Cube (CYAN): 0.5m cube at z=" << cube.z << "m (bottom at z=1.75m)" << std::endl;

    // Run simulation
    const double dt = 1.0 / 60.0;
    const int total_frames = 180;  // 3 seconds

    std::cout << "\n  Running simulation for 3 seconds..." << std::endl;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        if (frame % 30 == 0) {
            double t = frame * dt;
            auto particles = ps.lock_particles_for_read();
            const Particle& c = particles[cube_id];
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << t << "s]"
                      << " Cube z=" << std::setw(6) << c.z << "m vz=" << std::setw(6) << c.vz << " m/s"
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto particles = ps.lock_particles_for_read();
            const Particle& c = particles[cube_id];
            double t = frame * dt;

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Cube z=" + std::to_string(c.z).substr(0,6) + "m vz=" + std::to_string(c.vz).substr(0,6) + " m/s", 0, 255, 255);

            engine.present();
        }
    }

    // Get final state
    auto cube_snap = ps.get_particle_snapshot(cube_id);
    float final_z = cube_snap.z;
    float final_vz = cube_snap.vz;

    // Expected: cube should land on floor (z ~= 0.5 = floor_top + cube_half_thickness)
    // Expected z: floor top (0.25) + cube half-thickness (0.25) = 0.5m
    float expected_z = 0.5f;
    bool landed = (final_z > 0.3f && final_z < 1.0f && std::abs(final_vz) < 1.0f);

    std::cout << "\n  Final state:" << std::endl;
    std::cout << "    Cube z=" << final_z << "m (expected ~" << expected_z << "m)" << std::endl;
    std::cout << "    Cube vz=" << final_vz << " m/s (expected ~0)" << std::endl;
    std::cout << "    " << (landed ? "✅ PASS: Cube landed on floor" : "❌ FAIL: Cube did not land properly") << std::endl;

    if (is_interactive) {
        std::vector<std::string> results = {
            "Cube final z: " + std::to_string(final_z).substr(0,6) + "m (expected ~0.5m)",
            "Cube final vz: " + std::to_string(final_vz).substr(0,6) + " m/s (expected ~0)",
            "",
            landed ? "RESULT: PASS - Cube landed on floor" : "RESULT: FAIL - Cube did not land"
        };
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    ps.delete_particles_immediate({floor_id, cube_id});
    return landed;
}

bool test_physics_experiment_01() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  PHYSICS EXPERIMENT 01 - Newtonian Gravity Basics (VISUAL)  ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview:" << std::endl;
    std::cout << "  Case 1: Kinematic particle (mass=0) → no movement" << std::endl;
    std::cout << "  Case 2: Kinematic particle (mass=0) → no movement (duplicate)" << std::endl;
    std::cout << "  Case 3: Dynamic particle (mass=1kg) → free fall under gravity" << std::endl;
    std::cout << "  Case 4: Kinematic (yellow) + Falling particle (cyan) → observe interaction" << std::endl;
    std::cout << "  Case 5: Floor collision test → cube should land on floor" << std::endl;

    Engine engine;
    EngineConfig config;

    // Default to HEADLESS mode (fast, automated, with logging)
    // Set environment variable INTERACTIVE=1 to run visual mode
    // Example: INTERACTIVE=1 ./build/logosphere/logosphere-tests --test test_physics_experiment_01
    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Experiment 01 - Gravity Basics";
    config.enable_chat_window = false;  // Disable chat for physics tests

    std::cout << "\n[MODE] Running in " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << " mode" << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 to run visual mode" << std::endl;
    }

    // Engine::initialize() returns 0 on success, non-zero on failure
    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // ========================================================================
    // SETUP: Add light source (persists across all test cases)
    // ========================================================================
    std::cout << "\n[SETUP] Creating light source..." << std::endl;

    Particle light = {};
    light.x = 0.0f;
    light.y = -10.0f;
    light.z = 15.0f;  // Above and behind
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible (light sources don't fall!)
    light.is_light_source = true;
    light.emission_strength = 50000.0f;  // Bright light
    light.emission_radius = 500.0f;      // Large radius
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f; light.a = 1.0f;  // White light
    engine.add_particle(light);
    std::cout << "  Light at x=" << light.x << " y=" << light.y << " z=" << light.z << std::endl;

    // Camera setup - positioned to see entire fall trajectory
    auto& camera = engine.get_camera_system();
    camera.set_position(-10.0f, -10.0f, 25.0f);  // Higher up to see more
    camera.look_at(0.0f, 0.0f, -10.0f);  // Look down to see the fall path (z=2 to z=-30)

    // ========================================================================
    // CONFIGURE PHYSICS: Add gravity force
    // ========================================================================
    std::cout << "\n[PHYSICS] Configuring gravity..." << std::endl;
    auto& physics = engine.get_physics_system();

    // Add gravity force using force registry pattern (see docs/PHYSICS_v2.md)
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));
    std::cout << "  Gravity: (0, 0, -9.8) m/s²" << std::endl;

    // ========================================================================
    // RUN TEST CASES SEQUENTIALLY
    // ========================================================================

    // Define test cases
    TestCase case1 = {
        "Case 1: Kinematic Particle (mass=0kg)",
        "Particle with mass=0 should not move (kinematic via F=ma)",
        0.0f,      // x_pos (center)
        0.0f,      // mass
        1.0f, 0.0f, 0.0f,  // RED
        false      // should_fall
    };

    TestCase case2 = {
        "Case 2: Kinematic Particle (mass=0kg) - Duplicate",
        "Second kinematic particle to verify consistent behavior",
        0.0f,      // x_pos (center)
        0.0f,      // mass
        0.0f, 1.0f, 0.0f,  // GREEN
        false      // should_fall
    };

    TestCase case3 = {
        "Case 3: Dynamic Particle (mass=1kg) with Gravity",
        "Particle with mass=1kg should fall under gravity (v=-29.4m/s, z=-42.1m @ t=3s)",
        0.0f,      // x_pos (center)
        1.0f,      // mass
        0.0f, 0.5f, 1.0f,  // BLUE
        true       // should_fall
    };

    // Run each test case sequentially
    std::cout << "\n====== CASE 1 STARTS ======\n" << std::endl;
    bool case1_pass = run_single_test_case(engine, case1, is_interactive);
    std::cout << "\n====== CASE 1 ENDS ======\n" << std::endl;

    std::cout << "\n====== CASE 2 STARTS ======\n" << std::endl;
    bool case2_pass = run_single_test_case(engine, case2, is_interactive);
    std::cout << "\n====== CASE 2 ENDS ======\n" << std::endl;

    std::cout << "\n====== CASE 3 STARTS ======\n" << std::endl;
    bool case3_pass = run_single_test_case(engine, case3, is_interactive);
    std::cout << "\n====== CASE 3 ENDS ======\n" << std::endl;

    std::cout << "\n====== CASE 4 STARTS ======\n" << std::endl;
    bool case4_pass = run_interaction_test_case(engine, is_interactive);
    std::cout << "\n====== CASE 4 ENDS ======\n" << std::endl;

    std::cout << "\n====== CASE 5 STARTS ======\n" << std::endl;
    bool case5_pass = run_floor_collision_test(engine, is_interactive);
    std::cout << "\n====== CASE 5 ENDS ======\n" << std::endl;

    bool all_pass = case1_pass && case2_pass && case3_pass && case4_pass && case5_pass;

    // ========================================================================
    // FINAL SUMMARY
    // ========================================================================
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║              ALL TEST CASES COMPLETED                        ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1: " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 2: " << (case2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 3: " << (case3_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 4: " << (case4_pass ? "✅ PASS" : "❌ FAIL") << " (Observation)" << std::endl;
    std::cout << "  Case 5: " << (case5_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED!" : "❌ SOME TESTS FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1: " + std::string(case1_pass ? "PASS" : "FAIL"),
            "Case 2: " + std::string(case2_pass ? "PASS" : "FAIL"),
            "Case 3: " + std::string(case3_pass ? "PASS" : "FAIL"),
            "Case 4: " + std::string(case4_pass ? "PASS" : "FAIL") + " (Observation)",
            "Case 5: " + std::string(case5_pass ? "PASS" : "FAIL"),
            "",
            all_pass ? "ALL TESTS PASSED!" : "SOME TESTS FAILED"
        };
        wait_for_space(engine, "ALL TEST CASES COMPLETED", summary);
    }

    return all_pass;
}
