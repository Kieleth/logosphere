// decide — given a personality and a perceived world, output the
// drive direction + head target yaw.
//
// Drive: for each LEGAL candidate direction (excludes 180° reversal
// per cycle rules), sum weighted tactic scores, argmax. Ties break
// in favour of current direction (via MaintainDirection bias).
//
// Look: weighted circular mean of LookTactic outputs. Circular mean
// is correct because yaw is an angle — arithmetic mean wraps wrong.

#pragma once

#include "ai/perceived_world.h"
#include "ai/personality.h"
#include "cycle.h"

namespace logotron::ai {

struct Decision {
    logotron::Direction drive;
    float head_target_yaw;
};

Decision decide(const Personality& p, const PerceivedWorld& pw);

} // namespace logotron::ai
