// Bilateral Animation Test
//
// Validates that the BodyMap parameterization produces correct mirrored
// animations for both LEFT and RIGHT sides.
//
// Controls:
//   SPACE = cycle through animations
//   ESC = exit
//
// Clips:
//   0: Right kick (skill 0.5)    1: Left kick (skill 0.5)
//   2: Right punch (skill 0.5)   3: Left punch (skill 0.5)
//   4: Right kick (skill 1.0)    5: Left kick (skill 1.0)
//
// Usage:
//   ./build/logomancers/test-bilateral-animation              # headless (auto)
//   ./build/logomancers/test-bilateral-animation --visual     # interactive window

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "core/force.h"
#include "humanoid_validator.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

struct ClipInfo {
    FKAnimationClip clip;
    const char* label;
    Side side;
    bool is_kick;  // true=kick, false=punch
};

// Snapshot of all limb endpoint positions (relative to hips)
struct EndpointSnapshot {
    float hips_x, hips_y, hips_z;
    float r_hand_x, r_hand_y, r_hand_z;
    float l_hand_x, l_hand_y, l_hand_z;
    float r_foot_x, r_foot_y, r_foot_z;
    float l_foot_x, l_foot_y, l_foot_z;

    void print(const char* label) const {
        printf("  [%s] Positions (relative to hips at %.3f, %.3f, %.3f):\n",
               label, hips_x, hips_y, hips_z);
        printf("    R_Hand: dx=%+.4f dy=%+.4f dz=%+.4f\n",
               r_hand_x - hips_x, r_hand_y - hips_y, r_hand_z - hips_z);
        printf("    L_Hand: dx=%+.4f dy=%+.4f dz=%+.4f\n",
               l_hand_x - hips_x, l_hand_y - hips_y, l_hand_z - hips_z);
        printf("    R_Foot: dx=%+.4f dy=%+.4f dz=%+.4f\n",
               r_foot_x - hips_x, r_foot_y - hips_y, r_foot_z - hips_z);
        printf("    L_Foot: dx=%+.4f dy=%+.4f dz=%+.4f\n",
               l_foot_x - hips_x, l_foot_y - hips_y, l_foot_z - hips_z);
    }
};

