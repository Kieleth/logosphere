// Tactic + LookTactic — abstract scoring interfaces for the driver.
//
// Each DRIVE tactic scores each cardinal direction. Each LOOK tactic
// proposes a head target yaw (absolute world-space angle). The
// personality blends tactics by weight and argmax wins.
//
// Hard rule (GAME_DESIGN.md §16): tactic files must NOT #include
// kg_module.h. They see only PerceivedWorld + HeadState.

#pragma once

#include "ai/perceived_world.h"
#include "cycle.h"

namespace logotron::ai {

// DRIVE TACTICS — score each candidate direction in [-1, +1].
// Higher = more desirable. Negative = actively avoid.
class Tactic {
public:
    virtual ~Tactic() = default;
    virtual const char* name() const = 0;
    virtual float score(logotron::Direction candidate,
                        const PerceivedWorld& pw) const = 0;
};

// LOOK TACTICS — propose where the rider should rotate its head.
// Return value is an absolute world-space yaw (engine CW convention,
// yaw=0 points +Y/north). decide() picks the weighted-blended
// target; the HeadState stepper rate-limits the actual motion.
class LookTactic {
public:
    virtual ~LookTactic() = default;
    virtual const char* name() const = 0;
    virtual float desired_yaw(const PerceivedWorld& pw) const = 0;
};

// Convert cycle Direction → engine yaw. Used by both tactic kinds.
inline float yaw_of(logotron::Direction d) {
    constexpr float kPi = 3.14159265358979323846f;
    switch (d) {
        case logotron::Direction::NORTH: return 0.0f;
        case logotron::Direction::EAST:  return  kPi * 0.5f;
        case logotron::Direction::SOUTH: return  kPi;
        case logotron::Direction::WEST:  return -kPi * 0.5f;
    }
    return 0.0f;
}

} // namespace logotron::ai
