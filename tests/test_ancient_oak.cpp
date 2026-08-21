#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>

// TO-INVESTIGATE (test-protocol migration, 2026-08-21): the wiggle check is
// COMPUTED AND THEN DROPPED FROM THE VERDICT. `wiggle_ok` requires fewer than
// 10 wiggly frames and a max trunk velocity under 25 mm/s, but `pass` is
// `!nan_detected && max_xy < 1.0 && max_z < 0.50 && total_segments > 0` and
// does not include it. A tree that stands still enough not to drift while
// vibrating forever prints "WARNING: Tree stands but WIGGLES" and RETURNS
// TRUE. Under INV-24 a settled scene performs zero corrective work, so a
// sustained trunk velocity is a correction firing forever, and under INV-3
// whatever sustains it is energy arriving from somewhere the ledger does not
// see. This is the same mask G-44 found under test_tree_wiggly (a sustained
// 0.0294 m/s oscillation in depth-3/4 oaks absorbed by the speed-only sleep
// entry, where "the audited green was a mask"). Nothing here was changed and
// the exit code is unchanged: an owner ruling is owed on whether wiggle_ok
// joins the verdict, and at which bound.
//
// ============================================================================
// THE ANCIENT OAK - A Great Old Tree
// ============================================================================
// A grand ancient oak: tall, thick trunk, deep branching, wide spread.
// Run with: ./logosphere-tests --test test_ancient_oak
// ============================================================================

