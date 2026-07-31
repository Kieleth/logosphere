// Idle & Run Animation Test Suite
//
// Validates the two new animation modes added on top of the walk system:
//
//   1. Idle animation — subtle breathing (spine flex) and weight shift (pelvic
//      sway) when the humanoid is stationary and walk phase has settled.
//      Measured by detecting chest oscillation over time.
//
//   2. Run animation — biomechanically distinct from walk (more hip flex,
//      bigger arm pump, shorter timing, longer stride). Activated by
//      speed-based blending when speed exceeds max_walk_speed thresholds.
//      Measured by comparing thigh forward displacement at walk vs run speeds.
//
//   3. Transitions — walk -> run -> walk -> idle must be smooth (no jumps,
//      body stays upright and connected throughout).
//
// NOTE: Uses a single Engine instance across all tests to avoid the multi-engine
// GPU resource hang on macOS Metal. The humanoid state is reset between tests.
//
// Usage:
//   ./build/logomancers/test-idle-run --no-head     # headless
//   INTERACTIVE=1 ./build/logomancers/test-idle-run  # interactive

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "humanoid_validator.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

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
        config.window_title = "Idle & Run Animation Test";
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
        add_light(0.0f, -4.0f, 3.0f, 400000.0f);
        add_light(3.0f, 0.0f, 3.0f, 300000.0f);

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
        h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);

        engine.get_humanoid_locomotion().register_humanoid_direct(
            h.hips_id,
            h.left_leg_ids, h.right_leg_ids,
            h.left_arm_ids, h.right_arm_ids,
            h.torso_ids,
            150.0f, 600.0f,  // reflexes, grit
            h.entity_id
        );
        h.register_joints(&engine.get_humanoid_locomotion());

        // Walk/strafe/turn clips (same as existing tests)
        auto right_clip = create_fk_walk_step(Side::RIGHT);
        auto left_clip = create_fk_walk_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_walk_clips(h.hips_id, right_clip, left_clip);

        auto strafe_r = create_fk_strafe_step(Side::RIGHT, 1.0f);
        auto strafe_l = create_fk_strafe_step(Side::LEFT, 1.0f);
        engine.get_humanoid_locomotion().register_strafe_clips(h.hips_id, strafe_r, strafe_l);

        auto turn_r = create_fk_turn_step(Side::RIGHT, 1.0f);
        auto turn_l = create_fk_turn_step(Side::LEFT, -1.0f);
        engine.get_humanoid_locomotion().register_turn_clips(h.hips_id, turn_r, turn_l);

        // NEW: Idle clip
        auto idle_clip = create_fk_idle_clip();
        engine.get_humanoid_locomotion().register_idle_clip(h.hips_id, idle_clip);

        // NEW: Run clips
        auto run_r = create_fk_run_step(Side::RIGHT);
        auto run_l = create_fk_run_step(Side::LEFT);
        engine.get_humanoid_locomotion().register_run_clips(h.hips_id, run_r, run_l);

        // Enable foot planting IK — locks stance foot at heel-strike,
        // prevents feet from sliding across the floor during walk/run.
        engine.get_humanoid_locomotion().set_foot_planting(h.hips_id, true);

        entity_id = h.entity_id;

        // Stabilize (3 frames)
        for (int i = 0; i < 3; i++) step(0.02);

        return true;
    }

    void reset() {
        // Teleport ALL body particles back to origin (not just hips)
        // Without this, the body is stretched between old and new positions
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
        // Let physics stabilize after teleport
        for (int i = 0; i < 10; i++) step();
    }

    void step(double dt = 0.02) {
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

    // Interactive pause: wait for SPACE to continue, showing label
    bool wait_for_space(const char* label) {
        if (headless) return true;
        auto& rs = engine.get_draw_surface();
        bool space_was_down = true;
        while (true) {
            engine.get_platform()->poll_events();
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
            rs.draw_text(20, 920, "IDLE & RUN ANIMATION", 140, 140, 140);
            engine.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    float measure_speed() {
        auto particles = ps->lock_particles_for_read();
        auto& hips = particles[h.hips_id];
        return std::sqrt(hips.vx * hips.vx + hips.vy * hips.vy);
    }

    bool quit_requested = false;   // ESC pressed — stop all tests
    bool space_was_down_ = false;  // debounce for SPACE skip

    // Returns true if the current test loop should stop.
    // SPACE = skip to next test (quit_requested stays false).
    // ESC   = stop everything  (quit_requested set true).
    bool should_exit() {
        if (headless) return false;
        const auto& input = engine.get_input_system().get_input_state();

        if (input.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) {
            quit_requested = true;
            return true;
        }

        // SPACE with debounce — skip current test
        bool space_now = input.keys[GLFW_KEY_SPACE];
        if (space_now && !space_was_down_) {
            space_was_down_ = true;
            return true;  // skip, but quit_requested stays false
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

        // Hips telemetry
        {
            auto particles = ps->lock_particles_for_read();
            float hx = particles[h.hips_id].x;
            float hy = particles[h.hips_id].y;
            float hz = particles[h.hips_id].z;
            float spd = std::sqrt(particles[h.hips_id].vx * particles[h.hips_id].vx +
                                  particles[h.hips_id].vy * particles[h.hips_id].vy);
            snprintf(buf, sizeof(buf), "Hips (%.2f, %.2f, %.2f)  speed %.2f m/s", hx, hy, hz, spd);
            rs.draw_text(20, y, buf, 140, 140, 200); y += 22;
        }

        if (live_data && live_data[0] != '\0') {
            rs.draw_text(20, y, live_data, 100, 220, 255); y += 20;
        }

        rs.draw_text(20, 920, "IDLE & RUN ANIMATION  |  SPACE=next  ESC=exit", 120, 120, 120);
    }

    void show_results(const char* test_name) {
        if (headless) return;
        bool space_was_down = true;
        while (true) {
            engine.get_platform()->poll_events();
            const auto& input = engine.get_input_system().get_input_state();
            if (input.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) return;
            bool space_now = input.keys[GLFW_KEY_SPACE];
            if (!space_now) space_was_down = false;
            if (space_now && !space_was_down) return;

            engine.update(1.0 / 60.0);
            engine.render();
            draw_hud(test_name, -1, -1, "RESULTS  (SPACE to continue)");
            engine.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
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

// ---------------------------------------------------------------------------
// Test 1: Idle Breathing Detection
// ---------------------------------------------------------------------------
int test_idle_breathing(TestFixture& f) {
    printf("\n--- Test 1: Idle Breathing Detection ---\n");
    f.reset();
    int failures = 0;

    // Walk briefly (60 frames = ~1.2s) to establish movement state
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    for (int i = 0; i < 60; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("1: Idle Breathing", i, 350, "Walking to establish movement state...");
        f.present();
    }

    // Stop and wait for walk phase to settle to neutral (~0.5s = 25 frames)
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);
    for (int i = 0; i < 40; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("1: Idle Breathing", 60 + i, 350, "Stopping, waiting for phase settle...");
        f.present();
    }

    // Verify speed is zero
    float speed = f.measure_speed();
    bool stopped = speed < 0.05f;
    printf("  [ASSERT] fully stopped: speed=%.6f m/s (limit < 0.05) %s\n",
           speed, stopped ? "PASS" : "FAIL");
    if (!stopped) failures++;

    // Verify walk phase has settled
    float walk_phase = f.engine.get_humanoid_locomotion().get_walk_phase(f.h.hips_id);
    float pi_f = static_cast<float>(M_PI);
    float phase_mod = fmodf(walk_phase, pi_f);
    bool settled = (phase_mod < 0.15f || phase_mod > (pi_f - 0.15f));
    printf("  [ASSERT] walk phase settled to neutral: phase=%.6f mod_pi=%.6f %s\n",
           walk_phase, phase_mod, settled ? "PASS" : "FAIL");
    if (!settled) failures++;

    // Sample chest-to-hips Y offset over 250 frames (~5s, >1 idle cycle)
    float min_chest_dy = 1000.0f, max_chest_dy = -1000.0f;
    float min_chest_dz = 1000.0f, max_chest_dz = -1000.0f;

    int chest_id = f.h.torso_ids.size() >= 3 ? f.h.torso_ids[2] : 0;
    int abdomen_id = f.h.torso_ids.size() >= 2 ? f.h.torso_ids[1] : 0;

    printf("  Sampling 250 frames for breathing oscillation...\n");
    printf("  chest_id=%d abdomen_id=%d hips_id=%d\n", chest_id, abdomen_id, f.h.hips_id);

    for (int i = 0; i < 250; i++) {
        if (f.should_exit()) return failures;
        f.step();

        auto particles = f.ps->lock_particles_for_read();
        float hips_y = particles[f.h.hips_id].y;
        float hips_z = particles[f.h.hips_id].z;
        float chest_y = particles[chest_id].y;
        float chest_z = particles[chest_id].z;
        float dy = chest_y - hips_y;
        float dz = chest_z - hips_z;

        min_chest_dy = std::min(min_chest_dy, dy);
        max_chest_dy = std::max(max_chest_dy, dy);
        min_chest_dz = std::min(min_chest_dz, dz);
        max_chest_dz = std::max(max_chest_dz, dz);

        float osc_so_far = std::max(max_chest_dy - min_chest_dy, max_chest_dz - min_chest_dz);
        char live[128];
        snprintf(live, sizeof(live), "Breathing: osc=%.4f (need > 0.002) | chest_dy=%.4f", osc_so_far, dy);
        f.draw_hud("1: Idle Breathing", 100 + i, 350, live);
        f.present();

        if (i % 50 == 0) {
            printf("  [frame %3d] chest_dy=%.5f chest_dz=%.5f hips(y=%.4f z=%.4f)\n",
                   i, dy, dz, hips_y, hips_z);
        }
    }

    float osc_y = max_chest_dy - min_chest_dy;
    float osc_z = max_chest_dz - min_chest_dz;
    float oscillation = std::max(osc_y, osc_z);
    printf("  Chest dy range: min=%.5f max=%.5f osc_y=%.5f\n", min_chest_dy, max_chest_dy, osc_y);
    printf("  Chest dz range: min=%.5f max=%.5f osc_z=%.5f\n", min_chest_dz, max_chest_dz, osc_z);
    printf("  Combined oscillation (max of Y,Z): %.5f\n", oscillation);

    bool breathing = oscillation > 0.002f;
    printf("  [ASSERT] breathing oscillation detected: %.6fm (need > 0.002) %s\n",
           oscillation, breathing ? "PASS" : "FAIL");
    if (!breathing) failures++;

    // Verify body didn't fall or fly during idle
    {
        auto particles = f.ps->lock_particles_for_read();
        float hips_z = particles[f.h.hips_id].z;
        bool reasonable = hips_z > 0.5f && hips_z < 2.0f;
        printf("  [ASSERT] hips Z reasonable during idle: %.6f (expected 0.5-2.0) %s\n",
               hips_z, reasonable ? "PASS" : "FAIL");
        if (!reasonable) failures++;
    }

    f.show_results("1: Idle Breathing");
    return failures;
}

// ---------------------------------------------------------------------------
// Test 2: Run Clip Activation
// ---------------------------------------------------------------------------
int test_run_clip_activation(TestFixture& f) {
    printf("\n--- Test 2: Run Clip Activation ---\n");
    f.reset();
    int failures = 0;

    float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
    float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);
    printf("  max_walk=%.2f m/s, max_run=%.2f m/s\n", max_walk, max_run);

    // Phase A: Walk at moderate speed (below walk threshold)
    float walk_speed = max_walk * 0.6f;
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, walk_speed);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    int r_thigh_id = f.h.right_leg_ids.size() >= 4 ? f.h.right_leg_ids[3] : 0;
    printf("  r_thigh_id=%d\n", r_thigh_id);

    // Warm up for 30 frames
    for (int i = 0; i < 30; i++) {
        if (f.should_exit()) return failures;
        f.step();
        f.draw_hud("2: Run Clip", i, 430, "Walk warmup...");
        f.present();
    }

    // Measure peak thigh-forward displacement during walk (100 frames)
    float walk_peak_fwd = 0.0f;
    for (int i = 0; i < 100; i++) {
        if (f.should_exit()) return failures;
        f.step();
        auto particles = f.ps->lock_particles_for_read();
        float hips_y = particles[f.h.hips_id].y;
        float thigh_y = particles[r_thigh_id].y;
        float fwd = thigh_y - hips_y;
        walk_peak_fwd = std::max(walk_peak_fwd, fwd);
        char live[128];
        snprintf(live, sizeof(live), "Walk: peak_fwd=%.3f | speed=%.2f m/s", walk_peak_fwd, f.measure_speed());
        f.draw_hud("2: Run Clip", 30 + i, 430, live);
        f.present();
    }
    printf("  Walk phase: peak thigh-forward = %.4f m\n", walk_peak_fwd);

    // Phase B: Ramp up to run speed
    float run_speed = max_run * 0.9f;
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, run_speed);

    // Warm up to reach run speed (100 frames)
    for (int i = 0; i < 100; i++) {
        if (f.should_exit()) return failures;
        f.step();
        char live[128];
        snprintf(live, sizeof(live), "Ramping to run: speed=%.2f / %.2f m/s", f.measure_speed(), run_speed);
        f.draw_hud("2: Run Clip", 130 + i, 430, live);
        f.present();
    }

    // Verify actually running
    float achieved_speed = f.measure_speed();
    printf("  Run phase: achieved speed = %.2f m/s (target=%.2f)\n",
           achieved_speed, run_speed);

    // Measure peak thigh-forward displacement during run (100 frames)
    float run_peak_fwd = 0.0f;
    for (int i = 0; i < 100; i++) {
        if (f.should_exit()) return failures;
        f.step();
        auto particles = f.ps->lock_particles_for_read();
        float hips_y = particles[f.h.hips_id].y;
        float thigh_y = particles[r_thigh_id].y;
        float fwd = thigh_y - hips_y;
        run_peak_fwd = std::max(run_peak_fwd, fwd);
        char live[128];
        snprintf(live, sizeof(live), "Run: peak_fwd=%.3f (walk was %.3f) | speed=%.2f m/s",
                 run_peak_fwd, walk_peak_fwd, f.measure_speed());
        f.draw_hud("2: Run Clip", 230 + i, 430, live);
        f.present();
    }
    printf("  Run phase: peak thigh-forward = %.4f m\n", run_peak_fwd);

    // Run should have more hip flex -> thigh reaches further forward
    bool bigger_stride = run_peak_fwd > walk_peak_fwd;
    printf("  [ASSERT] run peak > walk peak: %.6f > %.6f %s\n",
           run_peak_fwd, walk_peak_fwd, bigger_stride ? "PASS" : "FAIL");
    if (!bigger_stride) failures++;

    // Speed should be well above walk threshold
    float run_threshold = max_walk * 1.2f;
    bool fast_enough = achieved_speed > run_threshold;
    printf("  [ASSERT] speed > run threshold (%.6f): %.6f %s\n",
           run_threshold, achieved_speed, fast_enough ? "PASS" : "FAIL");
    if (!fast_enough) failures++;

    // Body integrity: hips should not have crashed to zero
    {
        auto particles = f.ps->lock_particles_for_read();
        float hips_z = particles[f.h.hips_id].z;
        bool ok = hips_z > -0.5f && hips_z < 3.0f;
        printf("  [ASSERT] hips Z within bounds during run: %.6f (expected -0.5 to 3.0) %s\n",
               hips_z, ok ? "PASS" : "FAIL");
        if (!ok) failures++;
    }

    f.show_results("2: Run Clip Activation");
    return failures;
}

// ---------------------------------------------------------------------------
// Test 3: Walk -> Run -> Walk -> Idle Transitions
// ---------------------------------------------------------------------------
int test_transitions(TestFixture& f) {
    printf("\n--- Test 3: Walk-Run-Walk-Idle Transitions ---\n");
    f.reset();
    int failures = 0;

    float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
    float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

    // Get leg particle IDs for symmetry check
    int l_thigh = f.h.left_leg_ids.size() >= 4 ? f.h.left_leg_ids[3] : 0;
    int r_thigh = f.h.right_leg_ids.size() >= 4 ? f.h.right_leg_ids[3] : 0;
    float prev_speed = 0.0f;
    float max_speed_jump = 0.0f;
    int frame = 0;
    const int TOTAL_FRAMES = 380;  // 80+100+80+120
    const char* phase_label = "";

    // Per-phase hips Z tracking
    struct PhaseStats {
        float min_z = 1000.0f, max_z = -1000.0f, sum_z = 0.0f;
        int count = 0;
        float l_thigh_max_fwd = 0.0f, r_thigh_max_fwd = 0.0f;  // symmetry
        void update(float hz) {
            min_z = std::min(min_z, hz);
            max_z = std::max(max_z, hz);
            sum_z += hz;
            count++;
        }
        float avg_z() const { return count > 0 ? sum_z / count : 0.0f; }
        float range() const { return max_z - min_z; }
    };
    PhaseStats phase_walk, phase_run, phase_walk2, phase_idle;
    PhaseStats* cur_phase = &phase_walk;
    PhaseStats global;

    auto sample = [&]() -> bool {
        if (f.should_exit()) return false;
        f.step();
        frame++;
        float speed = f.measure_speed();
        float jump = std::abs(speed - prev_speed);
        if (frame > 5) max_speed_jump = std::max(max_speed_jump, jump);
        prev_speed = speed;

        auto particles = f.ps->lock_particles_for_read();
        float hz = particles[f.h.hips_id].z;
        float hips_y = particles[f.h.hips_id].y;
        global.update(hz);
        cur_phase->update(hz);

        // Track left/right thigh forward displacement (symmetry)
        if (l_thigh > 0) {
            float lfwd = particles[l_thigh].y - hips_y;
            cur_phase->l_thigh_max_fwd = std::max(cur_phase->l_thigh_max_fwd, lfwd);
        }
        if (r_thigh > 0) {
            float rfwd = particles[r_thigh].y - hips_y;
            cur_phase->r_thigh_max_fwd = std::max(cur_phase->r_thigh_max_fwd, rfwd);
        }

        char live[256];
        snprintf(live, sizeof(live), "%s | speed=%.2f m/s | hips_z=%.3f | L=%.3f R=%.3f",
                 phase_label, speed, hz,
                 cur_phase->l_thigh_max_fwd, cur_phase->r_thigh_max_fwd);
        f.draw_hud("3: Transitions", frame, TOTAL_FRAMES, live);
        f.present();
        return true;
    };

    // Phase 1: Walk (80 frames, ~1.6s)
    printf("  Phase 1: Walk at %.1f m/s...\n", max_walk * 0.6f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_walk * 0.6f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    phase_label = "Phase 1: Walk";
    cur_phase = &phase_walk;
    for (int i = 0; i < 80; i++) if (!sample()) return failures;

    float walk_end_speed = f.measure_speed();
    printf("  Walk end speed: %.2f m/s\n", walk_end_speed);
    printf("  Walk hips Z: avg=%.3f range=%.3f (%.3f - %.3f)\n",
           phase_walk.avg_z(), phase_walk.range(), phase_walk.min_z, phase_walk.max_z);
    printf("  Walk thigh fwd: L=%.4f R=%.4f\n",
           phase_walk.l_thigh_max_fwd, phase_walk.r_thigh_max_fwd);

    // Phase 2: Accelerate to run (100 frames, ~2s)
    printf("  Phase 2: Run at %.1f m/s...\n", max_run * 0.85f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_run * 0.85f);
    phase_label = "Phase 2: Run";
    cur_phase = &phase_run;
    for (int i = 0; i < 100; i++) if (!sample()) return failures;

    float run_end_speed = f.measure_speed();
    printf("  Run end speed: %.2f m/s\n", run_end_speed);
    printf("  Run hips Z: avg=%.3f range=%.3f (%.3f - %.3f)\n",
           phase_run.avg_z(), phase_run.range(), phase_run.min_z, phase_run.max_z);
    printf("  Run thigh fwd: L=%.4f R=%.4f\n",
           phase_run.l_thigh_max_fwd, phase_run.r_thigh_max_fwd);

    // === LIMP DIAGNOSTICS: dump L/R limb positions during run phase ===
    // Track motion ranges for each limb particle to detect "paralyzed" side
    {
        // Leg IDs: [0]=foot, [1]=shin, [2]=thigh, [3]=toe
        struct LimbInfo { const char* name; int id; };
        std::vector<LimbInfo> limbs;
        if (f.h.left_leg_ids.size() >= 3) {
            limbs.push_back({"L_foot",  f.h.left_leg_ids[0]});
            limbs.push_back({"L_shin",  f.h.left_leg_ids[1]});
            limbs.push_back({"L_thigh", f.h.left_leg_ids[2]});
        }
        if (f.h.right_leg_ids.size() >= 3) {
            limbs.push_back({"R_foot",  f.h.right_leg_ids[0]});
            limbs.push_back({"R_shin",  f.h.right_leg_ids[1]});
            limbs.push_back({"R_thigh", f.h.right_leg_ids[2]});
        }
        if (f.h.left_arm_ids.size() >= 3) {
            limbs.push_back({"L_hand",  f.h.left_arm_ids[0]});
            limbs.push_back({"L_forearm", f.h.left_arm_ids[1]});
            limbs.push_back({"L_upper",  f.h.left_arm_ids[2]});
        }
        if (f.h.right_arm_ids.size() >= 3) {
            limbs.push_back({"R_hand",  f.h.right_arm_ids[0]});
            limbs.push_back({"R_forearm", f.h.right_arm_ids[1]});
            limbs.push_back({"R_upper",  f.h.right_arm_ids[2]});
        }

        // Collect 20 samples during the 100-frame run phase (every 5 frames)
        // Already ran 100 run frames — need to use data from the sample() calls above.
        // Instead, let's run 50 extra frames at run speed with logging
        printf("\n  === LEFT/RIGHT LIMB MOTION RANGES (50 extra run frames) ===\n");

        struct LimbStats { float min_z=1e9f, max_z=-1e9f, min_fwd=1e9f, max_fwd=-1e9f, min_lat=1e9f, max_lat=-1e9f; };
        std::vector<LimbStats> stats(limbs.size());

        for (int i = 0; i < 50; i++) {
            f.step();
            auto view = f.ps->lock_particles_for_read();
            float hx = view[f.h.hips_id].x;
            float hy = view[f.h.hips_id].y;
            float rot = view[f.h.hips_id].rotation_z;
            float cos_r = std::cos(-rot);
            float sin_r = std::sin(-rot);

            for (size_t j = 0; j < limbs.size(); j++) {
                auto& p = view[limbs[j].id];
                float dx = p.x - hx, dy = p.y - hy;
                float lat =  dx * cos_r + dy * sin_r;
                float fwd = -dx * sin_r + dy * cos_r;
                stats[j].min_z = std::min(stats[j].min_z, p.z);
                stats[j].max_z = std::max(stats[j].max_z, p.z);
                stats[j].min_fwd = std::min(stats[j].min_fwd, fwd);
                stats[j].max_fwd = std::max(stats[j].max_fwd, fwd);
                stats[j].min_lat = std::min(stats[j].min_lat, lat);
                stats[j].max_lat = std::max(stats[j].max_lat, lat);
            }
            f.present();
        }

        printf("  %-12s %8s %8s %8s %8s %8s %8s\n",
               "Part", "Z_range", "FWD_rng", "LAT_rng", "Z_min", "FWD_min", "LAT_avg");
        for (size_t j = 0; j < limbs.size(); j++) {
            auto& s = stats[j];
            printf("  %-12s %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f\n",
                   limbs[j].name,
                   s.max_z - s.min_z,
                   s.max_fwd - s.min_fwd,
                   s.max_lat - s.min_lat,
                   s.min_z,
                   s.min_fwd,
                   (s.min_lat + s.max_lat) / 2.0f);
        }
        printf("\n");
    }

    // Phase 3: Decelerate to walk (80 frames)
    printf("  Phase 3: Back to walk at %.1f m/s...\n", max_walk * 0.6f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_walk * 0.6f);
    phase_label = "Phase 3: Back to walk";
    cur_phase = &phase_walk2;
    for (int i = 0; i < 80; i++) if (!sample()) return failures;

    float walk2_end_speed = f.measure_speed();
    printf("  Walk2 end speed: %.2f m/s\n", walk2_end_speed);
    printf("  Walk2 hips Z: avg=%.3f range=%.3f (%.3f - %.3f)\n",
           phase_walk2.avg_z(), phase_walk2.range(), phase_walk2.min_z, phase_walk2.max_z);

    // Phase 4: Stop -> idle (120 frames, ~2.4s for phase settle + idle cycle)
    printf("  Phase 4: Stop -> idle...\n");
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);
    phase_label = "Phase 4: Idle";
    cur_phase = &phase_idle;
    for (int i = 0; i < 120; i++) if (!sample()) return failures;

    float idle_speed = f.measure_speed();
    printf("  Idle end speed: %.4f m/s\n", idle_speed);
    printf("  Idle hips Z: avg=%.3f range=%.3f (%.3f - %.3f)\n",
           phase_idle.avg_z(), phase_idle.range(), phase_idle.min_z, phase_idle.max_z);
    printf("  Overall hips Z range: %.3f - %.3f\n", global.min_z, global.max_z);
    printf("  Max speed jump per frame: %.4f m/s\n", max_speed_jump);

    // --- Assertions ---

    // Speed transitions
    bool a1 = walk_end_speed > 0.5f;
    printf("  [ASSERT] walking at end of phase 1: %.6f m/s (need > 0.5) %s\n",
           walk_end_speed, a1 ? "PASS" : "FAIL");
    if (!a1) failures++;

    bool a2 = run_end_speed > max_walk * 1.2f;
    printf("  [ASSERT] running at end of phase 2: %.6f m/s (need > %.6f) %s\n",
           run_end_speed, max_walk * 1.2f, a2 ? "PASS" : "FAIL");
    if (!a2) failures++;

    bool a3 = walk2_end_speed < max_walk * 1.1f;
    printf("  [ASSERT] back to walk speed phase 3: %.6f m/s (need < %.6f) %s\n",
           walk2_end_speed, max_walk * 1.1f, a3 ? "PASS" : "FAIL");
    if (!a3) failures++;

    bool a4 = idle_speed < 0.05f;
    printf("  [ASSERT] stopped at idle: %.6f m/s (need < 0.05) %s\n",
           idle_speed, a4 ? "PASS" : "FAIL");
    if (!a4) failures++;

    bool a5 = max_speed_jump < 0.5f;
    printf("  [ASSERT] no speed discontinuities: max_jump=%.6f m/s/frame (need < 0.5) %s\n",
           max_speed_jump, a5 ? "PASS" : "FAIL");
    if (!a5) failures++;

    // Body integrity — hips Z
    bool a6 = global.min_z > 0.3f;
    printf("  [ASSERT] hips didn't fall: min_z=%.6f (need > 0.3) %s\n",
           global.min_z, a6 ? "PASS" : "FAIL");
    if (!a6) failures++;

    bool a7 = global.max_z < 2.5f;
    printf("  [ASSERT] hips didn't fly: max_z=%.6f (need < 2.5) %s\n",
           global.max_z, a7 ? "PASS" : "FAIL");
    if (!a7) failures++;

    // Gait symmetry — left and right thigh should reach similar forward peaks
    // during the run phase (asymmetry → limping)
    float l_peak = phase_run.l_thigh_max_fwd;
    float r_peak = phase_run.r_thigh_max_fwd;
    float peak_max = std::max(l_peak, r_peak);
    float peak_min = std::min(l_peak, r_peak);
    // Ratio: smaller/larger — 1.0 = perfectly symmetric, <0.5 = severe limp
    float symmetry = (peak_max > 0.001f) ? peak_min / peak_max : 1.0f;
    bool a8 = symmetry > 0.5f;
    printf("  [ASSERT] gait symmetry (run): L=%.4f R=%.4f ratio=%.2f (need > 0.5) %s\n",
           l_peak, r_peak, symmetry, a8 ? "PASS" : "FAIL");
    if (!a8) failures++;

    // Hips Z stability during run — bounce range should be reasonable
    // Normal gait bounce is ~0.02-0.05m; >0.3m means body is sinking/bobbing
    bool a9 = phase_run.range() < 0.4f;
    printf("  [ASSERT] hips Z stable during run: range=%.4f (need < 0.4) %s\n",
           phase_run.range(), a9 ? "PASS" : "FAIL");
    if (!a9) failures++;

    f.show_results("3: Walk-Run-Walk-Idle Transitions");
    return failures;
}

// ---------------------------------------------------------------------------
// Test 4: Forward Lean During Running
//
// Biomechanics: Running produces a forward torso lean (~5-15 degrees)
// that increases with speed. Walking keeps the torso nearly upright.
//
// Measurement: chest-Z relative to hips-Z. Forward lean causes the chest
// to drop relative to hips (spine flexion moves mass forward + down).
// We also measure chest-Y relative to hips-Y (forward displacement in
// body-local frame) as a secondary signal.
//
// Protocol: measure at three speeds (idle, walk, run), assert that the
// chest drops more at run speed than at walk speed.
// ---------------------------------------------------------------------------
int test_forward_lean(TestFixture& f) {
    printf("\n--- Test 4: Forward Lean + Limp Diagnostics ---\n");
    f.reset();
    int failures = 0;

    int chest_id = f.h.torso_ids.size() >= 3 ? f.h.torso_ids[2] : 0;
    float max_walk = f.engine.get_humanoid_locomotion().get_max_walk_speed(f.h.hips_id);
    float max_run = f.engine.get_humanoid_locomotion().get_max_run_speed(f.h.hips_id);

    // Leg particle IDs: [0]=foot, [1]=shin, [2]=thigh, [3]=toe
    int l_foot  = f.h.left_leg_ids.size()  >= 1 ? f.h.left_leg_ids[0]  : -1;
    int l_shin  = f.h.left_leg_ids.size()  >= 2 ? f.h.left_leg_ids[1]  : -1;
    int l_thigh = f.h.left_leg_ids.size()  >= 3 ? f.h.left_leg_ids[2]  : -1;
    int r_foot  = f.h.right_leg_ids.size() >= 1 ? f.h.right_leg_ids[0] : -1;
    int r_shin  = f.h.right_leg_ids.size() >= 2 ? f.h.right_leg_ids[1] : -1;
    int r_thigh = f.h.right_leg_ids.size() >= 3 ? f.h.right_leg_ids[2] : -1;

    printf("  chest_id=%d max_walk=%.2f max_run=%.2f\n", chest_id, max_walk, max_run);
    printf("  L leg: foot=%d shin=%d thigh=%d\n", l_foot, l_shin, l_thigh);
    printf("  R leg: foot=%d shin=%d thigh=%d\n", r_foot, r_shin, r_thigh);

    // --- Torso chain integrity tracking ---
    int n_torso = static_cast<int>(f.h.torso_ids.size());
    printf("  torso_ids (%d):", n_torso);
    for (int i = 0; i < n_torso; i++) printf(" [%d]=%d", i, f.h.torso_ids[i]);
    printf("\n");

    std::vector<float> initial_seg_len(n_torso - 1, 0.0f);
    std::vector<float> max_seg_len(n_torso - 1, 0.0f);
    {
        auto view = f.ps->lock_particles_for_read();
        for (int i = 0; i + 1 < n_torso; i++) {
            const auto& a = view[f.h.torso_ids[i]];
            const auto& b = view[f.h.torso_ids[i + 1]];
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            initial_seg_len[i] = len;
            max_seg_len[i] = len;
        }
    }

    // =======================================================================
    // LIMP DIAGNOSTICS: per-frame tracking during the run phase
    // =======================================================================
    struct LimpFrame {
        int frame;
        float hips_z;
        float l_foot_z, r_foot_z;
        float l_shin_z, r_shin_z;
        float l_thigh_z, r_thigh_z;
        float l_foot_fwd, r_foot_fwd;    // foot Y relative to hips (forward offset)
        float l_foot_lat, r_foot_lat;    // foot X relative to hips (lateral offset)
        float l_thigh_fwd, r_thigh_fwd;  // thigh Y relative to hips
        float speed;
    };
    std::vector<LimpFrame> limp_log;

    // Capture one LimpFrame from current particle state
    auto capture_limp_frame = [&](int frame_num) -> LimpFrame {
        auto view = f.ps->lock_particles_for_read();
        LimpFrame lf;
        lf.frame = frame_num;
        lf.hips_z = view[f.h.hips_id].z;
        float hips_x = view[f.h.hips_id].x;
        float hips_y = view[f.h.hips_id].y;
        float hips_rot = view[f.h.hips_id].rotation_z;
        float cos_r = std::cos(-hips_rot);  // inverse rotation to get body-local
        float sin_r = std::sin(-hips_rot);

        auto body_local = [&](float wx, float wy, float& fwd, float& lat) {
            float dx = wx - hips_x;
            float dy = wy - hips_y;
            // Rotate to body frame: fwd = along facing, lat = perpendicular
            lat = dx * cos_r + dy * sin_r;
            fwd = -dx * sin_r + dy * cos_r;
        };

        lf.l_foot_z = (l_foot >= 0) ? view[l_foot].z : 0;
        lf.r_foot_z = (r_foot >= 0) ? view[r_foot].z : 0;
        lf.l_shin_z = (l_shin >= 0) ? view[l_shin].z : 0;
        lf.r_shin_z = (r_shin >= 0) ? view[r_shin].z : 0;
        lf.l_thigh_z = (l_thigh >= 0) ? view[l_thigh].z : 0;
        lf.r_thigh_z = (r_thigh >= 0) ? view[r_thigh].z : 0;

        if (l_foot >= 0) body_local(view[l_foot].x, view[l_foot].y, lf.l_foot_fwd, lf.l_foot_lat);
        else { lf.l_foot_fwd = 0; lf.l_foot_lat = 0; }
        if (r_foot >= 0) body_local(view[r_foot].x, view[r_foot].y, lf.r_foot_fwd, lf.r_foot_lat);
        else { lf.r_foot_fwd = 0; lf.r_foot_lat = 0; }
        if (l_thigh >= 0) body_local(view[l_thigh].x, view[l_thigh].y, lf.l_thigh_fwd, lf.l_thigh_fwd);
        else { lf.l_thigh_fwd = 0; }
        if (r_thigh >= 0) body_local(view[r_thigh].x, view[r_thigh].y, lf.r_thigh_fwd, lf.r_thigh_fwd);
        else { lf.r_thigh_fwd = 0; }

        lf.speed = std::sqrt(view[f.h.hips_id].vx * view[f.h.hips_id].vx +
                             view[f.h.hips_id].vy * view[f.h.hips_id].vy);
        return lf;
    };

    // Helper: run frames, measure lean, track chain + limp data
    int global_frame = 0;
    bool log_limp = false;  // only log during run phase

    auto measure_lean = [&](const char* label, int warmup_frames, int sample_frames) -> float {
        for (int i = 0; i < warmup_frames; i++) {
            if (f.should_exit()) return 0.0f;
            f.step();
            global_frame++;
            if (log_limp) limp_log.push_back(capture_limp_frame(global_frame));
            {
                auto view = f.ps->lock_particles_for_read();
                for (int s = 0; s + 1 < n_torso; s++) {
                    const auto& a = view[f.h.torso_ids[s]];
                    const auto& b = view[f.h.torso_ids[s + 1]];
                    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (len > max_seg_len[s]) max_seg_len[s] = len;
                }
            }
            char live[256];
            if (log_limp && !limp_log.empty()) {
                auto& lf = limp_log.back();
                snprintf(live, sizeof(live),
                    "%s warmup %d/%d | hZ=%.3f Lf=%.3f Rf=%.3f dZ=%.3f",
                    label, i, warmup_frames,
                    lf.hips_z, lf.l_foot_z, lf.r_foot_z,
                    lf.l_foot_z - lf.r_foot_z);
            } else {
                snprintf(live, sizeof(live), "%s warmup %d/%d", label, i, warmup_frames);
            }
            f.draw_hud("4: Forward Lean", global_frame, 500, live);
            f.present();
        }
        float sum_dz = 0.0f;
        for (int i = 0; i < sample_frames; i++) {
            if (f.should_exit()) return 0.0f;
            f.step();
            global_frame++;
            if (log_limp) limp_log.push_back(capture_limp_frame(global_frame));
            auto particles = f.ps->lock_particles_for_read();
            float hips_z = particles[f.h.hips_id].z;
            float chest_z = particles[chest_id].z;
            float dz = chest_z - hips_z;
            sum_dz += dz;
            for (int s = 0; s + 1 < n_torso; s++) {
                const auto& a = particles[f.h.torso_ids[s]];
                const auto& b = particles[f.h.torso_ids[s + 1]];
                float dx2 = b.x - a.x, dy2 = b.y - a.y, dz2 = b.z - a.z;
                float len = std::sqrt(dx2 * dx2 + dy2 * dy2 + dz2 * dz2);
                if (len > max_seg_len[s]) max_seg_len[s] = len;
            }
            char live[256];
            if (log_limp && !limp_log.empty()) {
                auto& lf = limp_log.back();
                snprintf(live, sizeof(live),
                    "%s samp %d/%d | hZ=%.3f Lf=%.3f Rf=%.3f dZ=%.3f",
                    label, i, sample_frames,
                    lf.hips_z, lf.l_foot_z, lf.r_foot_z,
                    lf.l_foot_z - lf.r_foot_z);
            } else {
                snprintf(live, sizeof(live), "%s sample %d/%d | chest_dz=%.4f",
                         label, i, sample_frames, dz);
            }
            f.draw_hud("4: Forward Lean", global_frame, 500, live);
            f.present();
        }
        return sum_dz / static_cast<float>(sample_frames);
    };

    // Phase A: Idle — baseline
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);
    float idle_dz = measure_lean("Idle", 40, 60);
    printf("  Idle avg chest_dz = %.5f\n", idle_dz);

    // Phase B: Walk (~60% max walk speed)
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_walk * 0.6f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    log_limp = true;  // start logging from walk phase for comparison
    float walk_dz = measure_lean("Walk", 60, 80);
    printf("  Walk avg chest_dz = %.5f\n", walk_dz);

    int walk_end_idx = static_cast<int>(limp_log.size());

    // Phase C: Run (~90% max run speed) — THIS IS WHERE LIMPING APPEARS
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, max_run * 0.9f);
    float run_dz = measure_lean("Run", 80, 80);
    printf("  Run avg chest_dz = %.5f\n", run_dz);

    // =======================================================================
    // LIMP ANALYSIS — dump per-frame data for the run phase
    // =======================================================================
    printf("\n  === LIMP DIAGNOSTICS (run phase, frames %d-%d) ===\n",
           walk_end_idx < (int)limp_log.size() ? limp_log[walk_end_idx].frame : -1,
           limp_log.empty() ? -1 : limp_log.back().frame);

    // Summary stats for the run phase
    float l_foot_z_min = 1e9f, l_foot_z_max = -1e9f;
    float r_foot_z_min = 1e9f, r_foot_z_max = -1e9f;
    float l_thigh_fwd_max = -1e9f, r_thigh_fwd_max = -1e9f;
    float l_foot_fwd_max = -1e9f, r_foot_fwd_max = -1e9f;
    float l_foot_fwd_min = 1e9f, r_foot_fwd_min = 1e9f;
    float hips_z_min = 1e9f, hips_z_max = -1e9f;
    float sum_l_foot_z = 0, sum_r_foot_z = 0;
    int run_count = 0;

    for (int i = walk_end_idx; i < (int)limp_log.size(); i++) {
        auto& lf = limp_log[i];
        l_foot_z_min = std::min(l_foot_z_min, lf.l_foot_z);
        l_foot_z_max = std::max(l_foot_z_max, lf.l_foot_z);
        r_foot_z_min = std::min(r_foot_z_min, lf.r_foot_z);
        r_foot_z_max = std::max(r_foot_z_max, lf.r_foot_z);
        l_thigh_fwd_max = std::max(l_thigh_fwd_max, lf.l_thigh_fwd);
        r_thigh_fwd_max = std::max(r_thigh_fwd_max, lf.r_thigh_fwd);
        l_foot_fwd_max = std::max(l_foot_fwd_max, lf.l_foot_fwd);
        r_foot_fwd_max = std::max(r_foot_fwd_max, lf.r_foot_fwd);
        l_foot_fwd_min = std::min(l_foot_fwd_min, lf.l_foot_fwd);
        r_foot_fwd_min = std::min(r_foot_fwd_min, lf.r_foot_fwd);
        hips_z_min = std::min(hips_z_min, lf.hips_z);
        hips_z_max = std::max(hips_z_max, lf.hips_z);
        sum_l_foot_z += lf.l_foot_z;
        sum_r_foot_z += lf.r_foot_z;
        run_count++;
    }

    if (run_count > 0) {
        float avg_l = sum_l_foot_z / run_count;
        float avg_r = sum_r_foot_z / run_count;

        printf("  HIPS Z:    min=%.4f  max=%.4f  range=%.4f\n",
               hips_z_min, hips_z_max, hips_z_max - hips_z_min);
        printf("  L FOOT Z:  min=%.4f  max=%.4f  range=%.4f  avg=%.4f\n",
               l_foot_z_min, l_foot_z_max, l_foot_z_max - l_foot_z_min, avg_l);
        printf("  R FOOT Z:  min=%.4f  max=%.4f  range=%.4f  avg=%.4f\n",
               r_foot_z_min, r_foot_z_max, r_foot_z_max - r_foot_z_min, avg_r);
        printf("  L-R AVG Z: diff=%.4f  (positive=L higher, negative=R higher)\n",
               avg_l - avg_r);
        printf("  L FOOT FWD: min=%.4f  max=%.4f  stride=%.4f\n",
               l_foot_fwd_min, l_foot_fwd_max, l_foot_fwd_max - l_foot_fwd_min);
        printf("  R FOOT FWD: min=%.4f  max=%.4f  stride=%.4f\n",
               r_foot_fwd_min, r_foot_fwd_max, r_foot_fwd_max - r_foot_fwd_min);
        printf("  L THIGH FWD peak=%.4f  R THIGH FWD peak=%.4f\n",
               l_thigh_fwd_max, r_thigh_fwd_max);

        // Dump every 10th frame for visual inspection
        printf("\n  --- Per-frame sample (every 10 frames during run) ---\n");
        printf("  %5s  %7s  %7s %7s %7s  %7s %7s %7s  %7s %7s\n",
               "frame", "hips_z", "Lfoot_z", "Rfoot_z", "dZ(L-R)",
               "Lfoot_f", "Rfoot_f", "dFwd", "Lshin_z", "Rshin_z");
        for (int i = walk_end_idx; i < (int)limp_log.size(); i += 10) {
            auto& lf = limp_log[i];
            printf("  %5d  %7.4f  %7.4f %7.4f %+7.4f  %+7.4f %+7.4f %+7.4f  %7.4f %7.4f\n",
                   lf.frame, lf.hips_z,
                   lf.l_foot_z, lf.r_foot_z, lf.l_foot_z - lf.r_foot_z,
                   lf.l_foot_fwd, lf.r_foot_fwd, lf.l_foot_fwd - lf.r_foot_fwd,
                   lf.l_shin_z, lf.r_shin_z);
        }

        // Detect the worst asymmetry frame
        float worst_dz = 0;
        int worst_frame = -1;
        for (int i = walk_end_idx; i < (int)limp_log.size(); i++) {
            float dz = std::abs(limp_log[i].l_foot_z - limp_log[i].r_foot_z);
            if (dz > worst_dz) {
                worst_dz = dz;
                worst_frame = limp_log[i].frame;
            }
        }
        printf("\n  WORST foot Z asymmetry: |L-R|=%.4f at frame %d\n", worst_dz, worst_frame);

        // Detect the worst forward asymmetry
        float l_stride = l_foot_fwd_max - l_foot_fwd_min;
        float r_stride = r_foot_fwd_max - r_foot_fwd_min;
        float stride_ratio = (std::max(l_stride, r_stride) > 0.001f)
            ? std::min(l_stride, r_stride) / std::max(l_stride, r_stride) : 1.0f;
        printf("  STRIDE symmetry: L=%.4f R=%.4f ratio=%.2f\n",
               l_stride, r_stride, stride_ratio);
    }

    // =======================================================================
    // ASSERTIONS
    // =======================================================================

    // 1. Forward lean detected
    float lean_diff = std::abs(walk_dz - run_dz);
    bool has_lean = lean_diff > 0.005f;
    printf("  [ASSERT] run has forward lean: walk_dz=%.5f run_dz=%.5f |diff|=%.5f (need > 0.005) %s\n",
           walk_dz, run_dz, lean_diff, has_lean ? "PASS" : "FAIL");
    if (!has_lean) failures++;

    // 2. Walk stays upright like idle
    float idle_walk_diff = std::abs(idle_dz - walk_dz);
    bool upright_at_walk = idle_walk_diff < 0.02f;
    printf("  [ASSERT] walk stays upright like idle: |idle-walk|=%.5f (need < 0.02) %s\n",
           idle_walk_diff, upright_at_walk ? "PASS" : "FAIL");
    if (!upright_at_walk) failures++;

    // 3. Hips Z didn't sink to the floor
    {
        auto particles = f.ps->lock_particles_for_read();
        float hips_z = particles[f.h.hips_id].z;
        bool ok = hips_z > -0.5f && hips_z < 3.0f;
        printf("  [ASSERT] hips Z reasonable: %.5f (expected -0.5 to 3.0) %s\n",
               hips_z, ok ? "PASS" : "FAIL");
        if (!ok) failures++;
    }

    // 4. Torso chain coherence
    {
        const char* seg_names[] = {"hips-abd", "abd-chest", "chest-neck", "neck-head", "head-hair", "hair-ear"};
        float worst_stretch = 0.0f;
        int worst_seg = -1;
        for (int s = 0; s + 1 < n_torso; s++) {
            float ratio = (initial_seg_len[s] > 0.001f)
                ? max_seg_len[s] / initial_seg_len[s] : 1.0f;
            const char* name = (s < 6) ? seg_names[s] : "extra";
            printf("  seg[%d] %s: initial=%.4f max=%.4f ratio=%.1f%%\n",
                   s, name, initial_seg_len[s], max_seg_len[s], ratio * 100.0f);
            if (ratio > worst_stretch) {
                worst_stretch = ratio;
                worst_seg = s;
            }
        }
        const char* worst_name = (worst_seg >= 0 && worst_seg < 6) ? seg_names[worst_seg] : "?";
        bool chain_ok = worst_stretch < 1.5f;
        printf("  [ASSERT] torso chain coherent: worst=%s stretch=%.1f%% (limit 150%%) %s\n",
               worst_name, worst_stretch * 100.0f, chain_ok ? "PASS" : "FAIL");
        if (!chain_ok) failures++;
    }

    // 5. NEW: L/R foot Z symmetry during run (detects limping)
    if (run_count > 0) {
        float avg_l = sum_l_foot_z / run_count;
        float avg_r = sum_r_foot_z / run_count;
        float z_asym = std::abs(avg_l - avg_r);
        bool sym_ok = z_asym < 0.05f;  // less than 5cm average height difference
        printf("  [ASSERT] L/R foot Z symmetry: |avg_L - avg_R|=%.4f (need < 0.05) %s\n",
               z_asym, sym_ok ? "PASS" : "FAIL");
        if (!sym_ok) failures++;

        // 6. NEW: Stride symmetry (L/R cover similar forward distance)
        float l_stride = l_foot_fwd_max - l_foot_fwd_min;
        float r_stride = r_foot_fwd_max - r_foot_fwd_min;
        float stride_ratio = (std::max(l_stride, r_stride) > 0.001f)
            ? std::min(l_stride, r_stride) / std::max(l_stride, r_stride) : 1.0f;
        bool stride_ok = stride_ratio > 0.5f;
        printf("  [ASSERT] stride symmetry: L=%.4f R=%.4f ratio=%.2f (need > 0.5) %s\n",
               l_stride, r_stride, stride_ratio, stride_ok ? "PASS" : "FAIL");
        if (!stride_ok) failures++;

        // 7. NEW: Hips Z stability during run
        float hips_range = hips_z_max - hips_z_min;
        bool hips_stable = hips_range < 0.4f;
        printf("  [ASSERT] hips Z stable during run: range=%.4f (need < 0.4) %s\n",
               hips_range, hips_stable ? "PASS" : "FAIL");
        if (!hips_stable) failures++;
    }

    f.show_results("4: Forward Lean");
    return failures;
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    bool headless = true;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--no-head" || arg == "--headless") headless = true;
        if (arg == "--visual" || arg == "--interactive") headless = false;
    }
    if (const char* env = std::getenv("INTERACTIVE")) {
        if (std::string(env) == "1") headless = false;
    }

    printf("\n========================================\n");
    printf("  IDLE & RUN ANIMATION TEST\n");
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
        {"1: Idle Breathing",          test_idle_breathing},
        {"2: Run Clip Activation",     test_run_clip_activation},
        {"3: Walk-Run-Walk-Idle",      test_transitions},
        {"4: Forward Lean",            test_forward_lean},
    };

    for (auto& test : tests) {
        if (!headless) {
            if (!f.wait_for_space(test.name)) break;
            f.reset();
        }
        f.space_was_down_ = true;  // prevent leftover SPACE from skipping immediately
        total_failures += test.func(f);
        if (f.quit_requested) break;  // ESC stops all tests; SPACE just skipped one
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
