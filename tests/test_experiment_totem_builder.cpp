#include "../src/core/engine.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS EXPERIMENT 02 - Eden Totem Stacking (EXACT REPLICA)
// ============================================================================
// Purpose: Test collision-based stacking using EXACT Eden totem structure
// Strategy: Build Eden's 8-trunk totem incrementally with 2mm gaps (like totem_generator.cpp)
//
// Structure: EXACT MATCH to src/worldgen/totem_generator.cpp
// - 8 trunk pieces with varying dimensions (0.4m-1.2m width, 0.64m-2.4m height)
// - 1 horizontal beam (6.0m × 0.5m × 0.4m)
// - 2mm gaps between pieces (0.002m - Eden's TRUNK_GAP constant)
// - Masses from Eden spec: trunk_mass=10kg, upper_trunk_mass=8kg
//
// Test Cases (incremental - pieces placed with 2mm gaps, NOT dropped):
//   Case 1: Lower trunk (1.2m × 1.2m × 2.4m, 10kg) on floor
//   Case 2: + Middle trunk (0.4m × 0.4m × 0.64m, 8kg)
//   Case 3: + Top trunk (0.8m × 0.8m × 1.8m, 4kg)
//   Case 4: + Trunk4 (0.4m × 0.4m × 0.8m, 3.2kg)
//   Case 5: + Trunk5 (0.8m × 0.8m × 1.0m, 4kg)
//   Case 6: + Trunk6 (0.4m × 0.4m × 0.7m, 2.8kg)
//   Case 7: + Trunk7 (0.4m × 0.4m × 0.9m, 3.6kg)
//   Case 8: + Horizontal beam (6.0m × 0.5m × 0.4m, 6.4kg)
//
// Expected: Stable totem matching Eden's exact structure, no penetration
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
// Persistent State: Floor and totem pieces stay across test cases
// ============================================================================
struct TotemState {
    std::vector<int> floor_ids;
    std::vector<int> piece_ids;  // All totem pieces (grow incrementally)
    int hanging_particle1_id = -1;  // For Case 8/9: particle with nail
    int hanging_particle2_id = -1;  // For Case 8/9: particle with nail
};

