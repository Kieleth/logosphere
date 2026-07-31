// Shared depth-buffer encoding for every GPU rasterization kernel.
//
// The depth scalar produced by CameraSystem::compute_depth is SIGNED —
// it's defined as monotone along the view direction, nothing more. For
// parallel projections (iso, bird's-eye, cabinet) it can be negative,
// especially when the camera sits near the scene plane (bike_viewer
// pattern: cam_z == scene_z).
//
// The atomic depth buffer MUST therefore be signed. A `uint` depth buffer
// silently destroys negative values (float→uint of a negative number is
// implementation-defined; in practice saturates to 0 or wraps to ~2^32),
// and that loss corrupts the ordering the rasterizer's compare-and-swap
// relies on. Result: a back-facing triangle wins a pixel that should
// show the front face, the gbuffer ends up with an outward normal
// opposite to what's visible, and Lambert clamps to 0 → black pixel.
//
// History: pre-2026-04-20 the depth metric was Euclidean distance, which
// is non-negative by construction, so uint silently worked. Once
// compute_depth became projection-aware (committing to orthographic
// view-direction depth for iso), the implicit uint contract broke.
// See tests/test_gbuffer_depth_encoding.cpp for the pinned failure.
//
// CONTRACT (must be in lockstep with logosphere/rendering/depth_encoding.h):
//   - Depth is stored as int32 (atomic_int).
//   - Encoding: int(depth_meters * DEPTH_ENCODE_SCALE).
//   - Smaller encoded value = closer to the viewer.
//   - Init value DEPTH_INIT_VALUE is greater than any real encoded depth.
//   - DEPTH_INIT_VALUE = 0x7F7F7F7F so MTLBlitCommandEncoder's fillBuffer
//     can initialize the whole buffer with a single byte (0x7F).
//     Equivalent to ~2.14e9, i.e., ~2.14e6 m of depth — way beyond any
//     plausible scene extent.
//   - Epsilon for depth-test tolerance (coplanar rounding) is a small
//     positive int added to the old depth on the "normal" code path.

#ifndef DEPTH_ENCODING_METAL
#define DEPTH_ENCODING_METAL

#include <metal_stdlib>
using namespace metal;

// Single-byte-fillable "infinity". Value bytes are 0x7F 0x7F 0x7F 0x7F.
// Any real depth after encode_depth is well below this.
constant int DEPTH_INIT_VALUE = 0x7F7F7F7F;

// World-depth to encoded-int scale. 1 m → 1000 encoded units.
constant float DEPTH_ENCODE_SCALE = 1000.0f;

// Tolerance added to the existing (winning) depth when comparing a new
// fragment — lets nearly-coplanar triangles both pass the test rather
// than one arbitrarily losing to float noise.
constant int DEPTH_EPSILON_ENCODED = 100;

// Encode a signed depth (meters) into a signed int32 suitable for the
// atomic<int> depth buffer. Negative inputs are preserved correctly
// because the cast is to signed int, not unsigned.
inline int encode_depth(float depth_meters) {
    return int(depth_meters * DEPTH_ENCODE_SCALE);
}

inline float decode_depth(int depth_encoded) {
    return float(depth_encoded) / DEPTH_ENCODE_SCALE;
}

#endif  // DEPTH_ENCODING_METAL
