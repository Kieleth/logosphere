// MaintainDirectionTactic — tiny positive bias for the current
// direction. Its job is to break near-ties so the AI doesn't jitter
// back and forth every decision tick when all other tactics are
// indifferent.

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class MaintainDirectionTactic : public Tactic {
public:
    const char* name() const override { return "MaintainDirection"; }
    float score(logotron::Direction candidate,
                const PerceivedWorld& pw) const override;
};

} // namespace logotron::ai
