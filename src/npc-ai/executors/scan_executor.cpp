// Scan Executor Implementation

#include "scan_executor.h"
#include <iostream>

namespace npc_ai {

void ScanExecutor::on_start(ExecutionContext& ctx) {
    // Initialize scan state
    ctx.scan_angle = 0;
    ctx.scan_direction = 1;

    // Set action timer from duration
    if (ctx.action_timer) {
        *ctx.action_timer = ctx.action_duration;
    }
}

ExecutionResult ScanExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Body stays still during scan
    intent.is_moving = false;
    intent.forward_velocity = 0;
    intent.wants_body_turn = false;

    // Check if timer expired
    if (ctx.action_timer && *ctx.action_timer <= 0) {
        // Scan complete, no target found
        return ExecutionResult::succeeded(intent);
    }

    // Update scan angle (sweep left/right)
    // Note: scan_angle and scan_direction need to be non-const for update
    // This is a limitation - in practice, the creature brain tracks these
    float current_scan_angle = ctx.scan_angle;
    int current_direction = ctx.scan_direction;

    current_scan_angle += SCAN_SPEED * ctx.dt * current_direction;

    // Reverse at limits
    if (std::abs(current_scan_angle) > MAX_SCAN_ANGLE) {
        current_direction *= -1;
    }

    // Calculate look position
    float look_angle = ctx.rotation + current_scan_angle;
    intent.look_x = ctx.pos_x - std::sin(look_angle) * LOOK_DISTANCE;
    intent.look_y = ctx.pos_y + std::cos(look_angle) * LOOK_DISTANCE;
    intent.wants_head_look = true;

    // Call perception callback if available
    if (ctx.perception) {
        PerceptionRequest request;
        request.pos_x = ctx.pos_x;
        request.pos_y = ctx.pos_y;
        request.pos_z = 1.5f;  // Eye height
        request.look_angle = look_angle;

        PerceptionResult result = ctx.perception(request);

        if (result.target_found) {
            // Target detected - creature brain will handle this
            // We just report the movement and let the callback trigger world state updates
            // The creature brain's perception callback should update world_state
            // and trigger replan when target is found
        }
    }

    return ExecutionResult::in_progress(intent);
}

} // namespace npc_ai
