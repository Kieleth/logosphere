// FleeOwnRecentTrailTactic — prefer directions that move AWAY from
// the rider's own active run-start point. The rider *knows* where
// its own trail is (it laid it); reading self.run_start is not a
// cheat — it's proprioception. Keeps the AI from boxing itself in.

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class FleeOwnRecentTrailTactic : public Tactic {
public:
    const char* name() const override { return "FleeOwnRecentTrail"; }
    float score(logotron::Direction candidate,
                const PerceivedWorld& pw) const override;
};

} // namespace logotron::ai
