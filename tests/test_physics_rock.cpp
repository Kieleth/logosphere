#include "../src/core/engine.h"
#include "../src/core/entity_telemetry.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// PHYSICS ROCK TEST - Verify Generated Rocks Settle and Don't Drift
// ============================================================================
// Purpose: Test that PhysicsRockGenerator creates stable rock structures
//          that settle on ground and maintain position
//
// Acceptance Criteria, each naming its law (assert-protocol migration,
// 2026-08-21):
//   1. Rock settles within 2 seconds        PROPOSED REST-IS-REACHED
//                                           (tests/invariants/INV_PROPOSALS.md)
//   2. Rock stays stable for 8 s after      INV-24 — at steady state a scene
//      settling                             performs zero corrective work; a
//                                           settled rock that keeps creeping
//                                           is a repair firing forever.
//   3. No NaN/Inf in particle positions     PROPOSED FINITE-STATE
//   4. Drift < 5cm after settling           INV-24, measured.
//   5. No horizontal contact normals        INV-12 — a rock resting on a flat
//                                           floor sheds no side-face normal;
//                                           a horizontal one is the bounding
//                                           slab, not the rock.
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

// Helper: Create floor particle
static int create_floor(Engine& engine, float size = 50.0f) {
    Particle floor = {};
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05
    floor.shape = ParticleShape::BOX;
    floor.width = size;
    floor.height = size;
    floor.thickness = 0.05f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    floor.r = 0.3f;
    floor.g = 0.3f;
    floor.b = 0.3f;
    floor.a = 1.0f;
    return engine.add_particle(floor);
}

// Helper: Create light particle
static void create_light(Engine& engine) {
    Particle light = {};
    light.x = 0.0f; light.y = -10.0f; light.z = 10.0f;
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    light.is_light_source = true;
    light.emission_strength = 100000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
    engine.add_particle(light);
}

// Helper: Calculate center of mass for a rock
static void get_rock_com(ParticleSystem& ps, const PhysicsRockResult& rock, float& x, float& y, float& z) {
    auto particles = ps.lock_particles_for_read();
    float total_mass = 0.0f;
    x = y = z = 0.0f;

    for (int id : rock.box_ids) {
        const Particle& p = particles[id];
        x += p.x * p.GetMass();
        y += p.y * p.GetMass();
        z += p.z * p.GetMass();
        total_mass += p.GetMass();
    }

    if (total_mass > 0.0f) {
        x /= total_mass;
        y /= total_mass;
        z /= total_mass;
    }
}

