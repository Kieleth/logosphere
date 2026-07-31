#include "ai/head.h"

#include <algorithm>
#include <cmath>

namespace logotron::ai {

namespace {
constexpr float kPi   = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
}

float shortest_arc_delta(float from, float to) {
    float d = std::fmod(to - from, kTwoPi);
    if (d >  kPi) d -= kTwoPi;
    if (d < -kPi) d += kTwoPi;
    return d;
}

void step_head(HeadState& h, float dt) {
    if (dt <= 0.0f) return;
    float delta = shortest_arc_delta(h.current_yaw, h.target_yaw);
    float max_step = h.max_rate * dt;
    if (std::fabs(delta) <= max_step) {
        // Arrived within this frame — snap. Prevents chatter from
        // per-frame rounding.
        h.current_yaw = h.target_yaw;
        return;
    }
    // Step toward target, preserving sign of delta.
    h.current_yaw += (delta > 0.0f ? +max_step : -max_step);
    // Wrap back into (-π, π] so downstream consumers see a clean
    // range; any renderer / particle sink that wraps internally will
    // still behave, but this keeps telemetry readable.
    if (h.current_yaw >  kPi) h.current_yaw -= kTwoPi;
    if (h.current_yaw < -kPi) h.current_yaw += kTwoPi;
}

} // namespace logotron::ai
