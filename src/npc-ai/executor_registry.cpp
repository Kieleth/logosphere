// Executor Registry Implementation

#include "executor_registry.h"
#include <iostream>

namespace npc_ai {

void ExecutorRegistry::register_executor(const std::string& action_name,
                                          std::unique_ptr<ActionExecutor> executor) {
    if (!executor) {
        std::cerr << "[ExecutorRegistry] WARNING: null executor for " << action_name << std::endl;
        return;
    }
    executors_[action_name] = std::move(executor);
}

ActionExecutor* ExecutorRegistry::get_executor(const std::string& action_name) {
    auto it = executors_.find(action_name);
    if (it == executors_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool ExecutorRegistry::has_executor(const std::string& action_name) const {
    return executors_.find(action_name) != executors_.end();
}

ExecutionResult ExecutorRegistry::execute(const std::string& action_name,
                                          const ExecutionContext& ctx) {
    ActionExecutor* executor = get_executor(action_name);
    if (!executor) {
        std::cerr << "[ExecutorRegistry] ERROR: no executor for action '"
                  << action_name << "'" << std::endl;
        return ExecutionResult::failed();
    }

    return executor->execute(ctx);
}

void ExecutorRegistry::start_action(const std::string& action_name, ExecutionContext& ctx) {
    ActionExecutor* executor = get_executor(action_name);
    if (executor) {
        executor->on_start(ctx);
    }
}

void ExecutorRegistry::end_action(const std::string& action_name,
                                  const ExecutionContext& ctx, bool success) {
    ActionExecutor* executor = get_executor(action_name);
    if (executor) {
        executor->on_end(ctx, success);
    }
}

} // namespace npc_ai
