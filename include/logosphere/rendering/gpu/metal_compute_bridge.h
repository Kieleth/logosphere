// metal_compute_bridge.h
// C++ interface to Metal GPU compute for shadow ray tracing
//
// PURPOSE: Bridge between C++ engine code and Metal GPU compute
// ARCHITECTURE: Thin wrapper over Metal API, minimal overhead
//
// USAGE:
//   MetalComputeBridge bridge;
//   if (bridge.initialize()) {
//       float result = bridge.trace_shadow_ray(ray, triangle);
//       // result = 0.0 (shadow) or 1.0 (lit)
//   }
//
// PHASE I MVP: Single shadow ray, single triangle
// FUTURE: Batch multiple rays for efficiency

#ifndef METAL_COMPUTE_BRIDGE_H
#define METAL_COMPUTE_BRIDGE_H

#include <cstddef>  // For size_t
#include <cstdint>  // For uint8_t, uint32_t
#include "optimization_flags.h"  // For GPU_BUFFER_SLOTS configuration

// Forward declarations (must be at global scope for Objective-C)
#ifdef __OBJC__
@class MTLDevice;
@class MTLComputePipelineState;
@class MTLCommandQueue;
@class MTLLibrary;
#else
typedef void MTLDevice;
typedef void MTLComputePipelineState;
typedef void MTLCommandQueue;
typedef void MTLLibrary;
#endif

namespace Logosphere {

// =========================================================================
// RAY AND TRIANGLE STRUCTURES (CPU side)
// =========================================================================
// Must match GPU layout in shadow_ray.metal (16-byte alignment!)
// These are POD types safe to memcpy to GPU buffers

struct alignas(16) ShadowRay {
    float origin[3];      // Ray origin in world space (surface pixel position)

    // =====================================================================
    // CRITICAL FIX (2025-10-06): Maximum ray distance = distance to light
    // =====================================================================
    //
    // BUG: "Mirror Shadow" - objects behind light cast shadows on wrong side
    //
    // ROOT CAUSE:
    // Shadow rays were testing for intersections INFINITELY along the ray
    // direction, instead of stopping at the light source. This caused rays
    // to continue PAST the light and hit objects on the opposite side,
    // creating impossible "mirror shadows" that shouldn't exist.
    //
    // OBSERVED BEHAVIOR:
    // - Eva at (0, -5, z) BEHIND light at (0, 0, 2)
    // - Wall at (0, 10, z) IN FRONT of light
    // - Shadow ray from wall: origin=(0,10,z), direction toward light (0,0,2)
    // - Ray hits light at t=10.05m (CORRECT - should stop here)
    // - WITHOUT max_distance: ray continues infinitely
    // - Ray hits Eva at t=15.2m (WRONG - Eva is PAST the light!)
    // - Result: Wall shows Eva's shadow (impossible - Eva is behind light!)
    //
    // VISUAL DIAGRAM:
    //
    //   Eva (behind)    Light       Wall (front)
    //       |             |              |
    //       o  ←--------- * ----------→  o
    //     t=15.2        t=10.05         t=0
    //     IGNORED!    STOP HERE!      ORIGIN
    //     (beyond     (light           (shadow ray
    //      light)      source)          starts here)
    //
    // "MIRROR SHADOW" SYMPTOMS:
    // 1. Shadows appear on WRONG side of light (north wall instead of south)
    // 2. Shadow SHRINKS as object moves farther behind light (inverse behavior)
    // 3. Object behind light casts shadow in front (physically impossible)
    //
    // THE FIX:
    // Store the actual distance from surface to light in max_distance.
    // GPU shader checks: MIN_T < t < max_distance (not just t > MIN_T)
    // Any intersection beyond max_distance is IGNORED (it's past the light).
    //
    // CALCULATION (in convert_to_gpu_rays):
    // max_distance = sqrt((light.x - pixel.x)² + (light.y - pixel.y)² + (light.z - pixel.z)²)
    //
    // This is PARAMETRIC and PER-RAY - no hardcoding, works for any geometry.
    //
    // IMPLEMENTATION:
    // - CPU: surface_rasterizer_single_pass.cpp:convert_to_gpu_rays() sets this
    // - GPU: shadow_ray.metal:ray_intersects_triangle() checks t < max_distance
    // =====================================================================
    float max_distance;

