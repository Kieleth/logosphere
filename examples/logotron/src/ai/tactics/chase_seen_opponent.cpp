#include "ai/tactics/chase_seen_opponent.h"

namespace logotron::ai {

float ChaseSeenOpponentTactic::score(logotron::Direction candidate,
                                     const PerceivedWorld& pw) const {
    if (!pw.opponent_rider.has_value()) return 0.0f;  // blind → no cheat

    const auto& op = *pw.opponent_rider;
    float dx = op.x - pw.self.x;
    float dy = op.y - pw.self.y;

    // Cardinal helper: does moving 1 unit in this dir reduce
    // Manhattan distance to the opponent?
    switch (candidate) {
        case logotron::Direction::NORTH: return dy > 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::SOUTH: return dy < 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::EAST:  return dx > 0.0f ? +1.0f : -0.3f;
        case logotron::Direction::WEST:  return dx < 0.0f ? +1.0f : -0.3f;
    }
    return 0.0f;
}

} // namespace logotron::ai
