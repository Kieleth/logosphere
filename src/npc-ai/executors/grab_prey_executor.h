// Grab Prey Executor - Latch onto living prey and bite
//
// When shambler reaches living prey (humanoid), it grabs on and bites.
// This is different from EAT which consumes static food.
//
// Behavior:
// - Stay stationary while grabbing
// - Look at prey (target position)
// - Signals "grabbing" via world state for brain to deal damage
// - Never completes on its own - brain must detect prey death/escape

#ifndef GRAB_PREY_EXECUTOR_H
#define GRAB_PREY_EXECUTOR_H

#include "../action_executor.h"

namespace npc_ai {

class GrabPreyExecutor : public ActionExecutor {
public:
    GrabPreyExecutor() = default;

    const char* name() const override { return "GRAB_PREY"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;
    void on_end(const ExecutionContext& ctx, bool success) override;

private:
    // World state keys
    static constexpr const char* KEY_PREY_GRABBED = "prey_grabbed";
    static constexpr const char* KEY_HUNGRY = "hungry";
};

} // namespace npc_ai

#endif // GRAB_PREY_EXECUTOR_H
