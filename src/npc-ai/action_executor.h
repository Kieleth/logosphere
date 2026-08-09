// Action Executor - Base interface for GOAP action execution
//
// GOAP planner outputs action NAMES (e.g., "PURSUE", "EAT").
// ActionExecutors implement the actual behavior for each action.
// This separation allows reusable executors across different NPC types.
//
// Architecture:
//   GOAP Planner → ["PURSUE", "EAT"]
//                        ↓
//   ExecutorRegistry → find executor for "PURSUE"
//                        ↓
//   PursueExecutor.execute(context) → MovementIntent + completion status
//
// Creatures configure executors differently:
//   Shambler: PURSUE at 1.5 m/s (slow)
//   Hunter:   PURSUE at 4.0 m/s (fast)
//   Same executor, different creature parameters in context.

#ifndef ACTION_EXECUTOR_H
#define ACTION_EXECUTOR_H

#include "goap_system.h"
#include "pathfinding_system.h"
#include "perception_query.h"

// ============================================
// MOVEMENT INTENT
// Output from brain to locomotion system
// ============================================
// Moved here from shambler_brain.h for generic use
struct MovementIntent {
    // Body locomotion - where the body faces for walking
    float body_facing = 0;         // Radians, 0 = +Y north
    bool wants_body_turn = false;  // Should body rotate toward body_facing?
    float forward_velocity = 0;    // m/s, positive = forward
    bool is_moving = false;

    // Head look-at - independent of body (for scanning, tracking)
    float look_x = 0, look_y = 0;  // World position to look at
    bool wants_head_look = false;  // Should head turn to look_x/look_y?
};

// ============================================
// EXECUTION RESULT
// Returned by executor each frame
// ============================================
struct ExecutionResult {
    MovementIntent movement;
    bool completed = false;     // Action finished?
    bool success = false;       // Succeeded (true) or failed (false)?

    // Static helpers for common results
    static ExecutionResult in_progress(const MovementIntent& m) {
        return { m, false, false };
    }

    static ExecutionResult succeeded(const MovementIntent& m = {}) {
        return { m, true, true };
    }

    static ExecutionResult failed(const MovementIntent& m = {}) {
        return { m, true, false };
    }
};

// ============================================
// EXECUTION CONTEXT
// Data passed to executors each frame
// ============================================
// Contains:
//   - Current creature state (position, rotation)
//   - Frame timing (dt)
//   - World access (world_state for reading goals)
//   - Pathfinding (for navigation)
//   - Creature config (speeds, distances - creature-specific!)
//
// Executors are stateless; all state comes through context.
// This allows sharing executors across multiple creatures.

struct ExecutionContext {
    // ---- Current creature state ----
    float pos_x = 0;           // World X position
    float pos_y = 0;           // World Y position
    float rotation = 0;        // Body rotation (radians, 0 = +Y north)
    float dt = 0;              // Delta time this frame (seconds)

    // ---- GOAP state ----
    // Read-only access to world state for checking conditions
    const goap::WorldState* world_state = nullptr;

    // Mutable world state for executors to update on completion
    // (e.g., EatExecutor sets hungry=0)
    goap::WorldState* world_state_mutable = nullptr;

    // ---- Pathfinding ----
    pathfinding::Pathfinder* pathfinder = nullptr;
    pathfinding::NavGrid* nav_grid = nullptr;
    pathfinding::Path* current_path = nullptr;  // Executor can read/modify path

    // ---- Target location (set by brain when action starts) ----
    // For PURSUE: food/target position
    // For FLEE: threat position (run opposite direction)
    float target_x = 0;
    float target_y = 0;
    bool has_target = false;

    // ---- Creature parameters (creature-specific!) ----
    // These vary per creature type, allowing same executor to behave differently
    float walk_speed = 0.8f;       // Walking speed (m/s)
    float run_speed = 1.5f;        // Running speed (m/s) - used for PURSUE
    float turn_speed = 1.5f;       // Turning rate (rad/s)
    float arrival_distance = 1.0f; // How close before "arrived"

    // ---- Action-specific config ----
    // Duration for timed actions (EAT, PAUSE)
    float action_duration = 2.0f;  // Seconds

    // Timer tracking for current action (executor reads/writes)
    float* action_timer = nullptr;

    // ---- Perception (Option C: Perception Layer) ----
    // Creature brain provides this callback for executors that need perception.
    // Executors call this to query what the creature can see.
    // Keeps sensing logic in creature brain (where senses are configured).
    npc_ai::PerceptionCallback perception = nullptr;

    // Scan state for SCAN executor
    float scan_angle = 0;       // Current offset from body facing
    int scan_direction = 1;     // 1 = right, -1 = left

    // ---- Game data passthrough ----
    // Opaque pointer the engine copies from CreatureParams and never
    // reads. Game-registered executors cast it back to whatever context
    // their game defines. This is the boundary working as intended: the
    // slot used to hold mouth_volume_cm3 and a FoodState*, which are
    // creature-diet policy, and the engine had no business knowing a
    // mouth exists (#37 item 3; the diet executors now live in
    // examples/predator/ai/ and carry their own context through here).
    void* game_data = nullptr;
};

// ============================================
// ACTION EXECUTOR (Base Interface)
// ============================================
// Implement this interface for each action type.
// Executors are stateless - all state passes through context.

class ActionExecutor {
public:
    virtual ~ActionExecutor() = default;

    // Execute one frame of the action
    // Returns: movement intent + completion status
    // Called every frame while action is active
    virtual ExecutionResult execute(const ExecutionContext& ctx) = 0;

    // Called when action starts (optional override)
    // Use for initialization (e.g., calculating path)
    virtual void on_start(ExecutionContext& ctx) {}

    // Called when action ends, success or failure (optional override)
    // Use for cleanup
    virtual void on_end(const ExecutionContext& ctx, bool success) {}

    // Get action name for debugging
    virtual const char* name() const = 0;
};

#endif // ACTION_EXECUTOR_H
