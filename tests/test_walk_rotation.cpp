// Walk Rotation Test — FK Walk Verification
//
// Exercises the engine FK walk code paths through engine.update() + render(),
// validating rotation, spine blend, walk cycle, and phase decay.
// Every test includes three rotation sanity checks (per-frame sampling):
//
//   - No rotation jumps:  base_rotation delta < threshold per frame
//   - Upright:            hips pitch/yaw < 0.3 rad
//   - Facing coherence:   hips.rotation_z tracks base_rotation (all tests use look-at)
//
// Tests:
//   1. Body turn (B2)     — walk North + look East, verify monotonic turn
//   2. Spine blend (B1)   — walk + look sideways, verify neck/head twist
//   3. No micro-pauses    — walk North, verify continuous phase advance
//   4. Phase decay (A2)   — walk then stop, verify smooth phase return to neutral
//
// Controls (visual mode):
//   SPACE = advance to next test
//   ESC   = exit
//
// Usage:
//   ./build/logomancers/test-walk-rotation                        # headless
//   ./build/logomancers/test-walk-rotation --visual               # interactive
//   INTERACTIVE=1 ./build/logomancers/test-walk-rotation          # interactive (env)

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
// Shared setup: engine + humanoid with full registration
// ========================================================================
struct TestFixture {
    Engine engine;
    ParticleSystem* ps = nullptr;
    ParticleDynamicsSystem* dynamics = nullptr;
    PhysicsSystem* physics = nullptr;
    PhysicsHumanoidResult h;
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    bool headless = true;

    // Tracking worst drift across all tests
    float worst_pivot_error = 0.0f;
    std::string worst_pivot_joint;
    int worst_pivot_frame = 0;
    const char* worst_pivot_test = "";

    // Rotation sanity tracking
    struct RotationTrack {
        bool active = false;
        bool allow_turn = false;
        bool has_look_at = false;
        float prev_base_rotation = 0.0f;
        float max_rotation_delta = 0.0f;
        float max_abs_pitch = 0.0f;
        float max_abs_yaw = 0.0f;
        float max_facing_divergence = 0.0f;
        float prev_hips_rot_z = 0.0f;
        float max_hips_rot_z_delta = 0.0f;  // Visual spin detection
    } rot;

    bool init(bool headless_) {
        headless = headless_;
        EngineConfig config;
        config.create_display = !headless;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "Walk Rotation Test";
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

        // Lights — multiple for good visibility
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
        add_light(-3.0f, 0.0f, 3.0f, 300000.0f);

        // Camera — close to humanoid
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

        // Register walk clips — activates fk_walk_enabled
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

        entity_id = h.entity_id;
        return true;
    }