    float direction[3];   // Ray direction (MUST be normalized!)
    float _padding1;      // Explicit padding for 16-byte alignment

    // Constructor for convenience
    ShadowRay(float ox, float oy, float oz, float dx, float dy, float dz, float max_dist = 1000.0f)
        : origin{ox, oy, oz}, max_distance(max_dist),
          direction{dx, dy, dz}, _padding1(0.0f) {}

    ShadowRay() : origin{0,0,0}, max_distance(1000.0f), direction{0,0,0}, _padding1(0) {}
};

struct alignas(16) ShadowTriangle {
    float v0[3];  // Vertex 0
    float _padding0;
    float v1[3];  // Vertex 1
    float _padding1;
    float v2[3];  // Vertex 2
    float _padding2;
    uint8_t base_color[4]; // BGRA surface color for indirect ray hit color lookup
    float _padding3[3];    // Padding to 64 bytes (16-byte aligned)

    // Constructor for convenience
    ShadowTriangle(
        float v0x, float v0y, float v0z,
        float v1x, float v1y, float v1z,
        float v2x, float v2y, float v2z,
        uint8_t b = 0, uint8_t g = 0, uint8_t r = 0, uint8_t a = 255)
        : v0{v0x, v0y, v0z}, _padding0(0),
          v1{v1x, v1y, v1z}, _padding1(0),
          v2{v2x, v2y, v2z}, _padding2(0),
          base_color{b, g, r, a}, _padding3{0, 0, 0} {}

    ShadowTriangle()
        : v0{0,0,0}, _padding0(0),
          v1{0,0,0}, _padding1(0),
          v2{0,0,0}, _padding2(0),
          base_color{0, 0, 0, 255}, _padding3{0, 0, 0} {}
};

// =========================================================================
// LIGHTING COMPUTE STRUCTURES (CPU side)
// =========================================================================
// Must match GPU layout in lighting_compute.metal

struct alignas(16) PixelData {
    float position[3];  // World position
    float _padding0;
    float normal[3];    // Surface normal
    float _padding1;

    PixelData(float px, float py, float pz, float nx, float ny, float nz)
        : position{px, py, pz}, _padding0(0),
          normal{nx, ny, nz}, _padding1(0) {}

    PixelData()
        : position{0,0,0}, _padding0(0),
          normal{0,0,0}, _padding1(0) {}
};

struct alignas(16) LightData {
    float position[3];         // Light position
    float emission_strength;   // Lumens
    float emission_radius;     // Maximum distance
    float light_size;          // Physical size of light (derived from particle bounding box)
    float emission_color[3];   // RGB color (0-1 range, default white)
    float _padding;

    LightData(float x, float y, float z, float strength, float radius, float size = 0.0f,
              float r = 1.0f, float g = 1.0f, float b = 1.0f)
        : position{x, y, z}, emission_strength(strength),
          emission_radius(radius), light_size(size),
          emission_color{r, g, b}, _padding(0) {}

    LightData()
        : position{0,0,0}, emission_strength(0),
          emission_radius(0), light_size(0),
          emission_color{1,1,1}, _padding(0) {}
};

// =========================================================================
// PARTICLE TRANSFORM DATA (For local-coordinate pattern calculation)
// =========================================================================
// Must match GPU layout in gpu_types.metal:ParticleTransform

struct alignas(16) ParticleTransform {
    float center[3];        // Particle center in world space
    float _padding0;
    float primary_axis[3];  // Primary axis in world space
    float _padding1;

    ParticleTransform()
        : center{0, 0, 0}, _padding0(0),
          primary_axis{0, 0, 1}, _padding1(0) {}  // Default: Z-up

    ParticleTransform(float cx, float cy, float cz, float ax, float ay, float az)
        : center{cx, cy, cz}, _padding0(0),
          primary_axis{ax, ay, az}, _padding1(0) {}
};

// =========================================================================
// VISION CONE POST-PROCESS (Pass 4)
// =========================================================================
// Parameters for vision cone effect - darkens pixels outside viewer's FOV
// Must match GPU layout in gpu_types.metal:VisionConeParams

struct alignas(16) VisionConeParams {
    float viewer_x;        // Viewer world position X
    float viewer_y;        // Viewer world position Y
    float look_direction;  // Direction facing (radians, 0 = +Y/North)
    float half_fov;        // Half of field of view (radians)
    float range;           // Maximum vision distance (meters)
    float inner_falloff;   // Edge softness start (0.0-1.0 of half_fov)
    float darkness;        // Outside cone brightness (0.0=black, 1.0=visible)
    uint32_t enabled;      // 0 = disabled, 1 = enabled

