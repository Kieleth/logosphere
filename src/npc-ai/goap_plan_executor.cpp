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
    const goap::Action& action,
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

    // Target routing is the ACTION's declaration, not a name ladder.
    // The old code matched action names against a hardcoded list
    // (PURSUE, EAT, INVESTIGATE_SMELL, ESCAPE_BLOCK), which put game
    // vocabulary in the engine, and used `food_x != 0` as "no target",
    // which mistook the world origin for absence and could weld x from
    // one location to y from another (#44). Now: the action names which
    // target it consumes; the brain publishes targets by name; absence
    // is the key being absent.
    if (!action.target_key.empty()) {
        auto it = params.targets.find(action.target_key);
        if (it != params.targets.end()) {
            ctx.target_x = it->second.first;
            ctx.target_y = it->second.second;
            ctx.has_target = true;
        } else {
            ctx.has_target = false;   // declared but not published: honest absence
        }
    } else {
        // Action declares no target: legacy direct-drive set_target()
        // still works for brains that steer by hand.
        ctx.target_x = target_x_;
        ctx.target_y = target_y_;
        ctx.has_target = has_target_;
    }

    // Creature parameters
    ctx.walk_speed = params.walk_speed;
    ctx.run_speed = params.run_speed;
    ctx.turn_speed = params.turn_speed;
    ctx.arrival_distance = params.arrival_distance;
    ctx.action_duration = params.action_duration;

    // Timer for timed actions
    ctx.action_timer = &state_.action_timer;

    // Opaque game context: copied, never read.
    ctx.game_data = params.game_data;

    return ctx;
}

PlanExecutionResult GOAPPlanExecutor::update(
    goap::Plan& plan,
    goap::WorldState& world_state,
    float x, float y, float rotation, float dt,
    const CreatureParams& params)
{
    PlanExecutionResult result;

    // No registry = can't execute. Reported as a FAILURE, not as a
    // completed plan: plan_completed here used to be the same signal a
    // finished plan gives, so a brain doing `if (plan_completed)
    // pick_new_goal()` concluded the creature achieved something while
    // it silently did nothing every frame (#45). The plan is left
    // intact, action_failed and needs_replan say why nothing ran.
    if (!registry_) {
        log_goap(verbose_, "ERROR", "No registry connected");
        result.action_failed = true;
        result.needs_replan = true;
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
    ExecutionContext ctx = build_context(*action, world_state, x, y, rotation, dt, params);

    // Check if action changed (new action starting)
    if (action->name != state_.current_action_name) {
        // End previous action if any
        if (state_.action_started && !state_.current_action_name.empty()) {
            registry_->end_action(state_.current_action_name, ctx, false);
            state_.current_action_name.clear();
            state_.action_started = false;
        }

        // An action only STARTS if the world satisfies its declared
        // preconditions. The planner chained these actions because each
        // one's effects enable the next; when an action fails, its
        // effects are (correctly) not applied, and without this gate the
        // next action ran anyway — the predator AT caught EAT running in
        // a place PURSUE never reached, eating food that was not there.
        // Unmet preconditions mean the plan is STALE, not that anything
        // failed: report needs_replan and leave the plan for the brain.
        // Checked at action START only; an in-flight action may
        // legitimately consume the very state that admitted it.
        if (!action->can_execute(world_state)) {
            std::ostringstream oss;
            oss << "Preconditions for " << action->name
                << " no longer hold - needs replan";
            log_goap(verbose_, "PLAN", oss.str());
            result.needs_replan = true;
            return result;
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