    // Reset humanoid to origin facing North, stopped
    void reset() {
        // Teleport hips back to origin
        {
            auto view = ps->lock_particles_for_write();
            view[h.hips_id].x = 0.0f;
            view[h.hips_id].y = 0.0f;
        }
        engine.get_humanoid_locomotion().reset_humanoid_position(h.hips_id);  // Sync prev_world tracking
        engine.get_humanoid_locomotion().set_facing_direction(h.hips_id, 0.0f);  // North
        engine.get_humanoid_locomotion().set_target_velocity(h.hips_id, 0.0f, 0.0f);
        engine.get_humanoid_locomotion().set_volitional(h.hips_id, false);
        engine.get_humanoid_locomotion().clear_look_at_target(h.hips_id);
        engine.get_humanoid_locomotion().zero_all_velocities(h.hips_id);
        // One step to let maintain_entity_shape snap all particles to new hips position
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

    // Run connectivity check, track worst, return failures
    int check_connectivity(const char* test_name, int frame, float tolerance = 0.05f) {
        auto validation = validate_humanoid(*ps, *physics, h, tolerance);
        if (validation.max_pivot_error > worst_pivot_error) {
            worst_pivot_error = validation.max_pivot_error;
            worst_pivot_joint = validation.worst_joint;
            worst_pivot_frame = frame;
            worst_pivot_test = test_name;
        }
        if (!validation.all_connected()) {
            printf("  [DRIFT] %s frame=%d\n", test_name, frame);
            validation.print_report();
            return 1;
        }
        return 0;
    }

    // Check hips haven't fallen through floor or flown away
    int check_hips_sanity(const char* test_name, int frame) {
        auto hips = ps->get_particle_copy(h.hips_id);
        int failures = 0;
        if (hips.z < -0.5f) {
            printf("  [ASSERT FAIL] %s frame=%d: hips fell through floor z=%.3f\n",
                   test_name, frame, hips.z);
            failures++;
        }
        if (hips.z > 5.0f) {
            printf("  [ASSERT FAIL] %s frame=%d: hips flew up z=%.3f\n",
                   test_name, frame, hips.z);
            failures++;
        }
        float dist_from_origin = std::sqrt(hips.x * hips.x + hips.y * hips.y);
        if (dist_from_origin > 15.0f) {
            printf("  [ASSERT FAIL] %s frame=%d: hips too far from origin d=%.3f (x=%.2f y=%.2f)\n",
                   test_name, frame, dist_from_origin, hips.x, hips.y);
            failures++;
        }
        return failures;
    }

    // ---- Rotation sanity checks ----

    void begin_rotation_tracking(bool allow_turn = false, bool has_look_at = false) {
        rot = {};
        rot.active = true;
        rot.allow_turn = allow_turn;
        rot.has_look_at = has_look_at;
        rot.prev_base_rotation = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        rot.prev_hips_rot_z = ps->get_particle_copy(h.hips_id).rotation_z;
    }

    void sample_rotation() {
        if (!rot.active) return;

        float base_rot = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        auto hips = ps->get_particle_copy(h.hips_id);

        float delta = std::abs(base_rot - rot.prev_base_rotation);
        if (delta > static_cast<float>(M_PI)) delta = 2.0f * static_cast<float>(M_PI) - delta;
        if (delta > rot.max_rotation_delta) rot.max_rotation_delta = delta;
        rot.prev_base_rotation = base_rot;

        float abs_pitch = std::abs(hips.rotation_x);
        float abs_yaw = std::abs(hips.rotation_y);
        if (abs_pitch > rot.max_abs_pitch) rot.max_abs_pitch = abs_pitch;
        if (abs_yaw > rot.max_abs_yaw) rot.max_abs_yaw = abs_yaw;

        float div = std::abs(hips.rotation_z - base_rot);
        if (div > static_cast<float>(M_PI)) div = 2.0f * static_cast<float>(M_PI) - div;
        if (div > rot.max_facing_divergence) rot.max_facing_divergence = div;

        // Visual spin: frame-to-frame hips.rotation_z delta
        float hips_rz_delta = std::abs(hips.rotation_z - rot.prev_hips_rot_z);
        if (hips_rz_delta > static_cast<float>(M_PI)) hips_rz_delta = 2.0f * static_cast<float>(M_PI) - hips_rz_delta;
        if (hips_rz_delta > rot.max_hips_rot_z_delta) rot.max_hips_rot_z_delta = hips_rz_delta;
        rot.prev_hips_rot_z = hips.rotation_z;
    }

    int resolve_rotation_asserts() {
        int failures = 0;

        float jump_threshold = rot.allow_turn ? 0.2f : 0.1f;
        bool jump_ok = rot.max_rotation_delta < jump_threshold;
        printf("  [ASSERT] No rotation jumps (delta < %.2f): max_delta=%.4f %s\n",
               jump_threshold, rot.max_rotation_delta, jump_ok ? "PASS" : "FAIL");
        if (!jump_ok) failures++;

        bool upright_ok = rot.max_abs_pitch < 0.3f && rot.max_abs_yaw < 0.3f;
        printf("  [ASSERT] Upright (pitch/yaw < 0.3): pitch=%.3f yaw=%.3f %s\n",
               rot.max_abs_pitch, rot.max_abs_yaw, upright_ok ? "PASS" : "FAIL");
        if (!upright_ok) failures++;

        if (rot.has_look_at) {
            float div_threshold = rot.allow_turn ? 1.0f : 0.3f;
            bool facing_ok = rot.max_facing_divergence < div_threshold;
            printf("  [ASSERT] Facing matches base_rotation (div < %.2f): div=%.3f %s\n",
                   div_threshold, rot.max_facing_divergence, facing_ok ? "PASS" : "FAIL");
            if (!facing_ok) failures++;
        } else {
            printf("  [ASSERT] Facing matches base_rotation: N/A (no look-at target) PASS\n");
        }

        // Visual spin: frame-to-frame hips.rotation_z must not change too fast
        float spin_threshold = rot.allow_turn ? 0.3f : 0.15f;
        bool spin_ok = rot.max_hips_rot_z_delta < spin_threshold;
        printf("  [ASSERT] No visual spin (hips rot_z delta < %.2f): max_delta=%.4f %s\n",
               spin_threshold, rot.max_hips_rot_z_delta, spin_ok ? "PASS" : "FAIL");
        if (!spin_ok) failures++;

        rot.active = false;
        return failures;
    }

    void draw_hud(const char* test_name, int frame, int total_frames,
                  const char* info, float pivot_err = 0.0f) {
        if (headless) return;
        auto& rs = engine.get_draw_surface();
        char buf[256];

        snprintf(buf, sizeof(buf), "Test: %s", test_name);
        rs.draw_text(20, 30, buf, 255, 255, 255);

        snprintf(buf, sizeof(buf), "Frame: %d / %d", frame, total_frames);
        rs.draw_text(20, 50, buf, 200, 200, 200);

        if (info[0] != '\0') {
            rs.draw_text(20, 70, info, 255, 255, 0);
        }

        // Hips position
        auto hips = ps->get_particle_copy(h.hips_id);
        snprintf(buf, sizeof(buf), "Hips: (%.2f, %.2f, %.2f)  rot_z=%.2f",
                 hips.x, hips.y, hips.z, hips.rotation_z);
        rs.draw_text(20, 95, buf, 180, 180, 255);

        // Pivot error
        uint8_t err_r = pivot_err > 0.02f ? 255 : 100;
        uint8_t err_g = pivot_err > 0.02f ? 80 : 255;
        snprintf(buf, sizeof(buf), "Pivot err: %.4f  worst: %.4f (%s)",
                 pivot_err, worst_pivot_error, worst_pivot_joint.c_str());
        rs.draw_text(20, 115, buf, err_r, err_g, 100);

        rs.draw_text(20, 920, "WALK ROTATION: Phase 1 FK Walk Verification  |  SPACE=next  ESC=exit", 140, 140, 140);
    }

    bool should_exit() {
        if (headless) return false;
        const auto& input_state = engine.get_input_system().get_input_state();
        return input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close();
    }

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
            rs.draw_text(20, 50, "ESC to exit", 200, 200, 200);
            rs.draw_text(20, 920, "WALK ROTATION: Phase 1 FK Walk Verification", 140, 140, 140);
            engine.present();
        }
    }
};

