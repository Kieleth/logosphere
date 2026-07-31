#include "logosphere/rendering/vision_cone_pixel.h"

#include <algorithm>
#include <cmath>

namespace logosphere::rendering {

namespace {

// Apex offset matches APEX_OFFSET in vision_cone_postprocess.metal.
// The cone's geometric apex is shifted 30 cm BEHIND the viewer so
// the rider's body falls inside the cone.
constexpr float kApexOffset = 0.3f;
// Bias applied to the LOS occlusion compare — 30 cm apex + 40 cm
// wall thickness slack. Mirrors OCC_BIAS in the shader.
constexpr float kOcclusionBias = kApexOffset + 0.4f;

float wrap_pi(float a) {
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    while (a >  kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

} // namespace

float compute_vision_cone_visibility(
    const VisionConePixelParams& params,
    const VisionConePixelInput& pixel)
{
    // Sky pixels: shader's early branch — visibility = darkness.
    if (pixel.particle_id == kVisionConeSkyId) return params.darkness;

    // Same apex shift as the shader.
    const float ox = params.viewer_x - std::sin(params.look_direction) * kApexOffset;
    const float oy = params.viewer_y - std::cos(params.look_direction) * kApexOffset;
    const float dx = pixel.world_x - ox;
    const float dy = pixel.world_y - oy;
    const float distance = std::sqrt(dx * dx + dy * dy);

    float visibility = 1.0f;

    if (distance > params.range) {
        visibility = params.darkness;
    } else {
        const float angle_to_pixel = std::atan2(dx, dy);
        const float angle_diff     = wrap_pi(angle_to_pixel - params.look_direction);
        const float abs_angle      = std::fabs(angle_diff);

        const float falloff_start = params.half_fov * params.inner_falloff;
        if (abs_angle <= falloff_start) {
            visibility = 1.0f;
        } else if (abs_angle <= params.half_fov) {
            const float falloff_range = params.half_fov - falloff_start;
            const float t = (abs_angle - falloff_start) / falloff_range;
            visibility = 1.0f * (1.0f - t * t) + params.darkness * (t * t);
        } else {
            visibility = params.darkness;
        }

        // Range falloff (same shape as shader).
        const float range_falloff_start = params.range * 0.8f;
        if (distance > range_falloff_start) {
            const float range_t = (distance - range_falloff_start)
                                / (params.range - range_falloff_start);
            visibility *= (1.0f * (1.0f - range_t) + params.darkness * range_t);
        }

        // LOS occlusion (Pass 4b).
        if (params.occlusion_count > 0 && abs_angle <= params.half_fov &&
            params.occlusion_distance) {
            float t_bin = (angle_diff + params.half_fov)
                        / (2.0f * params.half_fov);
            int   bin   = static_cast<int>(t_bin * static_cast<float>(params.occlusion_count));
            if (bin < 0) bin = 0;
            if (bin >= params.occlusion_count) bin = params.occlusion_count - 1;
            const float occ_dist = params.occlusion_distance[bin];
            if (distance > occ_dist + kOcclusionBias) {
                visibility = params.darkness;
            }
        }
    }

    // Vision-memory blend. Skipped for dynamic pixels — bikes etc.
    // shouldn't ghost into cells the player previously looked at.
    if (params.memory_enabled != 0 && params.memory_width > 0 &&
        params.memory_height > 0 && params.memory_grid != nullptr) {
        bool pixel_is_dynamic = false;
        if (params.dynamic_map_size > 0 &&
            params.is_dynamic_map != nullptr &&
            static_cast<int>(pixel.particle_id) >= 0 &&
            pixel.particle_id < static_cast<unsigned int>(params.dynamic_map_size)) {
            pixel_is_dynamic =
                (params.is_dynamic_map[pixel.particle_id] != 0);
        }
        if (!pixel_is_dynamic) {
            const int cx = static_cast<int>(
                (pixel.world_x - params.memory_origin_x) / params.memory_cell_size);
            const int cy = static_cast<int>(
                (pixel.world_y - params.memory_origin_y) / params.memory_cell_size);
            if (cx >= 0 && cx < params.memory_width &&
                cy >= 0 && cy < params.memory_height) {
                const float mem = params.memory_grid[cy * params.memory_width + cx];
                const float mem_visibility = params.memory_dim * mem;
                if (mem_visibility > visibility) visibility = mem_visibility;
            }
        }
    }

    return visibility;
}

} // namespace logosphere::rendering
