// gpu_rasterizer.h
// C++ interface to Metal GPU triangle rasterization
//
// PURPOSE: Bridge between C++ render pipeline and Metal GPU rasterization
// ARCHITECTURE: Thin wrapper over Metal API, follows MetalComputeBridge pattern
//
// USAGE:
//   GPURasterizer rasterizer;
//   if (rasterizer.initialize(width, height)) {
//       rasterizer.rasterize_minimal(framebuffer);
//   }
//
// PHASE III: Full GPU Rasterization
// STEP 1: Minimal kernel (1 pixel, hardcoded)
// Future: Full triangle → Barycentric → Depth → Multiple triangles → Lighting

#ifndef GPU_RASTERIZER_H
#define GPU_RASTERIZER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "optimization_flags.h"  // For GPU_BUFFER_SLOTS configuration

// Forward declarations for C++ types
struct Surface;
class CameraSystem;

// Forward declarations for Objective-C types
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
// GPU RASTERIZER
// =========================================================================
// Manages Metal device and compute pipeline for GPU rasterization
// Provides C++ interface to GPU triangle rendering
//
// IMPLEMENTATION:
// - Uses pImpl idiom to hide Metal types from C++ headers
// - All Metal API calls in .mm implementation file
// - Safe to include in pure C++ files

// =============================================================================
// Shadow acceleration backend — the portability seam
// =============================================================================
// Shadow rays need a spatial structure to trace against. There are two ways to
// supply one, and which is live decides whether the CPU-side BVHs are worth
// building at all:
//
//   HardwareRT   The driver builds and owns the structure (Metal RT's
//                MTLAccelerationStructure today; DXR or Vulkan RT for a port).
//                The shader traces it with an intersector. The CPU BVHs are
//                NOT read and must not be built.
//
//   SoftwareBVH  The engine builds a TriangleBVH (and an EntityBVH for
//                directional culling) on the CPU, uploads them, and the shader
//                walks them itself. Portable in PRINCIPLE to any GPU with
//                compute.
//
// *** THE SoftwareBVH PATH IS CURRENTLY BROKEN: IT RENDERS NO LIGHTING. ***
// Its output is byte-identical to the same scene with the lights off. The path
// builds its trees and dispatches its kernel and produces nothing. No
// supported target hits it (every one has Metal RT), which is how it stayed
// broken unnoticed. A port to hardware without ray tracing MUST fix it first;
// see docs/PORTING_SHADOWS.md, and test_shadow_accel_backend which reports the
// defect on demand.
//
// A port that wires up DXR or Vulkan RT implements it behind HardwareRT and the
// CPU trees go dormant automatically, with no render-pipeline changes. That is
// what this seam is for.
//
// Measured cost of getting this wrong: on M3/M4 the engine built and uploaded
// both CPU trees every frame while the hardware structure did the actual
// tracing — 2.16 ms of a 21.7 ms Eden frame, and up to 97 ms on a single frame
// in a spawning scene.
enum class ShadowAccelBackend {
    HardwareRT,
    SoftwareBVH,
};

const char* to_string(ShadowAccelBackend b);

// Force a backend regardless of hardware, for tests and for bringing up a port.
// LOGOSPHERE_SHADOW_ACCEL=hardware|software does the same at startup. Forcing
// HardwareRT on a device without support is ignored: capability still wins.
void set_forced_shadow_accel_backend(const char* name_or_null);

class GPURasterizer {
public:
    GPURasterizer();
    ~GPURasterizer();

    // Prevent copying (Metal resources are not copyable)
    GPURasterizer(const GPURasterizer&) = delete;
    GPURasterizer& operator=(const GPURasterizer&) = delete;

    // =====================================================================
    // INITIALIZATION
    // =====================================================================
    // Initialize Metal device and load rasterization shaders
    //
    // INPUT:
    //   width: Framebuffer width in pixels
    //   height: Framebuffer height in pixels
    //
    // RETURNS: true on success, false on failure
    //
    // FAILURE MODES:
    // - Metal not available (old macOS, VM, etc)
    // - default.metallib not found (build error)
    // - Shader compilation failed (Metal syntax error)
    bool initialize(int width, int height);

    // Check if GPU rasterizer is initialized
    bool is_initialized() const { return initialized_; }

    // Check if Metal Ray Tracing is available (M3+ chip required)
    // Ray tracing uses hardware-accelerated BVH traversal for faster shadows
    bool supports_raytracing() const { return supports_raytracing_; }

    // Which structure the shadow kernel will actually trace against this run.
    // Anything that builds acceleration data must consult this rather than
    // assuming, which is the bug this API exists to make impossible.
    ShadowAccelBackend shadow_accel_backend() const;

    // =====================================================================
    // METAL RT: ACCELERATION STRUCTURE (Phase 1.2)
    // =====================================================================
    // Build Metal acceleration structure from shadow triangles
    // Replaces software BVH with hardware-accelerated structure
    //
    // INPUT:
    //   triangles: Array of shadow triangles (v0, v1, v2 per triangle)
    //   triangle_count: Number of triangles
    //
    // RETURNS: true on success, false on failure (or RT not available)
    //
    // USAGE: Call once per frame (or when geometry changes) before shadow pass
    //        The acceleration structure is used automatically by trace_shadows_metal_rt
    bool build_acceleration_structure(const void* triangles, uint32_t triangle_count);