// ========================================================================
// Test 1: Body Turn During FK Walk (B2)
// Walk North + look East (5,0) for 120 frames. Verify base_rotation
// increases monotonically toward pi/2, walk displacement follows facing,
// and no connectivity drift between body parts.
// ========================================================================
int test_body_turn(TestFixture& f) {
    printf("\n--- Test 1: Body Turn During FK Walk (B2) ---\n");
    f.reset();
    f.step(); f.step(); f.present();

    // Baseline connectivity
    {
        auto baseline = validate_humanoid(*f.ps, *f.physics, f.h);
        printf("  [BASELINE] max_pivot_err=%.4f %s\n",
               baseline.max_pivot_error, baseline.all_connected() ? "OK" : "DISCONNECTED");
        if (!baseline.all_connected()) baseline.print_report();
    }

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 5.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);

    float prev_rotation = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);
    int monotonic_violations = 0;
    int drift_failures = 0;
    int sanity_failures = 0;
    const int FRAMES = 120;

    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();
        float rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

        // Periodic connectivity check
        float pivot_err = 0.0f;
        if (i % 10 == 0) {
            auto val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
            pivot_err = val.max_pivot_error;
            if (!val.all_connected()) {
                printf("  [DRIFT] frame=%d\n", i);
                val.print_report();
                drift_failures++;
            }
            if (val.max_pivot_error > f.worst_pivot_error) {
                f.worst_pivot_error = val.max_pivot_error;
                f.worst_pivot_joint = val.worst_joint;
                f.worst_pivot_frame = i;
                f.worst_pivot_test = "Body Turn";
            }
            sanity_failures += f.check_hips_sanity("Body Turn", i);
        }

        char info[128];
        snprintf(info, sizeof(info), "base_rot=%.2f rad (%.0f deg)  target=pi/2",
                 rot, rot * 180.0f / static_cast<float>(M_PI));
        f.draw_hud("1: Body Turn (B2)", i, FRAMES, info, pivot_err);
        f.present();

        if (rot < prev_rotation - 0.001f) {
            monotonic_violations++;
            if (monotonic_violations <= 3) {
                printf("  [WARN] Frame %d: rotation regressed %.4f -> %.4f\n", i, prev_rotation, rot);
            }
        }
        prev_rotation = rot;
    }

    // Post-test connectivity
    auto final_val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
    printf("  [POST] max_pivot_err=%.4f at %s %s\n",
           final_val.max_pivot_error, final_val.worst_joint.c_str(),
           final_val.all_connected() ? "OK" : "DISCONNECTED");
    if (!final_val.all_connected()) {
        final_val.print_report();
        drift_failures++;
    }

    float final_rotation = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);
    float target_angle = static_cast<float>(M_PI) / 2.0f;
    int failures = drift_failures + sanity_failures;

    // Read final hips position to check walk direction
    float final_hips_x, final_hips_y;
    {
        auto view = f.ps->lock_particles_for_read();
        final_hips_x = view[f.h.hips_id].x;
        final_hips_y = view[f.h.hips_id].y;
    }

    bool turn_ok = final_rotation > 0.5f;
    printf("  [ASSERT] base_rotation > 0.5 rad: %.4f %s\n",
           final_rotation, turn_ok ? "PASS" : "FAIL");
    if (!turn_ok) failures++;

    bool direction_ok = final_rotation > 0.0f && final_rotation < target_angle + 0.5f;
    printf("  [ASSERT] Turning toward East (0 < rot < pi): %.4f %s\n",
           final_rotation, direction_ok ? "PASS" : "FAIL");
    if (!direction_ok) failures++;

    // Walk direction must follow body rotation — if body turned East,
    // hips.x must increase (humanoid walks where it's facing)
    bool walk_follows_facing = final_hips_x > 0.5f;
    printf("  [ASSERT] Walk follows facing (hips.x > 0.5): x=%.3f y=%.3f %s\n",
           final_hips_x, final_hips_y, walk_follows_facing ? "PASS" : "FAIL");
    if (!walk_follows_facing) failures++;

    bool mono_ok = monotonic_violations <= 3;
    printf("  [ASSERT] Monotonic turn (violations <= 3): %d %s\n",
           monotonic_violations, mono_ok ? "PASS" : "FAIL");
    if (!mono_ok) failures++;

    bool conn_ok = drift_failures == 0;
    printf("  [ASSERT] No connectivity drift: %d disconnects %s\n",
           drift_failures, conn_ok ? "PASS" : "FAIL");

    bool sanity_ok = sanity_failures == 0;
    printf("  [ASSERT] Hips sanity (no fall/fly): %s\n", sanity_ok ? "PASS" : "FAIL");

    failures += f.resolve_rotation_asserts();
    return failures;
}