    VisionConeParams()
        : viewer_x(0), viewer_y(0), look_direction(0),
          half_fov(0.785f),  // 45° = 90° FOV
          range(20.0f), inner_falloff(0.8f), darkness(0.1f), enabled(0) {}

    VisionConeParams(float x, float y, float dir, float fov_rad, float r)
        : viewer_x(x), viewer_y(y), look_direction(dir),
          half_fov(fov_rad / 2.0f), range(r),
          inner_falloff(0.8f), darkness(0.1f), enabled(1) {}
};

// =========================================================================
// METAL COMPUTE BRIDGE
// =========================================================================
// Manages Metal device, command queue, and compute pipeline
// Provides C++ interface to GPU shadow ray tracing
//
// IMPLEMENTATION NOTE:
// - Uses pImpl idiom to hide Metal types from C++ headers
// - All Metal API calls are in the .mm implementation file
// - This header can be safely included in pure C++ files

class MetalComputeBridge {
public:
    MetalComputeBridge();
    ~MetalComputeBridge();

    // Prevent copying (Metal resources are not copyable)
    MetalComputeBridge(const MetalComputeBridge&) = delete;
    MetalComputeBridge& operator=(const MetalComputeBridge&) = delete;

    // =====================================================================
    // INITIALIZATION
    // =====================================================================
    // Call once at startup to initialize Metal device and load shaders
    // Returns: true on success, false on failure
    //
    // FAILURE MODES:
    // - Metal not available (old macOS, VM, etc)
    // - default.metallib not found (build error)
    // - Shader compilation failed (Metal syntax error)
    // - GPU not available (no discrete GPU on this Mac)
    bool initialize();

    // Check if Metal compute is available and initialized
    bool is_initialized() const { return initialized_; }

    // =====================================================================
    // SHADOW RAY TRACING (Phase I MVP)
    // =====================================================================
    // Trace a single shadow ray against a single triangle on GPU
    //
    // INPUT:
    //   ray: Shadow ray to trace (origin + direction)
    //   triangle: Triangle to test against (3 vertices)
    //
    // RETURNS:
    //   0.0 = ray hits triangle (pixel is in shadow)
    //   1.0 = ray misses triangle (pixel is lit)
    //
    // PERFORMANCE:
    //   Phase I: Slower than CPU due to upload/download overhead
    //   Expected overhead: ~0.2-0.5ms for single ray
    //   Break-even point: ~500-1000 rays (Phase II)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    float trace_shadow_ray(const ShadowRay& ray, const ShadowTriangle& triangle);

    // =====================================================================
    // MULTI-TRIANGLE SHADOW RAY (Phase I-B)
    // =====================================================================
    // Test ray against multiple triangles using linear search (no BVH)
    //
    // INPUT:
    //   ray: Shadow ray to trace (origin + direction)
    //   triangles: Array of triangles to test against
    //   triangle_count: Number of triangles in array
    //
    // RETURNS:
    //   0.0 = ray hits any triangle (pixel is in shadow)
    //   1.0 = ray misses all triangles (pixel is lit)
    //
    // PERFORMANCE:
    //   O(N) linear search - educational baseline before BVH
    //   10 triangles: ~0.5ms
    //   100 triangles: ~5ms
    //   1000+ triangles: BVH required (Phase II)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    float trace_shadow_ray_multi(
        const ShadowRay& ray,
        const ShadowTriangle* triangles,
        int triangle_count);

    // =====================================================================
    // PARALLEL SHADOW RAYS (Phase I-C)
    // =====================================================================
    // Trace multiple rays in parallel - one GPU thread per ray
    //
    // INPUT:
    //   rays: Array of rays to trace (N rays)
    //   ray_count: Number of rays in array
    //   triangles: Array of triangles to test against (M triangles)
    //   triangle_count: Number of triangles in array
    //   results: Output array (must be pre-allocated, size = ray_count)
    //
    // OUTPUT:
    //   results[i] = 0.0 if rays[i] hits any triangle (shadow)
    //   results[i] = 1.0 if rays[i] misses all triangles (lit)
    //
    // PERFORMANCE:
    //   TRUE GPU PARALLELISM - all rays processed simultaneously!
    //   64 rays × 100 triangles = 6400 tests in ~5ms (not 64×5ms!)
    //   GPU can run 10,000+ threads in parallel
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void trace_shadow_rays_parallel(
        const ShadowRay* rays,
        int ray_count,
        const ShadowTriangle* triangles,
        int triangle_count,
        float* results);  // Output array (caller must allocate)

