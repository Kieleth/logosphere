#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>

// ============================================================================
// TEST: Hunter Rotation - Physics Angular Constraint Test
// ============================================================================
//
// Tests Hunter's angular physics system:
//   Case 1: Standing still (no rotation should occur)
//   Case 2: Head rotation within limits (only head moves)
//   Case 3: Head rotation beyond limits (torso follows)
//   Case 4: Full body rotation via hips
//
// Run: ./logosphere-tests --test test_hunter_rotation
// Visual: INTERACTIVE=1 ./logosphere-tests --test test_hunter_rotation
// ============================================================================

static const float CASE_DURATION = 5.0f;    // seconds per case
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

    // Wait for release
    while (true) {
        engine.get_platform()->poll_events();
        const auto& state = input.get_input_state();
        if (!state.keys[GLFW_KEY_SPACE]) break;
    }
}

// Capture initial rotations for comparison
struct InitialRotations {
    std::vector<int> ids;
    std::vector<float> rotation_z;
    std::vector<float> x, y, z;

    void capture(ParticleSystem& ps, const std::vector<int>& body_ids) {
        ids = body_ids;
        rotation_z.clear();
        x.clear(); y.clear(); z.clear();

        auto particles = ps.lock_particles_for_read();
        size_t particle_count = particles.size();

        for (int id : body_ids) {
            if (id >= 0 && static_cast<size_t>(id) < particle_count) {
                rotation_z.push_back(particles[id].rotation_z);
                x.push_back(particles[id].x);
                y.push_back(particles[id].y);
                z.push_back(particles[id].z);
            } else {
                rotation_z.push_back(0);
                x.push_back(0); y.push_back(0); z.push_back(0);
            }
        }
    }

    float get_rotation_change(ParticleSystem& ps, int particle_id) const {
        auto particles = ps.lock_particles_for_read();
        for (size_t i = 0; i < ids.size(); i++) {
            if (ids[i] == particle_id) {
                return particles[particle_id].rotation_z - rotation_z[i];
            }
        }
        return 0.0f;
    }

    float get_position_change(ParticleSystem& ps, int particle_id) const {
        auto particles = ps.lock_particles_for_read();
        for (size_t i = 0; i < ids.size(); i++) {
            if (ids[i] == particle_id) {
                float dx = particles[particle_id].x - x[i];
                float dy = particles[particle_id].y - y[i];
                float dz = particles[particle_id].z - z[i];
                return std::sqrt(dx*dx + dy*dy + dz*dz);
            }
        }
        return 0.0f;
    }

    void restore(ParticleSystem& ps) const {
        auto particles = ps.lock_particles_for_write();
        size_t particle_count = particles.size();

        for (size_t i = 0; i < ids.size(); ++i) {
            int id = ids[i];
            if (id >= 0 && static_cast<size_t>(id) < particle_count) {
                particles[id].rotation_z = rotation_z[i];
                particles[id].omega_z = 0.0f;
                particles[id].torque_z = 0.0f;
                particles[id].x = x[i];
                particles[id].y = y[i];
                particles[id].z = z[i];
                particles[id].vx = 0.0f;
                particles[id].vy = 0.0f;
                particles[id].vz = 0.0f;
            }
        }
    }
};

