#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <GLFW/glfw3.h>
#include <string>

// ADJUDICATED 2026-08-21 (test-adjudication pass): THE ORIGINAL BANDS ARE
// RESTORED. `max_velocity < 0.01f` and `wiggly_frames < 10`.
//
// Both had been LOOSENED IN PLACE and the comments recorded it — "Relaxed from
// 0.01" and "Relaxed from 10". Loosening an assertion until it passes is the
// one move docs/testing_guidelines.md forbids outright: the rule is to say
// which was wrong, the code or the expectation. TEST_AUDIT already books what
// the loosening was hiding — G-44 unmasked a sustained 0.0294 m/s oscillation
// in depth-3/4 oaks that the speed-only sleep entry had been absorbing, and
// "the audited green was a mask" — and 0.0294 sits outside the original band
// and inside the relaxed one.
//
// MEASURED, and this is the sharpest fact about the relaxation: it did not
// even buy the green it was widened for. Under the relaxed bands, depth 3 and
// depth 4 both read 0.0294 m/s and 30 wiggly frames, so both failed anyway
// (0.0294 > 0.025, 30 > 15) and the file already exited 1. The wider band
// bought nothing and cost the honest number.
//
// Before this change: 1 of 3 trees passing, exit 1. After: 1 of 3 trees
// passing, exit 1, with depth 3 and 4 now failing against the bounds the law
// actually asks for. Booked known_open against the G-44 gluon-tree oscillation
// RCA already on the board.
//
// The laws: INV-34 (rest-is-reached, ratified 2026-08-21 — an untouched scene
// settles and stays settled, and a sustained oscillation is an energy source
// however small its amplitude), INV-24 (a settled scene performs zero
// corrective work), INV-3 (whatever sustains it is energy the ledger does not
// see), and G-44 (low speed is not a fixed point; quietness must be priced in
// extremity speed sqrt(v^2 + (omega*r)^2), so a slow-COM fast-spin trunk is not
// quiet at all — the bound here is still COM speed, which is the remaining
// owner question).
//
// ============================================================================
// TREE WIGGLY TEST - Multiple Tree Sizes
// ============================================================================
// Purpose: Test if physics trees reach stable rest or keep wiggling
// LAWS: INV-34 (rest-is-reached: an untouched scene settles and stays
// settled), INV-24 (zero corrective work at steady state), INV-3 (nothing
// sustains the motion without energy arriving), G-44 (the honest quietness
// predicate).
// Tests multiple tree configurations: small, medium, large, extra-large
//
// Run: INTERACTIVE=1 ./logosphere-tests --test test_tree_wiggly
// ============================================================================

struct TreeTestCase {
    std::string name;
    float height;
    float trunk_diameter;
    int branch_depth;
    float branch_angle;       // Degrees from vertical (35=upright, 70=horizontal)
    float expected_mass_min;  // Sanity check
    float expected_mass_max;
};

struct TreeTestResult {
    std::string name;
    float max_velocity;
    int wiggly_frames;
    bool passed;
};