    // =====================================================================
    // BATCHED SHADOW RAYS (Phase II-A)
    // =====================================================================
    // Process multiple rays per GPU thread (8 rays/thread)
    // Foundation for BVH traversal (Phase II-B)
    //
    // INPUT:
    //   rays: Array of rays to trace (N rays)
    //   ray_count: Number of rays in array
    //   triangles: Array of triangles to test against (M triangles)
    //   triangle_count: Number of triangles in array
    //   results: Output array (must be pre-allocated, size = ray_count)
    //
    // OUTPUT:
    //   results[i] = 0.0 if rays[i] hits any triangle (shadow)
    //   results[i] = 1.0 if rays[i] misses all triangles (lit)
    //
    // DIFFERENCE FROM PARALLEL:
    //   Parallel: 1 ray per thread → N threads for N rays
    //   Batched: 8 rays per thread → N/8 threads for N rays
    //   More work per thread → better GPU utilization
    //
    // NOTE: This is Phase II-A (linear search, NO BVH yet)
    // Phase II-B will add BVH traversal to this kernel
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void trace_shadow_rays_batched(
        const ShadowRay* rays,
        int ray_count,
        const ShadowTriangle* triangles,
        int triangle_count,
        float* results);  // Output array (caller must allocate)

    // =====================================================================
    // BVH UPLOAD (Phase II-B)
    // =====================================================================
    // Upload triangle BVH to GPU for O(log N) traversal
    //
    // INPUT:
    //   bvh_nodes: Array of BVH nodes (from TriangleBVH::get_nodes())
    //   node_count: Number of nodes in BVH
    //
    // PERFORMANCE:
    //   Upload once when triangles change (dirty flag pattern)
    //   ~140 KB for 1464 triangles (2900 nodes × 48 bytes)
    //   Persistent buffer reused across frames
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void upload_bvh(const void* bvh_nodes, int node_count);

    // =====================================================================
    // BATCHED SHADOW RAYS WITH BVH (Phase II-B)
    // =====================================================================
    // Process multiple rays per GPU thread using BVH acceleration
    //
    // INPUT:
    //   rays: Array of rays to trace (N rays)
    //   ray_count: Number of rays in array
    //   triangles: Array of triangles to test against (M triangles)
    //   triangle_count: Number of triangles in array
    //   results: Output array (must be pre-allocated, size = ray_count)
    //
    // OUTPUT:
    //   results[i] = 0.0 if rays[i] hits any triangle (shadow)
    //   results[i] = 1.0 if rays[i] misses all triangles (lit)
    //
    // PERFORMANCE:
    //   O(log N) BVH traversal instead of O(N) linear search
    //   1464 triangles: ~11 nodes/ray vs 1464 tests/ray (122× reduction)
    //   Expected: 100+ FPS (1 light), 50+ FPS (2 lights)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void trace_shadow_rays_batched_bvh(
        const ShadowRay* rays,
        int ray_count,
        const ShadowTriangle* triangles,
        int triangle_count,
        float* results);  // Output array (caller must allocate)

    // =====================================================================
    // GPU LIGHTING COMPUTE (Option B)
    // =====================================================================
    // Compute lighting for pixels using GPU (distance²/sqrt/dot/intensity)
    //
    // INPUT:
    //   pixels: Array of pixel data (position + normal)
    //   pixel_count: Number of pixels
    //   lights: Array of light data (position + strength + radius)
    //   light_count: Number of lights
    //   shadow_results: Shadow ray results (0.0=blocked, 1.0=clear)
    //   lighting_output: Output array (must be pre-allocated, size = pixel_count)
    //
    // OUTPUT:
    //   lighting_output[i] = accumulated lighting value for pixel i
    //
    // PERFORMANCE:
    //   One GPU thread per pixel, processes all lights
    //   Expected: 5-16% FPS gain (per GPU_RENDERING_better_cpu.md)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void compute_lighting(
        const PixelData* pixels,
        int pixel_count,
        const LightData* lights,
        int light_count,
        const float* shadow_results,
        float* lighting_output);

    // =====================================================================
    // ASYNC GPU EXECUTION (Phase III: Triple-Buffered CPU/GPU Overlap)
    // =====================================================================
    // Asynchronous version: GPU processes rays while CPU continues working
    // Completion callback runs when GPU finishes (on background thread!)
    //
    // USAGE:
    //   bridge.trace_shadow_rays_batched_bvh_async(rays, count, triangles, tri_count,
    //       [](const float* results, int result_count, void* user_data) {
    //           // Apply lighting here (runs on background thread)
    //           // Or dispatch to main thread if needed
    //       }, user_data);
    //
    // THREADING: Callback runs on Metal's completion thread (NOT main thread!)
    //            Use dispatch_async(dispatch_get_main_queue(), ...) if needed
    //
    // PERFORMANCE: +10-30% FPS from CPU/GPU overlap
    //              GPU works on Frame N while CPU collects Frame N+1 rays
    //
    using CompletionCallback = void (*)(const float* results, int result_count, void* user_data);

    void trace_shadow_rays_batched_bvh_async(
        const ShadowRay* rays,
        int ray_count,
        const ShadowTriangle* triangles,
        int triangle_count,
        CompletionCallback callback,
        void* user_data);  // Opaque pointer passed to callback

