// Animation Layering Test Suite
//
// Validates the two-layer animation system:
//   Layer 1 (base): Locomotion (walk/run/strafe/turn/idle)
//   Layer 2 (overlay): One-shot clips (punch, kick, guard)
//
// When a clip is tagged UPPER_BODY, only upper body joints are controlled
// by the one-shot, while legs continue their locomotion animation.
//
// Tests:
//   1. Punch while walking — legs keep moving, arms punch
//   2. Punch while idle — legs idle, arms punch
//   3. Full-body kick while walking — all joints taken over (backward compat)
//   4. Crossfade blend-in/blend-out — verifies smooth transitions
//
// Uses a single Engine instance to avoid multi-engine GPU hangs on macOS Metal.
//
// Usage:
//   ./build/logomancers/test-animation-layering --no-head     # headless
//   INTERACTIVE=1 ./build/logomancers/test-animation-layering  # interactive

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

// The floor these scenes build is 0.1 m thick with its bottom on the
// turtle, so its walking surface is 0.10 m. world_z for a humanoid is
// the FEET'S BOTTOM: spawning at 0.0 buried them 80 mm in the slab,
// which the creation door refuses (INV-37).
static constexpr float FLOOR_TOP = 0.10f;

// ---------------------------------------------------------------------------
// Test fixture — single Engine reused across all tests
// ---------------------------------------------------------------------------
struct TestFixture {
    Engine engine;
    ParticleSystem* ps = nullptr;
    ParticleDynamicsSystem* dynamics = nullptr;
    PhysicsSystem* physics = nullptr;
    PhysicsHumanoidResult h;
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    bool headless = true;

    int total_assertions = 0;
    int passed_assertions = 0;
    int failed_assertions = 0;

    bool init(bool headless_) {
        headless = headless_;
        EngineConfig config;
        config.create_display = !headless;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "Animation Layering Test";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;

        if (engine.initialize(config) != 0) {
            std::cerr << "Engine init failed" << std::endl;
            return false;
        }

        ps = &engine.get_particle_system();
        dynamics = &engine.get_dynamics_system();
        physics = &(engine.get_physics_system());

        physics->add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

        // Floor
        Particle floor = {};
        floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
        floor.shape = ParticleShape::BOX;
        floor.width = 200.0f; floor.height = 200.0f; floor.thickness = 0.1f;
        floor.r = 0.3f; floor.g = 0.3f; floor.b = 0.3f; floor.a = 1.0f;
        floor.SetMaterial(Materials::Type::HEAVY_STATIC);
        engine.add_particle(floor);

        // Light
        Particle light = {};
        light.x = 0.0f; light.y = 0.0f; light.z = 10.0f;
        light.shape = ParticleShape::BOX;
        light.width = 0.2f; light.height = 0.2f; light.thickness = 0.2f;
        light.r = 1.0f; light.g = 0.95f; light.b = 0.9f; light.a = 1.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = 600000.0f;
        light.emission_radius = 100.0f;
        engine.add_particle(light);

        // Camera
        auto& camera = engine.get_camera_system();
        camera.set_position(-10.0f, -10.0f, 10.0f);
        camera.look_at(0.0f, 3.0f, 1.0f);
        camera.set_pixels_per_unit(40.0f);

        // Humanoid
        auto& kg = engine.get_kg();
        HumanoidGenerator humanoid_gen;
        humanoid_gen.initialize(&engine, &kg);
        HumanoidSpec spec = HumanoidSpec::hunter();
        // world_z is the feet's BOTTOM; the floor's top is 0.10 (INV-37).
        h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, FLOOR_TOP, -1, spec, false);

        engine.get_humanoid_locomotion().register_humanoid_direct(
            h.hips_id,
            h.left_leg_ids, h.right_leg_ids,
            h.left_arm_ids, h.right_arm_ids,
            h.torso_ids,
            150.0f, 600.0f,
            h.entity_id
        );
        h.register_joints(&engine.get_humanoid_locomotion());

