#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <map>

// ============================================================================
// SLEEP DIAGNOSTICS TEST
// ============================================================================
// Diagnostic test to understand why particles aren't sleeping.
// Creates a deterministic scene with trees and analyzes:
// - Velocity distribution (which band are particles in?)
// - Adaptive damping effectiveness (is low_velocity_frames incrementing?)
// - Sleep/wake patterns over time
//
// Run with: ./logosphere-tests --test test_sleep_diagnostics
// ============================================================================

bool test_sleep_diagnostics() {
    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Sleep Diagnostics";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    auto gravity = std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f);
    engine.get_physics_system().add_force(std::move(gravity));

    auto& ps = engine.get_particle_system();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  SLEEP DIAGNOSTICS TEST                                      ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Create floor (kinematic)
    Particle floor = {};
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.z = 0.05f;  // sit ON the turtle
    floor.shape = ParticleShape::BOX;
    floor.width = 50.0f;
    floor.height = 50.0f;
    floor.thickness = 0.1f;
    floor.r = 0.35f;
    floor.g = 0.25f;
    floor.b = 0.15f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    engine.add_particle(floor);

    // Create 5 trees in a row
    PhysicsTreeGenerator generator;
    generator.initialize(&engine);

    std::vector<PhysicsTreeResult> trees;
    int total_tree_particles = 0;

    TreeSpec spec;
    spec.height = 8.0f;
    spec.trunk_diameter = 0.4f;
    spec.branch_depth = 4;
    spec.branch_angle = 60.0f;
    spec.branches_per_split = 2;
    spec.length_ratio = 0.7f;
    spec.thickness_ratio = 0.6f;
    spec.side_branch_probability = 0.4f;
    spec.angle_variance = 30.0f;
    spec.length_variance = 0.2f;

    std::cout << "\n[SETUP] Creating 5 trees..." << std::endl;
    for (int i = 0; i < 5; i++) {
        spec.random_seed = 1000 + i;
        float x = -10.0f + i * 5.0f;  // Spread trees 5m apart
        PhysicsTreeResult tree = generator.generate_tree(x, 0.0f, 0.0f, spec);
        trees.push_back(tree);
        total_tree_particles += tree.total_segments + static_cast<int>(tree.leaf_ids.size());
        std::cout << "  Tree " << i << ": " << tree.total_segments << " segments, "
                  << tree.leaf_ids.size() << " leaves at x=" << x << std::endl;
    }

    std::cout << "  Total tree particles: " << total_tree_particles << std::endl;

    // Velocity bands for analysis
    // Band 0: < 0.01 m/s (should be sleeping)
    // Band 1: 0.01 - 0.1 m/s (near sleep threshold)
    // Band 2: 0.1 - 0.5 m/s (adaptive damping range)
    // Band 3: > 0.5 m/s (too fast for damping)
    const float BAND_THRESHOLDS[] = {0.01f, 0.1f, 0.5f};
    const char* BAND_NAMES[] = {"<0.01 (sleeping)", "0.01-0.1 (near threshold)",
                                 "0.1-0.5 (DAMPING RANGE)", ">0.5 (too fast)"};

    std::cout << "\n[SIMULATION] Running 300 frames (5 seconds @ 60Hz)..." << std::endl;
    std::cout << "  Velocity bands:" << std::endl;
    std::cout << "    Band 0: " << BAND_NAMES[0] << std::endl;
    std::cout << "    Band 1: " << BAND_NAMES[1] << std::endl;
    std::cout << "    Band 2: " << BAND_NAMES[2] << " <- Adaptive damping should help here" << std::endl;
    std::cout << "    Band 3: " << BAND_NAMES[3] << std::endl;

    const double dt = 1.0 / 60.0;
    const int total_frames = 300;

    // Track metrics over time
    struct FrameMetrics {
        int band_counts[4] = {0, 0, 0, 0};
        int sleeping_count = 0;
        int woke_count = 0;
        int high_low_vel_frames = 0;  // Particles with low_velocity_frames > 60
        float avg_low_vel_frames = 0.0f;
        float max_velocity = 0.0f;
    };

    std::vector<FrameMetrics> metrics_history;

    for (int frame = 0; frame <= total_frames; frame++) {
        engine.update(dt);

        // Analyze every 30 frames (0.5 seconds)
        if (frame % 30 == 0 && frame > 0) {
            FrameMetrics m;
            float total_low_vel_frames = 0.0f;
            int dynamic_count = 0;

            auto particles = ps.lock_particles_for_read();
            for (size_t i = 0; i < particles.size(); i++) {
                const Particle& p = particles[i];
                if (p.GetMass() > 50000.0f) continue;  // Skip heavy floor

                dynamic_count++;
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                m.max_velocity = std::max(m.max_velocity, vel);

                // Categorize by velocity band
                int band = 3;
                if (vel < BAND_THRESHOLDS[0]) band = 0;
                else if (vel < BAND_THRESHOLDS[1]) band = 1;
                else if (vel < BAND_THRESHOLDS[2]) band = 2;
                m.band_counts[band]++;

                if (p.is_at_rest) m.sleeping_count++;

                // Check adaptive damping counter
                total_low_vel_frames += p.low_velocity_frames;
                if (p.low_velocity_frames > 60) m.high_low_vel_frames++;
            }

            m.avg_low_vel_frames = dynamic_count > 0 ? total_low_vel_frames / dynamic_count : 0.0f;
            metrics_history.push_back(m);

            // Print progress
            float time_s = frame / 60.0f;
            std::cout << "\n  [" << std::fixed << std::setprecision(1) << time_s << "s] "
                      << "Dynamic particles: " << dynamic_count << std::endl;
            std::cout << "    Velocity distribution:" << std::endl;
            for (int b = 0; b < 4; b++) {
                float pct = dynamic_count > 0 ? 100.0f * m.band_counts[b] / dynamic_count : 0.0f;
                std::cout << "      Band " << b << ": " << std::setw(4) << m.band_counts[b]
                          << " (" << std::setprecision(1) << std::setw(5) << pct << "%) "
                          << BAND_NAMES[b] << std::endl;
            }
            std::cout << "    Sleeping: " << m.sleeping_count << "/" << dynamic_count
                      << " (" << std::setprecision(1) << (100.0f * m.sleeping_count / dynamic_count) << "%)" << std::endl;
            std::cout << "    Avg low_velocity_frames: " << std::setprecision(1) << m.avg_low_vel_frames << std::endl;
            std::cout << "    Particles with low_vel_frames > 60: " << m.high_low_vel_frames
                      << " (should be getting damped)" << std::endl;
            std::cout << "    Max velocity: " << std::setprecision(4) << m.max_velocity << " m/s" << std::endl;
        }
    }

    // Final analysis
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  ANALYSIS SUMMARY                                            ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    if (metrics_history.size() >= 2) {
        const auto& first = metrics_history.front();
        const auto& last = metrics_history.back();

        std::cout << "\n  Initial vs Final state:" << std::endl;
        std::cout << "    Band 2 (damping range): " << first.band_counts[2] << " → " << last.band_counts[2];
        if (last.band_counts[2] < first.band_counts[2]) {
            std::cout << " ✓ (reduced)" << std::endl;
        } else {
            std::cout << " ✗ (NOT reduced - damping not helping!)" << std::endl;
        }

        std::cout << "    Sleeping: " << first.sleeping_count << " → " << last.sleeping_count;
        if (last.sleeping_count > first.sleeping_count) {
            std::cout << " ✓ (increased)" << std::endl;
        } else {
            std::cout << " ✗ (NOT increased)" << std::endl;
        }

        std::cout << "    Avg low_vel_frames: " << std::setprecision(1) << first.avg_low_vel_frames
                  << " → " << last.avg_low_vel_frames;
        if (last.avg_low_vel_frames > first.avg_low_vel_frames) {
            std::cout << " ✓ (counter incrementing)" << std::endl;
        } else {
            std::cout << " ✗ (counter NOT incrementing - particles above 0.5 m/s?)" << std::endl;
        }

        std::cout << "    Particles ready for damping (frames>60): " << last.high_low_vel_frames << std::endl;
    }

    // Diagnosis
    std::cout << "\n  DIAGNOSIS:" << std::endl;
    if (metrics_history.size() >= 2) {
        const auto& last = metrics_history.back();

        if (last.band_counts[3] > last.band_counts[2]) {
            std::cout << "    → Most particles are in Band 3 (>0.5 m/s)" << std::endl;
            std::cout << "    → Adaptive damping threshold (0.5 m/s) is TOO LOW" << std::endl;
            std::cout << "    → Consider raising LOW_VEL_THRESHOLD to 1.0 m/s" << std::endl;
        } else if (last.band_counts[2] > 0 && last.high_low_vel_frames == 0) {
            std::cout << "    → Particles in damping range but low_vel_frames not reaching 60" << std::endl;
            std::cout << "    → Particles are oscillating ABOVE 0.5 m/s intermittently" << std::endl;
            std::cout << "    → This resets the counter - damping never kicks in" << std::endl;
        } else if (last.high_low_vel_frames > 0 && last.sleeping_count < total_tree_particles / 2) {
            std::cout << "    → Damping IS running but particles still not sleeping" << std::endl;
            std::cout << "    → Either damping rate (0.98) is too weak" << std::endl;
            std::cout << "    → Or particles are being woken by contacts" << std::endl;
        } else {
            std::cout << "    → Need more data - run test longer or with more trees" << std::endl;
        }
    }

    std::cout << "\n  [TEST COMPLETE]" << std::endl;
    return true;  // Diagnostic test always passes
}
