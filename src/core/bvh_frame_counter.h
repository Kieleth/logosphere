#ifndef LOGOSPHERE_CORE_BVH_FRAME_COUNTER_H
#define LOGOSPHERE_CORE_BVH_FRAME_COUNTER_H

#include <cstdint>

// Tiny shared utility — a single global atomic counter that the main
// thread bumps whenever the spatial BVH is rebuilt. Worker threads in
// the lighting code use it to know when to refresh thread-local BVH
// snapshots.
//
// Lives in logosphere_core so callers like ParticleSystem (which is
// itself part of the larger logosphere library) can signal a rebuild
// without dragging in the entire LightingPrimitives translation unit
// — and so headless test harnesses that link logosphere_core can
// resolve the symbol without pulling rendering.

namespace logosphere {
namespace bvh_frame {

void increment();
std::uint64_t read();

}  // namespace bvh_frame
}  // namespace logosphere

#endif  // LOGOSPHERE_CORE_BVH_FRAME_COUNTER_H
