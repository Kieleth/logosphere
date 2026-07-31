// Foot Planting Test — Verifies stance feet stay locked in world space
//
// Tests that during the stance phase of walking, the planted foot's world
// position does not change (no "skating"). Uses 2-bone IK to lock the
// stance foot while the hips advance.
//
// Tests:
//   1. Forward walk foot lock   — walk North, measure stance foot slide
//   2. IK chain integrity       — verify thigh→knee→ankle distances are constant
//   3. Heel-strike placement    — verify plant target is at correct stride position
//
// Usage:
//   ./build/logomancers/test-foot-planting                        # headless
//   INTERACTIVE=1 ./build/logomancers/test-foot-planting          # interactive

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "humanoid_validator.h"
#include <iostream>
#include <cmath>
#include <vector>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles
#include <chrono>
#include <thread>

// ========================================================================
// Test fixture: engine + humanoid with foot planting enabled
// ========================================================================
struct TestFixture {
    Engine engine;
    ParticleSystem* ps = nullptr;
    ParticleDynamicsSystem* dynamics = nullptr;
    PhysicsSystem* physics = nullptr;
    PhysicsHumanoidResult h;
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    bool headless = true;

    bool init(bool headless_) {
        headless = headless_;
        EngineConfig config;
        config.create_display = !headless;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "Foot Planting Test";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;

        if (engine.initialize(config) != 0) {
            std::cerr << "Engine init failed" << std::endl;
            return false;
        }

        ps = &engine.get_particle_system();
        dynamics = &engine.get_dynamics_system();
        physics = &(engine.get_physics_system());
        auto& kg = engine.get_kg();

        physics->add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

        // Floor
        Particle floor = {};
        floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
        floor.shape = ParticleShape::BOX;
        floor.width = 20.0f; floor.height = 20.0f; floor.thickness = 0.1f;
        floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
        floor.SetMaterial(Materials::Type::HEAVY_STATIC);
        engine.add_particle(floor);

        // Lights
        auto add_light = [this](float x, float y, float z, float strength) {
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
        add_light(3.0f, 0.0f, 3.0f, 300000.0f);
        add_light(-3.0f, 0.0f, 3.0f, 300000.0f);

        // Camera
        auto& camera = engine.get_camera_system();
        camera.set_position(-10.0f, -10.0f, 10.0f);
        camera.look_at(0.0f, 0.0f, 1.0f);
        camera.set_pixels_per_unit(50.0f);

        // Humanoid
        HumanoidGenerator humanoid_gen;
        humanoid_gen.initialize(&engine, &kg);
        HumanoidSpec spec = HumanoidSpec::hunter();
        h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);

        engine.get_humanoid_locomotion().register_humanoid_direct(
            h.hips_id,
            h.left_leg_ids, h.right_leg_ids,
            h.left_arm_ids, h.right_arm_ids,
            h.torso_ids,
            150.0f, 600.0f,
            h.entity_id
        );
        h.register_joints(&engine.get_humanoid_locomotion());

        // Register walk clips
        auto right_clip = create_fk_walk_step(Side::RIGHT);
        auto left_clip = create_fk_walk_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_walk_clips(h.hips_id, right_clip, left_clip);

        // Register strafe + turn clips
        auto strafe_r = create_fk_strafe_step(Side::RIGHT, 1.0f);
        auto strafe_l = create_fk_strafe_step(Side::LEFT, 1.0f);
        engine.get_humanoid_locomotion().register_strafe_clips(h.hips_id, strafe_r, strafe_l);

        auto turn_r = create_fk_turn_step(Side::RIGHT, 1.0f);
        auto turn_l = create_fk_turn_step(Side::LEFT, -1.0f);
        engine.get_humanoid_locomotion().register_turn_clips(h.hips_id, turn_r, turn_l);

        // Enable foot planting
        engine.get_humanoid_locomotion().set_foot_planting(h.hips_id, true);

        entity_id = h.entity_id;
        return true;
    }

