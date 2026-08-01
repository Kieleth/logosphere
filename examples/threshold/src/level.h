// World-unit X-axis column layout for the threshold level.
//
// Mirrors examples/pop/src/level_one.h's column table 1:1 as meters,
// translated onto the engine's real coordinate system: X is the axis the
// player moves along, Y is a fixed "depth" the camera looks down (see
// threshold_app.cpp), Z is up (gravity, jump).
#pragma once

namespace threshold {

constexpr float kFixedDepthY = 0.0f;   // every actor/tile sits at this Y

constexpr float kPrinceStartX = 1.0f;
constexpr float kGapMinX      = 4.0f;   // gap: no floor, spikes below
constexpr float kGapMaxX      = 5.0f;
constexpr float kLooseTileX   = 9.0f;   // collapsing floor tile
constexpr float kGuardStartX  = 27.0f;
constexpr float kExitX        = 30.0f;

constexpr float kTileSize = 1.0f;       // meters per StrataFloorGenerator tile

// Bedrock layer thickness (must match build_level's bedrock StrataLayerSpec
// in scene.cpp). The loose tile spawned separately from the strata floor
// (spawn_loose_tile) needs this to sit its top flush with the walkway
// surface -- StrataFloorGenerator stacks layers bottom-up from z=0, so
// walkway tiles' centers sit at kBedrockThickness + walkway_thickness/2,
// not at walkway_thickness/2 the way a standalone create_floor_grid() tile
// defaults to.
constexpr float kBedrockThickness = 0.30f;

} // namespace threshold