// Test a single tree configuration and return result
TreeTestResult test_single_tree(Engine& engine, PhysicsTreeGenerator& generator,
                                const TreeTestCase& test_case, bool is_interactive) {
    TreeTestResult result;
    result.name = test_case.name;

    std::cout << "\n------------------------------------------------------" << std::endl;
    std::cout << "  Testing: " << test_case.name << std::endl;
    std::cout << "  Height: " << test_case.height << "m, Trunk: " << test_case.trunk_diameter << "m" << std::endl;
    std::cout << "------------------------------------------------------" << std::endl;

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Clear previous tree (gluons and all particles)
    physics.clear_gluons();
    ps.clear_particles();

    // Re-add light source
    Particle light = {};
    light.x = 0.0f; light.y = -20.0f; light.z = 20.0f;
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    light.is_light_source = true;
    light.emission_strength = 100000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
    engine.add_particle(light);

    // Add floor tile - trees need a floor tile to gluon to
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.1f;
    floor.shape = ParticleShape::BOX;
    floor.width = 3.0f; floor.height = 3.0f; floor.thickness = 0.2f;
    floor.r = 0.3f; floor.g = 0.5f; floor.b = 0.3f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor.is_at_rest = true;  // Floor starts at rest
    engine.add_particle(floor);

    // Create tree spec
    TreeSpec spec;
    spec.height = test_case.height;
    spec.trunk_diameter = test_case.trunk_diameter;
    spec.branch_depth = test_case.branch_depth;
    spec.branches_per_split = 2;
    spec.length_ratio = 0.7f;
    spec.branch_angle = test_case.branch_angle;
    spec.random_seed = 12345;

    // Standard oak colors
    spec.trunk_r = 0.35f; spec.trunk_g = 0.25f; spec.trunk_b = 0.15f;
    spec.leaf_r = 0.15f; spec.leaf_g = 0.45f; spec.leaf_b = 0.1f;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "  Created: " << tree.total_segments << " segments, "
              << tree.leaf_ids.size() << " leaves" << std::endl;

    // Update camera for this tree size
    auto& camera = engine.get_camera_system();
    float cam_dist = test_case.height * 2.0f;
    camera.set_position(-cam_dist, -cam_dist, test_case.height * 0.8f);
    camera.look_at(0.0f, 0.0f, test_case.height * 0.5f);

    const double dt = 1.0 / 60.0;

    // PHASE 1: Settle for 3 seconds
    std::cout << "  Settling (3s)..." << std::flush;
    for (int frame = 0; frame < 180; frame++) {
        engine.update(dt);

        // Render every frame in interactive mode (vsync provides timing)
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "SETTLING: " + test_case.name, 255, 255, 0);
            ui->draw_text(10, 40, "Time: " + std::to_string(frame / 60) + "s / 3s", 200, 200, 200);
            engine.present();
        }
    }
    std::cout << " done" << std::endl;

    // PHASE 2: Measure wiggliness for 5 seconds
    std::cout << "  Measuring (5s)..." << std::flush;

    float max_velocity = 0.0f;
    float max_omega = 0.0f;
    int wiggly_frames = 0;
    const float WIGGLY_THRESHOLD = 0.005f;

    for (int frame = 0; frame < 300; frame++) {
        engine.update(dt);

        if (frame % 10 == 0) {
            auto particles = ps.lock_particles_for_read();

            float frame_max_v = 0.0f;
            float frame_max_omega = 0.0f;

            for (int id : tree.branch_ids) {
                if (id >= 0 && id < static_cast<int>(particles.size())) {
                    const Particle& p = particles[id];
                    float v = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    frame_max_v = std::max(frame_max_v, v);
                    frame_max_omega = std::max(frame_max_omega, std::abs(p.omega_z));
                }
            }

            // Also check leaves
            for (int id : tree.leaf_ids) {
                if (id >= 0 && id < static_cast<int>(particles.size())) {
                    const Particle& p = particles[id];
                    float v = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    frame_max_v = std::max(frame_max_v, v);
                }
            }

            max_velocity = std::max(max_velocity, frame_max_v);
            max_omega = std::max(max_omega, frame_max_omega);

            if (frame_max_v > WIGGLY_THRESHOLD) {
                wiggly_frames++;
            }
        }

        // Render every frame in interactive mode (vsync provides timing)
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "MEASURING: " + test_case.name, 255, 200, 100);
            float t = 3.0f + (frame / 60.0f);
            ui->draw_text(10, 40, "Time: " + std::to_string(t).substr(0, 4) + "s", 200, 200, 200);
            ui->draw_text(10, 70, "Max v: " + std::to_string(max_velocity).substr(0, 8) + " m/s", 200, 200, 200);
            engine.present();
        }
    }
    std::cout << " done" << std::endl;

    // Results
    //
    // THE ORIGINAL BANDS, RESTORED 2026-08-21. They had been loosened in place
    // (0.01 -> 0.025 m/s and 10 -> 15 frames) with the relaxation recorded in
    // the comments, which is the one move docs/testing_guidelines.md forbids
    // outright: say which was wrong, the code or the expectation, never move
    // the line. What the loosening hid is already booked — G-44 unmasked a
    // sustained 0.0294 m/s oscillation in depth-3/4 oaks that the speed-only
    // sleep entry had been absorbing — and 0.0294 sits outside the original
    // band and inside the relaxed one. The relaxed band did not even buy the
    // green it was widened for: 0.0294 > 0.025 and 30 > 15, so both depths
    // failed anyway. INV-34 is the law (an untouched scene settles and stays
    // settled), INV-24 the corrective half, INV-3 the question of where the
    // energy comes from.
    bool velocity_ok = max_velocity < 0.01f;
    bool wiggly_ok = wiggly_frames < 10;
    bool pass = velocity_ok && wiggly_ok;

    std::cout << "  INV-34/INV-24/G-44 max velocity: " << std::fixed << std::setprecision(4)
              << max_velocity << " m/s  (bound 0.0100)"
              << (velocity_ok ? " OK" : " FAIL") << std::endl;
    std::cout << "  INV-34/INV-24/G-44 wiggly frames: " << wiggly_frames << "/30  (bound 10)"
              << (wiggly_ok ? " OK" : " FAIL") << std::endl;
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << std::endl;

    result.max_velocity = max_velocity;
    result.wiggly_frames = wiggly_frames;
    result.passed = pass;

    return result;
}

