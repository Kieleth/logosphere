#ifndef SURFACE_RASTERIZER_H
#define SURFACE_RASTERIZER_H

#include <vector>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include "particle.h"
#include "core/camera_system.h"
#include "core/light_system.h"
#include "engine_metrics.h"
#include "optimization_flags.h"
#include "logosphere/rendering/pixel_shader.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"  // For ShadowTriangle (Phase II-B)
#include "logosphere/physics/bvh.h"  // For ShadowRay (Phase II-C)
#include "logosphere/rendering/triangle_bvh.h"  // For GPU BVH (Phase II-B)
#include "logosphere/rendering/pixel_buffer.h"  // For per-frame buffers (Phase IV)
#include "logosphere/rendering/depth_buffer.h"  // For per-frame buffers (Phase IV)
#include "logosphere/rendering/rasterization_math.h"  // For math primitives (Phase III)

// Forward declarations
class TileThreadPool;
class BVH;

namespace Logosphere {
    class MetalComputeBridge;
}

/**
 * SurfaceRasterizer - Tile-based triangle and quad rasterization
 * 
 * Following KISS & UNIX philosophy: This class does ONE thing well - rasterization.
 * It takes 3D surfaces and converts them to pixels using tile-based rendering for
 * optimal cache performance.
 * 
 * Part of the rendering system refactoring to break down the monolithic RenderSystem.
 * 
 * Historical note: Tile-based rendering was popularized by PowerVR chips in the late 90s.
 * The Dreamcast's PowerVR2 used tile-based deferred rendering to achieve impressive
 * graphics with limited memory bandwidth. Modern mobile GPUs still use this approach.
 */
class SurfaceRasterizer {
public:
    // Structure to pass surface data for rendering
    struct SurfaceData {
        Surface surface;  // Contains roughness from particle material
        float particle_r, particle_g, particle_b, particle_a;  // Particle color (copied, not pointer!)
        bool is_light_source;
        float emission_strength;
        int particle_index;
        int surface_index;
        float closest_distance;
    };
    
    // Structure for tile binning
    struct TileBin {
        std::vector<const SurfaceData*> surfaces;
    };

    // Frame-level ray batching (Phase II-C: Single GPU call per frame)
    // Collected pixel data for deferred lighting application
    struct CollectedPixel {
        int screen_x, screen_y;              // Screen coordinates
        float world_x, world_y, world_z;     // World position
        float normal_x, normal_y, normal_z;  // Surface normal
        float particle_r, particle_g, particle_b, particle_a;  // Particle color (copied, not pointer!)
        int particle_index;                  // For shadow ray exclusion and object ID
        float lighting;                      // Accumulated lighting result
    };

    // Constructor/Destructor
    SurfaceRasterizer();
    ~SurfaceRasterizer();  // Defined in .cpp to handle unique_ptr<TileThreadPool>
    
    // Main rasterization entry point
    // Returns number of pixels drawn
    int rasterize_surfaces(
        const std::deque<SurfaceData>& surfaces,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        SparseObjectMap& object_map,  // NEW: RenderSystem's unified object map
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,  // Modular shading strategy
        const std::vector<Particle>& particles,  // All particles for light lookup
        const BVH* shadow_bvh,  // BVH for shadow acceleration
        int render_width,
        int render_height,
        EngineMetrics* metrics = nullptr
    );
    
    // Configuration
    void set_tile_size(int size) { tile_size_ = size; }
    int get_tile_size() const { return tile_size_; }

    // Frame tracking for GPU synchronization (triple buffering)
    int get_frame_counter() const { return frame_counter_; }
    void advance_frame() {
        frame_counter_++;
        // std::cout << "[SURF RASTER FRAME] " << old << " → " << frame_counter_ << std::endl;
    }

    // Wait for all pending GPU work (needed before particle deletion)
    void wait_for_gpu_completion();

    // Wait for all worker threads to complete (needed before clearing surface_cache_)
    void wait_for_workers_completion();

    // Initialize per-frame buffers (call when resolution changes)
    void init_frame_buffers(int width, int height);

private:
    // Tile configuration
    int tile_size_;
    
    // Thread pool for parallel tile processing
    std::unique_ptr<TileThreadPool> thread_pool_;
    
