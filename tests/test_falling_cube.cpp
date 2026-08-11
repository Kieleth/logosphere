#include "../src/core/engine.h"
#include "../src/core/entity_telemetry.h"
#include <iostream>
#include <cmath>
#include <iomanip>

// ============================================================================
// FALLING CUBE TEST - Debug physics collision with floor
// ============================================================================
// A simple cube falls onto a floor. Tests:
// 1. Gravity pulls cube down
// 2. Floor collision stops the cube
// 3. Cube rests on floor, not sinking through
//
// Run with: ./logosphere-tests --test test_falling_cube
// ============================================================================

bool test_falling_cube() {
    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Falling Cube Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    // Add gravity
    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    auto& ps = engine.get_particle_system();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  FALLING CUBE TEST                                           ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // === CREATE FLOOR ===
    // Large flat floor at z=0
    Particle floor;
    floor.shape = ParticleShape::BOX;
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05  // Floor surface at z = 0 + thickness/2 = 0.05
    floor.width = 50.0f;   // 50m x 50m floor
    floor.height = 50.0f;
    floor.thickness = 0.05f;  // 10cm thick
    floor.r = 0.4f;
    floor.g = 0.3f;
    floor.b = 0.2f;
    floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable

    int floor_id = engine.add_particle(floor);
    std::cout << "\n[FLOOR] Created at z=0, 50x50m, heavy mass (static)" << std::endl;
    std::cout << "  ID: " << floor_id << std::endl;
    std::cout << "  Top surface at z = " << (floor.z + floor.thickness/2) << "m" << std::endl;

    // === CREATE FALLING CUBE ===
    // 6m cube starting at z=10 (bottom at z=7)
    Particle cube;
    cube.shape = ParticleShape::BOX;
    cube.x = 0.0f;
    cube.y = 0.0f;
    cube.z = 10.0f;  // Center at z=10, bottom at z=10-3=7
    cube.size = 6.0f;  // 6m cube (for rendering)
    // CRITICAL: Physics uses width/height/thickness, NOT size!
    cube.width = 6.0f;      // X extent
    cube.height = 6.0f;     // Y extent
    cube.thickness = 6.0f;  // Z extent
    cube.r = 0.7f;
    cube.g = 0.7f;
    cube.b = 0.7f;
    cube.a = 1.0f;
    cube.SetMaterial(Materials::Type::STONE);  // 500kg - physics-based, affected by gravity
    cube.vz = 0.0f;  // Start at rest

    int cube_id = engine.add_particle(cube);
    std::cout << "\n[CUBE] Created at z=10, size=6m, mass=500kg, physics-based" << std::endl;
    std::cout << "  ID: " << cube_id << std::endl;
    std::cout << "  Physics AABB: width=" << cube.width << " height=" << cube.height << " thickness=" << cube.thickness << std::endl;
    std::cout << "  Bottom at z = " << (cube.z - cube.thickness/2) << "m" << std::endl;
    std::cout << "  Expected landing z = " << (floor.z + floor.thickness/2 + cube.thickness/2) << "m" << std::endl;

    // === TELEMETRY ===
    auto& telemetry = engine.get_physics_system().get_telemetry();
    telemetry.set_enabled(true);
    telemetry.track_particle(cube_id);
    telemetry.track_particle(floor_id);
    std::cout << "\n[TELEMETRY] Tracking cube (id=" << cube_id << ") and floor (id=" << floor_id << ")" << std::endl;

    // === SIMULATION ===
    std::cout << "\n[SIMULATION] 5 seconds @ 60Hz" << std::endl;
    std::cout << "  Cube should fall ~7m and rest on floor at z~3.05m" << std::endl;
    std::cout << std::endl;

    const double dt = 1.0 / 60.0;
    const int total_frames = 300;  // 5 seconds
    bool nan_detected = false;
    float min_z = 100.0f;
    float final_z = 0.0f;
    float final_vz = 0.0f;

    for (int frame = 0; frame <= total_frames; frame++) {
        engine.update(dt);

        auto particles = ps.lock_particles_for_read();
        float cube_z = particles[cube_id].z;
        float cube_vz = particles[cube_id].vz;
        float floor_z = particles[floor_id].z;

        if (std::isnan(cube_z) || std::isnan(cube_vz)) {
            nan_detected = true;
            std::cout << "  [" << frame << "] NaN detected! z=" << cube_z << " vz=" << cube_vz << std::endl;
            break;
        }

        min_z = std::min(min_z, cube_z);
        final_z = cube_z;
        final_vz = cube_vz;

        // Log every second + first few frames
        if (frame <= 5 || frame % 60 == 0) {
            float bottom_z = cube_z - cube.thickness/2;
            float expected_floor_top = floor_z + floor.thickness/2;
            float penetration = expected_floor_top - bottom_z;

            std::cout << "  [" << std::setw(3) << frame << "] "
                      << "z=" << std::fixed << std::setprecision(3) << std::setw(7) << cube_z
                      << " vz=" << std::setw(7) << cube_vz
                      << " bottom=" << std::setw(7) << bottom_z;

            if (penetration > 0) {
                std::cout << " PENETRATION=" << std::setw(6) << penetration << "m";
            }
            std::cout << std::endl;
        }
    }

    // === CONTACT TELEMETRY ANALYSIS ===
    std::cout << "\n[CONTACT TELEMETRY]" << std::endl;

    const auto* cube_buf = telemetry.query(cube_id);
    const auto* floor_buf = telemetry.query(floor_id);

    int total_contacts = 0;
    int vertical_contacts = 0;   // normal_z dominant (correct for floor)
    int horizontal_contacts = 0; // normal_x or normal_y dominant (wrong for floor)
    int corner_contacts = 0;
    float worst_normal_z = 1.0f; // Track the least-vertical normal

    if (cube_buf && !cube_buf->empty()) {
        std::cout << "  Cube telemetry: " << cube_buf->size() << " frames" << std::endl;

        for (size_t i = 0; i < cube_buf->size(); i++) {
            const auto& frame = cube_buf->at(i);
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
                }

                if (abs_nz < worst_normal_z) {
                    worst_normal_z = abs_nz;
                }

                // Print first 10 contacts and any horizontal ones
                if (total_contacts <= 10 || (abs_nx > 0.1f || abs_ny > 0.1f)) {
                    std::cout << "    f" << frame.frame_number
                              << " P" << c.particle_a << "<>P" << c.particle_b
                              << " n=(" << std::fixed << std::setprecision(3)
                              << c.normal_x << "," << c.normal_y << "," << c.normal_z << ")"
                              << " pen=" << std::setprecision(4) << c.penetration
                              << (c.is_corner_contact ? " CORNER" : "")
                              << (c.is_horizontal ? " HORIZONTAL" : "")
                              << std::endl;
                }
            }
        }
    } else {
        std::cout << "  WARNING: No telemetry recorded for cube!" << std::endl;
    }

    std::cout << "\n  Contact summary:" << std::endl;
    std::cout << "    Total contacts: " << total_contacts << std::endl;
    std::cout << "    Vertical (Z dominant): " << vertical_contacts << std::endl;
    std::cout << "    Horizontal (X/Y dominant): " << horizontal_contacts << std::endl;
    std::cout << "    Corner contacts: " << corner_contacts << std::endl;
    std::cout << "    Worst |normal_z|: " << worst_normal_z << std::endl;

    // === POSITION ANALYSIS ===
    std::cout << "\n[ANALYSIS]" << std::endl;

    float cube_bottom = final_z - cube.thickness/2;
    float floor_top = floor.z + floor.thickness/2;
    float penetration = floor_top - cube_bottom;
    float expected_rest_z = floor_top + cube.thickness/2;

    std::cout << "  Final cube z: " << final_z << "m (center)" << std::endl;
    std::cout << "  Final cube bottom: " << cube_bottom << "m" << std::endl;
    std::cout << "  Floor top: " << floor_top << "m" << std::endl;
    std::cout << "  Expected rest z: " << expected_rest_z << "m" << std::endl;
    std::cout << "  Minimum z reached: " << min_z << "m" << std::endl;
    std::cout << "  Final velocity: " << final_vz << " m/s" << std::endl;

    if (penetration > 0) {
        std::cout << "  PENETRATION: " << penetration << "m into floor!" << std::endl;
    }

    // === CHECK PHYSICS SYSTEM STATE ===
    std::cout << "\n[PHYSICS STATE]" << std::endl;
    auto& physics = engine.get_physics_system();
    std::cout << "  Forces registered: " << physics.get_force_count() << std::endl;
    // Check if BVH is being used
    std::cout << "  Collision detection: checking..." << std::endl;

    // === RESULT ===
    // Pass criteria:
    // 1. No NaN
    // 2. Cube stopped (vz near 0)
    // 3. Cube resting on floor (not penetrating significantly)
    // 4. All contact normals should be vertical (Z dominant) for cube-on-single-floor
    // 5. At least some contacts were recorded
    bool stopped = std::abs(final_vz) < 0.5f;
    bool on_floor = penetration < 0.1f && penetration > -0.5f;
    bool normals_correct = (horizontal_contacts == 0) && (total_contacts > 0);
    bool has_contacts = total_contacts > 0;

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  " << (!nan_detected ? "PASS" : "FAIL") << ": No NaN" << std::endl;
    std::cout << "  " << (stopped ? "PASS" : "FAIL") << ": Cube stopped (vz=" << final_vz << ")" << std::endl;
    std::cout << "  " << (on_floor ? "PASS" : "FAIL") << ": On floor (pen=" << penetration << ")" << std::endl;
    std::cout << "  " << (has_contacts ? "PASS" : "FAIL") << ": Contacts recorded (" << total_contacts << ")" << std::endl;
    std::cout << "  " << (normals_correct ? "PASS" : "FAIL") << ": All normals vertical (" << horizontal_contacts << " horizontal)" << std::endl;

    bool pass = !nan_detected && stopped && on_floor && normals_correct && has_contacts;
    std::cout << "\n" << (pass ? "PASS" : "FAIL") << ": Falling cube (physics + telemetry)" << std::endl;

    return pass;
}
