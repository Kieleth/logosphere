// ============================================================================
// TURTLE EXPERIMENT: Single particle resting on Turtle
// ============================================================================
// Minimal test: ONE particle, just above Turtle (z=0), observe frame-by-frame
// Goal: Understand exactly what happens when a particle rests on the Turtle
//
// Setup:
//   - Turtle at z=0 (infinite mass world boundary)
//   - Single particle (1kg, 0.5m cube) at z=0.25 (bottom touching Turtle)
//   - Gravity enabled
//
// Expected behavior:
//   - Gravity pulls down: vz -= 9.8 * dt each frame
//   - Turtle contact pushes up: vz += impulse / mass
//   - Should reach equilibrium: vz ≈ 0, z ≈ 0.25
//
// Logs every frame for first 30 frames to trace physics exactly.
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_solver.h"  // For TURTLE_Z constant
#include <iostream>
#include <iomanip>

using PhysicsV4::TURTLE_Z;

bool test_turtle_single_particle() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  TURTLE EXPERIMENT: Single Particle Resting on Turtle        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Engine setup (headless)
    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Turtle Experiment";

    if (engine.initialize(config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Enable gravity
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Print Turtle info
    std::cout << "[SETUP] TURTLE_Z = " << TURTLE_Z << "m (world floor)\n";
    std::cout << "[SETUP] Gravity = -9.8 m/s^2\n\n";

    // Create single particle resting on Turtle
    // Particle is 0.5m cube, so half-thickness = 0.25m
    // Place center at z=0.25 so bottom is at z=0 (exactly on Turtle)
    Particle p = {};
    p.x = 0.0f;
    p.y = 0.0f;
    p.z = 0.25f;  // Center at 0.25m, bottom at z=0 (on Turtle)
    p.shape = ParticleShape::BOX;
    p.width = 0.5f;
    p.height = 0.5f;
    p.thickness = 0.5f;
    p.SetMaterial(Materials::Type::FLESH);  // 1kg - light, dynamic
    p.r = 1.0f; p.g = 0.0f; p.b = 0.0f;  // Red
    p.friction = 0.5f;
    p.material_density = 1000.0f;

    int pid = engine.add_particle(p);

    // V4.3: No dummy particle needed - the early return bug is fixed
    // (Changed "if (count < 2) return;" to "if (count < 1) return;")

    std::cout << "[SETUP] Particle created:\n";
    std::cout << "  ID: " << pid << "\n";
    std::cout << "  Position: (0, 0, " << p.z << ")m\n";
    std::cout << "  Size: " << p.width << " x " << p.height << " x " << p.thickness << "m\n";
    std::cout << "  Mass: " << p.GetMass() << "kg\n";
    std::cout << "  Bottom z: " << (p.z - p.thickness/2) << "m (should touch Turtle at z=0)\n\n";

    // Run physics frame by frame with detailed logging
    const float dt = 1.0f / 60.0f;  // 60Hz
    const int total_frames = 60;  // 1 second

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FRAME-BY-FRAME PHYSICS LOG                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Frame |    z (m)    |   vz (m/s)  |  bottom_z   | penetration\n";
    std::cout << "------+-------------+-------------+-------------+-------------\n";

    float prev_z = 0.25f;
    float prev_vz = 0.0f;

    for (int frame = 0; frame < total_frames; ++frame) {
        // Get state BEFORE physics update
        float z_before, vz_before;
        {
            auto particles = ps.lock_particles_for_read();
            z_before = particles[pid].z;
            vz_before = particles[pid].vz;
        }

        // Run physics
        engine.update(dt);

        // Get state AFTER physics update
        float z_after, vz_after;
        {
            auto particles = ps.lock_particles_for_read();
            z_after = particles[pid].z;
            vz_after = particles[pid].vz;
        }

        float bottom_z = z_after - 0.25f;  // half-thickness
        float penetration = TURTLE_Z - bottom_z;  // positive = penetrating

        // Log first 30 frames, then every 10th
        if (frame < 30 || frame % 10 == 0) {
            std::cout << std::setw(5) << frame << " | "
                      << std::setw(11) << z_after << " | "
                      << std::setw(11) << vz_after << " | "
                      << std::setw(11) << bottom_z << " | "
                      << std::setw(11) << penetration;

            // Annotate significant events
            if (frame == 0) {
                std::cout << "  <- INITIAL";
            } else if (penetration > 0.001f) {
                std::cout << "  <- PENETRATING!";
            } else if (std::abs(vz_after) < 0.01f && std::abs(z_after - 0.25f) < 0.01f) {
                std::cout << "  <- EQUILIBRIUM";
            } else if (vz_after > 0 && vz_before < 0) {
                std::cout << "  <- BOUNCE!";
            }

            std::cout << "\n";
        }

        prev_z = z_after;
        prev_vz = vz_after;
    }

    // Final state
    float final_z, final_vz;
    {
        auto particles = ps.lock_particles_for_read();
        final_z = particles[pid].z;
        final_vz = particles[pid].vz;
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FINAL STATE @ t=1s                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    std::cout << "  z:  " << final_z << "m (expected ~0.25m)\n";
    std::cout << "  vz: " << final_vz << " m/s (expected ~0)\n";
    std::cout << "  bottom_z: " << (final_z - 0.25f) << "m\n";

    // Check result
    bool at_rest = (std::abs(final_z - 0.25f) < 0.1f) && (std::abs(final_vz) < 0.5f);

    std::cout << "\n";
    if (at_rest) {
        std::cout << "  RESULT: Particle resting on Turtle (good)\n";
    } else if (final_z > 1.0f) {
        std::cout << "  RESULT: Particle BOUNCING UP (bad - Baumgarte too aggressive)\n";
    } else if (final_z < -0.1f) {
        std::cout << "  RESULT: Particle fell THROUGH Turtle (bad - collision missed)\n";
    } else {
        std::cout << "  RESULT: Particle oscillating (bad - no convergence)\n";
    }

    return at_rest;
}
