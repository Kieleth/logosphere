// GOAP System - Goal-Oriented Action Planning
//
// Generic GOAP implementation for NPC decision-making.
// Game-specific actions and world states are defined by the game,
// this system provides the planning infrastructure.
//
// Based on F.E.A.R.'s GOAP (Jeff Orkin, 2006)
//
// ============================================================================
// HOW GOAP WORKS - A GPS FOR DECISIONS
// ============================================================================
//
// GOAP is like GPS navigation for AI decisions:
//   1. Where you are     → WorldState  (current facts the NPC believes)
//   2. Where you want    → Goal        (desired world state)
//   3. Available roads   → Actions     (things NPC can do)
//   4. Route calculation → Planner     (A* search finds action sequence)
//
// ============================================================================
// EXAMPLE: HUNGRY SHAMBLER FINDS FOOD
// ============================================================================
//
// WorldState (what shambler knows):
//   { hungry: 1, food_known: 1, at_food: 0 }
//
// Goal:
//   { hungry: 0 }   // "I want to not be hungry"
//
// Available Actions:
//   PURSUE: preconditions={food_known:1, at_food:0} → effects={at_food:1}
//   EAT:    preconditions={at_food:1, hungry:1}     → effects={hungry:0}
//
// A* Search Process:
//   ┌──────────────────────────────────────────────────────────────────┐
//   │  State: {hungry:1, food_known:1, at_food:0}                      │
//   │  Goal satisfied? No (hungry != 0)                                │
//   │                                                                  │
//   │  Try PURSUE: preconditions met? ✓ YES                            │
//   │    → Apply effects: at_food becomes 1                            │
//   │    → New state: {hungry:1, food_known:1, at_food:1}              │
//   │    → Add to search queue                                         │
//   │                                                                  │
//   │  Try EAT: preconditions met? ✗ NO (at_food=0, need at_food=1)    │
//   └──────────────────────────────────────────────────────────────────┘
//   ┌──────────────────────────────────────────────────────────────────┐
//   │  State: {hungry:1, food_known:1, at_food:1}  (after PURSUE)      │
//   │  Goal satisfied? No (hungry != 0)                                │
//   │                                                                  │
//   │  Try EAT: preconditions met? ✓ YES                               │
//   │    → Apply effects: hungry becomes 0                             │
//   │    → New state: {hungry:0, food_known:1, at_food:1}              │
//   │    → Goal satisfied! ✓                                           │
//   └──────────────────────────────────────────────────────────────────┘
//
// PLAN OUTPUT: [PURSUE → EAT]
//
// ============================================================================
// INTEGRATION WITH BRAIN
// ============================================================================
//
// The planner outputs action NAMES. The brain must EXECUTE them:
//
//   GOAP Planner              Brain::update()              Locomotion
//       │                           │                           │
//       │ Plan: [PURSUE, EAT]       │                           │
//       └──────────────────────────>│                           │
//                                   │ current action = PURSUE   │
//                                   │                           │
//                                   │ Execute PURSUE:           │
//                                   │   set_path_to(food_x, y)  │
//                                   │   follow_path()───────────┼──> movement
//                                   │                           │
//                                   │ Arrived at food?          │
//                                   │   YES → pop_action()      │
//                                   │   current action = EAT    │
//                                   │                           │
//                                   │ Execute EAT:              │
//                                   │   consume food            │
//                                   │   world_state[hungry]=0   │
//                                   │   pop_action()            │
//                                   │   plan empty → wander     │
//
// ============================================================================
// WHEN TO REPLAN
// ============================================================================
//
// Call replan() when:
//   - WorldState changes (food discovered, target lost)
//   - Current action fails (path blocked)
//   - Goal changes (new priority emerges)
//
// Do NOT replan every frame - it's expensive (A* search).
//
// ============================================================================
// TODO[ARCH]: MULTI-GOAL SUPPORT
// ============================================================================
//
// Current: Shambler POC uses single goal (SATIATED = hungry:0)
//
// Extension needed:
//   1. Multiple goals with priorities:
//      Goal{"SURVIVE",  {hurt:0},   priority: 100}
//      Goal{"SATIATED", {hungry:0}, priority: 50}
//      Goal{"REST",     {tired:0},  priority: 30}
//
//   2. Dynamic priority calculation:
//      goal_survive.priority = (health < 20) ? 150 : 10;
//      goal_eat.priority = hunger_level * 2;
//
//   3. Replan picks highest-priority unsatisfied goal
//
// This gives GOAP the context-awareness of Utility AI while keeping
// the planning/sequencing benefits of GOAP.
//
// ============================================================================