    // Per-thread workspace to avoid allocation during rendering
    struct ThreadWorkspace {
        // Thread-local workspace (currently empty - may be used for future optimizations)

        void init(int tile_size, int render_width, int render_height) {
            // Thread workspace initialized (single-pass rendering uses no buffers)
        }

        void reset() {
            // Thread workspace reset
        }
    };
    
    // One workspace per thread (including main thread)
    std::vector<ThreadWorkspace> thread_workspaces_;
    
    // Persistent worker threads for tile processing
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> worker_running_{false};
    int num_worker_threads_ = 0;  // Actual number of worker threads
    
    // Simple completion tracking - each worker sets its flag when done
    std::unique_ptr<std::atomic<bool>[]> worker_busy_;  // true = working, false = idle

    // GPU compute bridge for shadow rays (Phase II-A integration)
    Logosphere::MetalComputeBridge* gpu_bridge_;

    // GPU triangle cache (Phase II-B optimization)
    // Convert particles → triangles once per frame, not per segment
    std::vector<Logosphere::ShadowTriangle> gpu_triangle_cache_;

    // GPU triangle BVH (Phase II-B BVH)
    // Acceleration structure for O(log N) GPU traversal
    TriangleBVH gpu_triangle_bvh_;

    // BVH Refitting optimization (stable triangle slot mapping)
    std::unordered_map<uint32_t, int> particle_to_triangle_slot_;  // particle_id → first_triangle_index
    std::vector<bool> triangle_slot_active_;                        // true = slot contains valid triangle
    int next_free_slot_ = 0;                                        // Track next available triangle slot (monotonically increasing)
    bool bvh_needs_rebuild_ = true;                                 // true = full rebuild, false = refit only
    int frames_since_rebuild_ = 0;                                  // Counter for periodic rebuilds

    // Dirty flags for GPU data (avoid redundant uploads when particles static)
    bool gpu_triangles_dirty_ = true;  // Set true initially to force first upload
    size_t last_particle_count_ = 0;   // Track particle count to detect changes

    // Light data snapshot for GPU callbacks
    // Copies light properties instead of storing pointers to avoid use-after-free
    // when particle vector reallocates during chunk loading
    struct LightSnapshot {
        float x, y, z;
        float emission_strength;
        float emission_radius;
        float light_size;          // Physical size of light source (from particle size)

        LightSnapshot(float px, float py, float pz, float strength, float radius, float size = 0.0f)
            : x(px), y(py), z(pz), emission_strength(strength), emission_radius(radius), light_size(size) {}
    };

    // Frame-level ray batching storage (Phase IV: Per-frame buffers for async execution)
    struct FrameData {
        std::vector<CollectedPixel> pixels;          // All pixels needing lighting
        std::vector<ShadowRay> rays;                 // All shadow rays for GPU (pixels × lights)
        std::vector<bool> ray_results;               // GPU results (blocked/not blocked)
        std::vector<LightSnapshot> lights;           // Light data snapshot (not pointers!)

        // Per-frame buffers (survive across frames for async GPU)
        std::unique_ptr<PixelBuffer> pixel_buffer;   // Frame's pixel buffer (owns)
        std::unique_ptr<DepthBuffer> depth_buffer;   // Frame's depth buffer (owns)

        bool ready_to_write;                         // GPU completed, lighting applied
        int frame_number;                            // Global frame number (for ordering)

        std::mutex buffer_mutex;                     // Protects buffer access from Metal thread

        FrameData() : pixel_buffer(nullptr), depth_buffer(nullptr), ready_to_write(false), frame_number(-1) {}
    };

    FrameData frame_data_[3];                    // Triple-buffered frame data
    int current_frame_index_;                    // Current buffer being filled (0, 1, 2)
    int frame_counter_;                          // Global frame number (monotonic)
    std::mutex frame_pixels_mutex_;              // Thread-safe insertion during collection