bool test_tree_wiggly() {
    std::cout << "\n======================================================" << std::endl;
    std::cout << "  TREE WIGGLY TEST - Multiple Sizes                    " << std::endl;
    std::cout << "======================================================" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

    // Define test cases: escalating branch depth to find stability threshold
    // Logomancers uses TreeSpec::oak() which has branch_depth=5
    std::vector<TreeTestCase> test_cases = {
        // name, height, trunk_diameter, branch_depth, branch_angle, mass_min, mass_max
        {"Oak depth=3 (8 branches)",   12.0f, 0.4f, 3, 70.0f,  100.0f, 5000.0f},
        {"Oak depth=4 (16 branches)",  12.0f, 0.4f, 4, 70.0f,  100.0f, 5000.0f},
        {"Oak depth=5 (32 branches)",  12.0f, 0.4f, 5, 70.0f,  100.0f, 5000.0f},
    };

    // Create ONE engine for all tests
    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Tree Wiggly Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "  Engine init failed!" << std::endl;
        return false;
    }

    // Gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    std::vector<TreeTestResult> results;
    int passed = 0;
    int total = test_cases.size();

    for (size_t i = 0; i < test_cases.size(); i++) {
        const auto& test_case = test_cases[i];

        TreeTestResult result = test_single_tree(engine, generator, test_case, is_interactive);
        results.push_back(result);

        if (result.passed) {
            passed++;
        }

        // Wait for space in interactive mode between tests
        if (is_interactive && i < test_cases.size() - 1) {
            std::cout << "\n  Press SPACE for next test..." << std::endl;
            auto& input = engine.get_input_system();
            bool was_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            while (true) {
                engine.get_platform()->poll_events();
                engine.render();
                auto* ui = engine.get_ui_system();
                ui->draw_text(10, 10, "Press SPACE for next test", 150, 150, 255);
                engine.present();

                const auto& state = input.get_input_state();
                if (!was_pressed && state.keys[GLFW_KEY_SPACE]) break;
                was_pressed = state.keys[GLFW_KEY_SPACE];

                // Check for ESC to exit early
                if (state.keys[GLFW_KEY_ESCAPE]) {
                    std::cout << "\n  ESC pressed - exiting early" << std::endl;
                    goto summary;
                }
            }
        }
    }

summary:
    // Final summary
    std::cout << "\n======================================================" << std::endl;
    std::cout << "  TREE WIGGLY TEST RESULTS                             " << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "\n  | Tree               | Max Vel  | Wiggly | Result |" << std::endl;
    std::cout << "  |--------------------|----------|--------|--------|" << std::endl;

    for (const auto& r : results) {
        std::cout << "  | " << std::left << std::setw(18) << r.name.substr(0, 18)
                  << " | " << std::fixed << std::setprecision(4) << std::setw(8) << r.max_velocity
                  << " | " << std::setw(6) << r.wiggly_frames << " | "
                  << (r.passed ? "PASS  " : "FAIL  ") << " |" << std::endl;
    }

    std::cout << "\n  Total: " << passed << "/" << total << " passed" << std::endl;
    std::cout << "======================================================" << std::endl;

    return (passed == total);
}