int main(int argc, char** argv) {
    bool headless = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--visual") headless = false;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  BILATERAL ANIMATION TEST" << std::endl;
    std::cout << "  BodyMap side parameterization" << std::endl;
    std::cout << "  Mode: " << (headless ? "Headless" : "Visual") << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "  SPACE = cycle animations" << std::endl;
    std::cout << "  ESC = exit" << std::endl;
    std::cout << "========================================\n" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "Bilateral Animation Test";
    config.show_debug_overlay = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "Engine init failed" << std::endl;
        return 1;
    }

    auto& ps = engine.get_particle_system();
    auto& dynamics = engine.get_dynamics_system();
    auto& physics = engine.get_physics_system();
    auto& input = engine.get_input_system();
    auto& kg = engine.get_kg();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Floor
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
    floor.shape = ParticleShape::BOX;
    floor.width = 10.0f; floor.height = 10.0f; floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    engine.add_particle(floor);

    // Lights
    auto add_light = [&engine](float x, float y, float z, float strength) {
        Particle light = {};
        light.x = x; light.y = y; light.z = z;
        light.shape = ParticleShape::BOX;
        light.width = 0.2f; light.height = 0.2f; light.thickness = 0.2f;
        light.r = 1.0f; light.g = 0.95f; light.b = 0.9f; light.a = 1.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = strength;
        light.emission_radius = 100.0f;
        engine.add_particle(light);
    };
    add_light(0.0f, 0.0f, 10.0f, 600000.0f);
    add_light(0.0f, -4.0f, 3.0f, 400000.0f);
    add_light(3.0f, 0.0f, 3.0f, 300000.0f);
    add_light(-3.0f, 0.0f, 3.0f, 300000.0f);

    // Create humanoid
    HumanoidGenerator humanoid_gen;
    humanoid_gen.initialize(&engine, &kg);
    HumanoidSpec spec = HumanoidSpec::hunter();
    auto h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        h.hips_id,
        h.left_leg_ids, h.right_leg_ids,
        h.left_arm_ids, h.right_arm_ids,
        h.torso_ids,
        150.0f, 600.0f,
        h.entity_id
    );
    h.register_joints(&engine.get_humanoid_locomotion());
    engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
    engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);

    constexpr float FACING_S = static_cast<float>(M_PI);
    engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-6.0f, -6.0f, 6.0f);
    camera.look_at(0.0f, 0.0f, 1.0f);
    camera.set_pixels_per_unit(80.0f);

    // Endpoint particle IDs
    // left_leg_ids = {foot, shin, thigh}, right_leg_ids = {foot, shin, thigh}
    // left_arm_ids = {shoulder, upper_arm, forearm, hand}, right_arm_ids = same
    int r_hand_id = h.right_arm_ids[3];
    int l_hand_id = h.left_arm_ids[3];
    int r_foot_id = h.right_leg_ids[0];
    int l_foot_id = h.left_leg_ids[0];

    auto take_snapshot = [&]() -> EndpointSnapshot {
        EndpointSnapshot s;
        auto hips = ps.get_particle_copy(h.hips_id);
        auto rh = ps.get_particle_copy(r_hand_id);
        auto lh = ps.get_particle_copy(l_hand_id);
        auto rf = ps.get_particle_copy(r_foot_id);
        auto lf = ps.get_particle_copy(l_foot_id);
        s.hips_x = hips.x; s.hips_y = hips.y; s.hips_z = hips.z;
        s.r_hand_x = rh.x; s.r_hand_y = rh.y; s.r_hand_z = rh.z;
        s.l_hand_x = lh.x; s.l_hand_y = lh.y; s.l_hand_z = lh.z;
        s.r_foot_x = rf.x; s.r_foot_y = rf.y; s.r_foot_z = rf.z;
        s.l_foot_x = lf.x; s.l_foot_y = lf.y; s.l_foot_z = lf.z;
        return s;
    };

    // Build clip list: pairs of RIGHT/LEFT for each move type and skill
    std::vector<ClipInfo> clips;
    clips.push_back({create_fk_front_kick(0.5f, Side::RIGHT, true), "Right Kick (skill 0.5)", Side::RIGHT, true});
    clips.push_back({create_fk_front_kick(0.5f, Side::LEFT, true),  "Left Kick (skill 0.5)",  Side::LEFT, true});
    clips.push_back({create_fk_cross_punch(0.5f, Side::RIGHT, true),"Right Punch (skill 0.5)", Side::RIGHT, false});
    clips.push_back({create_fk_cross_punch(0.5f, Side::LEFT, true), "Left Punch (skill 0.5)",  Side::LEFT, false});
    clips.push_back({create_fk_front_kick(1.0f, Side::RIGHT, true), "Right Kick (skill 1.0)", Side::RIGHT, true});
    clips.push_back({create_fk_front_kick(1.0f, Side::LEFT, true),  "Left Kick (skill 1.0)",  Side::LEFT, true});

    // Store peak snapshots for mirror comparison (peak = max forward displacement)
    std::vector<EndpointSnapshot> peak_snapshots(clips.size());
    std::vector<float> peak_forward_y(clips.size(), 0.0f);  // most negative = most forward

    std::cout << "[CLIPS] Animation sequence:" << std::endl;
    for (size_t i = 0; i < clips.size(); ++i) {
        std::cout << "  " << i << ": " << clips[i].label
                  << " (" << clips[i].clip.duration_ms << "ms)" << std::endl;
    }

    // Animation state
    size_t current_clip = 0;
    bool fk_playing = false;
    float fk_time_ms = 0.0f;
    int assertion_failures = 0;
    bool space_was_pressed = false;
    int frame_count = 0;

    const double dt = 1.0 / 60.0;
    const float animation_speed = 0.5f;
    const float dt_ms = static_cast<float>(dt * 1000.0) * animation_speed;

    // Headless: auto-start after brief settle
    bool headless_auto_started = false;

    std::cout << "\n[READY] Press SPACE to play: " << clips[current_clip].label << "\n" << std::endl;

    while (true) {
        engine.get_platform()->poll_events();
        frame_count++;

        const auto& input_state = input.get_input_state();
        if (!headless && (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close())) {
            break;
        }

        // Headless: auto-start each animation immediately when idle
        if (headless && !fk_playing && frame_count >= 2) {
            engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
            engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);
            fk_playing = true;
            fk_time_ms = 0.0f;
            std::cout << "\n[FK PLAY] Starting: " << clips[current_clip].label << std::endl;
        }

        // SPACE starts current animation (visual mode)
        bool space_pressed = input_state.keys[GLFW_KEY_SPACE];
        if (space_pressed && !space_was_pressed && !fk_playing) {
            engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
            engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);
            fk_playing = true;
            fk_time_ms = 0.0f;
            std::cout << "\n[FK PLAY] Starting: " << clips[current_clip].label << std::endl;
        }
        space_was_pressed = space_pressed;

        // Update engine (physics)
        engine.update(dt);

        // Apply FK animation
        if (fk_playing) {
            FKAnimationClip& clip = clips[current_clip].clip;
            fk_time_ms += dt_ms;
            float query_time = std::min(fk_time_ms, clip.duration_ms);

            RotationPose pose;
            if (clip.get_pose_at_time(query_time, pose)) {
                // Apply all joint targets
                for (const auto& target : pose.targets) {
                    switch (target.type) {
                        case JointTargetType::DRIVEN:
                            switch (target.axis) {
                                case RotationAxis::X:
                                    engine.get_humanoid_locomotion().set_joint_rotation_x(h.entity_id, target.joint_name, target.angle);
                                    break;
                                case RotationAxis::Y:
                                    engine.get_humanoid_locomotion().set_joint_rotation_y(h.entity_id, target.joint_name, target.angle);
                                    break;
                                case RotationAxis::Z:
                                    engine.get_humanoid_locomotion().set_joint_rotation_z(h.entity_id, target.joint_name, target.angle);
                                    break;
                            }
                            break;
                        case JointTargetType::INHERIT:
                            engine.get_humanoid_locomotion().set_joint_rigid(h.entity_id, target.joint_name);
                            break;
                        case JointTargetType::PHYSICS:
                            engine.get_humanoid_locomotion().set_joint_relax(h.entity_id, target.joint_name);
                            break;
                        case JointTargetType::SEMANTIC:
                            switch (target.semantic) {
                                case SemanticChannel::FLEX:
                                    engine.get_humanoid_locomotion().set_joint_flex(h.entity_id, target.joint_name, target.angle);
                                    break;
                                case SemanticChannel::ABDUCT:
                                    engine.get_humanoid_locomotion().set_joint_abduct(h.entity_id, target.joint_name, target.angle);
                                    break;
                                case SemanticChannel::TWIST:
                                    engine.get_humanoid_locomotion().set_joint_twist(h.entity_id, target.joint_name, target.angle);
                                    break;
                            }
                            break;
                        default:
                            break;
                    }
                }
                engine.get_humanoid_locomotion().apply_entity_fk(h.entity_id);

                // Track peak forward displacement of the active endpoint
                auto snap = take_snapshot();
                float active_y;
                if (clips[current_clip].is_kick) {
                    active_y = (clips[current_clip].side == Side::RIGHT)
                        ? snap.r_foot_y : snap.l_foot_y;
                } else {
                    active_y = (clips[current_clip].side == Side::RIGHT)
                        ? snap.r_hand_y : snap.l_hand_y;
                }
                // Facing south → forward = -Y, so most negative = most forward
                if (active_y < peak_forward_y[current_clip]) {
                    peak_forward_y[current_clip] = active_y;
                    peak_snapshots[current_clip] = snap;
                }
            }

            // Periodic connectivity check
            if (frame_count % 10 == 0) {
                auto val = validate_humanoid(ps, physics, h);
                if (!val.all_connected()) {
                    printf("[DRIFT] frame=%d clip=%s\n", frame_count, clips[current_clip].label);
                    val.print_report();
                }
            }

            // Animation complete
            if (fk_time_ms > clip.duration_ms) {
                printf("\n[COMPLETE] %s\n", clips[current_clip].label);

                // Print peak snapshot (moment of max forward displacement)
                printf("  --- PEAK (max forward) ---\n");
                peak_snapshots[current_clip].print("PEAK");
                auto& pk = peak_snapshots[current_clip];

                // Active endpoint forward assertion (facing south → forward = -Y)
                // At peak, the active limb should be AHEAD of hips
                float active_peak_dy;
                const char* limb_name;
                if (clips[current_clip].is_kick) {
                    if (clips[current_clip].side == Side::RIGHT) {
                        active_peak_dy = pk.r_foot_y - pk.hips_y;
                        limb_name = "R_Foot";
                    } else {
                        active_peak_dy = pk.l_foot_y - pk.hips_y;
                        limb_name = "L_Foot";
                    }
                } else {
                    if (clips[current_clip].side == Side::RIGHT) {
                        active_peak_dy = pk.r_hand_y - pk.hips_y;
                        limb_name = "R_Hand";
                    } else {
                        active_peak_dy = pk.l_hand_y - pk.hips_y;
                        limb_name = "L_Hand";
                    }
                }

                // Forward = negative Y. Active limb should be at least 0.1m ahead of hips.
                bool forward_ok = active_peak_dy < -0.10f;
                printf("[ASSERT] %s forward at peak: dy=%+.4f %s\n",
                       limb_name, active_peak_dy, forward_ok ? "PASS" : "FAIL");
                if (!forward_ok) assertion_failures++;

                // Print end-of-animation snapshot (recovery pose)
                auto end_snap = take_snapshot();
                printf("  --- END (recovery) ---\n");
                end_snap.print("END");

                // Connectivity
                auto val = validate_humanoid(ps, physics, h);
                float max_err = val.max_pivot_error;
                bool conn_ok = val.all_connected();
                printf("[ASSERT] Connectivity: max_err=%.4f %s\n",
                       max_err, conn_ok ? "PASS" : "FAIL");
                if (!conn_ok) assertion_failures++;

                // Spine neutral check on last keyframe
                if (!clip.keyframes.empty()) {
                    bool spine_ok = true;
                    for (const auto& tgt : clip.keyframes.back().pose.targets) {
                        if ((tgt.joint_name == "lower_spine" || tgt.joint_name == "upper_spine") &&
                            tgt.semantic == SemanticChannel::TWIST &&
                            std::abs(tgt.angle) > 0.01f) {
                            spine_ok = false;
                        }
                    }
                    printf("[ASSERT] Spine neutral at end: %s\n", spine_ok ? "PASS" : "FAIL");
                    if (!spine_ok) assertion_failures++;
                }

                fk_playing = false;
                fk_time_ms = 0.0f;

                // Cycle to next
                current_clip = (current_clip + 1) % clips.size();
                std::cout << "[FK] Next: " << clips[current_clip].label << " (press SPACE)" << std::endl;

                // Headless: exit after cycling through all
                if (headless && current_clip == 0) {
                    std::cout << "\n[HEADLESS] All clips tested, exiting" << std::endl;
                    break;
                }
            }
        }

        // Render
        engine.render();

        // On-screen UI
        auto& rs = engine.get_draw_surface();
        char buf[128];

        snprintf(buf, sizeof(buf), "%zu: %s", current_clip, clips[current_clip].label);
        rs.draw_text(20, 30, buf, 255, 255, 255);

        const char* side_str = (clips[current_clip].side == Side::RIGHT) ? "RIGHT" : "LEFT";
        uint8_t side_r = (clips[current_clip].side == Side::RIGHT) ? 100 : 255;
        uint8_t side_g = 200;
        uint8_t side_b = (clips[current_clip].side == Side::LEFT) ? 100 : 255;
        snprintf(buf, sizeof(buf), "Side: %s", side_str);
        rs.draw_text(20, 50, buf, side_r, side_g, side_b);

        if (fk_playing) {
            snprintf(buf, sizeof(buf), "PLAYING  %.0f / %.0fms",
                     fk_time_ms, clips[current_clip].clip.duration_ms);
            rs.draw_text(20, 70, buf, 255, 255, 0);
        } else {
            rs.draw_text(20, 70, "READY (SPACE)", 200, 200, 200);
        }

        snprintf(buf, sizeof(buf), "Assertions failed: %d", assertion_failures);
        rs.draw_text(20, 95, buf, assertion_failures > 0 ? (uint8_t)255 : (uint8_t)100,
                     assertion_failures > 0 ? (uint8_t)80 : (uint8_t)255, 100);

        rs.draw_text(20, 920, "BILATERAL: BodyMap side parameterization test", 140, 140, 140);

        engine.present();
    }

    // Mirror symmetry assertions (compare paired clips)
    // Pairs: 0/1 (kick 0.5), 2/3 (punch 0.5), 4/5 (kick 1.0)
    auto check_mirror = [&](size_t right_idx, size_t left_idx, const char* pair_name) {
        auto& rp = peak_snapshots[right_idx];
        auto& lp = peak_snapshots[left_idx];

        bool is_kick = clips[right_idx].is_kick;

        // Get active limb offsets relative to hips
        float r_dy, l_dy, r_dx, l_dx, r_dz, l_dz;
        if (is_kick) {
            r_dy = rp.r_foot_y - rp.hips_y;  l_dy = lp.l_foot_y - lp.hips_y;
            r_dx = rp.r_foot_x - rp.hips_x;  l_dx = lp.l_foot_x - lp.hips_x;
            r_dz = rp.r_foot_z - rp.hips_z;  l_dz = lp.l_foot_z - lp.hips_z;
        } else {
            r_dy = rp.r_hand_y - rp.hips_y;  l_dy = lp.l_hand_y - lp.hips_y;
            r_dx = rp.r_hand_x - rp.hips_x;  l_dx = lp.l_hand_x - lp.hips_x;
            r_dz = rp.r_hand_z - rp.hips_z;  l_dz = lp.l_hand_z - lp.hips_z;
        }

        printf("\n[MIRROR] %s\n", pair_name);
        printf("  Right active: dx=%+.4f dy=%+.4f dz=%+.4f\n", r_dx, r_dy, r_dz);
        printf("  Left  active: dx=%+.4f dy=%+.4f dz=%+.4f\n", l_dx, l_dy, l_dz);

        // Y (forward) should match within tolerance
        float y_diff = std::abs(r_dy - l_dy);
        bool y_ok = y_diff < 0.15f;
        printf("  [ASSERT] Forward symmetry |R_dy - L_dy| = %.4f %s\n",
               y_diff, y_ok ? "PASS" : "FAIL");
        if (!y_ok) assertion_failures++;

        // X should be mirrored (opposite sign, similar magnitude)
        float x_sum = std::abs(r_dx + l_dx);  // if mirrored, r_dx + l_dx ≈ 0
        bool x_ok = x_sum < 0.15f;
        printf("  [ASSERT] Lateral mirror |R_dx + L_dx| = %.4f %s\n",
               x_sum, x_ok ? "PASS" : "FAIL");
        if (!x_ok) assertion_failures++;

        // Z (height) should match
        float z_diff = std::abs(r_dz - l_dz);
        bool z_ok = z_diff < 0.15f;
        printf("  [ASSERT] Height symmetry |R_dz - L_dz| = %.4f %s\n",
               z_diff, z_ok ? "PASS" : "FAIL");
        if (!z_ok) assertion_failures++;
    };

    check_mirror(0, 1, "Kick skill 0.5");
    check_mirror(2, 3, "Punch skill 0.5");
    check_mirror(4, 5, "Kick skill 1.0");

    // Final summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  BILATERAL TEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;

    if (assertion_failures > 0) {
        std::cerr << "  *** " << assertion_failures << " ASSERTION FAILURES ***" << std::endl;
        return 1;
    }
    std::cout << "  ALL ASSERTIONS PASSED" << std::endl;
    return 0;
}
