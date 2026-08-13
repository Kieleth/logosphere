// Humanoid Movement Test Suite
//
// End-to-end movement validation: spawns a humanoid with full FK walk
// registration and runs it through five scenarios. Each test measures
// domain-specific assertions (speed, displacement, z-height) plus three
// rotation sanity checks that run every frame:
//
//   - No rotation jumps:  base_rotation delta < 0.1 rad/frame (0.2 for turns)
//   - Upright:            hips pitch/yaw < 0.3 rad
//   - Facing coherence:   hips.rotation_z tracks base_rotation (look-at only)
//
// Tests:
//   1. Speed achievement    — ramp to 1.0/1.5/2.0/3.0 m/s in 60 frames each
//   2. Direction after turn — walk North + look East, verify displacement
//   3. Grass traversal      — walk through 40 grass blades, no stall/climb
//   4. Deceleration/stop    — walk 60 frames, stop, measure braking distance
//   5. Step vs grass        — solid step climbs, grass does not
//   6. Diagonal walking     — walk NE at (1.5, 1.5), verify 45° displacement
//   7. Start-stop-start     — walk, stop, walk again, verify clean restart
//   8. Impulse knockback    — walk + apply_impulse, verify disruption & recovery
//   9. Southward walking    — face South, walk at 60% speed (Eden backward mult)
//  10. Eastward walking     — face East, walk at 70% speed (Eden strafe mult)
//  11. Direction change     — walk North 60f then switch to East 90f
//  12. Backward (body-rel)  — body faces North, walks South via body-relative API
//  13. Strafe right (body-rel) — body faces North, moves East via body-relative API
//  14. Strafe left (body-rel)  — body faces North, moves West via body-relative API
//  15. Forward body-rel     — body-relative forward matches world-space forward
//  16. Body-rel with turn   — body turns East via look-at, movement follows
//
// Collision filtering (engine-level):
//   - Material filter: LEAVES and LIGHT skip collision response
//   - Size filter: obstacles < 0.2m in both width and height are ignored
//   Both filters live in ParticleDynamicsSystem::handle_collision_events()
//
// Controls (visual mode):
//   SPACE = advance to next test
//   ESC   = exit
//
// Usage:
//   ./build/logomancers/test-humanoid-movement                        # headless
//   ./build/logomancers/test-humanoid-movement --visual               # interactive
//   INTERACTIVE=1 ./build/logomancers/test-humanoid-movement          # interactive (env)

#include "core/engine.h"
#include <cstdarg>
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/force.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "humanoid_validator.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles
#include <chrono>
#include <thread>

