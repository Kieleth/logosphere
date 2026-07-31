// GiveUpExecutor - Frustration threshold response
//
// When frustration exceeds threshold (from repeated blocked attempts),
// the creature gives up on its current goal.
//
// Behavior:
// - Stand confused for a moment (1-2 seconds)
// - Return to wandering (GOAP clears smell_detected, frustrated)
//
// This is the visceral "I can't do this" response, not intelligent planning.
// The creature doesn't know WHY it failed, just that it's frustrated.

#ifndef GIVE_UP_EXECUTOR_H
#define GIVE_UP_EXECUTOR_H

#include "../action_executor.h"

namespace npc_ai {

class GiveUpExecutor : public ActionExecutor {
public:
    GiveUpExecutor() = default;

    const char* name() const override { return "GIVE_UP"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

private:
    float elapsed_time_ = 0;
    static constexpr float CONFUSION_DURATION = 1.5f;  // Stand confused for 1.5 sec
};

} // namespace npc_ai

#endif // GIVE_UP_EXECUTOR_H
