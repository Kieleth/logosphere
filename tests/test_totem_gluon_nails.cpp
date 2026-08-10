#include "../src/core/engine.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS EXPERIMENT - Gluon Constraint Testing (NAILS)
// ============================================================================
// Purpose: Test gluon constraint system (nails, joints) with pre-built totem
// Strategy: Start with complete totem already assembled, test constraint physics
//
// Test Cases:
//   Case 1: Hanging particles attached to beam ends → test beam attachment
//   Case 2: Constrained particles (nailed together) → test constraint forces
//   Case 3: Unconstrained particles → verify free fall (control test)
//
// Note: Totem is created directly (no dropping/collision) to focus on constraints
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
// Persistent State: Floor and totem pieces
// ============================================================================
struct TotemState {
    std::vector<int> floor_ids;
    std::vector<int> piece_ids;  // All 6 totem pieces (created directly)
    int hanging_particle1_id = -1;  // For Case 2/3: particle with nail
    int hanging_particle2_id = -1;  // For Case 2/3: particle with nail
};

// ============================================================================
// Setup Function: Create Complete Totem (No Simulation)
// ============================================================================
// Creates floor + all 6 totem pieces positioned correctly (no dropping)
// This is a SETUP function, not a test - it prepares the scene for constraint tests
// ============================================================================
void create_complete_totem(Engine& engine, TotemState& totem) {
    auto& ps = engine.get_particle_system();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║                  TOTEM SETUP (Pre-built)                    ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nCreating complete totem structure (no simulation, direct placement)..." << std::endl;

    // Create floor (7x5 grid)
    std::cout << "\n[SETUP] Creating floor tiles..." << std::endl;
    for (int x = -4; x <= 3; x++) {
        for (int y = -3; y <= 2; y++) {
            Particle tile = {};
            tile.x = x * 2.0f;  // 2.0m spacing for 2m tiles = no overlap
            tile.y = y * 2.0f;
            tile.z = 0.1f;  // Floor surface at z=0
            tile.shape = ParticleShape::BOX;
            tile.width = 2.0f;
            tile.height = 2.0f;
            tile.thickness = 0.2f;
            tile.r = 0.3f; tile.g = 0.6f; tile.b = 0.3f;
            tile.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
            tile.material_density = 1000.0f;
            totem.floor_ids.push_back(engine.add_particle(tile));
        }
    }
    std::cout << "  Floor tiles: " << totem.floor_ids.size() << " (heavy)" << std::endl;

    // Floor geometry constants
    const float floor_center_z = 0.1f;
    const float floor_thickness = 0.2f;
    const float floor_top_z = floor_center_z + (floor_thickness * 0.5f);  // 0.2m

    // Build totem from bottom to top (Eden structure)
    std::cout << "\n[SETUP] Creating totem pieces (Eden structure)..." << std::endl;

    float current_z = floor_top_z;  // Start at floor top

    // Piece 1: Lower trunk (1.2m × 1.2m × 2.4m, 10kg)
    const float lower_thickness = 2.4f;
    Particle lower = {};
    lower.x = 0.0f; lower.y = 0.0f;
    lower.z = current_z + (lower_thickness / 2.0f);
    lower.shape = ParticleShape::BOX;
    lower.width = 1.2f; lower.height = 1.2f; lower.thickness = lower_thickness;
    lower.r = 0.4f; lower.g = 0.25f; lower.b = 0.15f;
    lower.SetMaterial(Materials::Type::WOOD_HARD);
    int lower_id = engine.add_particle(lower);
    totem.piece_ids.push_back(lower_id);
    current_z += lower_thickness;

    // Piece 2: Middle trunk (0.4m × 0.4m × 0.64m, 8kg)
    const float middle_thickness = 0.64f;
    Particle middle = {};
    middle.x = 0.0f; middle.y = 0.0f;
    middle.z = current_z + (middle_thickness / 2.0f);
    middle.shape = ParticleShape::BOX;
    middle.width = 0.4f; middle.height = 0.4f; middle.thickness = middle_thickness;
    middle.r = 0.5f; middle.g = 0.3f; middle.b = 0.2f;
    middle.SetMaterial(Materials::Type::WOOD_HARD);
    int middle_id = engine.add_particle(middle);
    totem.piece_ids.push_back(middle_id);
    current_z += middle_thickness;

    // Piece 3: Top trunk (0.8m × 0.8m × 1.8m, 4kg)
    const float top_thickness = 1.8f;
    Particle top = {};
    top.x = 0.0f; top.y = 0.0f;
    top.z = current_z + (top_thickness / 2.0f);
    top.shape = ParticleShape::BOX;
    top.width = 0.8f; top.height = 0.8f; top.thickness = top_thickness;
    top.r = 0.6f; top.g = 0.35f; top.b = 0.25f;
    top.SetMaterial(Materials::Type::WOOD_HARD);
    int top_id = engine.add_particle(top);
    totem.piece_ids.push_back(top_id);
    current_z += top_thickness;

    // Piece 4: Trunk4 (0.4m × 0.4m × 0.8m, 3.2kg)
    const float trunk4_thickness = 0.8f;
    Particle trunk4 = {};
    trunk4.x = 0.0f; trunk4.y = 0.0f;
    trunk4.z = current_z + (trunk4_thickness / 2.0f);
    trunk4.shape = ParticleShape::BOX;
    trunk4.width = 0.4f; trunk4.height = 0.4f; trunk4.thickness = trunk4_thickness;
    trunk4.r = 0.7f; trunk4.g = 0.4f; trunk4.b = 0.3f;
    trunk4.SetMaterial(Materials::Type::WOOD_HARD);
    int trunk4_id = engine.add_particle(trunk4);
    totem.piece_ids.push_back(trunk4_id);
    current_z += trunk4_thickness;

    // Piece 5: Trunk5 (1.0m × 1.0m × 1.6m, 7kg)
    const float trunk5_thickness = 1.6f;
    Particle trunk5 = {};
    trunk5.x = 0.0f; trunk5.y = 0.0f;
    trunk5.z = current_z + (trunk5_thickness / 2.0f);
    trunk5.shape = ParticleShape::BOX;
    trunk5.width = 1.0f; trunk5.height = 1.0f; trunk5.thickness = trunk5_thickness;
    trunk5.r = 0.55f; trunk5.g = 0.35f; trunk5.b = 0.2f;
    trunk5.SetMaterial(Materials::Type::WOOD_HARD);
    int trunk5_id = engine.add_particle(trunk5);
    totem.piece_ids.push_back(trunk5_id);
    current_z += trunk5_thickness;

    // Piece 6: Horizontal beam (6.0m × 0.5m × 0.4m, 6.4kg)
    const float beam_thickness = 0.4f;
    Particle beam = {};
    beam.x = 0.0f; beam.y = 0.0f;
    beam.z = current_z + (beam_thickness / 2.0f);
    beam.shape = ParticleShape::BOX;
    beam.width = 6.0f; beam.height = 0.5f; beam.thickness = beam_thickness;
    beam.r = 0.8f; beam.g = 0.5f; beam.b = 0.3f;
    beam.SetMaterial(Materials::Type::WOOD_HARD);
    int beam_id = engine.add_particle(beam);
    totem.piece_ids.push_back(beam_id);
    current_z += beam_thickness;

    std::cout << "  Created 6 totem pieces:" << std::endl;
    std::cout << "    P" << lower_id << ": Lower trunk (1.2×1.2×2.4m, 10kg)" << std::endl;
    std::cout << "    P" << middle_id << ": Middle trunk (0.4×0.4×0.64m, 8kg)" << std::endl;
    std::cout << "    P" << top_id << ": Top trunk (0.8×0.8×1.8m, 4kg)" << std::endl;
    std::cout << "    P" << trunk4_id << ": Trunk4 (0.4×0.4×0.8m, 3.2kg)" << std::endl;
    std::cout << "    P" << trunk5_id << ": Trunk5 (1.0×1.0×1.6m, 7kg)" << std::endl;
    std::cout << "    P" << beam_id << ": Horizontal beam (6.0×0.5×0.4m, 6.4kg)" << std::endl;
    std::cout << "  Total totem height: " << current_z << "m" << std::endl;
    std::cout << "\n✅ Totem created successfully (ready for constraint testing)" << std::endl;
}

