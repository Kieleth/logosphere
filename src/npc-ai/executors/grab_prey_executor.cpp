// Grab Prey Executor Implementation
//
// The shambler grabs living prey and holds on while biting.
// Damage dealing is handled by the brain (ShamblerBrain) which
// monitors the PREY_GRABBED state and generates damage events.
//
// This executor just maintains the grab state and keeps the
// shambler stationary while grabbing.

#include "grab_prey_executor.h"
#include <iostream>

namespace npc_ai {

void GrabPreyExecutor::on_start(ExecutionContext& ctx) {
    // Set PREY_GRABBED state
    if (ctx.world_state_mutable) {
        (*ctx.world_state_mutable)[KEY_PREY_GRABBED] = 1;
    }

    // Log grab start
    std::cout << "[GRAB_PREY] Latched onto prey!" << std::endl;
}

ExecutionResult GrabPreyExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Stay stationary while grabbing
    intent.is_moving = false;
    intent.forward_velocity = 0;
    intent.wants_body_turn = false;

    // Look at prey (target position)
    if (ctx.has_target) {
        intent.look_x = ctx.target_x;
        intent.look_y = ctx.target_y;
        intent.wants_head_look = true;
    }

    // This action never completes on its own.
    // The brain must detect prey death/escape and replan.
    // We could check for prey escape here, but for now keep it simple.
    //
    // The brain handles:
    // - Dealing damage (via damage event queue)
    // - Detecting prey death (HP <= 0)
    // - Detecting prey escape (distance > grab range after struggle)
    //
    // For MVP, we stay in this state indefinitely until brain replans.

    return ExecutionResult::in_progress(intent);
}

void GrabPreyExecutor::on_end(const ExecutionContext& ctx, bool success) {
    // Clear PREY_GRABBED state
    if (ctx.world_state_mutable) {
        (*ctx.world_state_mutable)[KEY_PREY_GRABBED] = 0;
    }

    if (success) {
        // Prey consumed or dead
        std::cout << "[GRAB_PREY] Prey consumed!" << std::endl;
    } else {
        // Prey escaped
        std::cout << "[GRAB_PREY] Prey escaped!" << std::endl;
    }
}

} // namespace npc_ai