// ========================================================================
// Visual assertion tracking — shown live on HUD
// ========================================================================
struct VisualAssert {
    std::string label;
    std::string value;      // Current measured value as string
    bool resolved = false;  // Has been evaluated
    bool passed = false;
};

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

    // Visual HUD state
    std::string test_intent;
    std::vector<VisualAssert> asserts;

    // Rotation sanity tracking — catches spinning, flipping, jerking that might
    // pass movement assertions but be visually broken. Samples every frame.
    //
    // Usage per test:
    //   f.add_rotation_asserts();                          // before sim loop
    //   f.begin_rotation_tracking(allow_turn, has_look_at); // after reset, before sim
    //   f.sample_rotation();                               // inside sim loop
    //   failures += f.resolve_rotation_asserts();          // after sim loop
    struct RotationTrack {
        bool active = false;
        bool allow_turn = false;
        bool has_look_at = false;  // Only check facing divergence when look-at is active
        int frame_count = 0;
        float prev_base_rotation = 0.0f;
        float max_rotation_delta = 0.0f;   // Largest frame-to-frame base_rotation jump
        float max_abs_pitch = 0.0f;        // Max |rotation_x| (tipping forward/back)
        float max_abs_yaw = 0.0f;          // Max |rotation_y| (tipping sideways)
        float max_facing_divergence = 0.0f; // Max |rotation_z - base_rotation|
        int rot_assert_jump = -1;
        int rot_assert_upright = -1;
        int rot_assert_facing = -1;
        float prev_hips_rot_z = 0.0f;
        float max_hips_rot_z_delta = 0.0f;  // Visual spin detection
        int rot_assert_spin = -1;
    } rot;

    // Body integrity tracking — catches limb collapse, deformity, arm folding
    // into torso. Measures body-local lateral offset of each arm particle from
    // the hips centerline every frame.
    //
    // Usage per test:
    //   f.add_body_integrity_asserts();                     // before sim loop
    //   f.begin_body_integrity_tracking();                  // after reset, before sim
    //   f.sample_body_integrity();                          // inside sim loop
    //   failures += f.resolve_body_integrity_asserts();     // after sim loop
    struct BodyIntegrityTrack {
        bool active = false;
        int frame_count = 0;

        // Minimum lateral offset of any right arm particle from hips (should stay positive)
        float min_right_arm_spread = 999.0f;
        int min_right_arm_frame = 0;
        // Minimum lateral offset of any left arm particle from hips (should stay negative, track abs)
        float min_left_arm_spread = 999.0f;
        int min_left_arm_frame = 0;

        // Arm chain length tracking (shoulder → hand via segments)
        float initial_right_arm_length = 0.0f;
        float initial_left_arm_length = 0.0f;
        float min_right_arm_length = 999.0f;
        float min_left_arm_length = 999.0f;

        // Assertion slot indices
        int assert_right_spread = -1;
        int assert_left_spread = -1;
        int assert_right_chain = -1;
        int assert_left_chain = -1;
    } body;

    bool init(bool headless_) {
        headless = headless_;
        EngineConfig config;
        config.create_display = !headless;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "Humanoid Movement Test";
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
        floor.width = 40.0f; floor.height = 40.0f; floor.thickness = 0.1f;
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
        HumanoidGenerator humanoid_gen;
        humanoid_gen.initialize(&engine, &kg);
        HumanoidSpec spec = HumanoidSpec::hunter();
        h = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, 0.0f, -1, spec, false);

        // Build the KG body graph BEFORE registering with entity_id.
        // register_humanoid_direct(entity_id) derives DynamicsParams from
        // the KG capability profile; an entity with no body-part graph
        // aggregates locomotion_factor = 0, so max_run_speed = 0 and the
        // walker covers 0 m in every scenario (task #42 RCA: the log line
        // "[CAP] Entity N ... loco=0 ... " was the tell).
        h.create_kg_entities(kg, "Humanoid", 150.0f, 600.0f);

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

        // Register strafe clips (rightward strafe; left strafe is mirror)
        auto strafe_r = create_fk_strafe_step(Side::RIGHT, 1.0f);
        auto strafe_l = create_fk_strafe_step(Side::LEFT, 1.0f);
        engine.get_humanoid_locomotion().register_strafe_clips(h.hips_id, strafe_r, strafe_l);

        // Register turn-in-place clips
        auto turn_r = create_fk_turn_step(Side::RIGHT, 1.0f);
        auto turn_l = create_fk_turn_step(Side::LEFT, -1.0f);
        engine.get_humanoid_locomotion().register_turn_clips(h.hips_id, turn_r, turn_l);

        entity_id = h.entity_id;
        return true;
    }

    // Begin a new test — sets intent and clears assertions
    void begin_test(const char* intent) {
        test_intent = intent;
        asserts.clear();
    }

    // Add an assertion slot (unresolved, shown as "..." on HUD)
    int add_assert(const char* label) {
        asserts.push_back({label, "...", false, false});
        return static_cast<int>(asserts.size()) - 1;
    }

    // Update the live value display for an assertion (before resolving)
    void update_assert_value(int idx, const char* fmt, ...) {
        if (idx < 0 || idx >= static_cast<int>(asserts.size())) return;
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        asserts[idx].value = buf;
    }

    // Resolve an assertion — marks pass/fail, logs to stdout
    bool resolve_assert(int idx, bool passed, const char* fmt, ...) {
        if (idx < 0 || idx >= static_cast<int>(asserts.size())) return passed;
        asserts[idx].resolved = true;
        asserts[idx].passed = passed;

        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        asserts[idx].value = buf;

        printf("  [ASSERT] %s: %s %s\n", asserts[idx].label.c_str(), buf, passed ? "PASS" : "FAIL");
        return passed;
    }

    // Reset humanoid to origin facing North, stopped.
    // 3 stabilization steps let FK walk settle before test begins.
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
        step(); step(); step();
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

    float measure_speed() {
        auto p = ps->get_particle_copy(h.hips_id);
        return std::sqrt(p.vx * p.vx + p.vy * p.vy);
    }

    void get_hips_pos(float& x, float& y, float& z) {
        auto p = ps->get_particle_copy(h.hips_id);
        x = p.x; y = p.y; z = p.z;
    }

    void add_grass_blade(float x, float y, bool tall) {
        Particle g = {};
        g.x = x; g.y = y;
        g.shape = ParticleShape::BOX;
        g.a = 1.0f;
        g.is_at_rest = true;
        g.SetMaterial(Materials::Type::LEAVES);
        if (tall) {
            g.width = 0.025f + (std::rand() % 100) * 0.00015f;
            g.height = 0.025f + (std::rand() % 100) * 0.00015f;
            g.thickness = 0.30f + (std::rand() % 100) * 0.002f;
            g.r = 0.10f; g.g = 0.35f; g.b = 0.08f;
        } else {
            g.width = 0.015f + (std::rand() % 100) * 0.0002f;
            g.height = 0.015f + (std::rand() % 100) * 0.0002f;
            g.thickness = 0.12f + (std::rand() % 100) * 0.0028f;
            g.r = 0.15f; g.g = 0.45f; g.b = 0.10f;
        }
        // Root the blade IN the floor slab (z 0..0.1): bottom lands at a
        // random depth inside it, never below the turtle. The old fixed-z
        // draws picked z and thickness independently, so tall blades
        // dipped up to 5 cm under the world floor.
        g.z = g.thickness * 0.5f + (std::rand() % 100) * 0.001f;
        engine.add_particle(g);
    }

    void add_grass_field(float x_center, float y_start, float y_end, int count) {
        float spread = 1.5f;
        for (int i = 0; i < count; i++) {
            float gx = x_center + (std::rand() % 200 - 100) * 0.01f * spread;
            float gy = y_start + (std::rand() % 100) * 0.01f * (y_end - y_start);
            add_grass_blade(gx, gy, std::rand() % 3 == 0);
        }
    }

    int add_step_block(float x, float y, float top_z) {
        Particle block = {};
        block.x = x; block.y = y;
        block.z = top_z / 2.0f;
        block.shape = ParticleShape::BOX;
        block.width = 0.5f; block.height = 0.5f;
        block.thickness = top_z;
        block.r = 0.5f; block.g = 0.4f; block.b = 0.3f; block.a = 1.0f;
        block.SetMaterial(Materials::Type::HEAVY_STATIC);
        return engine.add_particle(block);
    }

    void remove_particle(int id) {
        auto view = ps->lock_particles_for_write();
        view[id].x = 9999.0f;  // Move far away (safe removal)
        view[id].y = 9999.0f;
        view[id].width = 0.0f;
        view[id].height = 0.0f;
        view[id].thickness = 0.0f;
    }

    // ---- Rotation sanity checks ----

    void begin_rotation_tracking(bool allow_turn = false, bool has_look_at = false) {
        // Preserve assertion slot indices (set by add_rotation_asserts before this call)
        int saved_jump = rot.rot_assert_jump;
        int saved_upright = rot.rot_assert_upright;
        int saved_facing = rot.rot_assert_facing;
        int saved_spin = rot.rot_assert_spin;
        rot = {};
        rot.active = true;
        rot.allow_turn = allow_turn;
        rot.has_look_at = has_look_at;
        rot.prev_base_rotation = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        rot.prev_hips_rot_z = ps->get_particle_copy(h.hips_id).rotation_z;
        rot.rot_assert_jump = saved_jump;
        rot.rot_assert_upright = saved_upright;
        rot.rot_assert_facing = saved_facing;
        rot.rot_assert_spin = saved_spin;
    }

    void add_rotation_asserts() {
        rot.rot_assert_jump = add_assert("no rotation jumps (delta < 0.1 rad/frame)");
        rot.rot_assert_upright = add_assert("upright (pitch/yaw < 0.3 rad)");
        rot.rot_assert_facing = add_assert("facing matches base_rotation (div < 0.3 rad)");
        rot.rot_assert_spin = add_assert("no visual spin (hips rot_z delta < 0.15/frame)");
    }

    void sample_rotation() {
        if (!rot.active) return;
        rot.frame_count++;

        float base_rot = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        auto hips = ps->get_particle_copy(h.hips_id);

        // Frame-to-frame base_rotation delta
        float delta = std::abs(base_rot - rot.prev_base_rotation);
        // Handle wrap-around at +/- PI
        if (delta > static_cast<float>(M_PI)) delta = 2.0f * static_cast<float>(M_PI) - delta;
        if (delta > rot.max_rotation_delta) rot.max_rotation_delta = delta;
        rot.prev_base_rotation = base_rot;

        // Pitch/yaw tipping
        float abs_pitch = std::abs(hips.rotation_x);
        float abs_yaw = std::abs(hips.rotation_y);
        if (abs_pitch > rot.max_abs_pitch) rot.max_abs_pitch = abs_pitch;
        if (abs_yaw > rot.max_abs_yaw) rot.max_abs_yaw = abs_yaw;

        // Facing divergence: how far hips.rotation_z is from base_rotation
        float div = std::abs(hips.rotation_z - base_rot);
        if (div > static_cast<float>(M_PI)) div = 2.0f * static_cast<float>(M_PI) - div;
        if (div > rot.max_facing_divergence) rot.max_facing_divergence = div;

        // Visual spin: frame-to-frame hips.rotation_z delta
        float hips_rz_delta = std::abs(hips.rotation_z - rot.prev_hips_rot_z);
        if (hips_rz_delta > static_cast<float>(M_PI)) hips_rz_delta = 2.0f * static_cast<float>(M_PI) - hips_rz_delta;
        if (hips_rz_delta > rot.max_hips_rot_z_delta) rot.max_hips_rot_z_delta = hips_rz_delta;
        rot.prev_hips_rot_z = hips.rotation_z;

        // Update live values on HUD
        if (rot.rot_assert_jump >= 0)
            update_assert_value(rot.rot_assert_jump, "max_delta=%.4f", rot.max_rotation_delta);
        if (rot.rot_assert_upright >= 0)
            update_assert_value(rot.rot_assert_upright, "pitch=%.3f yaw=%.3f", rot.max_abs_pitch, rot.max_abs_yaw);
        if (rot.rot_assert_facing >= 0)
            update_assert_value(rot.rot_assert_facing, "div=%.3f", rot.max_facing_divergence);
        if (rot.rot_assert_spin >= 0)
            update_assert_value(rot.rot_assert_spin, "max_delta=%.4f", rot.max_hips_rot_z_delta);
    }

    int resolve_rotation_asserts() {
        int failures = 0;

        // Jump threshold: 0.1 rad/frame for straight walking, wider for turning tests
        float jump_threshold = rot.allow_turn ? 0.2f : 0.1f;
        if (!resolve_assert(rot.rot_assert_jump,
                            rot.max_rotation_delta < jump_threshold,
                            "max_delta=%.4f (limit %.2f)", rot.max_rotation_delta, jump_threshold))
            failures++;

        if (!resolve_assert(rot.rot_assert_upright,
                            rot.max_abs_pitch < 0.3f && rot.max_abs_yaw < 0.3f,
                            "pitch=%.3f yaw=%.3f", rot.max_abs_pitch, rot.max_abs_yaw))
            failures++;

        // Facing divergence: only meaningful when look-at system is active
        // (without look-at, FK walk animation controls hips.rotation_z independently)
        if (rot.has_look_at) {
            float div_threshold = rot.allow_turn ? 1.0f : 0.3f;
            if (!resolve_assert(rot.rot_assert_facing,
                                rot.max_facing_divergence < div_threshold,
                                "div=%.3f (limit %.2f)", rot.max_facing_divergence, div_threshold))
                failures++;
        } else {
            resolve_assert(rot.rot_assert_facing, true,
                           "N/A (no look-at target)");
        }

        // Visual spin: frame-to-frame hips.rotation_z must not change too fast
        float spin_threshold = rot.allow_turn ? 0.3f : 0.15f;
        if (!resolve_assert(rot.rot_assert_spin,
                            rot.max_hips_rot_z_delta < spin_threshold,
                            "max_delta=%.4f (limit %.2f)", rot.max_hips_rot_z_delta, spin_threshold))
            failures++;

        rot.active = false;
        return failures;
    }

    // ---- Body integrity checks ----

    // Compute arm chain length: sum of segment distances (shoulder→upper→forearm→hand)
    float compute_arm_chain_length(const std::vector<int>& arm_ids) {
        if (arm_ids.size() < 2) return 0.0f;
        auto view = ps->lock_particles_for_read();
        float total = 0.0f;
        for (size_t i = 0; i + 1 < arm_ids.size(); i++) {
            const auto& a = view[arm_ids[i]];
            const auto& b = view[arm_ids[i + 1]];
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            total += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        return total;
    }

    // Compute minimum body-local lateral offset of arm particles from hips.
    // body_right = (cos(base_rot), -sin(base_rot)) — perpendicular to facing.
    // For right arm: offset should be positive. For left arm: negative (return abs).
    float compute_arm_lateral_spread(const std::vector<int>& arm_ids, bool right_side) {
        if (arm_ids.empty()) return 0.0f;
        auto view = ps->lock_particles_for_read();
        float base_rot = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        float right_x = std::cos(base_rot);
        float right_y = -std::sin(base_rot);
        float hx = view[h.hips_id].x;
        float hy = view[h.hips_id].y;

        float min_spread = 999.0f;
        for (int id : arm_ids) {
            float dx = view[id].x - hx;
            float dy = view[id].y - hy;
            float lateral = dx * right_x + dy * right_y;  // project onto body-right axis
            float spread = right_side ? lateral : -lateral;  // right arm → positive, left → flip
            if (spread < min_spread) min_spread = spread;
        }
        return min_spread;
    }

    void add_body_integrity_asserts() {
        body.assert_right_spread = add_assert("right arm spread (lateral > 0.04m)");
        body.assert_left_spread  = add_assert("left arm spread (lateral > 0.04m)");
        body.assert_right_chain  = add_assert("right arm chain length stable");
        body.assert_left_chain   = add_assert("left arm chain length stable");
    }

    void begin_body_integrity_tracking() {
        int saved_rs = body.assert_right_spread;
        int saved_ls = body.assert_left_spread;
        int saved_rc = body.assert_right_chain;
        int saved_lc = body.assert_left_chain;
        body = {};
        body.active = true;
        body.assert_right_spread = saved_rs;
        body.assert_left_spread = saved_ls;
        body.assert_right_chain = saved_rc;
        body.assert_left_chain = saved_lc;

        // Capture initial arm chain lengths after stabilization
        body.initial_right_arm_length = compute_arm_chain_length(h.right_arm_ids);
        body.initial_left_arm_length = compute_arm_chain_length(h.left_arm_ids);
        body.min_right_arm_length = body.initial_right_arm_length;
        body.min_left_arm_length = body.initial_left_arm_length;
    }

    // Dump detailed arm diagnostics for a single frame
    void dump_arm_diagnostics(int frame, const char* label) {
        auto view = ps->lock_particles_for_read();
        float base_rot = engine.get_humanoid_locomotion().get_base_rotation(h.hips_id);
        float right_x = std::cos(base_rot);
        float right_y = -std::sin(base_rot);
        float hx = view[h.hips_id].x;
        float hy = view[h.hips_id].y;
        float hz = view[h.hips_id].z;
        float hips_rz = view[h.hips_id].rotation_z;

        printf("[ARM_DIAG] frame=%d %s base_rot=%.3f hips_rz=%.3f hips=(%.3f,%.3f,%.3f)\n",
               frame, label, base_rot, hips_rz, hx, hy, hz);
        printf("[ARM_DIAG]   body_right=(%.3f,%.3f)\n", right_x, right_y);

        const char* r_names[] = {"R_Shoulder", "R_UpperArm", "R_Forearm", "R_Hand"};
        const char* l_names[] = {"L_Shoulder", "L_UpperArm", "L_Forearm", "L_Hand"};

        for (size_t i = 0; i < h.right_arm_ids.size() && i < 4; i++) {
            int id = h.right_arm_ids[i];
            const auto& p = view[id];
            float dx = p.x - hx, dy = p.y - hy;
            float lateral = dx * right_x + dy * right_y;
            printf("[ARM_DIAG]   %s id=%d pos=(%.3f,%.3f,%.3f) rot_z=%.3f lateral=%.3f owner=%d\n",
                   r_names[i], id, p.x, p.y, p.z, p.rotation_z, lateral, (int)p.owner);
        }
        for (size_t i = 0; i < h.left_arm_ids.size() && i < 4; i++) {
            int id = h.left_arm_ids[i];
            const auto& p = view[id];
            float dx = p.x - hx, dy = p.y - hy;
            float lateral = -(dx * right_x + dy * right_y);
            printf("[ARM_DIAG]   %s id=%d pos=(%.3f,%.3f,%.3f) rot_z=%.3f lateral=%.3f owner=%d\n",
                   l_names[i], id, p.x, p.y, p.z, p.rotation_z, lateral, (int)p.owner);
        }

        // Also dump chest position for reference
        if (h.torso_ids.size() >= 3) {
            int chest_id = h.torso_ids[2];
            const auto& chest = view[chest_id];
            printf("[ARM_DIAG]   Chest id=%d pos=(%.3f,%.3f,%.3f) rot_z=%.3f\n",
                   chest_id, chest.x, chest.y, chest.z, chest.rotation_z);
        }
    }

    void sample_body_integrity() {
        if (!body.active) return;
        body.frame_count++;

        // Lateral spread (body-local)
        float r_spread = compute_arm_lateral_spread(h.right_arm_ids, true);
        float l_spread = compute_arm_lateral_spread(h.left_arm_ids, false);

        // Dump detailed diagnostics when right arm spread is below torso collision clamp (0.05m)
        if (r_spread < 0.05f) {
            dump_arm_diagnostics(body.frame_count, "R_ARM_LOW");
        }
        if (r_spread < body.min_right_arm_spread) {
            body.min_right_arm_spread = r_spread;
            body.min_right_arm_frame = body.frame_count;
        }
        if (l_spread < body.min_left_arm_spread) {
            body.min_left_arm_spread = l_spread;
            body.min_left_arm_frame = body.frame_count;
        }

        // Chain lengths
        float r_chain = compute_arm_chain_length(h.right_arm_ids);
        float l_chain = compute_arm_chain_length(h.left_arm_ids);
        if (r_chain < body.min_right_arm_length) body.min_right_arm_length = r_chain;
        if (l_chain < body.min_left_arm_length) body.min_left_arm_length = l_chain;

        // Update live HUD values
        if (body.assert_right_spread >= 0)
            update_assert_value(body.assert_right_spread, "min=%.3f (frame %d)",
                                body.min_right_arm_spread, body.min_right_arm_frame);
        if (body.assert_left_spread >= 0)
            update_assert_value(body.assert_left_spread, "min=%.3f (frame %d)",
                                body.min_left_arm_spread, body.min_left_arm_frame);
        if (body.assert_right_chain >= 0)
            update_assert_value(body.assert_right_chain, "%.3f / %.3f (%.0f%%)",
                                body.min_right_arm_length, body.initial_right_arm_length,
                                body.initial_right_arm_length > 0 ? 100.0f * body.min_right_arm_length / body.initial_right_arm_length : 0.0f);
        if (body.assert_left_chain >= 0)
            update_assert_value(body.assert_left_chain, "%.3f / %.3f (%.0f%%)",
                                body.min_left_arm_length, body.initial_left_arm_length,
                                body.initial_left_arm_length > 0 ? 100.0f * body.min_left_arm_length / body.initial_left_arm_length : 0.0f);
    }

    int resolve_body_integrity_asserts() {
        int failures = 0;

        // Arm spread: each arm particle should maintain at least 0.04m lateral offset
        // from the body center. At rest, shoulder is ~0.25m out, hand ~0.17m.
        // An arm "collapsing into the body" would drop below 0.04m.
        const float SPREAD_THRESHOLD = 0.04f;
        if (!resolve_assert(body.assert_right_spread,
                            body.min_right_arm_spread > SPREAD_THRESHOLD,
                            "min=%.3f at frame %d (limit > %.2f)",
                            body.min_right_arm_spread, body.min_right_arm_frame, SPREAD_THRESHOLD))
            failures++;
        if (!resolve_assert(body.assert_left_spread,
                            body.min_left_arm_spread > SPREAD_THRESHOLD,
                            "min=%.3f at frame %d (limit > %.2f)",
                            body.min_left_arm_spread, body.min_left_arm_frame, SPREAD_THRESHOLD))
            failures++;

        // Arm chain length: should not shrink more than 40% from initial.
        // A collapsed arm would have near-zero chain length.
        const float CHAIN_RATIO_THRESHOLD = 0.6f;
        float r_ratio = body.initial_right_arm_length > 0
            ? body.min_right_arm_length / body.initial_right_arm_length : 0.0f;
        float l_ratio = body.initial_left_arm_length > 0
            ? body.min_left_arm_length / body.initial_left_arm_length : 0.0f;
        if (!resolve_assert(body.assert_right_chain,
                            r_ratio > CHAIN_RATIO_THRESHOLD,
                            "min=%.3f init=%.3f ratio=%.0f%% (limit > %.0f%%)",
                            body.min_right_arm_length, body.initial_right_arm_length,
                            r_ratio * 100.0f, CHAIN_RATIO_THRESHOLD * 100.0f))
            failures++;
        if (!resolve_assert(body.assert_left_chain,
                            l_ratio > CHAIN_RATIO_THRESHOLD,
                            "min=%.3f init=%.3f ratio=%.0f%% (limit > %.0f%%)",
                            body.min_left_arm_length, body.initial_left_arm_length,
                            l_ratio * 100.0f, CHAIN_RATIO_THRESHOLD * 100.0f))
            failures++;

        body.active = false;
        return failures;
    }

    // ---- Visual HUD ----

    void draw_hud(const char* test_name, int frame, int total_frames, const char* live_data) {
        if (headless) return;
        auto& rs = engine.get_draw_surface();
        char buf[256];
        int y = 20;

        // Test name — large, white
        snprintf(buf, sizeof(buf), "TEST: %s", test_name);
        rs.draw_text(20, y, buf, 255, 255, 255); y += 22;

        // Intent — yellow, explains what we're testing
        if (!test_intent.empty()) {
            rs.draw_text(20, y, test_intent.c_str(), 255, 220, 80); y += 20;
        }

        // Frame counter
        snprintf(buf, sizeof(buf), "Frame %d / %d", frame, total_frames);
        rs.draw_text(20, y, buf, 160, 160, 160); y += 22;

        // Live measurement data — cyan
        if (live_data && live_data[0] != '\0') {
            rs.draw_text(20, y, live_data, 100, 220, 255); y += 20;
        }

        // Hips telemetry
        float hx, hy, hz;
        get_hips_pos(hx, hy, hz);
        float spd = measure_speed();
        snprintf(buf, sizeof(buf), "Hips (%.2f, %.2f, %.2f)  speed %.2f m/s", hx, hy, hz, spd);
        rs.draw_text(20, y, buf, 140, 140, 200); y += 24;

        // Assertions — each on its own line, colored by status
        for (const auto& a : asserts) {
            uint8_t cr, cg, cb;
            const char* status;
            if (!a.resolved) {
                cr = 180; cg = 180; cb = 180; status = "...";
            } else if (a.passed) {
                cr = 80; cg = 255; cb = 80; status = "PASS";
            } else {
                cr = 255; cg = 60; cb = 60; status = "FAIL";
            }
            snprintf(buf, sizeof(buf), "[%s] %s: %s", status, a.label.c_str(), a.value.c_str());
            rs.draw_text(20, y, buf, cr, cg, cb); y += 18;
        }

        // Footer
        rs.draw_text(20, 920, "HUMANOID MOVEMENT  |  SPACE=next  ESC=exit", 120, 120, 120);
    }

    // Hold on screen showing final results until SPACE (visual) or just return (headless)
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
        }
    }

    bool should_exit() {
        if (headless) return false;
        const auto& input = engine.get_input_system().get_input_state();
        return input.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close();
    }

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
            rs.draw_text(20, 920, "HUMANOID MOVEMENT", 140, 140, 140);
            engine.present();
        }
    }
};