// ============================================================================
// CASE 1: Standing Still
// ============================================================================
bool run_standing_case(Engine& engine, PhysicsHumanoidResult& hunter,
                       const InitialRotations& initial, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Case 1: Standing Still" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Expected: No movement, no rotation" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 1: Standing Still", {
            "Hunter should remain completely stationary",
            "No rotation, no position drift",
            "Press SPACE to begin..."
        });
    }

    int total_frames = static_cast<int>(CASE_DURATION / DT);
    float max_rotation = 0.0f;
    float max_position_change = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(DT);

        if (frame % 30 == 0) {
            float head_rot = std::abs(initial.get_rotation_change(ps, hunter.head_id));
            float hips_rot = std::abs(initial.get_rotation_change(ps, hunter.hips_id));
            float head_pos = initial.get_position_change(ps, hunter.head_id);
            float hips_pos = initial.get_position_change(ps, hunter.hips_id);

            max_rotation = std::max(max_rotation, std::max(head_rot, hips_rot));
            max_position_change = std::max(max_position_change, std::max(head_pos, hips_pos));
        }

        if (frame % 60 == 0) {
            int seconds = static_cast<int>(frame * DT);
            std::cout << "  [" << seconds << "s] max_rot=" << std::fixed << std::setprecision(4)
                      << max_rotation << " rad, max_pos=" << max_position_change << " m" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, "Case 1: Standing Still", 255, 255, 0);
            ui->draw_text(10, 40, "Hunter should not move or rotate", 200, 200, 200);
            std::string rot_str = "Max rotation: " + std::to_string(max_rotation * 57.3f).substr(0, 6) + " deg";
            ui->draw_text(10, 65, rot_str, 200, 200, 200);
            engine.present();
        }
    }

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  Max rotation: " << std::fixed << std::setprecision(4) << max_rotation << " rad ("
              << max_rotation * 57.3f << " deg)" << std::endl;
    std::cout << "  Max position change: " << max_position_change << " m" << std::endl;

    bool passed = (max_rotation < 0.1f) && (max_position_change < 0.2f);  // Allow some settling
    std::cout << "  " << (passed ? "PASS" : "FAIL") << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 1: Standing Still - COMPLETE", {
            "Max rotation: " + std::to_string(max_rotation * 57.3f).substr(0, 6) + " deg",
            "Max position: " + std::to_string(max_position_change).substr(0, 5) + " m",
            passed ? "Result: PASS" : "Result: FAIL"
        });
    }

    return passed;
}

// ============================================================================
// CASE 2: Head Rotation Within Limits
// ============================================================================
bool run_head_only_case(Engine& engine, PhysicsHumanoidResult& hunter,
                        const InitialRotations& initial, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Case 2: Head Rotation Within Limits" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Expected: Head rotates ~30 deg, torso stays still" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 2: Head Rotation", {
            "Head will rotate left/right",
            "Torso should NOT rotate (within joint limits)",
            "Press SPACE to begin..."
        });
    }

    int total_frames = static_cast<int>(CASE_DURATION / DT);
    const float TARGET_ROTATION = 0.5f;  // ~30 degrees

    for (int frame = 0; frame < total_frames; frame++) {
        {
            auto particles = ps.lock_particles_for_write();
            float t = frame * DT;
            float target = (t < 2.5f) ? TARGET_ROTATION : -TARGET_ROTATION;

            float current_rot = particles[hunter.head_id].rotation_z;
            float error = target - current_rot;

            const float KP = 50.0f;
            const float KD = 10.0f;
            particles[hunter.head_id].torque_z = KP * error - KD * particles[hunter.head_id].omega_z;
        }

        engine.update(DT);

        if (frame % 60 == 0) {
            float head_rot = initial.get_rotation_change(ps, hunter.head_id);
            float chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);
            float hips_rot = initial.get_rotation_change(ps, hunter.hips_id);

            int seconds = static_cast<int>(frame * DT);
            std::cout << "  [" << seconds << "s] head=" << std::fixed << std::setprecision(3)
                      << head_rot * 57.3f << " chest=" << chest_rot * 57.3f
                      << " hips=" << hips_rot * 57.3f << " deg" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, "Case 2: Head Rotation Within Limits", 255, 255, 0);

            float head_rot = initial.get_rotation_change(ps, hunter.head_id);
            std::string rot_str = "Head rotation: " + std::to_string(head_rot * 57.3f).substr(0, 5) + " deg";
            ui->draw_text(10, 40, rot_str, 200, 200, 200);
            engine.present();
        }
    }

    float final_head_rot = std::abs(initial.get_rotation_change(ps, hunter.head_id));
    float final_chest_rot = std::abs(initial.get_rotation_change(ps, hunter.torso_ids[2]));
    float final_hips_rot = std::abs(initial.get_rotation_change(ps, hunter.hips_id));

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  Head rotation: " << std::fixed << std::setprecision(3)
              << final_head_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Chest rotation: " << final_chest_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Hips rotation: " << final_hips_rot * 57.3f << " deg" << std::endl;

    bool head_moved = final_head_rot > 0.1f;
    bool head_leads = final_head_rot > final_chest_rot;
    bool body_follows = final_chest_rot < final_head_rot * 0.9f;

    bool passed = head_moved && head_leads && body_follows;
    std::cout << "  " << (passed ? "PASS" : "FAIL");
    if (!passed) {
        if (!head_moved) std::cout << " (head didn't rotate enough)";
        if (!head_leads) std::cout << " (head should lead body)";
        if (!body_follows) std::cout << " (body matched head too closely)";
    }
    std::cout << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 2: Head Rotation - COMPLETE", {
            "Head: " + std::to_string(final_head_rot * 57.3f).substr(0, 5) + " deg",
            "Chest: " + std::to_string(final_chest_rot * 57.3f).substr(0, 5) + " deg",
            passed ? "Result: PASS" : "Result: FAIL"
        });
    }

    return passed;
}

