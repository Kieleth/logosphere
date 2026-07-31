#include "logosphere/dynamics/animation_controller.h"
#include <iostream>

void AnimationController::register_clip(const std::string& name, const AnimationClip& clip) {
    clips_[name] = clip;
}

bool AnimationController::play(const std::string& name) {
    auto it = clips_.find(name);
    if (it == clips_.end()) {
        std::cerr << "[AnimController] Animation not found: " << name << std::endl;
        return false;
    }

    state_.start(&it->second);
    std::cout << "[AnimController] Playing: " << name
              << " (duration: " << it->second.duration_ms << "ms)" << std::endl;
    return true;
}

void AnimationController::stop() {
    state_.stop();
}

bool AnimationController::is_playing() const {
    return state_.is_playing;
}

std::string AnimationController::current_animation_name() const {
    if (!state_.is_playing || !state_.clip) return "";
    return state_.clip->name;
}

bool AnimationController::update(float dt_seconds) {
    return state_.update(dt_seconds * 1000.0f);  // Convert to ms
}

bool AnimationController::get_current_pose(AnimationPose& out_pose) const {
    return state_.get_current_pose(out_pose);
}

void AnimationController::set_rest_position(unsigned int particle_id, float x, float y, float z) {
    state_.rest_positions[particle_id] = std::make_tuple(x, y, z);
}

bool AnimationController::get_world_target(unsigned int particle_id, const AnimationPose& pose,
                                           float facing_angle,
                                           float& out_x, float& out_y, float& out_z) const {
    auto rest_it = state_.rest_positions.find(particle_id);
    if (rest_it == state_.rest_positions.end()) {
        return false;
    }

    // Rest position is stored in LOCAL body space (set_rest_position converts to local)
    auto [local_rest_x, local_rest_y, rest_z] = rest_it->second;

    // Get animation offset (also in local space)
    float local_anim_x = 0.0f, local_anim_y = 0.0f, local_anim_z = 0.0f;
    for (const auto& target : pose.targets) {
        if (target.particle_id == particle_id) {
            local_anim_x = target.offset_x;
            local_anim_y = target.offset_y;
            local_anim_z = target.offset_z;
            break;
        }
    }

    // Combine rest + animation in LOCAL space
    float local_total_x = local_rest_x + local_anim_x;
    float local_total_y = local_rest_y + local_anim_y;
    float local_total_z = rest_z + local_anim_z;

    // Rotate combined offset to WORLD space using facing_angle
    // Convention: facing_angle=0 → face +Y (north), π/2 → face +X (east)
    // Body's local axes when at angle θ:
    //   Local +Y (forward) = world (sin(θ), cos(θ))
    //   Local +X (right)   = world (cos(θ), -sin(θ))
    float cos_a = std::cos(facing_angle);
    float sin_a = std::sin(facing_angle);

    // Transform: world = local_x * right_axis + local_y * forward_axis
    out_x = local_total_x * cos_a + local_total_y * sin_a;
    out_y = -local_total_x * sin_a + local_total_y * cos_a;
    out_z = local_total_z;

    // DEBUG: Print animation calculation for hand particle
    static int debug_count = 0;
    if (debug_count++ < 30 && (local_anim_x != 0.0f || local_anim_y != 0.0f)) {
        std::cout << "[ANIM_CALC] pid=" << particle_id
                  << " local_rest=(" << local_rest_x << "," << local_rest_y << ")"
                  << " local_anim=(" << local_anim_x << "," << local_anim_y << ")"
                  << " facing=" << (facing_angle * 180.0f / 3.14159f) << "deg"
                  << " -> world=(" << out_x << "," << out_y << ")" << std::endl;
    }

    return true;
}

void AnimationController::calculate_motor_force(
    float target_x, float target_y, float target_z,
    float current_x, float current_y, float current_z,
    float vel_x, float vel_y, float vel_z,
    const MotorGains& gains,
    float& out_fx, float& out_fy, float& out_fz
) {
    // PD controller: F = Kp * (target - pos) - Kd * vel
    out_fx = gains.Kp * (target_x - current_x) - gains.Kd * vel_x;
    out_fy = gains.Kp * (target_y - current_y) - gains.Kd * vel_y;
    out_fz = gains.Kp * (target_z - current_z) - gains.Kd * vel_z;
}

MotorGains& AnimationController::gains() {
    return gains_;
}

const MotorGains& AnimationController::gains() const {
    return gains_;
}
