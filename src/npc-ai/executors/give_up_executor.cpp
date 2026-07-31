// GiveUpExecutor Implementation

#include "give_up_executor.h"
#include <iostream>

namespace npc_ai {

void GiveUpExecutor::on_start(ExecutionContext& /* ctx */) {
    elapsed_time_ = 0;
    std::cout << "[GIVE_UP] Frustrated! Standing confused..." << std::endl;
}

ExecutionResult GiveUpExecutor::execute(const ExecutionContext& ctx) {
    elapsed_time_ += ctx.dt;

    MovementIntent intent;
    intent.is_moving = false;
    intent.forward_velocity = 0;
    intent.wants_body_turn = false;
    intent.wants_head_look = false;

    // Just stand there, confused
    if (elapsed_time_ >= CONFUSION_DURATION) {
        std::cout << "[GIVE_UP] Done. Returning to wandering." << std::endl;
        return ExecutionResult::succeeded(intent);
    }

    return ExecutionResult::in_progress(intent);
}

} // namespace npc_ai