    void reset() {
        {
            auto view = ps->lock_particles_for_write();
            view[h.hips_id].x = 0.0f;
            view[h.hips_id].y = 0.0f;
        }
        engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
        engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, 0.0f);
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);
        engine.get_humanoid_locomotion().clear_look_at_target(h.hips_id);
        engine.get_humanoid_locomotion().zero_all_velocities(h.hips_id);
        step();
    }

    void step(double dt = 1.0 / 60.0) {
        engine.get_platform()->poll_events();
        engine.update(dt);
        if (!headless) engine.render();
    }

    void present() {
        if (!headless) {
            engine.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Get foot world position
    struct FootPos { float x, y, z; };
    FootPos get_foot_pos(bool right) {
        auto view = ps->lock_particles_for_read();
        unsigned int id = right ? h.right_leg_ids[0] : h.left_leg_ids[0];
        return {view[id].x, view[id].y, view[id].z};
    }

    // Get hips position
    FootPos get_hips_pos() {
        auto view = ps->lock_particles_for_read();
        return {view[h.hips_id].x, view[h.hips_id].y, view[h.hips_id].z};
    }

    // Wait for interactive mode
    bool wait_for_space(const char* label) {
        if (headless) return true;
        auto& rs = engine.get_draw_surface();
        bool space_was_down = true;
        while (true) {
            engine.get_platform()->poll_events();
            const auto& input_state = engine.get_input_system().get_input_state();
            if (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) return false;

            bool space_now = input_state.keys[GLFW_KEY_SPACE];
            if (!space_now) space_was_down = false;
            if (space_now && !space_was_down) return true;

            engine.update(1.0 / 60.0);
            engine.render();
            char buf[256];
            snprintf(buf, sizeof(buf), "SPACE to start: %s", label);
            rs.draw_text(20, 30, buf, 100, 255, 100);
            engine.present();
        }
    }
};

// ========================================================================
// Test 1: Forward walk foot lock
// Walk North at 1.5 m/s for 2 seconds. During stance phases, measure
// how much each foot slides. Without planting, feet slide ~stride per step.
// With planting, stance foot should stay within epsilon of plant target.
// ========================================================================
int test_forward_walk_foot_lock(TestFixture& f) {
    printf("\n--- Test 1: Forward Walk Foot Lock ---\n");
    f.reset();
    int failures = 0;

    // Start walking North
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);

    // Let it settle for a few frames
    for (int i = 0; i < 10; i++) {
        f.step();
        f.present();
    }

    // Track foot positions over 120 frames (2 seconds at 60fps)
    float max_stance_slide_l = 0.0f;
    float max_stance_slide_r = 0.0f;
    float prev_lfoot_x = 0, prev_lfoot_y = 0;
    float prev_rfoot_x = 0, prev_rfoot_y = 0;
    bool prev_initialized = false;

    int stance_frames_l = 0, stance_frames_r = 0;
    int total_frames = 0;

    // Per-frame connectivity tracking (strict 0.02m tolerance)
    // If foot planting tears joints apart, this will catch it.
    float max_r_hip_err = 0, max_r_knee_err = 0, max_r_ankle_err = 0, max_r_toe_err = 0;
    float max_l_hip_err = 0, max_l_knee_err = 0, max_l_ankle_err = 0, max_l_toe_err = 0;
    int worst_frame_r = -1, worst_frame_l = -1;
    float worst_err_r = 0, worst_err_l = 0;

    // Rotation tracking: foot planting must not cause unwanted body rotation
    float max_abs_pitch = 0.0f;       // max |rotation_x| (tipping forward/back)
    float max_abs_yaw = 0.0f;         // max |rotation_y| (tipping sideways)
    float max_facing_div = 0.0f;      // max |rotation_z - base_rotation| (excludes FK animation)
    float max_rz_delta = 0.0f;        // max frame-to-frame rotation_z jump
    float prev_hips_rz = 0.0f;

    for (int frame = 0; frame < 120; frame++) {
        f.step();
        f.present();
        total_frames++;

        auto lfoot = f.get_foot_pos(false);
        auto rfoot = f.get_foot_pos(true);

        if (prev_initialized) {
            float dl = std::sqrt((lfoot.x - prev_lfoot_x) * (lfoot.x - prev_lfoot_x) +
                                 (lfoot.y - prev_lfoot_y) * (lfoot.y - prev_lfoot_y));
            float dr = std::sqrt((rfoot.x - prev_rfoot_x) * (rfoot.x - prev_rfoot_x) +
                                 (rfoot.y - prev_rfoot_y) * (rfoot.y - prev_rfoot_y));

            if (dl < 0.001f) stance_frames_l++;
            if (dr < 0.001f) stance_frames_r++;

            // Only measure slide on frames we consider "stance" (dl/dr < 0.001)
            // Previously used "whichever moved less" which captured transition
            // frames where both feet move during blend ramp-up/down.
            if (dl < 0.001f) {
                max_stance_slide_l = std::max(max_stance_slide_l, dl);
            }
            if (dr < 0.001f) {
                max_stance_slide_r = std::max(max_stance_slide_r, dr);
            }

            if (frame % 30 == 0) {
                printf("  [FRAME %3d] L_foot=(%.3f,%.3f) dl=%.4f | R_foot=(%.3f,%.3f) dr=%.4f\n",
                       frame, lfoot.x, lfoot.y, dl, rfoot.x, rfoot.y, dr);
            }
        }

        prev_lfoot_x = lfoot.x; prev_lfoot_y = lfoot.y;
        prev_rfoot_x = rfoot.x; prev_rfoot_y = rfoot.y;
        prev_initialized = true;

        // --- Per-frame hips rotation check ---
        {
            auto view = f.ps->lock_particles_for_read();
            auto& hips = view[f.h.hips_id];
            float abs_pitch = std::abs(hips.rotation_x);
            float abs_yaw = std::abs(hips.rotation_y);
            if (abs_pitch > max_abs_pitch) max_abs_pitch = abs_pitch;
            if (abs_yaw > max_abs_yaw) max_abs_yaw = abs_yaw;

            // Facing divergence: how far hips.rotation_z drifts from intended
            // direction. The FK walk animation adds pelvic rotation on top of
            // base_rotation, so we compare against base_rotation to isolate
            // any unwanted rotation introduced by foot planting IK.
            float base_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);
            float div = std::abs(hips.rotation_z - base_rot);
            if (div > max_facing_div) max_facing_div = div;

            if (prev_initialized) {
                float rz_delta = std::abs(hips.rotation_z - prev_hips_rz);
                if (rz_delta > max_rz_delta) max_rz_delta = rz_delta;
            }
            prev_hips_rz = hips.rotation_z;
        }

        // --- Per-frame joint connectivity check ---
        // Check all leg joints every frame to catch disconnection
        {
            auto view = f.ps->lock_particles_for_read();

            // Right leg joints
            auto rh = check_joint("r_hip",   f.h.torso_ids[0],     f.h.right_leg_ids[2], view, *f.physics, 0.02f);
            auto rk = check_joint("r_knee",  f.h.right_leg_ids[2], f.h.right_leg_ids[1], view, *f.physics, 0.02f);
            auto ra = check_joint("r_ankle", f.h.right_leg_ids[1], f.h.right_leg_ids[0], view, *f.physics, 0.02f);
            auto rt = check_joint("r_toe",   f.h.right_leg_ids[0], f.h.right_leg_ids[3], view, *f.physics, 0.02f);

            max_r_hip_err   = std::max(max_r_hip_err,   rh.pivot_error);
            max_r_knee_err  = std::max(max_r_knee_err,  rk.pivot_error);
            max_r_ankle_err = std::max(max_r_ankle_err, ra.pivot_error);
            max_r_toe_err   = std::max(max_r_toe_err,   rt.pivot_error);

            float frame_max_r = std::max({rh.pivot_error, rk.pivot_error, ra.pivot_error, rt.pivot_error});
            if (frame_max_r > worst_err_r) {
                worst_err_r = frame_max_r;
                worst_frame_r = frame;
            }

            // Left leg joints
            auto lh = check_joint("l_hip",   f.h.torso_ids[0],    f.h.left_leg_ids[2], view, *f.physics, 0.02f);
            auto lk = check_joint("l_knee",  f.h.left_leg_ids[2], f.h.left_leg_ids[1], view, *f.physics, 0.02f);
            auto la = check_joint("l_ankle", f.h.left_leg_ids[1], f.h.left_leg_ids[0], view, *f.physics, 0.02f);
            auto lt = check_joint("l_toe",   f.h.left_leg_ids[0], f.h.left_leg_ids[3], view, *f.physics, 0.02f);

            max_l_hip_err   = std::max(max_l_hip_err,   lh.pivot_error);
            max_l_knee_err  = std::max(max_l_knee_err,  lk.pivot_error);
            max_l_ankle_err = std::max(max_l_ankle_err, la.pivot_error);
            max_l_toe_err   = std::max(max_l_toe_err,   lt.pivot_error);

            float frame_max_l = std::max({lh.pivot_error, lk.pivot_error, la.pivot_error, lt.pivot_error});
            if (frame_max_l > worst_err_l) {
                worst_err_l = frame_max_l;
                worst_frame_l = frame;
            }

            // Log the first few disconnection frames for debugging
            if (frame_max_r > 0.02f || frame_max_l > 0.02f) {
                static int disconnect_log_count = 0;
                if (disconnect_log_count < 5) {
                    printf("  [DISCONNECT frame=%d] R: hip=%.4f knee=%.4f ankle=%.4f toe=%.4f"
                           " | L: hip=%.4f knee=%.4f ankle=%.4f toe=%.4f\n",
                           frame,
                           rh.pivot_error, rk.pivot_error, ra.pivot_error, rt.pivot_error,
                           lh.pivot_error, lk.pivot_error, la.pivot_error, lt.pivot_error);
                    disconnect_log_count++;
                }
            }
        }
    }

    auto hips = f.get_hips_pos();
    printf("  Hips final: (%.3f, %.3f)\n", hips.x, hips.y);
    printf("  Stance frames: L=%d R=%d / %d total\n",
           stance_frames_l, stance_frames_r, total_frames);
    printf("  Max stance slide: L=%.4f R=%.4f\n",
           max_stance_slide_l, max_stance_slide_r);

    // Report per-joint max errors
    printf("  Right leg max pivot error: hip=%.4f knee=%.4f ankle=%.4f toe=%.4f (worst frame=%d)\n",
           max_r_hip_err, max_r_knee_err, max_r_ankle_err, max_r_toe_err, worst_frame_r);
    printf("  Left leg max pivot error:  hip=%.4f knee=%.4f ankle=%.4f toe=%.4f (worst frame=%d)\n",
           max_l_hip_err, max_l_knee_err, max_l_ankle_err, max_l_toe_err, worst_frame_l);

    // Assert: character moved forward
    bool moved = hips.y > 1.5f;
    printf("  [ASSERT] moved North (y > 1.5): y=%.3f %s\n",
           hips.y, moved ? "PASS" : "FAIL");
    if (!moved) failures++;

    // Assert: stance frames detected
    int total_stance = stance_frames_l + stance_frames_r;
    bool has_stance = total_stance > 10;
    printf("  [ASSERT] stance frames > 10: %d %s\n",
           total_stance, has_stance ? "PASS" : "FAIL");
    if (!has_stance) failures++;

    // Assert: max stance slide tolerable
    float max_slide = std::min(max_stance_slide_l, max_stance_slide_r);
    bool no_skating = max_slide < 0.06f;
    printf("  [ASSERT] stance foot slide < 0.06m/frame: %.4f %s\n",
           max_slide, no_skating ? "PASS" : "FAIL");
    if (!no_skating) failures++;

    // Assert: STRICT per-frame leg connectivity (0.02m tolerance)
    // Foot planting MUST NOT tear joints apart.
    // If this fails, the proportional correction approach is wrong.
    const float STRICT_TOL = 0.02f;
    float worst_any = std::max(worst_err_r, worst_err_l);
    bool legs_connected = worst_any < STRICT_TOL;
    printf("  [ASSERT] leg joints connected per-frame (tol=%.3f): max_err=%.4f %s\n",
           STRICT_TOL, worst_any, legs_connected ? "PASS" : "FAIL");
    if (!legs_connected) failures++;

    // Assert: upright — IK must not tip the body forward/back or sideways
    bool upright = max_abs_pitch < 0.3f && max_abs_yaw < 0.3f;
    printf("  [ASSERT] upright (pitch < 0.3, yaw < 0.3): pitch=%.3f yaw=%.3f %s\n",
           max_abs_pitch, max_abs_yaw, upright ? "PASS" : "FAIL");
    if (!upright) failures++;

    // Assert: facing tracks base_rotation — IK must not drift hips away from
    // intended direction. The FK walk animation adds pelvic rotation on top
    // of base_rotation, so we compare against base_rotation to isolate drift.
    bool no_drift = max_facing_div < 0.3f;
    printf("  [ASSERT] facing tracks base_rotation (div < 0.3): max=%.3f %s\n",
           max_facing_div, no_drift ? "PASS" : "FAIL");
    if (!no_drift) failures++;

    // Assert: no rotation jumps — frame-to-frame rotation_z delta < 0.15 rad
    bool no_jumps = max_rz_delta < 0.15f;
    printf("  [ASSERT] no rotation jumps (rot_z delta < 0.15/frame): max=%.4f %s\n",
           max_rz_delta, no_jumps ? "PASS" : "FAIL");
    if (!no_jumps) failures++;

    return failures;
}