        // Walk/strafe/turn clips
        auto right_clip = create_fk_walk_step(Side::RIGHT);
        auto left_clip = create_fk_walk_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_walk_clips(h.hips_id, right_clip, left_clip);

        auto strafe_r = create_fk_strafe_step(Side::RIGHT, 1.0f);
        auto strafe_l = create_fk_strafe_step(Side::LEFT, 1.0f);
        engine.get_humanoid_locomotion().register_strafe_clips(h.hips_id, strafe_r, strafe_l);

        auto turn_r = create_fk_turn_step(Side::RIGHT, 1.0f);
        auto turn_l = create_fk_turn_step(Side::LEFT, -1.0f);
        engine.get_humanoid_locomotion().register_turn_clips(h.hips_id, turn_r, turn_l);

        // Idle clip
        auto idle_clip = create_fk_idle_clip();
        engine.get_humanoid_locomotion().register_idle_clip(h.hips_id, idle_clip);

        // Run clips
        auto run_r = create_fk_run_step(Side::RIGHT);
        auto run_l = create_fk_run_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_run_clips(h.hips_id, run_r, run_l);

        // Enable foot planting IK — locks stance foot at heel-strike,
        // prevents feet from sliding across the floor during walk/run.
        engine.get_humanoid_locomotion().set_foot_planting(h.hips_id, true);

        // Cross punch (UPPER_BODY tagged in animation_primitives.h)
        auto fk_punch = create_fk_cross_punch_right(0.5f, true);
        engine.get_humanoid_locomotion().register_fk_animation(h.hips_id, "right_cross", fk_punch);

        // Front kick (FULL_BODY — default)
        auto fk_kick = create_fk_front_kick_right(0.5f, true);
        engine.get_humanoid_locomotion().register_fk_animation(h.hips_id, "right_kick", fk_kick);

        entity_id = h.entity_id;

        // Stabilize
        for (int i = 0; i < 3; i++) step(0.02);

