#include "logosphere/dynamics/animation_types.h"
#include <cmath>
#include <iostream>

// AnimationPose

void AnimationPose::add_target(unsigned int particle_id, float ox, float oy, float oz) {
    targets.push_back({particle_id, ox, oy, oz});
}

// AnimationClip

void AnimationClip::add_keyframe(float time_ms, const AnimationPose& pose) {
    keyframes.push_back({time_ms, pose});
    if (time_ms > duration_ms) {
        duration_ms = time_ms;
    }
}

bool AnimationClip::get_pose_at_time(float time_ms, AnimationPose& out_pose) const {
    if (keyframes.empty()) return false;

    // Handle looping
    float t = time_ms;
    if (loops && duration_ms > 0.0f) {
        t = fmodf(time_ms, duration_ms);
    } else if (time_ms >= duration_ms) {
        out_pose = keyframes.back().pose;
        return false;  // Animation complete
    }

    // Find surrounding keyframes
    size_t next_idx = 0;
    for (size_t i = 0; i < keyframes.size(); i++) {
        if (keyframes[i].time_ms > t) {
            next_idx = i;
            break;
        }
        next_idx = i + 1;
    }

    // Edge cases
    if (next_idx == 0) {
        out_pose = keyframes[0].pose;
        return true;
    }
    if (next_idx >= keyframes.size()) {
        out_pose = keyframes.back().pose;
        return true;
    }

    // Interpolate between prev and next keyframes
    const auto& prev = keyframes[next_idx - 1];
    const auto& next = keyframes[next_idx];

    float dt = next.time_ms - prev.time_ms;
    float alpha = (dt > 0.0f) ? (t - prev.time_ms) / dt : 0.0f;

    // DEBUG: Log keyframe interpolation
    static int pose_debug_count = 0;
    bool should_log = (pose_debug_count++ < 200);

    if (should_log) {
        std::cout << "[KEYFRAME] t=" << t << "ms keyframe " << (next_idx-1)
                  << "→" << next_idx << " alpha=" << alpha << std::endl;
    }

    // Linear interpolation of all targets
    out_pose.targets.clear();
    for (size_t i = 0; i < prev.pose.targets.size(); i++) {
        const auto& pt = prev.pose.targets[i];

        // Find matching target in next keyframe
        float nx = pt.offset_x, ny = pt.offset_y, nz = pt.offset_z;
        for (const auto& nt : next.pose.targets) {
            if (nt.particle_id == pt.particle_id) {
                nx = nt.offset_x;
                ny = nt.offset_y;
                nz = nt.offset_z;
                break;
            }
        }

        // Lerp
        float lerp_x = pt.offset_x + alpha * (nx - pt.offset_x);
        float lerp_y = pt.offset_y + alpha * (ny - pt.offset_y);
        float lerp_z = pt.offset_z + alpha * (nz - pt.offset_z);

        out_pose.add_target(pt.particle_id, lerp_x, lerp_y, lerp_z);

        // DEBUG: Log lerp result for first particle (hand)
        if (should_log && i == 0) {
            std::cout << "  [LERP] pid=" << pt.particle_id
                      << " prev=(" << pt.offset_x << "," << pt.offset_y << "," << pt.offset_z << ")"
                      << " next=(" << nx << "," << ny << "," << nz << ")"
                      << " -> lerp=(" << lerp_x << "," << lerp_y << "," << lerp_z << ")" << std::endl;
        }
    }

    return true;
}

// AnimationState

void AnimationState::start(const AnimationClip* animation_clip) {
    clip = animation_clip;
    current_time_ms = 0.0f;
    is_playing = true;
}

void AnimationState::stop() {
    is_playing = false;
    current_time_ms = 0.0f;
}

bool AnimationState::update(float dt_ms) {
    if (!is_playing || !clip) return false;

    current_time_ms += dt_ms;

    if (!clip->loops && current_time_ms >= clip->duration_ms) {
        is_playing = false;
        return false;
    }

    return true;
}

bool AnimationState::get_current_pose(AnimationPose& out_pose) const {
    if (!is_playing || !clip) return false;
    return clip->get_pose_at_time(current_time_ms, out_pose);
}