// ============================================================================
// CASE 3: Head Rotation Beyond Limits (Torso Follows)
// ============================================================================
bool run_torso_follows_case(Engine& engine, PhysicsHumanoidResult& hunter,
                            const InitialRotations& initial, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Case 3: Head Beyond Limits -> Torso Follows" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Expected: Head hits limit, torso follows" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 3: Torso Follows Head", {
            "Head will try to rotate 90 degrees",
            "Should hit joint limit and pull torso",
            "Press SPACE to begin..."
        });
    }

    int total_frames = static_cast<int>(CASE_DURATION / DT);
    const float TARGET_ROTATION = 1.57f;  // 90 degrees

    for (int frame = 0; frame < total_frames; frame++) {
        {
            auto particles = ps.lock_particles_for_write();

            float current_rot = particles[hunter.head_id].rotation_z;
            float error = TARGET_ROTATION - current_rot;

            const float KP = 100.0f;
            const float KD = 20.0f;
            particles[hunter.head_id].torque_z = KP * error - KD * particles[hunter.head_id].omega_z;
        }

        engine.update(DT);

        if (frame % 60 == 0) {
            float head_rot = initial.get_rotation_change(ps, hunter.head_id);
            float chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);
            float hips_rot = initial.get_rotation_change(ps, hunter.hips_id);

            int seconds = static_cast<int>(frame * DT);
            std::cout << "  [" << seconds << "s] head=" << std::fixed << std::setprecision(3)
                      << head_rot * 57.3f << " chest=" << chest_rot * 57.3f
                      << " hips=" << hips_rot * 57.3f << " deg" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, "Case 3: Torso Follows Head", 255, 255, 0);

            float head_rot = initial.get_rotation_change(ps, hunter.head_id);
            float chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);
            std::string head_str = "Head: " + std::to_string(head_rot * 57.3f).substr(0, 5) + " deg";
            std::string chest_str = "Chest: " + std::to_string(chest_rot * 57.3f).substr(0, 5) + " deg";
            ui->draw_text(10, 40, head_str, 200, 200, 200);
            ui->draw_text(10, 65, chest_str, 200, 200, 200);
            engine.present();
        }
    }

    float final_head_rot = initial.get_rotation_change(ps, hunter.head_id);
    float final_chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);
    float final_hips_rot = initial.get_rotation_change(ps, hunter.hips_id);

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  Head rotation: " << std::fixed << std::setprecision(3)
              << final_head_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Chest rotation: " << final_chest_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Hips rotation: " << final_hips_rot * 57.3f << " deg" << std::endl;

    bool head_moved = final_head_rot > 0.3f;
    bool chest_followed = final_chest_rot > 0.1f;

    bool passed = head_moved && chest_followed;
    std::cout << "  " << (passed ? "PASS" : "FAIL");
    if (!passed) {
        if (!head_moved) std::cout << " (head didn't rotate enough)";
        if (!chest_followed) std::cout << " (chest didn't follow)";
    }
    std::cout << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 3: Torso Follows - COMPLETE", {
            "Head: " + std::to_string(final_head_rot * 57.3f).substr(0, 5) + " deg",
            "Chest: " + std::to_string(final_chest_rot * 57.3f).substr(0, 5) + " deg",
            passed ? "Result: PASS" : "Result: FAIL"
        });
    }

    return passed;
}

