#include "ai/tactics/avoid_seen_opponent.h"

namespace logotron::ai {

float AvoidSeenOpponentTactic::score(logotron::Direction candidate,
                                     const PerceivedWorld& pw) const {
    if (!pw.opponent_rider.has_value()) return 0.0f;

    const auto& op = *pw.opponent_rider;
    float dx = op.x - pw.self.x;
    float dy = op.y - pw.self.y;

    // Flip the chase scoring: directions that OPEN distance score +1,
    // close scores -0.3. Same shape otherwise.
    switch (candidate) {
        case logotron::Direction::NORTH: return dy < 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::SOUTH: return dy > 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::EAST:  return dx < 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::WEST:  return dx > 0.0f ? +1.0f : -0.3f;
    }
    return 0.0f;
}

} // namespace logotron::ai