    // =====================================================================
    // STEP 1: MINIMAL RASTERIZATION
    // =====================================================================
    // Write 1 red pixel at (150, 150) to prove GPU can write to PixelBuffer
    //
    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA, matches PixelBuffer)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void rasterize_minimal(uint32_t* pixel_buffer);

    // =====================================================================
    // STEP 2: FULL TRIANGLE RASTERIZATION
    // =====================================================================
    // Rasterize all pixels inside triangle bbox using edge equations
    //
    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA)
    //   triangle_verts: 6 floats (x0, y0, x1, y1, x2, y2) in screen space
    //   r, g, b, a: Color (0-255)
    //
    // THREAD SAFETY: Not thread-safe (call from render thread only)
    void rasterize_triangle(uint32_t* pixel_buffer,
                           const float triangle_verts[6],
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // =====================================================================
    // STEP 3: BARYCENTRIC INTERPOLATION TEST
    // =====================================================================
    // Rasterize triangle with barycentric coordinates visualized as RGB
    // Verifies barycentric math before adding depth in STEP 4
    //
    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA)
    //   triangle_verts: 6 floats (x0, y0, x1, y1, x2, y2) in screen space
    //
    // OUTPUT: RGB visualization (w=R, u=G, v=B)
    void rasterize_triangle_barycentric(uint32_t* pixel_buffer,
                                       const float triangle_verts[6]);

    // =====================================================================
    // STEP 4: DEPTH BUFFER WITH ATOMICS
    // =====================================================================
    // Rasterize triangle with depth testing for handling overlapping triangles
    //
    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA)
    //   depth_buffer: CPU-side depth buffer (uint32_t, atomic on GPU)
    //   triangle_verts: 9 floats (x0,y0,z0, x1,y1,z1, x2,y2,z2) in screen space
    //   r, g, b, a: Color (0-255)
    //
    // THREAD SAFETY: Atomic depth operations on GPU, not thread-safe on CPU
    void rasterize_triangle_with_depth(uint32_t* pixel_buffer,
                                       uint32_t* depth_buffer,
                                       const float triangle_verts[9],
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // =====================================================================
    // STEP 5: MULTIPLE TRIANGLES BATCH RASTERIZATION
    // =====================================================================
    // Rasterize N triangles in single GPU call
    //
    // Triangle struct matching gpu_types.metal::TriangleGPU
    struct TriangleGPU {
        float x0, y0, z0, _padding0;
        float x1, y1, z1, _padding1;
        float x2, y2, z2, _padding2;
        float r, g, b, a;  // Color 0-1 range
    };

    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA)
    //   depth_buffer: CPU-side depth buffer (uint32_t, atomic on GPU)
    //   triangles: Array of TriangleGPU structs
    //   triangle_count: Number of triangles in array
    //
    // THREAD SAFETY: Atomic depth operations on GPU
    void rasterize_triangles_batch(uint32_t* pixel_buffer,
                                   uint32_t* depth_buffer,
                                   const TriangleGPU* triangles,
                                   uint32_t triangle_count);

    // =====================================================================
    // STEP 7: LIGHTING INTEGRATION
    // =====================================================================
    // Extended triangle with lighting data - matches gpu_types.metal::TriangleLit
    struct TriangleLit {
        float x0, y0, z0, _padding0;
        float x1, y1, z1, _padding1;
        float x2, y2, z2, _padding2;
        float r, g, b, a;                    // Base material color 0-1
        float world_pos0[3], _padding3;      // World position vertex 0
        float world_pos1[3], _padding4;      // World position vertex 1
        float world_pos2[3], _padding5;      // World position vertex 2
        float normal[3];                     // Surface normal
        int particle_id;                     // Particle ID (for debug)
        // Total so far: 160 bytes
        float roughness;                     // Material roughness (0=mirror, 1=matte) for SSGI
        float _padding_roughness[3];         // Padding to 176 bytes (16-byte aligned)
    };

    // Rasterize triangles with GPU lighting
    // Combines rasterization + shadow rays + lighting computation in single pass
    //
    // INPUT:
    //   pixel_buffer: CPU-side native buffer (uint32_t BGRA)
    //   depth_buffer: CPU-side depth buffer (uint32_t, atomic on GPU)
    //   triangles: Array of TriangleLit structs with world positions/normals
    //   triangle_count: Number of triangles
    //   lights: Array of light data
    //   light_count: Number of lights
    //   bvh_nodes: BVH for shadow ray acceleration
    //   bvh_count: Number of BVH nodes
    //   bvh_triangles: Triangle geometry for shadow tests
    //   bvh_triangle_count: Number of shadow triangles
    //
    // OUTPUT: Fully lit pixels written to pixel_buffer
    void rasterize_triangles_lit(uint32_t* pixel_buffer,
                                uint32_t* depth_buffer,
                                const TriangleLit* triangles,
                                uint32_t triangle_count,
                                const void* lights,
                                uint32_t light_count,
                                const void* bvh_nodes,
                                uint32_t bvh_count,
                                const void* bvh_triangles,
                                uint32_t bvh_triangle_count);

    // =====================================================================
    // ASYNC GPU RASTERIZATION (Triple-Buffered CPU/GPU Overlap)
    // =====================================================================
    // Asynchronous version: GPU processes frame while CPU continues working
    // Completion callback runs when GPU finishes (on background thread!)
    //
    // USAGE:
    //   rasterizer.rasterize_triangles_lit_async(triangles, count, lights, ...,
    //       [](uint32_t* fb, uint32_t* db, int w, int h, void* ud) {
    //           // Apply results here (runs on background thread)
    //       }, user_data);
    //
    // THREADING: Callback runs on Metal's completion thread (NOT main thread!)
    //
    // PERFORMANCE: +50-100% FPS from CPU/GPU overlap
    //              GPU renders Frame N while CPU builds BVH for Frame N+1
    //
    using CompletionCallback = void (*)(uint32_t* framebuffer, uint32_t* depth_buffer,
                                         void* gbuffer,
                                         int width, int height, void* user_data);

    void rasterize_triangles_lit_async(
        const TriangleLit* triangles,
        uint32_t triangle_count,
        const void* lights,
        uint32_t light_count,
        const void* bvh_nodes,
        uint32_t bvh_count,
        const void* bvh_triangles,
        uint32_t bvh_triangle_count,
        // Tile binning data (optional - pass nullptr to disable)
        const uint32_t* tile_indices,      // Flattened triangle indices
        const uint32_t* tile_offsets,      // Offset per tile into tile_indices
        const uint32_t* tile_counts,       // Triangle count per tile
        int tiles_x,                       // Tile grid width
        int tiles_y,                       // Tile grid height
        CompletionCallback callback,
        void* user_data);

    // =====================================================================
    // DEFERRED RENDERING (3-Pass Architecture)
    // =====================================================================
    // Asynchronous deferred rendering: Separates geometry, shadows, lighting
    // PERFORMANCE: 19× faster than forward rendering (coherent BVH traversal)
    //
    // ARCHITECTURE:
    //   Pass 1: Rasterize G-buffer (geometry data for visible pixels only)
    //   Pass 2: Trace shadow rays (coherent BVH traversal on visible pixels)
    //   Pass 3: Apply lighting (simple math: G-buffer + shadow results → framebuffer)
    //
    // USAGE: Same as rasterize_triangles_lit_async but uses 3-pass pipeline
    //
    // THREADING: Same async pattern - callback runs on Metal completion thread
    void rasterize_triangles_deferred_async(
        const TriangleLit* triangles,
        uint32_t triangle_count,
        const void* lights,
        uint32_t light_count,
        const void* bvh_nodes,
        uint32_t bvh_count,
        const void* bvh_triangles,
        uint32_t bvh_triangle_count,
        // Entity BVH data (optional - pass nullptr to use flat BVH)
        const void* entity_bvh_nodes,      // EntityBVHNode array (48 bytes each)
        uint32_t entity_node_count,        // Number of entity BVH nodes
        const void* directional_groups,    // DirectionalGroup array (32 bytes each)
        uint32_t dir_group_count,          // Number of directional groups
        // Tile binning data (optional - pass nullptr to disable)
        const uint32_t* tile_indices,      // Flattened triangle indices
        const uint32_t* tile_offsets,      // Offset per tile into tile_indices
        const uint32_t* tile_counts,       // Triangle count per tile
        int tiles_x,                       // Tile grid width
        int tiles_y,                       // Tile grid height
        // Light source mapping (for emissive rendering in Pass 3)
        const uint8_t* is_light_source_map,// Maps particle_id -> is_light_source (1/0)
        uint32_t map_size,                 // Size of mapping buffer
        // Particle transforms (for pattern rendering in Pass 3)
        const void* particle_transforms,   // ParticleTransform array (or nullptr)
        uint32_t particle_count,           // Number of particles
        CompletionCallback callback,
        void* user_data);

    // Set transparent triangle data for Pass 3.5 forward rendering.
    // Called from render_pipeline BEFORE rasterize_triangles_deferred_async().
    // Data is uploaded to current_buffer_index_ (the slot that the next
    // deferred async call will use). Pass 3.5 dispatches if count > 0.
    // Upload the per-triangle screen-bbox stream (int4 per triangle:
    // min_x, min_y, max_x, max_y) for RASTER_BBOX_STREAM. Call BEFORE
    // rasterize_triangles_deferred_async(), same slot discipline as
    // set_transparent_triangles.
    void set_triangle_bboxes(const int32_t* bboxes, uint32_t triangle_count);

    void set_transparent_triangles(
        const TriangleLit* triangles,
        uint32_t triangle_count);

    // Wait for all pending GPU work to complete (for safe shutdown)
    void wait_for_completion();

    // Synchronous framebuffer read-back. Blocks until the GPU has
    // drained all pending command buffers, then copies the most
    // recently written framebuffer slot into `out_pixels`. Designed
    // for headless acceptance tests that need real GPU-rendered
    // pixels without a window, swapchain, or Metal-completion-
    // thread runloop pump. Caller owns `out_pixels` — it must be
    // at least `width_ * height_` uint32_t in BGRA byte order.
    //
    // Contract: call this AFTER at least one
    // `rasterize_triangles_deferred_async` + any follow-up you need
    // (no separate wait needed — this function waits internally).
    //
    // Returns false if no frame has ever been dispatched (nothing
    // to read), or if `out_pixels` is null.
    //
    // This is the engine-side surgical fix for "EngineMode::Headless
    // doesn't expose GPU pixels" — see memory
    // `project_renderer_display_split.md` for the long-term
    // architectural fix (option C).
    bool read_latest_framebuffer(uint32_t* out_pixels,
                                 int& out_width, int& out_height);

    // Reset all temporal GPU state for scene transitions.
    // Clears gi_temporal, temporal_lighting, sample_count buffers and resets
    // GI frame counter so indirect lighting reconverges from scratch.
    // MUST call wait_for_completion() first to ensure no in-flight GPU work.
    void reset_temporal_state();

    // Split acquire/release for resolution changes (holds GPU idle while changing drawable)
    // Returns number of slots acquired (0 on timeout/failure, GPU_BUFFER_SLOTS on success)
    int acquire_all_slots();
    void release_all_slots(int slots_to_release);

    // =====================================================================
    // VISION CONE POST-PROCESS (Pass 4)
    // =====================================================================
    // Apply fog-of-war effect based on viewer's field of view
    // Darkens pixels outside the viewer's vision cone
    //
    // USAGE:
    //   rasterizer.set_vision_cone(viewer_x, viewer_y, look_dir, fov_rad, range);
    //   rasterizer.set_vision_cone_enabled(true);
    //   // Pass 4 will be automatically applied after Pass 3
    //
    // NOTE: Set vision cone params BEFORE calling rasterize_triangles_deferred_async

    // Enable/disable vision cone post-process
    void set_vision_cone_enabled(bool enabled);
    bool get_vision_cone_enabled() const { return vision_cone_enabled_; }

    // Set vision cone parameters
    // viewer_x, viewer_y: World position of viewer
    // look_direction: Direction facing (radians, 0 = +Y/North)
    // fov_radians: Total field of view in radians (e.g., pi/2 = 90 degrees)
    // range: Maximum vision distance in world units
    void set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                         float fov_radians, float range);

    // Fine-tune vision cone appearance
    // inner_falloff: Where edge softness starts (0.0-1.0 of half_fov, default 0.8)
    // darkness: Brightness outside cone (0.0=black, 1.0=fully visible, default 0.1)
    void set_vision_cone_style(float inner_falloff, float darkness);

    // Set focus point for foveal vision simulation
    // Pixels near focus point are sharp/full color, pixels farther away are subtly desaturated
    // focus_x, focus_y: World position of focus (typically mouse position)
    // focus_radius: Radius of sharp focus area in meters (default 3.0)
    void set_vision_cone_focus(float focus_x, float focus_y, float focus_radius = 3.0f);

    // Number of angular bins in the LOS occlusion mask. Mirror of the
    // VISION_CONE_OCCLUSION_BINS macro in gpu_types.metal — the
    // VisionConeParams struct is byte-identical between C++ and the
    // shader, so this MUST stay in sync.
    static constexpr int kVisionConeOcclusionBins = 256;

    // LOS occlusion mask. The host pre-computes the distance to the
    // nearest occluder along each of `kVisionConeOcclusionBins`
    // angular bins spanning the cone (bin 0 = look_direction -
    // half_fov, bin N-1 = look_direction + half_fov), and the
    // shader darkens any pixel whose distance from the viewer
    // exceeds that bin's value. Pass count = 0 (or call
    // clear_vision_cone_occlusion()) to disable the mask, leaving
    // the cone as a pure angle+range dimmer (legacy behavior).
    //
    // distances: array of `count` floats in meters. Indices beyond
    //   `count` are zeroed in the shader buffer.
    // count: must equal kVisionConeOcclusionBins to enable the mask
    //   (any other value disables it — the API is intentionally
    //   strict to prevent ambiguous bin counts).
    void set_vision_cone_occlusion(const float* distances, int count);
    void clear_vision_cone_occlusion();

    // Vision memory grid — world-space "what the viewer just saw"
    // buffer that decays over time. See
    // include/logosphere/rendering/vision_memory.h for the math
    // and tests/test_vision_memory.cpp for the contract.
    //
    // Call sequence:
    //   1. set_vision_memory_extent(...)   — once, sets resolution + world bounds
    //   2. set_vision_memory_decay(...)    — once or as needed
    //   3. set_vision_memory_enabled(true) — opt in
    //   4. update_vision_memory(dt)        — every frame, after set_vision_cone(...)
    void set_vision_memory_enabled(bool enabled);
    void set_vision_memory_extent(float min_x, float min_y,
                                  float max_x, float max_y,
                                  int cells_per_side);
    void set_vision_memory_decay(float decay_seconds, float memory_dim);
    void update_vision_memory(float dt);

    // Per-particle "is_dynamic" map, indexed by particle_id.
    // Mirrors the is_light_source_map pattern. The vision-cone Pass 4
    // shader reads this to skip the memory-blend for dynamic pixels —
    // so a moving cycle (or any other transient particle) doesn't
    // appear in the player's "what I just saw" trail.
    // Pass nullptr (or size = 0) to clear: nothing is treated as
    // dynamic and memory blends every pixel as before.
    void set_dynamic_particle_map(const uint8_t* data, size_t size);

    // =====================================================================
    // SHADOW DISTANCE CULLING (Pass 3)
    // =====================================================================
    // Fade distant pixels to black to prevent bright artifacts when shadow
    // triangles are culled for distant particles.
    //
    // NOTE: Set before calling rasterize_triangles_deferred_async
    void set_shadow_culling_camera(float camera_x, float camera_y);
    // View azimuth (orbit) for temporal shadow reprojection: the
    // camera-delta rotation runs in the view frame, and an azimuth
    // change invalidates translation-based history entirely.
    void set_view_azimuth(float radians) { view_azimuth_ = radians; }

    // Set projection params for temporal reprojection (soft shadow aura fix)
    // pixels_per_unit: Isometric projection scale from camera system
    void set_shadow_projection_params(float pixels_per_unit);

    // Diagnostic: read shadow pipeline internal state for a pixel
    // Returns false if buffers are not available (e.g. not initialized)
    bool read_shadow_debug(int screen_x, int screen_y,
                           uint32_t& out_sample_count,
                           float& out_temporal_lux,
                           uint32_t& out_prev_particle_id) const;

    // Diagnostic: read GI temporal buffer and last shadow_results for a pixel
    bool read_gi_debug(int screen_x, int screen_y,
                       float& out_gi_r, float& out_gi_g, float& out_gi_b,
                       float& out_shadow_lux) const;

    // Diagnostic: read the SSDO buffer Pass 3 consumed (denoised slot;
    // raw results slot as fallback). Decodes half4 storage when
    // SSDO_HALF_PRECISION is on, including the pack range shift.
    bool read_ssdo_debug(int screen_x, int screen_y,
                         float& out_r, float& out_g, float& out_b,
                         float& out_ao) const;

    // Diagnostic: read G-buffer base_color and particle_id at a pixel
    bool read_gbuffer_debug(int screen_x, int screen_y,
                            uint8_t& out_r, uint8_t& out_g, uint8_t& out_b,
                            uint32_t& out_particle_id) const;

    // Diagnostic: read GPU-side framebuffer directly (bypass CPU copy)
    bool read_gpu_framebuffer(int screen_x, int screen_y,
                              uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) const;

    // =====================================================================
    // STEP 6: SURFACE UPLOAD HELPER
    // =====================================================================
    // Convert Surface (quad with 4 vertices) to 2 GPU triangles
    // Handles projection from world space to screen space
    //
    // INPUT:
    //   surface: World-space surface with vertices
    //   camera_system: For world→screen projection
    //   particle_r, particle_g, particle_b, particle_a: Color (0-255)
    //   out_triangles: Output vector to append triangles to
    //
    // OUTPUT: Appends 1 or 2 triangles to out_triangles (1 for tri, 2 for quad)
    static void convert_surface_to_gpu_triangles(
        const struct Surface& surface,
        class CameraSystem& camera_system,
        uint8_t particle_r, uint8_t particle_g, uint8_t particle_b, uint8_t particle_a,
        std::vector<TriangleGPU>& out_triangles);

    // STEP 7: Convert Surface to TriangleLit (with lighting data)
    // Same as convert_surface_to_gpu_triangles but includes world positions and normals
    //
    // INPUT:
    //   surface: World-space surface with vertices and normal
    //   camera_system: For world→screen projection
    //   particle_r, particle_g, particle_b, particle_a: Color (0-255)
    //   out_triangles: Output vector to append lit triangles to
    //
    // OUTPUT: Appends 1 or 2 TriangleLit structs (1 for tri, 2 for quad)
    static void convert_surface_to_lit_triangles(
        const struct Surface& surface,
        class CameraSystem& camera_system,
        uint8_t particle_r, uint8_t particle_g, uint8_t particle_b, uint8_t particle_a,
        std::vector<TriangleLit>& out_triangles);

