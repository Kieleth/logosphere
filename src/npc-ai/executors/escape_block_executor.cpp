// EscapeBlockExecutor Implementation
//
// Strategy: When blocked, sidestep perpendicular to goal direction.
// Turn 90° right (or left if that's blocked), walk sideways, then
// resume pursuit. Success = moved 1m+ perpendicular to goal direction.

#include "escape_block_executor.h"
#include <iostream>

namespace npc_ai {

void EscapeBlockExecutor::on_start(ExecutionContext& ctx) {
    // Record starting state
    start_rotation_ = ctx.rotation;
    total_rotation_ = 0;
    accumulated_movement_ = 0;
    rotating_ = true;
    last_pos_x_ = ctx.pos_x;
    last_pos_y_ = ctx.pos_y;
    start_pos_x_ = ctx.pos_x;
    start_pos_y_ = ctx.pos_y;

    // Calculate goal direction
    if (ctx.has_target) {
        goal_angle_ = angle_to(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
    } else {
        // No target? Just rotate right as default
        goal_angle_ = ctx.rotation + M_PI / 2;
    }

    // Turn PERPENDICULAR to goal (90° right) to sidestep around obstacle
    // This is the key fix: we don't walk toward the goal, we walk sideways
    escape_angle_ = normalize_angle(goal_angle_ + M_PI / 2);  // 90° right
    turn_direction_ = 1;  // Default: turn right to face escape direction

    std::cout << "[ESCAPE_BLOCK] Started: pos=(" << ctx.pos_x << "," << ctx.pos_y << ")"
              << " goal_angle=" << goal_angle_
              << " escape_angle=" << escape_angle_
              << std::endl;
}

ExecutionResult EscapeBlockExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Update position tracking
    last_pos_x_ = ctx.pos_x;
    last_pos_y_ = ctx.pos_y;

    // Calculate perpendicular distance from start (how far we've sidestepped)
    // Project displacement onto escape direction
    float dx = ctx.pos_x - start_pos_x_;
    float dy = ctx.pos_y - start_pos_y_;
    float escape_dx = std::sin(escape_angle_);
    float escape_dy = std::cos(escape_angle_);
    float sidestep_distance = dx * escape_dx + dy * escape_dy;

    // Success: sidestepped far enough to try going around
    if (sidestep_distance > SIDESTEP_DISTANCE) {
        std::cout << "[ESCAPE_BLOCK] Sidestepped " << sidestep_distance << "m - SUCCESS" << std::endl;
        intent.is_moving = false;
        return ExecutionResult::succeeded(intent);
    }

    // Timeout: been trying too long without success
    total_rotation_ += ctx.dt;  // Reuse as time counter
    if (total_rotation_ > 5.0f) {  // 5 second timeout
        std::cout << "[ESCAPE_BLOCK] Timeout after " << total_rotation_ << "s - FAILED" << std::endl;
        intent.is_moving = false;
        return ExecutionResult::failed(intent);
    }

    // Turn toward escape angle and walk
    intent.body_facing = escape_angle_;
    intent.wants_body_turn = true;
    intent.forward_velocity = ctx.walk_speed;
    intent.is_moving = true;

    // Look in the direction we're moving
    float look_dist = 3.0f;
    intent.look_x = ctx.pos_x + escape_dx * look_dist;
    intent.look_y = ctx.pos_y + escape_dy * look_dist;
    intent.wants_head_look = true;

    // Debug logging
    static int frame = 0;
    if (frame++ % 60 == 0) {
        std::cout << "[ESCAPE_BLOCK] sidestepping: dist=" << sidestep_distance
                  << " target=" << SIDESTEP_DISTANCE << "m" << std::endl;
    }

    return ExecutionResult::in_progress(intent);
}

} // namespace npc_ai