        return true;
    }

    void reset() {
        {
            auto view = ps->lock_particles_for_write();
            float dx = view[h.hips_id].x;
            float dy = view[h.hips_id].y;
            for (int pid : h.body_ids) {
                if (pid >= 0 && static_cast<size_t>(pid) < view.size()) {
                    view[pid].x -= dx;
                    view[pid].y -= dy;
                }
            }
        }
        engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);
        engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, 0.0f);
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);
        engine.get_humanoid_locomotion().zero_all_velocities(h.hips_id);
        engine.get_humanoid_locomotion().set_speed_modifier(h.hips_id, 1.0f);
        for (int i = 0; i < 10; i++) step();
    }

    void step(double dt = 0.02) {
        engine.get_platform()->poll_events();
        engine.update(dt);
        engine.render();
    }

    void present() {
        engine.present();
        if (!headless) std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    bool wait_for_space(const char* label) {
        if (headless) return true;
        auto& rs = engine.get_draw_surface();
        bool space_was_down = true;
        while (true) {
            const auto& input = engine.get_input_system().get_input_state();
            if (input.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) return false;

            bool space_now = input.keys[GLFW_KEY_SPACE];
            if (!space_now) space_was_down = false;
            if (space_now && !space_was_down) return true;

            engine.update(1.0 / 60.0);
            engine.render();
            char buf[256];
            snprintf(buf, sizeof(buf), "SPACE to start: %s", label);
            rs.draw_text(20, 30, buf, 100, 255, 100);
            rs.draw_text(20, 50, "ESC to exit", 200, 200, 200);
            rs.draw_text(20, 920, "ANIMATION LAYERING", 140, 140, 140);
            engine.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    bool quit_requested = false;
    bool space_was_down_ = false;

    bool should_exit() {
        if (headless) return false;
        const auto& input = engine.get_input_system().get_input_state();
        if (input.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) {
            quit_requested = true;
            return true;
        }
        bool space_now = input.keys[GLFW_KEY_SPACE];
        if (space_now && !space_was_down_) {
            space_was_down_ = true;
            return true;
        }
        if (!space_now) space_was_down_ = false;
        return false;
    }

    void draw_hud(const char* test_name, int frame, int total_frames, const char* live_data) {
        if (headless) return;
        auto& rs = engine.get_draw_surface();
        char buf[256];
        int y = 20;

        snprintf(buf, sizeof(buf), "TEST: %s", test_name);
        rs.draw_text(20, y, buf, 255, 255, 255); y += 22;

        if (total_frames > 0) {
            snprintf(buf, sizeof(buf), "Frame %d / %d", frame, total_frames);
            rs.draw_text(20, y, buf, 160, 160, 160); y += 22;
        }

        {
            auto particles = ps->lock_particles_for_read();
            float hx = particles[h.hips_id].x;
            float hy = particles[h.hips_id].y;
            float hz = particles[h.hips_id].z;
            snprintf(buf, sizeof(buf), "Hips (%.2f, %.2f, %.2f)", hx, hy, hz);
            rs.draw_text(20, y, buf, 140, 140, 200); y += 22;
        }

        if (live_data && live_data[0] != '\0') {
            rs.draw_text(20, y, live_data, 100, 220, 255); y += 20;
        }

        rs.draw_text(20, 920, "ANIMATION LAYERING  |  SPACE=next  ESC=exit", 120, 120, 120);
    }

    void check(bool condition, const std::string& msg) {
        total_assertions++;
        if (condition) {
            passed_assertions++;
            printf("  [ASSERT] %s PASS\n", msg.c_str());
        } else {
            failed_assertions++;
            printf("  [ASSERT] %s FAIL\n", msg.c_str());
        }
    }
};

// Helper: get foot Z positions
struct FootPositions {
    float left_foot_z = 0.0f;
    float right_foot_z = 0.0f;
};

FootPositions get_foot_positions(TestFixture& f) {
    auto particles = f.ps->lock_particles_for_read();
    FootPositions fp;
    // Foot IDs are leg[0] in the leg ID arrays
    fp.left_foot_z = particles[f.h.left_leg_ids[0]].z;
    fp.right_foot_z = particles[f.h.right_leg_ids[0]].z;
    return fp;
}

// Helper: get right hand Y (forward direction) relative to hips
float get_right_hand_forward_offset(TestFixture& f) {
    auto particles = f.ps->lock_particles_for_read();
    // Hand is arm[3]
    float hand_y = particles[f.h.right_arm_ids[3]].y;
    float hips_y = particles[f.h.hips_id].y;
    return hand_y - hips_y;
}

// ---------------------------------------------------------------------------
// Test 1: Punch while walking — legs keep moving
// ---------------------------------------------------------------------------
int test_punch_while_walking(TestFixture& f) {
    printf("\n--- Test 1: Punch While Walking (UPPER_BODY overlay) ---\n");
    f.reset();
    int failures = 0;

    // Walk forward at normal speed
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    // Warm up walk cycle (60 frames)
    for (int i = 0; i < 60; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("1: Punch While Walking", i, 250, "Warming up walk...");
        f.present();
    }

    // Measure foot Z range during normal walk (30 frames baseline)
    float walk_foot_z_min = 999.0f, walk_foot_z_max = -999.0f;
    for (int i = 0; i < 30; i++) {
        f.step();
        auto fp = get_foot_positions(f);
        float combined = fp.left_foot_z + fp.right_foot_z;
        walk_foot_z_min = std::min(walk_foot_z_min, std::min(fp.left_foot_z, fp.right_foot_z));
        walk_foot_z_max = std::max(walk_foot_z_max, std::max(fp.left_foot_z, fp.right_foot_z));
        (void)combined;  // suppress unused warning
    }
    float walk_foot_range = walk_foot_z_max - walk_foot_z_min;
    printf("  Walk foot Z range: %.4f (min=%.4f max=%.4f)\n",
           walk_foot_range, walk_foot_z_min, walk_foot_z_max);

    // Trigger punch while still walking
    printf("  Triggering right_cross punch...\n");
    f.engine.get_humanoid_locomotion().play_fk_animation(f.h.hips_id, "right_cross");

    // Verify punch is active
    bool punch_started = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
    f.check(punch_started, "Punch started (fk_playing=true)");
    if (!punch_started) failures++;

    // Measure foot Z range DURING punch (50 frames ≈ 1s)
    float punch_foot_z_min = 999.0f, punch_foot_z_max = -999.0f;
    int frames_with_punch = 0;
    for (int i = 0; i < 50; i++) {
        if (f.should_exit()) return failures;
        f.step();

        bool still_playing = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        if (still_playing) frames_with_punch++;

        auto fp = get_foot_positions(f);
        punch_foot_z_min = std::min(punch_foot_z_min, std::min(fp.left_foot_z, fp.right_foot_z));
        punch_foot_z_max = std::max(punch_foot_z_max, std::max(fp.left_foot_z, fp.right_foot_z));

        char buf[128];
        snprintf(buf, sizeof(buf), "Punching: fk=%s  foot_range=%.3f",
                 still_playing ? "YES" : "no",
                 punch_foot_z_max - punch_foot_z_min);
        f.draw_hud("1: Punch While Walking", 90 + i, 250, buf);
        f.present();
    }
    float punch_foot_range = punch_foot_z_max - punch_foot_z_min;
    printf("  Punch foot Z range: %.4f (min=%.4f max=%.4f)\n",
           punch_foot_range, punch_foot_z_min, punch_foot_z_max);
    printf("  Frames with punch active: %d / 50\n", frames_with_punch);

    // KEY ASSERTION: During an UPPER_BODY punch, feet should still be
    // moving (non-zero range). Without layering, they'd freeze.
    f.check(punch_foot_range > 0.01f,
            "Legs animated during UPPER_BODY punch: foot_range=" +
            std::to_string(punch_foot_range) + " (need > 0.01)");
    if (punch_foot_range <= 0.01f) failures++;

    // The foot range during punch should be comparable to normal walk
    // (at least 25% of walk range — some reduction is OK from the
    // different upper body pose affecting balance, but not near-zero)
    float ratio = (walk_foot_range > 0.001f) ? punch_foot_range / walk_foot_range : 0.0f;
    f.check(ratio > 0.25f,
            "Punch foot range is >=25% of walk: ratio=" +
            std::to_string(ratio) + " (" + std::to_string(punch_foot_range) +
            "/" + std::to_string(walk_foot_range) + ")");
    if (ratio <= 0.25f) failures++;

    // Verify some frames had the punch playing
    f.check(frames_with_punch > 10,
            "Punch played for >10 frames: " + std::to_string(frames_with_punch));
    if (frames_with_punch <= 10) failures++;

    // After punch ends, walk should continue — measure another 30 frames
    for (int i = 0; i < 30; i++) {
        f.step();
        f.present();
    }
    float post_foot_z_min = 999.0f, post_foot_z_max = -999.0f;
    for (int i = 0; i < 30; i++) {
        f.step();
        auto fp = get_foot_positions(f);
        post_foot_z_min = std::min(post_foot_z_min, std::min(fp.left_foot_z, fp.right_foot_z));
        post_foot_z_max = std::max(post_foot_z_max, std::max(fp.left_foot_z, fp.right_foot_z));
        f.draw_hud("1: Punch While Walking", 170 + i, 250, "Post-punch walk...");
        f.present();
    }
    float post_foot_range = post_foot_z_max - post_foot_z_min;
    f.check(post_foot_range > 0.01f,
            "Walk resumes after punch: post_range=" + std::to_string(post_foot_range));
    if (post_foot_range <= 0.01f) failures++;

    return failures;
}

// ---------------------------------------------------------------------------
// Test 2: Punch while idle — lower body stays in idle pose
// ---------------------------------------------------------------------------
int test_punch_while_idle(TestFixture& f) {
    printf("\n--- Test 2: Punch While Idle (UPPER_BODY overlay + idle base) ---\n");
    f.reset();
    int failures = 0;

    // Let idle settle (walk phase needs to settle first)
    for (int i = 0; i < 80; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("2: Punch While Idle", i, 200, "Settling to idle...");
        f.present();
    }

    // Trigger punch while idle
    printf("  Triggering right_cross punch during idle...\n");
    f.engine.get_humanoid_locomotion().play_fk_animation(f.h.hips_id, "right_cross");

    bool punch_started = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
    f.check(punch_started, "Punch started during idle");
    if (!punch_started) failures++;

    // During punch, hips should stay relatively stable (not falling)
    float hips_z_min = 999.0f, hips_z_max = -999.0f;
    int frames_with_punch = 0;
    for (int i = 0; i < 50; i++) {
        if (f.should_exit()) return failures;
        f.step();

        bool still_playing = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        if (still_playing) frames_with_punch++;

        auto particles = f.ps->lock_particles_for_read();
        float hz = particles[f.h.hips_id].z;
        hips_z_min = std::min(hips_z_min, hz);
        hips_z_max = std::max(hips_z_max, hz);

        char buf[128];
        snprintf(buf, sizeof(buf), "Idle punch: fk=%s  hips_z=%.3f",
                 still_playing ? "YES" : "no", hz);
        f.draw_hud("2: Punch While Idle", 80 + i, 200, buf);
        f.present();
    }
    float hips_z_range = hips_z_max - hips_z_min;
    printf("  Hips Z range during idle punch: %.4f\n", hips_z_range);

    // Hips should stay upright (not collapse)
    f.check(hips_z_min > 0.5f,
            "Hips upright during idle punch: min_z=" + std::to_string(hips_z_min) +
            " (need > 0.5)");
    if (hips_z_min <= 0.5f) failures++;

    f.check(frames_with_punch > 5,
            "Punch played during idle: " + std::to_string(frames_with_punch) + " frames");
    if (frames_with_punch <= 5) failures++;

    return failures;
}

// ---------------------------------------------------------------------------
// Test 3: Full-body kick while walking — should take over all joints
// ---------------------------------------------------------------------------
int test_fullbody_kick_while_walking(TestFixture& f) {
    printf("\n--- Test 3: Full-Body Kick While Walking ---\n");
    f.reset();
    int failures = 0;

    // Walk forward
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    for (int i = 0; i < 60; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("3: Full-Body Kick", i, 180, "Walking...");
        f.present();
    }

    // Record hips Y before kick (character is walking forward along Y)
    float hips_y_before_kick;
    {
        auto particles = f.ps->lock_particles_for_read();
        hips_y_before_kick = particles[f.h.hips_id].y;
    }

    // Trigger kick (FULL_BODY — should stop and override everything)
    printf("  Triggering right_kick (FULL_BODY)...\n");
    f.engine.get_humanoid_locomotion().play_fk_animation(f.h.hips_id, "right_kick");

    bool kick_started = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
    f.check(kick_started, "Kick started (fk_playing=true)");
    if (!kick_started) failures++;

    // The kick should stop the character and control all joints
    int frames_with_kick = 0;
    float hips_z_min = 999.0f;
    float hips_y_max_during_kick = hips_y_before_kick;
    for (int i = 0; i < 60; i++) {
        if (f.should_exit()) return failures;
        f.step();

        bool still_playing = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        if (still_playing) frames_with_kick++;

        auto particles = f.ps->lock_particles_for_read();
        float hz = particles[f.h.hips_id].z;
        float hy = particles[f.h.hips_id].y;
        hips_z_min = std::min(hips_z_min, hz);
        hips_y_max_during_kick = std::max(hips_y_max_during_kick, hy);

        char buf[128];
        snprintf(buf, sizeof(buf), "Kicking: fk=%s  hips_z=%.3f  drift=%.3f",
                 still_playing ? "YES" : "no", hz,
                 hy - hips_y_before_kick);
        f.draw_hud("3: Full-Body Kick", 60 + i, 180, buf);
        f.present();
    }

    // Character should NOT have slid forward during kick
    float kick_drift = hips_y_max_during_kick - hips_y_before_kick;
    printf("  Kick forward drift: %.4f m (before=%.3f, max_during=%.3f)\n",
           kick_drift, hips_y_before_kick, hips_y_max_during_kick);
    f.check(kick_drift < 0.3f,
            "Character stopped during FULL_BODY kick: drift=" +
            std::to_string(kick_drift) + "m (need < 0.3)");
    if (kick_drift >= 0.3f) failures++;

    f.check(kick_started, "Kick animation played");

    // After kick ends, walk should resume
    for (int i = 0; i < 60; i++) {
        f.step();
        f.draw_hud("3: Full-Body Kick", 120 + i, 180, "Post-kick walk...");
        f.present();
    }

    bool walk_resumed = !f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
    f.check(walk_resumed, "Walk resumed after kick (fk_playing=false)");
    if (!walk_resumed) failures++;

    return failures;
}

// ---------------------------------------------------------------------------
// Test 4: Crossfade blend-in / blend-out
// ---------------------------------------------------------------------------
// Verifies that combat clips crossfade smoothly instead of snapping.
//
// Strategy: Compare peak hand offset during the clip to the post-clip state.
// The walk cycle arm swing confounds single-frame measurements, so we:
//   1. Track max hand offset during entire clip (= peak punch extension)
//   2. Track max hand offset in first 2 frames (= blend-in partial)
//   3. Track max hand offset in last frames (= blend-out returning)
//   4. After clip ends, verify hand returns close to walk-like values
//
// The peak punch is when the fist is maximally extended forward. During
// blend-in, the max offset should be less than the peak. After blend-out,
// the hand should be close to walk values again.
int test_crossfade_blend(TestFixture& f) {
    printf("\n--- Test 4: Crossfade Blend-In / Blend-Out ---\n");
    f.reset();
    int failures = 0;

    // Walk forward
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    // Warm up walk (60 frames)
    for (int i = 0; i < 60; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("4: Crossfade Blend", i, 250, "Warming up walk...");
        f.present();
    }

    // Measure hand forward offset range during normal walk (30 frames)
    float walk_hand_max = -999.0f;
    for (int i = 0; i < 30; i++) {
        f.step();
        float h = get_right_hand_forward_offset(f);
        walk_hand_max = std::max(walk_hand_max, h);
    }
    printf("  Walk hand forward max offset: %.4f\n", walk_hand_max);

    // Trigger punch
    printf("  Triggering right_cross (blend_in_ms=80, blend_out_ms=100)...\n");
    f.engine.get_humanoid_locomotion().play_fk_animation(f.h.hips_id, "right_cross");

    // Track all frames during clip: collect max offsets in phases
    // Phase 1: first 4 frames (0-80ms) — blend-in
    // Phase 2: middle frames (80ms - duration-100ms) — full weight
    // Phase 3: last frames — blend-out
    float blend_in_max = -999.0f;
    float full_weight_max = -999.0f;
    float overall_max = -999.0f;
    int total_frames = 0;

    // Run blend-in phase (first 4 frames = 80ms at 20ms/frame)
    for (int i = 0; i < 4; i++) {
        f.step();
        float h = get_right_hand_forward_offset(f);
        blend_in_max = std::max(blend_in_max, h);
        overall_max = std::max(overall_max, h);
        total_frames++;

        char buf[128];
        snprintf(buf, sizeof(buf), "Blend-in f%d: hand=%.3f", i+1, h);
        f.draw_hud("4: Crossfade Blend", 94 + i, 250, buf);
        f.present();
    }
    printf("  Blend-in max offset (first 4 frames): %.4f\n", blend_in_max);

    // Run full weight phase (next 40 frames ≈ 800ms)
    for (int i = 0; i < 40; i++) {
        if (f.should_exit()) return failures;
        f.step();
        float h = get_right_hand_forward_offset(f);
        full_weight_max = std::max(full_weight_max, h);
        overall_max = std::max(overall_max, h);
        total_frames++;

        bool still = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        char buf[128];
        snprintf(buf, sizeof(buf), "Full: fk=%s hand=%.3f peak=%.3f",
                 still ? "YES" : "no", h, full_weight_max);
        f.draw_hud("4: Crossfade Blend", 98 + i, 250, buf);
        f.present();
    }
    printf("  Full weight max offset: %.4f\n", full_weight_max);

    // Run blend-out phase + post-clip (30 more frames)
    float blend_out_last = 0.0f;
    for (int i = 0; i < 30; i++) {
        if (f.should_exit()) return failures;
        f.step();
        float h = get_right_hand_forward_offset(f);
        overall_max = std::max(overall_max, h);
        blend_out_last = h;
        total_frames++;

        bool still = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        char buf[128];
        snprintf(buf, sizeof(buf), "Blend-out: fk=%s hand=%.3f",
                 still ? "YES" : "no", h);
        f.draw_hud("4: Crossfade Blend", 138 + i, 250, buf);
        f.present();
    }

    // Let walk settle after clip (20 frames)
    for (int i = 0; i < 20; i++) {
        f.step();
        f.present();
    }
    float post_hand_max = -999.0f;
    for (int i = 0; i < 10; i++) {
        f.step();
        float h = get_right_hand_forward_offset(f);
        post_hand_max = std::max(post_hand_max, h);
    }

    bool punch_done = !f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
    printf("  Overall max offset: %.4f\n", overall_max);
    printf("  Post-clip hand max: %.4f (punch done: %s)\n",
           post_hand_max, punch_done ? "yes" : "no");
    printf("  Total frames tracked: %d\n", total_frames);

    // ASSERTION 1: Full weight max should exceed walk max
    // (punch extends hand forward beyond normal walk swing)
    f.check(full_weight_max > walk_hand_max,
            "Punch extends hand beyond walk: full_max=" +
            std::to_string(full_weight_max) + " > walk_max=" +
            std::to_string(walk_hand_max));
    if (full_weight_max <= walk_hand_max) failures++;

    // ASSERTION 2: Blend-in max should be less than full weight max
    // (partial weight during blend-in limits extension)
    f.check(blend_in_max < full_weight_max,
            "Blend-in partial: blend_in_max=" +
            std::to_string(blend_in_max) + " < full_max=" +
            std::to_string(full_weight_max));
    if (blend_in_max >= full_weight_max) failures++;

    // ASSERTION 3: Post-clip hand should return closer to walk values
    // (blend-out returns arm to locomotion pose)
    f.check(post_hand_max < full_weight_max,
            "Post-clip hand returned: post_max=" +
            std::to_string(post_hand_max) + " < full_max=" +
            std::to_string(full_weight_max));
    if (post_hand_max >= full_weight_max) failures++;

    // ASSERTION 4: Punch clip completed
    f.check(punch_done, "Punch clip completed (fk_playing=false)");
    if (!punch_done) failures++;

    return failures;
}

// ---------------------------------------------------------------------------
// Test 5: Punch teleport diagnostic — track hips XY across punch trigger
// ---------------------------------------------------------------------------
int test_punch_teleport_diag(TestFixture& f) {
    printf("\n--- Test 5: Punch Teleport Diagnostic ---\n");
    f.reset();
    int failures = 0;

    // Walk forward
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    // Warm up walk (60 frames)
    for (int i = 0; i < 60; i++) f.step();

    // Record hips position for 10 frames before punch
    printf("  --- Pre-punch walk (10 frames) ---\n");
    float prev_x, prev_y;
    {
        auto p = f.ps->lock_particles_for_read();
        prev_x = p[f.h.hips_id].x;
        prev_y = p[f.h.hips_id].y;
    }
    for (int i = 0; i < 10; i++) {
        f.step();
        auto p = f.ps->lock_particles_for_read();
        float x = p[f.h.hips_id].x;
        float y = p[f.h.hips_id].y;
        float z = p[f.h.hips_id].z;
        float dx = x - prev_x;
        float dy = y - prev_y;
        float dist = sqrtf(dx * dx + dy * dy);
        printf("  [PRE ] f=%2d  hips=(%.4f, %.4f, %.4f)  delta=%.4f\n",
               i, x, y, z, dist);
        prev_x = x;
        prev_y = y;
    }

    // Record position at punch trigger
    float punch_trigger_x, punch_trigger_y;
    {
        auto p = f.ps->lock_particles_for_read();
        punch_trigger_x = p[f.h.hips_id].x;
        punch_trigger_y = p[f.h.hips_id].y;
    }
    printf("  --- TRIGGERING PUNCH at hips=(%.4f, %.4f) ---\n",
           punch_trigger_x, punch_trigger_y);
    f.engine.get_humanoid_locomotion().play_fk_animation(f.h.hips_id, "right_cross");

    // Record hips position for 20 frames after punch
    printf("  --- Post-punch (20 frames) ---\n");
    float max_frame_delta = 0.0f;
    int max_delta_frame = -1;
    prev_x = punch_trigger_x;
    prev_y = punch_trigger_y;

    for (int i = 0; i < 20; i++) {
        f.step();
        auto p = f.ps->lock_particles_for_read();
        float x = p[f.h.hips_id].x;
        float y = p[f.h.hips_id].y;
        float z = p[f.h.hips_id].z;
        float vx = p[f.h.hips_id].vx;
        float vy = p[f.h.hips_id].vy;
        float dx = x - prev_x;
        float dy = y - prev_y;
        float dist = sqrtf(dx * dx + dy * dy);
        bool playing = f.engine.get_humanoid_locomotion().is_fk_animation_playing(f.h.hips_id);
        printf("  [POST] f=%2d  hips=(%.4f, %.4f, %.4f)  delta=%.4f  vel=(%.3f,%.3f)  fk=%s\n",
               i, x, y, z, dist, vx, vy, playing ? "YES" : "no");

        if (dist > max_frame_delta) {
            max_frame_delta = dist;
            max_delta_frame = i;
        }
        prev_x = x;
        prev_y = y;
    }

    // Total displacement from punch trigger
    float total_dx = prev_x - punch_trigger_x;
    float total_dy = prev_y - punch_trigger_y;
    float total_dist = sqrtf(total_dx * total_dx + total_dy * total_dy);
    printf("  Total displacement over 20 frames: %.4f\n", total_dist);
    printf("  Max single-frame delta: %.4f at frame %d\n", max_frame_delta, max_delta_frame);

    // A normal walk at 1.5 m/s for 20ms/frame = 0.03m/frame
    // A "teleport" would be >0.2m in a single frame
    f.check(max_frame_delta < 0.2f,
            "No teleport: max_delta=" + std::to_string(max_frame_delta) +
            " (need < 0.2)");
    if (max_frame_delta >= 0.2f) failures++;

    return failures;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // Headless by default; window only when INTERACTIVE=1 is set.
    bool headless = true;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--no-head") headless = true;
    }
    if (std::getenv("INTERACTIVE")) headless = false;

    TestFixture f;
    if (!f.init(headless)) return 1;

    int total_failures = 0;

    struct TestEntry {
        const char* name;
        int (*func)(TestFixture&);
    };

    TestEntry tests[] = {
        {"1: Punch While Walking",          test_punch_while_walking},
        {"2: Punch While Idle",             test_punch_while_idle},
        {"3: Full-Body Kick While Walking", test_fullbody_kick_while_walking},
        {"4: Crossfade Blend",              test_crossfade_blend},
        {"5: Punch Teleport Diagnostic",    test_punch_teleport_diag},
    };

    for (auto& test : tests) {
        if (!headless) {
            if (!f.wait_for_space(test.name)) break;
            f.reset();
        }
        f.space_was_down_ = true;
        total_failures += test.func(f);
        if (f.quit_requested) break;
    }

    if (!headless) {
        f.wait_for_space("EXIT (or press ESC)");
    }

    printf("\n========================================\n");
    if (total_failures == 0) {
        printf("  ALL ASSERTIONS PASSED\n");
    } else {
        printf("  *** %d ASSERTION FAILURES ***\n", total_failures);
    }
    printf("========================================\n\n");

    return total_failures > 0 ? 1 : 0;
}
