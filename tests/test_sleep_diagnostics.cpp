#include "../src/core/engine.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <map>

// RETIREMENT PROPOSED (adjudicated 2026-08-21). This file diagnoses a
// mechanism that no longer exists, and the mechanism's absence was verified in
// the tree, not assumed:
//
//   - THE DAMPER IT IS ABOUT IS GONE. src/core/physics_system_v4.cpp:5048-5067
//     carries its obituary: "FRAME-GATED DAMPING: ERADICATED (2026-08-14,
//     owner decree). A speed-gated *0.90/tick velocity tax lived here from
//     2025-12-11 ... Rest belongs to the sleep law (INV-18/24); dissipation
//     belongs to modeled processes (INV-19). Nothing else may touch velocity."
//   - THE COUNTER IT READS SURVIVES ONLY FOR THIS FILE. Same block:
//     "The low_velocity_frames counter still ticks (wake sites reset it); its
//     only remaining reader is diagnostics." Grep agrees — every other
//     reference in src/ is a reset at a wake site.
//   - THE NUMBERS IN ITS ADVICE DO NOT EXIST. There is no LOW_VEL_THRESHOLD
//     and no 0.5 m/s: the surviving constant is DAMPING_VELOCITY_THRESHOLD =
//     0.4 m/s, declared in schema/physics.yaml per INV-29. There is no 0.98
//     damping rate in the physics engine at all; the two 0.98 literals in the
//     tree are IMPULSE_MEMORY_DECAY and an animation ANGULAR_DRAG, neither of
//     which this file measures.
//
// So the DIAGNOSIS block below recommends tuning a dead knob by a dead rate,
// and the recommendation is also forbidden three ways over: INV-19 (damping
// only where a real dissipation process is modelled), INV-29 (a constant like
// that is a declared engine input, never a number tuned at its call site) and
// G-44, closed, which ruled that low speed is not a fixed point at all and
// that quietness must price BOTH channels in one currency, extremity speed
// sqrt(v^2 + (omega*r)^2). Following this file's advice would tune the exact
// mechanism G-44 replaced.
//
// The verdict text is now marked accordingly so a reader cannot act on it by
// mistake. NOTHING WAS DELETED: the file still runs, still asserts nothing,
// still exits 0, and its measurements (velocity bands, sleep fractions) remain
// readable. Retirement — or a rewrite of the bands to extremity speed, which
// is the only version that would say anything about sleep under G-44 — is the
// owner's call. Booked in tests/invariants/TO_INVESTIGATE.md.
//
// What the measurements say TODAY, which is the argument for retiring it:
// 393 of 393 bodies asleep at every checkpoint, the "damping range" band
// empty from first sample to last, and the summary printing "Band 2 (damping
// range): 0 -> 0 (NOT reduced - damping not helping!)" about a world that is
// entirely at rest.
//
// ============================================================================
// SLEEP DIAGNOSTICS TEST
// ============================================================================
// Diagnostic test to understand why particles aren't sleeping.
// LAWS it bears on (none enforced here, see the block above): INV-18
// (sleep hides nothing), INV-7 (a sleeping body never gains velocity),
// G-44 (sleep may only cache a fixed point).
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
            // DEAD MECHANISM. "damping not helping" is not a finding: the
            // frame-gated damper was ERADICATED 2026-08-14 (owner decree,
            // physics_system_v4.cpp:5048). An empty band that stays empty in a
            // world that is entirely at rest is the correct outcome, not a
            // failure. Kept, marked, and not acted on.
            std::cout << " (unchanged — reads as a failure only under the"
                         " frame-gated damper, ERADICATED 2026-08-14)" << std::endl;
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
    //
    // RETIREMENT PROPOSED. Every branch below reasons about the frame-gated
    // adaptive damper, which no longer exists, and two of them recommend
    // tuning constants that no longer exist either. The text is preserved
    // rather than rewritten (a rewrite would be inventing a diagnosis for a
    // mechanism nobody has studied under G-44) and is fenced so a reader
    // cannot act on it by mistake.
    std::cout << "\n  DIAGNOSIS — READS A DEAD MECHANISM, DO NOT ACT ON IT:" << std::endl;
    std::cout << "    The frame-gated adaptive damper this section is about was\n"
                 "    ERADICATED 2026-08-14 by owner decree (physics_system_v4.cpp:5048).\n"
                 "    There is no LOW_VEL_THRESHOLD and no 0.5 m/s gate: the surviving\n"
                 "    constant is DAMPING_VELOCITY_THRESHOLD = 0.4 m/s, declared in\n"
                 "    schema/physics.yaml (INV-29). There is no 0.98 damping rate in the\n"
                 "    physics engine. Acting on the advice below would also break INV-19\n"
                 "    (damping only where a real dissipation process is modelled) and\n"
                 "    would tune the exact mechanism G-44 replaced: low speed is not a\n"
                 "    fixed point, and quietness is extremity speed sqrt(v^2+(omega*r)^2).\n"
                 "    Preserved verbatim below as the historical text it is." << std::endl;
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

    std::cout << "\n  [TEST COMPLETE — diagnostic: INV-18/INV-7/G-44 are "
                 "measured here, none is enforced; this returns true "
                 "unconditionally]" << std::endl;
    std::cout << "  [RETIREMENT PROPOSED 2026-08-21: the mechanism this file "
                 "diagnoses was eradicated 2026-08-14. Booked in "
                 "tests/invariants/TO_INVESTIGATE.md; the owner rules on "
                 "retiring it or rewriting its bands to extremity speed.]"
              << std::endl;
    return true;  // Diagnostic test always passes
}
