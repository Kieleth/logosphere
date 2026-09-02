// Isolated Animation Test
//
// Pure dynamics/animation testing for humanoids.
// No movement, no combat, no AI - just animation playback.
//
// Controls:
//   SPACE = cycle through states (jab → cross → spawn #2 → bite → jab...)
//   ESC = exit

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "animation/punch_animation.h"
#include "animation/bite_animation.h"
#include <iostream>
#include <GLFW/glfw3.h>

// The floor these scenes build is 0.1 m thick with its bottom on the
// turtle, so its walking surface is 0.10 m. world_z for a humanoid is
// the FEET'S BOTTOM: spawning at 0.0 buried them 80 mm in the slab,
// which the creation door refuses (INV-37).
static constexpr float FLOOR_TOP = 0.10f;

// ========================================
// ISOMETRIC FACING DIRECTIONS
// ========================================
// Convention: rotation_z = 0 → facing North (+Y)
// CLOCKWISE rotation as angle increases
//
// Screen appearance (camera at SW looking NE):
//   N = upper-left diagonal (10:30 clock)
//   E = upper-right diagonal (1:30 clock)
//   S = lower-right diagonal (4:30 clock)
//   W = lower-left diagonal (7:30 clock)
constexpr float FACING_N = 0.0f;                           // +Y
constexpr float FACING_E = static_cast<float>(M_PI / 2.0); // +X
constexpr float FACING_S = static_cast<float>(M_PI);       // -Y
constexpr float FACING_W = static_cast<float>(-M_PI / 2.0); // -X

enum TestState {
    STATE_JAB_READY,     // Waiting to jab on humanoid #1
    STATE_CROSS_READY,   // Jab done, waiting for cross (first time, leads to spawn)
    STATE_SPAWN_READY,   // Cross done, waiting to spawn #2
    STATE_BITE_READY,    // #2 spawned, waiting to bite
    STATE_JAB_CYCLE,     // After bite, cycle back to jab
    STATE_CROSS_CYCLE    // After jab cycle, cross that goes to bite (not spawn)
};

