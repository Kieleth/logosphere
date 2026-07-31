// Executor Registry - Maps action names to executor instances
//
// The registry holds all registered executors and dispatches
// action execution based on action name from GOAP plans.
//
// Usage:
//   ExecutorRegistry registry;
//   registry.register_executor("PURSUE", std::make_unique<PursueExecutor>());
//   registry.register_executor("EAT", std::make_unique<EatExecutor>());
//
//   // During update:
//   const char* action_name = current_plan.next_action()->name;
//   ExecutionResult result = registry.execute(action_name, context);

#ifndef EXECUTOR_REGISTRY_H
#define EXECUTOR_REGISTRY_H

#include "action_executor.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace npc_ai {

// ============================================
// EXECUTOR REGISTRY
// Maps action names → executor instances
// ============================================
class ExecutorRegistry {
public:
    ExecutorRegistry() = default;

    // Register an executor for an action name
    // Takes ownership of the executor
    void register_executor(const std::string& action_name,
                           std::unique_ptr<ActionExecutor> executor);

    // Get executor by action name
    // Returns nullptr if not registered
    ActionExecutor* get_executor(const std::string& action_name);

    // Check if executor exists for action
    bool has_executor(const std::string& action_name) const;

    // Execute an action by name
    // Returns failed result if executor not found
    ExecutionResult execute(const std::string& action_name,
                           const ExecutionContext& ctx);

    // Call on_start for an action (when action begins)
    void start_action(const std::string& action_name, ExecutionContext& ctx);

    // Call on_end for an action (when action completes)
    void end_action(const std::string& action_name,
                   const ExecutionContext& ctx, bool success);

    // Get number of registered executors
    size_t size() const { return executors_.size(); }

    // Clear all registered executors
    void clear() { executors_.clear(); }

private:
    std::unordered_map<std::string, std::unique_ptr<ActionExecutor>> executors_;
};

} // namespace npc_ai

#endif // EXECUTOR_REGISTRY_H
