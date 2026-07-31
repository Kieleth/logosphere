#ifndef SHADOW_SAMPLING_CONFIG_H
#define SHADOW_SAMPLING_CONFIG_H

// Rendering-only shadow sampling configuration.
//
// Kept separate from optimization_flags.h so the headless profile does
// not transitively pull in the rendering strategy templates (which
// include BVH + lighting primitives).

#include "logosphere/rendering/shadow_sampling_strategy.h"

namespace Optimizations {

// Active pattern (compile-time switch — zero runtime overhead).
constexpr ShadowSampling::Pattern SHADOW_SAMPLING_PATTERN =
    ShadowSampling::Pattern::SCANLINE_1D;

namespace Scanline1D {
    constexpr int   SEGMENT_SIZE          = 21;
    constexpr float SHADOW_DIFF_THRESHOLD = 0.1f;
}

namespace Hierarchical2D {
    constexpr float UNIFORMITY_THRESHOLD = 0.1f;
}

}  // namespace Optimizations

#endif  // SHADOW_SAMPLING_CONFIG_H
