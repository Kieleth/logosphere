// Walk Locomotion Test — Phase-Driven FK
//
// Validates phase-driven walk: actual hips velocity controls animation cadence.
// One full phase cycle (2π) = R step + L step. Each half-cycle (π) = one step clip.
// Physics → Animation: velocity controls phase rate → FK clip speed
// Animation → Physics: FK positions particles → gluon constraints propagate
//
// Controls:
//   SPACE = cycle through animations
//   ESC = exit
//
// Clips:
//   0: Right walk step    1: Left walk step
//   2: Walk forward (4 steps)  3: Walk forward (8 steps)  4: Walk circle
//
// Usage:
//   ./build/logomancers/test-walk-locomotion              # headless (auto)
//   ./build/logomancers/test-walk-locomotion --visual      # interactive window

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "core/force.h"
#include "humanoid_validator.h"
#include <iostream>
#include <cmath>
#include <vector>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

struct WalkClipInfo {
    FKAnimationClip clip;
    const char* label;
    Side starting_side;
    int chain_steps;        // 0 = single (existing), N = chain N alternating steps
    bool with_root_motion;  // true = apply velocity during playback
    bool with_turn;         // true = slowly rotate facing between steps
};

// Snapshot of foot positions relative to hips
struct FootSnapshot {
    float hips_x, hips_y, hips_z;
    float r_foot_x, r_foot_y, r_foot_z;
    float l_foot_x, l_foot_y, l_foot_z;

