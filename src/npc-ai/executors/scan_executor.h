// Scan Executor - Look around searching for targets
//
// Generic "look around" behavior for GOAP SCAN actions.
// Sweeps head left/right while checking for targets via perception callback.
// Completes when duration expires OR target found.
//
// Uses ctx.perception callback to query creature's senses.
// Uses ctx.action_duration for scan duration.
// Uses ctx.scan_angle and ctx.scan_direction for sweep state.
//
// If target found via perception, the creature brain handles the discovery
// (updating world state, triggering replan, etc.)

#ifndef SCAN_EXECUTOR_H
#define SCAN_EXECUTOR_H

#include "../action_executor.h"
#include <cmath>

namespace npc_ai {

class ScanExecutor : public ActionExecutor {
public:
    ScanExecutor() = default;

    const char* name() const override { return "SCAN"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

private:
    // Scan parameters (configurable per creature via context)
    static constexpr float SCAN_SPEED = 1.0f;      // rad/s
    static constexpr float MAX_SCAN_ANGLE = 1.0f;  // ~60 degrees from center
    static constexpr float LOOK_DISTANCE = 10.0f;  // How far ahead to look
};

} // namespace npc_ai

#endif // SCAN_EXECUTOR_H
