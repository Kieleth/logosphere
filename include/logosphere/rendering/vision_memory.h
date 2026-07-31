// Vision-memory grid — world-space "what the viewer just saw" buffer.
//
// Companion to the vision-cone Pass-4 post-process. Each frame the
// host calls `update_vision_memory` to decay every cell and re-mark
// the cells the cone is currently illuminating (cone + range +
// optional LOS occlusion mask). The shader then samples this
// buffer per pixel and blends the cell's value with the live cone
// visibility, so pixels that just left the cone stay dimly lit
// (decaying over `decay_seconds`) instead of snapping to black.
//
// Pure CPU math, no Metal, no KG. Engine-generic — any game using
// `set_vision_cone(...)` opts in via `Engine::set_vision_memory_*`.
//
// Engine convention: `look_direction = 0` means +Y (engine yaw=0).
// `atan2(dx, dy)` is the bearing from viewer to world point. Same
// math as `compute_vision_cone_occlusion`.

#pragma once

namespace logosphere::rendering {

struct VisionMemoryConfig {
    int   width;          // grid columns (cells along world X)
    int   height;         // grid rows    (cells along world Y)
    float origin_x;       // world coord of cell (0, 0)'s SW corner
    float origin_y;
    float cell_size;      // world meters per cell side
    float decay_seconds;  // time for a fully-lit cell to reach 0
};

// Decay every cell by `dt / decay_seconds`, then mark every cell
// whose center is currently illuminated by the cone (in cone arc,
// within range, not occluded by the optional occlusion mask) to
// 1.0. Cells outside the cone or occluded are left to decay.
//
// `memory` is a flat array of `cfg.width * cfg.height` floats in
// row-major order — `memory[y * cfg.width + x]`.
//
// `occlusion_distances` is the same array the cone shader reads:
// `bin_count` floats spanning the cone arc from -half_fov to
// +half_fov. Pass `nullptr` (and `bin_count = 0`) to disable the
// LOS check; cells inside the cone arc + range get marked
// regardless of geometry.
//
// `dt`: seconds since the last call. Negative or zero `dt` skips
// the decay step but still re-marks visible cells (useful for
// "instantly seed memory" calls).
void update_vision_memory(
    float* memory,
    const VisionMemoryConfig& cfg,
    float viewer_x, float viewer_y,
    float look_direction, float half_fov, float range,
    const float* occlusion_distances, int bin_count,
    float dt);

} // namespace logosphere::rendering
