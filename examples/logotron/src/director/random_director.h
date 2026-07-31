// Offline Director: picks 1-2 mutations at random and emits them as
// the same JSON the LLM would produce. Used when no API key is
// configured so the game still feels alive between rounds.
//
// Same Responder shape as the LLM responder, so plugging this into
// Director is one-line: `director.set_responder(make_random_responder(seed))`.

#pragma once

#include <cstdint>
#include <functional>

#include "director/director.h"

namespace logotron::director {

// Live game state the random responder reads at FIRE time (the
// responder used to bake 40x40 dims at construction, so arena
// resizes never fed back into wall placement). escalation_tier
// gates the vocabulary: tier 2+ adds personality swaps and denser
// wall spawns, tier 3+ shrinks the arena harder, tier 4+ may darken
// the Grid (grid_fog).
struct RandomDirectorContext {
    float arena_w = 40.0f;
    float arena_h = 40.0f;
    int   escalation_tier = 0;
};

// Build a synchronous Responder that ignores its prompt and emits a
// random valid mutation JSON. The seed makes test runs deterministic;
// pass 0 to seed from std::random_device. `context` is called at
// each fire to read live dims + tier; pass nullptr for defaults
// (tests, setup pre-fire).
Director::Responder make_random_responder(
    uint32_t seed,
    std::function<RandomDirectorContext()> context = nullptr);

// Pure helper used by the responder. Exposed for direct testing.
std::string generate_random_mutation_json(uint32_t seed,
                                            const RandomDirectorContext& ctx);

} // namespace logotron::director
