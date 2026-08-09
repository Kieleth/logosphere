// Eat Executor - Consume food at current location
//
// Timed action that simulates eating. Creature stands still,
// waits for ctx.action_duration seconds, then completes.
// On success, updates world_state to mark hunger as satisfied.

#ifndef EAT_EXECUTOR_H
#define EAT_EXECUTOR_H

#include "npc-ai/action_executor.h"

namespace npc_ai {

class EatExecutor : public ActionExecutor {
public:
    EatExecutor() = default;

    const char* name() const override { return "EAT"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;
    void on_end(const ExecutionContext& ctx, bool success) override;

private:
    // World state keys to update on completion
    // Default: sets "hungry" to 0
    static constexpr const char* KEY_HUNGRY = "hungry";
};

} // namespace npc_ai

#endif // EAT_EXECUTOR_H
