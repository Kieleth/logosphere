#include "hud/sight_occluders.h"

#include "occluders.h"
#include "logosphere/rendering/vision_cone_occlusion.h"

namespace logotron::hud {

void compute_vision_cone_occlusion(
    const kg::KGModule& kg,
    kg::EntityID self_cycle,
    float viewer_x, float viewer_y,
    float look_direction,
    float half_fov,
    float range,
    float now_seconds,
    float out_distances[kVisionConeBins])
{
    // Single source of truth for "what's a wall this frame" lives
    // in logotron::build_active_occluders — same list also drives
    // the AI's perception layer (ai/perception.cpp ray_occluded),
    // so the visual cone and the AI tactics never disagree about
    // which trails block sight. The engine's pure-math helper
    // does the per-bin raycasting.
    auto occluders = logotron::build_active_occluders(kg, now_seconds, self_cycle);
    logosphere::rendering::compute_vision_cone_occlusion(
        occluders.data(), static_cast<int>(occluders.size()),
        viewer_x, viewer_y,
        look_direction, half_fov, range,
        out_distances, kVisionConeBins);
}

} // namespace logotron::hud