// ========================================================================
// Test 1: Speed Achievement
// Verify humanoid reaches 80% of target velocity within 60 frames.
// Runs 4 sub-tests at 1.0, 1.5, 2.0, 3.0 m/s walking North.
// ========================================================================
int test_speed_achievement(TestFixture& f) {
    printf("\n--- Test 1: Speed Achievement ---\n");
    f.begin_test("Can humanoid reach target velocities? (1.0, 1.5, 2.0, 3.0 m/s in 60 frames each)");

    float target_speeds[] = {1.0f, 1.5f, 2.0f, 3.0f};
    int failures = 0;

    // Pre-create assertion slots for all 4 speeds
    int assert_ids[4];
    char label[64];
    for (int s = 0; s < 4; s++) {
        snprintf(label, sizeof(label), "speed >= %.0f%% of %.1f m/s", 80.0f, target_speeds[s]);
        assert_ids[s] = f.add_assert(label);
    }
    f.add_rotation_asserts();

    for (int s = 0; s < 4; s++) {
        float target = target_speeds[s];
        f.reset();
        f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
        f.begin_rotation_tracking(false, true);

        f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, target);
        f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

        const int FRAMES = 60;
        for (int i = 0; i < FRAMES; ++i) {
            if (f.should_exit()) return 0;
            f.step();
            f.sample_rotation();

            char live[128];
            snprintf(live, sizeof(live), "Target %.1f m/s | Actual %.2f m/s | Ramp %d/60",
                     target, f.measure_speed(), i + 1);
            f.update_assert_value(assert_ids[s], "%.2f / %.2f m/s", f.measure_speed(), target);
            f.draw_hud("1: Speed Achievement", i, FRAMES, live);
            f.present();
        }

        float actual = f.measure_speed();
        float threshold = target * 0.8f;
        bool ok = actual >= threshold;
        if (!f.resolve_assert(assert_ids[s], ok, "%.2f m/s (need >= %.2f)", actual, threshold))
            failures++;
    }

    failures += f.resolve_rotation_asserts();
    f.show_results("1: Speed Achievement");
    return failures;
}

