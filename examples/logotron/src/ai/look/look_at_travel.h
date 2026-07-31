// LookAtTravelTactic — keep the head aimed where the bike is going.
// The safe default; suitable for DEFENSIVE personalities where you
// want forward awareness rather than target fixation.

#pragma once

#include "ai/tactic.h"

namespace logotron::ai {

class LookAtTravelTactic : public LookTactic {
public:
    const char* name() const override { return "LookAtTravel"; }
    float desired_yaw(const PerceivedWorld& pw) const override {
        return yaw_of(pw.self.direction);
    }
};

} // namespace logotron::ai
