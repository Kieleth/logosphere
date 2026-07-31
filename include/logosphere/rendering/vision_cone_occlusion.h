// Vision-cone LOS occlusion mask generator.
//
// Pure-math primitive, no GPU, no KG. The host (game) provides a
// flat list of line-segment occluders; this function returns one
// distance-to-nearest-occluder per angular bin across the cone.
// The companion `set_vision_cone_occlusion` engine API uploads
// that array to the Pass-4 vision-cone Metal kernel, which then
// darkens any pixel beyond its bin's distance — making the visual
// cone agree with the geometry it's supposed to be looking at.
//
// Rationale (and why it lives at engine level): every game using
// the vision-cone post-process eventually needs LOS, and the
// raycast math is identical regardless of game (Logotron trails,
// Logomancers walls, Eden hedges). The game owns "what counts as
// an occluder"; the engine owns "given a list of segments, fill
// the per-bin distances."
//
// Engine convention: `look_direction = 0` means looking toward +Y
// (engine yaw=0). Angles use atan2(dx, dy) consistent with
// Particle::rotation_z (CW around +Z viewed from above; see
// docs/ARCHITECTURE.md axis section).

#pragma once

namespace logosphere::rendering {

// One occluder. Endpoints in world meters. The segment is treated
// as a rectangle (oriented bounding box) of length |b - a| and
// width 2 * half_thick. Axis-aligned segments are not required —
// arbitrary angles work via the OBB form, so this scales to
// continuous-rotation games (Logotron Phase F).
struct OccluderSegment {
    float ax, ay;
    float bx, by;
    float half_thick;
};

// Compute the per-bin nearest-occluder distance for a vision cone.
//
// Sweeps `bin_count` rays from `(viewer_x, viewer_y)` across the
// cone arc [look_direction - half_fov, look_direction + half_fov].
// For each bin, finds the smallest ray-vs-occluder hit distance
// across all `segments`. Bins with no hit get `range`.
//
// Cost: O(bin_count * segment_count). 64 × 30 = ~2k ops/frame; cheap.
//
// `out_distances` must point to at least `bin_count` floats. Caller
// is responsible for pre-filtering segments (e.g. excluding the
// player's own active run).
void compute_vision_cone_occlusion(
    const OccluderSegment* segments, int segment_count,
    float viewer_x, float viewer_y,
    float look_direction, float half_fov, float range,
    float* out_distances, int bin_count);

} // namespace logosphere::rendering
