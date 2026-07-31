// Active-occluders snapshot — the shared Logotron answer to
// "what counts as a wall this frame?" Both the AI's perception
// layer (ai/perception.cpp ray_occluded) and the visual cone's
// LOS mask (hud/sight_occluders.cpp) consume the same list, so
// they always agree on which trails block sight.
//
// Filters applied (mirrors the gameplay-collision rules in
// arena.cpp's check_collision_detailed):
//   - Sealed TrailSegments whose owner Cycle is CRASHED are
//     dropped (matches §8 "dead duelist, dead wall").
//   - Sealed TrailSegments older than `kTrailLifetime` are
//     dropped (matches the visual lifetime fade in §7). Pass
//     `now_seconds = 0` to disable the age gate (used by AI
//     perception which doesn't currently track game-clock time).
//   - The caller's OWN active run is excluded — you can always
//     see past your own bike.
//   - Other cycles' active runs are included only while their
//     owner is RIDING.
//   - Degenerate (zero-length) active runs are dropped.
//
// Returns the list as the engine's generic `OccluderSegment`
// type so the visual cone can hand it straight to
// `logosphere::rendering::compute_vision_cone_occlusion` and the
// AI can iterate it for its own (cheaper, axis-aligned)
// ray-hit checks.

#pragma once

#include <vector>

#include "logosphere/kg/kg_module.h"
#include "logosphere/rendering/vision_cone_occlusion.h"

namespace logotron {

std::vector<logosphere::rendering::OccluderSegment>
build_active_occluders(const kg::KGModule& kg,
                       float now_seconds,
                       kg::EntityID self_cycle);

} // namespace logotron