// ========================================================================
// Test 2: Additive Spine Blend (B1)
// Walk North + look East (3,0) for 30 frames. Verify neck and head have
// active twist targets with positive angles (looking right). Tests the
// save/clear/apply/re-add twist pipeline during FK walk.
// ========================================================================
int test_spine_blend(TestFixture& f) {
    printf("\n--- Test 2: Additive Spine Blend (B1) ---\n");
    f.reset();
    f.step(); f.step(); f.present();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 3.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);

    int drift_failures = 0;
    int sanity_failures = 0;
    const int FRAMES = 30;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float neck_tw = 0, head_tw = 0;
        f.engine.get_humanoid_locomotion().get_joint_twist(f.h.hips_id, "neck", neck_tw);
        f.engine.get_humanoid_locomotion().get_joint_twist(f.h.hips_id, "head", head_tw);

        float pivot_err = 0.0f;
        if (i % 10 == 0) {
            auto val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
            pivot_err = val.max_pivot_error;
            if (!val.all_connected()) {
                printf("  [DRIFT] frame=%d\n", i);
                val.print_report();
                drift_failures++;
            }
            if (val.max_pivot_error > f.worst_pivot_error) {
                f.worst_pivot_error = val.max_pivot_error;
                f.worst_pivot_joint = val.worst_joint;
                f.worst_pivot_frame = i;
                f.worst_pivot_test = "Spine Blend";
            }
            sanity_failures += f.check_hips_sanity("Spine Blend", i);
        }

        char info[128];
        snprintf(info, sizeof(info), "neck_twist=%.2f  head_twist=%.2f", neck_tw, head_tw);
        f.draw_hud("2: Spine Blend (B1)", i, FRAMES, info, pivot_err);
        f.present();
    }

    // Post-test connectivity
    auto final_val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
    printf("  [POST] max_pivot_err=%.4f at %s %s\n",
           final_val.max_pivot_error, final_val.worst_joint.c_str(),
           final_val.all_connected() ? "OK" : "DISCONNECTED");
    if (!final_val.all_connected()) {
        final_val.print_report();
        drift_failures++;
    }

    int failures = drift_failures + sanity_failures;

    float neck_twist = 0.0f;
    bool neck_has_twist = f.engine.get_humanoid_locomotion().get_joint_twist(f.h.hips_id, "neck", neck_twist);
    printf("  [ASSERT] Neck has twist target: %s (angle=%.4f)\n",
           neck_has_twist ? "PASS" : "FAIL", neck_twist);
    if (!neck_has_twist) failures++;

    float head_twist = 0.0f;
    bool head_has_twist = f.engine.get_humanoid_locomotion().get_joint_twist(f.h.hips_id, "head", head_twist);
    printf("  [ASSERT] Head has twist target: %s (angle=%.4f)\n",
           head_has_twist ? "PASS" : "FAIL", head_twist);
    if (!head_has_twist) failures++;

    if (neck_has_twist) {
        bool sign_ok = neck_twist > 0.01f;
        printf("  [ASSERT] Neck twist > 0 (looking East): %.4f %s\n",
               neck_twist, sign_ok ? "PASS" : "FAIL");
        if (!sign_ok) failures++;
    }

    bool conn_ok = drift_failures == 0;
    printf("  [ASSERT] No connectivity drift: %d disconnects %s\n",
           drift_failures, conn_ok ? "PASS" : "FAIL");

    failures += f.resolve_rotation_asserts();
    return failures;
}

