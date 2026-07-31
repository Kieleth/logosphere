// Animation Controller - Motor Force Animation Management
//
// Manages animation playback and calculates motor forces for physics-driven animation.
// Motor forces use PD controller: F = Kp*(target-pos) - Kd*vel

#ifndef LOGOSPHERE_ANIMATION_CONTROLLER_H
#define LOGOSPHERE_ANIMATION_CONTROLLER_H

#include "logosphere/dynamics/animation_types.h"
#include <unordered_map>
#include <string>

// PD Controller gains for motor forces
// High Kp needed to overcome gluon constraint solver (16 iterations)
struct MotorGains {
    float Kp = 5000.0f;   // Position gain (N/m) - high to overcome constraints
    float Kd = 100.0f;    // Damping gain (N·s/m)
};

// Animation Controller manages animation playback for an entity
class AnimationController {
public:
    AnimationController() = default;

    void register_clip(const std::string& name, const AnimationClip& clip);
    bool play(const std::string& name);
    void stop();
    bool is_playing() const;
    std::string current_animation_name() const;
    bool update(float dt_seconds);
    bool get_current_pose(AnimationPose& out_pose) const;
    void set_rest_position(unsigned int particle_id, float x, float y, float z);
    bool get_world_target(unsigned int particle_id, const AnimationPose& pose,
                          float facing_angle,  // radians: 0 = +Y (north), rotates offsets to world
                          float& out_x, float& out_y, float& out_z) const;

    static void calculate_motor_force(
        float target_x, float target_y, float target_z,
        float current_x, float current_y, float current_z,
        float vel_x, float vel_y, float vel_z,
        const MotorGains& gains,
        float& out_fx, float& out_fy, float& out_fz
    );

    MotorGains& gains();
    const MotorGains& gains() const;

private:
    std::unordered_map<std::string, AnimationClip> clips_;
    AnimationState state_;
    MotorGains gains_;
};

#endif // LOGOSPHERE_ANIMATION_CONTROLLER_H