private:
    // Metal resources (opaque pointers in C++, actual types in .mm file)
    void* device_;           // id<MTLDevice>
    void* command_queue_;    // id<MTLCommandQueue> (for shadow rays)
    void* lighting_queue_;   // id<MTLCommandQueue> (for lighting - separate to avoid deadlock)
    void* compute_pipeline_; // id<MTLComputePipelineState> (for trace_shadow_ray)
    void* library_;          // id<MTLLibrary>

    // Persistent GPU buffers (Phase II-B)
    void* bvh_buffer_;              // id<MTLBuffer> for BVH nodes (persistent)
    void* bvh_compute_pipeline_;    // id<MTLComputePipelineState> for BVH kernel (persistent)

    // Lighting compute buffers (Option B)
    void* lighting_compute_pipeline_;  // id<MTLComputePipelineState> for lighting kernel (persistent)
    void* pixels_buffer_;              // id<MTLBuffer> for pixel data (persistent, resizable)
    void* lights_buffer_;              // id<MTLBuffer> for light data (persistent, resizable)
    void* lighting_output_buffer_;     // id<MTLBuffer> for lighting results (persistent, resizable)
    size_t pixels_buffer_capacity_;
    size_t lights_buffer_capacity_;
    size_t lighting_output_buffer_capacity_;

    // Multi-buffered GPU buffers (async execution with CPU/GPU overlap)
    // Configurable buffer count (GPU_BUFFER_SLOTS) - typically 3-5 buffers
    // More buffers = less CPU blocking when GPU falls behind, but more memory
    void* rays_buffer_[Optimizations::GPU_BUFFER_SLOTS];
    void* triangles_buffer_[Optimizations::GPU_BUFFER_SLOTS];
    void* results_buffer_[Optimizations::GPU_BUFFER_SLOTS];
    void* raycount_buffer_;

    // Buffer capacity tracking
    size_t rays_buffer_capacity_[Optimizations::GPU_BUFFER_SLOTS];
    size_t triangles_buffer_capacity_[Optimizations::GPU_BUFFER_SLOTS];
    size_t results_buffer_capacity_[Optimizations::GPU_BUFFER_SLOTS];

    // Multi-buffering state (async execution)
    void* buffer_semaphore_;        // dispatch_semaphore_t (tracks available buffers)
    int current_buffer_index_;      // Ring buffer rotation: 0..GPU_BUFFER_SLOTS-1

    // Initialization state
    bool initialized_;

    // =====================================================================
    // INTERNAL HELPERS (implemented in .mm file)
    // =====================================================================
    bool load_shader_library();
    bool create_compute_pipeline();

public:
    // Wait for all pending GPU work to complete (for safe shutdown)
    void wait_for_completion();
};

} // namespace Logosphere

#endif // METAL_COMPUTE_BRIDGE_H
