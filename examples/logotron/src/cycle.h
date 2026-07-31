// Cycle motion for Logotron — continuous position, grid-snapped trails.
//
// Logosphere is a particle-first, continuous-space engine, so a Cycle
// has continuous (x, y) world coordinates and advances by velocity * dt
// each frame. Trails snap to integer cells on a 1 m sub-grid: as a
// cycle crosses a cell boundary, one TrailSegment particle is laid at
// the entered cell. This gives smooth cycle motion with cell-level
// collision queries that don't require a spatial proximity index.
//
// Grid convention: world (0, 0) is the arena's origin; +x east,
// +y north (NORTH = +y matches the iso camera's screen-up). The
// arena center is also the world origin (the spawn-side code
// centers the playing field). cell_x = floor(x / kCollisionCell).
//
// All functions in this header are pure (no engine, no KG). Headless-
// testable. The KG-coupled wrappers live in arena.h.

#pragma once

#include <limits>

namespace logotron {

enum class Direction { NORTH, EAST, SOUTH, WEST };
enum class CycleState { RIDING, CRASHED };

struct Cycle {
    float x = 0.0f;            // continuous world position (meters)
    float y = 0.0f;
    Direction direction = Direction::EAST;
    CycleState state = CycleState::RIDING;
    // Start of the current straight-line run (since spawn or last
    // turn). When the cycle turns, this run gets "frozen" into a
    // TrailSegment spanning (run_start → current pos), then the new
    // run_start becomes the current position.
    float run_start_x = 0.0f;
    float run_start_y = 0.0f;
    // Speed model — recency-of-turn ramp (see GAME_DESIGN.md §18).
    // Every turn snaps current_speed back to base_speed and restarts
    // the time_since_turn clock. Each tick, current_speed climbs
    // toward max_speed at speed_ramp_rate (m/s per second).
    //
    // Defaults preserve LEGACY constant-speed behavior:
    // max_speed == base_speed and speed_ramp_rate == 0 means no
    // ramping. The game (main.cpp) explicitly sets nonzero ramp
    // values on the player + AI cycles to enable the §18 mechanic.
    // This keeps existing tests + tools that construct a default
    // Cycle behaving exactly as before.
    float base_speed       = 5.0f;
    float max_speed        = 5.0f;
    float speed_ramp_rate  = 0.0f;
    float current_speed    = 5.0f;   // runtime, recomputed each tick
    float time_since_turn  = 0.0f;   // seconds; reset by turn()
};

// Default cruising speed in m/s. Seeds Cycle::base_speed for legacy
// callers; new code should read from the KG entity instead.
constexpr float kCycleSpeed = 5.0f;

// Wall thickness (perpendicular extent of a trail). Used by both the
// visual and the collision proximity check — if the cycle's next
// position is within half this distance of any trail line, it's a
// hit.
constexpr float kWallThickness = 0.15f;

// Seconds a frozen TrailSegment stays LETHAL before its age starts
// the kTrailFadeDuration fade-out. Set effectively infinite for the
// Trail policy: the RIBBON model (2026-07-23, replaces the brief
// age-fade attempt from earlier the same day — whole-segment fades
// read as chunky, nothing like a light cycle). Each cycle's trail is
// a finite ribbon: total length (sealed segments + the live run) is
// capped by the cycle's `tail_length` KG property (Director-mutable,
// schema range 8..200; default below). Every frame the oldest end
// erodes by exactly the overage — a continuous recede, the classic
// look. Erosion rewrites the TrailSegment start coords in the KG,
// which is the same data collision reads: visible == lethal holds BY
// CONSTRUCTION, no age gate needed (kTrailLifetime stays effectively
// infinite; geometry that no longer exists cannot kill).
constexpr float kTrailLifetime         = 1.0e9f;
// Ribbon budget: base meters plus a speed-coupled bonus — a fast bike
// drags a longer, more dangerous wake (kTailSpeedCoupling meters per
// m/s above base speed). A turn dumps speed back to base, and the
// overgrown tail collapses in a rush behind you. Capped by the
// tail_length schema range (200 m).
constexpr float kTrailDefaultTailMeters = 140.0f;
constexpr float kTailSpeedCoupling      = 7.0f;

// Speed policy: the recency-of-turn ramp gets a ceiling worth fearing.
// Long straights soar toward kCycleMaxSpeed over ~9 s; every turn
// resets to kCycleBaseSpeed. The Director may still retune max_speed
// per cycle within the schema range (0.1..25); the fairness clamp
// keeps the Programs <= the User's ceiling.
constexpr float kCycleBaseSpeed = 6.0f;
constexpr float kCycleMaxSpeed  = 18.0f;
constexpr float kCycleRampRate  = 1.3f;   // m/s per second of straight

// Roster policy: enemy Programs seeded at round start (arcade Tron
// 3v1). The Director can grow or cull the roster mid-round via
// create_entity AICycle / destroy_entity ops (capped at 6 in the
// app reconciler).
constexpr int kDefaultEnemyCount = 3;

// (dx, dy) unit vector for a cardinal direction. NORTH = (0, -1) by
// the +y-south grid convention.
struct GridDelta { int dx; int dy; };
GridDelta delta_for(Direction d);

// One frame of motion. dt is wall-clock seconds since the last call.
// - Advances x/y by speed * dt in the cycle's current direction.
// - Does nothing if state != RIDING.
// - Does NOT lay trails or check collisions; arena.h handles those.
void step_cycle(Cycle& c, float dt);


// Tron rule: 90° turns OK, U-turn into your own trail not OK.
bool is_legal_turn(Direction current, Direction next);

// Relative steering. LEFT / RIGHT rotate 90° about the cycle's
// current heading. Always orthogonal to input, so always legal under
// the no-U-turn rule.
Direction turn_left(Direction d);
Direction turn_right(Direction d);

// Commit a direction change AND reset the recency-of-turn speed
// state. Always use this from caller code that changes direction —
// writing `c.direction = ...` directly bypasses the speed reset and
// breaks the §18 locomotion contract. Idempotent for same-direction
// "turns" (no reset; a no-op).
void turn(Cycle& c, Direction next_dir);

} // namespace logotron
