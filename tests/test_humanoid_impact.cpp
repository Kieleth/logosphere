// ============================================================================
// PHYSICS EXPERIMENT - Humanoid Impact Response
// ============================================================================
// Purpose: Test physics response when projectile hits humanoid body
//
// Test Cases:
//   Case 1: Humanoid standing alone (baseline) - verify stability
//   Case 2: Boulder thrown at torso from South (-X) - measure knockback
//   Case 3: Boulder thrown at torso from West (-Y) - measure sideways knockback
//
// Key question: Does physics propagate impact through gluon skeleton?
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <GLFW/glfw3.h>

// Helper to wait for SPACE key press
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

struct HumanoidState {
    PhysicsHumanoidResult physics;
    float initial_hips_x = 0.0f;
    float initial_hips_y = 0.0f;
    float initial_hips_z = 0.0f;
    float initial_chest_x = 0.0f;
    float initial_chest_y = 0.0f;
    float initial_chest_z = 0.0f;
};

// Capture initial humanoid positions
void capture_initial_state(Engine& engine, HumanoidState& state) {
    auto& ps = engine.get_particle_system();
    auto particles = ps.lock_particles_for_read();

    state.initial_hips_x = particles[state.physics.hips_id].x;
    state.initial_hips_y = particles[state.physics.hips_id].y;
    state.initial_hips_z = particles[state.physics.hips_id].z;

    // Chest is torso_ids[1] (after abdomen)
    if (state.physics.torso_ids.size() >= 2) {
        int chest_id = state.physics.torso_ids[1];
        state.initial_chest_x = particles[chest_id].x;
        state.initial_chest_y = particles[chest_id].y;
        state.initial_chest_z = particles[chest_id].z;
    }
}

