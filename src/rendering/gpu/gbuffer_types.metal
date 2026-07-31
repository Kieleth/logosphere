//
// gbuffer_types.metal
// Shared G-buffer structure definitions for deferred rendering
//
// Architecture: 3-pass deferred rendering
// 1. Pass 1: Rasterize geometry → G-buffer
// 2. Pass 2: Trace shadow rays → Shadow results
// 3. Pass 3: Apply lighting → Final framebuffer
//

#ifndef GBUFFER_TYPES_METAL
#define GBUFFER_TYPES_METAL

#include <metal_stdlib>
using namespace metal;

// G-Buffer pixel structure (36 bytes per pixel — reduced from 48)
// Stores surface data for visible pixels only
//
// MEMORY: 36 × 1.92M pixels × 2 slots = ~138 MB (was 48 × 3 = 276 MB)
// Removed 12-byte padding that existed only for 16-byte struct alignment.
// Metal buffers don't require power-of-2 struct sizes for correctness.
//
// CRITICAL: Use packed_float3 to avoid Metal's automatic 16-byte alignment
// Metal's float3 is padded to 16 bytes, but we need exactly 12 bytes
// See gpu_types.metal lines 11-18 for alignment rules
struct GBufferPixel {
    packed_float3 world_pos;  // 12 bytes - 3D world position of surface point
    packed_float3 normal;     // 12 bytes - surface normal (for lighting calculations)
    uchar4 base_color;        // 4 bytes - RGBA base color (before lighting)
    uint particle_id;         // 4 bytes - which particle (UINT_MAX = sky/background)
    // Total: 32 bytes (no padding). roughness removed 2026-07-24: its only
    // readers were the retired SSGI/BVH-indirect kernels (USE_SSGI=false,
    // USE_BVH_INDIRECT_GI=false); 36->32 B/px cuts G-buffer traffic ~11%
    // in every pass that touches it.
};

// Sentinel value for background/sky pixels
constant uint GBUFFER_SKY_ID = UINT_MAX;

// SSDO storage precision. MUST mirror Optimizations::SSDO_HALF_PRECISION
// (optimization_flags.h) — the A/B scripts flip both together.
// half4 halves SSDO I/O (trace write, 25-tap denoise ping-pong, apply
// read); math stays float32 in registers, only storage quantizes.
#define SSDO_HALF_PRECISION 1

// Penumbra blur id stream. MUST mirror Optimizations::PENUMBRA_COMPACT_IDS.
// The V-blur walks up to 65 rows per pixel; reading particle_id through the
// 32-byte GBufferPixel stride pulls a mostly-wasted cacheline per tap. The
// H-blur emits a packed uint id buffer as a free second output; the V-blur
// taps that instead. Same ids, same comparisons — bit-exact.
#define PENUMBRA_COMPACT_IDS 1

// Raster reject-path bbox stream. MUST mirror
// Optimizations::RASTER_BBOX_STREAM (optimization_flags.h).
#define RASTER_BBOX_STREAM 1

// Binning tile edge in pixels. MUST mirror
// Optimizations::GPU_BINNING_TILE_SIZE (optimization_flags.h) — a
// mismatch makes pixels walk the WRONG tile's triangle list. The kernel
// previously hardcoded 64 next to a "must match" comment; unified
// 2026-07-24 for the tile-size sweep.
#define GPU_BINNING_TILE_SIZE_METAL 64
#if SSDO_HALF_PRECISION
typedef half4 ssdo_store_t;
// Plain conversion. (A x1024 range shift and a device-const binding were
// tried against a phase-1 glow loss; both were coincidental — the real
// cause was frame-skip nondeterminism in the oracle, since fixed. The
// shift measured one +/-1 channel across four oracle frames: removed.)
inline ssdo_store_t ssdo_pack(float4 v)   { return ssdo_store_t(v); }
inline float4 ssdo_unpack(ssdo_store_t v) { return float4(v); }
#else
typedef float4 ssdo_store_t;
inline ssdo_store_t ssdo_pack(float4 v)   { return v; }
inline float4 ssdo_unpack(ssdo_store_t v) { return v; }
#endif

#endif // GBUFFER_TYPES_METAL