// ========================================================================
// Test 3: No Micro-Pauses (A1)
// Walk North + look North (0,5) for 60 frames (after 5 warmup frames).
// Verify walk phase advances continuously (< 3 stall frames) and total
// phase advance > 2 rad. Catches the volitional-gate bug where walk
// cycle would stall between steps.
// ========================================================================
int test_no_micropause(TestFixture& f) {
    printf("\n--- Test 3: No Micro-Pauses (A1) ---\n");
    f.reset();
    f.step(); f.step(); f.present();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 5.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    for (int i = 0; i < 5; ++i) { f.step(); f.sample_rotation(); }

    int drift_failures = 0;
    int sanity_failures = 0;
    const int FRAMES = 60;
    std::vector<float> phases(FRAMES);

    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();
        phases[i] = f.engine.get_humanoid_locomotion().get_walk_phase(f.h.hips_id);

        float pivot_err = 0.0f;
        if (i % 10 == 0) {
            auto val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
            pivot_err = val.max_pivot_error;
            if (!val.all_connected()) {
                printf("  [DRIFT] frame=%d\n", i);
                val.print_report();
                drift_failures++;
            }
            if (val.max_pivot_error > f.worst_pivot_error) {
                f.worst_pivot_error = val.max_pivot_error;
                f.worst_pivot_joint = val.worst_joint;
                f.worst_pivot_frame = i;
                f.worst_pivot_test = "No Micropause";
            }
            sanity_failures += f.check_hips_sanity("No Micropause", i);
        }

        char info[128];
        snprintf(info, sizeof(info), "walk_phase=%.2f rad (%.0f deg)",
                 phases[i], phases[i] * 180.0f / static_cast<float>(M_PI));
        f.draw_hud("3: No Micro-Pauses (A1)", i, FRAMES, info, pivot_err);
        f.present();
    }

    // Post-test connectivity
    auto final_val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
    printf("  [POST] max_pivot_err=%.4f at %s %s\n",
           final_val.max_pivot_error, final_val.worst_joint.c_str(),
           final_val.all_connected() ? "OK" : "DISCONNECTED");
    if (!final_val.all_connected()) {
        final_val.print_report();
        drift_failures++;
    }

    int phase_stall_count = 0;
    for (int i = 1; i < FRAMES; ++i) {
        float diff = phases[i] - phases[i-1];
        if (diff < 0) diff += 2.0f * static_cast<float>(M_PI);
        if (diff < 0.001f) {
            phase_stall_count++;
            if (phase_stall_count <= 3) {
                printf("  [WARN] Phase stall at frame %d: %.4f -> %.4f\n",
                       i, phases[i-1], phases[i]);
            }
        }
    }

    int failures = drift_failures + sanity_failures;

    bool stall_ok = phase_stall_count <= 3;
    printf("  [ASSERT] Phase stalls <= 3: %d %s\n",
           phase_stall_count, stall_ok ? "PASS" : "FAIL");
    if (!stall_ok) failures++;

    float total_advance = 0.0f;
    for (int i = 1; i < FRAMES; ++i) {
        float diff = phases[i] - phases[i-1];
        if (diff < 0) diff += 2.0f * static_cast<float>(M_PI);
        total_advance += diff;
    }
    bool advance_ok = total_advance > 2.0f;
    printf("  [ASSERT] Total phase advance > 2 rad: %.2f %s\n",
           total_advance, advance_ok ? "PASS" : "FAIL");
    if (!advance_ok) failures++;

    bool conn_ok = drift_failures == 0;
    printf("  [ASSERT] No connectivity drift: %d disconnects %s\n",
           drift_failures, conn_ok ? "PASS" : "FAIL");

    failures += f.resolve_rotation_asserts();
    return failures;
}