// ============================================================================
// Case 1: Baseline - Humanoid standing (no impact)
// ============================================================================
bool run_case_1_baseline(Engine& engine, bool is_interactive, HumanoidState& humanoid) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 1: Baseline Standing";
    std::string description = "Humanoid standing with no external forces - verify stability";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {description, "", "Expected: No significant drift"});
    }

    capture_initial_state(engine, humanoid);

    std::cout << "\n[INITIAL STATE]" << std::endl;
    std::cout << "  Hips: (" << humanoid.initial_hips_x << ", " << humanoid.initial_hips_y
              << ", " << humanoid.initial_hips_z << ")" << std::endl;

    // Run 5 seconds
    const double dt = 1.0 / 60.0;
    const int total_frames = 300;  // 5 seconds

    std::cout << "\n[RUNNING] 5 seconds simulation..." << std::endl;

    float final_hips_x = 0.0f, final_hips_y = 0.0f, final_hips_z = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        {
            auto particles = ps.lock_particles_for_read();
            final_hips_x = particles[humanoid.physics.hips_id].x;
            final_hips_y = particles[humanoid.physics.hips_id].y;
            final_hips_z = particles[humanoid.physics.hips_id].z;
        }

        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " Hips: (" << std::setprecision(3) << final_hips_x
                      << ", " << final_hips_y << ", " << final_hips_z << ")" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);
            ui->draw_text(10, 40, ("Time: " + std::to_string(frame * dt).substr(0, 4) + "s / 5s").c_str(), 200, 200, 200);

            char pos_buf[128];
            snprintf(pos_buf, sizeof(pos_buf), "Hips: (%.3f, %.3f, %.3f)", final_hips_x, final_hips_y, final_hips_z);
            ui->draw_text(10, 70, pos_buf, 255, 200, 100);

            engine.present();
        }
    }

    // Analysis
    float drift_x = std::abs(final_hips_x - humanoid.initial_hips_x);
    float drift_y = std::abs(final_hips_y - humanoid.initial_hips_y);
    float drift_z = std::abs(final_hips_z - humanoid.initial_hips_z);
    float total_drift = std::sqrt(drift_x*drift_x + drift_y*drift_y + drift_z*drift_z);

    std::cout << "\n[ANALYSIS]" << std::endl;
    std::cout << "  Drift X: " << drift_x << "m" << std::endl;
    std::cout << "  Drift Y: " << drift_y << "m" << std::endl;
    std::cout << "  Drift Z: " << drift_z << "m" << std::endl;
    std::cout << "  Total drift: " << total_drift << "m" << std::endl;

    bool pass = total_drift < 0.1f;  // 10cm tolerance
    std::cout << (pass ? "  ✅ PASS: Humanoid stable" : "  ❌ FAIL: Humanoid drifted") << std::endl;

    if (is_interactive) {
        std::vector<std::string> results;
        char buf[64];
        snprintf(buf, sizeof(buf), "Total drift: %.4fm", total_drift);
        results.push_back(buf);
        results.push_back(pass ? "RESULT: PASS (stable)" : "RESULT: FAIL (drifted)");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 2: Boulder Impact - Throw boulder at torso
// ============================================================================
bool run_case_2_boulder_impact(Engine& engine, bool is_interactive, HumanoidState& humanoid) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 2: Boulder Impact";
    std::string description = "5kg boulder thrown at humanoid torso @ 5 m/s";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {
            description, "",
            "Boulder: 5kg, radius 0.15m",
            "Velocity: 5 m/s toward torso",
            "Impact momentum: 25 kg⋅m/s",
            "",
            "Expected: Visible knockback or torso movement"
        });
    }

    capture_initial_state(engine, humanoid);

    // Get chest position to aim boulder at
    float chest_x, chest_y, chest_z;
    if (humanoid.physics.torso_ids.size() >= 2) {
        int chest_id = humanoid.physics.torso_ids[1];
        auto particles = ps.lock_particles_for_read();
        chest_x = particles[chest_id].x;
        chest_y = particles[chest_id].y;
        chest_z = particles[chest_id].z;
    } else {
        // Fallback to hips
        auto particles = ps.lock_particles_for_read();
        chest_x = particles[humanoid.physics.hips_id].x;
        chest_y = particles[humanoid.physics.hips_id].y;
        chest_z = particles[humanoid.physics.hips_id].z + 0.5f;
    }

    std::cout << "\n[SETUP]" << std::endl;
    std::cout << "  Target (chest): (" << chest_x << ", " << chest_y << ", " << chest_z << ")" << std::endl;

    // Create boulder 1m away in -X direction, will travel +X toward humanoid.
    // 1 m at 5 m/s = 0.2 s flight, ~0.2 m gravity drop: hits the torso as
    // intended. (Was 2 m, authored in the retired double-advance world
    // where horizontal flight ran at ~2x and the drop stayed small; at
    // honest speed the 0.4 s flight dropped the boulder below the torso.)
    Particle boulder = {};
    boulder.x = chest_x - 1.0f;  // 1m away in -X
    boulder.y = chest_y;
    boulder.z = chest_z;
    boulder.vx = 5.0f;  // 5 m/s toward humanoid (+X direction)
    boulder.vy = 0.0f;
    boulder.vz = 0.0f;
    boulder.shape = ParticleShape::BOX;
    boulder.width = 0.3f;      // 30cm cube (boulder)
    boulder.height = 0.3f;
    boulder.thickness = 0.3f;
    boulder.size = 0.3f;
    boulder.SetMaterial(Materials::Type::STONE);  // 5kg boulder
    boulder.r = 0.5f; boulder.g = 0.4f; boulder.b = 0.35f; boulder.a = 1.0f;
    boulder.friction = 0.3f;

    int boulder_id = engine.add_particle(boulder);

    std::cout << "  Boulder: id=" << boulder_id << " pos=(" << boulder.x << ", " << boulder.y << ", " << boulder.z << ")" << std::endl;
    std::cout << "  Velocity: " << boulder.vx << " m/s (toward humanoid)" << std::endl;
    std::cout << "  Momentum: " << (boulder.GetMass() * boulder.vx) << " kg⋅m/s" << std::endl;

    // Run simulation - boulder should hit in ~0.2s (1m / 5 m/s)
    const double dt = 1.0 / 60.0;
    const int total_frames = 300;  // 5 seconds to observe aftermath

    std::cout << "\n[RUNNING] 5 seconds simulation..." << std::endl;
    std::cout << "  Boulder should hit chest in ~0.2s" << std::endl;

    float max_hips_displacement = 0.0f;
    float max_chest_displacement = 0.0f;
    bool impact_detected = false;
    float impact_time = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float hips_x, hips_y, hips_z, chest_curr_x, chest_curr_z, boulder_x_now;
        {
            auto particles = ps.lock_particles_for_read();
            hips_x = particles[humanoid.physics.hips_id].x;
            hips_y = particles[humanoid.physics.hips_id].y;
            hips_z = particles[humanoid.physics.hips_id].z;
            boulder_x_now = particles[boulder_id].x;

            if (humanoid.physics.torso_ids.size() >= 2) {
                int chest_id = humanoid.physics.torso_ids[1];
                chest_curr_x = particles[chest_id].x;
                chest_curr_z = particles[chest_id].z;
            } else {
                chest_curr_x = hips_x;
                chest_curr_z = hips_z + 0.5f;
            }
        }

        // Calculate displacements
        float hips_disp = std::sqrt(
            (hips_x - humanoid.initial_hips_x) * (hips_x - humanoid.initial_hips_x) +
            (hips_y - humanoid.initial_hips_y) * (hips_y - humanoid.initial_hips_y)
        );
        float chest_disp = std::sqrt(
            (chest_curr_x - humanoid.initial_chest_x) * (chest_curr_x - humanoid.initial_chest_x) +
            (chest_curr_z - humanoid.initial_chest_z) * (chest_curr_z - humanoid.initial_chest_z)
        );

        if (hips_disp > max_hips_displacement) max_hips_displacement = hips_disp;
        if (chest_disp > max_chest_displacement) max_chest_displacement = chest_disp;

        // Detect impact by CONTACT: a collision event pairing the boulder
        // with any humanoid body particle. (The old check required the
        // boulder's center to reach within 0.3 m of the chest center —
        // that detects deep penetration, not collision, and only ever
        // fired because the retired double-advance rammed the boulder
        // through the arms at 2x momentum. At honest speed the boulder
        // legitimately bounces off the arm it hits first.)
        if (!impact_detected) {
            const auto& events = engine.get_physics_system().get_collision_events();
            for (const auto& evt : events) {
                bool a_boulder = ((int)evt.particle_a == boulder_id);
                bool b_boulder = ((int)evt.particle_b == boulder_id);
                if (!a_boulder && !b_boulder) continue;
                size_t other = a_boulder ? evt.particle_b : evt.particle_a;
                for (int body_id : humanoid.physics.body_ids) {
                    if ((int)other == body_id) { impact_detected = true; break; }
                }
                if (impact_detected) break;
            }
            if (impact_detected) {
                impact_time = frame * dt;
                std::cout << "  [IMPACT DETECTED] t=" << std::fixed << std::setprecision(3) << impact_time << "s" << std::endl;
                std::cout << "    Boulder at x=" << boulder_x_now << std::endl;
                std::cout << "    Hips displacement: " << hips_disp << "m" << std::endl;
                std::cout << "    Chest displacement: " << chest_disp << "m" << std::endl;
            }
        }

        // Log every second
        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " Hips disp=" << std::setprecision(4) << hips_disp << "m"
                      << " Chest disp=" << chest_disp << "m"
                      << " Boulder x=" << std::setprecision(2) << boulder_x_now
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);

            char buf[128];
            snprintf(buf, sizeof(buf), "Time: %.2fs / 5s", frame * dt);
            ui->draw_text(10, 40, buf, 200, 200, 200);

            snprintf(buf, sizeof(buf), "Hips displacement: %.4fm", hips_disp);
            ui->draw_text(10, 70, buf, 255, 200, 100);

            snprintf(buf, sizeof(buf), "Chest displacement: %.4fm", chest_disp);
            ui->draw_text(10, 100, buf, 255, 200, 100);

            snprintf(buf, sizeof(buf), "Boulder X: %.2fm", boulder_x_now);
            ui->draw_text(10, 130, buf, 100, 200, 255);

            if (impact_detected) {
                ui->draw_text(10, 160, "IMPACT!", 255, 0, 0);
            }

            snprintf(buf, sizeof(buf), "Max hips disp: %.4fm", max_hips_displacement);
            ui->draw_text(10, 190, buf, 150, 255, 150);

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[ANALYSIS]" << std::endl;
    std::cout << "  Impact detected: " << (impact_detected ? "YES" : "NO") << std::endl;
    if (impact_detected) {
        std::cout << "  Impact time: " << impact_time << "s" << std::endl;
    }
    std::cout << "  Max hips displacement: " << max_hips_displacement << "m" << std::endl;
    std::cout << "  Max chest displacement: " << max_chest_displacement << "m" << std::endl;

    // Analysis: Did physics respond?
    // We expect SOME displacement from a 25 kg⋅m/s impact on a ~50kg humanoid
    // Even small displacement indicates physics is working
    bool physics_responded = max_hips_displacement > 0.001f || max_chest_displacement > 0.001f;
    bool significant_knockback = max_hips_displacement > 0.05f;  // 5cm knockback

    std::cout << "\n[PHYSICS RESPONSE]" << std::endl;
    if (physics_responded) {
        std::cout << "  ✅ Physics detected impact (displacement > 1mm)" << std::endl;
    } else {
        std::cout << "  ⚠️  No measurable displacement - friction absorbing all momentum?" << std::endl;
    }

    if (significant_knockback) {
        std::cout << "  ✅ Visible knockback (> 5cm)" << std::endl;
    } else {
        std::cout << "  ⚠️  No visible knockback (< 5cm) - planted stance absorbs impact" << std::endl;
    }

    // This test passes if impact was detected (collision system working)
    // The knockback amount tells us about friction absorption
    bool pass = impact_detected;

    std::cout << "\n" << (pass ? "  ✅ PASS: Collision detected" : "  ❌ FAIL: No collision") << std::endl;

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back(impact_detected ? "Impact: DETECTED" : "Impact: NOT DETECTED");

        char buf[64];
        snprintf(buf, sizeof(buf), "Max hips displacement: %.4fm", max_hips_displacement);
        results.push_back(buf);
        snprintf(buf, sizeof(buf), "Max chest displacement: %.4fm", max_chest_displacement);
        results.push_back(buf);

        if (significant_knockback) {
            results.push_back("Knockback: VISIBLE (> 5cm)");
        } else if (physics_responded) {
            results.push_back("Knockback: MINIMAL (friction absorbing)");
        } else {
            results.push_back("Knockback: NONE");
        }

        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Case 3: Boulder from West - Throw boulder at torso from -Y direction
