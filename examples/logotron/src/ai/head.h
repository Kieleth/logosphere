// Rider head — independent rotation state for the embodied driver.
//
// The bike has its own heading (travel direction). The rider has a
// SEPARATE head rotation that follows a target the AI can change each
// decision tick. Head rotation is rate-limited so turning to look
// over the shoulder costs real time — this is the mechanism that
// lets opponents exploit blind spots.
//
// See GAME_DESIGN.md §16 for the design stance.
//
// Pure data + pure step function. No dependency on KG or particles
// so this is trivially unit-testable headless. The integration layer
// (main.cpp) copies `current_yaw` into the rider particle's
// local_yaw each frame.

#pragma once

namespace logotron::ai {

struct HeadState {
    float current_yaw = 0.0f;  // radians, CW around world Z (engine convention)
    float target_yaw  = 0.0f;  // where the rider WANTS to look
    float max_rate    = 4.0f;  // radians/second — personality-tunable
};

// Advance current_yaw toward target_yaw by at most max_rate * dt,
// taking the shortest arc (so e.g. looking from +170° to -170°
// travels 20° through π, not 340° the long way).
void step_head(HeadState& h, float dt);

// Utility: shortest-arc delta from `from` to `to`, in (-π, π].
// Used by step_head; exposed for tests + for tactics that want to
// know "how far is my head from where I want it".
float shortest_arc_delta(float from, float to);

} // namespace logotron::ai
