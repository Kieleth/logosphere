// Eat Executor Implementation
//
// Two modes:
// 1. Physics-based: Uses food_state for bite-by-bite consumption
// 2. Legacy: Fixed timer (ctx.action_duration)
//
// See logomancers/docs/npc_consumables.md for consumption design.

#include "eat_executor.h"
#include "../food_state.h"
#include <iostream>
#include <random>

namespace npc_ai {

// Random number generator for bite variability
static std::mt19937& get_rng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

static float random_variability() {
    // ±20% variability per bite
    std::uniform_real_distribution<float> dist(0.8f, 1.2f);
    return dist(get_rng());
}

void EatExecutor::on_start(ExecutionContext& ctx) {
    // Physics-based mode: initialize first bite
    if (ctx.food_state && ctx.mouth_volume_cm3 > 0) {
        FoodState& food = *ctx.food_state;

        // Calculate time for first bite
        float base_time = 1.5f;  // seconds per bite
        float variability = random_variability();
        food.current_bite_timer = base_time * food.toughness * variability;
        food.is_chewing = true;
        // Note: Brain handles logging to event log
        return;
    }

    // Legacy mode: use fixed timer
    if (ctx.action_timer) {
        *ctx.action_timer = ctx.action_duration;
    }
}

ExecutionResult EatExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;

    // Stand still while eating
    intent.is_moving = false;
    intent.forward_velocity = 0;
    intent.wants_body_turn = false;

    // Look at food (target position)
    if (ctx.has_target) {
        intent.look_x = ctx.target_x;
        intent.look_y = ctx.target_y;
        intent.wants_head_look = true;
    }

    // ========================================
    // Physics-based consumption (bite-by-bite)
    // ========================================
    if (ctx.food_state && ctx.mouth_volume_cm3 > 0) {
        FoodState& food = *ctx.food_state;

        // Already depleted?
        if (food.is_depleted()) {
            return ExecutionResult::succeeded(intent);
        }

        // Process current bite
        if (food.is_chewing) {
            food.current_bite_timer -= ctx.dt;

            if (food.current_bite_timer <= 0) {
                // Bite complete - consume food
                bool has_more = food.take_bite(ctx.mouth_volume_cm3);

                if (!has_more) {
                    // Food fully consumed (brain handles logging)
                    return ExecutionResult::succeeded(intent);
                }

                // Start next bite
                float base_time = 1.5f;
                float variability = random_variability();
                food.current_bite_timer = base_time * food.toughness * variability;
            }
        }

        return ExecutionResult::in_progress(intent);
    }

    // ========================================
    // Legacy mode: fixed timer
    // ========================================
    if (ctx.action_timer) {
        if (*ctx.action_timer <= 0) {
            return ExecutionResult::succeeded(intent);
        }
    } else {
        // No timer, no food_state - complete immediately
        return ExecutionResult::succeeded(intent);
    }

    return ExecutionResult::in_progress(intent);
}

void EatExecutor::on_end(const ExecutionContext& ctx, bool success) {
    if (success && ctx.world_state_mutable) {
        // Mark as no longer hungry
        (*ctx.world_state_mutable)[KEY_HUNGRY] = 0;
    }
}

} // namespace npc_ai