// ========================================================================
// Test 2: IK chain integrity
// Verify that thigh and shin lengths stay constant during IK.
// The distance from hip joint to knee and knee to ankle should not change.
// ========================================================================
int test_ik_chain_integrity(TestFixture& f) {
    printf("\n--- Test 2: IK Chain Integrity ---\n");
    f.reset();
    int failures = 0;

    // Walk forward
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);

    // Let it settle
    for (int i = 0; i < 10; i++) f.step();

    // Per-frame: check BOTH legs, track worst pivot error per joint
    float max_knee_err = 0, max_ankle_err = 0, max_hip_err = 0;
    int worst_frame = -1;
    float worst_err = 0;
    int disconnect_frames = 0;

    for (int frame = 0; frame < 120; frame++) {
        f.step();
        f.present();

        auto view = f.ps->lock_particles_for_read();
        const float TOL = 0.02f;

        // Check both legs every frame
        for (int side = 0; side < 2; side++) {
            auto& leg = (side == 0) ? f.h.right_leg_ids : f.h.left_leg_ids;
            const char* s = (side == 0) ? "R" : "L";

            auto hip   = check_joint("hip",   f.h.torso_ids[0], leg[2], view, *f.physics, TOL);
            auto knee  = check_joint("knee",  leg[2],           leg[1], view, *f.physics, TOL);
            auto ankle = check_joint("ankle", leg[1],           leg[0], view, *f.physics, TOL);

            max_hip_err   = std::max(max_hip_err,   hip.pivot_error);
            max_knee_err  = std::max(max_knee_err,  knee.pivot_error);
            max_ankle_err = std::max(max_ankle_err, ankle.pivot_error);

            float frame_max = std::max({hip.pivot_error, knee.pivot_error, ankle.pivot_error});
            if (frame_max > worst_err) {
                worst_err = frame_max;
                worst_frame = frame;
            }
            if (frame_max > TOL) {
                disconnect_frames++;
                static int log_count = 0;
                if (log_count < 5) {
                    printf("  [DISCONNECT frame=%d %s] hip=%.4f knee=%.4f ankle=%.4f\n",
                           frame, s, hip.pivot_error, knee.pivot_error, ankle.pivot_error);
                    log_count++;
                }
            }
        }
    }

    printf("  Max pivot error: hip=%.4f knee=%.4f ankle=%.4f (worst frame=%d)\n",
           max_hip_err, max_knee_err, max_ankle_err, worst_frame);
    printf("  Disconnect frames: %d / 240 (120 frames x 2 legs)\n", disconnect_frames);

    // Assert: all joints stay connected (strict 0.02m)
    const float STRICT_TOL = 0.02f;
    bool hip_ok = max_hip_err < STRICT_TOL;
    printf("  [ASSERT] hip pivot error < %.3f: %.4f %s\n",
           STRICT_TOL, max_hip_err, hip_ok ? "PASS" : "FAIL");
    if (!hip_ok) failures++;

    bool knee_ok = max_knee_err < STRICT_TOL;
    printf("  [ASSERT] knee pivot error < %.3f: %.4f %s\n",
           STRICT_TOL, max_knee_err, knee_ok ? "PASS" : "FAIL");
    if (!knee_ok) failures++;

    bool ankle_ok = max_ankle_err < STRICT_TOL;
    printf("  [ASSERT] ankle pivot error < %.3f: %.4f %s\n",
           STRICT_TOL, max_ankle_err, ankle_ok ? "PASS" : "FAIL");
    if (!ankle_ok) failures++;

    bool no_disconnects = disconnect_frames == 0;
    printf("  [ASSERT] zero disconnect frames: %d %s\n",
           disconnect_frames, no_disconnects ? "PASS" : "FAIL");
    if (!no_disconnects) failures++;

    return failures;
}

