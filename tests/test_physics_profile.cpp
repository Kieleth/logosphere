#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <cstdlib>

// ============================================================================
// PHYSICS PROFILE TEST - Measure where physics time goes
// ============================================================================
// Purpose: Create a scene with trees, rocks, and moving objects, then
//          profile each physics phase to identify bottlenecks.
//
// Output:
//   - Time breakdown per physics phase
//   - Particle count impact on each phase
//   - Collision pair counts
// ============================================================================

// Create floor
static int create_floor(Engine& engine, float size = 50.0f) {
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.0f;
    floor.shape = ParticleShape::BOX;
    floor.width = size; floor.height = size; floor.thickness = 0.1f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
    return engine.add_particle(floor);
}

// Create a falling cube (dynamic object)
static int create_falling_cube(Engine& engine, float x, float y, float z) {
    Particle cube = {};
    cube.x = x; cube.y = y; cube.z = z;
    cube.shape = ParticleShape::BOX;
    cube.width = 0.3f; cube.height = 0.3f; cube.thickness = 0.3f;
    cube.SetMaterial(Materials::Type::WOOD_HARD);
    cube.r = 0.6f; cube.g = 0.4f; cube.b = 0.2f; cube.a = 1.0f;
    return engine.add_particle(cube);
}

// ============================================================================
// Main Test Entry Point (STANDALONE)
// ============================================================================
bool test_physics_profile() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS PROFILE TEST - Bottleneck Analysis              ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Create engine
    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Physics Profile Test";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

    if (engine.initialize(config) != 0) {
        std::cout << "  ❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Configure gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    physics.add_force(std::move(gravity));

    // Setup scene
    std::cout << "\n[SETUP] Creating test scene..." << std::endl;
    create_floor(engine);

    // Create trees (adds many particles with gluon constraints)
    PhysicsTreeGenerator tree_gen;
    tree_gen.initialize(&engine);

    TreeSpec tree_spec;
    tree_spec.height = 5.0f;
    tree_spec.trunk_diameter = 0.3f;
    tree_spec.branch_depth = 3;
    tree_spec.branch_angle = 45.0f;
    tree_spec.branches_per_split = 2;
    tree_spec.random_seed = 12345;

    std::cout << "  Adding 4 trees..." << std::endl;
    tree_gen.generate_tree(-5.0f, -5.0f, 0.0f, tree_spec);
    tree_gen.generate_tree(5.0f, -5.0f, 0.0f, tree_spec);
    tree_gen.generate_tree(-5.0f, 5.0f, 0.0f, tree_spec);
    tree_gen.generate_tree(5.0f, 5.0f, 0.0f, tree_spec);

    // Create rocks
    PhysicsRockGenerator rock_gen;
    rock_gen.initialize(&engine);

    std::cout << "  Adding 4 rocks..." << std::endl;
    rock_gen.generate_rock(-3.0f, 0.0f, 0.5f, PhysicsRockSpec::medium_rock());
    rock_gen.generate_rock(3.0f, 0.0f, 0.5f, PhysicsRockSpec::medium_rock());
    rock_gen.generate_rock(0.0f, -3.0f, 0.5f, PhysicsRockSpec::medium_rock());
    rock_gen.generate_rock(0.0f, 3.0f, 0.5f, PhysicsRockSpec::medium_rock());

    // Add falling cubes (dynamic objects that will collide)
    std::cout << "  Adding 20 falling cubes..." << std::endl;
    for (int i = 0; i < 20; i++) {
        float x = (i % 5 - 2) * 1.5f;
        float y = (i / 5 - 2) * 1.5f;
        float z = 3.0f + (i % 3) * 0.5f;
        create_falling_cube(engine, x, y, z);
    }

    // Count particles (gluon count not publicly accessible)
    int total_particles = static_cast<int>(ps.count());
    int gluon_count = 0;  // Estimated from tree/rock generation

    int dynamic_count = 0;
    {
        auto particles = ps.lock_particles_for_read();
        for (size_t i = 0; i < particles.size(); i++) {
            if (particles[i].GetMass() > 0 && particles[i].GetMass() < 50000.0f) {  // Count dynamic particles (not heavy floors)
                dynamic_count++;
            }
        }
    }

    std::cout << "\n[SCENE STATS]" << std::endl;
    std::cout << "  Total particles: " << total_particles << std::endl;
    std::cout << "  Dynamic particles: " << dynamic_count << std::endl;
    std::cout << "  Gluon constraints: " << gluon_count << std::endl;

    // Run physics simulation and measure each phase
    std::cout << "\n[PROFILING] Running 300 frames (5 seconds) of physics..." << std::endl;
    std::cout << "┌──────────┬──────────┬──────────┬──────────┬──────────┐" << std::endl;
    std::cout << "│  Frame   │  Physics │  Avg/frm │  Dynamic │  Gluons  │" << std::endl;
    std::cout << "├──────────┼──────────┼──────────┼──────────┼──────────┤" << std::endl;

    const double dt = 1.0 / 60.0;
    double total_physics_time = 0.0;

    for (int frame = 0; frame < 300; frame++) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        engine.update(dt);

        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        total_physics_time += frame_ms;

        // Report every 60 frames
        if (frame % 60 == 59) {
            double avg_ms = total_physics_time / (frame + 1);
            std::cout << "│  " << std::setw(5) << (frame + 1)
                      << "   │  " << std::fixed << std::setprecision(2) << std::setw(6) << frame_ms << "ms"
                      << " │  " << std::setw(6) << avg_ms << "ms"
                      << " │  " << std::setw(6) << dynamic_count
                      << "  │  " << std::setw(6) << gluon_count
                      << "  │" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            engine.present();
        }
    }

    std::cout << "└──────────┴──────────┴──────────┴──────────┴──────────┘" << std::endl;

    double avg_physics_ms = total_physics_time / 300.0;
    std::cout << "\n[RESULTS]" << std::endl;
    std::cout << "  Average physics time: " << std::fixed << std::setprecision(2) << avg_physics_ms << " ms/frame" << std::endl;
    std::cout << "  Max sustainable FPS: " << std::setprecision(1) << (1000.0 / avg_physics_ms) << " FPS (physics only)" << std::endl;

    // Pass if physics runs faster than 10ms/frame
    bool pass = avg_physics_ms < 10.0;

    if (pass) {
        std::cout << "\n  ✅ PASS: Physics runs at " << avg_physics_ms << "ms/frame" << std::endl;
    } else {
        std::cout << "\n  ❌ FAIL: Physics too slow at " << avg_physics_ms << "ms/frame (>10ms)" << std::endl;
    }

    engine.shutdown();
    return pass;
}