// ========================================================================
// Test 2: Direction After Turn
// Walk North + look East (5,0). Verify: body turns (base_rotation > 0.5),
// displacement follows facing (angle diff < 30 deg), moved East (x > 1.0).
// Only test with look-at active — facing divergence check is meaningful here.
// ========================================================================
int test_direction_after_turn(TestFixture& f) {
    printf("\n--- Test 2: Direction After Turn ---\n");
    f.begin_test("Walk North + look East: body turns, displacement follows facing direction");
    f.reset();

    int a_rot   = f.add_assert("base_rotation > 0.5 rad");
    int a_east  = f.add_assert("moved East (hips.x > 1.0)");
    int a_angle = f.add_assert("displacement angle within 30 deg of facing");
    int a_dist  = f.add_assert("total displacement > 2.0m");
    f.add_rotation_asserts();
    f.add_body_integrity_asserts();
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);
    f.begin_body_integrity_tracking();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 5.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();
        f.sample_body_integrity();

        float rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);
        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float disp = std::sqrt(hx * hx + hy * hy);

        f.update_assert_value(a_rot, "%.2f rad", rot);
        f.update_assert_value(a_east, "x=%.2f", hx);
        f.update_assert_value(a_dist, "%.2f m", disp);

        char live[128];
        snprintf(live, sizeof(live), "rot=%.2f rad | hips=(%.2f, %.2f) | disp=%.2f m | R_arm=%.3f L_arm=%.3f",
                 rot, hx, hy, disp, f.body.min_right_arm_spread, f.body.min_left_arm_spread);
        f.draw_hud("2: Direction After Turn", i, FRAMES, live);
        f.present();
    }

    float final_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);
    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float displacement = std::sqrt(hx * hx + hy * hy);
    float disp_angle = std::atan2(hx, hy);
    float angle_diff = std::abs(disp_angle - final_rot);
    if (angle_diff > static_cast<float>(M_PI)) angle_diff = 2.0f * static_cast<float>(M_PI) - angle_diff;

    int failures = 0;
    if (!f.resolve_assert(a_rot, final_rot > 0.5f, "%.4f rad", final_rot)) failures++;
    if (!f.resolve_assert(a_east, hx > 1.0f, "x=%.3f y=%.3f", hx, hy)) failures++;
    if (!f.resolve_assert(a_angle, angle_diff < 0.52f, "diff=%.0f deg",
                          angle_diff * 180.0f / static_cast<float>(M_PI))) failures++;
    if (!f.resolve_assert(a_dist, displacement > 2.0f, "%.3f m", displacement)) failures++;
    failures += f.resolve_rotation_asserts();
    failures += f.resolve_body_integrity_asserts();

    f.show_results("2: Direction After Turn");
    return failures;
}

// ========================================================================
// Test 3: Grass Traversal
// Walk North at 2.0 m/s through 40 grass blades (y=2..6, LEAVES material).
// Verify: traverses field (y > 5), no climb spikes (z < 1.5),
// no stalling (max consecutive slow frames <= 15), still moving at end.
// This test validated the collision filtering fix — grass used to block.
// ========================================================================
int test_grass_traversal(TestFixture& f) {
    printf("\n--- Test 3: Grass Traversal ---\n");
    f.begin_test("Walk through 40 grass blades (y=2..6). Should NOT stall or climb.");
    f.reset();

    int a_trav  = f.add_assert("traversed field (hips.y > 5.0)");
    int a_climb = f.add_assert("no climb spikes (max_z < 1.5)");
    int a_stall = f.add_assert("no stall > 15 consecutive frames");
    int a_move  = f.add_assert("still moving at end (speed > 1.0)");
    f.add_rotation_asserts();

    printf("  Adding 40 grass blades at y=[2,6]...\n");
    f.add_grass_field(0.0f, 2.0f, 6.0f, 40);

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(false, true);

    const int FRAMES = 180;
    float max_z = 0.0f;
    int stall_count = 0, max_stall = 0;

    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();
        if (hz > max_z) max_z = hz;

        if (spd < 0.5f) {
            stall_count++;
            if (stall_count > max_stall) max_stall = stall_count;
        } else {
            stall_count = 0;
        }

        f.update_assert_value(a_trav, "y=%.2f", hy);
        f.update_assert_value(a_climb, "max_z=%.2f", max_z);
        f.update_assert_value(a_stall, "current=%d max=%d", stall_count, max_stall);
        f.update_assert_value(a_move, "%.2f m/s", spd);

        char live[128];
        snprintf(live, sizeof(live), "speed=%.2f m/s | stall=%d (max %d) | max_z=%.2f",
                 spd, stall_count, max_stall, max_z);
        f.draw_hud("3: Grass Traversal", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_trav, hy > 5.0f, "y=%.3f", hy)) failures++;
    if (!f.resolve_assert(a_climb, max_z < 1.5f, "%.3f", max_z)) failures++;
    if (!f.resolve_assert(a_stall, max_stall <= 15, "max_stall=%d", max_stall)) failures++;
    if (!f.resolve_assert(a_move, final_speed > 1.0f, "%.2f m/s", final_speed)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("3: Grass Traversal");
    return failures;
}