private:
    // Metal resources (opaque pointers in C++, actual types in .mm file)
    void* device_;                       // id<MTLDevice>
    void* command_queue_;                // id<MTLCommandQueue>
    void* compute_pipeline_minimal_;     // id<MTLComputePipelineState> for STEP 1
    void* compute_pipeline_triangle_;    // id<MTLComputePipelineState> for STEP 2
    void* compute_pipeline_barycentric_; // id<MTLComputePipelineState> for STEP 3
    void* compute_pipeline_with_depth_;  // id<MTLComputePipelineState> for STEP 4
    void* compute_pipeline_batch_;       // id<MTLComputePipelineState> for STEP 5
    void* compute_pipeline_lit_;         // id<MTLComputePipelineState> for STEP 7
    void* compute_pipeline_clear_;       // id<MTLComputePipelineState> for buffer clearing
    void* compute_pipeline_gbuffer_;     // id<MTLComputePipelineState> for deferred pass 1 (G-buffer)
    void* compute_pipeline_shadows_;     // id<MTLComputePipelineState> for deferred pass 2 (shadow rays)
    void* compute_pipeline_shadows_instrumented_; // id<MTLComputePipelineState> for instrumented shadow rays (BVH profiling)
    void* compute_pipeline_shadows_batched_; // id<MTLComputePipelineState> for batched shadow rays (Phase II-B: 8 pixels/thread)
    void* compute_pipeline_lighting_;    // id<MTLComputePipelineState> for deferred pass 3 (apply lighting)
    void* compute_pipeline_ssao_;              // id<MTLComputePipelineState> for SSAO pass 2.7
    void* compute_pipeline_denoise_ssao_;      // id<MTLComputePipelineState> for SSAO denoise pass 2.8
    void* compute_pipeline_ddgi_trace_;        // id<MTLComputePipelineState> for DDGI probe tracing
    void* compute_pipeline_ddgi_update_;       // id<MTLComputePipelineState> for DDGI probe update
    void* compute_pipeline_denoise_shadow_;  // id<MTLComputePipelineState> for shadow denoise pass 2.05
    void* compute_pipeline_forward_transparent_; // id<MTLComputePipelineState> for forward transparency pass 3.5
    void* compute_pipeline_forward_transparent_rt_ = nullptr; // RT-intersector variant (preferred when Metal RT available)

    // Item C (RT AS refit): whether the live acceleration structure was
    // built with Refit usage, and frames since the last full build
    // (periodic rebuild keeps tree quality under sustained motion).
    bool accel_supports_refit_ = false;
    int  accel_frames_since_full_build_ = 0;
    void* library_;                      // id<MTLLibrary>

    // Persistent GPU buffers (STEP 7.5: Prevent memory leak)
    // Pattern from metal_compute_bridge.mm - reuse buffers instead of creating new ones every frame
    void* framebuffer_buffer_;           // id<MTLBuffer> - pixel data
    void* depth_buffer_;                 // id<MTLBuffer> - depth data
    void* triangles_buffer_;             // id<MTLBuffer> - triangle geometry
    void* lights_buffer_;                // id<MTLBuffer> - light sources
    void* bvh_nodes_buffer_;             // id<MTLBuffer> - BVH tree
    void* bvh_triangles_buffer_;         // id<MTLBuffer> - shadow triangles

    // Capacity tracking (only reallocate if size grows)
    size_t framebuffer_capacity_;
    size_t depth_capacity_;
    size_t triangles_capacity_;
    size_t lights_capacity_;
    size_t bvh_nodes_capacity_;
    size_t bvh_triangles_capacity_;

    // Multi-buffered GPU buffers (async execution with CPU/GPU overlap)
    // Configurable buffer count (GPU_BUFFER_SLOTS) - typically 3-5 buffers
    // More buffers = less CPU blocking when GPU falls behind, but more memory
    void* framebuffer_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* depth_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* triangles_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* lights_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* bvh_nodes_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* bvh_triangles_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* entity_bvh_nodes_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];   // Entity BVH nodes (48 bytes each)
    void* directional_groups_buffer_async_[Optimizations::GPU_BUFFER_SLOTS]; // Directional groups (32 bytes each)
    void* tile_indices_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];   // Tile binning: flattened triangle indices
    void* tile_offsets_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];   // Tile binning: offset per tile
    void* tile_counts_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];    // Tile binning: triangle count per tile
    void* gbuffer_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];        // Deferred rendering: G-buffer (48 bytes per pixel with roughness)
    void* shadow_results_buffer_async_[Optimizations::GPU_BUFFER_SLOTS]; // Deferred rendering: shadow results (4 bytes per pixel)
    void* light_color_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];   // Per-pixel light color ratio (16 bytes, float4 RGB+pad)
    void* shadow_denoised_buffer_async_[Optimizations::GPU_BUFFER_SLOTS]; // Shadow denoise output (pass 2.05)
    void* ssao_results_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];   // SSAO: per-pixel AO (4 bytes per pixel)
    void* ssao_denoised_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];  // SSAO denoise output (ping-pong)
    void* transparent_triangles_buffer_async_[Optimizations::GPU_BUFFER_SLOTS]; // Forward transparency (pass 3.5)

    // Buffer capacity tracking (async)
    size_t framebuffer_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t depth_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t triangles_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t lights_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t bvh_nodes_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t bvh_triangles_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t entity_bvh_nodes_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t directional_groups_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t tile_indices_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t tile_offsets_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t tile_counts_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t gbuffer_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];        // Deferred rendering: G-buffer capacity
    size_t shadow_results_capacity_async_[Optimizations::GPU_BUFFER_SLOTS]; // Deferred rendering: shadow results capacity
    size_t light_color_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];   // Per-pixel light color ratio capacity
    size_t shadow_denoised_capacity_async_[Optimizations::GPU_BUFFER_SLOTS]; // Shadow denoise output capacity
    size_t ssao_results_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];    // SSAO results capacity
    size_t ssao_denoised_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];   // SSAO denoise capacity
    size_t transparent_triangles_capacity_async_[Optimizations::GPU_BUFFER_SLOTS]; // Forward transparency capacity
    uint32_t transparent_triangle_count_async_[Optimizations::GPU_BUFFER_SLOTS]; // Count per slot for Pass 3.5

    // Multi-buffering state (async execution)
    void* buffer_semaphore_;        // dispatch_semaphore_t (tracks available buffers)
    int current_buffer_index_;      // Ring buffer rotation: 0..GPU_BUFFER_SLOTS-1
    std::atomic<bool> gpu_device_lost_{false};  // Set on AccessRevoked — stops frame submission

    // Cached constant buffers (avoid recreation every frame)
    // These rarely change, so cache them and only update when needed
    void* width_buffer_;            // id<MTLBuffer> - framebuffer width
    void* height_buffer_;           // id<MTLBuffer> - framebuffer height
    void* clear_color_buffer_;      // id<MTLBuffer> - clear color constant

    // Phase 1: Shadow quality - reduced resolution shadow buffer dimensions
    void* shadow_width_buffer_;     // id<MTLBuffer> - shadow buffer width (uint32_t)
    void* shadow_height_buffer_;    // id<MTLBuffer> - shadow buffer height (uint32_t)

    // Phase 2: Temporal distribution - persistent buffers for checkerboard rendering
    void* temporal_lighting_buffer_;     // id<MTLBuffer> - previous frame lighting results (NOT per-buffer, shared across frames)
    size_t temporal_lighting_capacity_;  // Capacity tracking for temporal buffer
    void* prev_particle_id_buffer_;      // id<MTLBuffer> - previous frame particle IDs for motion detection (soft shadow ghosting fix)
    size_t prev_particle_id_capacity_;   // Capacity tracking for particle ID buffer
    void* sample_count_buffer_;          // id<MTLBuffer> - per-pixel sample count for running average (soft shadow convergence)
    size_t sample_count_capacity_;       // Capacity tracking for sample count buffer
    void* frame_index_buffer_;           // id<MTLBuffer> - current frame index for checkerboard pattern (uint32_t)

    // GI temporal accumulation - persistent buffer for indirect lighting history

    // Phase 2: Indirect dispatch - pre-computed pixel indices for optimized thread dispatch
    // Support for N-frame temporal distribution (2, 3, 4, or 5 frames)
    static constexpr int MAX_TEMPORAL_FRAMES = 5;  // Maximum TEMPORAL_FRAME_COUNT value
    void* pixel_indices_buffers_[MAX_TEMPORAL_FRAMES];  // id<MTLBuffer> array - one buffer per frame pattern
    uint32_t pixel_indices_count_;       // Number of indices per frame (total_pixels / TEMPORAL_FRAME_COUNT)

    // QW2: Cache small constant buffers (4 bytes each)
    // Previously created every frame (8 allocations/frame), now cached and updated
    void* triangle_count_buffer_;   // id<MTLBuffer> - triangle count (uint32_t)
    void* light_count_buffer_;      // id<MTLBuffer> - light count (uint32_t)
    void* bvh_node_count_buffer_;   // id<MTLBuffer> - BVH node count (uint32_t)
    void* bvh_triangle_count_buffer_; // id<MTLBuffer> - BVH triangle count (uint32_t)

    // QW4: GPU destination buffers (Private mode) for blit transfers
    // Staging buffers (Shared) → GPU buffers (Private) via blit encoder
    // Benefits: Less cache coherency overhead, GPU-optimized bandwidth
    void* triangles_buffer_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* bvh_nodes_buffer_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];
    void* bvh_triangles_buffer_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];

    // QW4: Capacity tracking for GPU buffers
    size_t triangles_capacity_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t bvh_nodes_capacity_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t bvh_triangles_capacity_gpu_async_[Optimizations::GPU_BUFFER_SLOTS];

    // Framebuffer dimensions
    int width_;
    int height_;

    // Phase 1: Runtime shadow dimensions (calculated from framebuffer × SHADOW_RESOLUTION_SCALE)
    // Why runtime: Framebuffer size is runtime-dependent (window size), not compile-time
    // Ownership: GPURasterizer owns shadow buffer → owns shadow dimensions
    uint32_t shadow_width_;   // = width_ × SHADOW_RESOLUTION_SCALE
    uint32_t shadow_height_;  // = height_ × SHADOW_RESOLUTION_SCALE

    // Initialization state
    bool initialized_;

    // Metal RT (Ray Tracing) support - requires M3+ chip (Apple6 family)
    bool supports_raytracing_ = false;

    // Metal RT acceleration structure (built from shadow triangles)
    // Replaces software BVH traversal with hardware-accelerated traversal
    void* acceleration_structure_;           // id<MTLAccelerationStructure>
    void* accel_scratch_buffer_;            // id<MTLBuffer> - scratch space for AS build
    size_t accel_scratch_capacity_ = 0;
    uint32_t accel_triangle_count_ = 0;     // Number of triangles in current AS

    // Metal RT compute pipeline for shadow rays
    void* compute_pipeline_shadows_rt_;     // id<MTLComputePipelineState> for RT shadow kernel

    // Deterministic shadow kernel (1 closest-hit ray/pixel/light, no temporal, no PCSS)
    void* compute_pipeline_shadows_deterministic_;  // id<MTLComputePipelineState>

    // Blocker distance buffer (per-pixel closest blocker distance for penumbra computation)
    void* blocker_distance_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t blocker_distance_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];

    // Penumbra post-process pipelines (Pass 2.5)
    void* compute_pipeline_penumbra_blocker_;   // id<MTLComputePipelineState> for Approach C

    // JFA + Separable Blur penumbra pipelines (Tier 2)
    void* compute_pipeline_jfa_seed_ = nullptr;
    void* compute_pipeline_jfa_propagate_ = nullptr;
    void* compute_pipeline_blur_h_ = nullptr;
    void* compute_pipeline_blur_v_ = nullptr;

    // Penumbra temp buffer: copy of shadow_results for read-while-write
    void* penumbra_temp_buffer_async_[Optimizations::GPU_BUFFER_SLOTS];
    size_t penumbra_temp_capacity_async_[Optimizations::GPU_BUFFER_SLOTS];

    // JFA ping-pong buffers for penumbra width propagation
    void* jfa_buffer_a_[Optimizations::GPU_BUFFER_SLOTS];
    void* jfa_buffer_b_[Optimizations::GPU_BUFFER_SLOTS];
    size_t jfa_buffer_capacity_[Optimizations::GPU_BUFFER_SLOTS];
    // Packed particle-id stream for the penumbra V-blur (PENUMBRA_COMPACT_IDS)
    void* penumbra_id_buffer_[Optimizations::GPU_BUFFER_SLOTS] = {};
    size_t penumbra_id_capacity_[Optimizations::GPU_BUFFER_SLOTS] = {};
    // Precomputed screen-bbox stream for the raster reject path (RASTER_BBOX_STREAM)
    void* tri_bbox_buffer_[Optimizations::GPU_BUFFER_SLOTS] = {};
    size_t tri_bbox_capacity_[Optimizations::GPU_BUFFER_SLOTS] = {};
    uint32_t tri_bbox_count_[Optimizations::GPU_BUFFER_SLOTS] = {};

    // Vision cone state (Pass 4 post-process)
    bool vision_cone_enabled_ = false;
    float vision_cone_viewer_x_ = 0.0f;
    float vision_cone_viewer_y_ = 0.0f;
    float vision_cone_look_direction_ = 0.0f;
    float vision_cone_half_fov_ = 0.785f;  // 45 degrees = 90 degree FOV
    float vision_cone_range_ = 20.0f;
    float vision_cone_inner_falloff_ = 0.8f;
    float vision_cone_darkness_ = 0.1f;
    float vision_cone_focus_x_ = 0.0f;     // Focus point (mouse position)
    float vision_cone_focus_y_ = 0.0f;
    float vision_cone_focus_radius_ = 3.0f; // Radius of sharp focus area (meters)
    // LOS occlusion mask. _count == 0 disables; on enable the host
    // pre-fills _distances each frame from CPU raycasts.
    int   vision_cone_occlusion_count_ = 0;
    float vision_cone_occlusion_distance_[kVisionConeOcclusionBins] = {};

    // Vision-memory grid state (CPU mirror + GPU buffer).
    bool                vision_memory_enabled_     = false;
    int                 vision_memory_width_       = 0;
    int                 vision_memory_height_      = 0;
    float               vision_memory_origin_x_    = 0.0f;
    float               vision_memory_origin_y_    = 0.0f;
    float               vision_memory_cell_size_   = 1.0f;
    float               vision_memory_decay_seconds_ = 3.0f;
    float               vision_memory_dim_         = 0.4f;
    float               vision_memory_pending_dt_  = 0.0f;     // recorded by update_vision_memory();
                                                                // consumed inside Pass-4 dispatch
    std::vector<float>  vision_memory_data_;             // CPU mirror
    void*               vision_memory_buffer_      = nullptr;  // id<MTLBuffer>
    size_t              vision_memory_buffer_capacity_ = 0;    // # floats GPU buf can hold

    // Per-particle "is_dynamic" map (mirrors is_light_source_map_
    // semantics). dynamic_particle_map_data_ is the CPU-side mirror;
    // dynamic_particle_map_buffer_ is the GPU buffer at MTL bind
    // index 6 in Pass 4. dynamic_particle_map_size_ is the number of
    // valid entries (0 = no map; shader skips the dynamic check).
    std::vector<uint8_t> dynamic_particle_map_data_;
    void*                dynamic_particle_map_buffer_ = nullptr;
    size_t               dynamic_particle_map_capacity_ = 0;
    uint32_t             dynamic_particle_map_size_   = 0;

    // GI resolution (half-res for Tier 3 optimization)

    // Shadow distance culling state (Pass 3 distance fade)
    float view_azimuth_ = 0.0f;       // Orbit angle for temporal reprojection
    float prev_view_azimuth_ = 0.0f;  // Detects orbit motion -> history reset
    float shadow_culling_camera_x_ = 0.0f;  // Camera position for distance fade
    float shadow_culling_camera_y_ = 0.0f;

    // Temporal reprojection state (soft shadow aura fix)
    float prev_shadow_camera_x_ = 0.0f;  // Previous frame camera position
    float prev_shadow_camera_y_ = 0.0f;
    float shadow_pixels_per_unit_ = 20.0f;  // Isometric projection scale

    // Vision cone compute pipeline (Pass 4)
    void* compute_pipeline_vision_cone_;  // id<MTLComputePipelineState>
    void* vision_cone_params_buffer_;     // id<MTLBuffer> - cached params buffer

    // GPU Performance Profiling (MTLCounterSet API)
    void* timestamp_counter_set_;        // id<MTLCounterSet> - timestamp counters
    void* performance_counter_set_;      // id<MTLCounterSet> - occupancy/bandwidth counters
    void* counter_sample_buffer_;        // id<MTLCounterSampleBuffer> - holds sampled counter data
    static constexpr int kCounterSampleCount = 4;  // Before/after for Pass 2, plus extras

    // GPU Shader Instrumentation (Atomic Counters)
    // Measure BVH traversal and shadow ray activity during Pass 2
    void* debug_rays_traced_buffer_;         // id<MTLBuffer> - atomic_uint counter for rays traced
    void* debug_bvh_nodes_visited_buffer_;   // id<MTLBuffer> - atomic_uint counter for BVH nodes visited
    void* debug_triangles_tested_buffer_;    // id<MTLBuffer> - atomic_uint counter for triangles tested

    // Metal RT acceleration structure caching (prevent per-frame allocation leak)
    void* accel_vertex_buffer_ = nullptr;
    size_t accel_vertex_capacity_ = 0;
    size_t accel_struct_capacity_ = 0;   // Size of current acceleration_structure_

    // DDGI probe buffers (persistent across frames)
    void* ddgi_irradiance_buffer_ = nullptr;   // float4 per texel per probe
    size_t ddgi_irradiance_capacity_ = 0;
    void* ddgi_depth_buffer_ = nullptr;        // float2 per texel per probe
    size_t ddgi_depth_capacity_ = 0;
    void* ddgi_ray_results_buffer_ = nullptr;  // float4 per ray per probe (transient)
    size_t ddgi_ray_results_capacity_ = 0;
    uint32_t ddgi_frame_counter_ = 0;

    // GI frame counters (Halton sequence index for indirect lighting convergence).
    // Promoted from static locals to members so reset_temporal_state() can reset them.

    // GI temporal reset: conditional on camera/light changes (not every frame)
    std::vector<uint8_t> prev_light_data_;   // Previous frame raw light bytes for change detection
    uint32_t prev_light_count_ = 0;
};

} // namespace Logosphere

#endif // GPU_RASTERIZER_H