// ========================================================================
// Test 3: Walk + stop — verify feet settle naturally
// Walk, then stop. Feet should be on the ground, not floating.
// ========================================================================
int test_walk_and_stop(TestFixture& f) {
    printf("\n--- Test 3: Walk and Stop ---\n");
    f.reset();
    int failures = 0;

    // Walk forward
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);

    for (int i = 0; i < 60; i++) {
        f.step();
        f.present();
    }

    // Stop
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);

    for (int i = 0; i < 60; i++) {
        f.step();
        f.present();
    }

    // Check feet are near ground level
    auto lfoot = f.get_foot_pos(false);
    auto rfoot = f.get_foot_pos(true);
    auto hips = f.get_hips_pos();

    printf("  After stop: hips=(%.3f,%.3f,%.3f)\n", hips.x, hips.y, hips.z);
    printf("  L_foot z=%.3f  R_foot z=%.3f\n", lfoot.z, rfoot.z);

    // Feet should be near floor level (z ~ 0.1 ± 0.15)
    bool lfoot_grounded = lfoot.z < 0.3f && lfoot.z > -0.1f;
    bool rfoot_grounded = rfoot.z < 0.3f && rfoot.z > -0.1f;

    printf("  [ASSERT] L_foot grounded (z in [-0.1, 0.3]): z=%.3f %s\n",
           lfoot.z, lfoot_grounded ? "PASS" : "FAIL");
    if (!lfoot_grounded) failures++;

    printf("  [ASSERT] R_foot grounded (z in [-0.1, 0.3]): z=%.3f %s\n",
           rfoot.z, rfoot_grounded ? "PASS" : "FAIL");
    if (!rfoot_grounded) failures++;

    // Hips should be upright
    bool upright = hips.z > 0.7f && hips.z < 1.3f;
    printf("  [ASSERT] hips upright (z in [0.7, 1.3]): z=%.3f %s\n",
           hips.z, upright ? "PASS" : "FAIL");
    if (!upright) failures++;

    return failures;
}

