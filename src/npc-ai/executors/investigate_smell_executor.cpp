// InvestigateSmellExecutor Implementation

#include "investigate_smell_executor.h"
#include <iostream>

namespace npc_ai {

void InvestigateSmellExecutor::on_start(ExecutionContext& ctx) {
    // Target should be set by brain to a point in the smelled direction
    if (!ctx.has_target) {
        std::cerr << "[INVESTIGATE_SMELL] ERROR: no target set" << std::endl;
        return;
    }

    if (!ctx.pathfinder || !ctx.nav_grid || !ctx.current_path) {
        std::cerr << "[INVESTIGATE_SMELL] ERROR: pathfinding not available" << std::endl;
        return;
    }

    // Find path from current position to investigation point
    pathfinding::Point start(ctx.pos_x, ctx.pos_y);
    pathfinding::Point goal(ctx.target_x, ctx.target_y);

    *ctx.current_path = ctx.pathfinder->find_path(*ctx.nav_grid, start, goal);

    std::cout << "[INVESTIGATE_SMELL] Started toward (" << ctx.target_x
              << ", " << ctx.target_y << ") from (" << ctx.pos_x << "," << ctx.pos_y
              << ") path_valid=" << ctx.current_path->valid
              << " waypoints=" << ctx.current_path->waypoints.size() << std::endl;
    // Debug: print all waypoints
    std::cout << "[INVESTIGATE_SMELL] Path: ";
    for (size_t i = 0; i < ctx.current_path->waypoints.size(); i++) {
        std::cout << "(" << ctx.current_path->waypoints[i].x << ","
                  << ctx.current_path->waypoints[i].y << ") ";
    }
    std::cout << std::endl;
}

ExecutionResult InvestigateSmellExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Debug: log every ~60 frames
    static int frame = 0;
    if (frame++ % 60 == 0) {
        std::cout << "[INVESTIGATE_SMELL] pos=(" << ctx.pos_x << ", " << ctx.pos_y
                  << ") target=(" << ctx.target_x << ", " << ctx.target_y << ")"
                  << std::endl;
    }

    // No target = fail
    if (!ctx.has_target) {
        std::cerr << "[INVESTIGATE_SMELL] FAILED: no target!" << std::endl;
        return ExecutionResult::failed(intent);
    }

    // Check if we've arrived at investigation point
    float dist_to_target = distance(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
    if (dist_to_target <= ctx.arrival_distance) {
        // Arrived at investigation point
        // Brain will check if food was spotted during the walk
        std::cout << "[INVESTIGATE_SMELL] Arrived at investigation point" << std::endl;
        intent.is_moving = false;
        intent.forward_velocity = 0;
        return ExecutionResult::succeeded(intent);
    }

    // Follow path or walk direct
    if (!ctx.current_path || ctx.current_path->empty()) {
        // Direct approach
        float angle = angle_to(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
        intent.body_facing = angle;
        intent.wants_body_turn = true;
        intent.forward_velocity = ctx.walk_speed;  // Walk, don't run
        intent.is_moving = true;

        // Look forward while investigating
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

        if (ctx.current_path->empty()) {
            // Check if we're actually at the investigation point
            float final_dist = distance(ctx.pos_x, ctx.pos_y, ctx.target_x, ctx.target_y);
            if (final_dist <= ctx.arrival_distance) {
                // Arrived at investigation point
                intent.is_moving = false;
                intent.forward_velocity = 0;
                return ExecutionResult::succeeded(intent);
            } else {
                // Path ended but not at target - use direct approach
                waypoint = pathfinding::Point(ctx.target_x, ctx.target_y);
            }
        } else {
            waypoint = ctx.current_path->current();
        }
    }

    // Calculate desired facing toward waypoint
    float desired_facing = angle_to(ctx.pos_x, ctx.pos_y, waypoint.x, waypoint.y);

    // Set movement intent - walking speed for investigation
    intent.body_facing = desired_facing;
    intent.wants_body_turn = true;
    intent.forward_velocity = ctx.walk_speed;
    intent.is_moving = true;

    // Look ahead in movement direction
    intent.look_x = ctx.target_x;
    intent.look_y = ctx.target_y;
    intent.wants_head_look = true;

    return ExecutionResult::in_progress(intent);
}

} // namespace npc_ai