// ========================================================================
// Test 4: Deceleration/Stop
// Walk North at 2.0 m/s for 60 frames, then set velocity to zero.
// Verify: reached >= 1.5 m/s during walk, fully stopped (< 0.05 m/s),
// braking distance < 1.5m. Rotation tracked across both phases.
// ========================================================================
int test_deceleration(TestFixture& f) {
    printf("\n--- Test 4: Deceleration/Stop ---\n");
    f.begin_test("Walk 60 frames at 2.0 m/s, then stop. Measure speed + braking distance.");
    f.reset();

    int a_speed = f.add_assert("reached >= 1.5 m/s during walk");
    int a_stop  = f.add_assert("fully stopped (speed < 0.05)");
    int a_brake = f.add_assert("braking distance < 1.5m");
    f.add_rotation_asserts();
    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.begin_rotation_tracking(false, true);

    // Walk phase
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    const int WALK_FRAMES = 60;
    float peak_speed = 0.0f;
    for (int i = 0; i < WALK_FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float spd = f.measure_speed();
        if (spd > peak_speed) peak_speed = spd;
        f.update_assert_value(a_speed, "peak=%.2f m/s", peak_speed);

        char live[128];
        snprintf(live, sizeof(live), "WALKING | speed=%.2f m/s | peak=%.2f m/s", spd, peak_speed);
        f.draw_hud("4: Deceleration", i, WALK_FRAMES + 60, live);
        f.present();
    }

    float walk_speed = f.measure_speed();
    float hx_before, hy_before, hz_before;
    f.get_hips_pos(hx_before, hy_before, hz_before);
    printf("  Speed after walk phase: %.2f m/s (peak: %.2f)\n", walk_speed, peak_speed);

    // Stop phase
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);

    const int STOP_FRAMES = 60;
    for (int i = 0; i < STOP_FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float spd = f.measure_speed();
        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float brake = std::sqrt((hx - hx_before) * (hx - hx_before) +
                                (hy - hy_before) * (hy - hy_before));

        f.update_assert_value(a_stop, "%.3f m/s", spd);
        f.update_assert_value(a_brake, "%.3f m", brake);

        char live[128];
        snprintf(live, sizeof(live), "STOPPING | speed=%.3f m/s | brake_dist=%.3f m", spd, brake);
        f.draw_hud("4: Deceleration", WALK_FRAMES + i, WALK_FRAMES + STOP_FRAMES, live);
        f.present();
    }

    float stop_speed = f.measure_speed();
    float hx_after, hy_after, hz_after;
    f.get_hips_pos(hx_after, hy_after, hz_after);
    float stop_disp = std::sqrt((hx_after - hx_before) * (hx_after - hx_before) +
                                (hy_after - hy_before) * (hy_after - hy_before));

    int failures = 0;
    // Use peak speed for the walk assertion (more resilient to frame-exact measurement)
    if (!f.resolve_assert(a_speed, peak_speed >= 1.5f, "peak=%.2f m/s", peak_speed)) failures++;
    if (!f.resolve_assert(a_stop, stop_speed < 0.05f, "%.4f m/s", stop_speed)) failures++;
    if (!f.resolve_assert(a_brake, stop_disp < 1.5f, "%.3f m", stop_disp)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("4: Deceleration");
    return failures;
}

// ========================================================================
// Test 5: Step Block vs Grass (differential)
// Run A: solid HEAVY_STATIC step at y=3 — humanoid should climb (max_z > 1.0).
// Run B: 10 LEAVES grass blades at y=3 — humanoid should NOT climb (max_z < 1.2).
// Step block is removed between runs so it doesn't contaminate Run B.
// Rotation tracked across both runs (worst-of-both merge before resolve).
// ========================================================================
int test_step_vs_grass(TestFixture& f) {
    printf("\n--- Test 5: Step Block vs Grass ---\n");
    f.begin_test("A: Solid step at y=3 (should climb). B: Grass at y=3 (should NOT climb).");

    int a_climb = f.add_assert("[A] step climb (max_z > 1.0)");
    int a_pass  = f.add_assert("[A] got past step (y > 3.5)");
    int b_noclimb = f.add_assert("[B] no grass climbing (max_z < 1.2)");
    int b_pass    = f.add_assert("[B] walked through grass (y > 3.5)");
    f.add_rotation_asserts();

    // --- Run A: Solid step ---
    printf("  [Run A] Solid step block at y=3\n");
    f.reset();
    int step_id = f.add_step_block(0.0f, 3.0f, 0.35f);

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(false, true);

    const int FRAMES = 120;
    float a_max_z = 0.0f;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        if (hz > a_max_z) a_max_z = hz;

        f.update_assert_value(a_climb, "max_z=%.2f", a_max_z);
        f.update_assert_value(a_pass, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "[A: STEP] speed=%.2f | z=%.2f (max %.2f) | y=%.2f",
                 f.measure_speed(), hz, a_max_z, hy);
        f.draw_hud("5A: Step Block", i, FRAMES, live);
        f.present();
    }

    float a_hx, a_hy, a_hz;
    f.get_hips_pos(a_hx, a_hy, a_hz);
    printf("  [A] final y=%.3f  max_z=%.3f\n", a_hy, a_max_z);

    f.resolve_assert(a_climb, a_max_z > 1.0f, "%.3f", a_max_z);
    f.resolve_assert(a_pass, a_hy > 3.5f, "y=%.3f", a_hy);

    if (!f.headless) {
        // Brief pause to show Run A results before Run B
        for (int i = 0; i < 30; i++) {
            if (f.should_exit()) return 0;
            f.step();
            f.draw_hud("5A: Step Block — DONE", 0, 0, "Run A complete. Starting Run B...");
            f.present();
        }
    }

    // --- Run B: Grass ---
    // Save Run A rotation maxima, re-begin for Run B (avoids false delta from reset)
    auto run_a_rot = f.rot;
    f.remove_particle(step_id);  // Remove step block so it doesn't interfere
    printf("  [Run B] Grass blades at y=3\n");
    f.reset();
    for (int i = 0; i < 10; i++) {
        float gx = (std::rand() % 100 - 50) * 0.01f;
        float gy = 2.8f + (std::rand() % 100) * 0.004f;
        f.add_grass_blade(gx, gy, i % 3 == 0);
    }

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(false, true);

    float b_max_z = 0.0f;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        if (hz > b_max_z) b_max_z = hz;

        f.update_assert_value(b_noclimb, "max_z=%.2f", b_max_z);
        f.update_assert_value(b_pass, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "[B: GRASS] speed=%.2f | z=%.2f (max %.2f) | y=%.2f",
                 f.measure_speed(), hz, b_max_z, hy);
        f.draw_hud("5B: Grass", i, FRAMES, live);
        f.present();
    }

    float b_hx, b_hy, b_hz;
    f.get_hips_pos(b_hx, b_hy, b_hz);
    printf("  [B] final y=%.3f  max_z=%.3f\n", b_hy, b_max_z);

    // Merge rotation maxima from Run A and Run B (worst of both)
    if (run_a_rot.max_rotation_delta > f.rot.max_rotation_delta)
        f.rot.max_rotation_delta = run_a_rot.max_rotation_delta;
    if (run_a_rot.max_abs_pitch > f.rot.max_abs_pitch)
        f.rot.max_abs_pitch = run_a_rot.max_abs_pitch;
    if (run_a_rot.max_abs_yaw > f.rot.max_abs_yaw)
        f.rot.max_abs_yaw = run_a_rot.max_abs_yaw;
    if (run_a_rot.max_facing_divergence > f.rot.max_facing_divergence)
        f.rot.max_facing_divergence = run_a_rot.max_facing_divergence;

    int failures = 0;
    // Count only Run A/B failures now (resolve_assert already called for A above)
    if (!f.asserts[0].passed) failures++;  // a_climb
    if (!f.asserts[1].passed) failures++;  // a_pass
    if (!f.resolve_assert(b_noclimb, b_max_z < 1.2f, "%.3f", b_max_z)) failures++;
    if (!f.resolve_assert(b_pass, b_hy > 3.5f, "y=%.3f", b_hy)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("5: Step Block vs Grass");
    return failures;
}