    // Work queue for the worker thread
    struct WorkData {
        const std::vector<TileBin>* tile_bins = nullptr;
        PixelBuffer* pixel_buffer = nullptr;
        DepthBuffer* depth_buffer = nullptr;
        SparseObjectMap* object_map = nullptr;  // RenderSystem's unified object map (thread-safe!)
        CameraSystem* camera_system = nullptr;
        LightSystem* light_system = nullptr;
        IPixelShader* pixel_shader = nullptr;
        const std::vector<Particle>* particles = nullptr;
        const BVH* shadow_bvh = nullptr;
        std::vector<const Particle*> lights;  // Pre-collected lights (owned by WorkData for thread safety)
        const std::vector<Logosphere::ShadowTriangle>* gpu_triangles = nullptr;  // Cached GPU triangles (Phase II-B)
        int render_width = 0;
        int render_height = 0;
        float cam_x = 0, cam_y = 0, cam_z = 0;
        EngineMetrics* metrics = nullptr;
        std::atomic<int> next_work_index{0};
        std::atomic<int> pixels_done{0};
        int total_tiles = 0;  // Total number of tiles to process
        int tiles_x = 0;  // Tiles per row (for index calculation)
    } worker_data_;
    
    // Persistent storage for tile bins to avoid stack allocation
    // CRITICAL: Triple buffered for GPU safety (matches surface_cache_)
    std::vector<TileBin> tile_bins_storage_[3];
    
    // Worker thread function
    void worker_thread_func(int thread_id);
    
    // Binning phase - assign surfaces to tiles
    void bin_surfaces_to_tiles(
        const std::deque<SurfaceData>& surfaces,
        std::vector<TileBin>& tile_bins,
        CameraSystem& camera_system,
        int tiles_x,
        int tiles_y,
        int& surfaces_on_screen,
        int& total_assignments
    );
    
    // Rasterization phase - render each tile
    int rasterize_tiles(
        const std::vector<TileBin>& tile_bins,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        SparseObjectMap& object_map,  // RenderSystem's unified object map (thread-safe!)
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        const std::vector<Particle>& particles,  // For light lookup
        const BVH* shadow_bvh,  // BVH for shadow acceleration
        const std::vector<const Particle*>& lights,  // Pre-collected lights
        const std::vector<Logosphere::ShadowTriangle>* gpu_triangles,  // Cached GPU triangles (Phase II-B)
        int render_width,
        int render_height,
        int tiles_x,
        int tiles_y,
        EngineMetrics* metrics
    );
    
    // Process a single tile
    int process_tile(
        const TileBin& tile_bin,
        int tile_x,
        int tile_y,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        int render_width,
        int render_height,
        float cam_x, float cam_y, float cam_z,
        double& total_depth_time,
        double& total_lighting_time,
        EngineMetrics* metrics = nullptr
    );
    
    // Process a single tile with inline shadow testing (thread-safe)
    // Each thread can process tiles independently
    int process_tile_simple(
        const TileBin& tile_bin,
        int tile_x,
        int tile_y,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        const std::vector<Particle>& particles,  // For light lookup
        const BVH* shadow_bvh,  // BVH for shadow acceleration
        const std::vector<const Particle*>& lights,  // Pre-collected lights
        int render_width,
        int render_height,
        float cam_x, float cam_y, float cam_z,
        double& total_depth_time,
        double& total_lighting_time,
        EngineMetrics* metrics = nullptr
    );
    
    // Process a single surface within a tile
    int process_surface_in_tile(
        const SurfaceData& surf_data,
        int tile_screen_x,
        int tile_screen_y,
        int tile_screen_x_end,
        int tile_screen_y_end,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        float cam_x, float cam_y, float cam_z,
        double& total_depth_time,
        double& total_lighting_time,
        EngineMetrics* metrics = nullptr
    );

    // UV Gradient helper struct for tile-based gradient optimization
    struct UVGradient {
        float u_base, v_base;      // UV at tile origin
        float du_dx, dv_dx;        // Horizontal gradient
        float du_dy, dv_dy;        // Vertical gradient
        bool valid;                // Whether gradients are valid
    };

    // Process a single scanline using SIMD (8 pixels at once)
    // Extracted from process_surface_in_tile for clarity
    int process_scanline_simd(
        int y, int x_start, int x_end,
        const CameraSystem::ProjectedTriangle& projected,
        const float vertices[3][3],
        const SurfaceData& surf_data,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        float cam_x, float cam_y, float cam_z,
        EngineMetrics* metrics
    );