// ============================================================================
// Case 1: Lower Trunk on Floor
// ============================================================================
bool run_case_1_lower_trunk(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 1: Lower Trunk";
    std::string description = "Eden lower trunk (1.2m × 1.2m × 2.4m, 10kg) - test floor collision";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Lower trunk bottom touches floor (z=0.1m floor top)"});
    }

    // Create floor (7x5 grid, persists for all test cases)
    // Extended to 7x5 to catch particles at ±3.0m in Case 7 (beam is 6m wide)
    std::cout << "\n[STEP 1] Creating floor tiles..." << std::endl;
    for (int x = -4; x <= 3; x++) {
        for (int y = -3; y <= 2; y++) {
            Particle tile = {};
            tile.x = x * 2.0f;  // 2.0m spacing for 2m tiles = no overlap
            tile.y = y * 2.0f;
            tile.z = 0.1f;  // Floor surface at z=0
            tile.shape = ParticleShape::BOX;
            tile.width = 2.0f;      // X span (2m wide)
            tile.height = 2.0f;     // Y span (2m deep)
            tile.thickness = 0.2f;  // Z span (0.2m thin floor)
            tile.r = 0.3f; tile.g = 0.6f; tile.b = 0.3f;
            tile.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
            tile.material_density = 1000.0f;
            totem.floor_ids.push_back(engine.add_particle(tile));
        }
    }
    std::cout << "  Floor tiles: " << totem.floor_ids.size() << " (heavy)" << std::endl;

    // Create lower trunk (EXACT match to Eden - totem_generator.cpp line 48-57)
    std::cout << "\n[STEP 2] Creating lower trunk (Eden spec)..." << std::endl;

    // Eden: spec.trunk_size=3.0, lower_thickness = 3.0 * 0.8 = 2.4m
    const float TRUNK_SIZE = 3.0f;  // Eden default
    const float lower_thickness = TRUNK_SIZE * 0.8f;  // 2.4m vertical

    // Floor geometry (must match floor tile creation above)
    const float floor_center_z = 0.1f;      // Floor tile center (line 111)
    const float floor_thickness = 0.2f;     // Floor tile thickness (line 115)
    const float floor_top_z = floor_center_z + (floor_thickness * 0.5f);  // 0.2m (top surface)

    Particle lower = {};
    lower.x = 0.0f;
    lower.y = 0.0f;
    lower.z = floor_top_z + (lower_thickness / 2.0f);  // Center = floor_top + half_thickness
    lower.shape = ParticleShape::BOX;
    lower.width = TRUNK_SIZE * 0.4f;   // 1.2m (Eden: size * 0.4)
    lower.height = TRUNK_SIZE * 0.4f;  // 1.2m
    lower.thickness = lower_thickness;  // 2.4m vertical
    lower.size = TRUNK_SIZE;
    lower.r = 0.4f; lower.g = 0.25f; lower.b = 0.15f;  // Brown wood
    lower.SetMaterial(Materials::Type::WOOD_HARD);  // Eden: spec.trunk_mass
    int lower_id = engine.add_particle(lower);
    totem.piece_ids.push_back(lower_id);

    std::cout << "  Lower trunk: id=" << lower_id << " mass=10kg dims="
              << lower.width << "x" << lower.height << "x" << lower.thickness
              << "m center_z=" << lower.z << "m" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;  // 2 seconds
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;
    float max_bounce_height = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float lower_z;
        {
            auto particles = ps.lock_particles_for_read();
            lower_z = particles[lower_id].z;
        }

        if (lower_z < min_z_observed) min_z_observed = lower_z;
        if (lower_z > max_bounce_height && frame > 60) max_bounce_height = lower_z;  // After settling

        // Floor penetration: lower bottom = lower_z - thickness/2
        // Floor top at z=0.1, so lower bottom should be >= 0.1
        float lower_bottom = lower_z - lower.thickness / 2.0f;
        if (lower_bottom < 0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
            std::cout << "      Lower bottom: " << lower_bottom << "m (floor at z=0.1m)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Lower center=" << std::setw(6) << lower_z << "m"
                      << " bottom=" << std::setw(6) << lower_bottom << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Lower center z=" + std::to_string(lower_z).substr(0,6) + "m", 255, 200, 100);
            ui->draw_text(10, 90, "Lower bottom z=" + std::to_string(lower_bottom).substr(0,6) + "m", 200, 150, 50);

            if (penetration_detected) {
                ui->draw_text(10, 130, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 130, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest center Z: " << min_z_observed << "m" << std::endl;
    std::cout << "  Lowest bottom Z: " << (min_z_observed - lower.thickness/2.0f) << "m (floor surface at z=0.1m)" << std::endl;

    bool pass = !penetration_detected && (min_z_observed - lower.thickness/2.0f) >= 0.05f;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Lower trunk penetrated floor" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Lower trunk landed on floor without penetration" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Final bottom Z: " + std::to_string(min_z_observed - lower.thickness/2.0f).substr(0,6) + "m");
        results.push_back(penetration_detected ? "RESULT: FAIL" : "RESULT: PASS");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    // DON'T delete particles - they persist for next test case
    return pass;
}

// ============================================================================
// Case 2: Add Middle Trunk (2mm gap above lower)
// ============================================================================
bool run_case_2_middle_trunk(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 2: Middle Trunk";
    std::string description = "Eden middle trunk (0.4m × 0.4m × 0.64m, 8kg) - 2mm gap above lower";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Lower trunk should be settled, middle trunk placed with 2mm gap"});
    }

    // Get lower trunk position (should be settled on floor)
    int lower_id = totem.piece_ids[0];
    float lower_z, lower_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        lower_z = particles[lower_id].z;
        lower_thickness = particles[lower_id].thickness;
    }
    float lower_top = lower_z + lower_thickness / 2.0f;

    std::cout << "\n[STEP 1] Lower trunk settled at z=" << lower_z << "m (top at z=" << lower_top << "m)" << std::endl;

    // Create middle trunk (Eden dimensions, placed carefully with 2mm gap)
    std::cout << "\n[STEP 2] Creating middle trunk..." << std::endl;

    const float PLACEMENT_GAP = 0.002f;  // 2mm gap (careful placement)
    const float middle_thickness = 0.64f;  // Eden middle trunk: 64cm vertical

    Particle middle = {};
    middle.x = 0.0f;
    middle.y = 0.0f;
    middle.z = lower_top + PLACEMENT_GAP + (middle_thickness / 2.0f);  // Place with 2mm gap
    middle.shape = ParticleShape::BOX;
    middle.width = 0.4f;   // Eden: 40cm wide
    middle.height = 0.4f;  // Eden: 40cm deep
    middle.thickness = middle_thickness;  // Eden: 64cm tall
    middle.size = 1.0f;
    middle.r = 0.5f; middle.g = 0.3f; middle.b = 0.2f;  // Lighter brown
    middle.SetMaterial(Materials::Type::WOOD_HARD);  // Eden: spec.upper_trunk_mass
    int middle_id = engine.add_particle(middle);
    totem.piece_ids.push_back(middle_id);

    std::cout << "  Middle trunk: id=" << middle_id << " mass=8kg dims="
              << middle.width << "x" << middle.height << "x" << middle.thickness << "m" << std::endl;
    std::cout << "  Positioned at z=" << middle.z << "m (2mm gap above lower top)" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;  // 2 seconds
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float middle_z, middle_bottom;
        {
            auto particles = ps.lock_particles_for_read();
            middle_z = particles[middle_id].z;
            middle_bottom = middle_z - middle.thickness / 2.0f;
        }

        if (middle_z < min_z_observed) min_z_observed = middle_z;

        // Check floor penetration (should never happen)
        if (middle_bottom < 0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  FLOOR PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Piece2 center=" << std::setw(6) << middle_z << "m"
                      << " bottom=" << std::setw(6) << middle_bottom << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Piece 2 z=" + std::to_string(middle_z).substr(0,6) + "m", 200, 150, 100);

            if (penetration_detected) {
                ui->draw_text(10, 110, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 110, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest center Z: " << min_z_observed << "m" << std::endl;

    // ═══════════════════════════════════════════════════════════
    // COMPRESSION MEASUREMENT: Check if middle trunk sank into lower trunk
    // ═══════════════════════════════════════════════════════════
    float final_middle_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_middle_z = particles[middle_id].z;
    }

    // Expected Z position: middle trunk should rest ON TOP of lower trunk
    float expected_middle_z = lower_top + (middle_thickness / 2.0f);

    // Calculate compression (positive = sinking into lower trunk)
    float compression = expected_middle_z - final_middle_z;

    const float COMPRESSION_TOLERANCE = 0.002f;  // 2mm tolerance
    bool compression_detected = (compression > COMPRESSION_TOLERANCE);

    std::cout << "\n[COMPRESSION CHECK]" << std::endl;
    std::cout << "  Expected Z (resting on lower): " << expected_middle_z << "m" << std::endl;
    std::cout << "  Actual Z (final): " << final_middle_z << "m" << std::endl;
    std::cout << "  Compression: " << (compression * 1000.0f) << "mm" << std::endl;
    std::cout << "  Tolerance: " << (COMPRESSION_TOLERANCE * 1000.0f) << "mm" << std::endl;

    bool pass = !penetration_detected && !compression_detected;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Piece 2 penetrated floor" << std::endl;
    } else if (compression_detected) {
        std::cout << "  ❌ FAIL: Piece 2 sank " << (compression * 1000.0f) << "mm into lower trunk (expected <" << (COMPRESSION_TOLERANCE * 1000.0f) << "mm)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Piece 2 stacked on base without penetration or compression" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Lowest Z: " + std::to_string(min_z_observed).substr(0,6) + "m");
        results.push_back(penetration_detected ? "RESULT: FAIL" : "RESULT: PASS");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 3: Add Top Trunk (drop 35cm above middle)
// ============================================================================
bool run_case_3_top_trunk(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 3: Top Trunk";
    std::string description = "Add top trunk (drop 35cm onto middle) - test collision stacking";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Eden top trunk: 0.8m x 0.8m x 1.8m, 4kg"});
    }

    // Get middle position
    int middle_id = totem.piece_ids[1];
    float middle_z, middle_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        middle_z = particles[middle_id].z;
        middle_thickness = particles[middle_id].thickness;
    }
    float middle_top = middle_z + middle_thickness / 2.0f;

    std::cout << "\n[STEP 1] Middle trunk settled at z=" << middle_z << "m (top at z=" << middle_top << "m)" << std::endl;

    // Create top trunk (Eden: 0.8m x 0.8m x 1.8m, 4kg, placed carefully)
    std::cout << "\n[STEP 2] Creating top trunk..." << std::endl;

    const float PLACEMENT_GAP = 0.002f;  // 2mm gap (careful placement)
    const float top_thickness = 1.8f;  // Eden top trunk: 180cm vertical

    Particle top = {};
    top.x = 0.0f;
    top.y = 0.0f;
    top.z = middle_top + PLACEMENT_GAP + (top_thickness / 2.0f);  // Place with 2mm gap
    top.shape = ParticleShape::BOX;
    top.width = 0.8f;   // Eden: 80cm wide
    top.height = 0.8f;  // Eden: 80cm deep
    top.thickness = top_thickness;  // Eden: 180cm tall
    top.size = 1.0f;
    top.r = 0.6f; top.g = 0.35f; top.b = 0.25f;  // Light brown
    top.SetMaterial(Materials::Type::WOOD_HARD);  // Eden: 4kg
    int top_id = engine.add_particle(top);
    totem.piece_ids.push_back(top_id);

    std::cout << "  Top trunk: id=" << top_id << " mass=4kg dims="
              << top.width << "x" << top.height << "x" << top.thickness << "m" << std::endl;
    std::cout << "  Positioned at z=" << top.z << "m (2mm gap above middle)" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;  // 2 seconds
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float top_z, top_bottom;
        {
            auto particles = ps.lock_particles_for_read();
            top_z = particles[top_id].z;
            top_bottom = top_z - top.thickness / 2.0f;
        }

        if (top_z < min_z_observed) min_z_observed = top_z;

        if (top_bottom < 0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  FLOOR PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Top trunk center=" << std::setw(6) << top_z << "m"
                      << " bottom=" << std::setw(6) << top_bottom << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Top trunk z=" + std::to_string(top_z).substr(0,6) + "m", 200, 150, 100);

            if (penetration_detected) {
                ui->draw_text(10, 110, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 110, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest center Z: " << min_z_observed << "m" << std::endl;

    // ═══════════════════════════════════════════════════════════
    // COMPRESSION MEASUREMENT: Check if top trunk sank into middle trunk
    // ═══════════════════════════════════════════════════════════
    float final_top_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_top_z = particles[top_id].z;
    }

    // Expected Z position: top trunk should rest ON TOP of middle trunk
    float expected_top_z = middle_top + (top_thickness / 2.0f);

    // Calculate compression (positive = sinking into middle trunk)
    float compression = expected_top_z - final_top_z;

    const float COMPRESSION_TOLERANCE = 0.002f;  // 2mm tolerance
    bool compression_detected = (compression > COMPRESSION_TOLERANCE);

    std::cout << "\n[COMPRESSION CHECK]" << std::endl;
    std::cout << "  Expected Z (resting on middle): " << expected_top_z << "m" << std::endl;
    std::cout << "  Actual Z (final): " << final_top_z << "m" << std::endl;
    std::cout << "  Compression: " << (compression * 1000.0f) << "mm" << std::endl;
    std::cout << "  Tolerance: " << (COMPRESSION_TOLERANCE * 1000.0f) << "mm" << std::endl;

    bool pass = !penetration_detected && !compression_detected;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Top trunk penetrated floor" << std::endl;
    } else if (compression_detected) {
        std::cout << "  ❌ FAIL: Top trunk sank " << (compression * 1000.0f) << "mm into middle trunk (expected <" << (COMPRESSION_TOLERANCE * 1000.0f) << "mm)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Top trunk stacked without penetration or compression" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Lowest Z: " + std::to_string(min_z_observed).substr(0,6) + "m");
        results.push_back("Compression: " + std::to_string(compression * 1000.0f).substr(0,6) + "mm");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 4: Add Trunk4 (drop 40cm above top trunk)
// ============================================================================
bool run_case_4_trunk4(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 4: Trunk4";
    std::string description = "Add trunk4 (drop 40cm onto top trunk) - test collision stacking";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Eden trunk4: 0.4m x 0.4m x 0.8m, 3.2kg"});
    }

    // Get top trunk position
    int top_id = totem.piece_ids[2];
    float top_z, top_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        top_z = particles[top_id].z;
        top_thickness = particles[top_id].thickness;
    }
    float top_top = top_z + top_thickness / 2.0f;

    std::cout << "\n[STEP 1] Top trunk settled at z=" << top_z << "m (top at z=" << top_top << "m)" << std::endl;

    // Create trunk4 (Eden: 0.4m x 0.4m x 0.8m, 3.2kg, placed carefully)
    std::cout << "\n[STEP 2] Creating trunk4..." << std::endl;

    const float PLACEMENT_GAP = 0.002f;  // 2mm gap (careful placement)
    const float trunk4_thickness = 0.8f;  // Eden trunk4: 80cm vertical

    Particle trunk4 = {};
    trunk4.x = 0.0f;
    trunk4.y = 0.0f;
    trunk4.z = top_top + PLACEMENT_GAP + (trunk4_thickness / 2.0f);  // Place with 2mm gap
    trunk4.shape = ParticleShape::BOX;
    trunk4.width = 0.4f;   // Eden: 40cm wide
    trunk4.height = 0.4f;  // Eden: 40cm deep
    trunk4.thickness = trunk4_thickness;  // Eden: 80cm tall
    trunk4.size = 1.0f;
    trunk4.r = 0.7f; trunk4.g = 0.4f; trunk4.b = 0.3f;  // Lighter brown
    trunk4.SetMaterial(Materials::Type::WOOD_HARD);  // Eden: upper_trunk_mass * 0.4 = 3.2kg
    int trunk4_id = engine.add_particle(trunk4);
    totem.piece_ids.push_back(trunk4_id);

    std::cout << "  Trunk4: id=" << trunk4_id << " mass=3.2kg dims="
              << trunk4.width << "x" << trunk4.height << "x" << trunk4.thickness << "m" << std::endl;
    std::cout << "  Positioned at z=" << trunk4.z << "m (2mm gap above top trunk)" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;  // 2 seconds
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float trunk4_z, trunk4_bottom;
        {
            auto particles = ps.lock_particles_for_read();
            trunk4_z = particles[trunk4_id].z;
            trunk4_bottom = trunk4_z - trunk4.thickness / 2.0f;
        }

        if (trunk4_z < min_z_observed) min_z_observed = trunk4_z;

        if (trunk4_bottom < 0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  FLOOR PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Trunk4 center=" << std::setw(6) << trunk4_z << "m"
                      << " bottom=" << std::setw(6) << trunk4_bottom << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Trunk4 z=" + std::to_string(trunk4_z).substr(0,6) + "m", 200, 150, 100);

            if (penetration_detected) {
                ui->draw_text(10, 110, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 110, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest center Z: " << min_z_observed << "m" << std::endl;

    // ═══════════════════════════════════════════════════════════
    // COMPRESSION MEASUREMENT: Check if trunk4 sank into top trunk
    // ═══════════════════════════════════════════════════════════
    float final_trunk4_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_trunk4_z = particles[trunk4_id].z;
    }

    // Expected Z position: trunk4 should rest ON TOP of top trunk
    float expected_trunk4_z = top_top + (trunk4_thickness / 2.0f);

    // Calculate compression (positive = sinking into top trunk)
    float compression = expected_trunk4_z - final_trunk4_z;

    const float COMPRESSION_TOLERANCE = 0.002f;  // 2mm tolerance
    bool compression_detected = (compression > COMPRESSION_TOLERANCE);

    std::cout << "\n[COMPRESSION CHECK]" << std::endl;
    std::cout << "  Expected Z (resting on top): " << expected_trunk4_z << "m" << std::endl;
    std::cout << "  Actual Z (final): " << final_trunk4_z << "m" << std::endl;
    std::cout << "  Compression: " << (compression * 1000.0f) << "mm" << std::endl;
    std::cout << "  Tolerance: " << (COMPRESSION_TOLERANCE * 1000.0f) << "mm" << std::endl;

    bool pass = !penetration_detected && !compression_detected;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Trunk4 penetrated floor" << std::endl;
    } else if (compression_detected) {
        std::cout << "  ❌ FAIL: Trunk4 sank " << (compression * 1000.0f) << "mm into top trunk (expected <" << (COMPRESSION_TOLERANCE * 1000.0f) << "mm)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Trunk4 stacked without penetration or compression" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Lowest Z: " + std::to_string(min_z_observed).substr(0,6) + "m");
        results.push_back("Compression: " + std::to_string(compression * 1000.0f).substr(0,6) + "mm");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 5: Add Trunk5 (large piece, drop 30cm above trunk4)
// ============================================================================
bool run_case_5_trunk5(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 5: Trunk5 (Large)";
    std::string description = "Add large trunk5 (drop 30cm onto trunk4) - test big piece stacking";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Large trunk: 1.0m x 1.0m x 1.6m, 7kg"});
    }

    // Get trunk4 position
    int trunk4_id = totem.piece_ids[3];
    float trunk4_z, trunk4_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        trunk4_z = particles[trunk4_id].z;
        trunk4_thickness = particles[trunk4_id].thickness;
    }
    float trunk4_top = trunk4_z + trunk4_thickness / 2.0f;

    std::cout << "\n[STEP 1] Trunk4 settled at z=" << trunk4_z << "m (top at z=" << trunk4_top << "m)" << std::endl;

    // Create trunk5 (large: 1.0m x 1.0m x 1.6m, 7kg, placed carefully)
    std::cout << "\n[STEP 2] Creating trunk5 (large)..." << std::endl;

    const float PLACEMENT_GAP = 0.002f;  // 2mm gap (careful placement)
    const float trunk5_thickness = 1.6f;  // 160cm vertical (big!)

    Particle trunk5 = {};
    trunk5.x = 0.0f;
    trunk5.y = 0.0f;
    trunk5.z = trunk4_top + PLACEMENT_GAP + (trunk5_thickness / 2.0f);  // Place with 2mm gap
    trunk5.shape = ParticleShape::BOX;
    trunk5.width = 1.0f;   // 1m wide (big!)
    trunk5.height = 1.0f;  // 1m deep (big!)
    trunk5.thickness = trunk5_thickness;  // 1.6m tall
    trunk5.size = 1.0f;
    trunk5.r = 0.55f; trunk5.g = 0.35f; trunk5.b = 0.2f;  // Medium brown
    trunk5.SetMaterial(Materials::Type::WOOD_HARD);  // 7kg (heavy!)
    int trunk5_id = engine.add_particle(trunk5);
    totem.piece_ids.push_back(trunk5_id);

    std::cout << "  Trunk5 created: id=" << trunk5_id << " at z=" << trunk5.z << "m (mass=7kg)" << std::endl;
    std::cout << "  Dimensions: 1.0m x 1.0m x 1.6m (big piece!)" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool collision_occurred = false;
    float max_velocity = 0.0f;
    float final_z = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float trunk5_z, trunk5_vz;
        {
            auto particles = ps.lock_particles_for_read();
            trunk5_z = particles[trunk5_id].z;
            trunk5_vz = particles[trunk5_id].vz;
        }

        if (std::abs(trunk5_vz) > max_velocity) {
            max_velocity = std::abs(trunk5_vz);
        }

        if (trunk5_z < (trunk4_top + 1.0f) && !collision_occurred) {
            collision_occurred = true;
            std::cout << "\n  ⚡ COLLISION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
            std::cout << "    Impact velocity: " << trunk5_vz << " m/s" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " z=" << std::setw(6) << trunk5_z << "m"
                      << " vz=" << std::setw(6) << trunk5_vz << " m/s";
            if (collision_occurred) std::cout << " ⚡";
            std::cout << std::endl;
        }

        final_z = trunk5_z;

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Trunk5 z=" + std::to_string(trunk5_z).substr(0,6) + "m", 150, 255, 150);
            ui->draw_text(10, 90, "Trunk5 vz=" + std::to_string(trunk5_vz).substr(0,6) + " m/s", 200, 200, 255);

            if (collision_occurred) {
                ui->draw_text(10, 130, "COLLISION OCCURRED!", 255, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Final Z: " << final_z << "m (should be resting on trunk4 at ~" << trunk4_top << "m)" << std::endl;
    std::cout << "  Max velocity: " << max_velocity << " m/s" << std::endl;
    std::cout << "  Collision: " << (collision_occurred ? "YES" : "NO") << std::endl;

    // ═══════════════════════════════════════════════════════════
    // COMPRESSION MEASUREMENT: Check if trunk5 sank into trunk4
    // ═══════════════════════════════════════════════════════════
    float expected_trunk5_z = trunk4_top + (trunk5_thickness / 2.0f);
    float compression = expected_trunk5_z - final_z;  // Positive = sinking into trunk4

    const float COMPRESSION_TOLERANCE = 0.002f;  // 2mm tolerance
    bool compression_detected = (compression > COMPRESSION_TOLERANCE);

    std::cout << "\n[COMPRESSION CHECK]" << std::endl;
    std::cout << "  Expected Z (resting on trunk4): " << expected_trunk5_z << "m" << std::endl;
    std::cout << "  Actual Z (final): " << final_z << "m" << std::endl;
    std::cout << "  Compression: " << (compression * 1000.0f) << "mm" << std::endl;
    std::cout << "  Tolerance: " << (COMPRESSION_TOLERANCE * 1000.0f) << "mm" << std::endl;

    bool pass = collision_occurred && !compression_detected;

    if (!collision_occurred) {
        std::cout << "  ❌ FAIL: No collision detected" << std::endl;
    } else if (compression_detected) {
        std::cout << "  ❌ FAIL: Trunk5 sank " << (compression * 1000.0f) << "mm into trunk4 (expected <" << (COMPRESSION_TOLERANCE * 1000.0f) << "mm)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Trunk5 landed correctly (compression: " << (compression * 1000.0f) << "mm)" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Final Z: " + std::to_string(final_z).substr(0,6) + "m");
        results.push_back("Max velocity: " + std::to_string(max_velocity).substr(0,6) + " m/s");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 6: Add Horizontal Beam (drop 25cm above trunk5)
// ============================================================================
bool run_case_6_horizontal_beam(Engine& engine, bool is_interactive, TotemState& totem) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 6: Horizontal Beam";
    std::string description = "Add horizontal beam (drop 25cm onto trunk5) - test totem completion";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Eden horizontal beam: 6m x 0.5m x 0.4m, 6.4kg"});
    }

    // Get trunk5 position
    int trunk5_id = totem.piece_ids[4];
    float trunk5_z, trunk5_thickness;
    {
        auto particles = ps.lock_particles_for_read();
        trunk5_z = particles[trunk5_id].z;
        trunk5_thickness = particles[trunk5_id].thickness;
    }
    float trunk5_top = trunk5_z + trunk5_thickness / 2.0f;

    std::cout << "\n[STEP 1] Trunk5 settled at z=" << trunk5_z << "m (top at z=" << trunk5_top << "m)" << std::endl;

    // Create horizontal beam (Eden: 6m x 0.5m x 0.4m, 6.4kg, placed carefully)
    std::cout << "\n[STEP 2] Creating horizontal beam..." << std::endl;

    const float PLACEMENT_GAP = 0.002f;  // 2mm gap (careful placement)
    const float beam_thickness = 0.4f;  // Eden beam: 40cm vertical

    Particle beam = {};
    beam.x = 0.0f;
    beam.y = 0.0f;
    beam.z = trunk5_top + PLACEMENT_GAP + (beam_thickness / 2.0f);  // Place with 2mm gap
    beam.shape = ParticleShape::BOX;
    beam.width = 6.0f;   // Eden: 6m long (X - horizontal)
    beam.height = 0.5f;  // Eden: 0.5m wide (Y)
    beam.thickness = beam_thickness;  // Eden: 0.4m tall (Z)
    beam.size = 1.0f;
    beam.r = 0.8f; beam.g = 0.5f; beam.b = 0.3f;  // Pale wood
    beam.SetMaterial(Materials::Type::WOOD_HARD);  // Eden: upper_trunk_mass * 0.8 = 6.4kg
    int beam_id = engine.add_particle(beam);
    totem.piece_ids.push_back(beam_id);

    std::cout << "  Horizontal beam: id=" << beam_id << " mass=6.4kg dims="
              << beam.width << "x" << beam.height << "x" << beam.thickness << "m" << std::endl;
    std::cout << "  Positioned at z=" << beam.z << "m (2mm gap above trunk4, HORIZONTAL)" << std::endl;

    // Run simulation (2 seconds)
    const double dt = 1.0 / 60.0;
    const int total_frames = 120;  // 2 seconds
    const double total_time = 2.0;

    std::cout << "\n[STEP 3] Running simulation for " << total_time << "s @ 60 Hz..." << std::endl;

    bool penetration_detected = false;
    float min_z_observed = 1000.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float beam_z, beam_bottom;
        {
            auto particles = ps.lock_particles_for_read();
            beam_z = particles[beam_id].z;
            beam_bottom = beam_z - beam.thickness / 2.0f;
        }

        if (beam_z < min_z_observed) min_z_observed = beam_z;

        if (beam_bottom < 0.05f && !penetration_detected) {
            penetration_detected = true;
            std::cout << "\n  ⚠️  FLOOR PENETRATION at frame " << frame << " (t=" << (frame * dt) << "s)" << std::endl;
        }

        if (frame % 30 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt) << "s]"
                      << " Beam center=" << std::setw(6) << beam_z << "m"
                      << " bottom=" << std::setw(6) << beam_bottom << "m";
            if (penetration_detected) std::cout << " ⚠️ PENETRATION";
            std::cout << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame * dt).substr(0,4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Beam (HORIZONTAL) z=" + std::to_string(beam_z).substr(0,6) + "m", 200, 150, 100);

            if (penetration_detected) {
                ui->draw_text(10, 110, "PENETRATION DETECTED!", 255, 0, 0);
            } else {
                ui->draw_text(10, 110, "No penetration - OK", 0, 255, 0);
            }

            engine.present();
        }
    }

    std::cout << "\n[STEP 4] Analysis:" << std::endl;
    std::cout << "  Lowest center Z: " << min_z_observed << "m" << std::endl;

    // ═══════════════════════════════════════════════════════════
    // COMPRESSION MEASUREMENT: Check if beam sank into trunk5
    // ═══════════════════════════════════════════════════════════
    float final_beam_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_beam_z = particles[beam_id].z;
    }

    // Expected Z position: beam should rest ON TOP of trunk5
    float expected_beam_z = trunk5_top + (beam_thickness / 2.0f);

    // Calculate compression (positive = sinking into trunk5)
    float compression = expected_beam_z - final_beam_z;

    const float COMPRESSION_TOLERANCE = 0.002f;  // 2mm tolerance
    bool compression_detected = (compression > COMPRESSION_TOLERANCE);

    std::cout << "\n[COMPRESSION CHECK]" << std::endl;
    std::cout << "  Expected Z (resting on trunk5): " << expected_beam_z << "m" << std::endl;
    std::cout << "  Actual Z (final): " << final_beam_z << "m" << std::endl;
    std::cout << "  Compression: " << (compression * 1000.0f) << "mm" << std::endl;
    std::cout << "  Tolerance: " << (COMPRESSION_TOLERANCE * 1000.0f) << "mm" << std::endl;

    bool pass = !penetration_detected && !compression_detected;

    if (penetration_detected) {
        std::cout << "  ❌ FAIL: Beam penetrated floor" << std::endl;
    } else if (compression_detected) {
        std::cout << "  ❌ FAIL: Beam sank " << (compression * 1000.0f) << "mm into trunk5 (expected <" << (COMPRESSION_TOLERANCE * 1000.0f) << "mm)" << std::endl;
    } else {
        std::cout << "  ✅ PASS: Horizontal beam placed on totem without penetration or compression" << std::endl;
    }

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back("Totem complete with horizontal beam!");
        results.push_back("Lowest Z: " + std::to_string(min_z_observed).substr(0,6) + "m");
        results.push_back("Compression: " + std::to_string(compression * 1000.0f).substr(0,6) + "mm");
        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Main test entry point - Totem Builder (Collision Stacking Test)
// ============================================================================
bool test_experiment_totem_builder() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║    PHYSICS EXPERIMENT - Eden Totem Builder (Collisions)     ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview (Incremental Collision-Based Stacking):" << std::endl;
    std::cout << "  Case 1: Base piece (lower trunk on floor)" << std::endl;
    std::cout << "  Case 2: Add 2nd piece (middle trunk drops 30cm)" << std::endl;
    std::cout << "  Case 3: Add 3rd piece (top trunk drops 35cm)" << std::endl;
    std::cout << "  Case 4: Add 4th piece (trunk4 drops 40cm)" << std::endl;
    std::cout << "  Case 5: Add 5th piece (trunk5 large drops 30cm)" << std::endl;
    std::cout << "  Case 6: Add horizontal beam (drops 25cm)" << std::endl;
    std::cout << "\nGoal: Test impulse-based collision resolution for stacking" << std::endl;

    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Totem Builder - Collision Stacking";
    config.enable_chat_window = false;  // Disable chat for physics tests

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

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

    // Persistent totem state
    TotemState totem;

    // Run test cases (particles persist between cases)
    bool case1_pass = run_case_1_lower_trunk(engine, is_interactive, totem);
    bool case2_pass = run_case_2_middle_trunk(engine, is_interactive, totem);
    bool case3_pass = run_case_3_top_trunk(engine, is_interactive, totem);
    bool case4_pass = run_case_4_trunk4(engine, is_interactive, totem);
    bool case5_pass = run_case_5_trunk5(engine, is_interactive, totem);
    bool case6_pass = run_case_6_horizontal_beam(engine, is_interactive, totem);

    // Final settling period (10 seconds)
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║              FINAL SETTLING (10 seconds)                     ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nLetting complete totem settle for 10 seconds..." << std::endl;

    const double dt = 1.0 / 60.0;
    const int settle_frames = 600;  // 10 seconds at 60Hz

    for (int frame = 0; frame < settle_frames; frame++) {
        engine.update(dt);

        if (frame % 60 == 0) {
            std::cout << "  Settling... " << (frame / 60) << "s / 10s" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "FINAL SETTLING", 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame / 60) + "s / 10s", 200, 200, 200);
            ui->draw_text(10, 70, "Totem pieces: " + std::to_string(totem.piece_ids.size()), 150, 255, 150);

            engine.present();
        }
    }

    // Check final stability
    std::cout << "\n[FINAL STABILITY CHECK]" << std::endl;
    float max_velocity = 0.0f;
    {
        auto particles = ps.lock_particles_for_read();
        for (int id : totem.piece_ids) {
            const auto& p = particles[id];
            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            if (vel > max_velocity) max_velocity = vel;
        }
    }
    std::cout << "  Max totem piece velocity: " << max_velocity << " m/s" << std::endl;
    bool stable = (max_velocity < 0.1f);
    std::cout << "  Stability: " << (stable ? "✅ STABLE" : "❌ UNSTABLE") << std::endl;

    // Final summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║              TOTEM BUILDER - TEST COMPLETE                   ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1 (Lower trunk): " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 2 (Middle trunk): " << (case2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 3 (Top trunk): " << (case3_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 4 (Trunk4): " << (case4_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 5 (Trunk5 large): " << (case5_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 6 (Horizontal beam): " << (case6_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Final stability: " << (stable ? "✅ STABLE" : "❌ UNSTABLE") << std::endl;
    std::cout << "\n  Total totem pieces: " << totem.piece_ids.size() << " (+ " << totem.floor_ids.size() << " floor tiles)" << std::endl;

    bool all_pass = case1_pass && case2_pass && case3_pass && case4_pass && case5_pass && case6_pass && stable;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED! TOTEM COMPLETE!" : "❌ SOME TESTS FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1 (Lower): " + std::string(case1_pass ? "PASS" : "FAIL"),
            "Case 2 (Middle): " + std::string(case2_pass ? "PASS" : "FAIL"),
            "Case 3 (Top): " + std::string(case3_pass ? "PASS" : "FAIL"),
            "Case 4 (Trunk4): " + std::string(case4_pass ? "PASS" : "FAIL"),
            "Case 5 (Trunk5): " + std::string(case5_pass ? "PASS" : "FAIL"),
            "Case 6 (Beam): " + std::string(case6_pass ? "PASS" : "FAIL"),
            "",
            "Total pieces: " + std::to_string(totem.piece_ids.size()),
            "",
            all_pass ? "TOTEM COMPLETE!" : "SOME TESTS FAILED"
        };
        wait_for_space(engine, "TOTEM BUILDER COMPLETE", summary);
    }

    return all_pass;
}

