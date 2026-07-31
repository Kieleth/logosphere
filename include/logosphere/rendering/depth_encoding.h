#ifndef LOGOSPHERE_RENDERING_DEPTH_ENCODING_H
#define LOGOSPHERE_RENDERING_DEPTH_ENCODING_H

// C++ mirror of src/rendering/gpu/depth_encoding.metal.
//
// The depth scalar from CameraSystem::compute_depth can be negative for
// parallel projections (iso, bird's-eye, cabinet) when the camera sits
// near the scene. The shared depth buffer is therefore stored as SIGNED
// int32 — unsigned storage would silently lose the ordering of
// negative-depth pixels and produce the "bike body dark under cardinal
// light" class of bug (see CHANGELOG for the 2026-04-20 fix and
// tests/test_gbuffer_depth_encoding.cpp for the contract).
//
// When changing any constant in this file, change the matching constant
// in depth_encoding.metal in the SAME commit. Drift here silently
// miscolors pixels.

#include <cstdint>

namespace logosphere::rendering {

// Single-byte-fillable "infinity". The buffer is initialized with the
// Metal blit encoder's fillBuffer:value:0x7F, which writes 0x7F to
// every byte → 0x7F7F7F7F as int32 (~2.14e9 ≈ 2.14e6 m of depth).
// Any real scene's encoded depth is safely below this.
inline constexpr int32_t kDepthInitValue   = 0x7F7F7F7F;

// World-depth to encoded-int scale. 1 m → 1000 encoded units.
inline constexpr float   kDepthEncodeScale = 1000.0f;

// Tolerance added to the existing winning depth when testing a new
// fragment. Lets coplanar triangles both pass the atomic compare-and-set.
inline constexpr int32_t kDepthEpsilonEnc  = 100;

// Encode a signed depth (meters) into a signed int32. Negative inputs
// survive because the cast is to int32_t, not uint32_t. The atomic
// compare-exchange in the shader will order these correctly.
inline int32_t encode_depth(float depth_meters) {
    return static_cast<int32_t>(depth_meters * kDepthEncodeScale);
}

inline float decode_depth(int32_t depth_encoded) {
    return static_cast<float>(depth_encoded) / kDepthEncodeScale;
}

}  // namespace logosphere::rendering

#endif  // LOGOSPHERE_RENDERING_DEPTH_ENCODING_H