// ============================================================================
// CASE 4: Full Body Rotation via Hips
// ============================================================================
bool run_full_body_case(Engine& engine, PhysicsHumanoidResult& hunter,
                        const InitialRotations& initial, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Case 4: Full Body Rotation via Hips" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Expected: Entire body rotates together (including arms)" << std::endl;

    // Capture initial arm positions (upper arms = first particle in each arm chain)
    float init_left_arm_x, init_left_arm_y;
    float init_right_arm_x, init_right_arm_y;
    float init_hips_x, init_hips_y;
    {
        auto particles = ps.lock_particles_for_read();
        int left_upper = hunter.left_arm_ids[0];
        int right_upper = hunter.right_arm_ids[0];
        init_left_arm_x = particles[left_upper].x;
        init_left_arm_y = particles[left_upper].y;
        init_right_arm_x = particles[right_upper].x;
        init_right_arm_y = particles[right_upper].y;
        init_hips_x = particles[hunter.hips_id].x;
        init_hips_y = particles[hunter.hips_id].y;
    }

    std::cout << "  Initial arm positions:" << std::endl;
    std::cout << "    Left arm:  (" << init_left_arm_x << ", " << init_left_arm_y << ")" << std::endl;
    std::cout << "    Right arm: (" << init_right_arm_x << ", " << init_right_arm_y << ")" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 4: Full Body Rotation", {
            "Hips will rotate ~143 degrees",
            "ENTIRE body should follow (including arms!)",
            "Press SPACE to begin..."
        });
    }

    int total_frames = static_cast<int>(CASE_DURATION / DT);
    const float TARGET_ROTATION = 2.5f;  // ~143 degrees

    for (int frame = 0; frame < total_frames; frame++) {
        {
            auto particles = ps.lock_particles_for_write();

            float t = frame * DT;
            float target = TARGET_ROTATION * std::min(1.0f, t / 3.0f);

            float current_rot = particles[hunter.hips_id].rotation_z;
            float error = target - current_rot;

            particles[hunter.hips_id].omega_z = error * 10.0f;
        }

        engine.update(DT);

        if (frame % 60 == 0) {
            auto particles = ps.lock_particles_for_read();
            float hips_rot = initial.get_rotation_change(ps, hunter.hips_id);
            float chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);

            // Get shoulder IDs (particles right before upper arms in body_ids)
            // Shoulders connect chest to upper arms
            int chest_id = hunter.torso_ids[2];
            int left_upper = hunter.left_arm_ids[0];
            int right_upper = hunter.right_arm_ids[0];

            // Track positions
            float chest_x = particles[chest_id].x;
            float chest_y = particles[chest_id].y;
            float chest_rot_z = particles[chest_id].rotation_z;

            float l_arm_x = particles[left_upper].x;
            float l_arm_y = particles[left_upper].y;
            float l_arm_rot = particles[left_upper].rotation_z;

            float r_arm_x = particles[right_upper].x;
            float r_arm_y = particles[right_upper].y;
            float r_arm_rot = particles[right_upper].rotation_z;

            // Arm offset from chest center
            float l_dx = l_arm_x - chest_x;
            float l_dy = l_arm_y - chest_y;
            float r_dx = r_arm_x - chest_x;
            float r_dy = r_arm_y - chest_y;

            int seconds = static_cast<int>(frame * DT);
            std::cout << "  [" << seconds << "s] chest_rot=" << std::fixed << std::setprecision(1)
                      << chest_rot_z * 57.3f << "deg pos=(" << chest_x << "," << chest_y << ")" << std::endl;
            std::cout << "       L_arm: pos=(" << l_arm_x << "," << l_arm_y << ") rot=" << l_arm_rot * 57.3f
                      << "deg offset_from_chest=(" << l_dx << "," << l_dy << ")" << std::endl;
            std::cout << "       R_arm: pos=(" << r_arm_x << "," << r_arm_y << ") rot=" << r_arm_rot * 57.3f
                      << "deg offset_from_chest=(" << r_dx << "," << r_dy << ")" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, "Case 4: Full Body Rotation", 255, 255, 0);

            float hips_rot = initial.get_rotation_change(ps, hunter.hips_id);
            auto particles = ps.lock_particles_for_read();
            int left_upper = hunter.left_arm_ids[0];
            float left_arm_x = particles[left_upper].x;
            float left_arm_y = particles[left_upper].y;

            std::string hips_str = "Hips rotation: " + std::to_string(hips_rot * 57.3f).substr(0, 5) + " deg";
            std::string arm_str = "Left arm pos: (" + std::to_string(left_arm_x).substr(0, 5) + ", "
                                + std::to_string(left_arm_y).substr(0, 5) + ")";
            ui->draw_text(10, 40, hips_str, 200, 200, 200);
            ui->draw_text(10, 65, arm_str, 200, 200, 200);
            engine.present();
        }
    }

    // Get final positions
    float final_left_arm_x, final_left_arm_y;
    float final_right_arm_x, final_right_arm_y;
    {
        auto particles = ps.lock_particles_for_read();
        int left_upper = hunter.left_arm_ids[0];
        int right_upper = hunter.right_arm_ids[0];
        final_left_arm_x = particles[left_upper].x;
        final_left_arm_y = particles[left_upper].y;
        final_right_arm_x = particles[right_upper].x;
        final_right_arm_y = particles[right_upper].y;
    }

    float final_hips_rot = initial.get_rotation_change(ps, hunter.hips_id);
    float final_chest_rot = initial.get_rotation_change(ps, hunter.torso_ids[2]);
    float final_head_rot = initial.get_rotation_change(ps, hunter.head_id);

    // Calculate arm position changes
    float left_arm_dx = final_left_arm_x - init_left_arm_x;
    float left_arm_dy = final_left_arm_y - init_left_arm_y;
    float left_arm_move = std::sqrt(left_arm_dx*left_arm_dx + left_arm_dy*left_arm_dy);

    float right_arm_dx = final_right_arm_x - init_right_arm_x;
    float right_arm_dy = final_right_arm_y - init_right_arm_y;
    float right_arm_move = std::sqrt(right_arm_dx*right_arm_dx + right_arm_dy*right_arm_dy);

    std::cout << "\n[RESULT]" << std::endl;
    std::cout << "  Hips rotation: " << std::fixed << std::setprecision(3)
              << final_hips_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Chest rotation: " << final_chest_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Head rotation: " << final_head_rot * 57.3f << " deg" << std::endl;
    std::cout << "  Left arm movement: " << left_arm_move << " m (expected > 0.1m)" << std::endl;
    std::cout << "  Right arm movement: " << right_arm_move << " m (expected > 0.1m)" << std::endl;

    bool hips_rotated = std::abs(final_hips_rot) > 1.5f;
    bool chest_followed = std::abs(final_chest_rot) > 0.5f;
    bool head_followed = std::abs(final_head_rot) > 0.03f;
    // Arms orbit at different radii (left ~0.34m, right ~0.21m from center)
    // so right arm moves less for same rotation. 8cm threshold accounts for this.
    bool left_arm_followed = left_arm_move > 0.08f;
    bool right_arm_followed = right_arm_move > 0.08f;

    bool passed = hips_rotated && chest_followed && head_followed && left_arm_followed && right_arm_followed;
    std::cout << "  " << (passed ? "PASS" : "FAIL");
    if (!passed) {
        if (!hips_rotated) std::cout << " (hips didn't rotate)";
        if (!chest_followed) std::cout << " (chest didn't follow)";
        if (!head_followed) std::cout << " (head didn't follow)";
        if (!left_arm_followed) std::cout << " (LEFT ARM STUCK! moved only " << left_arm_move << "m)";
        if (!right_arm_followed) std::cout << " (RIGHT ARM STUCK! moved only " << right_arm_move << "m)";
    }
    std::cout << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "Case 4: Full Body - COMPLETE", {
            "Hips: " + std::to_string(final_hips_rot * 57.3f).substr(0, 5) + " deg",
            "L_arm move: " + std::to_string(left_arm_move).substr(0, 5) + " m",
            "R_arm move: " + std::to_string(right_arm_move).substr(0, 5) + " m",
            passed ? "Result: PASS" : "Result: FAIL (arms not following!)"
        });
    }

    return passed;
}

