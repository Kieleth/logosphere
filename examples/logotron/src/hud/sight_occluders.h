// CPU prep for the engine's vision-cone LOS occlusion mask.
//
// Each frame, the game raycasts kBins rays across the viewer's cone
// and writes the distance-to-nearest-occluder per bin into the
// engine's vision-cone params via Engine::set_vision_cone_occlusion.
// The shader (vision_cone_postprocess.metal) then darkens any pixel
// behind a bin's distance — so the visual cone matches what the AI
// already sees through its perception layer (LOS-occluded), instead
// of being a pure angle+range dimmer.
//
// Sealed TrailSegments + opponents' active runs all occlude. The
// caller's OWN active run is excluded (you can always see past your
// own bike). Trails owned by a CRASHED cycle and trails older than
// kTrailLifetime are excluded too — the visual fade and the gameplay
// collision both stop seeing them, so the cone must as well, or the
// player ends up "blind" through dead/expired walls. Mirrors the
// gameplay rules in arena.cpp's check_collision_detailed.

#pragma once

#include "logosphere/kg/kg_module.h"

namespace logotron::hud {

// Bin count must match GPURasterizer::kVisionConeOcclusionBins.
// Pinned here so the game can size its local array without pulling
// the engine header transitively.
constexpr int kVisionConeBins = 256;

// Fill `out_distances[kVisionConeBins]` with the nearest occluder
// distance per angular bin, sweeping the cone from
// (look_direction - half_fov) to (look_direction + half_fov).
// Bins with no hit get `range` (the cone's max draw distance).
//
// Engine convention: look_direction = 0 means looking toward +Y.
// half_fov is half the total FOV.
//
// `now_seconds` is the game clock; pass elapsed-since-launch.
// Drives both filters above (CRASHED owner exclusion, age >
// kTrailLifetime exclusion). Pass 0 to disable the age gate.
void compute_vision_cone_occlusion(
    const kg::KGModule& kg,
    kg::EntityID self_cycle,
    float viewer_x, float viewer_y,
    float look_direction,
    float half_fov,
    float range,
    float now_seconds,
    float out_distances[kVisionConeBins]);

} // namespace logotron::hud
