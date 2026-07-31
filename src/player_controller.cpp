// Logosphere - PlayerController Implementation

#include "player_controller.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/input_system.h"
#include "platform/platform_system.h"
#include <cmath>
#include <iostream>
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

PlayerController::PlayerController()
    : engine_(nullptr) {
}

void PlayerController::initialize(Engine* engine) {
    engine_ = engine;
}

void PlayerController::set_controlled_entity(kg::EntityID entity_id, int hips_id) {
    entity_id_ = entity_id;
    hips_id_ = hips_id;
}

void PlayerController::clear_controlled_entity() {
    entity_id_ = kg::INVALID_ENTITY;
    hips_id_ = -1;
    hand_id_ = 0;
    space_was_pressed_ = false;
}

void PlayerController::update(float dt) {
    (void)dt;

    if (!engine_ || entity_id_ == kg::INVALID_ENTITY || hips_id_ < 0) {
        return;
    }

    update_mouse_look();
    update_movement();
    update_abilities();
}

void PlayerController::update_mouse_look() {
    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
    if (!window) return;

    double screen_mx, screen_my;
    glfwGetCursorPos(window, &screen_mx, &screen_my);

    float target_x, target_y;
    engine_->get_coord_transformer().screen_to_world(
        static_cast<int>(screen_mx), static_cast<int>(screen_my),
        target_x, target_y);

    engine_->get_humanoid_locomotion().set_look_at_target(hips_id_, target_x, target_y);
}

void PlayerController::update_movement() {
    auto& input = engine_->get_input_system();
    const auto& state = input.get_input_state();

    bool w_pressed = state.keys[GLFW_KEY_W];
    bool s_pressed = state.keys[GLFW_KEY_S];
    bool a_pressed = state.keys[GLFW_KEY_A];
    bool d_pressed = state.keys[GLFW_KEY_D];
    bool running = state.keys[GLFW_KEY_LEFT_SHIFT] || state.keys[GLFW_KEY_RIGHT_SHIFT];

    // Speed: use override if set, otherwise read derived limits from dynamics
    auto& dynamics = engine_->get_dynamics_system();
    float walk = walk_speed_override_ > 0.0f ? walk_speed_override_ : engine_->get_humanoid_locomotion().get_max_walk_speed(hips_id_);
    float run = run_speed_override_ > 0.0f ? run_speed_override_ : engine_->get_humanoid_locomotion().get_max_run_speed(hips_id_);
    float move_speed = running ? run : walk;

    // Body-relative input: W/S = forward/backward, A/D = strafe
    float local_forward = 0.0f;
    float local_right = 0.0f;
    if (w_pressed) local_forward += 1.0f;
    if (s_pressed) local_forward -= 1.0f;
    if (a_pressed) local_right -= 1.0f;
    if (d_pressed) local_right += 1.0f;

    // Normalize diagonal
    float input_mag = std::sqrt(local_forward * local_forward + local_right * local_right);
    if (input_mag > 1.0f) {
        local_forward /= input_mag;
        local_right /= input_mag;
    }

    // Speed modifiers: backward and strafe slower than forward
    float forward_vel = local_forward * move_speed;
    if (local_forward < 0) forward_vel *= BACKWARD_MULT;
    float strafe_vel = local_right * move_speed * STRAFE_MULT;

    // Dynamics handles: world-space transform, locomotion ramping, animation triggers
    engine_->get_humanoid_locomotion().set_body_relative_velocity(hips_id_, forward_vel, strafe_vel);
}

void PlayerController::update_abilities() {
    auto& input = engine_->get_input_system();
    const auto& state = input.get_input_state();

    bool space_pressed = state.keys[GLFW_KEY_SPACE];

    if (space_pressed && !space_was_pressed_) {
        if (on_punch) on_punch();
    }

    space_was_pressed_ = space_pressed;
}
