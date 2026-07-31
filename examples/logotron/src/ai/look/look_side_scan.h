// LookSideScanTactic — sweep the head left and right in a slow sine
// wave around the travel direction. Gives CHAOTIC personalities a
// restless, searching vibe, and also genuinely covers blind spots
// over time. Period is state held externally (passed in), kept out
// of the tactic so the tactic stays a pure function.

#pragma once

#include <cmath>

#include "ai/tactic.h"

namespace logotron::ai {

class LookSideScanTactic : public LookTactic {
public:
    // `time_seconds` and `period` + `amplitude_rad` are personality-
    // tunable inputs the caller provides. Keeps the tactic a pure
    // function; decide() feeds it the current wall clock.
    LookSideScanTactic(float time_seconds, float period_s, float amplitude_rad)
        : time_(time_seconds), period_(period_s), amp_(amplitude_rad) {}

    const char* name() const override { return "LookSideScan"; }
    float desired_yaw(const PerceivedWorld& pw) const override {
        constexpr float kPi = 3.14159265358979323846f;
        float phase = (period_ > 0.0f)
            ? (2.0f * kPi * time_ / period_)
            : 0.0f;
        return yaw_of(pw.self.direction) + amp_ * std::sin(phase);
    }

private:
    float time_;
    float period_;
    float amp_;
};

} // namespace logotron::ai
