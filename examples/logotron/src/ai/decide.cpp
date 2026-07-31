#include "ai/decide.h"

#include <cmath>
#include <limits>

namespace logotron::ai {

Decision decide(const Personality& p, const PerceivedWorld& pw) {
    Decision out{};
    out.drive = pw.self.direction;

    // DRIVE — weighted sum of tactic scores over legal dirs.
    float best = -std::numeric_limits<float>::infinity();
    static constexpr logotron::Direction kCandidates[4] = {
        logotron::Direction::NORTH, logotron::Direction::EAST,
        logotron::Direction::SOUTH, logotron::Direction::WEST
    };
    for (auto d : kCandidates) {
        if (!logotron::is_legal_turn(pw.self.direction, d)) continue;
        float s = 0.0f;
        for (const auto& w : p.drive) {
            s += w.weight * w.tactic->score(d, pw);
        }
        if (s > best) {
            best = s;
            out.drive = d;
        }
    }

    // LOOK — weighted circular mean of desired_yaw outputs. Convert
    // each to a unit vector, weight-sum, atan2 back. This is the
    // right way to average angles; straight arithmetic mean has
    // wrap-around artifacts near ±π.
    float sum_x = 0.0f, sum_y = 0.0f;
    for (const auto& w : p.look) {
        float y = w.look->desired_yaw(pw);
        // Engine convention: yaw=0 points +Y. Vector on the unit
        // circle: (sin(yaw), cos(yaw)).
        sum_x += w.weight * std::sin(y);
        sum_y += w.weight * std::cos(y);
    }
    if (sum_x == 0.0f && sum_y == 0.0f) {
        out.head_target_yaw = yaw_of(pw.self.direction);
    } else {
        out.head_target_yaw = std::atan2(sum_x, sum_y);
    }
    return out;
}

} // namespace logotron::ai
