#include "logosphere/rendering/vision_memory.h"

#include <cmath>

namespace logosphere::rendering {

namespace {

// Wrap angle to (-π, π] — same as the shader's normalization in
// vision_cone_postprocess.metal so per-cell binning matches what
// the GPU does per pixel.
float wrap_pi(float a) {
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    while (a >  kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

} // namespace

void update_vision_memory(
    float* memory,
    const VisionMemoryConfig& cfg,
    float viewer_x, float viewer_y,
    float look_direction, float half_fov, float range,
    const float* occlusion_distances, int bin_count,
    float dt)
{
    if (!memory || cfg.width <= 0 || cfg.height <= 0) return;

    // Decay step. Linear: `decay = dt / decay_seconds` per cell.
    // A cell that hits the cone gets snapped to 1.0 below, which
    // overwrites this decay — the order is `decay then mark`, the
    // same shape every fog-of-war system uses.
    if (dt > 0.0f && cfg.decay_seconds > 0.0f) {
        const float dec = dt / cfg.decay_seconds;
        const int   n   = cfg.width * cfg.height;
        for (int i = 0; i < n; ++i) {
            float v = memory[i] - dec;
            memory[i] = (v < 0.0f) ? 0.0f : v;
        }
    }

    if (cfg.cell_size <= 0.0f || range <= 0.0f) return;

    // Mark step. Walk every cell, compute its center, run the same
    // visibility test the shader runs per pixel (cone + range +
    // optional LOS occlusion). Cost: O(width * height) — for the
    // expected 256 × 256 grid, ~65k cells per frame, < 1 ms on the
    // CPU.
    const float two_half_fov = 2.0f * half_fov;
    const float inv_two_half_fov = (two_half_fov > 0.0f) ? (1.0f / two_half_fov) : 0.0f;

    for (int j = 0; j < cfg.height; ++j) {
        const float wy = cfg.origin_y + (static_cast<float>(j) + 0.5f) * cfg.cell_size;
        for (int i = 0; i < cfg.width; ++i) {
            const float wx = cfg.origin_x + (static_cast<float>(i) + 0.5f) * cfg.cell_size;

            const float dx = wx - viewer_x;
            const float dy = wy - viewer_y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > range) continue;       // beyond cone range
            if (dist < 1e-4f) {               // viewer's own cell — always lit
                memory[j * cfg.width + i] = 1.0f;
                continue;
            }

            // Engine convention: yaw=0 → +Y. atan2(dx, dy) gives
            // the bearing from viewer to cell center.
            const float ang_to_cell = std::atan2(dx, dy);
            const float ang_diff    = wrap_pi(ang_to_cell - look_direction);
            if (std::fabs(ang_diff) > half_fov) continue;   // outside cone arc

            // LOS occlusion (optional). Cell is occluded iff the
            // bin's nearest-occluder distance is less than the
            // cell's distance from the viewer.
            if (occlusion_distances && bin_count > 0) {
                float t = (ang_diff + half_fov) * inv_two_half_fov;
                int   b = static_cast<int>(t * static_cast<float>(bin_count));
                if (b < 0) b = 0;
                if (b >= bin_count) b = bin_count - 1;
                if (dist > occlusion_distances[b]) continue;
            }

            // Cell is currently illuminated — refresh memory to 1.
            memory[j * cfg.width + i] = 1.0f;
        }
    }
}

} // namespace logosphere::rendering
