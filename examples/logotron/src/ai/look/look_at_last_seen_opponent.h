// LookAtLastSeenOpponentTactic — if the opponent is currently in
// sight, keep the head aimed at them. If not visible, fall back to
// looking at travel direction. With head-rotation rate-limited, the
// rider genuinely spends time acquiring the target; no instant
// snap-to-opponent.

#pragma once

#include <cmath>

#include "ai/tactic.h"

namespace logotron::ai {

class LookAtLastSeenOpponentTactic : public LookTactic {
public:
    const char* name() const override { return "LookAtLastSeenOpponent"; }
    float desired_yaw(const PerceivedWorld& pw) const override {
        if (pw.opponent_rider.has_value()) {
            const auto& op = *pw.opponent_rider;
            // atan2(dx, dy) — engine convention (+Y forward, +X east).
            return std::atan2(op.x - pw.self.x, op.y - pw.self.y);
        }
        return yaw_of(pw.self.direction);
    }
};

} // namespace logotron::ai