int main(int, char**) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  ISOLATED ANIMATION TEST" << std::endl;
    std::cout << "  Pure dynamics testing" << std::endl;
    std::cout << "  SPACE: jab → cross → spawn → bite..." << std::endl;
    std::cout << "  ESC: exit" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Engine setup
    Engine engine;
    EngineConfig config;
    config.mode = EngineMode::Interactive;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "Animation Test";
    config.show_debug_overlay = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cerr << "Engine init failed" << std::endl;
        return 1;
    }

    auto& ps = engine.get_particle_system();
    auto& dynamics = engine.get_dynamics_system();
    auto& kg = engine.get_kg();
    auto& input = engine.get_input_system();

    // ========================================
    // SIMPLE FLOOR + LIGHT
    // ========================================
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.05f;
    floor.shape = ParticleShape::BOX;
    floor.width = 10.0f; floor.height = 10.0f; floor.thickness = 0.1f;
    floor.r = 0.3f; floor.g = 0.25f; floor.b = 0.2f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    engine.add_particle(floor);

    // 4 lights for good visibility
    auto add_light = [&](float x, float y, float z) {
        Particle light = {};
        light.x = x; light.y = y; light.z = z;
        light.shape = ParticleShape::BOX;
        light.width = 0.2f; light.height = 0.2f; light.thickness = 0.2f;
        light.is_light_source = true;
        light.emission_strength = 150000.0f;
        light.emission_radius = 30.0f;
        light.r = 1.0f; light.g = 0.95f; light.b = 0.9f;
        light.a = 0.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        engine.add_particle(light);
    };
    add_light(-3.0f, -3.0f, 8.0f);  // SW
    add_light(3.0f, -3.0f, 8.0f);   // SE
    add_light(-3.0f, 3.0f, 8.0f);   // NW
    add_light(3.0f, 3.0f, 8.0f);    // NE

    // ========================================
    // HUMANOID GENERATOR
    // ========================================
    HumanoidGenerator humanoid_gen;
    humanoid_gen.initialize(&engine, &kg);

    // ========================================
    // HUMANOID #1: PUNCH TEST (Blue)
    // ========================================
    HumanoidSpec spec1 = HumanoidSpec::hunter();
    spec1.facing_angle = 0.0f;  // Generate facing north (rotation applied after registration)
    spec1.clothing_r = 0.2f;
    spec1.clothing_g = 0.3f;
    spec1.clothing_b = 0.8f;

    // world_z is the feet's BOTTOM and this floor's top is 0.10: spawning at
    // zero buried every foot 80 mm inside the slab (INV-37).
    auto h1 = humanoid_gen.generate_humanoid_physics(0.0f, 0.0f, FLOOR_TOP, -1, spec1, false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        h1.hips_id,
        h1.left_leg_ids, h1.right_leg_ids,
        h1.left_arm_ids, h1.right_arm_ids,
        h1.torso_ids,
        150.0f, 600.0f
    );
    engine.get_humanoid_locomotion().reset_humanoid_position(h1.hips_id);
    engine.get_humanoid_locomotion().set_volitional(h1.hips_id, false);  // Stationary

    // Register punch animations BEFORE setting facing direction
    // (rest positions must be captured while still facing North)
    int h1_shoulder_id = -1, h1_upper_arm_id = -1, h1_forearm_id = -1, h1_hand_id = -1;
    if (h1.right_arm_ids.size() >= 4) {
        h1_shoulder_id = h1.right_arm_ids[0];
        h1_upper_arm_id = h1.right_arm_ids[1];
        h1_forearm_id = h1.right_arm_ids[2];
        h1_hand_id = h1.right_arm_ids[3];

        // Register JAB (fast body shot) - includes full arm chain
        auto jab_clip = create_right_jab(
            static_cast<unsigned>(h1_hand_id),
            h1_upper_arm_id,
            h1_forearm_id,
            h1_shoulder_id
        );
        engine.get_humanoid_locomotion().register_animation(h1.hips_id, "right_jab", jab_clip);

        // Register CROSS (power head shot) - includes full arm chain
        auto cross_clip = create_right_cross(
            static_cast<unsigned>(h1_hand_id),
            h1_upper_arm_id,
            h1_forearm_id,
            h1_shoulder_id
        );
        engine.get_humanoid_locomotion().register_animation(h1.hips_id, "right_cross", cross_clip);

        // Set rest positions BEFORE facing change (particles still at North-facing positions)
        // Order: shoulder → upper_arm → forearm → hand (chain order)
        auto view = ps.lock_particles_for_read();
        engine.get_humanoid_locomotion().set_rest_position(h1.hips_id, h1_shoulder_id, view[h1_shoulder_id].x, view[h1_shoulder_id].y, view[h1_shoulder_id].z);
        engine.get_humanoid_locomotion().set_rest_position(h1.hips_id, h1_upper_arm_id, view[h1_upper_arm_id].x, view[h1_upper_arm_id].y, view[h1_upper_arm_id].z);
        engine.get_humanoid_locomotion().set_rest_position(h1.hips_id, h1_forearm_id, view[h1_forearm_id].x, view[h1_forearm_id].y, view[h1_forearm_id].z);
        engine.get_humanoid_locomotion().set_rest_position(h1.hips_id, h1_hand_id, view[h1_hand_id].x, view[h1_hand_id].y, view[h1_hand_id].z);
    }

    // NOW set facing direction (after rest positions are captured)
    // SOUTH + 75° clockwise = 255° (to see outward rotation of cross)
    constexpr float FACING_255 = static_cast<float>(M_PI + M_PI / 4.0 + M_PI / 6.0);  // 255°
    engine.get_humanoid_locomotion().set_facing_direction(h1.hips_id, FACING_255);
    std::cout << "[H1] Set facing 255° (75° clockwise from SOUTH)" << std::endl;

    std::cout << "[H1] Blue humanoid at (0,0), hips=" << h1.hips_id << std::endl;

    // ========================================
    // HUMANOID #2: BITE TEST (Red) - spawned later
    // ========================================
    int h2_hips_id = -1;
    int h2_head_id = -1;
    std::vector<int> h2_head_children;

    // ========================================
    // STATE MACHINE
    // ========================================
    TestState state = STATE_JAB_READY;
    bool space_was_pressed = false;

    std::cout << "\n[STATE] JAB_READY - Press SPACE for jab (body shot)" << std::endl;

    // ========================================
    // CAMERA
    // ========================================
    auto& camera = engine.get_camera_system();
    camera.set_position(-8.0f, -8.0f, 10.0f);
    camera.look_at(1.0f, 0.0f, 1.0f);  // Between both humanoids
    camera.set_pixels_per_unit(60.0f);

    // ========================================
    // MAIN LOOP
    // ========================================
    const double dt = 1.0 / 60.0;

    while (true) {
        engine.get_platform()->poll_events();

        const auto& input_state = input.get_input_state();
        if (input_state.keys[GLFW_KEY_ESCAPE] || engine.get_platform()->should_close()) {
            break;
        }

        // SPACE key handling (single press, not held)
        bool space_pressed = input_state.keys[GLFW_KEY_SPACE];
        if (space_pressed && !space_was_pressed) {
            switch (state) {
                case STATE_JAB_READY:
                    std::cout << "[ACTION] Playing JAB animation on H1 (body shot)" << std::endl;
                    engine.get_humanoid_locomotion().play_animation(h1.hips_id, "right_jab");
                    state = STATE_CROSS_READY;
                    std::cout << "[STATE] CROSS_READY - Press SPACE for cross (head shot)" << std::endl;
                    break;

                case STATE_CROSS_READY:
                    std::cout << "[ACTION] Playing CROSS animation on H1 (head shot)" << std::endl;
                    engine.get_humanoid_locomotion().play_animation(h1.hips_id, "right_cross");
                    state = STATE_SPAWN_READY;
                    std::cout << "[STATE] SPAWN_READY - Press SPACE to spawn H2" << std::endl;
                    break;

                case STATE_SPAWN_READY: {
                    std::cout << "[ACTION] Spawning humanoid #2 (Red)" << std::endl;

                    // Spawn humanoid #2
                    HumanoidSpec spec2 = HumanoidSpec::hunter();
                    spec2.facing_angle = 0.0f;  // Generate facing north (rotation applied after registration)
                    spec2.clothing_r = 0.8f;
                    spec2.clothing_g = 0.2f;
                    spec2.clothing_b = 0.2f;

                    auto h2 = humanoid_gen.generate_humanoid_physics(2.0f, 0.0f, FLOOR_TOP, -1, spec2, false);
                    h2_hips_id = h2.hips_id;

                    engine.get_humanoid_locomotion().register_humanoid_direct(
                        h2.hips_id,
                        h2.left_leg_ids, h2.right_leg_ids,
                        h2.left_arm_ids, h2.right_arm_ids,
                        h2.torso_ids,
                        150.0f, 600.0f
                    );
                    engine.get_humanoid_locomotion().reset_humanoid_position(h2.hips_id);
                    engine.get_humanoid_locomotion().set_volitional(h2.hips_id, false);  // Stationary

                    // Register bite animation BEFORE setting facing direction
                    if (h2.torso_ids.size() > 4) {
                        h2_head_id = h2.torso_ids[4];

                        // Collect head children (hair, ears)
                        h2_head_children.clear();
                        for (size_t i = 5; i < h2.torso_ids.size(); i++) {
                            h2_head_children.push_back(h2.torso_ids[i]);
                        }

                        auto bite_clip = create_head_bite(
                            static_cast<unsigned>(h2_head_id),
                            h2_head_children
                        );
                        engine.get_humanoid_locomotion().register_animation(h2.hips_id, "head_bite", bite_clip);

                        // Set rest positions BEFORE facing change
                        auto view = ps.lock_particles_for_read();
                        engine.get_humanoid_locomotion().set_rest_position(h2.hips_id, h2_head_id,
                            view[h2_head_id].x, view[h2_head_id].y, view[h2_head_id].z);
                        for (int child : h2_head_children) {
                            if (child >= 0) {
                                engine.get_humanoid_locomotion().set_rest_position(h2.hips_id, child,
                                    view[child].x, view[child].y, view[child].z);
                            }
                        }
                    }

                    // NOW set facing direction (after rest positions are captured)
                    engine.get_humanoid_locomotion().set_facing_direction(h2.hips_id, FACING_W);
                    std::cout << "[H2] Set facing WEST (towards H1)" << std::endl;

                    std::cout << "[H2] Red humanoid at (2,0), hips=" << h2_hips_id << std::endl;
                    state = STATE_BITE_READY;
                    std::cout << "[STATE] BITE_READY - Press SPACE for bite (H2)" << std::endl;
                    break;
                }

                case STATE_BITE_READY:
                    if (h2_hips_id >= 0) {
                        std::cout << "[ACTION] Playing bite animation on H2 (Red)" << std::endl;
                        engine.get_humanoid_locomotion().play_animation(h2_hips_id, "head_bite");
                    }
                    state = STATE_JAB_CYCLE;
                    std::cout << "[STATE] JAB_CYCLE - Press SPACE for jab (H1)" << std::endl;
                    break;

                case STATE_JAB_CYCLE:
                    std::cout << "[ACTION] Playing JAB animation on H1 (Blue)" << std::endl;
                    engine.get_humanoid_locomotion().play_animation(h1.hips_id, "right_jab");
                    state = STATE_CROSS_CYCLE;
                    std::cout << "[STATE] CROSS_CYCLE - Press SPACE for cross (H1)" << std::endl;
                    break;

                case STATE_CROSS_CYCLE:
                    std::cout << "[ACTION] Playing CROSS animation on H1 (Blue)" << std::endl;
                    engine.get_humanoid_locomotion().play_animation(h1.hips_id, "right_cross");
                    state = STATE_BITE_READY;
                    std::cout << "[STATE] BITE_READY - Press SPACE for bite (H2)" << std::endl;
                    break;
            }
        }
        space_was_pressed = space_pressed;

        // Update engine
        engine.update(dt);

        // Debug: print right hand position every 5 frames (more granular during animation)
        static int debug_frame = 0;
        debug_frame++;

        // AUTO-TRIGGER: Set shoulder angle to test FK (frame 3-10)
        if (debug_frame == 3) {
            std::cout << "[AUTO_TEST] Setting right_shoulder angle to -1.5 (90° down)" << std::endl;
            engine.get_humanoid_locomotion().set_joint_angle(h1.entity_id, "right_shoulder", -1.5f);
        }
        if (debug_frame == 10) {
            std::cout << "[AUTO_TEST] Resetting right_shoulder angle to 0" << std::endl;
            engine.get_humanoid_locomotion().set_joint_angle(h1.entity_id, "right_shoulder", 0.0f);
        }
        if (debug_frame % 5 == 0 && h1_hand_id >= 0) {
            auto view = ps.lock_particles_for_read();
            float hips_x = view[h1.hips_id].x;
            float hips_y = view[h1.hips_id].y;
            float hips_z = view[h1.hips_id].z;
            float hand_x = view[h1_hand_id].x;
            float hand_y = view[h1_hand_id].y;
            float hand_z = view[h1_hand_id].z;
            // Offset from hips in WORLD coords (includes Z!)
            float dx = hand_x - hips_x;
            float dy = hand_y - hips_y;
            float dz = hand_z - hips_z;  // Z offset shows jab vs cross difference
            std::cout << "[F" << debug_frame << "] R_Hand offset=(" << dx << "," << dy << "," << dz
                      << ") z=" << hand_z << std::endl;
        }

        // Render
        engine.render();
        engine.present();
    }

    std::cout << "\n=== TEST COMPLETE ===" << std::endl;
    return 0;
}