    void print(const char* label) const {
        printf("  [%s] Hips: (%.3f, %.3f, %.3f)\n", label, hips_x, hips_y, hips_z);
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
    std::cout << "  WALK LOCOMOTION TEST" << std::endl;
    std::cout << "  Phase-driven FK: velocity → cadence" << std::endl;
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
    config.window_title = "Walk Locomotion Test";
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
    floor.width = 20.0f; floor.height = 20.0f; floor.thickness = 0.1f;
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

    // Camera — pulled back further for locomotion traversal
    auto& camera = engine.get_camera_system();
    camera.set_position(-10.0f, -10.0f, 10.0f);
    camera.look_at(0.0f, -3.0f, 1.0f);
    camera.set_pixels_per_unit(60.0f);

    // Endpoint particle IDs
    int r_foot_id = h.right_leg_ids[0];
    int l_foot_id = h.left_leg_ids[0];

    auto take_snapshot = [&]() -> FootSnapshot {
        FootSnapshot s;
        auto hips = ps.get_particle_copy(h.hips_id);
        auto rf = ps.get_particle_copy(r_foot_id);
        auto lf = ps.get_particle_copy(l_foot_id);
        s.hips_x = hips.x; s.hips_y = hips.y; s.hips_z = hips.z;
        s.r_foot_x = rf.x; s.r_foot_y = rf.y; s.r_foot_z = rf.z;
        s.l_foot_x = lf.x; s.l_foot_y = lf.y; s.l_foot_z = lf.z;
        return s;
    };

    // Walk parameters derived from profile
    const auto& walk_profile = get_walk_reference_profile();
    const float step_duration_ms = walk_profile.swing_ms + walk_profile.contact_ms;  // one step clip = one half-cycle
    const float stride_length = walk_profile.stride_length;  // meters per step (half-stride)
    const float walk_speed = stride_length / (step_duration_ms / 1000.0f);
    const float turn_per_step = 0.15f;  // radians of facing change per step (circle mode)

    // Build clip list
    std::vector<WalkClipInfo> clips;
    clips.push_back({create_fk_walk_step(Side::RIGHT), "Right Walk Step",       Side::RIGHT, 0, false, false});
    clips.push_back({create_fk_walk_step(Side::LEFT),  "Left Walk Step",        Side::LEFT,  0, false, false});
    clips.push_back({create_fk_walk_step(Side::RIGHT), "Walk Forward (4 steps)", Side::RIGHT, 4, true,  false});
    clips.push_back({create_fk_walk_step(Side::RIGHT), "Walk Forward (8 steps)", Side::RIGHT, 8, true,  false});
    clips.push_back({create_fk_walk_step(Side::RIGHT), "Walk Circle",           Side::RIGHT, 8, true,  true});

    // Track peak forward displacement for single-step clips (0, 1)
    std::vector<FootSnapshot> peak_snapshots(clips.size());
    std::vector<float> peak_forward_y(clips.size(), 0.0f);

    std::cout << "[CLIPS] Animation sequence:" << std::endl;
    for (size_t i = 0; i < clips.size(); ++i) {
        std::cout << "  " << i << ": " << clips[i].label;
        if (clips[i].chain_steps > 0) {
            std::cout << " (chain=" << clips[i].chain_steps << " root_motion=" << clips[i].with_root_motion
                      << " turn=" << clips[i].with_turn << ")";
        }
        std::cout << " (" << clips[i].clip.duration_ms << "ms)" << std::endl;
    }

    // Animation state
    size_t current_clip = 0;
    bool fk_playing = false;
    int assertion_failures = 0;
    bool space_was_pressed = false;
    int frame_count = 0;
    int clip_start_frame = 0;

    // Headless frame budget per clip. Chained clips advance phase from the
    // hips' ACTUAL speed; if the walker never reaches 0.01 m/s the phase
    // never advances and the loop used to spin forever (killed at 4.3M
    // frames). 3600 frames = 60 s of sim, an order of magnitude above any
    // legitimate clip; hitting it is a FAILURE reported honestly, not a
    // hang.
    const int kClipFrameBudget = 3600;

    // Phase-driven walk state
    float walk_phase = 0.0f;         // [0, 2π) — one full cycle = R step + L step
    int completed_half_cycles = 0;   // counts finished steps
    Side phase_current_side = Side::RIGHT;
    float chain_start_x = 0.0f, chain_start_y = 0.0f;
    float chain_start_facing = FACING_S;
    float chain_current_facing = FACING_S;

    const double dt = 1.0 / 60.0;

    // Helper: apply FK pose to dynamics
    auto apply_fk_pose = [&](const FKAnimationClip& clip, float time_ms) {
        float query_time = std::min(time_ms, clip.duration_ms);
        RotationPose pose;
        if (!clip.get_pose_at_time(query_time, pose)) return;

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
    };

    // Helper: start root motion for a chain
    auto start_root_motion = [&]() {
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, true);
        // Forward = (sin(facing), cos(facing)). Facing π → (0, -1) = south
        float vx = walk_speed * std::sin(chain_current_facing);
        float vy = walk_speed * std::cos(chain_current_facing);
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, vx, vy);
    };

    // Helper: stop root motion
    auto stop_root_motion = [&]() {
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);
    };

    std::cout << "\n[READY] Press SPACE to play: " << clips[current_clip].label << "\n" << std::endl;

    while (true) {
        engine.get_platform()->poll_events();
        frame_count++;

        const auto& input_state = input.get_input_state();
        if (!headless && (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close())) {
            break;
        }

        // Headless: auto-start each animation when idle
        if (headless && !fk_playing && frame_count >= 2) {
            engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
            engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);
            chain_current_facing = FACING_S;

            auto& ci = clips[current_clip];
            walk_phase = 0.0f;
            completed_half_cycles = 0;
            phase_current_side = ci.starting_side;
            ci.clip = create_fk_walk_step(phase_current_side);

            if (ci.chain_steps > 0) {
                auto snap = take_snapshot();
                chain_start_x = snap.hips_x;
                chain_start_y = snap.hips_y;
                chain_start_facing = chain_current_facing;
                if (ci.with_root_motion) start_root_motion();
            }

            fk_playing = true;
            clip_start_frame = frame_count;
            std::cout << "\n[FK PLAY] Starting: " << ci.label << std::endl;
        }

        // SPACE starts current animation (visual mode)
        bool space_pressed = input_state.keys[GLFW_KEY_SPACE];
        if (space_pressed && !space_was_pressed && !fk_playing) {
            engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
            engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, FACING_S);
            chain_current_facing = FACING_S;

            auto& ci = clips[current_clip];
            walk_phase = 0.0f;
            completed_half_cycles = 0;
            phase_current_side = ci.starting_side;
            ci.clip = create_fk_walk_step(phase_current_side);

            if (ci.chain_steps > 0) {
                auto snap = take_snapshot();
                chain_start_x = snap.hips_x;
                chain_start_y = snap.hips_y;
                chain_start_facing = chain_current_facing;
                if (ci.with_root_motion) start_root_motion();
            }

            fk_playing = true;
            clip_start_frame = frame_count;
            std::cout << "\n[FK PLAY] Starting: " << ci.label << std::endl;
        }
        space_was_pressed = space_pressed;

        // Update engine (physics)
        engine.update(dt);

        // Phase-driven FK animation
        if (fk_playing) {
            auto& ci = clips[current_clip];
            FKAnimationClip& clip = ci.clip;

            // --- Phase accumulation from actual hips velocity ---
            auto hips = ps.get_particle_copy(h.hips_id);
            float actual_speed = std::sqrt(hips.vx * hips.vx + hips.vy * hips.vy);

            // --- Headless frame budget: a clip that cannot finish is a
            // FAILURE, reported with its stuck state, never a hang. ---
            if (headless && frame_count - clip_start_frame > kClipFrameBudget) {
                printf("\n[TIMEOUT] %s did not complete within %d frames: "
                       "phase=%.2f half_cycles=%d/%d speed=%.4f m/s FAIL\n",
                       ci.label, kClipFrameBudget, walk_phase,
                       completed_half_cycles, ci.chain_steps, actual_speed);
                assertion_failures++;
                if (ci.chain_steps > 0 && ci.with_root_motion) stop_root_motion();
                fk_playing = false;
                current_clip = (current_clip + 1) % clips.size();
                if (current_clip == 0) {
                    std::cout << "\n[HEADLESS] All clips tested, exiting" << std::endl;
                    break;
                }
                continue;
            }

            float d_phase = 0.0f;
            bool is_moving = false;

            if (ci.chain_steps > 0 && ci.with_root_motion) {
                // Chain with root motion: phase driven by actual velocity
                is_moving = actual_speed > 0.01f;
                if (is_moving) {
                    // stride_length is per-step (half-stride), full cycle (2π) = 2 steps
                    d_phase = (actual_speed / stride_length) * static_cast<float>(M_PI) * static_cast<float>(dt);
                }
            } else {
                // Single-step clips (no root motion): use fixed time advance
                // Phase runs [0, π) for one step clip
                d_phase = (static_cast<float>(M_PI) / (step_duration_ms / 1000.0f)) * static_cast<float>(dt);
                is_moving = true;  // always advancing for single clips
            }

            float prev_phase = walk_phase;
            walk_phase += d_phase;

            // --- Detect half-cycle boundary crossing (R↔L transition) ---
            int prev_half = static_cast<int>(prev_phase / static_cast<float>(M_PI));
            int curr_half = static_cast<int>(walk_phase / static_cast<float>(M_PI));

            if (curr_half > prev_half) {
                completed_half_cycles += (curr_half - prev_half);

                // Check if chain is done
                if (ci.chain_steps > 0 && completed_half_cycles >= ci.chain_steps) {
                    // --- Chain complete ---
                    printf("\n[COMPLETE] %s (%d steps)\n", ci.label, completed_half_cycles);
                    if (ci.with_root_motion) stop_root_motion();

                    auto end_snap = take_snapshot();
                    float dx = end_snap.hips_x - chain_start_x;
                    float dy = end_snap.hips_y - chain_start_y;
                    float displacement = std::sqrt(dx * dx + dy * dy);

                    printf("  Start: (%.3f, %.3f)  End: (%.3f, %.3f)\n",
                           chain_start_x, chain_start_y, end_snap.hips_x, end_snap.hips_y);
                    printf("  Displacement: %.3f m\n", displacement);

                    bool moved_ok = displacement > 0.5f;
                    printf("[ASSERT] Displacement > 0.5m: %.3f %s\n",
                           displacement, moved_ok ? "PASS" : "FAIL");
                    if (!moved_ok) assertion_failures++;

                    if (ci.with_turn) {
                        float facing_delta = std::abs(chain_current_facing - chain_start_facing);
                        printf("  Facing delta: %.3f rad\n", facing_delta);
                        bool turn_ok = facing_delta > 0.3f;
                        printf("[ASSERT] Facing changed > 0.3 rad: %.3f %s\n",
                               facing_delta, turn_ok ? "PASS" : "FAIL");
                        if (!turn_ok) assertion_failures++;
                    }

                    float tol = 0.05f;
                    auto val = validate_humanoid(ps, physics, h, tol);
                    printf("[ASSERT] Connectivity: max_err=%.4f %s\n",
                           val.max_pivot_error, val.all_connected() ? "PASS" : "FAIL");
                    if (!val.all_connected()) assertion_failures++;

                    fk_playing = false;
                    current_clip = (current_clip + 1) % clips.size();
                    std::cout << "[FK] Next: " << clips[current_clip].label << " (press SPACE)" << std::endl;
                    if (headless && current_clip == 0) {
                        std::cout << "\n[HEADLESS] All clips tested, exiting" << std::endl;
                        break;
                    }
                    continue;
                }

                // --- Half-cycle boundary: switch step side ---
                phase_current_side = opposite(phase_current_side);
                ci.clip = create_fk_walk_step(phase_current_side);

                if (ci.with_turn) {
                    chain_current_facing += turn_per_step;
                    engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, chain_current_facing);
                    if (ci.with_root_motion) {
                        float vx = walk_speed * std::sin(chain_current_facing);
                        float vy = walk_speed * std::cos(chain_current_facing);
                        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, vx, vy);
                    }
                }

                printf("[PHASE] half_cycle=%d side=%s phase=%.2f speed=%.3f\n",
                       completed_half_cycles,
                       phase_current_side == Side::RIGHT ? "R" : "L",
                       walk_phase, actual_speed);
            }

            // --- Single-step clip completion (no chain) ---
            if (ci.chain_steps == 0 && walk_phase >= static_cast<float>(M_PI)) {
                printf("\n[COMPLETE] %s\n", ci.label);

                printf("  --- PEAK (max forward) ---\n");
                peak_snapshots[current_clip].print("PEAK");
                auto& pk = peak_snapshots[current_clip];

                float active_peak_dy;
                const char* foot_name;
                if (ci.starting_side == Side::RIGHT) {
                    active_peak_dy = pk.r_foot_y - pk.hips_y;
                    foot_name = "R_Foot";
                } else {
                    active_peak_dy = pk.l_foot_y - pk.hips_y;
                    foot_name = "L_Foot";
                }

                bool forward_ok = active_peak_dy < -0.05f;
                printf("[ASSERT] %s forward at peak: dy=%+.4f %s\n",
                       foot_name, active_peak_dy, forward_ok ? "PASS" : "FAIL");
                if (!forward_ok) assertion_failures++;

                auto end_snap = take_snapshot();
                printf("  --- END (recovery) ---\n");
                end_snap.print("END");

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

                float tol = 0.02f;
                auto val = validate_humanoid(ps, physics, h, tol);
                printf("[ASSERT] Connectivity: max_err=%.4f %s\n",
                       val.max_pivot_error, val.all_connected() ? "PASS" : "FAIL");
                if (!val.all_connected()) assertion_failures++;

                fk_playing = false;
                current_clip = (current_clip + 1) % clips.size();
                std::cout << "[FK] Next: " << clips[current_clip].label << " (press SPACE)" << std::endl;
                if (headless && current_clip == 0) {
                    std::cout << "\n[HEADLESS] All clips tested, exiting" << std::endl;
                    break;
                }
                continue;
            }

            // --- Derive FK clip time from phase within current half-cycle ---
            if (is_moving) {
                float phase_in_half;
                if (ci.chain_steps > 0) {
                    // Chain: use modular phase within current half-cycle
                    phase_in_half = std::fmod(walk_phase, static_cast<float>(M_PI));
                } else {
                    // Single step: phase runs [0, π)
                    phase_in_half = walk_phase;
                }
                float clip_time = (phase_in_half / static_cast<float>(M_PI)) * step_duration_ms;
                apply_fk_pose(clip, clip_time);
            }
            // else: velocity ≈ 0, FK freezes (humanoid holds current pose)

            // Track peak forward displacement for single-step clips
            if (ci.chain_steps == 0) {
                auto snap = take_snapshot();
                float active_y = (ci.starting_side == Side::RIGHT)
                    ? snap.r_foot_y : snap.l_foot_y;
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
        }

        // Render — skipped in headless mode (no GPU available)
        if (headless) continue;

        engine.render();

        auto& rs = engine.get_draw_surface();
        char buf[128];

        snprintf(buf, sizeof(buf), "%zu: %s", current_clip, clips[current_clip].label);
        rs.draw_text(20, 30, buf, 255, 255, 255);

        const char* side_str = (phase_current_side == Side::RIGHT) ? "RIGHT" : "LEFT";
        uint8_t side_r = (phase_current_side == Side::RIGHT) ? 100 : 255;
        uint8_t side_b = (phase_current_side == Side::LEFT) ? 100 : 255;
        snprintf(buf, sizeof(buf), "Side: %s", side_str);
        rs.draw_text(20, 50, buf, side_r, 200, side_b);

        if (fk_playing) {
            float phase_deg = walk_phase * 180.0f / static_cast<float>(M_PI);
            auto hips_hud = ps.get_particle_copy(h.hips_id);
            float speed_hud = std::sqrt(hips_hud.vx * hips_hud.vx + hips_hud.vy * hips_hud.vy);
            snprintf(buf, sizeof(buf), "PLAYING  phase=%.0f deg  speed=%.2f m/s",
                     phase_deg, speed_hud);
            rs.draw_text(20, 70, buf, 255, 255, 0);

            if (clips[current_clip].chain_steps > 0) {
                snprintf(buf, sizeof(buf), "Chain: step %d/%d  side=%s",
                         completed_half_cycles + 1,
                         clips[current_clip].chain_steps,
                         phase_current_side == Side::RIGHT ? "R" : "L");
                rs.draw_text(20, 90, buf, 200, 200, 255);
            }
        } else {
            rs.draw_text(20, 70, "READY (SPACE)", 200, 200, 200);
        }

        snprintf(buf, sizeof(buf), "Assertions failed: %d", assertion_failures);
        rs.draw_text(20, 115, buf, assertion_failures > 0 ? (uint8_t)255 : (uint8_t)100,
                     assertion_failures > 0 ? (uint8_t)80 : (uint8_t)255, 100);

        rs.draw_text(20, 920, "WALK LOCOMOTION: Phase-driven FK (velocity -> cadence)", 140, 140, 140);

        engine.present();
    }

    // Mirror symmetry: compare right step vs left step (clips 0 and 1 only)
    {
        auto& rp = peak_snapshots[0];
        auto& lp = peak_snapshots[1];

        float r_dy = rp.r_foot_y - rp.hips_y;
        float l_dy = lp.l_foot_y - lp.hips_y;
        float r_dx = rp.r_foot_x - rp.hips_x;
        float l_dx = lp.l_foot_x - lp.hips_x;
        float r_dz = rp.r_foot_z - rp.hips_z;
        float l_dz = lp.l_foot_z - lp.hips_z;

        printf("\n[MIRROR] Walk Step Bilateral Symmetry\n");
        printf("  Right step: dx=%+.4f dy=%+.4f dz=%+.4f\n", r_dx, r_dy, r_dz);
        printf("  Left  step: dx=%+.4f dy=%+.4f dz=%+.4f\n", l_dx, l_dy, l_dz);

        float y_diff = std::abs(r_dy - l_dy);
        bool y_ok = y_diff < 0.15f;
        printf("  [ASSERT] Forward symmetry |R_dy - L_dy| = %.4f %s\n",
               y_diff, y_ok ? "PASS" : "FAIL");
        if (!y_ok) assertion_failures++;

        float x_sum = std::abs(r_dx + l_dx);
        bool x_ok = x_sum < 0.15f;
        printf("  [ASSERT] Lateral mirror |R_dx + L_dx| = %.4f %s\n",
               x_sum, x_ok ? "PASS" : "FAIL");
        if (!x_ok) assertion_failures++;

        float z_diff = std::abs(r_dz - l_dz);
        bool z_ok = z_diff < 0.15f;
        printf("  [ASSERT] Height symmetry |R_dz - L_dz| = %.4f %s\n",
               z_diff, z_ok ? "PASS" : "FAIL");
        if (!z_ok) assertion_failures++;
    }

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  WALK LOCOMOTION TEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;

    if (assertion_failures > 0) {
        std::cerr << "  *** " << assertion_failures << " ASSERTION FAILURES ***" << std::endl;
        return 1;
    }
    std::cout << "  ALL ASSERTIONS PASSED" << std::endl;
    return 0;
}