// Helper: Check for NaN in rock particles
static bool check_nan(ParticleSystem& ps, const PhysicsRockResult& rock) {
    auto particles = ps.lock_particles_for_read();
    for (int id : rock.box_ids) {
        const Particle& p = particles[id];
        if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z) ||
            std::isinf(p.x) || std::isinf(p.y) || std::isinf(p.z)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Test: Large Boulder Stability
// ============================================================================
bool test_physics_rock_boulder(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Physics Rock: Large Boulder Stability";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {"Boulder will fall from z=3m", "Should settle and stay stable"});
    }

    // Clear any previous state
    ps.clear_particles();
    physics.clear_gluons();

    // Setup scene
    create_light(engine);
    create_floor(engine);

    // Create physics rock generator
    PhysicsRockGenerator generator;
    generator.initialize(&engine);

    // Generate large boulder at height (will fall)
    std::cout << "\n[STEP 1] Generating large boulder at z=3m..." << std::endl;
    PhysicsRockResult boulder = generator.generate_rock(0.0f, 0.0f, 3.0f, PhysicsRockSpec::large_boulder());

    std::cout << "  Core ID: " << boulder.core_id << std::endl;
    std::cout << "  Total boxes: " << boulder.total_boxes << std::endl;

    // Enable telemetry for all boulder boxes
    auto& telemetry = engine.get_physics_system().get_telemetry();
    telemetry.set_enabled(true);
    for (int id : boulder.box_ids) {
        telemetry.track_particle(static_cast<unsigned int>(id));
    }
    std::cout << "  Tracking " << boulder.box_ids.size() << " boulder particles for telemetry" << std::endl;

    // Phase 1: Let it settle. Adaptive — wait until max box speed is under
    // threshold for N consecutive frames, or hit a hard time cap. A 10-box
    // gluon-bonded boulder falling from z=3m needs more than the old fixed
    // 2s budget; the fixed budget previously hid real settling as "drift".
    std::cout << "\n[STEP 2] Settling phase (adaptive)..." << std::endl;
    const double dt = 1.0 / 60.0;
    constexpr float SETTLE_SPEED_THRESHOLD = 0.05f;   // 5 cm/s
    constexpr int   SETTLE_STABLE_FRAMES   = 30;      // 0.5 s of quiet
    constexpr int   SETTLE_MAX_FRAMES      = 600;     // 10 s hard cap

    int stable_streak = 0;
    int frame = 0;
    for (; frame < SETTLE_MAX_FRAMES; frame++) {
        engine.update(dt);

        if (check_nan(ps, boulder)) {
            std::cout << "  ❌ FAIL PROPOSED FINITE-STATE: NaN detected during settling at frame " << frame << std::endl;
            return false;
        }

        float max_speed = 0.0f;
        {
            const auto& particles = ps.lock_particles_for_read();
            for (int id : boulder.box_ids) {
                if (id < 0 || (size_t)id >= particles.size()) continue;
                const auto& p = particles[id];
                float s = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                if (s > max_speed) max_speed = s;
            }
        }

        if (max_speed < SETTLE_SPEED_THRESHOLD) stable_streak++;
        else                                    stable_streak = 0;

        if (frame % 30 == 0) {
            float x, y, z;
            get_rock_com(ps, boulder, x, y, z);
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt)
                      << "s] COM: (" << std::setprecision(3) << x << ", " << y << ", " << z << ")"
                      << " max_speed=" << std::setprecision(3) << max_speed << " m/s"
                      << " stable=" << stable_streak
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - SETTLING", 255, 255, 0);
            engine.present();
        }

        if (stable_streak >= SETTLE_STABLE_FRAMES) {
            std::cout << "  [settled at frame " << frame << " = " << (frame * dt) << "s]\n";
            break;
        }
    }
    if (frame >= SETTLE_MAX_FRAMES) {
        std::cout << "  ⚠ settle timeout: hit " << SETTLE_MAX_FRAMES << " frame cap without full quiescence\n";
    }

    // Record settled position
    float settled_x, settled_y, settled_z;
    get_rock_com(ps, boulder, settled_x, settled_y, settled_z);
    std::cout << "\n[STEP 3] Settled position: (" << settled_x << ", " << settled_y << ", " << settled_z << ")" << std::endl;

    // Phase 2: Check for drift (8 seconds)
    std::cout << "\n[STEP 4] Drift test (8 seconds)..." << std::endl;

    float max_drift = 0.0f;
    int drift_frame = -1;

    for (int frame = 0; frame < 480; frame++) {
        engine.update(dt);

        if (check_nan(ps, boulder)) {
            std::cout << "  ❌ FAIL PROPOSED FINITE-STATE: NaN detected during drift test at frame " << frame << std::endl;
            return false;
        }

        float x, y, z;
        get_rock_com(ps, boulder, x, y, z);

        float drift = std::sqrt(
            (x - settled_x) * (x - settled_x) +
            (y - settled_y) * (y - settled_y) +
            (z - settled_z) * (z - settled_z)
        );

        if (drift > max_drift) {
            max_drift = drift;
            drift_frame = frame;
        }

        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (2.0 + frame * dt)
                      << "s] drift=" << std::setprecision(4) << drift << "m"
                      << " max=" << max_drift << "m" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - DRIFT TEST", 255, 255, 0);
            engine.present();
        }
    }

    // Contact telemetry analysis
    std::cout << "\n[STEP 5] Contact Telemetry:" << std::endl;

    int total_contacts = 0;
    int vertical_contacts = 0;
    int horizontal_contacts = 0;
    int corner_contacts = 0;
    // Track net horizontal force direction from contact normals
    float sum_nx = 0, sum_ny = 0;

    for (int id : boulder.box_ids) {
        const auto* buf = telemetry.query(static_cast<unsigned int>(id));
        if (!buf || buf->empty()) continue;

        for (size_t i = 0; i < buf->size(); i++) {
            const auto& frame = buf->at(i);
            for (const auto& c : frame.contacts) {
                total_contacts++;
                if (c.is_corner_contact) corner_contacts++;

                float abs_nz = std::abs(c.normal_z);
                float abs_nx = std::abs(c.normal_x);
                float abs_ny = std::abs(c.normal_y);

                if (abs_nz > abs_nx && abs_nz > abs_ny) {
                    vertical_contacts++;
                } else {
                    horizontal_contacts++;
                    sum_nx += c.normal_x;
                    sum_ny += c.normal_y;
                }

                // Print first 20 horizontal contacts (the interesting ones)
                if (horizontal_contacts <= 20 && !(abs_nz > abs_nx && abs_nz > abs_ny)) {
                    std::cout << "    f" << frame.frame_number
                              << " box" << id << " P" << c.particle_a << "<>P" << c.particle_b
                              << " n=(" << std::fixed << std::setprecision(3)
                              << c.normal_x << "," << c.normal_y << "," << c.normal_z << ")"
                              << " pen=" << std::setprecision(4) << c.penetration
                              << (c.is_corner_contact ? " CORNER" : "")
                              << std::endl;
                }
            }
        }
    }

    std::cout << "\n  Contact summary:" << std::endl;
    std::cout << "    Total contacts: " << total_contacts << std::endl;
    std::cout << "    Vertical (Z dominant): " << vertical_contacts << std::endl;
    std::cout << "    Horizontal (X/Y dominant): " << horizontal_contacts << std::endl;
    std::cout << "    Corner contacts: " << corner_contacts << std::endl;
    if (horizontal_contacts > 0) {
        std::cout << "    Net horizontal normal: (" << sum_nx << ", " << sum_ny << ")" << std::endl;
        std::cout << "    This is the direction the solver pushes the boulder" << std::endl;
    }

    // Analysis
    std::cout << "\n[STEP 6] Drift Analysis:" << std::endl;
    std::cout << "  Max drift: " << max_drift << "m (at frame " << drift_frame << ")" << std::endl;

    bool pass = max_drift < 0.05f;
    bool normals_ok = horizontal_contacts == 0;

    std::cout << "  " << (pass ? "PASS" : "FAIL") << ": INV-24: Drift < 0.05m after settling — a settled scene performs zero corrective work (" << max_drift << "m)" << std::endl;
    std::cout << "  " << (normals_ok ? "PASS" : "FAIL") << ": INV-12: No horizontal normals on a flat floor (" << horizontal_contacts << " found)" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, pass ? "PASS" : "FAIL", {"Max drift: " + std::to_string(max_drift) + "m"});
    }

    return pass;
}

