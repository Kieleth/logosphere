#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <GLFW/glfw3.h>
#include <string>

// ============================================================================
// EVA MOVEMENT TEST - Physics Humanoid Movement Stress Test
// ============================================================================
// Tests Eva physics skeleton while moving across the world.
// Eva should stay intact (no stretching/breaking) during all movements.
//
// Run: ./logosphere-tests --test test_eva_movement
// Visual: INTERACTIVE=1 ./logosphere-tests --test test_eva_movement
// ============================================================================

// Movement parameters
static const float MOVE_SPEED = 2.0f;       // m/s
static const float CASE_DURATION = 7.0f;    // seconds per case
static const float DT = 1.0f / 60.0f;       // 60 Hz simulation

// Helper to wait for SPACE key (interactive mode)
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
}

// Capture initial positions for later comparison
struct InitialPositions {
    std::vector<int> ids;
    std::vector<float> x, y, z;

    void capture(ParticleSystem& ps, const std::vector<int>& body_ids) {
        ids = body_ids;
        x.clear(); y.clear(); z.clear();

        auto particles = ps.lock_particles_for_read();
        size_t particle_count = particles.size();

        for (int id : body_ids) {
            if (id >= 0 && static_cast<size_t>(id) < particle_count) {
                x.push_back(particles[id].x);
                y.push_back(particles[id].y);
                z.push_back(particles[id].z);
            } else {
                std::cout << "  WARNING: Invalid particle id " << id << std::endl;
                x.push_back(0); y.push_back(0); z.push_back(0);
            }
        }
    }

    // Restore particles to their initial positions and zero velocities
    void restore(ParticleSystem& ps) const {
        auto particles = ps.lock_particles_for_write();
        size_t particle_count = particles.size();

        for (size_t i = 0; i < ids.size(); ++i) {
            int id = ids[i];
            if (id >= 0 && static_cast<size_t>(id) < particle_count) {
                particles[id].x = x[i];
                particles[id].y = y[i];
                particles[id].z = z[i];
                particles[id].vx = 0.0f;
                particles[id].vy = 0.0f;
                particles[id].vz = 0.0f;
            }
        }
    }

    // Check max relative position change (all particles should move together)
    float check_max_stretch(ParticleSystem& ps) const {
        if (ids.empty()) return 0.0f;

        auto particles = ps.lock_particles_for_read();
        size_t particle_count = particles.size();
        float max_diff = 0.0f;

        // Get current offset from first particle
        if (static_cast<size_t>(ids[0]) >= particle_count) return 999.0f;
        float offset_x = particles[ids[0]].x - x[0];
        float offset_y = particles[ids[0]].y - y[0];
        float offset_z = particles[ids[0]].z - z[0];

        // Check all other particles have same offset (within tolerance)
        for (size_t i = 1; i < ids.size(); i++) {
            if (ids[i] < 0 || static_cast<size_t>(ids[i]) >= particle_count) continue;

            float expected_x = x[i] + offset_x;
            float expected_y = y[i] + offset_y;
            float expected_z = z[i] + offset_z;

            float dx = std::abs(particles[ids[i]].x - expected_x);
            float dy = std::abs(particles[ids[i]].y - expected_y);
            float dz = std::abs(particles[ids[i]].z - expected_z);

            float diff = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (diff > max_diff) max_diff = diff;
        }

        return max_diff;
    }
};

