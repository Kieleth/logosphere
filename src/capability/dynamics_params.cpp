// DynamicsParams::from_capability()
//
// FORGE-calibrated default derivation. Calibrated against a reference human:
// 250ms reflexes, 500W grit, 75kg, 0.9m legs, 1.8m height.
//
// Games can use this as a default and override individual fields, or compute
// DynamicsParams entirely from scratch for non-humanoid entities.

#include "logosphere/capability/dynamics_params.h"
#include "logosphere/capability/capability_profile.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// Calibration constants
// ============================================================================

static constexpr float REF_REFLEXES = 250.0f;
static constexpr float REF_GRIT     = 500.0f;
static constexpr float REF_LEG      = 0.9f;
static constexpr float GRAVITY      = 9.81f;

// Look-at spring-damper
static constexpr float STIFFNESS_BASE  = 8000.0f;
static constexpr float DAMPING_RATIO   = 0.15f;

// Locomotion
static constexpr float WALK_EFFICIENCY = 0.71f;
static constexpr float RUN_EFFICIENCY  = 1.63f;
static constexpr float ACCEL_FACTOR    = 0.94f;

// Turn rates
static constexpr float BASE_WALK_TURN  = 2.0f;
static constexpr float BASE_STAND_TURN = 3.5f;
static constexpr float BASE_GAZE_TURN  = 5.0f;

// Gaze dead zones
static constexpr float REF_EYE_DEAD_ZONE    = 0.44f;
static constexpr float REF_HEAD_ENGAGE_ZONE = 0.70f;
static constexpr float REF_NECK_DEAD_ZONE   = 0.52f;
static constexpr float REF_TORSO_DEAD_ZONE  = 0.35f;
static constexpr float REF_HIPS_DEAD_ZONE   = 0.26f;

// Gaze timing
static constexpr float REF_GAZE_HOLD   = 1.0f;
static constexpr float REF_GAZE_OFFSET = 0.26f;

// Walk/run blend
static constexpr float REF_BLEND_RISE = 10.0f;
static constexpr float REF_BLEND_FALL = 5.0f;

// Stride
static constexpr float REF_WALK_STRIDE = 0.65f;
static constexpr float REF_RUN_STRIDE  = 0.85f;

// Animation timing
static constexpr float REF_WALK_SWING    = 400.0f;
static constexpr float REF_WALK_CONTACT  = 200.0f;
static constexpr float REF_RUN_SWING     = 300.0f;
static constexpr float REF_RUN_CONTACT   = 100.0f;

// Head stabilization
static constexpr float REF_HEAD_STAB_WALK = 0.4f;
static constexpr float REF_HEAD_STAB_RUN  = 0.3f;

// Friction
static constexpr float MOVING_FRICTION_VAL     = 0.0f;
static constexpr float STATIONARY_FRICTION_VAL = 0.8f;

// Turn threshold (~10 degrees, anatomical)
static constexpr float WALK_TURN_THRESHOLD_VAL = 0.17f;

// ============================================================================

DynamicsParams DynamicsParams::from_capability(const CapabilityProfile& cap) {
    DynamicsParams d;

    float reflexes_ms = cap.reflexes_ms < 1.0f ? 1.0f : cap.reflexes_ms;
    float mass        = cap.mass < 0.1f ? 75.0f : cap.mass;
    float leg_length  = cap.leg_length < 0.01f ? REF_LEG : cap.leg_length;
    float grit_W      = cap.grit_W;

    float reflex_scale = reflexes_ms / REF_REFLEXES;
    float reflex_inv   = REF_REFLEXES / reflexes_ms;
    float power_factor = std::sqrt(grit_W / REF_GRIT);
    float turn_factor  = reflex_inv * power_factor;
    float leg_scale    = leg_length / REF_LEG;
    float speed_base   = std::sqrt(grit_W / mass);

    // Look-at (perception affects stiffness)
    d.look_at_stiffness = (STIFFNESS_BASE / reflexes_ms) * power_factor * cap.perception_factor;
    d.look_at_damping   = d.look_at_stiffness * DAMPING_RATIO;

    // Gaze dead zones
    d.eye_dead_zone    = REF_EYE_DEAD_ZONE    * reflex_scale;
    d.head_engage_zone = REF_HEAD_ENGAGE_ZONE * reflex_scale;
    d.neck_dead_zone   = REF_NECK_DEAD_ZONE   * reflex_scale;
    d.torso_dead_zone  = REF_TORSO_DEAD_ZONE  * reflex_scale;
    d.hips_dead_zone   = REF_HIPS_DEAD_ZONE   * reflex_scale;

    // Gaze timing
    d.gaze_hold_threshold   = REF_GAZE_HOLD * reflex_scale;
    d.gaze_offset_threshold = REF_GAZE_OFFSET;

    // Locomotion (speed_cap from response rules baked in here)
    d.max_walk_speed   = speed_base * WALK_EFFICIENCY * cap.locomotion_factor * cap.speed_cap;
    d.max_run_speed    = speed_base * RUN_EFFICIENCY  * cap.locomotion_factor * cap.speed_cap;
    d.max_acceleration = (grit_W / mass) * ACCEL_FACTOR * cap.locomotion_factor;
    d.max_deceleration = STATIONARY_FRICTION_VAL * GRAVITY;

    // Turn rates
    d.walk_turn_rate  = turn_factor * BASE_WALK_TURN  * cap.rotation_factor;
    d.stand_turn_rate = turn_factor * BASE_STAND_TURN * cap.rotation_factor;
    d.body_turn_rate  = turn_factor * BASE_GAZE_TURN  * cap.rotation_factor;

    // Walk/run blend
    d.run_blend_rise_rate = REF_BLEND_RISE * reflex_inv;
    d.run_blend_fall_rate = REF_BLEND_FALL * reflex_inv;

    // Stride
    d.walk_stride_length = REF_WALK_STRIDE * leg_scale * cap.locomotion_factor;
    d.run_stride_length  = REF_RUN_STRIDE  * leg_scale * cap.locomotion_factor;

    // Animation timing
    d.walk_swing_ms   = REF_WALK_SWING   * reflex_scale;
    d.walk_contact_ms = REF_WALK_CONTACT * reflex_scale;
    d.run_swing_ms    = REF_RUN_SWING    * reflex_scale;
    d.run_contact_ms  = REF_RUN_CONTACT  * reflex_scale;

    // Head stabilization
    float stab_scale = std::clamp(reflex_inv, 0.7f, 1.3f);
    d.head_stabilize_walk = REF_HEAD_STAB_WALK * stab_scale;
    d.head_stabilize_run  = REF_HEAD_STAB_RUN  * stab_scale;

    // Friction
    d.moving_friction     = MOVING_FRICTION_VAL;
    d.stationary_friction = STATIONARY_FRICTION_VAL;

    // Thresholds
    d.walk_turn_threshold = WALK_TURN_THRESHOLD_VAL;

    return d;
}
