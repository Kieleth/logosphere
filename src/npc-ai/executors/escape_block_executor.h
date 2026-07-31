// EscapeBlockExecutor - Instinctual wall escape behavior
//
// When creature is blocked (proprioception: pushing but not moving),
// this executor rotates toward the goal direction and walks forward.
//
// Logic:
// 1. Goal direction known (target_x, target_y from brain)
// 2. Calculate which way to turn to face goal
// 3. Rotate incrementally (15-20° per check)
// 4. Walk forward while rotating
// 5. Succeed when rotation complete OR we start moving
// 6. If still blocked after 180° sweep, action fails
//
// The brain's proprioception detects if we're actually moving.
// If NOT_MOVING persists, frustration builds until GIVE_UP.
//
// Detection (vision OR collision):
// - Vision check: raycast forward, no hit = clear
// - Proprioception: position changed = moving
// Both are checked. Either clearing the path succeeds.

#ifndef ESCAPE_BLOCK_EXECUTOR_H
#define ESCAPE_BLOCK_EXECUTOR_H

#include "../action_executor.h"
#include <cmath>

namespace npc_ai {

class EscapeBlockExecutor : public ActionExecutor {
public:
    EscapeBlockExecutor() = default;

    const char* name() const override { return "ESCAPE_BLOCK"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

private:
    // State tracking (per-action instance)
    float start_rotation_ = 0;       // Rotation when action started
    float total_rotation_ = 0;       // How much we've rotated so far
    int turn_direction_ = 1;         // 1 = right (clockwise), -1 = left
    float goal_angle_ = 0;           // Direction to goal
    float escape_angle_ = 0;         // Direction to sidestep (perpendicular to goal)
    bool rotating_ = true;           // Still in rotation phase?

    // Config
    static constexpr float ROTATION_STEP = 0.3f;      // ~17 degrees per step
    static constexpr float MAX_ROTATION = 3.14159f;   // 180 degrees max sweep
    static constexpr float SIDESTEP_DISTANCE = 1.5f;  // Sidestep 1.5m before trying forward again

    // Position tracking for movement check
    float last_pos_x_ = 0;
    float last_pos_y_ = 0;
    float start_pos_x_ = 0;          // Position when action started
    float start_pos_y_ = 0;
    float accumulated_movement_ = 0;  // Cumulative distance moved

    static float distance(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return std::sqrt(dx * dx + dy * dy);
    }

    static float angle_to(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return std::atan2(dx, dy);  // 0 = +Y (north)
    }

    static float normalize_angle(float a) {
        while (a > M_PI) a -= 2 * M_PI;
        while (a < -M_PI) a += 2 * M_PI;
        return a;
    }

    static float angle_diff(float from, float to) {
        return normalize_angle(to - from);
    }
};

} // namespace npc_ai

#endif // ESCAPE_BLOCK_EXECUTOR_H