// ========================================================================
// Test 6: Diagonal Walking
// Walk Northeast at (vx=1.5, vy=1.5). Look-at (100, 100). 120 frames.
// Verify: displacement angle near 45 deg, total > 3.0m, speed > 1.5 m/s.
// ========================================================================
int test_diagonal_walking(TestFixture& f) {
    printf("\n--- Test 6: Diagonal Walking ---\n");
    f.begin_test("Walk NE at (1.5, 1.5). Verify ~45 deg displacement, distance > 3m.");
    f.reset();

    int a_angle = f.add_assert("displacement angle near 45 deg (within 20 deg)");
    int a_dist  = f.add_assert("total displacement > 3.0m");
    int a_speed = f.add_assert("speed > 1.5 m/s at end");
    f.add_rotation_asserts();
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 100.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 1.5f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float disp = std::sqrt(hx * hx + hy * hy);
        float spd = f.measure_speed();

        f.update_assert_value(a_dist, "%.2f m", disp);
        f.update_assert_value(a_speed, "%.2f m/s", spd);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | disp=%.2f m | speed=%.2f m/s",
                 hx, hy, disp, spd);
        f.draw_hud("6: Diagonal Walking", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float displacement = std::sqrt(hx * hx + hy * hy);
    float disp_angle = std::atan2(hx, hy);  // atan2(x, y) gives angle from +Y axis
    float target_angle = static_cast<float>(M_PI) / 4.0f;  // 45 deg
    float angle_diff = std::abs(disp_angle - target_angle);
    if (angle_diff > static_cast<float>(M_PI)) angle_diff = 2.0f * static_cast<float>(M_PI) - angle_diff;
    float angle_diff_deg = angle_diff * 180.0f / static_cast<float>(M_PI);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_angle, angle_diff_deg < 20.0f, "%.1f deg from 45 (limit 20)", angle_diff_deg)) failures++;
    if (!f.resolve_assert(a_dist, displacement > 3.0f, "%.3f m", displacement)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 1.5f, "%.2f m/s", final_speed)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("6: Diagonal Walking");
    return failures;
}

// ========================================================================
// Test 7: Start-Stop-Start
// Walk North 2.0 m/s × 60f → stop × 60f → walk North 2.0 m/s × 60f.
// Verify: peak speed in both walk phases, fully stopped in middle,
// total y displacement > 5.0m.
// ========================================================================
int test_start_stop_start(TestFixture& f) {
    printf("\n--- Test 7: Start-Stop-Start ---\n");
    f.begin_test("Walk 60f, stop 60f, walk 60f. Verify clean restart and stop.");
    f.reset();

    int a_walk1 = f.add_assert("peak speed > 1.5 in walk phase 1");
    int a_stop  = f.add_assert("fully stopped after stop phase (< 0.05)");
    int a_walk2 = f.add_assert("speed > 1.5 in walk phase 2 (restart)");
    int a_disp  = f.add_assert("total y displacement > 5.0m");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int PHASE_FRAMES = 60;
    const int TOTAL_FRAMES = PHASE_FRAMES * 3;
    float peak_walk1 = 0.0f, peak_walk2 = 0.0f;
    float stop_speed = 999.0f;

    for (int i = 0; i < TOTAL_FRAMES; ++i) {
        if (f.should_exit()) return 0;

        // Phase transitions
        if (i == 0) {
            // Walk phase 1
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
            f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
        } else if (i == PHASE_FRAMES) {
            // Stop phase
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 0.0f);
            f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, false);
        } else if (i == PHASE_FRAMES * 2) {
            // Walk phase 2
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
            f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
        }

        f.step();
        f.sample_rotation();

        float spd = f.measure_speed();
        const char* phase_label;
        if (i < PHASE_FRAMES) {
            if (spd > peak_walk1) peak_walk1 = spd;
            phase_label = "WALK 1";
            f.update_assert_value(a_walk1, "peak=%.2f m/s", peak_walk1);
        } else if (i < PHASE_FRAMES * 2) {
            stop_speed = spd;  // Keep updating — final value is what matters
            phase_label = "STOP";
            f.update_assert_value(a_stop, "%.3f m/s", stop_speed);
        } else {
            if (spd > peak_walk2) peak_walk2 = spd;
            phase_label = "WALK 2";
            f.update_assert_value(a_walk2, "peak=%.2f m/s", peak_walk2);
        }

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        f.update_assert_value(a_disp, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "[%s] speed=%.2f m/s | y=%.2f", phase_label, spd, hy);
        f.draw_hud("7: Start-Stop-Start", i, TOTAL_FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);

    int failures = 0;
    if (!f.resolve_assert(a_walk1, peak_walk1 > 1.5f, "peak=%.2f m/s", peak_walk1)) failures++;
    if (!f.resolve_assert(a_stop, stop_speed < 0.05f, "%.4f m/s", stop_speed)) failures++;
    if (!f.resolve_assert(a_walk2, peak_walk2 > 1.5f, "peak=%.2f m/s", peak_walk2)) failures++;
    if (!f.resolve_assert(a_disp, hy > 5.0f, "y=%.3f", hy)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("7: Start-Stop-Start");
    return failures;
}

// ========================================================================
// Test 8: Impulse Knockback Recovery
// Walk North 1.5 m/s × 30f, then apply_impulse(0, -3.0), continue 90f.
// Verify: y dipped after impulse, recovered forward, walking at end.
// ========================================================================
int test_impulse_knockback(TestFixture& f) {
    printf("\n--- Test 8: Impulse Knockback Recovery ---\n");
    f.begin_test("Walk North 30f, apply_impulse(0, -3.0), continue 90f. Verify recovery.");
    f.reset();

    int a_knockback = f.add_assert("knocked back (y dipped after impulse)");
    int a_recover   = f.add_assert("recovered forward (final y > pre-impulse y)");
    int a_walking   = f.add_assert("walking at end (speed > 1.0)");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 1.5f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);

    const int PRE_IMPULSE = 30;
    const int POST_IMPULSE = 90;
    const int TOTAL_FRAMES = PRE_IMPULSE + POST_IMPULSE;
    float pre_impulse_y = 0.0f;
    float min_y_after_impulse = 9999.0f;
    bool impulse_applied = false;

    for (int i = 0; i < TOTAL_FRAMES; ++i) {
        if (f.should_exit()) return 0;

        if (i == PRE_IMPULSE) {
            // Record position and apply knockback
            float hx, hy, hz;
            f.get_hips_pos(hx, hy, hz);
            pre_impulse_y = hy;
            f.engine.get_humanoid_locomotion().apply_impulse(f.h.hips_id, 0.0f, -3.0f);
            impulse_applied = true;
            printf("  Impulse applied at y=%.3f\n", pre_impulse_y);
        }

        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        if (impulse_applied && hy < min_y_after_impulse) {
            min_y_after_impulse = hy;
        }

        f.update_assert_value(a_knockback, "min_y=%.2f pre=%.2f", min_y_after_impulse, pre_impulse_y);
        f.update_assert_value(a_recover, "y=%.2f (need > %.2f)", hy, pre_impulse_y);
        f.update_assert_value(a_walking, "%.2f m/s", spd);

        const char* phase = (i < PRE_IMPULSE) ? "PRE-IMPULSE" : "POST-IMPULSE";
        char live[128];
        snprintf(live, sizeof(live), "[%s] speed=%.2f m/s | y=%.2f | min_y=%.2f",
                 phase, spd, hy, min_y_after_impulse);
        f.draw_hud("8: Impulse Knockback", i, TOTAL_FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_knockback, min_y_after_impulse < pre_impulse_y,
                          "min_y=%.3f pre=%.3f", min_y_after_impulse, pre_impulse_y)) failures++;
    if (!f.resolve_assert(a_recover, hy > pre_impulse_y,
                          "final_y=%.3f pre=%.3f", hy, pre_impulse_y)) failures++;
    if (!f.resolve_assert(a_walking, final_speed > 1.0f,
                          "%.2f m/s", final_speed)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("8: Impulse Knockback");
    return failures;
}

