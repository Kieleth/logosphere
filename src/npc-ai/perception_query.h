// Perception Query - Generic interface for creature senses
//
// Part of the Perception Layer (Option C) - provides types that creatures
// can use to report perception results to the GOAP system.
//
// Architecture:
//   Creature Brain (owns senses)
//         │
//         │ Calls senses_->look_for()
//         │
//         ▼
//   PerceptionResult (generic type)
//         │
//         │ Updates world state
//         │ Triggers GOAP replan
//         ▼
//   GOAP System → Plan actions based on perception
//
// Usage:
//   In creature update() or during SCAN action:
//     PerceptionResult result = perception_check(position, facing, targets);
//     if (result.target_found) {
//         world_state_[FOOD_KNOWN] = 1;
//         set_target(result.target_x, result.target_y);
//         replan();
//     }

#ifndef PERCEPTION_QUERY_H
#define PERCEPTION_QUERY_H

#include <functional>

namespace npc_ai {

// Request for a perception check
// Creature brain fills this with current state
struct PerceptionRequest {
    // Position to look from
    float pos_x = 0;
    float pos_y = 0;
    float pos_z = 0;

    // Direction to look (radians, 0 = +Y north)
    float look_angle = 0;

    // Optional: override default FOV for this check
    // Set to 0 to use creature's default FOV
    float fov_override = 0;
};

// Result of a perception check
// Single target found (nearest or most relevant)
struct PerceptionResult {
    bool target_found = false;

    // Target information (valid if target_found)
    int target_id = -1;
    float target_x = 0;
    float target_y = 0;
    float target_z = 0;
    float distance = 0;
    float angle_offset = 0;  // From look direction
};

// Callback type for perception queries
// Creature brain provides this, executors can call it
using PerceptionCallback = std::function<PerceptionResult(const PerceptionRequest&)>;

} // namespace npc_ai

#endif // PERCEPTION_QUERY_H
