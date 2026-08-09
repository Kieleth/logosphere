// PredatorContext - the predator game's per-frame executor context.
//
// This struct is what used to live INSIDE the engine's ExecutionContext
// as mouth_volume_cm3 and a FoodState pointer, until #37 item 3 drew the
// line: a mouth is creature-diet policy, and the engine has no business
// knowing one exists. The brain fills this in, hands it to the engine as
// CreatureParams.game_data, and the engine copies the pointer into
// ExecutionContext.game_data without ever reading it. Executors on THIS
// side of the boundary cast it back.
//
// Pattern for any game: define your context, pass it opaque, cast it in
// your executors. The engine's generic executors never see it.

#ifndef PREDATOR_CONTEXT_H
#define PREDATOR_CONTEXT_H

#include "food_state.h"
#include "npc-ai/action_executor.h"

namespace predator {

struct PredatorContext {
    // Mouth capacity, computed from the head particle. 0 means the
    // brain forgot to fill it in; the eat executor then falls back to
    // the engine's generic action timer rather than inventing a mouth.
    float mouth_volume_cm3 = 0.0f;

    // Food currently being eaten, or nullptr (timer mode).
    npc_ai::FoodState* food_state = nullptr;
};

// The one cast, in one place. Null-safe: no game_data means no
// predator context, and callers get the timer fallback.
inline PredatorContext* predator_context(const ExecutionContext& ctx) {
    return static_cast<PredatorContext*>(ctx.game_data);
}

} // namespace predator

#endif // PREDATOR_CONTEXT_H
