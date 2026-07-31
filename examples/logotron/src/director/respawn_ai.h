// AI-only respawn for the Weirden Director loop.
//
// When the AI cycle crashes, the round does NOT end (per Logotron
// design: player keeps riding through). Instead, the Director mutates
// the world and respawns the AI fresh. This is the KG-side primitive
// for that respawn: wipes the dead AI cycle and the trails it owned,
// then creates a new AICycle entity at the requested spawn point.
//
// Pure KG: no engine, no particle system, no rendering. The live game
// wraps this with particle cleanup and motorcycle assembly recreation
// (see LogotronApplication::respawn_ai_in_world).

#pragma once

#include "cycle.h"
#include "logosphere/kg/kg_module.h"

#include <unordered_set>

namespace logotron::director {

struct AISpawnSpec {
    float x = 5.5f;
    float y = 5.5f;
    Direction direction = Direction::NORTH;
};

// Result of an AI respawn. The new entity ID is what the application
// stores for subsequent ticks; trails_wiped + cycle_destroyed are
// useful for tests and diagnostics.
struct AIRespawnResult {
    kg::EntityID new_ai_entity = kg::INVALID_ENTITY;
    int trails_wiped = 0;
    bool old_cycle_destroyed = false;
};

// Wipe the previous AI cycle entity and every TrailSegment it owned,
// then spawn a fresh AICycle at the requested position/direction.
// Player entity, player trails, ArenaWalls, Wormholes, and any other
// non-AI state are left untouched.
//
// Pass kg::INVALID_ENTITY for old_ai_entity on the very first spawn
// (nothing to wipe).
//
// rendered_trails: the caller's "already has a particle" set — every
// destroyed trail entity is erased from it so a reused EntityID can
// render again. Passing nullptr skips the prune (KG-only callers).
AIRespawnResult respawn_ai(kg::KGModule& kg,
                            kg::EntityID old_ai_entity,
                            const AISpawnSpec& spec,
                            std::unordered_set<kg::EntityID>* rendered_trails = nullptr);

} // namespace logotron::director