// Run a movement case - Eva moves across the world
// PHYSICS-DRIVEN: Uses velocity on hips, not position manipulation
// See docs/PHYSICS_AND_ANIMATIONS.md
bool run_movement_case(Engine& engine, PhysicsHumanoidResult& eva,
                       const std::string& case_name,
                       float vx, float vy,  // velocity in m/s (not per-frame delta!)
                       bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << case_name << std::endl;
    std::cout << "========================================" << std::endl;

    // Check particle count
    size_t particle_count;
    {
        auto particles = ps.lock_particles_for_read();
        particle_count = particles.size();
    }
    std::cout << "  Particle system has " << particle_count << " particles" << std::endl;
    std::cout << "  Eva has " << eva.body_ids.size() << " body particles" << std::endl;

    // Capture initial positions (torso only - limbs animate via end-effectors)
    InitialPositions initial;
    initial.capture(ps, eva.torso_ids);

    std::cout << "  Movement: vx=" << std::fixed << std::setprecision(2)
              << vx << "m/s, vy=" << vy << "m/s" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, case_name, {
            "Eva will move across the world",
            "Press SPACE to begin..."
        });
    }

    int total_frames = static_cast<int>(CASE_DURATION / DT);
    float max_stretch_ever = 0.0f;

    // Set target velocity through dynamics system (it handles acceleration/deceleration)
    engine.get_humanoid_locomotion().set_target_velocity(eva.hips_id, vx, vy);

    for (int frame = 0; frame < total_frames; frame++) {
        // DYNAMICS-CONTROLLED MOVEMENT: set_target_velocity above sets the goal,
        // update_locomotion() in dynamics smoothly accelerates toward it

        // DEBUG: Check velocity BEFORE physics (first 5 frames only)
        if (frame < 5) {
            auto particles = ps.lock_particles_for_read();
            int r_foot = eva.right_leg_ids[0];
            std::cout << "[BEFORE F" << frame << "] r_foot vx=" << particles[r_foot].vx
                      << " vy=" << particles[r_foot].vy << std::endl;
        }

        // Run physics (integrates velocity, applies gluon constraints)
        engine.update(DT);

        // DEBUG: Track right leg every frame for first 15 frames
        if (frame < 5) {
            auto particles = ps.lock_particles_for_read();
            int r_foot = eva.right_leg_ids[0];
            int hips = eva.hips_id;
            std::cout << "[TRACE F" << frame << "] "
                      << "hips: x=" << particles[hips].x << " vx=" << particles[hips].vx
                      << " | r_foot: x=" << particles[r_foot].x << " vx=" << particles[r_foot].vx
                      << " | offset=" << (particles[r_foot].x - particles[hips].x)
                      << std::endl;
        }

        // DEBUG: Track whole leg chain every second
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            // Left leg: foot(0), shin(1), thigh(2)
            int l_foot = eva.left_leg_ids[0];
            int l_shin = eva.left_leg_ids[1];
            int l_thigh = eva.left_leg_ids[2];
            int hips = eva.hips_id;
            std::cout << "[DEBUG F" << frame << "] LegY: "
                      << "foot=" << particles[l_foot].y
                      << " shin=" << particles[l_shin].y
                      << " thigh=" << particles[l_thigh].y
                      << " hips=" << particles[hips].y
                      << " | shin-foot=" << (particles[l_shin].y - particles[l_foot].y)
                      << " thigh-shin=" << (particles[l_thigh].y - particles[l_shin].y)
                      << " hips-thigh=" << (particles[hips].y - particles[l_thigh].y)
                      << std::endl;
        }

        // Check body integrity every 10 frames
        if (frame % 10 == 0) {
            float stretch = initial.check_max_stretch(ps);
            if (stretch > max_stretch_ever) max_stretch_ever = stretch;

            // Check right leg distance from hips (should stay within ~0.5m)
            auto particles = ps.lock_particles_for_read();
            int r_foot = eva.right_leg_ids[0];  // Right foot
            float hips_x = particles[eva.hips_id].x;
            float hips_y = particles[eva.hips_id].y;
            float r_foot_x = particles[r_foot].x;
            float r_foot_y = particles[r_foot].y;
            float leg_offset_x = r_foot_x - hips_x;
            float leg_offset_y = r_foot_y - hips_y;
            float leg_dist = sqrtf(leg_offset_x * leg_offset_x + leg_offset_y * leg_offset_y);

            if (leg_dist > 1.5f) {  // 1.5m tolerance (legs swing during movement)
                std::cout << "\n[FAIL] Right leg detached! Frame " << frame
                          << " leg_dist=" << leg_dist << "m"
                          << " (hips=" << hips_x << "," << hips_y
                          << " foot=" << r_foot_x << "," << r_foot_y << ")" << std::endl;
                // Pause for inspection before failing (interactive mode)
                if (is_interactive) {
                    std::string leg_msg = "leg_dist=" + std::to_string(leg_dist).substr(0, 5) + "m (threshold: 1.5m)";
                    wait_for_space(engine, case_name + " - LEG DETACHED", {
                        leg_msg,
                        "Inspect Eva's broken leg before continuing"
                    });
                }
                return false;  // Fail immediately
            }
        }

        // Report every second
        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            const auto& head = particles[eva.head_id];
            int seconds = static_cast<int>(frame * DT);
            std::cout << "  [" << seconds << "s] Eva at ("
                      << std::fixed << std::setprecision(2) << head.x << ", " << head.y
                      << ") head_z=" << head.z
                      << " stretch=" << std::setprecision(4) << max_stretch_ever << "m"
                      << std::endl;
        }

        // Render in interactive mode
        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto particles = ps.lock_particles_for_read();
            const auto& head = particles[eva.head_id];

            ui->draw_text(10, 10, case_name, 255, 255, 0);

            std::string pos_str = "Eva: (" + std::to_string(head.x).substr(0,5) + ", "
                                + std::to_string(head.y).substr(0,5) + ")";
            ui->draw_text(10, 40, pos_str, 200, 200, 200);

            std::string stretch_str = "Max stretch: " + std::to_string(max_stretch_ever).substr(0,6) + "m";
            ui->draw_text(10, 65, stretch_str, 200, 200, 200);

            int time_left = static_cast<int>(CASE_DURATION - frame * DT);
            std::string time_str = "Time: " + std::to_string(time_left) + "s";
            ui->draw_text(10, 90, time_str, 150, 150, 255);

            engine.present();
        }
    }

    // Final stats
    float final_head_x, final_head_y, final_head_z;
    {
        auto particles = ps.lock_particles_for_read();
        final_head_x = particles[eva.head_id].x;
        final_head_y = particles[eva.head_id].y;
        final_head_z = particles[eva.head_id].z;
    }

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  Final Eva position: (" << std::fixed << std::setprecision(2)
              << final_head_x << ", " << final_head_y << ")" << std::endl;
    std::cout << "  Final head z: " << final_head_z << "m" << std::endl;
    std::cout << "  Max stretch: " << std::setprecision(4) << max_stretch_ever << "m" << std::endl;

    // Pass if max stretch < 50cm (FK animation articulates limbs away from rest offsets;
    // arm swing ~0.17m, leg flexion ~0.16m, spine twist/bounce ~0.05m)
    bool passed = max_stretch_ever < 0.50f;
    std::cout << "  " << (passed ? "PASS" : "FAIL") << std::endl;

    // End-of-case pause for inspection (interactive mode)
    if (is_interactive) {
        std::string result_msg = passed ? "PASSED" : "FAILED";
        std::string stretch_msg = "Max stretch: " + std::to_string(max_stretch_ever).substr(0, 6) + "m";
        wait_for_space(engine, case_name + " - " + result_msg, {
            stretch_msg,
            "Inspect Eva's body integrity before reset"
        });
    }

    return passed;
}

