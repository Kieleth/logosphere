// AvoidImminentCrashTactic — the single most important tactic.
// Hard-avoid directions that terminate in a wall or trail within
// a few meters. Without this the AI drives into walls for fun.

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class AvoidImminentCrashTactic : public Tactic {
public:
    const char* name() const override { return "AvoidImminentCrash"; }
    // score = -1 when a lethal hit is 0 m away, linearly rising to 0
    // at `horizon` meters, and 0 for anything farther than horizon.
    // Means a clear 10 m runway scores 0 (neutral) while a 1 m wall
    // scores ~-0.9 (hard avoid).
    float score(logotron::Direction candidate,
                const PerceivedWorld& pw) const override;

private:
    static constexpr float kHorizon = 10.0f;
};

} // namespace logotron::ai