#ifndef GOAP_SYSTEM_H
#define GOAP_SYSTEM_H

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace goap {

// ============================================
// WORLD STATE
// Key-value pairs representing what an NPC knows
// ============================================

using WorldState = std::unordered_map<std::string, int>;

// Helper: check if state A satisfies requirements B
// (all keys in B exist in A with same values)
inline bool satisfies(const WorldState& state, const WorldState& requirements) {
    for (const auto& [key, value] : requirements) {
        auto it = state.find(key);
        if (it == state.end() || it->second != value) {
            return false;
        }
    }
    return true;
}

// Helper: apply effects to state (modifies in place)
inline void apply_effects(WorldState& state, const WorldState& effects) {
    for (const auto& [key, value] : effects) {
        state[key] = value;
    }
}

// ============================================
// ACTION
// Something an NPC can do
// ============================================

struct Action {
    std::string name;
    WorldState preconditions;  // Required state to execute
    WorldState effects;        // State changes after execution
    float cost = 1.0f;         // Planning cost (lower = preferred)

    // Optional: dynamic cost function (called during planning)
    // If set, overrides static cost
    std::function<float(const WorldState&)> cost_fn = nullptr;

    float get_cost(const WorldState& state) const {
        if (cost_fn) return cost_fn(state);
        return cost;
    }

    bool can_execute(const WorldState& state) const {
        return satisfies(state, preconditions);
    }
};

// ============================================
// GOAL
// Desired world state
// ============================================

struct Goal {
    std::string name;
    WorldState desired_state;  // What we want to achieve
    float priority = 1.0f;     // Higher = more important

    bool is_satisfied(const WorldState& state) const {
        return satisfies(state, desired_state);
    }
};

// ============================================
// PLAN
// Sequence of actions to achieve a goal
// ============================================

struct Plan {
    std::vector<const Action*> actions;
    float total_cost = 0.0f;
    bool valid = false;

    bool empty() const { return actions.empty(); }
    size_t size() const { return actions.size(); }

    const Action* next_action() const {
        if (actions.empty()) return nullptr;
        return actions.front();
    }

    void pop_action() {
        if (!actions.empty()) {
            actions.erase(actions.begin());
        }
    }
};

// ============================================
// PLANNER
// A* search over action space
// ============================================

class Planner {
public:
    Planner() = default;

    // Register available actions
    void add_action(const Action& action);
    void clear_actions();

    // Plan: find action sequence to achieve goal from current state
    // Returns empty plan if no path found
    Plan plan(const WorldState& current_state, const Goal& goal, int max_depth = 10);

    // Debug: enable verbose output
    void set_verbose(bool v) { verbose_ = v; }

private:
    std::vector<Action> actions_;
    bool verbose_ = false;

    // A* node for planning
    struct Node {
        WorldState state;
        std::vector<const Action*> actions_taken;
        float g_cost = 0;  // Cost so far
        float h_cost = 0;  // Heuristic (estimated remaining)
        float f_cost() const { return g_cost + h_cost; }
    };

    // Heuristic: count unsatisfied goal conditions
    float heuristic(const WorldState& state, const Goal& goal) const;
};

} // namespace goap

#endif // GOAP_SYSTEM_H