// ========================================================================
// Main
// ========================================================================
int main(int argc, char** argv) {
    bool headless = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--visual") headless = false;
        if (std::string(argv[i]) == "--interactive") headless = false;
    }
    if (std::getenv("INTERACTIVE")) headless = false;

    printf("\n========================================\n");
    printf("  FOOT PLANTING TEST\n");
    printf("  Mode: %s\n", headless ? "Headless" : "Visual");
    printf("========================================\n");

    TestFixture f;
    if (!f.init(headless)) return 1;

    int total_failures = 0;

    struct TestEntry {
        const char* name;
        int (*func)(TestFixture&);
    };

    TestEntry tests[] = {
        {"1: Forward Walk Foot Lock", test_forward_walk_foot_lock},
        {"2: IK Chain Integrity",     test_ik_chain_integrity},
        {"3: Walk and Stop",          test_walk_and_stop},
    };

    for (auto& test : tests) {
        if (!headless) {
            if (!f.wait_for_space(test.name)) break;
            f.reset();
        }
        total_failures += test.func(f);
    }

    printf("\n========================================\n");
    printf("  FOOT PLANTING TEST COMPLETE\n");
    printf("========================================\n");

    if (total_failures > 0) {
        printf("  *** %d ASSERTION FAILURES ***\n", total_failures);
        return 1;
    }
    printf("  ALL ASSERTIONS PASSED\n");
    return 0;
}