// ========================================================================
// Test 4: Phase Decay to Neutral (A2)
// Walk 60 frames, then stop and observe 60 frames of phase decay.
// Verify phase returns to neutral (0 or pi within 0.1 rad) and decays
// smoothly (no jumps > 0.25 rad). Ensures feet land naturally when stopping.
// ========================================================================
int test_phase_decay(TestFixture& f) {
    printf("\n--- Test 4: Phase Decay to Neutral (A2) ---\n");
    f.reset();
    f.step(); f.step(); f.present();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 5.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    int drift_failures = 0;
    int sanity_failures = 0;

    // Walk phase: 60 frames
    for (int i = 0; i < 60; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();
        float phase = f.engine.get_humanoid_locomotion().get_walk_phase(f.h.hips_id);

        float pivot_err = 0.0f;
        if (i % 10 == 0) {
            auto val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
            pivot_err = val.max_pivot_error;
            if (!val.all_connected()) {
                printf("  [DRIFT] walk frame=%d\n", i);
                val.print_report();
                drift_failures++;
            }
            if (val.max_pivot_error > f.worst_pivot_error) {
                f.worst_pivot_error = val.max_pivot_error;
                f.worst_pivot_joint = val.worst_joint;
                f.worst_pivot_frame = i;
                f.worst_pivot_test = "Phase Decay (walk)";
            }
            sanity_failures += f.check_hips_sanity("Phase Decay (walk)", i);
        }

        char info[128];
        snprintf(info, sizeof(info), "WALKING  phase=%.2f rad", phase);
        f.draw_hud("4: Phase Decay (A2)", i, 120, info, pivot_err);
        f.present();
    }

    float phase_before_stop = f.engine.get_humanoid_locomotion().get_walk_phase(f.h.hips_id);
    printf("  Phase before stop: %.4f rad\n", phase_before_stop);

    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);

    // Decay phase: 60 frames
    std::vector<float> decay_phases;
    for (int i = 0; i < 60; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();
        float phase = f.engine.get_humanoid_locomotion().get_walk_phase(f.h.hips_id);
        decay_phases.push_back(phase);

        float pivot_err = 0.0f;
        if (i % 10 == 0) {
            auto val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
            pivot_err = val.max_pivot_error;
            if (!val.all_connected()) {
                printf("  [DRIFT] decay frame=%d\n", i);
                val.print_report();
                drift_failures++;
            }
            sanity_failures += f.check_hips_sanity("Phase Decay (decay)", i);
        }

        char info[128];
        snprintf(info, sizeof(info), "STOPPED  phase=%.2f rad (decaying)", phase);
        f.draw_hud("4: Phase Decay (A2)", 60 + i, 120, info, pivot_err);
        f.present();
    }

    // Post-test connectivity
    auto final_val = validate_humanoid(*f.ps, *f.physics, f.h, 0.05f);
    printf("  [POST] max_pivot_err=%.4f at %s %s\n",
           final_val.max_pivot_error, final_val.worst_joint.c_str(),
           final_val.all_connected() ? "OK" : "DISCONNECTED");
    if (!final_val.all_connected()) {
        final_val.print_report();
        drift_failures++;
    }

    float final_phase = decay_phases.back();
    printf("  Phase after decay: %.4f rad\n", final_phase);

    int failures = drift_failures + sanity_failures;

    float pi_f = static_cast<float>(M_PI);
    float dist_to_zero = std::min(std::abs(final_phase), 2.0f * pi_f - std::abs(final_phase));
    float dist_to_pi = std::abs(final_phase - pi_f);
    float nearest_neutral = std::min(dist_to_zero, dist_to_pi);

    bool decay_ok = nearest_neutral < 0.1f;
    printf("  [ASSERT] Phase at neutral (0 or pi), dist=%.4f: %s\n",
           nearest_neutral, decay_ok ? "PASS" : "FAIL");
    if (!decay_ok) failures++;

    int jump_count = 0;
    for (size_t i = 1; i < decay_phases.size(); ++i) {
        float diff = std::abs(decay_phases[i] - decay_phases[i-1]);
        if (diff > pi_f) diff = 2.0f * pi_f - diff;
        if (diff > 0.25f) {
            jump_count++;
            if (jump_count <= 3) {
                printf("  [WARN] Phase jump at frame %zu: %.4f -> %.4f (diff=%.4f)\n",
                       i, decay_phases[i-1], decay_phases[i], diff);
            }
        }
    }
    bool smooth_ok = jump_count == 0;
    printf("  [ASSERT] No phase jumps > 0.25 rad: %d jumps %s\n",
           jump_count, smooth_ok ? "PASS" : "FAIL");
    if (!smooth_ok) failures++;

    bool conn_ok = drift_failures == 0;
    printf("  [ASSERT] No connectivity drift: %d disconnects %s\n",
           drift_failures, conn_ok ? "PASS" : "FAIL");

    failures += f.resolve_rotation_asserts();
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
    printf("  WALK ROTATION TEST\n");
    printf("  Phase 1 FK Walk Verification\n");
    printf("  Mode: %s\n", headless ? "Headless" : "Visual");
    if (!headless) printf("  SPACE = next test, ESC = exit\n");
    printf("========================================\n");

    TestFixture f;
    if (!f.init(headless)) return 1;

    // Baseline connectivity before any test
    {
        auto baseline = validate_humanoid(*f.ps, *f.physics, f.h);
        printf("\n[BASELINE CONNECTIVITY]\n");
        baseline.print_report();
    }

    int total_failures = 0;

    struct TestEntry {
        const char* name;
        int (*func)(TestFixture&);
    };

    TestEntry tests[] = {
        {"1: Body Turn (B2)",       test_body_turn},
        {"2: Spine Blend (B1)",     test_spine_blend},
        {"3: No Micro-Pauses (A1)", test_no_micropause},
        {"4: Phase Decay (A2)",     test_phase_decay},
    };

    for (auto& test : tests) {
        if (!headless) {
            if (!f.wait_for_space(test.name)) break;
            f.reset();  // Reset humanoid to origin before each test
        }
        total_failures += test.func(f);
    }

    if (!headless) {
        f.wait_for_space("EXIT (or press ESC)");
    }

    printf("\n========================================\n");
    printf("  WALK ROTATION TEST COMPLETE\n");
    printf("========================================\n");
    printf("  Worst pivot error: %.4f at %s (frame %d, test: %s)\n",
           f.worst_pivot_error, f.worst_pivot_joint.c_str(),
           f.worst_pivot_frame, f.worst_pivot_test);

    if (total_failures > 0) {
        printf("  *** %d ASSERTION FAILURES ***\n", total_failures);
        return 1;
    }
    printf("  ALL ASSERTIONS PASSED\n");
    return 0;
}
