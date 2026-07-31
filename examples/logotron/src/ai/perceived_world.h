// PerceivedWorld — filtered snapshot of what the rider can sense.
//
// Tactics read ONLY this struct. They must not include kg_module.h
// or touch the KG directly; the perception layer (perception.h) is
// the one crossing point. This is the no-cheating contract
// described in GAME_DESIGN.md §16.
//
// Headless-safe: pure data + std types.

#pragma once

#include <array>
#include <optional>
#include <vector>

#include "ai/head.h"
#include "cycle.h"

namespace logotron::ai {

// One observed point in the rider's cone.
struct SeenPoint {
    float x = 0.0f;
    float y = 0.0f;
    float distance     = 0.0f;  // from rider to the point, meters
    float angle_offset = 0.0f;  // from rider forward, radians (-π..π)
};

// Indexed by cardinal direction — keep in sync with how tactics
// and the rest of the code look things up.
enum class DirIndex : int { NORTH = 0, EAST = 1, SOUTH = 2, WEST = 3, COUNT = 4 };
DirIndex to_dir_index(logotron::Direction d);
logotron::Direction from_dir_index(DirIndex d);

struct PerceivedWorld {
    // Distance in meters to the nearest lethal thing along each
    // cardinal direction. INFINITY if nothing lethal out there.
    // Proprioceptive — not cone-limited. The rider "feels" imminent
    // crashes in every direction without having to look.
    std::array<float, 4> lethal_distance{ {INFINITY_F, INFINITY_F, INFINITY_F, INFINITY_F} };

    // Distance to arena edge in each cardinal direction. Always
    // known (edges are self-illuminating; peripheral awareness).
    std::array<float, 4> arena_edge_distance{ {0, 0, 0, 0} };

    // Opponent rider. Populated only if inside the cone AND
    // unoccluded. Empty when the rider can't actually see them.
    std::optional<SeenPoint> opponent_rider;

    // Trail endpoints within the cone + LOS — useful for tactics
    // that want to pilot around specific wall ends. Empty in v1 if
    // we decide it's not needed; hook is here.
    std::vector<SeenPoint> visible_trail_endpoints;

    // The rider's own state this decision tick. Copied, not a
    // reference, so tactics can't mutate it via this struct.
    HeadState head;
    logotron::Cycle self;

    // Sentinel: our own INFINITY without dragging in <cmath> to the
    // header. The cpp defines the real value; this keeps tactic
    // includes tidy.
    static constexpr float INFINITY_F = 1e30f;
};

} // namespace logotron::ai