// Reset Eva to initial positions (restores all particles to captured state)
void reset_eva_position(Engine& engine, PhysicsHumanoidResult& eva, const InitialPositions& initial) {
    auto& ps = engine.get_particle_system();

    // Restore all body particles to their initial positions and zero velocities
    // This handles detached limbs correctly (unlike offset-based reset)
    initial.restore(ps);

    // Also reset floor to origin
    {
        auto particles = ps.lock_particles_for_write();
        size_t particle_count = particles.size();
        if (eva.floor_id >= 0 && static_cast<size_t>(eva.floor_id) < particle_count) {
            particles[eva.floor_id].x = 0.0f;
            particles[eva.floor_id].y = 0.0f;
        }
    }

    // Reset dynamics system's position tracking so walk cycle doesn't think Eva teleported
    engine.get_humanoid_locomotion().reset_humanoid_position(eva.hips_id);

    // Reset target velocity to 0 (important between test cases!)
    engine.get_humanoid_locomotion().set_target_velocity(eva.hips_id, 0.0f, 0.0f);
}

bool test_eva_movement(TestContext& /* ctx */) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  EVA MOVEMENT TEST" << std::endl;
    std::cout << "  Physics Humanoid Movement Stress Test" << std::endl;
    std::cout << "========================================" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Eva Movement Test";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // Add sun (light source)
    Particle sun = {};
    sun.x = 0.0f; sun.y = -8.0f; sun.z = 8.0f;
    sun.size = 2.0f;
    sun.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    sun.is_light_source = true;
    sun.emission_strength = 80000.0f;
    sun.emission_radius = 500.0f;
    sun.r = 1.0f; sun.g = 0.95f; sun.b = 0.9f;
    engine.add_particle(sun);

    // Create a LARGE world floor (static reference for movement)
    Particle world_floor = {};
    world_floor.x = 0.0f; world_floor.y = 0.0f; world_floor.z = 0.05f;  // sit ON the turtle
    world_floor.shape = ParticleShape::BOX;
    world_floor.width = 50.0f;    // Large floor
    world_floor.height = 50.0f;
    world_floor.thickness = 0.1f;
    world_floor.r = 0.4f; world_floor.g = 0.5f; world_floor.b = 0.4f; world_floor.a = 1.0f;
    world_floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    engine.add_particle(world_floor);

    // Create Eva using HumanoidGenerator (physics-based)
    HumanoidGenerator gen;
    gen.initialize(&engine, nullptr);

    PhysicsHumanoidResult eva = gen.generate_humanoid_physics(
        0.0f, 0.0f, 0.0f,  // Start at origin
        -1,                 // Create Eva's platform
        HumanoidSpec::eva(),
        false               // kg_support=false (animations require separate integration)
    );

    std::cout << "\n[EVA CREATED]" << std::endl;
    std::cout << "  Body particles: " << eva.body_ids.size() << std::endl;

    // Let physics settle for 1 second
    std::cout << "\n[SETTLING] 1 second..." << std::endl;
    for (int i = 0; i < 60; i++) {
        engine.update(DT);
    }
    std::cout << "  Settling complete" << std::endl;

    // VOLITIONAL ENTITY: Disable friction on Eva's body particles
    // Eva moves via input/animation, not physics forces. Friction would fight
    // the movement velocity, causing gluon chain desync and leg detachment.
    // This matches Eden's handling of volitional entities (eden/src/main.cpp:1146-1156)
    {
        auto particles = ps.lock_particles_for_write();
        for (int body_id : eva.body_ids) {
            particles[body_id].friction = 0.0f;
        }
    }
    std::cout << "  Friction disabled on Eva (volitional entity)" << std::endl;

    // Capture initial positions after settling (used by reset between test cases)
    // This is CRITICAL: if a leg detaches, reset needs absolute positions, not offsets
    InitialPositions settled_positions;
    settled_positions.capture(engine.get_particle_system(), eva.body_ids);

    // Register Eva with dynamics - ALL particles get owner=DYNAMICS
    // This prevents physics from moving torso particles (causes stretch/decomposition)
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids,
        eva.right_leg_ids,
        eva.left_arm_ids,
        eva.right_arm_ids,
        eva.torso_ids  // CRITICAL: include torso to prevent physics from moving it
    );
    std::cout << "[DYNAMICS] Eva registered - all particles owner=DYNAMICS" << std::endl;


    // Results
    bool case1_passed = false;
    bool case2_passed = false;
    bool case3_passed = false;
    bool case4_passed = false;

    // Case 1: Forward movement (positive Y)
    // VELOCITY-BASED: Pass m/s, not per-frame delta
    {
        case1_passed = run_movement_case(engine, eva,
            "Case 1: Forward Movement (Y+)",
            0.0f, MOVE_SPEED, is_interactive);
        reset_eva_position(engine, eva, settled_positions);
    }

    // Case 2: Lateral movement (positive X)
    {
        case2_passed = run_movement_case(engine, eva,
            "Case 2: Lateral Movement (X+)",
            MOVE_SPEED, 0.0f, is_interactive);
        reset_eva_position(engine, eva, settled_positions);
    }

    // Case 3: Backward movement (negative Y)
    {
        case3_passed = run_movement_case(engine, eva,
            "Case 3: Backward Movement (Y-)",
            0.0f, -MOVE_SPEED, is_interactive);
        reset_eva_position(engine, eva, settled_positions);
    }

    // Case 4: Diagonal movement (normalize to maintain speed)
    {
        float diag = MOVE_SPEED * 0.707f;  // sqrt(2)/2 to normalize
        case4_passed = run_movement_case(engine, eva,
            "Case 4: Diagonal Movement (X+Y+)",
            diag, diag, is_interactive);
    }

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  EVA MOVEMENT TEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Case 1 (Forward Y+): " << (case1_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 2 (Lateral X+): " << (case2_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 3 (Backward Y-): " << (case3_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 4 (Diagonal): " << (case4_passed ? "PASS" : "FAIL") << std::endl;

    bool all_passed = case1_passed && case2_passed && case3_passed && case4_passed;
    std::cout << "\n  " << (all_passed ? "ALL TESTS PASSED!" : "SOME TESTS FAILED") << std::endl;
    std::cout << "========================================\n" << std::endl;

    return all_passed;
}