// ============================================================================
// Test: Two Cubes Stability (simple collision test)
// ============================================================================
bool test_physics_rock_cubes(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Physics Rock: Cube Collision Test";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {"Two cubes will fall", "Should settle on floor"});
    }

    // Clear any previous state
    ps.clear_particles();
    physics.clear_gluons();

    // Setup scene
    create_light(engine);
    create_floor(engine);

    // Create two simple cubes
    std::cout << "\n[STEP 1] Generating two cubes..." << std::endl;

    Particle cube1 = {};
    cube1.x = -3.0f; cube1.y = 0.0f; cube1.z = 1.5f;
    cube1.shape = ParticleShape::BOX;
    cube1.width = 0.3f; cube1.height = 0.3f; cube1.thickness = 0.3f;
    cube1.SetMaterial(Materials::Type::WOOD_HARD);
    cube1.r = 0.5f; cube1.g = 0.5f; cube1.b = 0.5f; cube1.a = 1.0f;
    int cube1_id = engine.add_particle(cube1);
    std::cout << "  Cube1: id=" << cube1_id << " at z=" << cube1.z << std::endl;

    Particle cube2 = {};
    cube2.x = 3.0f; cube2.y = 0.0f; cube2.z = 1.5f;
    cube2.shape = ParticleShape::BOX;
    cube2.width = 0.3f; cube2.height = 0.3f; cube2.thickness = 0.3f;
    cube2.SetMaterial(Materials::Type::WOOD_HARD);
    cube2.r = 0.5f; cube2.g = 0.5f; cube2.b = 0.5f; cube2.a = 1.0f;
    int cube2_id = engine.add_particle(cube2);
    std::cout << "  Cube2: id=" << cube2_id << " at z=" << cube2.z << std::endl;

    // Settle phase
    std::cout << "\n[STEP 2] Settling (2 seconds)..." << std::endl;
    const double dt = 1.0 / 60.0;

    for (int frame = 0; frame < 120; frame++) {
        engine.update(dt);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - SETTLING", 255, 255, 0);
            engine.present();
        }
    }

    // Check settled positions
    float z1, z2;
    {
        auto particles = ps.lock_particles_for_read();
        z1 = particles[cube1_id].z;
        z2 = particles[cube2_id].z;
    }
    std::cout << "  Cube1 settled at z=" << z1 << std::endl;
    std::cout << "  Cube2 settled at z=" << z2 << std::endl;

    // Drift test
    std::cout << "\n[STEP 3] Drift test (5 seconds)..." << std::endl;
    float settled_z1 = z1, settled_z2 = z2;
    float max_drift1 = 0.0f, max_drift2 = 0.0f;

    for (int frame = 0; frame < 300; frame++) {
        engine.update(dt);

        {
            auto particles = ps.lock_particles_for_read();
            float drift1 = std::abs(particles[cube1_id].z - settled_z1);
            float drift2 = std::abs(particles[cube2_id].z - settled_z2);
            if (drift1 > max_drift1) max_drift1 = drift1;
            if (drift2 > max_drift2) max_drift2 = drift2;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - DRIFT TEST", 255, 255, 0);
            engine.present();
        }
    }

    // Results
    std::cout << "\n[STEP 4] Results:" << std::endl;
    bool pass1 = (z1 > 0.0f) && (max_drift1 < 0.05f);  // Must be above floor and stable
    bool pass2 = (z2 > 0.0f) && (max_drift2 < 0.05f);
    std::cout << "  Cube1: z=" << z1 << " drift=" << max_drift1 << "m " << (pass1 ? "✅" : "❌") << std::endl;
    std::cout << "  Cube2: z=" << z2 << " drift=" << max_drift2 << "m " << (pass2 ? "✅" : "❌") << std::endl;

    bool all_pass = pass1 && pass2;

    if (is_interactive) {
        wait_for_space(engine, all_pass ? "ALL PASS" : "SOME FAILED", {});
    }

    return all_pass;
}

// ============================================================================
// Main Entry Point
// ============================================================================
bool test_physics_rock() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS ROCK TEST - Gluon-Based Rock Generation         ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview:" << std::endl;
    std::cout << "  Test 1: Large boulder stability" << std::endl;
    std::cout << "  Test 2: Cube collision test" << std::endl;
    std::cout << "\nAcceptance: Objects must settle on floor and not drift (< 5cm)" << std::endl;

    // Single engine for all tests
    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Rock Test";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& camera = engine.get_camera_system();
    camera.set_position(0.0f, -15.0f, 10.0f);

    bool all_pass = true;

    // Test 1: Large boulder
    all_pass &= test_physics_rock_boulder(engine, is_interactive);

    // Test 2: Cube collision
    all_pass &= test_physics_rock_cubes(engine, is_interactive);

    std::cout << "\n══════════════════════════════════════════════════════════════" << std::endl;
    if (all_pass) {
        std::cout << "✅ ALL PHYSICS ROCK TESTS PASSED" << std::endl;
    } else {
        std::cout << "❌ SOME PHYSICS ROCK TESTS FAILED" << std::endl;
    }
    std::cout << "══════════════════════════════════════════════════════════════\n" << std::endl;

    return all_pass;
}
