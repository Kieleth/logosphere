#ifndef LOGOSPHERE_RENDERING_PENUMBRA_MATH_H
#define LOGOSPHERE_RENDERING_PENUMBRA_MATH_H

// Soft-shadow penumbra width — the C++ canonical mirror of the
// formula in src/rendering/gpu/penumbra.metal (kernel C, the
// "blocker analysis" path). Kept in a header so headless tests can
// pin the contract without a Metal toolchain.
//
// THE FORMULA (currently exact):
//
//     penumbra_width_pixels = blocker_distance_meters
//                           * light_size_meters
//                           * BLOCKER_PENUMBRA_SCALE
//
// Returned value is consumed by the post-process kernel as a PIXEL
// search radius (clamped to [1, BLOCKER_SEARCH_RADIUS]). The mixed
// units (meters × meters × scalar → pixels) are inherited from the
// shader; documenting them here so any test can express the constraint
// the rendering pipeline already (silently) assumes.
//
// Any change to the formula MUST be made in BOTH this header AND
// penumbra.metal — they are kept lockstep on purpose. A future fix
// might thread pixels-per-meter through so this becomes dimensionally
// consistent; the test will then update with it.

namespace logosphere::rendering {

// Mirror of penumbra.metal:34. Reverted from 80 → 2 because the blur
// kernel that consumes this value (penumbra.metal:271-297) has NO
// edge-awareness — at large penumbra widths it bleeds shadow into
// adjacent lit pixels, blackening surfaces facing the light. The
// architectural fix is edge-aware blur or true PCSS; until that lands
// scale=2 is the only safe value, even though it makes shadows hard.
inline constexpr float kBlockerPenumbraScale = 2.0f;

// Mirror of penumbra.metal:33. Pixels with center_blocker below this
// distance (in meters) are treated as "lit" — used to gate which
// pixels feed the penumbra computation.
inline constexpr float kBlockerMinDistance = 0.01f;

// Mirror of penumbra.metal:35. Hard cap on the search radius the
// shader applies AFTER the formula. So the effective penumbra width
// in pixels is clamp(formula(...), 1, kBlockerSearchRadius).
inline constexpr int kBlockerSearchRadius = 32;

// The raw, unclamped formula — same as line 201 / 224 / 240 / 315 of
// penumbra.metal. blocker_distance and light_size are in meters; the
// caller treats the return value as pixels.
inline float compute_penumbra_width_raw(float blocker_distance_m,
                                        float light_size_m) {
    return blocker_distance_m * light_size_m * kBlockerPenumbraScale;
}

// What the shader actually uses as a pixel search radius after the
// final clamp. Mirrors penumbra.metal:259.
inline int effective_search_radius_px(float blocker_distance_m,
                                      float light_size_m) {
    float pw = compute_penumbra_width_raw(blocker_distance_m, light_size_m);
    int r = static_cast<int>(pw + 0.5f);
    if (r < 1) r = 1;
    if (r > kBlockerSearchRadius) r = kBlockerSearchRadius;
    return r;
}

}  // namespace logosphere::rendering

#endif  // LOGOSPHERE_RENDERING_PENUMBRA_MATH_H