bool test_ancient_oak() {
    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Ancient Oak";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    auto& ps = engine.get_particle_system();

    // Create floor tile at (0,0) - required for PhysicsTreeGenerator
    Particle floor = {};
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.z = 0.05f;  // sit ON the turtle  // Floor surface at z=0
    floor.shape = ParticleShape::BOX;
    floor.width = 4.0f;
    floor.height = 4.0f;
    floor.thickness = 0.1f;
    floor.r = 0.35f;
    floor.g = 0.25f;
    floor.b = 0.15f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    int floor_id = engine.add_particle(floor);
    std::cout << "  Floor tile created at (0,0), id=" << floor_id << std::endl;

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  THE ANCIENT OAK                                             ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    // Ancient Oak - grand old tree
    TreeSpec spec;
    spec.height = 15.0f;              // 15m tall
    spec.trunk_diameter = 1.2f;       // 1.2m thick trunk
    spec.branch_depth = 6;            // 6 levels deep
    spec.branch_angle = 75.0f;        // Wide spreading
    spec.branches_per_split = 2;
    spec.length_ratio = 0.72f;
    spec.thickness_ratio = 0.65f;
    spec.side_branch_probability = 0.6f;
    spec.angle_variance = 45.0f;
    spec.length_variance = 0.35f;
    spec.random_seed = 1066;

    std::cout << "\n[GENERATING]" << std::endl;
    std::cout << "  Height: " << spec.height << "m" << std::endl;
    std::cout << "  Trunk: " << spec.trunk_diameter << "m diameter" << std::endl;
    std::cout << "  Depth: " << spec.branch_depth << " levels" << std::endl;

    PhysicsTreeResult tree = generator.generate_tree(0.0f, 0.0f, 0.0f, spec);

    std::cout << "\n[TREE]" << std::endl;
    std::cout << "  Segments: " << tree.total_segments << std::endl;
    std::cout << "  Leaves: " << tree.leaf_ids.size() << std::endl;

    // Record initial positions
    std::vector<float> initial_x, initial_y, initial_z;
    {
        auto particles = ps.lock_particles_for_read();
        for (int id : tree.branch_ids) {
            initial_x.push_back(particles[id].x);
            initial_y.push_back(particles[id].y);
            initial_z.push_back(particles[id].z);
        }
    }

    std::cout << "\n[SIMULATION] 10 seconds @ 60Hz" << std::endl;

    const double dt = 1.0 / 60.0;
    const int total_frames = 600;  // 10 seconds
    bool nan_detected = false;
    float max_x = 0.0f, max_y = 0.0f, max_z = 0.0f;
    float max_trunk_velocity = 0.0f;
    int wiggly_frames = 0;
    const float WIGGLY_THRESHOLD = 0.005f;  // 5mm/s considered wiggly

    for (int frame = 0; frame <= total_frames; frame++) {
        engine.update(dt);

        // Track velocity every 10 frames
        if (frame % 10 == 0) {
            auto particles = ps.lock_particles_for_read();
            if (tree.trunk_id >= 0 && tree.trunk_id < static_cast<int>(particles.size())) {
                const Particle& trunk = particles[tree.trunk_id];
                float v = std::sqrt(trunk.vx*trunk.vx + trunk.vy*trunk.vy + trunk.vz*trunk.vz);
                max_trunk_velocity = std::max(max_trunk_velocity, v);
                if (v > WIGGLY_THRESHOLD) wiggly_frames++;
            }
        }

        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            if (tree.trunk_id < 0 || tree.trunk_id >= static_cast<int>(particles.size())) {
                std::cout << "  [" << (frame/60) << "s] No trunk!" << std::endl;
                continue;
            }
            float trunk_z = particles[tree.trunk_id].z;
            const Particle& trunk = particles[tree.trunk_id];
            float trunk_v = std::sqrt(trunk.vx*trunk.vx + trunk.vy*trunk.vy + trunk.vz*trunk.vz);

            if (std::isnan(trunk_z)) {
                nan_detected = true;
                std::cout << "  [" << (frame/60) << "s] NaN detected!" << std::endl;
                break;
            }

            float dx = 0.0f, dy = 0.0f, dz = 0.0f;
            for (size_t i = 0; i < tree.branch_ids.size(); i++) {
                int id = tree.branch_ids[i];
                if (id >= 0 && id < static_cast<int>(particles.size())) {
                    dx = std::max(dx, std::abs(particles[id].x - initial_x[i]));
                    dy = std::max(dy, std::abs(particles[id].y - initial_y[i]));
                    dz = std::max(dz, std::abs(particles[id].z - initial_z[i]));
                }
            }

            max_x = std::max(max_x, dx);
            max_y = std::max(max_y, dy);
            max_z = std::max(max_z, dz);

            std::cout << "  [" << (frame/60) << "s] drift X=" << std::fixed << std::setprecision(3)
                      << dx << "m Y=" << dy << "m Z=" << dz << "m  trunk_v="
                      << std::setprecision(4) << (trunk_v * 1000.0f) << "mm/s" << std::endl;
        }
    }

    std::cout << "\n[WIGGLE ANALYSIS]" << std::endl;
    std::cout << "  Max trunk velocity: " << std::fixed << std::setprecision(4)
              << (max_trunk_velocity * 1000.0f) << " mm/s" << std::endl;
    std::cout << "  Wiggly frames (>" << WIGGLY_THRESHOLD*1000 << "mm/s): "
              << wiggly_frames << "/30" << std::endl;

    float max_xy = std::max(max_x, max_y);
    bool wiggle_ok = wiggly_frames < 10 && max_trunk_velocity < 0.025f;  // Less than 25mm/s max
    bool pass = !nan_detected && max_xy < 1.0f && max_z < 0.50f && tree.total_segments > 0;

    std::cout << "\n[RESULT]" << std::endl;
    if (tree.total_segments == 0) {
        std::cout << "  ❌ FAIL hygiene: No tree created (floor tile missing?)" << std::endl;
        return false;
    }
    if (pass && wiggle_ok) {
        std::cout << "  ✅ PASS INV-11/INV-24: Ancient Oak stands strong and stable!" << std::endl;
    } else if (pass && !wiggle_ok) {
        std::cout << "  ⚠️  TO-INVESTIGATE, still counted as a pass: INV-24 says a "
                     "settled scene performs zero corrective work. Tree stands but WIGGLES (trunk_v="
                  << (max_trunk_velocity * 1000.0f) << "mm/s)" << std::endl;
    } else {
        std::cout << "  ❌ FAIL " << (nan_detected ? "PROPOSED FINITE-STATE: Collapsed (NaN)"
                                                : "INV-11: Too much drift") << std::endl;
    }

    return pass;
}