    // Process a single scanline using scalar path (1 pixel at a time)
    // Extracted from process_surface_in_tile for clarity
    int process_scanline_scalar(
        int y, int x_start, int x_end,
        int tile_screen_x, int tile_screen_y,
        const UVGradient& gradient,
        const CameraSystem::ProjectedTriangle& projected,
        const float vertices[3][3],
        const SurfaceData& surf_data,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        float cam_x, float cam_y, float cam_z,
        EngineMetrics* metrics
    );

    // Compute UV gradient for tile-based optimization
    // Extracted from process_surface_in_tile for clarity
    UVGradient compute_uv_gradient_for_tile(
        int tile_screen_x,
        int tile_screen_y,
        int tile_screen_x_end,
        int tile_screen_y_end,
        const CameraSystem::ProjectedTriangle& projected,
        CameraSystem& camera_system
    );

    // Helper struct for work stealing
    struct TileWorkChunk {
        int start_tile;
        int chunk_size;
        bool has_work;
    };

    // Get next tile work chunk using work stealing or strided access
    // Extracted from worker_thread_func for clarity
    TileWorkChunk get_next_tile_work();

    // Main thread processing loop when it participates in parallel rendering
    // Extracted from rasterize_tiles for clarity
    void main_thread_process_tiles(
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        const std::vector<const Particle*>& lights,
        int render_width,
        int render_height,
        float cam_x, float cam_y, float cam_z,
        int tiles_x,
        int total_tiles,
        double& total_depth_time,
        double& total_lighting_time,
        EngineMetrics* metrics
    );

    // Process a single pixel
    bool process_pixel(
        int x, int y,
        float u, float v,
        const float vertices[3][3],
        const SurfaceData& surf_data,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        float cam_x, float cam_y, float cam_z,
        EngineMetrics* metrics = nullptr
    );
    
    // Calculate interpolated depth at pixel
    float calculate_pixel_depth(
        float u, float v,
        const float vertices[3][3],
        float cam_x, float cam_y, float cam_z
    ) const;
    
    // Helper to calculate squared depth (for performance)
    inline float calculate_squared_depth(float dx, float dy, float dz) const {
        return RasterizationMath::distance_squared_3d(dx, dy, dz);
    }
    
    // --- Refactored methods for shadow ray batching ---

    // Single-pass rasterization with immediate shading and inline segment sampling
    void rasterize_tile_single_pass(
        const TileBin& tile_bin,
        int tile_x, int tile_y,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        CameraSystem& camera_system,
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        const std::vector<Logosphere::ShadowTriangle>* gpu_triangles,  // Cached GPU triangles (Phase II-B)
        int render_width, int render_height,
        float cam_x, float cam_y, float cam_z,
        SparseObjectMap* object_map,  // Thread-local object map for mouse interaction
        EngineMetrics* metrics = nullptr);
    
private:

    // Phase II-C: Process collected pixels with single GPU batch
    // Builds all shadow rays, dispatches to GPU, maps results to pixels
    void process_gpu_shadow_batch(
        const std::vector<const Particle*>& lights,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh
    );

    // Phase II-C: Write all collected pixels with lighting to framebuffer
    void write_collected_pixels(PixelBuffer& pixel_buffer);

    // Phase III: Write a completed frame's pixels to its pixel buffer
    void write_frame_pixels(FrameData& frame);

    // Phase III: Async GPU helper methods
    FrameData& current_frame() { return frame_data_[current_frame_index_]; }
    void next_frame() { current_frame_index_ = (current_frame_index_ + 1) % 3; }

    // Static completion callback for async GPU (called from Metal thread!)
    static void async_gpu_completion_callback(const float* results, int result_count, void* user_data);

    // Apply lighting from GPU results to frame pixels
    void apply_lighting_to_frame(FrameData& frame, const float* results, int result_count);

    // Active projection-aware camera, cached for the duration of a
    // rasterize pass so calculate_pixel_depth can route through
    // CameraSystem::compute_depth. Set at the top of rasterize_surfaces
    // and rasterize_tile_single_pass; read (only) on worker / caller
    // threads after the atomic_thread_fence release that publishes
    // worker_data_. Not owning, never null during a pass.
    const CameraSystem* active_camera_system_ = nullptr;
};

#endif // SURFACE_RASTERIZER_H