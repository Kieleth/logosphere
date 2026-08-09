// MeanderExecutor - purposeless motion, on purpose.
//
// GAME-side (predator example): what "casting about" looks like is
// creature character, not engine mechanism. Picks a random reachable
// waypoint within meander_radius, walks there at walk_speed through the
// pathfinder (walls respected), picks another. Never completes: a
// meander has no goal, so only the brain replanning away from it — a
// scent, a sighting — ends it. That is the GOAP shape of "wander until
// the world gives you something better to do."
//
// Deliberately stateful (waypoint + rng), unlike the engine's stateless
// executors: this instance belongs to ONE creature in this game, and
// the seed makes headless runs reproducible.

#ifndef MEANDER_EXECUTOR_H
#define MEANDER_EXECUTOR_H

#include "npc-ai/action_executor.h"

namespace predator {

class MeanderExecutor : public ActionExecutor {
public:
    explicit MeanderExecutor(unsigned seed = 20260808u) : rng_state_(seed) {}

    const char* name() const override { return "MEANDER"; }
    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

    float meander_radius = 9.0f;   // how far afield one leg wanders

private:
    bool  has_waypoint_ = false;
    int   sector_ = 0;       // quartering: which compass eighth is next
    float wx_ = 0.0f, wy_ = 0.0f;
    unsigned rng_state_;

    float frand();                          // [0,1), xorshift, seeded
    void pick_waypoint(const ExecutionContext& ctx);
};

} // namespace predator

#endif // MEANDER_EXECUTOR_H
