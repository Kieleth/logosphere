#include "core/camera_director.h"
#include "core/camera_system.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
inline void capture_pose(const CameraSystem& cam,
                         float pos[3], float look_at[3]) {
    cam.get_position(pos[0], pos[1], pos[2]);
    cam.get_look_at_target(look_at[0], look_at[1]);
    // CameraSystem's look-at z isn't exposed directly; the dolly
    // re-aims at the target's z explicitly so this stays planar.
    look_at[2] = 0.0f;
}

inline void lerp3(const float a[3], const float b[3], float t, float out[3]) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}
}  // namespace

float CameraDirector::smoothstep5(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

void CameraDirector::focus_on(float target_x, float target_y, float target_z,
                              float distance, float tilt_deg,
                              float ease_seconds) {
    assert(camera_ && "CameraDirector::focus_on before bind()");

    // First focus snapshots the home pose. Subsequent retargets
    // keep the original home so release() still goes back there.
    if (!has_pre_) {
        capture_pose(*camera_, pre_pos_, pre_look_at_);
        has_pre_ = true;
    }

    // Compute target camera position. Keep the horizontal direction
    // the camera was already looking from (so the dolly approaches
    // from a familiar angle), pull in to `distance`, raise to
    // `tilt_deg` above horizontal.
    float cx, cy, cz;
    camera_->get_position(cx, cy, cz);
    float vx = cx - target_x;
    float vy = cy - target_y;
    float horizontal = std::sqrt(vx * vx + vy * vy);
    if (horizontal < 1e-4f) {
        // Camera is directly above/below the target — fall back to
        // approaching from -Y (south) so the dolly has a defined direction.
        vx = 0.0f;
        vy = -1.0f;
        horizontal = 1.0f;
    }
    const float inv_h = 1.0f / horizontal;
    const float dir_x = vx * inv_h;
    const float dir_y = vy * inv_h;

    const float tilt_rad   = tilt_deg * 3.14159265358979323846f / 180.0f;
    const float horiz_dist = distance * std::cos(tilt_rad);
    const float vert_dist  = distance * std::sin(tilt_rad);

    target_pos_[0]     = target_x + dir_x * horiz_dist;
    target_pos_[1]     = target_y + dir_y * horiz_dist;
    target_pos_[2]     = target_z + vert_dist;
    target_look_at_[0] = target_x;
    target_look_at_[1] = target_y;
    target_look_at_[2] = target_z;

    // New segment: from current actual pose to target.
    capture_pose(*camera_, segment_from_pos_, segment_from_look_at_);
    segment_t_   = 0.0f;
    segment_dur_ = std::max(0.0f, ease_seconds);
    phase_       = Phase::Engaging;

    // ease_seconds == 0: snap immediately, then go straight to hold.
    if (segment_dur_ == 0.0f) {
        apply_pose(1.0f);
        phase_ = Phase::Holding;
    }
}

void CameraDirector::release(float ease_seconds) {
    if (!has_pre_ || phase_ == Phase::Idle) return;

    // New segment: from wherever we are now back to the home pose.
    capture_pose(*camera_, segment_from_pos_, segment_from_look_at_);
    target_pos_[0]     = pre_pos_[0];
    target_pos_[1]     = pre_pos_[1];
    target_pos_[2]     = pre_pos_[2];
    target_look_at_[0] = pre_look_at_[0];
    target_look_at_[1] = pre_look_at_[1];
    target_look_at_[2] = pre_look_at_[2];
    segment_t_   = 0.0f;
    segment_dur_ = std::max(0.0f, ease_seconds);
    phase_       = Phase::Releasing;

    if (segment_dur_ == 0.0f) {
        apply_pose(1.0f);
        phase_  = Phase::Idle;
        has_pre_ = false;  // home consumed; next focus_on captures fresh.
    }
}

void CameraDirector::update(float real_dt) {
    if (phase_ == Phase::Idle || phase_ == Phase::Holding) return;
    if (segment_dur_ <= 0.0f) return;

    segment_t_ += real_dt;
    float linear = segment_t_ / segment_dur_;
    bool done = (linear >= 1.0f);
    if (done) linear = 1.0f;

    apply_pose(smoothstep5(linear));

    if (done) {
        if (phase_ == Phase::Engaging) {
            phase_ = Phase::Holding;
        } else /* Releasing */ {
            phase_   = Phase::Idle;
            has_pre_ = false;
        }
    }
}

void CameraDirector::apply_pose(float alpha) {
    assert(camera_);
    float pos[3], look[3];
    lerp3(segment_from_pos_,     target_pos_,     alpha, pos);
    lerp3(segment_from_look_at_, target_look_at_, alpha, look);
    camera_->set_position(pos[0], pos[1], pos[2]);
    camera_->look_at(look[0], look[1], look[2]);
}