// ========================================================================
// Test 9: Southward Walking (Reverse Direction)
// Walk South at 0.9 m/s (Eden backward speed: 1.5 * 0.6 = 0.9).
// The engine aligns velocity to facing, so we pre-face South then walk.
// Verify: y decreases, speed near target, body stays upright.
//
// NOTE: The engine doesn't have true backward walking (body facing North,
// walking South). Velocity always aligns to facing direction. Eden fakes
// backward movement by computing world-space velocity from body rotation.
// This test validates reverse-direction travel at reduced speed.
// ========================================================================
int test_backward_walking(TestFixture& f) {
    printf("\n--- Test 9: Southward Walking ---\n");
    f.begin_test("Face South, walk at 0.9 m/s (backward speed). Verify southward displacement.");
    f.reset();

    const float BACKWARD_SPEED = 0.9f;  // 1.5 * 0.6 (Eden backward mult)

    int a_south = f.add_assert("moved South (y < -1.5)");
    int a_speed = f.add_assert("speed near target (> 0.6 m/s)");
    int a_no_x  = f.add_assert("minimal x drift (|x| < 1.0)");
    f.add_rotation_asserts();

    // Pre-face South, then set velocity South
    f.engine.get_humanoid_locomotion().set_facing_direction(f.h.hips_id, static_cast<float>(M_PI));  // π = South
    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, -100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, -BACKWARD_SPEED);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        f.update_assert_value(a_south, "y=%.2f", hy);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_x, "x=%.2f", hx);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f m/s | target=%.1f",
                 hx, hy, spd, BACKWARD_SPEED);
        f.draw_hud("9: Southward Walking", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_south, hy < -1.5f, "y=%.3f", hy)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 0.6f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_x, std::abs(hx) < 1.0f, "x=%.3f", hx)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("9: Southward Walking");
    return failures;
}

// ========================================================================
// Test 10: Eastward Walking (Lateral Direction)
// Walk East at 1.05 m/s (Eden strafe speed: 1.5 * 0.7 = 1.05).
// Pre-face East then walk. Velocity aligns to facing.
// Verify: x increases, minimal y drift, speed near target.
//
// NOTE: The engine doesn't have true strafing (body facing North,
// walking East). Velocity always aligns to facing direction. Eden
// fakes strafing by computing world-space velocity from body rotation.
// This test validates lateral-direction travel at reduced speed.
// ========================================================================
int test_strafing(TestFixture& f) {
    printf("\n--- Test 10: Eastward Walking ---\n");
    f.begin_test("Face East, walk at 1.05 m/s (strafe speed). Verify eastward displacement.");
    f.reset();

    const float STRAFE_SPEED = 1.05f;  // 1.5 * 0.7 (Eden strafe mult)

    int a_east  = f.add_assert("moved East (x > 2.0)");
    int a_speed = f.add_assert("speed near target (> 0.7 m/s)");
    int a_no_y  = f.add_assert("minimal y drift (|y| < 1.0)");
    f.add_rotation_asserts();

    // Pre-face East, then set velocity East
    float east_angle = static_cast<float>(M_PI) / 2.0f;  // π/2 = East
    f.engine.get_humanoid_locomotion().set_facing_direction(f.h.hips_id, east_angle);
    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 100.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, STRAFE_SPEED, 0.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        f.update_assert_value(a_east, "x=%.2f", hx);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_y, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f m/s | target=%.1f",
                 hx, hy, spd, STRAFE_SPEED);
        f.draw_hud("10: Eastward Walking", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_east, hx > 2.0f, "x=%.3f", hx)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 0.7f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_y, std::abs(hy) < 1.0f, "y=%.3f", hy)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("10: Eastward Walking");
    return failures;
}

// ========================================================================
// Test 11: Mid-Movement Direction Change
// Walk North at 2.0 m/s × 60f, then switch to East at 2.0 m/s × 90f.
// Verify: initial y displacement, then x displacement grows,
// body turns smoothly (rotation sanity), total displacement > 4m.
// ========================================================================
int test_direction_change(TestFixture& f) {
    printf("\n--- Test 11: Mid-Movement Direction Change ---\n");
    f.begin_test("Walk North 60f then East 90f. Verify smooth transition, both axes displaced.");
    f.reset();

    int a_north  = f.add_assert("moved North in phase 1 (y > 1.5)");
    int a_east   = f.add_assert("moved East in phase 2 (x > 1.5)");
    int a_disp   = f.add_assert("total displacement > 4.0m");
    int a_smooth = f.add_assert("speed never dropped below 0.5 during transition");
    f.add_rotation_asserts();

    // Phase 1: Walk North, look North
    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 0.0f, 2.0f);
    f.engine.get_humanoid_locomotion().set_volitional(f.h.hips_id, true);
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);

    const int PHASE1_FRAMES = 60;
    const int PHASE2_FRAMES = 90;
    const int TOTAL_FRAMES = PHASE1_FRAMES + PHASE2_FRAMES;
    float y_at_transition = 0.0f;
    float min_speed_during_transition = 999.0f;

    for (int i = 0; i < TOTAL_FRAMES; ++i) {
        if (f.should_exit()) return 0;

        if (i == PHASE1_FRAMES) {
            // Record y and switch to East
            float hx, hy, hz;
            f.get_hips_pos(hx, hy, hz);
            y_at_transition = hy;
            printf("  Switching to East at y=%.3f\n", y_at_transition);

            f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 100.0f, hy);
            f.engine.get_humanoid_locomotion().set_target_velocity(f.h.hips_id, 2.0f, 0.0f);
        }

        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        // Track min speed in transition window (frames 55-75)
        if (i >= PHASE1_FRAMES - 5 && i <= PHASE1_FRAMES + 15) {
            if (spd < min_speed_during_transition) {
                min_speed_during_transition = spd;
            }
        }

        f.update_assert_value(a_north, "y=%.2f", hy);
        f.update_assert_value(a_east, "x=%.2f", hx);
        f.update_assert_value(a_smooth, "min=%.2f m/s", min_speed_during_transition);

        const char* phase = (i < PHASE1_FRAMES) ? "NORTH" : "EAST";
        char live[128];
        snprintf(live, sizeof(live), "[%s] pos=(%.2f, %.2f) | speed=%.2f m/s",
                 phase, hx, hy, spd);
        f.draw_hud("11: Direction Change", i, TOTAL_FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float displacement = std::sqrt(hx * hx + hy * hy);

    int failures = 0;
    if (!f.resolve_assert(a_north, y_at_transition > 1.5f, "y_at_switch=%.3f", y_at_transition)) failures++;
    if (!f.resolve_assert(a_east, hx > 1.5f, "x=%.3f", hx)) failures++;
    if (!f.resolve_assert(a_disp, displacement > 4.0f, "%.3f m", displacement)) failures++;
    if (!f.resolve_assert(a_smooth, min_speed_during_transition > 0.5f,
                          "min=%.3f m/s", min_speed_during_transition)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("11: Direction Change");
    return failures;
}

// ========================================================================
// Test 12: Backward Walking (Body-Relative API)
// Body faces North (look-at North), set_body_relative_velocity(forward=-0.9).
// Humanoid should walk SOUTH while facing NORTH — true backward walking.
// This is impossible with set_target_velocity (velocity aligns to facing).
// ========================================================================
int test_body_rel_backward(TestFixture& f) {
    printf("\n--- Test 12: Backward Walking (Body-Relative) ---\n");
    f.begin_test("Body faces North, walks backward (South) via body-relative API.");
    f.reset();

    const float BACKWARD_SPEED = 0.9f;  // 1.5 * 0.6 (Eden backward mult)

    int a_south  = f.add_assert("moved South (y < -1.5)");
    int a_north_face = f.add_assert("still facing North (|base_rotation| < 0.3)");
    int a_speed  = f.add_assert("speed near target (> 0.6 m/s)");
    int a_no_x   = f.add_assert("minimal x drift (|x| < 0.5)");
    f.add_rotation_asserts();

    // Face North, look North, walk backward
    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_body_relative_velocity(f.h.hips_id, -BACKWARD_SPEED, 0.0f);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();
        float rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

        f.update_assert_value(a_south, "y=%.2f", hy);
        f.update_assert_value(a_north_face, "rot=%.3f", rot);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_x, "x=%.2f", hx);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f | facing=%.2f rad",
                 hx, hy, spd, rot);
        f.draw_hud("12: Backward (Body-Rel)", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();
    float final_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

    int failures = 0;
    if (!f.resolve_assert(a_south, hy < -1.5f, "y=%.3f", hy)) failures++;
    if (!f.resolve_assert(a_north_face, std::abs(final_rot) < 0.3f, "rot=%.3f", final_rot)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 0.6f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_x, std::abs(hx) < 0.5f, "x=%.3f", hx)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("12: Backward (Body-Rel)");
    return failures;
}