// ============================================================================
// MAIN TEST
// ============================================================================
bool test_hunter_rotation(TestContext& /* ctx */) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  HUNTER ROTATION TEST" << std::endl;
    std::cout << "  Physics Angular Constraint Test" << std::endl;
    std::cout << "========================================" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    Engine engine;
    EngineConfig config;
    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Hunter Rotation Test";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;

    if (engine.initialize(config) != 0) {
        std::cout << "  Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& worldgen = engine.get_worldgen_system();
    auto& humanoid_gen = worldgen.get_humanoid_generator();
    auto& dynamics = engine.get_dynamics_system();
    auto& camera = engine.get_camera_system();

    // Camera setup - center on origin where Hunter spawns
    camera.set_pixels_per_unit(100.0f);
    camera.set_position(0.0f, 0.0f, 0.0f);

    // Gravity
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Light source
    Particle sun = {};
    sun.x = 0.0f; sun.y = -8.0f; sun.z = 8.0f;
    sun.size = 2.0f;
    sun.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    sun.is_light_source = true;
    sun.emission_strength = 80000.0f;
    sun.emission_radius = 500.0f;
    sun.r = 1.0f; sun.g = 0.95f; sun.b = 0.9f;
    engine.add_particle(sun);

    // Floor
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;  // sit ON the turtle
    floor.shape = ParticleShape::BOX;
    floor.width = 10.0f;
    floor.height = 10.0f;
    floor.thickness = 0.1f;
    floor.r = 0.4f; floor.g = 0.5f; floor.b = 0.4f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    engine.add_particle(floor);

    // Create Hunter
    PhysicsHumanoidResult hunter = humanoid_gen.generate_humanoid_physics(
        0.0f, 0.0f, floor.z + floor.thickness * 0.5f,   // ON the floor (INV-37)
        -1,
        HumanoidSpec::hunter(),
        false
    );

    // Volitional entity: friction=0
    {
        auto particles = ps.lock_particles_for_write();
        for (int body_id : hunter.body_ids) {
            particles[body_id].friction = 0.0f;
        }
    }

    // Register with dynamics
    engine.get_humanoid_locomotion().register_humanoid_direct(
        hunter.hips_id,
        hunter.left_leg_ids,
        hunter.right_leg_ids,
        hunter.left_arm_ids,
        hunter.right_arm_ids,
        hunter.torso_ids,
        120.0f,
        800.0f
    );

    std::cout << "\n[HUNTER CREATED]" << std::endl;
    std::cout << "  Body particles: " << hunter.body_ids.size() << std::endl;
    std::cout << "  Head ID: " << hunter.head_id << std::endl;
    std::cout << "  Chest ID: " << hunter.torso_ids[2] << std::endl;
    std::cout << "  Hips ID: " << hunter.hips_id << std::endl;

    // Let physics settle
    std::cout << "\n[SETTLING] 1 second..." << std::endl;
    for (int i = 0; i < 60; i++) {
        engine.update(DT);
    }
    std::cout << "  Settling complete" << std::endl;

    // Capture initial state
    InitialRotations initial;
    initial.capture(ps, hunter.body_ids);

    if (is_interactive) {
        wait_for_space(engine, "Hunter Rotation Test - SETUP COMPLETE", {
            "Hunter created with physics humanoid setup",
            "",
            "4 Test Cases:",
            "  1. Standing Still - no rotation",
            "  2. Head Rotation Within Limits",
            "  3. Head Rotation Beyond Limits (torso follows)",
            "  4. Full Body Rotation via Hips"
        });
    }

    // Run cases
    bool case1_passed = run_standing_case(engine, hunter, initial, is_interactive);
    initial.restore(ps);

    bool case2_passed = run_head_only_case(engine, hunter, initial, is_interactive);
    initial.restore(ps);

    bool case3_passed = run_torso_follows_case(engine, hunter, initial, is_interactive);
    initial.restore(ps);

    bool case4_passed = run_full_body_case(engine, hunter, initial, is_interactive);

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  HUNTER ROTATION TEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Case 1 (Standing): " << (case1_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 2 (Head Only): " << (case2_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 3 (Torso Follows): " << (case3_passed ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Case 4 (Full Body): " << (case4_passed ? "PASS" : "FAIL") << std::endl;

    bool all_passed = case1_passed && case2_passed && case3_passed && case4_passed;
    std::cout << "\n  " << (all_passed ? "ALL TESTS PASSED!" : "SOME TESTS FAILED") << std::endl;
    std::cout << "========================================\n" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "FINAL RESULTS", {
            "Case 1 (Standing): " + std::string(case1_passed ? "PASS" : "FAIL"),
            "Case 2 (Head Only): " + std::string(case2_passed ? "PASS" : "FAIL"),
            "Case 3 (Torso Follows): " + std::string(case3_passed ? "PASS" : "FAIL"),
            "Case 4 (Full Body): " + std::string(case4_passed ? "PASS" : "FAIL"),
            "",
            all_passed ? "ALL TESTS PASSED!" : "SOME TESTS FAILED"
        });
    }

    return all_passed;
}
