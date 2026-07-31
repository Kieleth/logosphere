// GOAP Plan Executor Implementation

#include "goap_plan_executor.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace npc_ai {

static void log_goap(bool verbose, const char* tag, const std::string& msg) {
    if (verbose) {
        std::cout << "[GOAP][" << tag << "] " << msg << std::endl;
    }
}

ExecutionContext GOAPPlanExecutor::build_context(
    const std::string& action_name,
    goap::WorldState& world_state,
    float x, float y, float rotation, float dt,
    const CreatureParams& params)
{
    ExecutionContext ctx;

    // Current position/state
    ctx.pos_x = x;
    ctx.pos_y = y;
    ctx.rotation = rotation;
    ctx.dt = dt;

    // World state access
    ctx.world_state = &world_state;
    ctx.world_state_mutable = &world_state;

    // Pathfinding access
    ctx.pathfinder = pathfinder_;
    ctx.nav_grid = nav_grid_;
    ctx.current_path = current_path_;

    // Target position - read from params based on action type
    // (Targets are set by brain's perception layer, not external set_target() calls)
    // NOTE: Use action_name parameter, NOT state_.current_action_name, because
    // state may not be updated yet on the first frame of a new action
    if (action_name == "PURSUE" || action_name == "EAT") {
        ctx.target_x = params.food_x;
        ctx.target_y = params.food_y;
        ctx.has_target = true;
    } else if (action_name == "INVESTIGATE_SMELL") {
        ctx.target_x = params.smell_target_x;
        ctx.target_y = params.smell_target_y;
        ctx.has_target = true;
    } else if (action_name == "ESCAPE_BLOCK") {
        // ESCAPE_BLOCK uses the last known target (where we were trying to go)
        ctx.target_x = params.food_x != 0 ? params.food_x : params.smell_target_x;
        ctx.target_y = params.food_y != 0 ? params.food_y : params.smell_target_y;
        ctx.has_target = true;
    } else {
        // Fallback to legacy set_target mechanism (for backwards compatibility)
        ctx.target_x = target_x_;
        ctx.target_y = target_y_;
        ctx.has_target = has_target_;
    }

    // Creature parameters
    ctx.walk_speed = params.walk_speed;
    ctx.run_speed = params.run_speed;
    ctx.turn_speed = params.turn_speed;
    ctx.arrival_distance = params.arrival_distance;
    ctx.action_duration = params.eat_duration;

    // Timer for timed actions
    ctx.action_timer = &state_.action_timer;

    // Eating parameters (physics-based consumption)
    ctx.mouth_volume_cm3 = params.mouth_volume_cm3;
    ctx.food_state = params.food_state;

    return ctx;
}

PlanExecutionResult GOAPPlanExecutor::update(
    goap::Plan& plan,
    goap::WorldState& world_state,
    float x, float y, float rotation, float dt,
    const CreatureParams& params)
{
    PlanExecutionResult result;

    // No registry = can't execute
    if (!registry_) {
        log_goap(verbose_, "ERROR", "No registry connected");
        result.plan_completed = true;
        return result;
    }

    // Empty plan = done
    if (plan.empty()) {
        result.plan_completed = true;
        return result;
    }

    // Get current action
    const goap::Action* action = plan.next_action();
    if (!action) {
        result.plan_completed = true;
        return result;
    }

    // Build execution context
    // Pass action->name explicitly so context has correct targets on first frame
    ExecutionContext ctx = build_context(action->name, world_state, x, y, rotation, dt, params);

    // Check if action changed (new action starting)
    if (action->name != state_.current_action_name) {
        // End previous action if any
        if (state_.action_started && !state_.current_action_name.empty()) {
            registry_->end_action(state_.current_action_name, ctx, false);
        }

        // Start new action
        state_.current_action_name = action->name;
        state_.action_started = true;
        state_.action_timer = 0.0f;
        registry_->start_action(action->name, ctx);

        std::ostringstream oss;
        oss << "Starting action: " << action->name;
        log_goap(verbose_, "ACTION", oss.str());
    }

    // Execute current action
    ExecutionResult exec_result = registry_->execute(action->name, ctx);
    result.movement = exec_result.movement;

    // Update timer (for timed actions like EAT)
    if (state_.action_timer > 0) {
        state_.action_timer -= dt;
    }

    // Check completion
    if (exec_result.completed) {
        // End the action
        registry_->end_action(action->name, ctx, exec_result.success);

        std::ostringstream oss;
        oss << "Action " << action->name << " "
            << (exec_result.success ? "COMPLETED" : "FAILED");
        log_goap(verbose_, "ACTION", oss.str());

        if (exec_result.success) {
            // Apply effects to world state
            goap::apply_effects(world_state, action->effects);
            log_goap(verbose_, "EFFECT", "Applied action effects to world state");
        }

        // Pop completed action
        plan.pop_action();

        // Reset state for next action
        state_.current_action_name.clear();
        state_.action_started = false;
        state_.action_timer = 0.0f;

        result.action_completed = true;
        result.action_failed = !exec_result.success;

        // Check if plan now empty
        if (plan.empty()) {
            result.plan_completed = true;

            // Check goal satisfaction (generic GOAP logic)
            if (goal_) {
                if (goal_->is_satisfied(world_state)) {
                    log_goap(verbose_, "GOAL", "Goal ACHIEVED!");
                    result.goal_achieved = true;
                } else {
                    log_goap(verbose_, "GOAL", "Plan completed but goal NOT satisfied - needs replan");
                    result.needs_replan = true;
                }
            } else {
                // No goal set - just report completion
                log_goap(verbose_, "PLAN", "Plan complete - all actions executed");
            }
        } else {
            std::ostringstream oss2;
            oss2 << "Actions remaining: " << plan.size();
            log_goap(verbose_, "PLAN", oss2.str());
        }
    }

    return result;
}

void GOAPPlanExecutor::reset() {
    // End current action if any
    if (state_.action_started && registry_ && !state_.current_action_name.empty()) {
        ExecutionContext ctx;
        ctx.action_timer = &state_.action_timer;
        registry_->end_action(state_.current_action_name, ctx, false);
    }

    state_ = ActionExecutionState{};
    has_target_ = false;
}

} // namespace npc_ai