// ========================================================================
// Test 13: Strafe Right (Body-Relative API)
// Body faces North (look-at North), set_body_relative_velocity(0, right=1.05).
// Humanoid should move EAST while facing NORTH — true strafing.
// ========================================================================
int test_body_rel_strafe_right(TestFixture& f) {
    printf("\n--- Test 13: Strafe Right (Body-Relative) ---\n");
    f.begin_test("Body faces North, strafes East via body-relative API.");
    f.reset();

    const float STRAFE_SPEED = 1.05f;  // 1.5 * 0.7 (Eden strafe mult)

    int a_east   = f.add_assert("moved East (x > 2.0)");
    int a_north_face = f.add_assert("still facing North (|base_rotation| < 0.3)");
    int a_speed  = f.add_assert("speed near target (> 0.7 m/s)");
    int a_no_y   = f.add_assert("minimal y drift (|y| < 0.5)");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_body_relative_velocity(f.h.hips_id, 0.0f, STRAFE_SPEED);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        f.update_assert_value(a_east, "x=%.2f", hx);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_y, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f m/s", hx, hy, spd);
        f.draw_hud("13: Strafe Right (Body-Rel)", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();
    float final_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

    int failures = 0;
    if (!f.resolve_assert(a_east, hx > 2.0f, "x=%.3f", hx)) failures++;
    if (!f.resolve_assert(a_north_face, std::abs(final_rot) < 0.3f, "rot=%.3f", final_rot)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 0.7f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_y, std::abs(hy) < 0.5f, "y=%.3f", hy)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("13: Strafe Right (Body-Rel)");
    return failures;
}

// ========================================================================
// Test 14: Strafe Left (Body-Relative API)
// Body faces North (look-at North), set_body_relative_velocity(0, right=-1.05).
// Humanoid should move WEST while facing NORTH.
// ========================================================================
int test_body_rel_strafe_left(TestFixture& f) {
    printf("\n--- Test 14: Strafe Left (Body-Relative) ---\n");
    f.begin_test("Body faces North, strafes West via body-relative API.");
    f.reset();

    const float STRAFE_SPEED = 1.05f;

    int a_west   = f.add_assert("moved West (x < -2.0)");
    int a_north_face = f.add_assert("still facing North (|base_rotation| < 0.3)");
    int a_speed  = f.add_assert("speed near target (> 0.7 m/s)");
    int a_no_y   = f.add_assert("minimal y drift (|y| < 0.5)");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_body_relative_velocity(f.h.hips_id, 0.0f, -STRAFE_SPEED);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        f.update_assert_value(a_west, "x=%.2f", hx);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_y, "y=%.2f", hy);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f m/s", hx, hy, spd);
        f.draw_hud("14: Strafe Left (Body-Rel)", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();
    float final_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

    int failures = 0;
    if (!f.resolve_assert(a_west, hx < -2.0f, "x=%.3f", hx)) failures++;
    if (!f.resolve_assert(a_north_face, std::abs(final_rot) < 0.3f, "rot=%.3f", final_rot)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 0.7f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_y, std::abs(hy) < 0.5f, "y=%.3f", hy)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("14: Strafe Left (Body-Rel)");
    return failures;
}

// ========================================================================
// Test 15: Forward Body-Relative = Forward World-Space
// Body faces North, set_body_relative_velocity(forward=1.5, right=0).
// Should behave identically to set_target_velocity(0, 1.5) — same displacement.
// ========================================================================
int test_body_rel_forward(TestFixture& f) {
    printf("\n--- Test 15: Forward Body-Relative ---\n");
    f.begin_test("Body-relative forward=1.5 should match world-space forward.");
    f.reset();

    int a_north  = f.add_assert("moved North (y > 3.0)");
    int a_speed  = f.add_assert("speed near 1.5 m/s (> 1.2)");
    int a_no_x   = f.add_assert("minimal x drift (|x| < 0.5)");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 0.0f, 100.0f);
    f.engine.get_humanoid_locomotion().set_body_relative_velocity(f.h.hips_id, 1.5f, 0.0f);
    f.begin_rotation_tracking(/*allow_turn=*/false, /*has_look_at=*/true);

    const int FRAMES = 120;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();

        f.update_assert_value(a_north, "y=%.2f", hy);
        f.update_assert_value(a_speed, "%.2f m/s", spd);
        f.update_assert_value(a_no_x, "x=%.2f", hx);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f m/s", hx, hy, spd);
        f.draw_hud("15: Forward (Body-Rel)", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();

    int failures = 0;
    if (!f.resolve_assert(a_north, hy > 3.0f, "y=%.3f", hy)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 1.2f, "%.2f m/s", final_speed)) failures++;
    if (!f.resolve_assert(a_no_x, std::abs(hx) < 0.5f, "x=%.3f", hx)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("15: Forward (Body-Rel)");
    return failures;
}

// ========================================================================
// Test 16: Body-Relative With Turn
// Body starts facing North, set_body_relative_velocity(forward=1.5, right=0).
// Look-at target is East (100, 0). Body should gradually turn East,
// and movement should follow the body — displacement shifts from +Y to +X.
// ========================================================================
int test_body_rel_with_turn(TestFixture& f) {
    printf("\n--- Test 16: Body-Relative With Turn ---\n");
    f.begin_test("Forward=1.5 + look East. Body turns, movement follows body direction.");
    f.reset();

    int a_east   = f.add_assert("moved East after turn (x > 1.5)");
    int a_north  = f.add_assert("moved North initially (y > 1.0)");
    int a_turned = f.add_assert("body turned toward East (base_rotation > 0.5)");
    int a_speed  = f.add_assert("speed maintained (> 1.0 m/s at end)");
    f.add_rotation_asserts();

    f.engine.get_humanoid_locomotion().set_look_at_target(f.h.hips_id, 100.0f, 0.0f);
    f.engine.get_humanoid_locomotion().set_body_relative_velocity(f.h.hips_id, 1.5f, 0.0f);
    f.begin_rotation_tracking(/*allow_turn=*/true, /*has_look_at=*/true);

    const int FRAMES = 150;
    for (int i = 0; i < FRAMES; ++i) {
        if (f.should_exit()) return 0;
        f.step();
        f.sample_rotation();

        float hx, hy, hz;
        f.get_hips_pos(hx, hy, hz);
        float spd = f.measure_speed();
        float rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

        f.update_assert_value(a_east, "x=%.2f", hx);
        f.update_assert_value(a_north, "y=%.2f", hy);
        f.update_assert_value(a_turned, "rot=%.3f", rot);
        f.update_assert_value(a_speed, "%.2f m/s", spd);

        char live[128];
        snprintf(live, sizeof(live), "pos=(%.2f, %.2f) | speed=%.2f | rot=%.2f rad",
                 hx, hy, spd, rot);
        f.draw_hud("16: Body-Rel + Turn", i, FRAMES, live);
        f.present();
    }

    float hx, hy, hz;
    f.get_hips_pos(hx, hy, hz);
    float final_speed = f.measure_speed();
    float final_rot = f.engine.get_humanoid_locomotion().get_base_rotation(f.h.hips_id);

    int failures = 0;
    if (!f.resolve_assert(a_east, hx > 1.5f, "x=%.3f", hx)) failures++;
    if (!f.resolve_assert(a_north, hy > 1.0f, "y=%.3f", hy)) failures++;
    if (!f.resolve_assert(a_turned, final_rot > 0.5f, "rot=%.3f", final_rot)) failures++;
    if (!f.resolve_assert(a_speed, final_speed > 1.0f, "%.2f m/s", final_speed)) failures++;
    failures += f.resolve_rotation_asserts();

    f.show_results("16: Body-Rel + Turn");
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
    printf("  HUMANOID MOVEMENT TEST\n");
    printf("  Mode: %s\n", headless ? "Headless" : "Visual");
    if (!headless) printf("  SPACE = next test, ESC = exit\n");
    printf("========================================\n");

    TestFixture f;
    if (!f.init(headless)) return 1;

    int total_failures = 0;

    struct TestEntry {
        const char* name;
        int (*func)(TestFixture&);
    };

    TestEntry tests[] = {
        {"1: Speed Achievement",       test_speed_achievement},
        {"2: Direction After Turn",    test_direction_after_turn},
        {"3: Grass Traversal",         test_grass_traversal},
        {"4: Deceleration/Stop",       test_deceleration},
        {"5: Step Block vs Grass",     test_step_vs_grass},
        {"6: Diagonal Walking",        test_diagonal_walking},
        {"7: Start-Stop-Start",        test_start_stop_start},
        {"8: Impulse Knockback",       test_impulse_knockback},
        {"9: Backward Walking",        test_backward_walking},
        {"10: Strafing",               test_strafing},
        {"11: Direction Change",       test_direction_change},
        {"12: Backward (Body-Rel)",    test_body_rel_backward},
        {"13: Strafe Right (Body-Rel)",test_body_rel_strafe_right},
        {"14: Strafe Left (Body-Rel)", test_body_rel_strafe_left},
        {"15: Forward (Body-Rel)",     test_body_rel_forward},
        {"16: Body-Rel + Turn",        test_body_rel_with_turn},
    };

    for (auto& test : tests) {
        if (!headless) {
            if (!f.wait_for_space(test.name)) break;
            f.reset();
        }
        total_failures += test.func(f);
    }

    if (!headless) {
        f.wait_for_space("EXIT (or press ESC)");
    }

    printf("\n========================================\n");
    printf("  HUMANOID MOVEMENT TEST COMPLETE\n");
    printf("========================================\n");

    if (total_failures > 0) {
        printf("  *** %d ASSERTION FAILURES ***\n", total_failures);
        return 1;
    }
    printf("  ALL ASSERTIONS PASSED\n");
    return 0;
}
