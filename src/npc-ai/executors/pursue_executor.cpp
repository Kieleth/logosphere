// Pursue Executor Implementation

#include "pursue_executor.h"
#include <iostream>

namespace npc_ai {

void PursueExecutor::on_start(ExecutionContext& ctx) {
    // Compute path to target when action starts
    if (!ctx.has_target) {
        std::cerr << "[PURSUE] ERROR: no target set" << std::endl;
        return;
    }

    if (!ctx.pathfinder || !ctx.nav_grid || !ctx.current_path) {
        std::cerr << "[PURSUE] ERROR: pathfinding not available" << std::endl;
        return;
    }

    // Find path from current position to target
    pathfinding::Point start(ctx.pos_x, ctx.pos_y);
    pathfinding::Point goal(ctx.target_x, ctx.target_y);

    *ctx.current_path = ctx.pathfinder->find_path(*ctx.nav_grid, start, goal);

    // Remember what target we computed path for
    last_target_x_ = ctx.target_x;
    last_target_y_ = ctx.target_y;

    if (!ctx.current_path->valid || ctx.current_path->empty()) {
        std::cerr << "[PURSUE] WARNING: no path found to target ("
                  << ctx.target_x << ", " << ctx.target_y << ")" << std::endl;
    }
}

ExecutionResult PursueExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Debug: log every ~60 frames
    static int frame = 0;
    if (frame++ % 60 == 0) {
        std::cout << "[PURSUE] has_target=" << ctx.has_target
                  << " target=(" << ctx.target_x << ", " << ctx.target_y << ")"
                  << " pos=(" << ctx.pos_x << ", " << ctx.pos_y << ")"
                  << std::endl;
    }

    // No target = fail
    if (!ctx.has_target) {
        std::cerr << "[PURSUE] FAILED: no target!" << std::endl;
        return ExecutionResult::failed(intent);
    }

    // Dynamic path update: recompute if target moved significantly
    float target_moved = distance(last_target_x_, last_target_y_, ctx.target_x, ctx.target_y);
    if (target_moved > RECOMPUTE_THRESHOLD && ctx.pathfinder && ctx.nav_grid && ctx.current_path) {
        pathfinding::Point start(ctx.pos_x, ctx.pos_y);
        pathfinding::Point goal(ctx.target_x, ctx.target_y);
        *ctx.current_path = ctx.pathfinder->find_path(*ctx.nav_grid, start, goal);
        last_target_x_ = ctx.target_x;
        last_target_y_ = ctx.target_y;
    }

    // Check if we've arrived at final destination
    float dist_to_target = distance(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
    if (dist_to_target <= ctx.arrival_distance) {
        // Arrived! Stop moving.
        intent.is_moving = false;
        intent.forward_velocity = 0;
        return ExecutionResult::succeeded(intent);
    }

    // No path = try to walk directly (for nearby targets)
    if (!ctx.current_path || ctx.current_path->empty()) {
        // Direct approach
        float angle = angle_to(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
        intent.body_facing = angle;
        intent.wants_body_turn = true;
        intent.forward_velocity = ctx.run_speed;
        intent.is_moving = true;

        // Look at target
        intent.look_x = ctx.target_x;
        intent.look_y = ctx.target_y;
        intent.wants_head_look = true;

        return ExecutionResult::in_progress(intent);
    }

    // Follow path
    pathfinding::Point waypoint = ctx.current_path->current();
    float dist_to_waypoint = distance(ctx.pos_x, ctx.pos_y, waypoint.x, waypoint.y);

    // Reached waypoint? Advance to next.
    if (dist_to_waypoint < 0.5f) {
        ctx.current_path->advance();

        // If path now empty, check if we're actually at the goal
        if (ctx.current_path->empty()) {
            // Recalculate distance to actual target
            float final_dist = distance(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
            if (final_dist <= ctx.arrival_distance) {
                // Actually arrived at target
                intent.is_moving = false;
                intent.forward_velocity = 0;
                return ExecutionResult::succeeded(intent);
            } else {
                // Path ended but not at target - use direct approach
                std::cout << "[PURSUE] Path ended but target still " << final_dist
                          << "m away, walking direct" << std::endl;
                // Use target as waypoint for direct movement
                waypoint = pathfinding::Point(ctx.target_x, ctx.target_y);
            }
        } else {
            waypoint = ctx.current_path->current();
        }
    }

    // Calculate desired facing toward waypoint
    float desired_facing = angle_to(ctx.pos_x, ctx.pos_y, waypoint.x, waypoint.y);

    // Debug: every 60 frames
    static int path_debug = 0;
    if (path_debug++ % 60 == 0) {
        std::cout << "[PURSUE_PATH] pos=(" << ctx.pos_x << "," << ctx.pos_y
                  << ") waypoint=(" << waypoint.x << "," << waypoint.y
                  << ") target=(" << ctx.target_x << "," << ctx.target_y
                  << ") desired_facing=" << desired_facing << std::endl;
    }

    // Set movement intent
    intent.body_facing = desired_facing;
    intent.wants_body_turn = true;
    intent.forward_velocity = ctx.run_speed;
    intent.is_moving = true;

    // Look at final target (not waypoint) for natural behavior
    intent.look_x = ctx.target_x;
    intent.look_y = ctx.target_y;
    intent.wants_head_look = true;

    return ExecutionResult::in_progress(intent);
}

} // namespace npc_ai
