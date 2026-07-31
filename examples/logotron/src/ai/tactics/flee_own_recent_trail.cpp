#include "ai/tactics/flee_own_recent_trail.h"

#include <algorithm>

namespace logotron::ai {

// All tactic scores must live in [-1, +1] so weight blending in
// decide() stays balanced. This tactic previously returned raw
// meters (run-length could easily exceed 30 on a 40-m arena), so its
// weighted contribution drowned every other tactic — the AI drove
// straight into walls because "go further from where I started"
// outweighed "stop, there's a wall in 1 m". Normalize against a
// conservative run length (half the arena's shorter side) and clamp.
namespace {
constexpr float kRunScale = 20.0f;  // meters — half of 40 m arena
}

float FleeOwnRecentTrailTactic::score(logotron::Direction candidate,
                                      const PerceivedWorld& pw) const {
    float dx = pw.self.x - pw.self.run_start_x;  // positive when self east of run start
    float dy = pw.self.y - pw.self.run_start_y;  // positive when self north of run start

    // Direction closing distance to run_start → negative (we want to
    // stay AWAY). Direction opening → positive.
    auto signed_bias = [&](int sx, int sy) {
        // component along (sx, sy): dot with the unit cardinal axis
        float along = sx * dx + sy * dy;
        return std::clamp(along / kRunScale, -1.0f, 1.0f);
    };
    switch (candidate) {
        case logotron::Direction::NORTH: return signed_bias(0, +1);
        case logotron::Direction::SOUTH: return signed_bias(0, -1);
        case logotron::Direction::EAST:  return signed_bias(+1, 0);
        case logotron::Direction::WEST:  return signed_bias(-1, 0);
    }
    return 0.0f;
}

} // namespace logotron::ai
