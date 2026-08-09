// GOAP Plan Executor - Generic plan execution loop
//
// This is the "brain" that drives GOAP plan execution.
// Any NPC can use this by providing:
//   1. An ExecutorRegistry with registered action executors
//   2. Current GOAP plan (from Planner)
//   3. World state (for precondition checking and effect application)
//   4. Creature-specific parameters (speeds, distances)
//
// The executor handles the generic loop:
//   Execute action → check completion → pop action → apply effects → next
//
// ============================================================================
// ARCHITECTURE
// ============================================================================
//
//   Creature Brain (config-only)
//         │
//         │ 1. Register executors
//         │ 2. Create plan
//         │ 3. Call executor.update()
//         │
//         ▼
//   GOAPPlanExecutor (generic logic)
//         │
//         │ - Execute current action
//         │ - Track completion
//         │ - Pop completed actions
//         │ - Apply effects to world state
//         │ - Return MovementIntent
//         │
//         ▼
//   ExecutorRegistry → ActionExecutors (PURSUE, EAT, etc.)
//
// ============================================================================
// USAGE
// ============================================================================
//
//   // In creature initialize():
//   goap_executor_.set_registry(&executor_registry_);
//
//   // In creature update():
//   if (current_plan_.valid && !current_plan_.empty()) {
//       auto result = goap_executor_.update(
//           current_plan_, world_state_,
//           x, y, rotation, dt,
//           run_speed_, arrival_dist_);
//
//       if (result.plan_completed) {
//           // Plan done, check if goal satisfied
//       }
//       return result.movement;
//   }
//   // else: do organic wandering
//
// ============================================================================

#ifndef GOAP_PLAN_EXECUTOR_H
#define GOAP_PLAN_EXECUTOR_H

#include "action_executor.h"
#include "executor_registry.h"
#include "goap_system.h"
#include "pathfinding_system.h"
#include <unordered_map>
#include <utility>

namespace npc_ai {

// ============================================
// EXECUTION STATE
// Tracks state between frames for current action
// ============================================
struct ActionExecutionState {
    std::string current_action_name;
    bool action_started = false;
    float action_timer = 0.0f;
};

// ============================================
// EXECUTION RESULT
// What update() returns to the brain
// ============================================
struct PlanExecutionResult {
    MovementIntent movement;
    bool plan_completed = false;    // All actions done
    bool action_completed = false;  // Current action just finished
    bool action_failed = false;     // Current action failed
    bool goal_achieved = false;     // Goal satisfied after plan completed
    bool needs_replan = false;      // Plan failed, goal not achieved, should replan
};

// ============================================
// CREATURE PARAMETERS
// Passed by brain, used to build ExecutionContext
// ============================================
struct CreatureParams {
    float walk_speed = 0.8f;
    float run_speed = 1.5f;
    float turn_speed = 1.5f;
    float arrival_distance = 1.0f;
    float action_duration = 2.0f;  // Timer for timed actions (feeds ctx.action_duration)

    // Named target locations, published by the brain each frame and
    // consumed by whichever action declares that name in its
    // Action::target_key ("food", "smell", "threat", ... — the engine
    // has no opinion about what the names mean). Presence IS the key
    // being present: no sentinel, so a target sitting exactly on the
    // world origin is a target like any other (#44).
    std::unordered_map<std::string, std::pair<float, float>> targets;

    // Opaque game context, copied into ExecutionContext.game_data and
    // never read by the engine. Game-registered executors cast it back
    // to whatever their game defines (the predator example's FoodState
    // rides through here; the engine no longer knows a mouth exists).
    void* game_data = nullptr;
};

// ============================================
// GOAP PLAN EXECUTOR
// Generic plan execution - reusable by any NPC
// ============================================
class GOAPPlanExecutor {
public:
    GOAPPlanExecutor() = default;

    // Connect to executor registry (owned by creature brain)
    void set_registry(ExecutorRegistry* registry) { registry_ = registry; }

    // Connect to pathfinding (owned by creature brain)
    void set_pathfinder(pathfinding::Pathfinder* pf) { pathfinder_ = pf; }
    void set_nav_grid(pathfinding::NavGrid* grid) { nav_grid_ = grid; }
    void set_current_path(pathfinding::Path* path) { current_path_ = path; }

    // Set target position (for PURSUE, etc.)
    void set_target(float x, float y) {
        target_x_ = x;
        target_y_ = y;
        has_target_ = true;
    }
    void clear_target() { has_target_ = false; }

    // Set current goal (for goal satisfaction checking)
    void set_goal(const goap::Goal* goal) { goal_ = goal; }

    // Enable/disable verbose logging
    void set_verbose(bool v) { verbose_ = v; }

    // Main update - executes current action in plan
    // Returns movement intent and completion status
    PlanExecutionResult update(
        goap::Plan& plan,
        goap::WorldState& world_state,
        float x, float y, float rotation, float dt,
        const CreatureParams& params);

    // Reset execution state (call when plan changes)
    void reset();

    // Debug
    const ActionExecutionState& get_state() const { return state_; }

private:
    ExecutorRegistry* registry_ = nullptr;
    pathfinding::Pathfinder* pathfinder_ = nullptr;
    pathfinding::NavGrid* nav_grid_ = nullptr;
    pathfinding::Path* current_path_ = nullptr;
    const goap::Goal* goal_ = nullptr;

    ActionExecutionState state_;

    float target_x_ = 0, target_y_ = 0;
    bool has_target_ = false;
    bool verbose_ = true;

    // Build context for executor. Takes the ACTION rather than its name:
    // target routing follows the action's target_key declaration, and
    // state_.current_action_name may not be updated yet on the first
    // frame of a new action.
    ExecutionContext build_context(
        const goap::Action& action,
        goap::WorldState& world_state,
        float x, float y, float rotation, float dt,
        const CreatureParams& params);
};

} // namespace npc_ai

#endif // GOAP_PLAN_EXECUTOR_H
