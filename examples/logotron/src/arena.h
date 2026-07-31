// KG-backed cycles + per-run trail segments.
//
// Each straight-line run between direction changes is ONE
// TrailSegment entity with (start_x, start_y, end_x, end_y, direction).
// When the cycle turns, freeze_run() seals the current run and
// starts a new one. The visualization layer renders each frozen
// segment as a single thin rectangle spanning its endpoints; the
// active (growing) run is drawn each frame by the game code.
//
// Collision is a line-segment proximity check: the cycle's next
// position is lethal if it's within kWallThickness/2 of any frozen
// trail's line or any ArenaWall, or outside the arena bounds.

#pragma once

#include "cycle.h"
#include "logosphere/kg/kg_module.h"

namespace logotron {

// Spawn a cycle entity at (x, y) facing dir. run_start is initialized
// to (x, y) so the first call to freeze_run seals the spawn-to-turn
// segment.
kg::EntityID spawn_cycle(kg::KGModule& kg,
                         const std::string& entity_type,
                         float x, float y,
                         Direction dir);

Cycle read_cycle(const kg::KGModule& kg, kg::EntityID cycle_entity);
void  write_cycle(kg::KGModule& kg, kg::EntityID cycle_entity, const Cycle& c);

// One frame of motion. Advances x/y by kCycleSpeed * dt in the
// cycle's current direction. Does NOT lay trails — those happen at
// turn time via freeze_run.
void step_cycle_in_kg(kg::KGModule& kg, kg::EntityID cycle_entity, float dt);

// Seal the current run: create a TrailSegment entity spanning
// (run_start_x, run_start_y) → (c.x, c.y) with the cycle's current
// direction. Then update run_start to the current position so the
// next run starts here. Called on direction change.
//
// If the cycle hasn't moved since spawn/last turn (run length ≈ 0),
// no entity is created.
void freeze_run(kg::KGModule& kg, kg::EntityID cycle_entity);

// Same as freeze_run but stamps a spawn_time on the created
// TrailSegment so a lifetime system can fade/remove it later.
// Pass the current game-layer clock; pass 0 to opt out of lifetime
// (trail persists for the round).
void freeze_run_at(kg::KGModule& kg, kg::EntityID cycle_entity,
                   float spawn_time);

// Count trail segments owned by a cycle (tests + diagnostics).
int count_trails_owned_by(const kg::KGModule& kg, kg::EntityID cycle_entity);

// Collision at a projected world position. Lethal if:
//   - out of bounds, OR
//   - within kWallThickness/2 perpendicular of any TrailSegment's
//     line (and between its endpoints along the run axis), OR
//   - within kWallThickness/2 of any ArenaWall's span.
//
// `self_run_endpoint` lets the caller say "ignore trails that end
// exactly here" — this is the just-frozen trail from the cycle's
// current run, which the cycle is riding on. Pass NaN to check
// everything.
// What killed a cycle. Logged verbatim into telemetry + used by the
// review script to print a crash narrative. String-enum rather than
// a C++ enum so the JSON files stay human-readable without a decoder.
enum class CollisionCause {
    NONE,                     // not lethal
    OUT_OF_BOUNDS,            // ran off the arena edge
    SEALED_TRAIL_SELF,        // hit a frozen segment the cycle itself laid
    SEALED_TRAIL_OTHER,       // hit an opponent's frozen segment
    OPPONENT_ACTIVE_RUN,      // hit an opponent's live (un-sealed) run
};
const char* collision_cause_name(CollisionCause c);

struct CollisionResult {
    bool            lethal = false;
    CollisionCause  cause  = CollisionCause::NONE;
    // Coordinates of the attempted position that got rejected.
    float           hit_x  = 0.0f;
    float           hit_y  = 0.0f;
    // Entity that did the killing, if applicable. INVALID_ENTITY for
    // OUT_OF_BOUNDS (the arena isn't an entity in this game).
    kg::EntityID    hit_entity = kg::INVALID_ENTITY;
    // For SEALED_TRAIL_* causes: age of the segment at kill time
    // (if spawn_time was recorded). 0 for missing.
    float           hit_age = 0.0f;
};

bool check_collision(const kg::KGModule& kg, float x, float y,
                     float arena_w, float arena_h,
                     float self_run_endpoint_x,
                     float self_run_endpoint_y);

// As above, but also tests the point against OTHER cycles' active
// (unsealed) runs — the live trail each RIDING cycle is currently
// drawing but has not yet frozen into a TrailSegment. Pass the caller's
// own cycle entity so its own live trail is excluded. Without this an
// opponent could only crash into already-frozen past segments, never
// the segment being drawn in real time; players could cross right
// through the opponent's live trail until the opponent turned.
bool check_collision(const kg::KGModule& kg, float x, float y,
                     float arena_w, float arena_h,
                     float self_run_endpoint_x,
                     float self_run_endpoint_y,
                     kg::EntityID self_cycle);

// Time-aware overload: same as above, but also treats a trail as
// non-lethal once its age exceeds kTrailLifetime. Pass the game-
// layer clock (seconds since round start) as `now_seconds`; pass
// 0 to opt out of lifetime tracking (legacy behavior).
bool check_collision_at(const kg::KGModule& kg, float x, float y,
                        float arena_w, float arena_h,
                        float self_run_endpoint_x,
                        float self_run_endpoint_y,
                        kg::EntityID self_cycle,
                        float now_seconds);

// Full detail: same semantics but returns WHY. Used by the step
// function to stamp crash metadata onto the cycle entity for
// telemetry. Layering the bool version on top keeps the existing
// tests + call sites cheap.
CollisionResult check_collision_detailed(const kg::KGModule& kg,
                                         float x, float y,
                                         float arena_w, float arena_h,
                                         float self_run_endpoint_x,
                                         float self_run_endpoint_y,
                                         kg::EntityID self_cycle,
                                         float now_seconds);

// Collision-aware step. Projects the next position, calls
// check_collision; on hit the cycle flips to CRASHED. Otherwise
// delegates to step_cycle_in_kg.
void step_cycle_in_kg_with_collision(kg::KGModule& kg,
                                     kg::EntityID cycle_entity,
                                     float arena_w, float arena_h,
                                     float dt);

// Same, but threads the game clock (seconds since round start)
// through to check_collision so trails past kTrailLifetime are
// treated as non-lethal.
void step_cycle_in_kg_with_collision_at(kg::KGModule& kg,
                                        kg::EntityID cycle_entity,
                                        float arena_w, float arena_h,
                                        float dt,
                                        float now_seconds);

} // namespace logotron
