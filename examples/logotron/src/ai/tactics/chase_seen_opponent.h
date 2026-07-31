// ChaseSeenOpponentTactic — only fires when the opponent is
// currently inside the rider's cone AND unoccluded. If the rider
// can't see the opponent, this tactic contributes 0 to all
// directions (no cheating with KG omniscience).

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class ChaseSeenOpponentTactic : public Tactic {
public:
    const char* name() const override { return "ChaseSeenOpponent"; }
    // score = +1 for directions that close Manhattan distance, -0.3
    // for directions that open it, 0 when unknown.
    float score(logotron::Direction candidate,
                const PerceivedWorld& pw) const override;
};

} // namespace logotron::ai