// ============================================================================
// Case 1: Add Hanging Particles (at beam ends)
// ============================================================================
bool run_case_1_hanging_particles(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 1: Constrained Hanging Particles";
    std::string description = "Two particles nailed to beam ends with gluon constraints - should stay rigid";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;
    std::cout << "\n✅ Gluon nail constraints added - particles should NOT fall!" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "2 particles nailed to beam ends", "✅ Should stay rigid and NOT fall"});
    }

    // Get horizontal beam position (last piece added in setup)
    int beam_id = totem.piece_ids[5];
    float beam_x, beam_y, beam_z, beam_width, beam_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        beam_x = particles[beam_id].x;
        beam_y = particles[beam_id].y;
        beam_z = particles[beam_id].z;
        beam_width = particles[beam_id].width;  // 6m long beam
        beam_thickness = particles[beam_id].thickness;  // 0.4m thick beam
    }

    std::cout << "\n[STEP 1] Beam at z=" << beam_z << "m, width=" << beam_width << "m, thickness=" << beam_thickness << "m" << std::endl;

    // Create two particles at beam ends using NEW GLUON PRIMITIVE API
    std::cout << "\n[STEP 2] Creating particles at beam ends with nail gluons..." << std::endl;
    std::cout << "  Using add_particle_with_gluon_to() primitive (Gluon v2 API)" << std::endl;

    const float PARTICLE_MASS = 2.0f;   // 2kg each
    const float PARTICLE_SIZE = 0.5f;   // 50cm cube (larger for visibility)

    auto& physics = engine.get_physics_system();

    // Beam geometry for gluon offsets
    float beam_half_thickness = beam_thickness * 0.5f;  // 0.2m
    float particle_half_size = PARTICLE_SIZE * 0.5f;    // 0.25m

    // ========================================================================
    // LEFT HANGING PARTICLE - Using Gluon v2 Primitive
    // ========================================================================

    // Particle template (position will be calculated by primitive)
    Particle left_hang_config = {};
    left_hang_config.shape = ParticleShape::BOX;
    left_hang_config.size = PARTICLE_SIZE;  // 50cm cube
    left_hang_config.r = 1.0f; left_hang_config.g = 0.5f; left_hang_config.b = 0.0f;  // Orange
    // Mass auto-calculates from volume and material_density
    left_hang_config.material_density = 1000.0f;

    // Create nail gluon for left end
    auto left_nail = std::make_unique<NailGluon>();
    left_nail->offset_a = Vec3(-beam_width / 2.0f, 0.0f, -beam_half_thickness);  // Beam's left-bottom
    left_nail->offset_b = Vec3(0.0f, 0.0f, particle_half_size);  // Particle's top surface
    left_nail->target_distance = 0.0f;  // Zero gap - rigid attachment
    left_nail->breaking_force = 5000.0f;  // 5kN breaking force

    // Use primitive to create particle + attach with nail (atomically)
    int left_hang_id = physics.add_particle_with_gluon_to(beam_id, left_hang_config, std::move(left_nail));
    totem.piece_ids.push_back(left_hang_id);

    // ========================================================================
    // RIGHT HANGING PARTICLE - Using Gluon v2 Primitive
    // ========================================================================

    // Particle template (position will be calculated by primitive)
    Particle right_hang_config = {};
    right_hang_config.shape = ParticleShape::BOX;
    right_hang_config.size = PARTICLE_SIZE;  // 50cm cube
    right_hang_config.r = 0.5f; right_hang_config.g = 0.0f; right_hang_config.b = 1.0f;  // Purple
    // Mass auto-calculates from volume and material_density
    right_hang_config.material_density = 1000.0f;

    // Create nail gluon for right end
    auto right_nail = std::make_unique<NailGluon>();
    right_nail->offset_a = Vec3(+beam_width / 2.0f, 0.0f, -beam_half_thickness);  // Beam's right-bottom
    right_nail->offset_b = Vec3(0.0f, 0.0f, particle_half_size);  // Particle's top surface
    right_nail->target_distance = 0.0f;  // Zero gap - rigid attachment
    right_nail->breaking_force = 5000.0f;  // 5kN breaking force

    // Use primitive to create particle + attach with nail (atomically)
    int right_hang_id = physics.add_particle_with_gluon_to(beam_id, right_hang_config, std::move(right_nail));
    totem.piece_ids.push_back(right_hang_id);

    // Log positions (for verification)
    float left_z, right_z;
    {
        auto particles = ps.lock_particles_for_read();
        left_z = particles[left_hang_id].z;
        right_z = particles[right_hang_id].z;
    }

    std::cout << "\n[STEP 3] Created particles with NAIL GLUONS:" << std::endl;
    std::cout << "  Left particle (ORANGE): id=" << left_hang_id << " at z=" << left_z << "m (auto-positioned)" << std::endl;
    std::cout << "  Right particle (PURPLE): id=" << right_hang_id << " at z=" << right_z << "m (auto-positioned)" << std::endl;
    std::cout << "  ✅ Particles created with zero-gap nail attachments to beam" << std::endl;

    // FORENSICS: Initial state of all totem particles + hanging particles
    std::cout << "\n[FORENSICS] Initial particle state:" << std::endl;
    {
        auto particles = ps.lock_particles_for_read();
        for (size_t i = 0; i < totem.piece_ids.size(); i++) {
            int pid = totem.piece_ids[i];
            const auto& p = particles[pid];
            std::cout << "  P" << pid << ": pos=(" << p.x << ", " << p.y << ", " << p.z << ") "
                      << "vel=(" << p.vx << ", " << p.vy << ", " << p.vz << ") "
                      << "mass=" << p.GetMass() << "kg "
                      << (std::isnan(p.z) || std::isnan(p.vz) ? "⚠️ NAN!" : "✓")
                      << std::endl;
        }
    }

    // Run simulation (5 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 300;  // 5 seconds @ 60Hz

    std::cout << "\n[STEP 4] Running simulation for 5.0s @ 60 Hz..." << std::endl;

    float min_left_z = 10000.0f;
    float min_right_z = 10000.0f;
    float final_left_z = 0.0f;
    float final_right_z = 0.0f;
    bool nan_detected = false;
    int nan_frame = -1;

    for (int frame = 0; frame < total_frames; frame++) {
        // FORENSICS: Detailed logging for first 5 frames
        if (frame < 5) {
            std::cout << "\n[FORENSICS Frame " << frame << "] BEFORE update:" << std::endl;
            auto particles = ps.lock_particles_for_read();
            for (size_t i = 0; i < totem.piece_ids.size(); i++) {
                int pid = totem.piece_ids[i];
                const auto& p = particles[pid];
                std::cout << "  P" << pid << ": z=" << p.z << " vz=" << p.vz << " "
                          << (std::isnan(p.z) || std::isnan(p.vz) ? "⚠️ NAN!" : "✓")
                          << std::endl;
            }
        }

        engine.update(dt);

        // FORENSICS: Check for NaN after update
        float left_z, right_z, left_vz, right_vz;
        {
            auto particles = ps.lock_particles_for_read();
            left_z = particles[left_hang_id].z;
            right_z = particles[right_hang_id].z;
            left_vz = particles[left_hang_id].vz;
            right_vz = particles[right_hang_id].vz;

            if (!nan_detected && (std::isnan(left_z) || std::isnan(right_z) || std::isnan(left_vz) || std::isnan(right_vz))) {
                nan_detected = true;
                nan_frame = frame;
                std::cout << "\n⚠️ ⚠️ ⚠️  NAN DETECTED AT FRAME " << frame << " ⚠️ ⚠️ ⚠️" << std::endl;
                std::cout << "  Left: z=" << left_z << " vz=" << left_vz << std::endl;
                std::cout << "  Right: z=" << right_z << " vz=" << right_vz << std::endl;

                // Dump all particle states when NaN detected
                std::cout << "\n[FORENSICS] ALL particles at NaN detection:" << std::endl;
                for (size_t i = 0; i < totem.piece_ids.size(); i++) {
                    int pid = totem.piece_ids[i];
                    const auto& p = particles[pid];
                    std::cout << "  P" << pid << ": z=" << p.z << " vz=" << p.vz << " "
                              << (std::isnan(p.z) || std::isnan(p.vz) ? "⚠️ NAN!" : "✓")
                              << std::endl;
                }
            }
        }

        if (frame < 5) {
            std::cout << "[FORENSICS Frame " << frame << "] AFTER update: Left z=" << left_z << " vz=" << left_vz
                      << " | Right z=" << right_z << " vz=" << right_vz << std::endl;
        }

        if (left_z < min_left_z) min_left_z = left_z;
        if (right_z < min_right_z) min_right_z = right_z;

        final_left_z = left_z;
        final_right_z = right_z;

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Left: z=" << std::setw(6) << left_z << "m"
                      << " | Right: z=" << std::setw(6) << right_z << "m"
                      << std::endl;
        }

        // Stop after NaN detected for analysis
        if (nan_detected && frame > nan_frame + 2) {
            std::cout << "\n[FORENSICS] Stopping simulation 2 frames after NaN for analysis" << std::endl;
            break;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Left (ORANGE) z=" + std::to_string(left_z).substr(0,6) + "m", 255, 128, 0);
            ui->draw_text(10, 90, "Right (PURPLE) z=" + std::to_string(right_z).substr(0,6) + "m", 128, 0, 255);

            engine.present();
        }
    }

    std::cout << "\n[STEP 5] Analysis:" << std::endl;
    std::cout << "  Final Left Z: " << final_left_z << "m (min: " << min_left_z << "m)" << std::endl;
    std::cout << "  Final Right Z: " << final_right_z << "m (min: " << min_right_z << "m)" << std::endl;

    // Test passes if particles don't fall significantly (stay near beam)
    // Expected position calculated from gluon geometry:
    //   attach_a_world_z = beam_z + offset_a.z = beam_z - beam_half_thickness
    //   particle_center_z = attach_a_world_z - offset_b.z = beam_z - beam_half_thickness - particle_half_size
    // Note: beam_half_thickness and particle_half_size already defined earlier in function
    float expected_z = beam_z - beam_half_thickness - particle_half_size;
    float tolerance = 0.10f;  // 100mm tolerance (gluons settle under load)
    bool left_ok = std::abs(final_left_z - expected_z) < tolerance;
    bool right_ok = std::abs(final_right_z - expected_z) < tolerance;
    bool pass = left_ok && right_ok;

    if (!pass) {
        std::cout << "  ❌ FAIL: Particles fell (expected z≈" << expected_z << "m)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Particles held by nails!" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Left Z: " + std::to_string(final_left_z).substr(0,6) + "m");
        results.push_back("Right Z: " + std::to_string(final_right_z).substr(0,6) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 2: Gluon Breaking Test - Increasing Weight Until Failure
// ============================================================================
bool run_case_2_constrained_hanging(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 2: Gluon Breaking Test";
    std::string description = "Gradually increase weight of hanging particles until gluons break";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Weight increases 100kg/sec", "Should break ~5 seconds"});
    }

    // Get hanging particle IDs from Case 1 (totem pieces include the hanging particles)
    if (totem.piece_ids.size() < 8) {
        std::cerr << "❌ FAIL: Case 2 requires Case 1 to run first (need hanging particles)" << std::endl;
        return false;
    }

    int left_hang_id = totem.piece_ids[6];   // Left hanging particle (orange)
    int right_hang_id = totem.piece_ids[7];  // Right hanging particle (purple)

    // Envelope math
    const float BREAKING_FORCE = 5000.0f;  // 5kN (from Case 1 nail setup)
    const float GRAVITY = 9.8f;            // m/s²
    const float INITIAL_MASS = 2.0f;       // kg (starting mass)
    const float MASS_INCREASE_PER_SEC = 100.0f;  // kg/s
    const float BREAKING_MASS = BREAKING_FORCE / GRAVITY;  // 510.2 kg
    const float EXPECTED_BREAK_TIME = (BREAKING_MASS - INITIAL_MASS) / MASS_INCREASE_PER_SEC;  // ~5.08 seconds
    const float FLOOR_TOP_Z = 0.2f;  // Floor tiles centered at z=0.1m with 0.2m thickness
    const float PARTICLE_HALF_HEIGHT = 0.25f;  // 0.25m for 0.5m cube
    const float FLOOR_CONTACT_Z = FLOOR_TOP_Z + PARTICLE_HALF_HEIGHT + 0.05f;  // 0.5m with tolerance

    std::cout << "\n[ENVELOPE MATH]" << std::endl;
    std::cout << "  Breaking force: " << BREAKING_FORCE << " N" << std::endl;
    std::cout << "  Initial mass (each): " << INITIAL_MASS << " kg" << std::endl;
    std::cout << "  Mass increase: " << MASS_INCREASE_PER_SEC << " kg/s" << std::endl;
    std::cout << "  Breaking mass: " << BREAKING_MASS << " kg" << std::endl;
    std::cout << "  Expected break time: " << std::fixed << std::setprecision(2) << EXPECTED_BREAK_TIME << " seconds" << std::endl;

    // Run simulation (max 10 seconds)
    const double dt = 1.0 / 60.0;
    const int max_frames = 600;  // 10 seconds @ 60Hz (gluon breaks at ~5s)

    std::cout << "\n[STEP 1] Starting weight increase test..." << std::endl;

    float current_mass = INITIAL_MASS;
    bool gluon_broken = false;
    float break_time = 0.0f;
    bool particles_on_floor = false;

    for (int frame = 0; frame < max_frames; frame++) {
        double t = frame * dt;

        // Increase mass over time
        current_mass = INITIAL_MASS + (MASS_INCREASE_PER_SEC * t);

        // Update particle masses. SetMass auto-wakes the particles, so the
        // solver re-evaluates the force balance at the new load.
        {
            auto particles = ps.lock_particles_for_write();
            particles[left_hang_id].SetMass(current_mass);
            particles[right_hang_id].SetMass(current_mass);
        }

        engine.update(dt);

        // Check positions
        float left_z, right_z;
        {
            auto particles = ps.lock_particles_for_read();
            left_z = particles[left_hang_id].z;
            right_z = particles[right_hang_id].z;
        }

        // Detect gluon breaking (particles start falling)
        if (!gluon_broken && (left_z < 7.0f || right_z < 7.0f)) {
            gluon_broken = true;
            break_time = t;
            std::cout << "\n🔥 GLUON BROKE at t=" << std::fixed << std::setprecision(2) << break_time
                      << "s, mass=" << current_mass << "kg" << std::endl;
        }

        // Detect floor contact
        if (left_z <= FLOOR_CONTACT_Z || right_z <= FLOOR_CONTACT_Z) {
            particles_on_floor = true;
            std::cout << "✅ Particles touched floor at t=" << std::fixed << std::setprecision(2) << t << "s" << std::endl;
            break;  // Success!
        }

        // Log every second
        if (frame % 60 == 0) {
            float force = current_mass * GRAVITY;
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << t << "s]"
                      << " mass=" << std::setw(6) << std::setprecision(1) << current_mass << "kg"
                      << " force=" << std::setw(7) << std::setprecision(1) << force << "N"
                      << " left_z=" << std::setprecision(2) << left_z << "m"
                      << " right_z=" << right_z << "m"
                      << std::endl;
        }

        // Render
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            float force_current = current_mass * GRAVITY;
            float percent_to_break = (force_current / BREAKING_FORCE) * 100.0f;

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Mass: " + std::to_string(current_mass).substr(0,6) + " kg", 255, 128, 0);
            ui->draw_text(10, 100, "Force: " + std::to_string(force_current).substr(0,7) + " N", 255, 128, 0);
            ui->draw_text(10, 130, "Stress: " + std::to_string(percent_to_break).substr(0,4) + "%",
                          percent_to_break > 90 ? 255 : 200,
                          percent_to_break > 90 ? 0 : 200,
                          0);

            if (gluon_broken) {
                ui->draw_text(10, 160, "🔥 GLUON BROKE!", 255, 0, 0);
            }

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[STEP 2] Analysis:" << std::endl;
    std::cout << "  Gluon broke: " << (gluon_broken ? "YES" : "NO") << std::endl;
    if (gluon_broken) {
        std::cout << "  Break time: " << break_time << "s (expected: " << EXPECTED_BREAK_TIME << "s)" << std::endl;
        std::cout << "  Break mass: " << (INITIAL_MASS + MASS_INCREASE_PER_SEC * break_time) << "kg (expected: " << BREAKING_MASS << "kg)" << std::endl;
    }
    std::cout << "  Particles on floor: " << (particles_on_floor ? "YES" : "NO") << std::endl;

    // Test passes if particles touched floor
    bool pass = particles_on_floor;

    if (!pass) {
        std::cout << "  ❌ FAIL: Particles didn't reach floor in 60 seconds" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Gluon broke and particles reached floor!" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back(gluon_broken ? "Broke: YES" : "Broke: NO");
        if (gluon_broken) {
            results.push_back("Time: " + std::to_string(break_time).substr(0,4) + "s");
        }
        results.push_back(particles_on_floor ? "Floor: YES" : "Floor: NO");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    // Store particle IDs for Case 3 (not used anymore, but kept for compatibility)
    totem.hanging_particle1_id = left_hang_id;
    totem.hanging_particle2_id = right_hang_id;

    return pass;
}

// ============================================================================
// Case 3: Unconstrained Hanging Particles (NO NAILS - should fall)
// ============================================================================
bool run_case_3_unconstrained_hanging(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 3: Unconstrained Hanging";
    std::string description = "Same particles WITHOUT constraints - should fall independently";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "NO constraints - free fall"});
    }

    // Reuse particles from Case 2 (constraints already removed)
    int id1 = totem.hanging_particle1_id;
    int id2 = totem.hanging_particle2_id;

    float initial_z1 = 0.0f;
    float initial_z2 = 0.0f;
    {
        auto particles = ps.lock_particles_for_read();
        initial_z1 = particles[id1].z;
        initial_z2 = particles[id2].z;
    }

    std::cout << "\n[STEP 1] Reusing particles from Case 2 (nails removed):" << std::endl;
    std::cout << "  P" << id1 << " (RED): z=" << initial_z1 << "m" << std::endl;
    std::cout << "  P" << id2 << " (BLUE): z=" << initial_z2 << "m" << std::endl;

    // Run simulation (3 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 180;

    std::cout << "\n[STEP 2] Running simulation for 3.0s @ 60 Hz (free fall)..." << std::endl;

    float final_z1 = 0.0f;
    float final_z2 = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        auto particles = ps.lock_particles_for_read();
        final_z1 = particles[id1].z;
        final_z2 = particles[id2].z;

        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " P" << id1 << ": z=" << std::setprecision(3) << final_z1 << "m"
                      << " | P" << id2 << ": z=" << final_z2 << "m" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "P1 z=" + std::to_string(final_z1).substr(0,6) + "m", 255, 128, 0);
            ui->draw_text(10, 90, "P2 z=" + std::to_string(final_z2).substr(0,6) + "m", 128, 0, 255);
            ui->draw_text(10, 120, "⚠️ NO constraints - falling freely", 255, 255, 0);
            engine.present();
        }
    }

    std::cout << "\n[STEP 3] Analysis:" << std::endl;
    std::cout << "  Final P1 z: " << final_z1 << "m" << std::endl;
    std::cout << "  Final P2 z: " << final_z2 << "m" << std::endl;

    // Test passes if both particles fell
    bool pass = (final_z1 < 1.0f) && (final_z2 < 1.0f);

    if (!pass) {
        std::cout << "  ❌ FAIL: Particles didn't fall as expected" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Particles fell independently (no constraints)!" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("P1 z: " + std::to_string(final_z1).substr(0,6) + "m");
        results.push_back("P2 z: " + std::to_string(final_z2).substr(0,6) + "m");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Main test entry point
// ============================================================================
bool test_totem_gluon_nails() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS EXPERIMENT - Gluon Constraint Testing (NAILS)   ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview (Gluon Constraints with Pre-built Totem):" << std::endl;
    std::cout << "  Setup: Create complete totem (no simulation, direct placement)" << std::endl;
    std::cout << "  Case 1: Hanging particles → test beam attachment with nails" << std::endl;
    std::cout << "  Case 2: Constrained particles → test constraint forces (NAILED)" << std::endl;
    std::cout << "  Case 3: Unconstrained particles → verify they fall freely (NO NAILS)" << std::endl;
    std::cout << "\nNote: Focus is on GLUON CONSTRAINTS, not collision stacking" << std::endl;

    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Experiment - Gluon Constraints (Nails)";
    config.enable_chat_window = false;  // Disable chat for physics tests

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
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    light.is_light_source = true;
    light.emission_strength = 100000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f; light.a = 1.0f;
    engine.add_particle(light);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-5.0f, -5.0f, 5.0f);
    camera.look_at(0.0f, 0.0f, 1.0f);

    // Physics: Gravity
    std::cout << "\n[PHYSICS] Configuring gravity..." << std::endl;
    auto& physics = engine.get_physics_system();
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));
    std::cout << "  Gravity: (0, 0, -9.8) m/s²" << std::endl;

    // Create totem state
    TotemState totem;
    const double dt = 1.0 / 60.0;

    // =========================================================================
    // PHASE 1: Wait for SPACE to create totem
    // =========================================================================
    if (is_interactive) {
        std::vector<std::string> phase1_info = {
            "Blank screen - waiting for SPACE",
            "Press SPACE to create totem and begin 5-second settling"
        };
        wait_for_space(engine, "PHASE 1: CREATE TOTEM", phase1_info);
    }

    // Create totem (direct placement, no simulation yet)
    std::cout << "\n[TOTEM] Creating complete totem..." << std::endl;
    create_complete_totem(engine, totem);
    std::cout << "  Totem created: " << totem.piece_ids.size() << " pieces" << std::endl;

    // =========================================================================
    // PHASE 2: 5-second settling period (runs immediately after creation)
    // =========================================================================
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║         SETTLING TOTEM FOR 5 SECONDS @ 60Hz                  ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nAllowing totem to settle and reach equilibrium..." << std::endl;

    const int settle_frames = 300;  // 5 seconds @ 60Hz
    std::cout << "Frames: " << settle_frames << " (5 seconds)\n" << std::endl;

    for (int frame = 0; frame < settle_frames; frame++) {
        engine.update(dt);

        // Show progress every second
        if (frame % 60 == 0) {
            std::cout << "  Settling... " << (frame / 60) << "s / 5s" << std::endl;
        }

        // Render in interactive mode (with proper presentation)
        if (is_interactive && frame % 2 == 0) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "SETTLING TOTEM - 5 SECONDS", 255, 255, 0);
            ui->draw_text(10, 40, "Progress: " + std::to_string(frame / 60) + "s / 5s", 200, 200, 200);
            ui->draw_text(10, 70, "Frame: " + std::to_string(frame) + " / 300", 150, 150, 150);

            engine.present();
        }
    }

    std::cout << "\n✅ Totem settled and at rest." << std::endl;

    // =========================================================================
    // PHASE 3: Wait for SPACE before running Case 1
    // =========================================================================
    if (is_interactive) {
        std::vector<std::string> phase3_info = {
            "Totem has settled for 5 seconds",
            "Ready to run Case 1: Hanging Particles Test",
            "",
            "Press SPACE to add hanging particles with nail gluons"
        };
        wait_for_space(engine, "PHASE 3: READY FOR CASE 1", phase3_info);
    }

    // =========================================================================
    // PHASE 4: Run Case 1 (hanging particles)
    // =========================================================================
    bool case1_pass = run_case_1_hanging_particles(engine, is_interactive, totem);
    bool case2_pass = run_case_2_constrained_hanging(engine, is_interactive, totem);

    // Final summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║         GLUON CONSTRAINT TESTING COMPLETE                    ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1 (Hanging particles): " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 2 (Gluon Breaking Test): " << (case2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "\n  Total totem pieces: " << totem.piece_ids.size() << " (+ " << totem.floor_ids.size() << " floor tiles)" << std::endl;

    bool all_pass = case1_pass && case2_pass;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED! GLUON CONSTRAINTS WORKING!" : "❌ SOME TESTS FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1 (Hanging): " + std::string(case1_pass ? "PASS" : "FAIL"),
            "Case 2 (Breaking): " + std::string(case2_pass ? "PASS" : "FAIL"),
            "",
            "Total pieces: " + std::to_string(totem.piece_ids.size()),
            "",
            all_pass ? "CONSTRAINTS WORKING!" : "SOME TESTS FAILED"
        };
        wait_for_space(engine, "GLUON TESTING COMPLETE", summary);
    }

    return all_pass;
}