// ============================================================================
bool run_case_3_boulder_from_west(Engine& engine, bool is_interactive, HumanoidState& humanoid) {
    auto& ps = engine.get_particle_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Case 3: Boulder from West";
    std::string description = "5kg boulder thrown at humanoid torso @ 5 m/s from -Y";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << description << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {
            description, "",
            "Boulder: 5kg, 30cm cube",
            "Velocity: 5 m/s toward torso (+Y direction)",
            "Impact momentum: 25 kg⋅m/s",
            "",
            "Expected: Sideways knockback"
        });
    }

    capture_initial_state(engine, humanoid);

    // Get chest position to aim boulder at
    float chest_x, chest_y, chest_z;
    if (humanoid.physics.torso_ids.size() >= 2) {
        int chest_id = humanoid.physics.torso_ids[1];
        auto particles = ps.lock_particles_for_read();
        chest_x = particles[chest_id].x;
        chest_y = particles[chest_id].y;
        chest_z = particles[chest_id].z;
    } else {
        // Fallback to hips
        auto particles = ps.lock_particles_for_read();
        chest_x = particles[humanoid.physics.hips_id].x;
        chest_y = particles[humanoid.physics.hips_id].y;
        chest_z = particles[humanoid.physics.hips_id].z + 0.5f;
    }

    std::cout << "\n[SETUP]" << std::endl;
    std::cout << "  Target (chest): (" << chest_x << ", " << chest_y << ", " << chest_z << ")" << std::endl;

    // Create boulder 1m away in -Y direction, will travel +Y toward humanoid.
    // Same honest-ballistics recalibration as Case 2 (0.2 s flight, ~0.2 m
    // drop → torso hit as authored).
    Particle boulder = {};
    boulder.x = chest_x;
    boulder.y = chest_y - 1.0f;  // 1m away in -Y (West)
    boulder.z = chest_z;
    boulder.vx = 0.0f;
    boulder.vy = 5.0f;  // 5 m/s toward humanoid (+Y direction)
    boulder.vz = 0.0f;
    boulder.shape = ParticleShape::BOX;
    boulder.width = 0.3f;      // 30cm cube (boulder)
    boulder.height = 0.3f;
    boulder.thickness = 0.3f;
    boulder.size = 0.3f;
    boulder.SetMaterial(Materials::Type::STONE);  // 5kg boulder
    boulder.r = 0.5f; boulder.g = 0.4f; boulder.b = 0.35f; boulder.a = 1.0f;
    boulder.friction = 0.3f;

    int boulder_id = engine.add_particle(boulder);

    std::cout << "  Boulder: id=" << boulder_id << " pos=(" << boulder.x << ", " << boulder.y << ", " << boulder.z << ")" << std::endl;
    std::cout << "  Velocity: " << boulder.vy << " m/s in +Y (toward humanoid from West)" << std::endl;
    std::cout << "  Momentum: " << (boulder.GetMass() * boulder.vy) << " kg⋅m/s" << std::endl;

    // Run simulation - boulder should hit in ~0.2s (1m / 5 m/s)
    const double dt = 1.0 / 60.0;
    const int total_frames = 300;  // 5 seconds to observe aftermath

    std::cout << "\n[RUNNING] 5 seconds simulation..." << std::endl;
    std::cout << "  Boulder should hit chest in ~0.2s" << std::endl;

    float max_hips_displacement = 0.0f;
    float max_chest_displacement = 0.0f;
    bool impact_detected = false;
    float impact_time = 0.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        float hips_x, hips_y, hips_z, chest_curr_x, chest_curr_y, chest_curr_z, boulder_y_now;
        {
            auto particles = ps.lock_particles_for_read();
            hips_x = particles[humanoid.physics.hips_id].x;
            hips_y = particles[humanoid.physics.hips_id].y;
            hips_z = particles[humanoid.physics.hips_id].z;
            boulder_y_now = particles[boulder_id].y;

            if (humanoid.physics.torso_ids.size() >= 2) {
                int chest_id = humanoid.physics.torso_ids[1];
                chest_curr_x = particles[chest_id].x;
                chest_curr_y = particles[chest_id].y;
                chest_curr_z = particles[chest_id].z;
            } else {
                chest_curr_x = hips_x;
                chest_curr_y = hips_y;
                chest_curr_z = hips_z + 0.5f;
            }
        }

        // Calculate displacements (focus on Y for West impact)
        float hips_disp = std::sqrt(
            (hips_x - humanoid.initial_hips_x) * (hips_x - humanoid.initial_hips_x) +
            (hips_y - humanoid.initial_hips_y) * (hips_y - humanoid.initial_hips_y)
        );
        float chest_disp = std::sqrt(
            (chest_curr_x - humanoid.initial_chest_x) * (chest_curr_x - humanoid.initial_chest_x) +
            (chest_curr_y - humanoid.initial_chest_y) * (chest_curr_y - humanoid.initial_chest_y)
        );

        if (hips_disp > max_hips_displacement) max_hips_displacement = hips_disp;
        if (chest_disp > max_chest_displacement) max_chest_displacement = chest_disp;

        // Detect impact by CONTACT (collision event pairing the boulder
        // with any humanoid body particle) — same rationale as Case 2:
        // the old position threshold detected penetration, not collision.
        if (!impact_detected) {
            const auto& events = engine.get_physics_system().get_collision_events();
            for (const auto& evt : events) {
                bool a_boulder = ((int)evt.particle_a == boulder_id);
                bool b_boulder = ((int)evt.particle_b == boulder_id);
                if (!a_boulder && !b_boulder) continue;
                size_t other = a_boulder ? evt.particle_b : evt.particle_a;
                for (int body_id : humanoid.physics.body_ids) {
                    if ((int)other == body_id) { impact_detected = true; break; }
                }
                if (impact_detected) break;
            }
            if (impact_detected) {
                impact_time = frame * dt;
                std::cout << "  [IMPACT DETECTED] t=" << std::fixed << std::setprecision(3) << impact_time << "s" << std::endl;
                std::cout << "    Boulder at y=" << boulder_y_now << std::endl;
                std::cout << "    Hips displacement: " << hips_disp << "m" << std::endl;
                std::cout << "    Chest displacement: " << chest_disp << "m" << std::endl;
            }
        }

        // Log every second
        if (frame % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (frame * dt) << "s]"
                      << " Hips disp=" << std::setprecision(4) << hips_disp << "m"
                      << " Chest disp=" << chest_disp << "m"
                      << " Boulder y=" << std::setprecision(2) << boulder_y_now
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            ui->draw_text(10, 10, test_name + " - RUNNING", 255, 255, 0);

            char buf[128];
            snprintf(buf, sizeof(buf), "Time: %.2fs / 5s", frame * dt);
            ui->draw_text(10, 40, buf, 200, 200, 200);

            snprintf(buf, sizeof(buf), "Hips displacement: %.4fm", hips_disp);
            ui->draw_text(10, 70, buf, 255, 200, 100);

            snprintf(buf, sizeof(buf), "Chest displacement: %.4fm", chest_disp);
            ui->draw_text(10, 100, buf, 255, 200, 100);

            snprintf(buf, sizeof(buf), "Boulder Y: %.2fm", boulder_y_now);
            ui->draw_text(10, 130, buf, 100, 200, 255);

            if (impact_detected) {
                ui->draw_text(10, 160, "IMPACT!", 255, 0, 0);
            }

            snprintf(buf, sizeof(buf), "Max hips disp: %.4fm", max_hips_displacement);
            ui->draw_text(10, 190, buf, 150, 255, 150);

            engine.present();
        }
    }

    // Analysis
    std::cout << "\n[ANALYSIS]" << std::endl;
    std::cout << "  Impact detected: " << (impact_detected ? "YES" : "NO") << std::endl;
    if (impact_detected) {
        std::cout << "  Impact time: " << impact_time << "s" << std::endl;
    }
    std::cout << "  Max hips displacement: " << max_hips_displacement << "m" << std::endl;
    std::cout << "  Max chest displacement: " << max_chest_displacement << "m" << std::endl;

    // Analysis: Did physics respond?
    bool physics_responded = max_hips_displacement > 0.001f || max_chest_displacement > 0.001f;
    bool significant_knockback = max_hips_displacement > 0.05f;  // 5cm knockback

    std::cout << "\n[PHYSICS RESPONSE]" << std::endl;
    if (physics_responded) {
        std::cout << "  ✅ Physics detected impact (displacement > 1mm)" << std::endl;
    } else {
        std::cout << "  ⚠️  No measurable displacement - friction absorbing all momentum?" << std::endl;
    }

    if (significant_knockback) {
        std::cout << "  ✅ Visible knockback (> 5cm)" << std::endl;
    } else {
        std::cout << "  ⚠️  No visible knockback (< 5cm) - planted stance absorbs impact" << std::endl;
    }

    // This test passes if impact was detected (collision system working)
    bool pass = impact_detected;

    std::cout << "\n" << (pass ? "  ✅ PASS: Collision detected" : "  ❌ FAIL: No collision") << std::endl;

    if (is_interactive) {
        std::vector<std::string> results;
        results.push_back(impact_detected ? "Impact: DETECTED" : "Impact: NOT DETECTED");

        char buf[64];
        snprintf(buf, sizeof(buf), "Max hips displacement: %.4fm", max_hips_displacement);
        results.push_back(buf);
        snprintf(buf, sizeof(buf), "Max chest displacement: %.4fm", max_chest_displacement);
        results.push_back(buf);

        if (significant_knockback) {
            results.push_back("Knockback: VISIBLE (> 5cm)");
        } else if (physics_responded) {
            results.push_back("Knockback: MINIMAL (friction absorbing)");
        } else {
            results.push_back("Knockback: NONE");
        }

        results.push_back(pass ? "RESULT: PASS" : "RESULT: FAIL");
        wait_for_space(engine, test_name + " - COMPLETE", results);
    }

    return pass;
}

