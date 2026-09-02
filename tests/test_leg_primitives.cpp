// Leg Animation Primitives Test (FK Version)
//
// Tests FK-based leg animations: hip flexion, kick prep.
// Segment distances should stay constant (no stretching).
//
// Controls:
//   SPACE = cycle through animations
//   ESC = exit

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles
#include <cstdlib>
#include "core/force.h"
#include "humanoid_validator.h"
#include <string>

// The floor these scenes build is 0.1 m thick with its bottom on the
// turtle, so its walking surface is 0.10 m. world_z for a humanoid is
// the FEET'S BOTTOM: spawning at 0.0 buried them 80 mm in the slab,
// which the creation door refuses (INV-37).
static constexpr float FLOOR_TOP = 0.10f;

int main(int, char**) {
    // Headless by default. This is an interactive FK demo: the main loop is
    // driven by GLFW input (SPACE cycles clips, ESC exits) against a live
    // window, so without a window it would wait on input forever.
    // Window only when INTERACTIVE=1 is set.
    if (std::getenv("INTERACTIVE") == nullptr) {
        std::cout << "SKIPPED (headless): interactive FK demo; "
                  << "run with INTERACTIVE=1 for the window" << std::endl;
        return 0;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  FK LEG ANIMATION TEST" << std::endl;
    std::cout << "  Joint rotations drive positions" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "  SPACE = cycle animations" << std::endl;
    std::cout << "  ESC = exit" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Engine setup
    Engine engine;
    EngineConfig config;
    config.mode = EngineMode::Interactive;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "FK Leg Animation Test";
    config.show_debug_overlay = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "Engine init failed" << std::endl;
        return 1;
    }

    auto& ps = engine.get_particle_system();
    auto& dynamics = engine.get_dynamics_system();
    auto& input = engine.get_input_system();
    auto& physics = engine.get_physics_system();

    // Add gravity (required for PHYSICS mode joints to hang correctly)
    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // Simple floor
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
    floor.shape = ParticleShape::BOX;
    floor.width = 10.0f; floor.height = 10.0f; floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    engine.add_particle(floor);

    // Multiple lights for better visibility
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

    add_light(0.0f, 0.0f, 10.0f, 600000.0f);   // Overhead
    add_light(0.0f, -4.0f, 3.0f, 400000.0f);   // Front (camera side)
    add_light(3.0f, 0.0f, 3.0f, 300000.0f);    // Right side
    add_light(-3.0f, 0.0f, 3.0f, 300000.0f);   // Left side

    // Create humanoid
    auto& kg = engine.get_kg();
    HumanoidGenerator humanoid_gen;
    humanoid_gen.initialize(&engine, &kg);

    HumanoidSpec spec = HumanoidSpec::hunter();
    // world_z is the feet's BOTTOM; the floor's top is 0.10 (INV-37).
    auto h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, FLOOR_TOP, -1, spec, false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        h.hips_id,
        h.left_leg_ids, h.right_leg_ids,
        h.left_arm_ids, h.right_arm_ids,
        h.torso_ids,
        150.0f, 600.0f,
        h.entity_id
    );

    // Register joints for FK (arms + legs)
    h.register_joints(&engine.get_humanoid_locomotion());

    engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
    engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);

    // Face SOUTH (toward camera)
    constexpr float FACING_S = static_cast<float>(M_PI);
    engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);

    // Baseline connectivity check (before any animation)
    {
        auto baseline = validate_humanoid(ps, physics, h);
        printf("\n[BASELINE CONNECTIVITY]\n");
        baseline.print_report();
    }

    // Create FK animation clips - leg only + skill-parameterized kicks
    std::vector<FKAnimationClip> clips;
    clips.push_back(create_fk_hip_flexion_right());       // Index 0: hip X-axis flexion (knee raise)
    clips.push_back(create_semantic_kick_prep_right());    // Index 1: full kick prep (hip + knee + snap)

    // Skill-parameterized front kicks (continuous 0.0-1.0)
    struct SkillKickInfo {
        float skill;
        bool from_stance;
        FrontKickProfile profile;
        const char* stage_name;
        const char* observe_hint;
    };
    std::vector<SkillKickInfo> skill_clips;
    auto add_skill_kick = [&](float skill, bool from_stance, const char* stage, const char* hint) {
        FrontKickProfile profile = get_interpolated_kick_profile(skill);
        clips.push_back(create_fk_front_kick_right(skill, from_stance));
        skill_clips.push_back({skill, from_stance, profile, stage, hint});
    };
    size_t first_skill_index = clips.size();  // Index where skill kicks start
    add_skill_kick(0.0f, false, "Cognitive",         "Low chamber, can't extend, foot drops, wide recovery");
    add_skill_kick(0.3f, true,  "Early Associative",  "Better chamber, some hip drive, beginning rechamber");
    add_skill_kick(0.5f, true,  "Mid Associative",    "Interpolated - between 0.3 and 0.7");
    add_skill_kick(0.7f, true,  "Late Associative",   "High chamber, tight fold, good hip drive, clean rechamber");
    add_skill_kick(1.0f, true,  "Autonomous",         "Max chamber, heel-to-glute, full chain, ball-of-foot strike");

    std::cout << "[FK] Leg animation sequence:" << std::endl;
    for (size_t i = 0; i < clips.size(); ++i) {
        std::cout << "  " << i << ": " << clips[i].name << " (" << clips[i].duration_ms << "ms)" << std::endl;
    }

    // FK animation state - TEST_CASE env var selects which test (0-6), default 0
    size_t current_clip_index = 0;
    const char* test_env = std::getenv("TEST_CASE");
    bool test_case_specified = (test_env != nullptr);
    if (test_env) {
        int val = std::atoi(test_env);
        if (val >= 0 && val < static_cast<int>(clips.size())) {
            current_clip_index = static_cast<size_t>(val);
        }
    }
    std::cout << "[CONFIG] Running test case " << current_clip_index << " (set TEST_CASE=0-6 to change)" << std::endl;
    bool fk_playing = false;
    float fk_time_ms = 0.0f;
    int assertion_failures = 0;

    // Get leg particle IDs for diagnostics
    int thigh_id = h.right_leg_ids.size() >= 3 ? h.right_leg_ids[2] : -1;
    int shin_id = h.right_leg_ids.size() >= 2 ? h.right_leg_ids[1] : -1;
    int foot_id = h.right_leg_ids.size() >= 1 ? h.right_leg_ids[0] : -1;

    std::cout << "[LEG IDs] thigh=" << thigh_id
              << " shin=" << shin_id
              << " foot=" << foot_id
              << " hips=" << h.hips_id << std::endl;

    auto calculate_distance = [](float ax, float ay, float az, float bx, float by, float bz) {
        return std::sqrt(
            (bx - ax) * (bx - ax) +
            (by - ay) * (by - ay) +
            (bz - az) * (bz - az)
        );
    };

    // Camera
    auto& camera = engine.get_camera_system();
    camera.set_position(-6.0f, -6.0f, 6.0f);
    camera.look_at(0.0f, 0.0f, 1.0f);
    camera.set_pixels_per_unit(80.0f);

    // Input state
    bool space_was_pressed = false;
    int frame_count = 0;
    bool auto_started = false;

    std::cout << "\n[READY] Press SPACE to play: " << clips[current_clip_index].name << "\n" << std::endl;
    std::cout << "[AUTO] Will auto-start first animation after 60 frames (~0.5 sec)" << std::endl;

    // Main loop
    const double dt = 1.0 / 60.0;
    const float animation_speed = 0.5f;  // Half speed for easier observation
    const float dt_ms = static_cast<float>(dt * 1000.0) * animation_speed;

    // Humanoid connectivity validation interval
    int validation_check_interval = 10;

    while (true) {
        engine.get_platform()->poll_events();
        frame_count++;

        const auto& input_state = input.get_input_state();
        if (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) {
            break;
        }

        // Auto-start animation after 60 frames ONLY when TEST_CASE is explicitly set
        bool auto_trigger = (test_case_specified && !auto_started && !fk_playing && frame_count >= 60);
        if (auto_trigger) {
            auto_started = true;
            std::cout << "[AUTO] Auto-starting animation test " << current_clip_index << std::endl;
        }

        // SPACE = start current animation (cycles after completion)
        bool space_pressed = input_state.keys[GLFW_KEY_SPACE] || auto_trigger;
        if (space_pressed && !space_was_pressed && !fk_playing) {
            {
                auto view = ps.lock_particles_for_read();
                std::cout << "\n===== LEG TEST " << current_clip_index << " STARTING: " << clips[current_clip_index].name << " =====" << std::endl;
                if (thigh_id >= 0) {
                    std::cout << "[INITIAL_STATE] thigh[" << thigh_id << "]: pos=(" << view[thigh_id].x << "," << view[thigh_id].y << "," << view[thigh_id].z << ")"
                              << " rot=(" << view[thigh_id].rotation_x << "," << view[thigh_id].rotation_y << "," << view[thigh_id].rotation_z << ")" << std::endl;
                }
                if (shin_id >= 0) {
                    std::cout << "[INITIAL_STATE] shin[" << shin_id << "]: pos=(" << view[shin_id].x << "," << view[shin_id].y << "," << view[shin_id].z << ")"
                              << " rot=(" << view[shin_id].rotation_x << "," << view[shin_id].rotation_y << "," << view[shin_id].rotation_z << ")" << std::endl;
                }
                if (foot_id >= 0) {
                    std::cout << "[INITIAL_STATE] foot[" << foot_id << "]: pos=(" << view[foot_id].x << "," << view[foot_id].y << "," << view[foot_id].z << ")"
                              << " rot=(" << view[foot_id].rotation_x << "," << view[foot_id].rotation_y << "," << view[foot_id].rotation_z << ")" << std::endl;
                }
                std::cout << "============================================================" << std::endl;
            }
            fk_playing = true;
            fk_time_ms = 0.0f;
            std::cout << "\n[FK PLAY] Starting: " << clips[current_clip_index].name << std::endl;
        }
        space_was_pressed = space_pressed;

        // Update engine FIRST (physics positions particles)
        engine.update(dt);

        // Apply FK AFTER physics to override positions
        if (fk_playing) {
            FKAnimationClip& clip = clips[current_clip_index];

            // Advance time
            fk_time_ms += dt_ms;
            float query_time = std::min(fk_time_ms, clip.duration_ms);

            // Interpolate joint targets
            RotationPose pose;
            if (clip.get_pose_at_time(query_time, pose)) {
                // === JOINT TARGET INSTRUMENTATION ===
                // Log every target's type, axis, and angle (once per keyframe boundary)
                static float last_target_log = -100.0f;
                bool at_boundary = (std::abs(query_time - 0.0f) < 20.0f ||
                                    std::abs(query_time - 300.0f) < 20.0f ||
                                    std::abs(query_time - 450.0f) < 20.0f ||
                                    std::abs(query_time - 600.0f) < 20.0f ||
                                    std::abs(query_time - 700.0f) < 20.0f);
                if (at_boundary && std::abs(query_time - last_target_log) > 30.0f) {
                    last_target_log = query_time;
                    auto type_str = [](JointTargetType t) {
                        switch(t) {
                            case JointTargetType::DRIVEN: return "DRIVEN";
                            case JointTargetType::DIRECTION: return "DIRECTION";
                            case JointTargetType::INHERIT: return "INHERIT";
                            case JointTargetType::PHYSICS: return "PHYSICS";
                            case JointTargetType::SEMANTIC: return "SEMANTIC";
                        }
                        return "?";
                    };
                    auto axis_str = [](RotationAxis a) {
                        switch(a) { case RotationAxis::X: return "X"; case RotationAxis::Y: return "Y"; case RotationAxis::Z: return "Z"; }
                        return "?";
                    };
                    auto sem_str = [](SemanticChannel s) {
                        switch(s) { case SemanticChannel::FLEX: return "FLEX"; case SemanticChannel::ABDUCT: return "ABDUCT"; case SemanticChannel::TWIST: return "TWIST"; }
                        return "?";
                    };
                    printf("\n[JOINT_TARGETS] t=%.0fms clip=%s\n", query_time, clip.name.c_str());
                    for (const auto& target : pose.targets) {
                        if (target.type == JointTargetType::SEMANTIC) {
                            printf("  %s: type=%s semantic=%s angle=%.4f\n",
                                   target.joint_name.c_str(), type_str(target.type),
                                   sem_str(target.semantic), target.angle);
                        } else {
                            printf("  %s: type=%s axis=%s angle=%.4f\n",
                                   target.joint_name.c_str(), type_str(target.type),
                                   axis_str(target.axis), target.angle);
                        }
                    }
                }

                // Set joint targets based on type
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
                        case JointTargetType::DIRECTION:
                            engine.get_humanoid_locomotion().set_joint_target(h.entity_id, target.joint_name,
                                ParticleDynamicsSystem::JointMode::DIRECTION, 0.0f,
                                {target.direction.x, target.direction.y, target.direction.z});
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
                    }
                }

                // Apply FK to compute positions
                engine.get_humanoid_locomotion().apply_entity_fk(h.entity_id);

                // === POST-FK POSITION INSTRUMENTATION ===
                // Log thigh XYZ + fk_rotation every 6 frames to track direction of motion
                static int pos_log_count = 0;
                if (pos_log_count++ % 6 == 0 && thigh_id >= 0) {
                    auto view = ps.lock_particles_for_read();
                    printf("[LEG_FK] t=%.0fms thigh pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f) fk_rot=(%.3f,%.3f,%.3f)\n",
                           query_time,
                           view[thigh_id].x, view[thigh_id].y, view[thigh_id].z,
                           view[thigh_id].rotation_x, view[thigh_id].rotation_y, view[thigh_id].rotation_z,
                           view[thigh_id].fk_rotation_x, view[thigh_id].fk_rotation_y, view[thigh_id].fk_rotation_z);
                    if (shin_id >= 0) {
                        printf("[LEG_FK] t=%.0fms shin  pos=(%.3f,%.3f,%.3f)\n",
                               query_time, view[shin_id].x, view[shin_id].y, view[shin_id].z);
                    }
                    // Log hips for reference (where the leg attaches)
                    printf("[LEG_FK] t=%.0fms hips  pos=(%.3f,%.3f,%.3f)\n",
                           query_time, view[h.hips_id].x, view[h.hips_id].y, view[h.hips_id].z);
                }
            }

            // Check for animation completion
            if (fk_time_ms > clip.duration_ms) {
                size_t completed_index = current_clip_index;
                {
                    auto view = ps.lock_particles_for_read();
                    std::cout << "\n========== AFTER COMPLETING LEG TEST " << completed_index << ": " << clip.name << " ==========" << std::endl;
                    if (thigh_id >= 0) {
                        std::cout << "  thigh[" << thigh_id << "]: pos=(" << view[thigh_id].x << "," << view[thigh_id].y << "," << view[thigh_id].z << ")"
                                  << " rot=(" << view[thigh_id].rotation_x << "," << view[thigh_id].rotation_y << "," << view[thigh_id].rotation_z << ")" << std::endl;
                    }
                    if (shin_id >= 0) {
                        std::cout << "  shin[" << shin_id << "]: pos=(" << view[shin_id].x << "," << view[shin_id].y << "," << view[shin_id].z << ")"
                                  << " rot=(" << view[shin_id].rotation_x << "," << view[shin_id].rotation_y << "," << view[shin_id].rotation_z << ")" << std::endl;
                    }
                    if (foot_id >= 0) {
                        std::cout << "  foot[" << foot_id << "]: pos=(" << view[foot_id].x << "," << view[foot_id].y << "," << view[foot_id].z << ")"
                                  << " rot=(" << view[foot_id].rotation_x << "," << view[foot_id].rotation_y << "," << view[foot_id].rotation_z << ")" << std::endl;
                    }
                    std::cout << "============================================================" << std::endl;
                }

                // Connectivity assertion: right leg chain intact
                {
                    auto validation = validate_humanoid(ps, physics, h);
                    printf("\n[LEG CONNECTIVITY]\n");
                    validation.print_report();
                    float leg_err = validation.chain_max_error("r_");
                    printf("[LEG-CONN] Chain error — r_leg max_err=%.4f %s\n", leg_err,
                           leg_err < 0.05f ? "PASS" : "FAIL");
                    if (leg_err > 0.05f) {
                        std::cerr << "[ASSERT FAIL LEG-CONN] Right leg chain disconnected!"
                                  << " max_pivot_err=" << leg_err << std::endl;
                        assertion_failures++;
                    }
                }

                // === SKILL KICK ASSERTIONS (tests 2-6) ===
                bool completed_is_skill_kick = (clip.name.find("front_kick_skill_") != std::string::npos);
                if (completed_is_skill_kick && completed_index >= first_skill_index) {
                    size_t skill_idx = completed_index - first_skill_index;
                    const auto& info = skill_clips[skill_idx];

                    printf("\n[SKILL KICK ANALYSIS] skill=%.1f stage=%s from_stance=%s duration=%.0fms\n",
                           info.skill, info.stage_name, info.from_stance ? "YES" : "NO", clip.duration_ms);

                    // KICK-A: Chain connectivity (already checked above via LEG-CONN)
                    // Re-check arm connectivity too
                    {
                        auto validation = validate_humanoid(ps, physics, h);
                        float leg_err = validation.chain_max_error("r_");
                        printf("[KICK-A] Chain connectivity — r_leg max_err=%.4f %s\n",
                               leg_err, leg_err < 0.05f ? "PASS" : "FAIL");
                        // Already counted in LEG-CONN if failed
                    }

                    // KICK-B: Spine neutral at end
                    if (!clip.keyframes.empty()) {
                        const auto& last_kf = clip.keyframes.back();
                        bool spine_neutral = true;
                        for (const auto& tgt : last_kf.pose.targets) {
                            if ((tgt.joint_name == "lower_spine" || tgt.joint_name == "upper_spine") &&
                                tgt.semantic == SemanticChannel::TWIST &&
                                std::abs(tgt.angle) > 0.01f) {
                                spine_neutral = false;
                            }
                        }
                        printf("[KICK-B] Spine neutral at end — %s\n",
                               spine_neutral ? "PASS" : "FAIL");
                        if (!spine_neutral) {
                            std::cerr << "[ASSERT FAIL KICK-B] Skill " << info.skill
                                      << ": spine not neutral at end!" << std::endl;
                            assertion_failures++;
                        }
                    }

                    // KICK-C: Stance end-pose (from_stance clips end with non-zero hip_flex)
                    if (info.from_stance && !clip.keyframes.empty()) {
                        const auto& last_kf = clip.keyframes.back();
                        bool has_hip_angle = false;
                        for (const auto& tgt : last_kf.pose.targets) {
                            if (tgt.joint_name == "right_hip" &&
                                tgt.semantic == SemanticChannel::FLEX &&
                                std::abs(tgt.angle) > 0.01f) {
                                has_hip_angle = true;
                                break;
                            }
                        }
                        printf("[KICK-C] Stance end-pose — has_hip_angle=%s %s\n",
                               has_hip_angle ? "YES" : "NO",
                               has_hip_angle ? "PASS" : "FAIL");
                        if (!has_hip_angle) {
                            std::cerr << "[ASSERT FAIL KICK-C] Skill " << info.skill
                                      << ": from_stance clip ends at neutral, not stance!" << std::endl;
                            assertion_failures++;
                        }
                    }

                    // KICK-D: Duration monotonicity (higher skill = shorter or equal)
                    if (skill_idx > 0) {
                        float prev_duration = clips[first_skill_index + skill_idx - 1].duration_ms;
                        float this_duration = clip.duration_ms;
                        bool monotonic = this_duration <= prev_duration + 50.0f;
                        printf("[KICK-D] Duration monotonicity — prev=%.0fms this=%.0fms %s\n",
                               prev_duration, this_duration, monotonic ? "PASS" : "FAIL");
                        if (!monotonic) {
                            std::cerr << "[ASSERT FAIL KICK-D] Skill " << info.skill
                                      << ": slower than lower skill! this=" << this_duration
                                      << "ms prev=" << prev_duration << "ms" << std::endl;
                            assertion_failures++;
                        }
                    }

                    // KICK-E: Chamber+snap timing (informational)
                    float chamber_snap_ms = info.profile.chamber_ms + info.profile.snap_ms;
                    printf("[KICK-E] Chamber+snap timing — %.0fms (chamber=%.0f snap=%.0f)\n",
                           chamber_snap_ms, info.profile.chamber_ms, info.profile.snap_ms);
                }

                fk_playing = false;
                fk_time_ms = 0.0f;
                std::cout << "[FK] Animation complete: " << clip.name << std::endl;

                // When TEST_CASE specified: exit after that single test
                if (test_case_specified && auto_started) {
                    std::cout << "[AUTO] Test " << completed_index << " complete, exiting" << std::endl;
                    break;
                }

                // Cycle to next animation
                current_clip_index = (current_clip_index + 1) % clips.size();
                std::cout << "[FK] Next animation: " << clips[current_clip_index].name << " (press SPACE)" << std::endl;
            }

            // Leg distance diagnostics (every frame during playback)
            if (thigh_id >= 0 && shin_id >= 0 && foot_id >= 0) {
                auto view = ps.lock_particles_for_read();
                float d_hips_thigh = calculate_distance(
                    view[h.hips_id].x, view[h.hips_id].y, view[h.hips_id].z,
                    view[thigh_id].x, view[thigh_id].y, view[thigh_id].z);
                float d_thigh_shin = calculate_distance(
                    view[thigh_id].x, view[thigh_id].y, view[thigh_id].z,
                    view[shin_id].x, view[shin_id].y, view[shin_id].z);
                float d_shin_foot = calculate_distance(
                    view[shin_id].x, view[shin_id].y, view[shin_id].z,
                    view[foot_id].x, view[foot_id].y, view[foot_id].z);

                static int leg_log = 0;
                if (leg_log++ % 30 == 0) {
                    std::cout << "[FK t=" << static_cast<int>(fk_time_ms) << "ms]"
                              << " hips->thigh=" << d_hips_thigh
                              << " thigh->shin=" << d_thigh_shin
                              << " shin->foot=" << d_shin_foot << std::endl;
                }
            }

            // Pivot coincidence check (every N frames)
            if (frame_count % validation_check_interval == 0 && fk_playing) {
                auto validation = validate_humanoid(ps, physics, h);
                if (!validation.all_connected()) {
                    printf("[HUMANOID_DRIFT] frame=%d\n", frame_count);
                    validation.print_report();
                }
            }
        }

        // Render
        engine.render();

        // Draw on-screen test info
        auto& rs = engine.get_draw_surface();
        std::string status = fk_playing ? "PLAYING" : "READY (SPACE)";
        std::string info = std::to_string(current_clip_index) + ": " + clips[current_clip_index].name;
        rs.draw_text(20, 30, info.c_str(), 255, 255, 255);
        rs.draw_text(20, 50, status.c_str(), 255, 255, 0);

        // Extended UI for skill-parameterized kick clips
        if (current_clip_index >= first_skill_index &&
            current_clip_index < first_skill_index + skill_clips.size()) {
            size_t si = current_clip_index - first_skill_index;
            const auto& sci = skill_clips[si];

            char buf[128];
            snprintf(buf, sizeof(buf), "Skill: %.1f  Stage: %s", sci.skill, sci.stage_name);
            rs.draw_text(20, 75, buf, 180, 220, 255);

            snprintf(buf, sizeof(buf), "Stance: %s", sci.from_stance ? "FROM STANCE" : "FROM NEUTRAL");
            rs.draw_text(20, 95, buf, 180, 220, 255);

            snprintf(buf, sizeof(buf), "Chamber: %.0fms  Snap: %.0fms  Total: %.0fms",
                     sci.profile.chamber_ms, sci.profile.snap_ms, clips[current_clip_index].duration_ms);
            rs.draw_text(20, 120, buf, 200, 200, 200);

            snprintf(buf, sizeof(buf), "Hip-leg gap: %.0fms  Spine: %.1fdeg  Recoil: %.0fms",
                     sci.profile.hip_leg_gap_ms,
                     sci.profile.spine_lower_twist * 180.0f / static_cast<float>(M_PI),
                     sci.profile.recoil_ms);
            rs.draw_text(20, 140, buf, 200, 200, 200);

            snprintf(buf, sizeof(buf), "Chamber hip: %.0fdeg  Knee: %.0fdeg  Ankle: %.1f",
                     sci.profile.chamber_hip_flex * 180.0f / static_cast<float>(M_PI),
                     sci.profile.chamber_knee_flex * 180.0f / static_cast<float>(M_PI),
                     sci.profile.snap_ankle_flex);
            rs.draw_text(20, 160, buf, 200, 200, 200);

            snprintf(buf, sizeof(buf), "Look for: %s", sci.observe_hint);
            rs.draw_text(20, 185, buf, 255, 220, 100);

            // Current phase indicator
            if (fk_playing) {
                const char* phase = "IDLE";
                float t_acc = 0.0f;
                t_acc += sci.profile.stance_settle_ms;
                if (fk_time_ms <= t_acc) { phase = sci.profile.stance_settle_ms > 0 ? "STANCE SETTLE" : "START"; }
                else {
                    float eff_chamber = sci.profile.chamber_ms;
                    if (!sci.from_stance && sci.skill < 0.3f) eff_chamber *= 1.2f;
                    t_acc += eff_chamber;
                    if (fk_time_ms <= t_acc) { phase = "CHAMBER"; }
                    else {
                        if (sci.profile.hip_leg_gap_ms > 0.5f) {
                            t_acc += sci.profile.hip_leg_gap_ms;
                            if (fk_time_ms <= t_acc) { phase = "HIP FIRES"; }
                        }
                        if (fk_time_ms > t_acc) {
                            t_acc += sci.profile.snap_ms;
                            if (fk_time_ms <= t_acc) { phase = ">>> SNAP <<<"; }
                            else {
                                t_acc += sci.profile.hold_ms;
                                if (fk_time_ms <= t_acc) { phase = "HOLD"; }
                                else {
                                    if (sci.profile.recoil_ms > 0.5f) {
                                        t_acc += sci.profile.recoil_ms;
                                        if (fk_time_ms <= t_acc) { phase = "RECOIL"; }
                                    }
                                    if (fk_time_ms > t_acc) {
                                        t_acc += sci.profile.recovery_ms;
                                        if (fk_time_ms <= t_acc) { phase = "RECOVERY"; }
                                        else { phase = "STANCE HOLD"; }
                                    }
                                }
                            }
                        }
                    }
                }
                snprintf(buf, sizeof(buf), "Phase: %s  (%.0f / %.0fms)",
                         phase, fk_time_ms, clips[current_clip_index].duration_ms);
                bool is_snap_phase = (std::string(phase).find("SNAP") != std::string::npos);
                rs.draw_text(20, 210, buf,
                             is_snap_phase ? (uint8_t)255 : (uint8_t)100,
                             is_snap_phase ? (uint8_t)80 : (uint8_t)255,
                             is_snap_phase ? (uint8_t)80 : (uint8_t)100);
            }

            rs.draw_text(20, 240, "Assertions: connectivity, spine neutral,", 140, 140, 140);
            rs.draw_text(20, 258, "stance end-pose, duration monotonicity", 140, 140, 140);
        }

        engine.present();
    }

    std::cout << "\n=== LEG TEST COMPLETE ===" << std::endl;

    if (assertion_failures > 0) {
        std::cerr << "\n*** " << assertion_failures << " LEG FK ASSERTION FAILURES ***" << std::endl;
        return 1;
    } else {
        std::cout << "\n*** ALL LEG FK ASSERTIONS PASSED ***" << std::endl;
    }

    return 0;
}
