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
#include "food_state.h"

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
    float eat_duration = 2.0f;  // Legacy: used if no food_state provided

    // Target locations (read from brain state, not set externally)
    float food_x = 0.0f;
    float food_y = 0.0f;
    float smell_target_x = 0.0f;
    float smell_target_y = 0.0f;

    // Eating parameters (physics-based consumption)
    // See logomancers/docs/npc_consumables.md
    float mouth_volume_cm3 = 0.0f;   // MUST be calculated from head particle (0 = error)
    FoodState* food_state = nullptr; // Current food being eaten (nullptr = use legacy timer)
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

    // Build context for executor
    // action_name is passed explicitly because state_.current_action_name
    // may not be updated yet on the first frame of a new action
    ExecutionContext build_context(
        const std::string& action_name,
        goap::WorldState& world_state,
        float x, float y, float rotation, float dt,
        const CreatureParams& params);
};

} // namespace npc_ai

#endif // GOAP_PLAN_EXECUTOR_H
