// Pursue Executor - Navigate to target location
//
// Generic "go to target" behavior for GOAP PURSUE actions.
// Uses A* pathfinding to navigate, running at ctx.run_speed.
// Completes when creature arrives within ctx.arrival_distance.
//
// The target position comes from ctx.target_x/target_y (set by brain).
// Path is computed on_start and followed until arrival.

#ifndef PURSUE_EXECUTOR_H
#define PURSUE_EXECUTOR_H

#include "../action_executor.h"
#include <cmath>

namespace npc_ai {

class PursueExecutor : public ActionExecutor {
public:
    PursueExecutor() = default;

    const char* name() const override { return "PURSUE"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

private:
    // Helper: calculate distance between two points
    static float distance(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Helper: calculate angle from (x1,y1) to (x2,y2)
    // Returns radians, 0 = +Y (north)
    static float angle_to(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        // atan2 returns angle from +X axis, we want from +Y
        // +Y (north) = 0, +X (east) = pi/2
        return std::atan2(dx, dy);
    }

    // Helper: normalize angle to [-pi, pi]
    static float normalize_angle(float a) {
        while (a > M_PI) a -= 2 * M_PI;
        while (a < -M_PI) a += 2 * M_PI;
        return a;
    }

    // Track last computed target for dynamic path updates
    float last_target_x_ = 0;
    float last_target_y_ = 0;
    static constexpr float RECOMPUTE_THRESHOLD = 3.0f;  // Recompute if target moved >3m
};

} // namespace npc_ai

#endif // PURSUE_EXECUTOR_H