// ============================================================================
// Main test entry point
// ============================================================================
bool test_humanoid_impact() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS EXPERIMENT - Humanoid Impact Response            ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "Physics Experiment - Humanoid Impact";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Floor
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = -0.15f;
    floor.shape = ParticleShape::BOX;
    floor.width = 20.0f; floor.height = 20.0f; floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.4f; floor.b = 0.3f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy floor - effectively immovable
    floor.friction = 0.8f;
    engine.add_particle(floor);

    // Light
    Particle light = {};
    light.x = -5.0f; light.y = -5.0f; light.z = 10.0f;
    light.size = 0.5f;
    light.SetMaterial(Materials::Type::LIGHT);  // Light source - floats
    light.is_light_source = true;
    light.emission_strength = 80000.0f;
    light.emission_radius = 50.0f;
    light.r = 1.0f; light.g = 0.95f; light.b = 0.9f;
    light.a = 0.0f;
    engine.add_particle(light);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-4.0f, -4.0f, 3.0f);
    camera.look_at(0.0f, 0.0f, 1.0f);
    camera.set_pixels_per_unit(80.0f);

    // Gravity
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Create humanoid
    std::cout << "\n[SETUP] Creating humanoid..." << std::endl;

    HumanoidGenerator gen;
    gen.initialize(&engine, nullptr);

    HumanoidState humanoid;
    humanoid.physics = gen.generate_humanoid_physics(
        0.0f, 0.0f, 0.0f,  // At origin
        -1,                 // Create platform
        HumanoidSpec::hunter(),
        false               // No KG support
    );

    std::cout << "  Body particles: " << humanoid.physics.body_ids.size() << std::endl;
    // TODO[RAGDOLL]: total_mass removed - use kg_->getEntityMass(entity_id) instead
    std::cout << "  Hips ID: " << humanoid.physics.hips_id << std::endl;
    std::cout << "  Torso IDs: ";
    for (int id : humanoid.physics.torso_ids) std::cout << id << " ";
    std::cout << std::endl;

    // Settle for 2 seconds
    std::cout << "\n[SETTLING] 2 seconds..." << std::endl;
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 120; i++) {
        engine.update(dt);
        if (is_interactive && i % 4 == 0) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            ui->draw_text(10, 10, "SETTLING - 2 SECONDS", 255, 255, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "Progress: %.1fs / 2s", i / 60.0);
            ui->draw_text(10, 40, buf, 200, 200, 200);

            engine.present();
        }
    }
    std::cout << "  Settled." << std::endl;

    if (is_interactive) {
        wait_for_space(engine, "READY FOR TESTS", {
            "Humanoid created and settled",
            "",
            "Press SPACE to begin test cases"
        });
    }

    // Run tests
    bool case1_pass = run_case_1_baseline(engine, is_interactive, humanoid);
    bool case2_pass = run_case_2_boulder_impact(engine, is_interactive, humanoid);
    bool case3_pass = run_case_3_boulder_from_west(engine, is_interactive, humanoid);

    // Summary
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║         HUMANOID IMPACT TESTING COMPLETE                     ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Case 1 (Baseline): " << (case1_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 2 (Boulder from South): " << (case2_pass ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "  Case 3 (Boulder from West): " << (case3_pass ? "✅ PASS" : "❌ FAIL") << std::endl;

    bool all_pass = case1_pass && case2_pass && case3_pass;
    std::cout << "\n" << (all_pass ? "✅ ALL TESTS PASSED!" : "❌ SOME TESTS FAILED") << std::endl;

    if (is_interactive) {
        std::vector<std::string> summary = {
            "Case 1 (Baseline): " + std::string(case1_pass ? "PASS" : "FAIL"),
            "Case 2 (Boulder S): " + std::string(case2_pass ? "PASS" : "FAIL"),
            "Case 3 (Boulder W): " + std::string(case3_pass ? "PASS" : "FAIL"),
            "",
            all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED"
        };
        wait_for_space(engine, "TESTING COMPLETE", summary);
    }

    return all_pass;
}
