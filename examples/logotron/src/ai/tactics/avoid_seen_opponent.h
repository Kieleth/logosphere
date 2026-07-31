// AvoidSeenOpponentTactic — inverse of ChaseSeenOpponent. Only
// fires when the opponent is currently visible in the rider's
// cone; contributes 0 to all dirs otherwise.

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class AvoidSeenOpponentTactic : public Tactic {
public:
    const char* name() const override { return "AvoidSeenOpponent"; }
    float score(logotron::Direction candidate,
                const PerceivedWorld& pw) const override;
};

} // namespace logotron::ai
