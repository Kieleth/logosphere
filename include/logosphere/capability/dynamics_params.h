// DynamicsParams: Game-overridable dynamics parameters derived from CapabilityProfile.
//
// These are the 30+ parameters the dynamics system consumes each frame: speeds,
// turn rates, gaze dead zones, animation timing, friction, etc.
//
// The static from_capability() method implements the FORGE-derived defaults
// (calibrated against a reference human at 250ms reflexes, 500W grit, 75kg).
// Games can call this as-is, modify individual fields after, or compute
// DynamicsParams entirely from scratch.
//
// Engine code reads from DynamicsParams. Games control what goes into it.

#pragma once

struct CapabilityProfile;

struct DynamicsParams {
    // Locomotion
    float max_walk_speed = 0;
    float max_run_speed = 0;
    float max_acceleration = 0;
    float max_deceleration = 0;

    // Stride
    float walk_stride_length = 0;
    float run_stride_length = 0;

    // Turn rates
    float walk_turn_rate = 0;
    float stand_turn_rate = 0;
    float body_turn_rate = 0;
    float walk_turn_threshold = 0;

    // Look-at spring-damper
    float look_at_stiffness = 0;
    float look_at_damping = 0;

    // Gaze dead zones
    float eye_dead_zone = 0;
    float head_engage_zone = 0;
    float neck_dead_zone = 0;
    float torso_dead_zone = 0;
    float hips_dead_zone = 0;

    // Gaze timing
    float gaze_hold_threshold = 0;
    float gaze_offset_threshold = 0;

    // Animation timing
    float walk_swing_ms = 0;
    float walk_contact_ms = 0;
    float run_swing_ms = 0;
    float run_contact_ms = 0;
    float head_stabilize_walk = 0;
    float head_stabilize_run = 0;

    // Walk/run blend
    float run_blend_rise_rate = 0;
    float run_blend_fall_rate = 0;

    // Friction
    float moving_friction = 0;
    float stationary_friction = 0;

    // Default derivation from capability profile.
    // Contains the FORGE-calibrated formulas as a starting point.
    // Games can use this directly or override individual fields.
    static DynamicsParams from_capability(const CapabilityProfile& cap);
};
