#include "logosphere/rendering/surface_rasterizer.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/depth_buffer.h"
#include "logosphere/rendering/pixel_shader.h"
#include "logosphere/rendering/tile_thread_pool.h"
#include "logosphere/rendering/parallel_tile_processor.h"
#include "logosphere/rendering/shadow_ray_batch.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include "../object_id.h"
#include "../frame_metrics.h"
#include "../debug_control.h"
#include "../engine_metrics.h"
#include "../optimization_flags.h"
#include "../simd_edge.h"
#include "../simd_uv.h"
#include "../lighting_primitives.h"
#include <cfloat>  // For FLT_MAX
#include "../lighting_config.h"
#include "../lighting_metrics.h"
#include "logosphere/physics/bvh.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <climits>  // For INT_MAX

// Particle geometry snapshot for GPU triangle conversion
// Protects from vector reallocation during chunk loading (same pattern as LightSnapshot)
struct ParticleGeometrySnapshot {
    uint32_t particle_id;
    bool is_light_source;
    uint8_t color_r, color_g, color_b, color_a;  // Particle color for indirect GI
    std::vector<Surface> surfaces;
};

// Forward declaration from surface_rasterizer_single_pass.cpp
void convert_particles_to_gpu_triangles(
    const std::vector<ParticleGeometrySnapshot>& particle_snapshots,
    std::vector<Logosphere::ShadowTriangle>& gpu_triangles,
    std::unordered_map<uint32_t, int>& particle_to_triangle_slot,
    std::vector<bool>& triangle_slot_active,
    int& next_free_slot,
    bool& bvh_needs_rebuild);

// Thread-local object map removed - all threads now write directly to RenderSystem's unified object_map
// (Fixed-array implementation makes concurrent writes safe via tile-based partitioning)

/**
 * SurfaceRasterizer - Tile-based triangle rasterization
 * 
 * REFACTORED ARCHITECTURE (January 2025)
 * ======================================
 * Originally a monolithic 140+ line method, now decomposed following KISS & UNIX philosophy.
 * Each method does ONE thing well, creating a clear hierarchy of responsibilities.
 * 
 * METHOD HIERARCHY:
 * ----------------
 * rasterize_surfaces (main entry - orchestration)
 * ├── bin_surfaces_to_tiles (Phase 1: Binning)
 * │   └── Assigns surfaces to screen tiles they overlap
 * └── rasterize_tiles (Phase 2: Rendering)
 *     └── process_tile (per tile - cache locality)
 *         └── process_surface_in_tile (per surface - triangle setup)
 *             └── process_pixel (per pixel - atomic operation)
 *                 ├── calculate_pixel_depth (interpolation)
 *                 └── calculate_pixel_color (via RenderSystem callback)
 * 
 * DESIGN BENEFITS:
 * ---------------
 * - Single Responsibility: Each method has ONE clear purpose
 * - Testability: Can unit test at any level of abstraction
 * - Debuggability: Can set breakpoints at specific processing stages
 * - Cache Efficiency: Tile-based approach keeps data in L1/L2 cache
 * - Maintainability: Easy to find and fix specific rendering issues
 * 
 * PERFORMANCE NOTES:
 * -----------------
 * - Tiles are 8x8 pixels (fits in cache line)
 * - Surfaces are binned once, processed many times
 * - Depth uses squared distances to avoid sqrt when possible
 * - Barycentric interpolation for smooth depth/color gradients
 * 
 * HISTORICAL CONTEXT:
 * ------------------
 * Tile-based rendering was pioneered by PowerVR (1996) for embedded GPUs.
 * The Sega Dreamcast (1998) used PowerVR2 with 32x32 tiles.
 * Modern mobile GPUs (Mali, Adreno) still use tile-based architectures.
 * This software implementation follows similar principles for CPU cache efficiency.
 */

// Constructor
SurfaceRasterizer::SurfaceRasterizer()
    : tile_size_(Optimizations::TILE_SIZE)
    , current_frame_index_(0)
    , frame_counter_(0) {

    // Initialize thread workspaces with pre-allocated buffers
    // Skip worker thread workspace allocation when GPU rasterization handles everything
    int total_threads = 1;  // Main thread always needs a workspace
    if constexpr (!Optimizations::USE_GPU_RASTERIZATION) {
        if (Optimizations::USE_PARALLEL_TILES && Optimizations::WORKER_THREAD_COUNT > 0) {
            total_threads += Optimizations::WORKER_THREAD_COUNT;
        }
    }

    thread_workspaces_.resize(total_threads);

    std::cout << "[SURFACE RASTERIZER] Initialized " << total_threads
              << " thread workspace(s)"
              << (Optimizations::USE_GPU_RASTERIZATION ? " (GPU mode — minimal)" : "")
              << "\n";
    
    // Initialize worker_data to safe defaults BEFORE creating threads
    // CRITICAL: Must set total_tiles high so threads stay in spin-wait
    // Otherwise they'll exit the wait loop and crash accessing null pointers
    worker_data_.total_tiles = 0;  // No tiles initially
    worker_data_.next_work_index = 0;
    worker_data_.tile_bins = nullptr;
    worker_data_.tiles_x = 1;  // Avoid division by zero
    
    // Create persistent worker threads if enabled
    // SKIP when GPU rasterization is active — GPURasterizer handles all rendering,
    // these CPU worker threads would be created but never used (waste of 14 threads per instance)
    if constexpr (!Optimizations::USE_GPU_RASTERIZATION) {
        if (Optimizations::USE_PARALLEL_TILES && Optimizations::WORKER_THREAD_COUNT > 0) {
            worker_running_ = true;
            num_worker_threads_ = Optimizations::WORKER_THREAD_COUNT;
            
            // Initialize busy flags for each worker
            worker_busy_ = std::make_unique<std::atomic<bool>[]>(num_worker_threads_);
            for (int i = 0; i < num_worker_threads_; ++i) {
                worker_busy_[i] = false;  // All workers start idle
                worker_threads_.emplace_back(&SurfaceRasterizer::worker_thread_func, this, i);
            }
            std::cout << "[SURFACE RASTERIZER] Created " << num_worker_threads_
                      << " persistent worker threads\n";
        }
    } else {
        std::cout << "[SURFACE RASTERIZER] GPU rasterization active — skipping CPU worker threads\n";
    }

    // Initialize GPU bridge if enabled (Phase II-A integration)
    // SKIP when GPU rasterization is active — GPURasterizer has its own Metal pipeline,
    // this MetalComputeBridge (1 Metal device + 2 command queues) would never be used
    gpu_bridge_ = nullptr;
    if constexpr (Optimizations::USE_GPU_SHADOW_RAYS && !Optimizations::USE_GPU_RASTERIZATION) {
        gpu_bridge_ = new Logosphere::MetalComputeBridge();
        if (!gpu_bridge_->initialize()) {
            std::cout << "[SURFACE RASTERIZER] GPU compute not available, using CPU path\n";
            delete gpu_bridge_;
            gpu_bridge_ = nullptr;
        } else {
            std::cout << "[SURFACE RASTERIZER] GPU compute initialized for shadow rays\n";
        }
    } else if constexpr (Optimizations::USE_GPU_RASTERIZATION) {
        std::cout << "[SURFACE RASTERIZER] GPU rasterization active — skipping MetalComputeBridge\n";
    }
}

// Destructor - shutdown worker threads cleanly
SurfaceRasterizer::~SurfaceRasterizer() {
    // Shutdown worker threads if running
    if (worker_running_) {
        worker_running_ = false;
        // Wake threads by setting a high tile count - they'll see worker_running_ = false
        worker_data_.total_tiles = 0;  // Reset to 0
        
        // No need for notify_all() - threads are spin-waiting
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        std::cout << "[SURFACE RASTERIZER] All worker threads shut down\n";
    }

    // CRITICAL: Wait for all pending GPU work before destroying GPU bridge!
    // Metal callbacks may still be executing and accessing frame buffers
    if (gpu_bridge_) {
        gpu_bridge_->wait_for_completion();  // Blocks until all GPU callbacks finish
        delete gpu_bridge_;
        gpu_bridge_ = nullptr;
    }
}

// Initialize per-frame buffers (Phase IV: Triple buffering for async execution)
void SurfaceRasterizer::init_frame_buffers(int width, int height) {
    std::cout << "[CHECKPOINT 030] SURFACE_RASTERIZER: init_frame_buffers("
              << width << "x" << height << ") starting" << std::endl;

    // CRITICAL: Wait for all GPU work to complete before destroying buffers!
    // GPU callbacks may still be accessing old buffers on background thread
    if (gpu_bridge_) {
        std::cout << "[CHECKPOINT 031] SURFACE_RASTERIZER: Calling gpu_bridge_->wait_for_completion..." << std::endl;
        gpu_bridge_->wait_for_completion();
        std::cout << "[CHECKPOINT 032] SURFACE_RASTERIZER: GPU wait completed" << std::endl;
    }

    std::cout << "[CHECKPOINT 033] SURFACE_RASTERIZER: Locking 3 frame mutexes..." << std::endl;
    // CRITICAL: Lock ALL frame mutexes to prevent GPU callbacks from accessing during reallocation
    // Completion handlers may fire AFTER wait_for_completion() returns
    std::lock_guard<std::mutex> lock0(frame_data_[0].buffer_mutex);
    std::lock_guard<std::mutex> lock1(frame_data_[1].buffer_mutex);
    std::lock_guard<std::mutex> lock2(frame_data_[2].buffer_mutex);
    std::cout << "[CHECKPOINT 034] SURFACE_RASTERIZER: Mutexes locked" << std::endl;

    for (int i = 0; i < 3; i++) {
        // Create pixel buffer for this frame
        frame_data_[i].pixel_buffer = std::make_unique<PixelBuffer>();
        frame_data_[i].pixel_buffer->resize(width, height);

        // Create depth buffer for this frame
        frame_data_[i].depth_buffer = std::make_unique<DepthBuffer>();
        frame_data_[i].depth_buffer->resize(width, height);

        // Reset frame state
        frame_data_[i].pixels.clear();
        frame_data_[i].rays.clear();
        frame_data_[i].ray_results.clear();
        frame_data_[i].lights.clear();
        frame_data_[i].ready_to_write = false;
        frame_data_[i].frame_number = -1;

        std::cout << "[FRAME BUFFER " << i << "] Allocated "
                  << (width * height * sizeof(EnhancedPixel) / 1024) << " KB pixels, "
                  << (width * height * sizeof(float) / 1024) << " KB depth\n";
    }
}

// Wait for all pending GPU work (needed before particle deletion)
void SurfaceRasterizer::wait_for_gpu_completion() {
    if (gpu_bridge_) {
        std::cout << "[SURFACE RASTERIZER] Waiting for GPU to complete before particle deletion...\n";
        gpu_bridge_->wait_for_completion();
        std::cout << "[SURFACE RASTERIZER] GPU work complete, safe to delete particles\n";
    }
}

// Main rasterization entry point with PixelShader - NEW INTERFACE
// Uses modular shading strategy instead of hardcoded callback
int SurfaceRasterizer::rasterize_surfaces(
    const std::deque<SurfaceData>& surfaces,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    SparseObjectMap& object_map,  // RenderSystem's unified object map
    CameraSystem& camera_system,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    int render_width,
    int render_height,
    EngineMetrics* metrics) {

    // =========================================================================
    // TILE-BASED RASTERIZATION
    // Instead of rendering surfaces one by one across the entire screen,
    // we divide the screen into small tiles and process all surfaces
    // within each tile before moving to the next tile.
    // This keeps pixel data in CPU cache for much better performance.
    // =========================================================================
    
    // Define timing types for performance measurement
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;
    
    // Reset UV metrics for this frame
    if (metrics) {
        metrics->uv_calculations_per_frame = 0;
        metrics->uv_calc_time_per_frame = 0.0;
    }

    // object_map is passed from RenderSystem (single source of truth!)
    // It's already cleared in RenderSystem::clear_framebuffer()
    // All threads write to it safely (tile-based partitioning guarantees non-overlapping pixels)

    if (tile_size_ == 0) return 0;  // Tile-based rendering disabled

    // =========================================================================
    // PHASE IV: PER-FRAME BUFFER MANAGEMENT
    // =========================================================================
    // Initialize frame buffers if not yet done (first call or resolution change)
    if (!frame_data_[0].pixel_buffer ||
        frame_data_[0].pixel_buffer->width() != render_width ||
        frame_data_[0].pixel_buffer->height() != render_height) {
        init_frame_buffers(render_width, render_height);
    }

    // CRITICAL: Synchronize frame index with frame counter for triple buffering
    // frame_counter_ is advanced at START of Engine::update() (from N to N+1)
    // But this frame is logically frame N, so use (frame_counter_ - 1) % 3
    // Must use same index for frame_data_, tile_bins_storage_, and surface_cache_
    // Handle negative modulo: ((x % n) + n) % n ensures result is always positive
    current_frame_index_ = ((frame_counter_ - 1) % 3 + 3) % 3;

    // Assign frame number
    current_frame().frame_number = frame_counter_;

    // CRITICAL: Lock mutex before accessing frame buffers AND before clearing pixels/lights!
    // The async GPU callback may still be reading pixels/lights vectors from a previous submission
    {
        std::lock_guard<std::mutex> lock(current_frame().buffer_mutex);

        // Only clear if frame was already presented and GPU callback is done
        // (ready_to_write=false means it was presented and cleared, or never used)
        // If ready_to_write=true, GPU wrote pixels that haven't been presented yet!
        if (!current_frame().ready_to_write) {
            std::cout << "[CLEAR] Clearing frame " << current_frame_index_
                      << " (frame_number=" << current_frame().frame_number << ")\n";

            // Clear pixel/light data structures (GPU callback is done with them)
            current_frame().pixels.clear();
            current_frame().lights.clear();
            current_frame().rays.clear();
            current_frame().ray_results.clear();

            // Clear rendering buffers
            current_frame().pixel_buffer->clear(10, 10, 15, 255, 0);  // Background color
            current_frame().depth_buffer->clear();
        } else {
            std::cout << "[CLEAR] SKIPPING clear of frame " << current_frame_index_
                      << " (GPU pixels not yet presented! ready_to_write=true)\n";
        }
    }

    // DON'T reset ready_to_write here - presentation will reset it after copying!

    // Calculate number of tiles needed
    int tiles_x = (render_width + tile_size_ - 1) / tile_size_;
    int tiles_y = (render_height + tile_size_ - 1) / tile_size_;

    // Tile debug suppressed for performance

    // Tile-based rendering initialized

    // =========================================================================
    // PHASE 1: BINNING - Assign surfaces to tiles they overlap
    // =========================================================================
    auto binning_start = Clock::now();

    // Triple buffering: Select tile bins for current frame (matches surface_cache_)
    // Use (frame_counter_ - 1) because counter was incremented at start of frame
    // Handle negative modulo properly
    int frame_idx = ((frame_counter_ - 1) % 3 + 3) % 3;
    auto& current_tile_bins = tile_bins_storage_[frame_idx];

    // ALWAYS prepare tile bins for current frame (CPU workers use them, not GPU)
    int total_tiles_needed = tiles_x * tiles_y;
    if (current_tile_bins.size() != total_tiles_needed) {
        current_tile_bins.resize(total_tiles_needed);
    }
    // Clear the surface lists in each bin (but keep the vector allocated)
    for (auto& bin : current_tile_bins) {
        bin.surfaces.clear();
    }
    // CRITICAL: Do NOT create a local reference - use member directly!
    // A local reference would go out of scope and leave workers with dangling pointer

    int surfaces_on_screen = 0;
    int total_tile_assignments = 0;

    // Bin surfaces to tiles
    bin_surfaces_to_tiles(surfaces, current_tile_bins, camera_system,
                         tiles_x, tiles_y,
                         surfaces_on_screen, total_tile_assignments);

    auto binning_end = Clock::now();
    double binning_time = Duration(binning_end - binning_start).count();
    if (metrics) {
        metrics->render_projection_time = binning_time;
    }
    LightingMetrics::get().binning_time = binning_time;

    // =========================================================================
    // PHASE 2: RENDERING - Process each tile independently
    // =========================================================================
    auto raster_tiles_start = Clock::now();

    // COLLECT LIGHTS ONCE
    std::vector<const Particle*> lights;
    for (const auto& particle : particles) {
        if (particle.is_light_source) {
            lights.push_back(&particle);
        }
    }

    // CONVERT PARTICLES TO GPU TRIANGLES ONCE PER FRAME
    if constexpr (Optimizations::USE_GPU_SHADOW_RAYS) {
        if (gpu_bridge_) {
            std::vector<ParticleGeometrySnapshot> particle_snapshots;
            particle_snapshots.reserve(particles.size());
            for (const auto& p : particles) {
                if (p.particle_id == 0) continue;
                particle_snapshots.push_back({
                    p.particle_id,
                    p.is_light_source,
                    static_cast<uint8_t>(p.r * 255.0f),
                    static_cast<uint8_t>(p.g * 255.0f),
                    static_cast<uint8_t>(p.b * 255.0f),
                    static_cast<uint8_t>(p.a * 255.0f),
                    p.GetSurfaces()
                });
            }

            convert_particles_to_gpu_triangles(particle_snapshots, gpu_triangle_cache_,
                                               particle_to_triangle_slot_,
                                               triangle_slot_active_,
                                               next_free_slot_,
                                               bvh_needs_rebuild_);

            // BUILD OR REFIT GPU BVH
            if (bvh_needs_rebuild_ || frames_since_rebuild_ >= 100) {
                gpu_triangle_bvh_.build(gpu_triangle_cache_);
                bvh_needs_rebuild_ = false;
                frames_since_rebuild_ = 0;
            } else {
                gpu_triangle_bvh_.refit(gpu_triangle_cache_);
                frames_since_rebuild_++;
            }

            // UPLOAD BVH TO GPU
            if (gpu_triangle_bvh_.is_ready()) {
                gpu_bridge_->upload_bvh(
                    gpu_triangle_bvh_.get_nodes(),
                    gpu_triangle_bvh_.get_node_count()
                );
            }
        }
    }
    int pixels_processed = rasterize_tiles(
        current_tile_bins,  // Use frame-specific tile bins (triple buffered)
        pixel_buffer,  // FIXED: Pass render_buffer_ (the actual output buffer), NOT frame buffer
        *current_frame().depth_buffer,  // Use frame's own depth buffer (Phase IV)
        object_map,  // Pass unified object_map
        camera_system, light_system, pixel_shader,  // Use PixelShader
        particles, shadow_bvh, lights,  // Pass pre-collected lights
        &gpu_triangle_cache_,  // Cached GPU triangles (Phase II-B)
        render_width, render_height,
        tiles_x, tiles_y,
        metrics
    );
    std::cout << "[RASTERIZE] rasterize_tiles returned\n" << std::flush;
    
    // Pixel processing complete
    
    auto raster_tiles_end = Clock::now();
    double raster_time = Duration(raster_tiles_end - raster_tiles_start).count();
    if (metrics) {
        metrics->pixels_drawn = pixels_processed;
        metrics->pixels_tested = pixels_processed;  // For tile rendering, tested = drawn
        metrics->render_rasterization_time = raster_time;
    }

    // PROFILING: Record rasterization time for granular report
    LightingMetrics::get().rasterization_time = raster_time;

    // object_map already contains all object IDs (written directly by all threads)
    // No merge needed - tile-based partitioning guaranteed non-overlapping writes

    return pixels_processed;
}


// PHASE 1: BINNING - Assign surfaces to tiles they overlap
// This pre-processing step determines which surfaces affect which tiles,
// allowing us to process tiles independently with good cache locality
void SurfaceRasterizer::bin_surfaces_to_tiles(
    const std::deque<SurfaceData>& surfaces,
    std::vector<TileBin>& tile_bins,
    CameraSystem& camera_system,
    int tiles_x,
    int tiles_y,
    int& surfaces_on_screen,
    int& total_assignments) {

    surfaces_on_screen = 0;
    total_assignments = 0;

    static int binning_call = 0;
    bool debug = (binning_call++ < 3);

    if (debug) {
        std::cout << "[BINNING START] surfaces=" << surfaces.size()
                  << " tile_bins=" << tile_bins.size()
                  << " tiles_x=" << tiles_x << " tiles_y=" << tiles_y << "\n";
    }

    int surface_idx = 0;
    for (const auto& surf_data : surfaces) {
        // Project triangle to screen
        const float (&vertices)[3][3] = (const float (&)[3][3])surf_data.surface.vertices;
        auto projected = camera_system.project_triangle(vertices);

        // Skip off-screen surfaces (frustum culling not yet implemented in camera_system.cpp)
        // Once frustum culling is implemented, these will be culled earlier and never reach here
        if (!projected.on_screen) {
            surface_idx++;
            continue;
        }
        surfaces_on_screen++;

        // Find which tiles this surface overlaps
        int tile_min_x = std::max(0, projected.min_x / tile_size_);
        int tile_max_x = std::min(tiles_x - 1, projected.max_x / tile_size_);
        int tile_min_y = std::max(0, projected.min_y / tile_size_);
        int tile_max_y = std::min(tiles_y - 1, projected.max_y / tile_size_);

        // Add surface to each overlapping tile's bin
        for (int ty = tile_min_y; ty <= tile_max_y; ++ty) {
            for (int tx = tile_min_x; tx <= tile_max_x; ++tx) {
                int tile_idx = ty * tiles_x + tx;

                // CRITICAL BOUNDS CHECK
                if (tile_idx >= tile_bins.size()) {
                    std::cerr << "[BINNING ERROR] tile_idx=" << tile_idx
                              << " >= tile_bins.size()=" << tile_bins.size()
                              << " (surface " << surface_idx << "/" << surfaces.size()
                              << " at tiles tx=" << tx << " ty=" << ty << ")\n";
                    continue;  // Skip this assignment
                }

                // Log suspicious assignments
                if (debug && surface_idx < 5 && total_assignments < 20) {
                    std::cout << "[BINNING] surface " << surface_idx
                              << " -> tile[" << tile_idx << "] (tx=" << tx << " ty=" << ty << ")"
                              << " bins_size=" << tile_bins.size() << "\n";
                }

                // CRASH PROTECTION: Verify vector integrity before push_back
                auto& bin = tile_bins[tile_idx];
                if (bin.surfaces.capacity() > 0 && bin.surfaces.data() == nullptr) {
                    std::cerr << "[BINNING CRASH] tile[" << tile_idx << "] has capacity="
                              << bin.surfaces.capacity() << " but null data pointer!\n";
                    std::cerr << "[BINNING CRASH] This indicates heap corruption from previous frame\n";
                    continue;
                }

                bin.surfaces.push_back(&surf_data);
                total_assignments++;
            }
        }
        surface_idx++;
    }

    if (debug) {
        std::cout << "[BINNING END] surfaces_on_screen=" << surfaces_on_screen
                  << " total_assignments=" << total_assignments << "\n";
    }
}

// PHASE 2: RASTERIZATION - Process tiles independently
// Iterates through all tiles, delegating actual rendering to process_tile
// This method manages the overall tile grid and accumulates metrics
int SurfaceRasterizer::rasterize_tiles(
    const std::vector<TileBin>& tile_bins,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    SparseObjectMap& object_map,  // RenderSystem's unified object map (thread-safe!)
    CameraSystem& camera_system,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    const std::vector<const Particle*>& lights,  // Pre-collected lights (vector for cache locality)
    const std::vector<Logosphere::ShadowTriangle>* gpu_triangles,  // Cached GPU triangles (Phase II-B)
    int render_width,
    int render_height,
    int tiles_x,
    int tiles_y,
    EngineMetrics* metrics) {
    
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    // Publish the active camera so calculate_pixel_depth can route
    // through its projection system. Set BEFORE any worker thread
    // starts — they acquire via the release fence below.
    active_camera_system_ = &camera_system;

    // Get camera position for depth calculation
    float cam_x, cam_y, cam_z;
    camera_system.get_position(cam_x, cam_y, cam_z);

    int tiles_processed = 0;
    int pixels_processed = 0;
    
    // Track per-pixel timing
    double total_depth_time = 0.0;
    double total_lighting_time = 0.0;
    
    // Setup for direct tile processing (no work_items collection)
    int total_tiles = tiles_x * tiles_y;
    
    if (worker_running_ && total_tiles > 0) {
        // Use persistent worker thread
        // CRITICAL: Set all data BEFORE setting total_tiles
        // Worker threads check total_tiles to know when work is available
        worker_data_.tile_bins = &tile_bins;  // Use frame-specific tile bins (triple buffered)
        worker_data_.pixel_buffer = &pixel_buffer;
        worker_data_.depth_buffer = &depth_buffer;
        worker_data_.object_map = &object_map;  // RenderSystem's unified object map (thread-safe!)
        worker_data_.camera_system = &camera_system;
        worker_data_.light_system = &light_system;
        worker_data_.pixel_shader = &pixel_shader;
        worker_data_.particles = &particles;
        worker_data_.shadow_bvh = shadow_bvh;
        worker_data_.lights = lights;  // Copy lights into WorkData (owns them)
        worker_data_.gpu_triangles = gpu_triangles;  // Cached GPU triangles (Phase II-B)
        worker_data_.render_width = render_width;
        worker_data_.render_height = render_height;
        worker_data_.cam_x = cam_x;
        worker_data_.cam_y = cam_y;
        worker_data_.cam_z = cam_z;
        worker_data_.metrics = metrics;
        worker_data_.next_work_index = 0;
        worker_data_.pixels_done = 0;
        worker_data_.tiles_x = tiles_x;
        
        // Memory barrier to ensure all writes are visible before setting total_tiles
        std::atomic_thread_fence(std::memory_order_release);
        
        // Set total_tiles LAST - this signals workers that work is ready
        worker_data_.total_tiles = total_tiles;
        
        // Signal ALL worker threads to start
        // Workers detect work by checking next_work_index < total_tiles

        if (!Optimizations::MAIN_THREAD_NO_RENDER) {
            // Main thread also processes tiles (extracted for clarity)
            main_thread_process_tiles(
                pixel_buffer, depth_buffer,
                camera_system, light_system, pixel_shader,
                particles, shadow_bvh, lights,
                render_width, render_height,
                cam_x, cam_y, cam_z,
                tiles_x, total_tiles,
                total_depth_time, total_lighting_time,
                metrics
            );
        }
        
        // Wait for worker threads to finish all tiles
        // When all tiles have been grabbed (next_work_index >= total_tiles),
        // workers are either processing their last tile or back in spin-wait
        while (worker_data_.next_work_index < total_tiles) {
            if (Optimizations::MAIN_THREAD_NO_RENDER) {
                // Sleep for 100 microseconds instead of yielding
                // This reduces CPU usage and may improve worker thread performance
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                // If main thread also renders, yield is appropriate
                std::this_thread::yield();
            }
        }
        
        tiles_processed = total_tiles;

        // Wait for all workers to complete (extracted for clarity)
        wait_for_workers_completion();

        // Phase IV: ASYNC GPU BATCH PROCESSING (triple buffering)
        if constexpr (Optimizations::USE_GPU_SHADOW_RAYS) {
            if (!current_frame().pixels.empty()) {
                std::cout << "[RASTER DEBUG] pixels=" << current_frame().pixels.size()
                          << " lights=" << lights.size()
                          << " gpu_bridge=" << (gpu_bridge_ ? "yes" : "no") << "\n";

                // Dispatch async GPU for current frame (returns immediately)
                if (gpu_bridge_ && !lights.empty()) {
                    std::cout << "[RASTER DEBUG] Calling process_gpu_shadow_batch\n";
                    process_gpu_shadow_batch(lights, particles, shadow_bvh);
                }
                // No lighting: write pixels as black (lighting=0), then mark ready
                else {
                    write_frame_pixels(current_frame());  // Write collected pixels to buffer
                    current_frame().ready_to_write = true;
                }

                // Update pixel count from collected pixels
                pixels_processed = static_cast<int>(current_frame().pixels.size());
            }
        } else {
            // CPU path: use worker pixel count
            pixels_processed = worker_data_.pixels_done.load();
        }

        // Reset in safe order
        worker_data_.total_tiles = 0;  // Signal no more work
        worker_data_.next_work_index = 0;  // Reset for next frame
        worker_data_.tile_bins = nullptr;
        worker_data_.tiles_x = 0;
    } else {
        // Sequential processing (no threading or no work)

        for (int tile_idx = 0; tile_idx < total_tiles; ++tile_idx) {
            const auto& tile_bin = tile_bins[tile_idx];
            if (tile_bin.surfaces.empty()) continue;  // Skip empty tiles

            int tile_x = tile_idx % tiles_x;
            int tile_y = tile_idx / tiles_x;

            double local_depth_time = 0.0;
            double local_lighting_time = 0.0;

            int tile_pixels = process_tile_simple(
                tile_bin, tile_x, tile_y,
                *current_frame().pixel_buffer,  // Use frame's own buffer (Phase IV)
                *current_frame().depth_buffer,  // Use frame's own buffer (Phase IV)
                camera_system, light_system, pixel_shader,
                particles, shadow_bvh, lights,
                render_width, render_height,
                cam_x, cam_y, cam_z,
                local_depth_time, local_lighting_time,
                metrics
            );
            
            pixels_processed += tile_pixels;
            total_depth_time += local_depth_time;
            total_lighting_time += local_lighting_time;
        }
        tiles_processed = total_tiles;
    }

    // PHASE IV: Find and present oldest completed frame (for async GPU)
    // Check all 3 frames and find the oldest one that's ready to write
    int oldest_ready_frame = -1;
    int oldest_frame_number = INT_MAX;

    for (int i = 0; i < 3; i++) {
        if (frame_data_[i].ready_to_write && frame_data_[i].frame_number >= 0) {
            if (frame_data_[i].frame_number < oldest_frame_number) {
                oldest_frame_number = frame_data_[i].frame_number;
                oldest_ready_frame = i;
            }
        }
    }

    std::cout << "[PRESENT] oldest_ready=" << oldest_ready_frame
              << " current=" << current_frame_index_
              << " ready_flags=[" << frame_data_[0].ready_to_write
              << "," << frame_data_[1].ready_to_write
              << "," << frame_data_[2].ready_to_write << "]"
              << " frame_numbers=[" << frame_data_[0].frame_number
              << "," << frame_data_[1].frame_number
              << "," << frame_data_[2].frame_number << "]\n";

    // Present oldest ready frame (NEVER present current frame in async mode!)
    if (oldest_ready_frame >= 0) {
        // CRITICAL: Lock mutex before reading frame buffer (Metal thread may be writing!)
        std::lock_guard<std::mutex> lock(frame_data_[oldest_ready_frame].buffer_mutex);

        // Async path: present completed frame from GPU callback
        pixel_buffer.copy_from(*frame_data_[oldest_ready_frame].pixel_buffer);

        frame_data_[oldest_ready_frame].ready_to_write = false;  // Mark as presented
    } else {
        // NO FALLBACK in async mode - don't present the current frame (it was just cleared!)
        // First few frames will be black until GPU pipeline fills up
        std::cout << "[PRESENT] Skipping (no ready frames yet)\n";
    }

    // Frame index rotation no longer needed - synchronized with frame_counter_ at start
    // current_frame_index_ = frame_counter_ % 3 (set at line 262)

    if (metrics) {
        // Per-pixel timing removed for performance (was killing FPS!)
        // These were causing 368k clock calls per frame
        metrics->pixel_depth_time = 0.0;  // total_depth_time;
        metrics->pixel_lighting_time = 0.0;  // total_lighting_time;
        
        // Calculate other pixel processing time (edge tests, UV calc, etc)
        double total_tile_time = metrics->render_rasterization_time;
        double other_pixel_time = total_tile_time - total_depth_time - total_lighting_time;
        metrics->pixel_edge_test_time = other_pixel_time; // Store in edge test for now
        
        // Copy accumulated UV time to the pixel UV field
        metrics->pixel_uv_calc_time = metrics->uv_calc_time_per_frame;
        
    }
    
    return pixels_processed;
}

// TILE PROCESSOR - Handles all surfaces within a single 8x8 tile
// Calculates tile bounds and delegates surface processing
// Key to cache efficiency: all pixels in tile are spatially close
int SurfaceRasterizer::process_tile(
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
    EngineMetrics* metrics) {
    
    // Calculate tile bounds in screen space
    int tile_screen_x = tile_x * tile_size_;
    int tile_screen_y = tile_y * tile_size_;
    int tile_screen_x_end = std::min(tile_screen_x + tile_size_, render_width);
    int tile_screen_y_end = std::min(tile_screen_y + tile_size_, render_height);
    
    int pixels_processed = 0;
    
    // Process all surfaces for this tile
    for (const auto* surf_data_ptr : tile_bin.surfaces) {
        pixels_processed += process_surface_in_tile(
            *surf_data_ptr,
            tile_screen_x, tile_screen_y,
            tile_screen_x_end, tile_screen_y_end,
            pixel_buffer, depth_buffer,
            camera_system, light_system, pixel_shader,
            cam_x, cam_y, cam_z,
            total_depth_time, total_lighting_time,
            metrics
        );
    }
    
    return pixels_processed;
}

// SIMPLE TILE PROCESSOR - Process tile with inline shadow testing
// Each tile can be processed independently by a thread
// Shadows are tested inline per pixel for thread safety
int SurfaceRasterizer::process_tile_simple(
    const TileBin& tile_bin,
    int tile_x,
    int tile_y,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    CameraSystem& camera_system,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    const std::vector<const Particle*>& lights,  // Pre-collected lights (vector for cache locality)
    int render_width,
    int render_height,
    float cam_x, float cam_y, float cam_z,
    double& total_depth_time,
    double& total_lighting_time,
    EngineMetrics* metrics) {
    
    // Determine which thread workspace to use
    // Each thread gets a unique ID that persists for the lifetime of the thread
    static std::atomic<int> next_thread_id{0};
    static thread_local int thread_id = -1;
    if (thread_id == -1) {
        // First time THIS THREAD calls this function, assign an ID
        // This happens exactly once per thread
        thread_id = next_thread_id.fetch_add(1);
    }
    
    // Get the pre-allocated workspace for this thread
    if (thread_id >= thread_workspaces_.size()) {
        // Fallback: shouldn't happen but safety check
        std::cerr << "[ERROR] Thread " << thread_id << " exceeds workspace count\n";
        return 0;
    }
    
    ThreadWorkspace& workspace = thread_workspaces_[thread_id];
    workspace.reset();  // Clear but keep capacity

    // Single-pass rasterization with immediate shading and inline segment sampling
    rasterize_tile_single_pass(
        tile_bin, tile_x, tile_y,
        pixel_buffer, depth_buffer,
        camera_system,
        lights,
        particles,
        shadow_bvh,
        worker_data_.gpu_triangles,  // Cached GPU triangles (Phase II-B)
        render_width, render_height,
        cam_x, cam_y, cam_z,
        worker_data_.object_map,  // RenderSystem's unified object map (thread-safe!)
        metrics  // Pass metrics for profiling
    );
    return 1; // TODO: Return actual pixel count
}

// SURFACE PROCESSOR - Rasterizes one triangle within tile bounds
// Projects the triangle, clips to tile, and processes scanlines
// Delegates actual pixel operations to process_pixel
int SurfaceRasterizer::process_surface_in_tile(
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
    EngineMetrics* metrics) {
    
    // Define timing types for this function
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;
    
    const auto& surface = surf_data.surface;

    // Project triangle
    const float (&vertices)[3][3] = (const float (&)[3][3])surface.vertices;
    auto projected = camera_system.project_triangle(vertices);
    
    int pixels_processed = 0;

    // UV Gradient Optimization: Pre-compute gradients for the tile (extracted for clarity)
    UVGradient gradient = compute_uv_gradient_for_tile(
        tile_screen_x, tile_screen_y,
        tile_screen_x_end, tile_screen_y_end,
        projected, camera_system
    );
    
    // Process only pixels within this tile
    int y_start = std::max(tile_screen_y, projected.min_y);
    int y_end = std::min(tile_screen_y_end - 1, projected.max_y);
    
    for (int y = y_start; y <= y_end; ++y) {
        // For triangles, always use bounding box
        int x_start = projected.min_x;
        int x_end = projected.max_x;
        
        // Clip to tile bounds
        x_start = std::max(tile_screen_x, x_start);
        x_end = std::min(tile_screen_x_end - 1, x_end);
        
        // Process pixels: SIMD or scalar path
        if (Optimizations::USE_SIMD) {
            // SIMD path: Process 8 pixels at once (extracted for clarity)
            pixels_processed += process_scanline_simd(
                y, x_start, x_end,
                projected, vertices, surf_data,
                pixel_buffer, depth_buffer,
                light_system, pixel_shader,
                cam_x, cam_y, cam_z,
                metrics
            );
        } else {
            // Scalar path: Process 1 pixel at a time (extracted for clarity)
            pixels_processed += process_scanline_scalar(
                y, x_start, x_end,
                tile_screen_x, tile_screen_y,
                gradient, projected, vertices, surf_data,
                pixel_buffer, depth_buffer,
                camera_system, light_system, pixel_shader,
                cam_x, cam_y, cam_z,
                metrics
            );
        }
    }
    
    return pixels_processed;
}

// PIXEL PROCESSOR - Atomic pixel operation
// Calculates depth, gets lighting, performs depth test, and writes pixel
// This is the innermost loop - optimized for performance
bool SurfaceRasterizer::process_pixel(
    int x, int y,
    float u, float v,
    const float vertices[3][3],
    const SurfaceData& surf_data,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    float cam_x, float cam_y, float cam_z,
    EngineMetrics* metrics) {
    
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;
    
    // Calculate depth at this pixel (NO TIMING IN HOT PATH!)
    float depth = calculate_pixel_depth(u, v, vertices, cam_x, cam_y, cam_z);
    
    // TODO[CLEAN-003]: Debug code for shader UV tracking - remove after testing
    
    // EARLY-Z OPTIMIZATION: Test depth BEFORE shading to eliminate overdraw!
    // This is the key change - only shade pixels that will be visible
    
    if (Optimizations::USE_EARLY_Z) {
        // Early-Z path: Test depth first
        if (!depth_buffer.test_and_set(x, y, depth, Optimizations::USE_SQUARED_DEPTH)) {
            // Pixel failed depth test - skip shading entirely!
            if (metrics) {
                metrics->pixels_depth_rejected++;
            }
            return false;
        }
    }
    
    // Either Early-Z passed or we're not using Early-Z
    // Calculate lighting at this pixel (NO TIMING IN HOT PATH!)
    // Use the PixelShader to calculate color
    auto color = pixel_shader.shade(
        surf_data.particle_r, surf_data.particle_g, surf_data.particle_b, surf_data.particle_a,
        surf_data.is_light_source, surf_data.emission_strength,
        surf_data.particle_index, surf_data.surface_index,
        u, v, light_system
    );
    
    // Track that we shaded this pixel (for overdraw metrics)
    if (metrics) {
        metrics->pixels_shaded++;
    }
    
    // Late depth test (if not using Early-Z)
    if (!Optimizations::USE_EARLY_Z) {
        if (!depth_buffer.test_and_set(x, y, depth, Optimizations::USE_SQUARED_DEPTH)) {
            // Pixel failed depth test after shading (overdraw!)
            if (metrics) {
                metrics->pixels_depth_rejected++;
            }
            return false;
        }
    }
    
    // Set pixel with object tracking
    // Encode both particle ID and surface index in object_id
    int encoded_id = ObjectID::encode_particle_surface(
        surf_data.particle_index, 
        surf_data.surface_index
    );
    
    pixel_buffer.set_pixel_with_object(
        x, y, color.r, color.g, color.b,
        static_cast<uint8_t>(surf_data.particle_a * 255),
        encoded_id
    );
    
    // Update thread-local sparse object map (only if depth test passed)
    // This is thread-safe because each thread has its own map

    // Legacy process_pixel function - not used in current rendering path
    // Object IDs are now written in write_lit_pixel (single-pass path)
    // This function remains for potential future use or debugging

    FRAME_COUNT_PIXEL;
    return true;
}

// DEPTH CALCULATOR - Interpolates depth using barycentric coordinates.
// Routes through CameraSystem::compute_depth so each projection gets
// the right metric: true Euclidean distance for perspective, orthographic
// view-direction depth for parallel projections (iso, bird's-eye,
// cabinet). Using radial distance to the camera point for a parallel
// projection is wrong — it makes depth ordering depend on camera XY
// even though a parallel projection's view direction is fixed, and it
// can flip the "which of two stacked particles is in front" ordering
// when the camera sits near the scene plane (the bike_viewer case).
// See tests/test_iso_depth_ordering.cpp for the locked-in contract.
//
// active_camera_system_ is set at the top of rasterize_surfaces and
// rasterize_tile_single_pass. The fallback path (active_camera_system_
// nullptr) preserves the old Euclidean behavior so legacy callers do
// not change meaning.
float SurfaceRasterizer::calculate_pixel_depth(
    float u, float v,
    const float vertices[3][3],
    float cam_x, float cam_y, float cam_z) const {

    float corner_depths[3];
    if (active_camera_system_) {
        for (int c = 0; c < 3; ++c) {
            corner_depths[c] = active_camera_system_->compute_depth(
                vertices[c][0], vertices[c][1], vertices[c][2]);
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            float dx = vertices[c][0] - cam_x;
            float dy = vertices[c][1] - cam_y;
            float dz = vertices[c][2] - cam_z;
            if (Optimizations::USE_SQUARED_DEPTH) {
                corner_depths[c] = RasterizationMath::distance_squared_3d(dx, dy, dz);
            } else {
                corner_depths[c] = RasterizationMath::distance_3d(dx, dy, dz);
            }
        }
    }

    // Interpolate depth using barycentric coordinates. The projection-
    // provided metric is linear in world position, so this interpolation
    // is exact (no hidden sqrt-in-the-middle) for parallel projections.
    float w = 1.0f - u - v;
    return RasterizationMath::barycentric_interpolate(
        corner_depths[0], corner_depths[1], corner_depths[2],
        w, u, v
    );
}

// =============================================================================
// SCANLINE PROCESSORS - SIMD and Scalar paths separated for clarity
// =============================================================================

// Process a single scanline using SIMD (8 pixels at once)
// Extracted from process_surface_in_tile to follow Single Responsibility Principle
int SurfaceRasterizer::process_scanline_simd(
    int y, int x_start, int x_end,
    const CameraSystem::ProjectedTriangle& projected,
    const float vertices[3][3],
    const SurfaceData& surf_data,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    float cam_x, float cam_y, float cam_z,
    EngineMetrics* metrics) {

    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    int pixels_processed = 0;

    // Get or compute edge equations
    CameraSystem::EdgeEquation edges[3];
    if (projected.edges_computed) {
        // Use pre-computed edges
        for (int i = 0; i < 3; ++i) {
            edges[i] = projected.edges[i];
        }
    } else {
        // Compute edge equations for the triangle
        // Must match the formula in camera_system.cpp lines 286-288
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;  // Next vertex

            float x0 = projected.screen_corners[i][0];
            float y0 = projected.screen_corners[i][1];
            float x1 = projected.screen_corners[j][0];
            float y1 = projected.screen_corners[j][1];

            // Edge equation: (y1-y0)*x - (x1-x0)*y + (x1*y0 - x0*y1) = 0
            edges[i].a = y1 - y0;
            edges[i].b = x0 - x1;
            edges[i].c = x1*y0 - x0*y1;
        }

        // Check winding order: compute signed area
        // If area is negative, triangle is clockwise and we need to flip edges
        float area = 0;
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            area += projected.screen_corners[i][0] * projected.screen_corners[j][1];
            area -= projected.screen_corners[j][0] * projected.screen_corners[i][1];
        }
    }

    // SIMD path needs edges flipped (empirically determined)
    // The pre-computed edges from camera_system have opposite sign convention
    // than what the SIMD test_inside function expects
    for (int i = 0; i < 3; ++i) {
        edges[i].a = -edges[i].a;
        edges[i].b = -edges[i].b;
        edges[i].c = -edges[i].c;
    }

    // Process 8 pixels at once
    for (int x = x_start; x <= x_end; x += 8) {
        // Prepare x coordinates for 8 pixels
        float pixel_x_array[8];
        int batch_size = std::min(8, x_end - x + 1);
        for (int i = 0; i < batch_size; ++i) {
            pixel_x_array[i] = float(x + i);
        }
        // Fill rest with dummy values (won't be used)
        for (int i = batch_size; i < 8; ++i) {
            pixel_x_array[i] = float(x);
        }

        // Evaluate all three edges for 8 pixels
        float edge_values[3][8];
        for (int edge_idx = 0; edge_idx < 3; ++edge_idx) {
            const auto& edge = edges[edge_idx];  // Always use our corrected edges
            SIMD::evaluate_edge(
                edge.a,
                edge.b,
                edge.c,
                pixel_x_array,
                float(y),
                edge_values[edge_idx]
            );
        }

        // Test which pixels are inside
        uint8_t inside_mask = SIMD::test_inside(
            edge_values[0],
            edge_values[1],
            edge_values[2]
        );

        // If we have pixels inside, calculate UV for all 8 at once
        if (inside_mask != 0) {
            // Prepare triangle vertices for UV calculation
            float tri_x[3] = {
                float(projected.screen_corners[0][0]),
                float(projected.screen_corners[1][0]),
                float(projected.screen_corners[2][0])
            };
            float tri_y[3] = {
                float(projected.screen_corners[0][1]),
                float(projected.screen_corners[1][1]),
                float(projected.screen_corners[2][1])
            };

            // Calculate all UV coordinates at once
            float uv_u[8], uv_v[8];
            auto uv_start = Clock::now();
            uint8_t uv_valid = SIMD::compute_uv_batch(
                pixel_x_array, float(y), tri_x, tri_y, uv_u, uv_v
            );
            auto uv_end = Clock::now();

            // Count UV calculations for metrics
            if (metrics) {
                int uv_count = __builtin_popcount(inside_mask & uv_valid);
                metrics->uv_calculations_per_frame += uv_count;
                metrics->uv_calc_time_per_frame += Duration(uv_end - uv_start).count();
            }

            // Process each pixel that's inside and has valid UV
            uint8_t valid_pixels = inside_mask & uv_valid;
            for (int i = 0; i < batch_size; ++i) {
                if (valid_pixels & (1 << i)) {
                    // Process the pixel with pre-computed UV
                    if (process_pixel(x + i, y, uv_u[i], uv_v[i], vertices, surf_data,
                                    pixel_buffer, depth_buffer, light_system, pixel_shader,
                                    cam_x, cam_y, cam_z, metrics)) {
                        pixels_processed++;
                    }
                }
            }
        }
    }

    return pixels_processed;
}

// Process a single scanline using scalar path (1 pixel at a time)
// Extracted from process_surface_in_tile to follow Single Responsibility Principle
int SurfaceRasterizer::process_scanline_scalar(
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
    EngineMetrics* metrics) {

    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    int pixels_processed = 0;

    // Scalar fallback path
    for (int x = x_start; x <= x_end; ++x) {
        // Check if pixel is inside the triangle and get UV coordinates
        float u, v;
        bool uv_success = false;

        // Time UV calculation (accumulated per frame, not per pixel)
        auto uv_start = Clock::now();

        if (Optimizations::USE_UV_GRADIENT_TILES && gradient.valid) {
            // Use gradient-based UV calculation (just 2 adds!)
            int dx = x - tile_screen_x;
            int dy = y - tile_screen_y;
            u = gradient.u_base + dx * gradient.du_dx + dy * gradient.du_dy;
            v = gradient.v_base + dx * gradient.dv_dx + dy * gradient.dv_dy;

            // Still need to check if we're inside the triangle
            // Quick check: UV coordinates should be in [0,1] range and sum to <= 1
            uv_success = (u >= 0.0f && v >= 0.0f && u + v <= 1.0f);
        } else {
            // Fall back to standard barycentric calculation
            uv_success = camera_system.pixel_to_triangle_uv(x, y, projected, u, v);
        }

        auto uv_end = Clock::now();

        if (metrics) {
            metrics->uv_calculations_per_frame++;
            metrics->uv_calc_time_per_frame += Duration(uv_end - uv_start).count();
        }

        if (!uv_success) continue;

        // TODO[CLEAN-002]: Debug code for UV coordinate verification - remove after testing

        // Process the pixel
        if (process_pixel(x, y, u, v, vertices, surf_data,
                        pixel_buffer, depth_buffer, light_system, pixel_shader,
                        cam_x, cam_y, cam_z, metrics)) {
            pixels_processed++;
        }
    }

    return pixels_processed;
}

// Compute UV gradient for tile-based optimization
// Extracted from process_surface_in_tile to follow Single Responsibility Principle
// Pure calculation function - computes screen-space UV gradients for faster pixel processing
SurfaceRasterizer::UVGradient SurfaceRasterizer::compute_uv_gradient_for_tile(
    int tile_screen_x,
    int tile_screen_y,
    int tile_screen_x_end,
    int tile_screen_y_end,
    const CameraSystem::ProjectedTriangle& projected,
    CameraSystem& camera_system) {

    UVGradient gradient = {0, 0, 0, 0, 0, 0, false};

    if (!Optimizations::USE_UV_GRADIENT_TILES) {
        return gradient;  // Feature disabled
    }

    // Compute UV gradients using three reference points
    // Use the triangle centroid as reference to ensure we're inside
    float u0, v0, u1, v1, u2, v2;

    // Try to find a good reference point inside the triangle
    // Use the center of the triangle's bounding box clipped to tile
    int ref_x = (std::max(tile_screen_x, projected.min_x) +
                 std::min(tile_screen_x_end - 1, projected.max_x)) / 2;
    int ref_y = (std::max(tile_screen_y, projected.min_y) +
                 std::min(tile_screen_y_end - 1, projected.max_y)) / 2;

    // Get UV at three points to compute gradients
    bool uv0 = camera_system.pixel_to_triangle_uv(ref_x, ref_y, projected, u0, v0);
    bool uv1 = camera_system.pixel_to_triangle_uv(ref_x + 1, ref_y, projected, u1, v1);
    bool uv2 = camera_system.pixel_to_triangle_uv(ref_x, ref_y + 1, projected, u2, v2);

    if (uv0 && uv1 && uv2) {
        // Store the reference point
        gradient.u_base = u0;
        gradient.v_base = v0;
        gradient.du_dx = u1 - u0;  // Change in u per pixel horizontally
        gradient.dv_dx = v1 - v0;  // Change in v per pixel horizontally
        gradient.du_dy = u2 - u0;  // Change in u per pixel vertically
        gradient.dv_dy = v2 - v0;  // Change in v per pixel vertically
        gradient.valid = true;

        // Adjust base to tile origin for easier calculation
        int dx_to_origin = tile_screen_x - ref_x;
        int dy_to_origin = tile_screen_y - ref_y;
        gradient.u_base += dx_to_origin * gradient.du_dx + dy_to_origin * gradient.du_dy;
        gradient.v_base += dx_to_origin * gradient.dv_dx + dy_to_origin * gradient.dv_dy;
    }

    return gradient;
}

// Wait for all worker threads to complete tile processing
// Extracted from rasterize_tiles to follow Single Responsibility Principle
// Handles synchronization and profiling of worker completion
void SurfaceRasterizer::wait_for_workers_completion() {
    // PROFILING: Measure synchronization wait time
    auto sync_start = std::chrono::high_resolution_clock::now();

    // Wait until all tiles have been claimed
    while (worker_data_.next_work_index < worker_data_.total_tiles) {
        std::this_thread::yield();
    }

    auto claim_wait_end = std::chrono::high_resolution_clock::now();

    // Now wait for all workers to finish processing
    // Check the busy flags of all workers
    bool all_idle = false;
    while (!all_idle) {
        all_idle = true;
        for (int i = 0; i < num_worker_threads_; i++) {
            if (worker_busy_[i].load()) {
                all_idle = false;
                break;
            }
        }
        if (!all_idle) {
            std::this_thread::yield();
        }
    }

    // CRITICAL: Memory fence to ensure all worker writes are visible
    // This ensures no stale pointer reads in subsequent frames
    std::atomic_thread_fence(std::memory_order_acquire);

    auto sync_end = std::chrono::high_resolution_clock::now();

    // Calculate synchronization times
    double claim_wait_ms = std::chrono::duration<double, std::milli>(claim_wait_end - sync_start).count();
    double finish_wait_ms = std::chrono::duration<double, std::milli>(sync_end - claim_wait_end).count();
    double total_sync_ms = std::chrono::duration<double, std::milli>(sync_end - sync_start).count();

    // Print every 60th frame
    static int sync_print_counter = 0;
    if (++sync_print_counter % 60 == 0) {
        std::cout << "[SYNC OVERHEAD] Claim wait: " << claim_wait_ms
                  << "ms, Finish wait: " << finish_wait_ms
                  << "ms, Total: " << total_sync_ms << "ms" << std::endl;
    }
}

// Get next tile work chunk using work stealing or strided access
// Extracted from worker_thread_func to follow Single Responsibility Principle
// Handles dynamic work distribution across worker threads
SurfaceRasterizer::TileWorkChunk SurfaceRasterizer::get_next_tile_work() {
    // Work stealing: grab multiple tiles if enabled
    int chunk_size = Optimizations::USE_WORK_STEALING ?
                   Optimizations::WORK_STEAL_CHUNK_SIZE : 1;

    // Get next work index based on access pattern
    int start_tile;
    if (Optimizations::USE_STRIDED_TILE_ACCESS) {
        // Simple strided access: grab tiles but in bigger jumps
        // Instead of 0,1,2,3... we take 0,4,8,12,1,5,9,13,2,6,10,14...
        // This spreads out memory access
        int stride = Optimizations::TILE_ACCESS_STRIDE;
        int base_tile = worker_data_.next_work_index.fetch_add(1);

        // Convert linear index to strided index
        // Pattern: interleave by stride
        int group = base_tile / stride;  // Which group (0,1,2,3 or 4,5,6,7, etc)
        int offset = base_tile % stride; // Offset within group
        start_tile = offset * (worker_data_.total_tiles / stride) + group;

        // Make sure we don't go out of bounds
        if (start_tile >= worker_data_.total_tiles) {
            // Wrap around to fill in any remaining tiles
            start_tile = base_tile;
        }
        chunk_size = 1; // Process one tile at a time in strided mode
    } else {
        // Linear access: tiles processed in order
        start_tile = worker_data_.next_work_index.fetch_add(chunk_size);
    }

    // Check if we have work
    bool has_work = (start_tile < worker_data_.total_tiles);

    return {start_tile, chunk_size, has_work};
}

// Main thread processing loop when it participates in parallel rendering
// Extracted from rasterize_tiles to follow Single Responsibility Principle
// Allows main thread to help process tiles alongside worker threads
void SurfaceRasterizer::main_thread_process_tiles(
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
    EngineMetrics* metrics) {

    while (true) {
        // Grab one tile at a time
        int tile_idx = worker_data_.next_work_index.fetch_add(1);

        if (tile_idx >= total_tiles) break;

        const auto& tile_bin = (*worker_data_.tile_bins)[tile_idx];
        if (tile_bin.surfaces.empty()) continue;

        int tile_x = tile_idx % tiles_x;
        int tile_y = tile_idx / tiles_x;

        double local_depth_time = 0.0;
        double local_lighting_time = 0.0;

        int tile_pixels = process_tile_simple(
            tile_bin, tile_x, tile_y,
            pixel_buffer, depth_buffer,
            camera_system, light_system, pixel_shader,
            particles, shadow_bvh, lights,
            render_width, render_height,
            cam_x, cam_y, cam_z,
            local_depth_time, local_lighting_time,
            metrics
        );

        worker_data_.pixels_done.fetch_add(tile_pixels);
        total_depth_time += local_depth_time;
        total_lighting_time += local_lighting_time;
    }
}

// =============================================================================
// END OF SURFACE RASTERIZER IMPLEMENTATION
// =============================================================================
// Old two-phase code (rasterize_tile_to_pixels, shade_visible_pixels) removed
// Now using single-pass approach in surface_rasterizer_single_pass.cpp

// Persistent worker thread function
// OPTIMIZATION: Changed from condition variable to spin-wait (2025-01-27)
// WHY: Condition variables require kernel transitions, mutex locks, and context switches
// HOW: Simple spin loop with yield() - threads continuously check for work
// IMPACT: Expected 15% speedup from avoiding kernel overhead
void SurfaceRasterizer::worker_thread_func(int thread_id) {
    //printf("DEBUG: Worker thread started\n");
    worker_busy_[thread_id] = false;  // Start idle
    
    // Set thread-local pointer to this thread's sparse object map
    while (worker_running_) {
        // Wait for work if none available
        if (worker_data_.next_work_index >= worker_data_.total_tiles) {
            // Already marked idle above or at end of previous frame
            
            // Wait for work
            while (worker_data_.next_work_index >= worker_data_.total_tiles && worker_running_) {
                std::this_thread::yield();  // Let other threads run
            }
            
            if (!worker_running_) break;
        }
        
        // Work is available - mark busy if not already
        if (!worker_busy_[thread_id]) {
            worker_busy_[thread_id] = true;
        }

        // THREAD SAFETY: Memory fence to ensure all worker_data_ writes are visible
        // Matches the release fence in rasterize_tiles() before setting total_tiles
        std::atomic_thread_fence(std::memory_order_acquire);

        //printf("DEBUG: Worker starting processing, total_tiles=%d\n", (int)worker_data_.total_tiles);

        // Process tiles while there's work
        while (worker_data_.next_work_index < worker_data_.total_tiles) {
            // CRITICAL: Check if tile_bins is still valid before proceeding
            if (!worker_data_.tile_bins || worker_data_.total_tiles == 0) {
                //printf("DEBUG: Work cancelled (tile_bins=%p, total_tiles=%d), breaking\n", 
                //       (void*)worker_data_.tile_bins, (int)worker_data_.total_tiles);
                break;
            }
            
            // Get next work chunk using work stealing or strided access (extracted for clarity)
            auto work = get_next_tile_work();

            // Check if we're done
            if (!work.has_work) {
                break;
            }

            int start_tile = work.start_tile;
            int chunk_size = work.chunk_size;
            
            // Process up to chunk_size tiles (but not beyond total_tiles)
            int end_tile = std::min(start_tile + chunk_size, (int)worker_data_.total_tiles);
            //printf("DEBUG: Processing tiles %d to %d (total_tiles=%d)\n", start_tile, end_tile, (int)worker_data_.total_tiles);
            
            for (int tile_idx = start_tile; tile_idx < end_tile; tile_idx++) {
                // CRITICAL: Check bounds AND tile_bins validity before accessing
                if (tile_idx >= worker_data_.total_tiles || !worker_data_.tile_bins) {
                    //printf("DEBUG: Tile %d >= total_tiles %d or tile_bins null, breaking\n", tile_idx, (int)worker_data_.total_tiles);
                    break;
                }

                // THREAD SAFETY: Validate all WorkData pointers before dereferencing
                if (!worker_data_.camera_system || !worker_data_.pixel_buffer ||
                    !worker_data_.depth_buffer || !worker_data_.light_system ||
                    !worker_data_.pixel_shader || !worker_data_.particles) {
                    std::cerr << "[WORKER FATAL] Null pointer in WorkData!\n"
                              << "  camera_system=" << worker_data_.camera_system << "\n"
                              << "  pixel_buffer=" << worker_data_.pixel_buffer << "\n"
                              << "  depth_buffer=" << worker_data_.depth_buffer << "\n";
                    break;
                }

                //printf("DEBUG: Processing tile %d\n", tile_idx);

                //printf("DEBUG: Getting tile_bin at index %d\n", tile_idx);
                const auto& tile_bin = (*worker_data_.tile_bins)[tile_idx];
                //printf("DEBUG: Got tile_bin, checking if empty\n");
                if (tile_bin.surfaces.empty()) {
                    //printf("DEBUG: Tile %d is empty, skipping\n", tile_idx);
                    continue;  // Skip empty tiles
                }

                int tile_x = tile_idx % worker_data_.tiles_x;
                int tile_y = tile_idx / worker_data_.tiles_x;

                double local_depth_time = 0.0;
                double local_lighting_time = 0.0;

                int tile_pixels = process_tile_simple(
                    tile_bin, tile_x, tile_y,
                    *worker_data_.pixel_buffer,
                    *worker_data_.depth_buffer,
                    *worker_data_.camera_system,
                    *worker_data_.light_system,
                    *worker_data_.pixel_shader,
                    *worker_data_.particles,
                    worker_data_.shadow_bvh,
                    worker_data_.lights,
                    worker_data_.render_width,
                    worker_data_.render_height,
                    worker_data_.cam_x, worker_data_.cam_y, worker_data_.cam_z,
                    local_depth_time, local_lighting_time,
                    worker_data_.metrics
                );
                
                worker_data_.pixels_done.fetch_add(tile_pixels);
            }
        }
        
        // Mark idle after processing all tiles
        worker_busy_[thread_id] = false;
    }
}

// Phase III: Callback context for async GPU completion
struct AsyncCallbackContext {
    SurfaceRasterizer* rasterizer;
    int frame_index;  // Which frame buffer these results belong to
};

// Phase IV: Process all collected pixels with async GPU batch
void SurfaceRasterizer::process_gpu_shadow_batch(
    const std::vector<const Particle*>& lights,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh)
{
    static int call_count = 0;
    if (call_count++ < 3) {
        std::cout << "[GPU ASYNC DISPATCH] pixels=" << current_frame().pixels.size()
                  << " lights=" << lights.size()
                  << " rays=" << (current_frame().pixels.size() * lights.size())
                  << " frame=" << current_frame().frame_number << "\n";
    }

    // Copy light data into frame (not pointers!) to avoid use-after-free on vector reallocation
    current_frame().lights.clear();
    current_frame().lights.reserve(lights.size());
    for (const auto* light : lights) {
        // Physical light size used for soft shadows' area-light
        // approximation. BOX and ELLIPSOID both have per-axis
        // dimensions; SPHERE is isotropic via size.
        float light_size = light->size;
        if (light->shape == ParticleShape::BOX ||
            light->shape == ParticleShape::ELLIPSOID) {
            light_size = std::max({light->width, light->height, light->thickness, light->size});
        }
        current_frame().lights.emplace_back(
            light->x, light->y, light->z,
            light->emission_strength,
            light->emission_radius,
            light_size  // Physical size for soft shadows
        );
    }

    // Build all shadow rays (pixels × lights)
    current_frame().rays.clear();
    current_frame().rays.reserve(current_frame().pixels.size() * current_frame().lights.size());

    for (const auto& pixel : current_frame().pixels) {
        for (const auto& light : current_frame().lights) {  // Now iterating over LightSnapshot values
            ShadowRay ray;
            ray.origin_x = pixel.world_x;
            ray.origin_y = pixel.world_y;
            ray.origin_z = pixel.world_z;
            ray.target_x = light.x;  // Direct member access
            ray.target_y = light.y;
            ray.target_z = light.z;
            ray.exclude_particle_id = pixel.particle_index;
            ray.pixel_index = 0;  // Not used in GPU path
            current_frame().rays.push_back(ray);
        }
    }

    // Convert CPU rays to GPU format
    std::vector<Logosphere::ShadowRay> gpu_rays;
    extern void convert_to_gpu_rays(const std::vector<ShadowRay>&, std::vector<Logosphere::ShadowRay>&);
    convert_to_gpu_rays(current_frame().rays, gpu_rays);

    // Create callback context (heap-allocated, deleted in callback)
    AsyncCallbackContext* context = new AsyncCallbackContext{this, current_frame_index_};

    // ASYNC GPU DISPATCH with triple buffering - CPU returns immediately!
    gpu_bridge_->trace_shadow_rays_batched_bvh_async(
        gpu_rays.data(), static_cast<int>(gpu_rays.size()),
        gpu_triangle_cache_.data(), static_cast<int>(gpu_triangle_cache_.size()),
        async_gpu_completion_callback,  // Callback runs on Metal thread when GPU finishes
        context                          // User data passed to callback
    );

    // CPU continues immediately - GPU works in parallel on this frame
    // while CPU starts next frame's rasterization!
    // Lighting will be applied by callback when GPU completes
}

// Phase II-C: Write all collected pixels with lighting to framebuffer
void SurfaceRasterizer::write_collected_pixels(PixelBuffer& pixel_buffer) {
    // Reuse existing write_lit_pixel function from surface_rasterizer_single_pass.cpp
    extern void write_lit_pixel(PixelBuffer&, int, int, float, float, float, float, float, int, SparseObjectMap*);

    // GPU path writes from main thread (not during parallel tile processing)
    // Use main thread's workspace (thread 0)
    SparseObjectMap* main_object_map = (thread_workspaces_.size() > 0 && worker_data_.object_map)
                                        ? worker_data_.object_map
                                        : nullptr;

    for (const auto& pixel : current_frame().pixels) {
        write_lit_pixel(pixel_buffer, pixel.screen_x, pixel.screen_y,
                       pixel.lighting, pixel.particle_r, pixel.particle_g, pixel.particle_b, pixel.particle_a,
                       pixel.particle_index, main_object_map);
    }
}

// Phase III: Write a completed frame's pixels to its pixel buffer (async execution)
void SurfaceRasterizer::write_frame_pixels(FrameData& frame) {
    // CRITICAL: Lock frame mutex - called from Metal completion thread!
    std::lock_guard<std::mutex> lock(frame.buffer_mutex);

    if (!frame.pixel_buffer) {
        std::cerr << "[ASYNC GPU] ERROR: Frame has no pixel buffer!\n";
        return;
    }


    // Reuse existing write_lit_pixel function
    extern void write_lit_pixel(PixelBuffer&, int, int, float, float, float, float, float, int, SparseObjectMap*);

    // GPU path writes from main thread
    SparseObjectMap* main_object_map = (thread_workspaces_.size() > 0 && worker_data_.object_map)
                                        ? worker_data_.object_map
                                        : nullptr;

    for (const auto& pixel : frame.pixels) {
        write_lit_pixel(*frame.pixel_buffer, pixel.screen_x, pixel.screen_y,
                       pixel.lighting, pixel.particle_r, pixel.particle_g, pixel.particle_b, pixel.particle_a,
                       pixel.particle_index, main_object_map);
    }
}

// Phase IV: Static completion callback for async GPU (called from Metal thread!)
void SurfaceRasterizer::async_gpu_completion_callback(const float* results, int result_count, void* user_data) {
    std::cout << "[GPU CALLBACK] Called! results=" << result_count << "\n";

    AsyncCallbackContext* context = static_cast<AsyncCallbackContext*>(user_data);
    if (!context || !context->rasterizer) {
        std::cerr << "[ASYNC GPU] ERROR: Invalid callback context!\n";
        delete context;
        return;
    }

    // Apply lighting to the specific frame buffer
    FrameData& frame = context->rasterizer->frame_data_[context->frame_index];
    context->rasterizer->apply_lighting_to_frame(frame, results, result_count);

    // Write lit pixels to frame's pixel buffer
    context->rasterizer->write_frame_pixels(frame);

    // Frame is now ready for presentation (checked in main render loop)
    // ready_to_write flag was already set by apply_lighting_to_frame

    // Clean up heap-allocated context
    delete context;
}

// Phase III: Apply lighting from GPU results to frame pixels (async execution)
void SurfaceRasterizer::apply_lighting_to_frame(FrameData& frame, const float* results, int result_count) {
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;
    auto lighting_start = Clock::now();

    static int apply_call = 0;
    bool debug = (apply_call++ < 3);
    int blocked_count = 0;

    // CRITICAL: Lock mutex before accessing frame.pixels and frame.lights!
    // The main thread may be clearing these structures while we're reading them
    std::lock_guard<std::mutex> lock(frame.buffer_mutex);

    // Convert GPU results to blocked/not blocked
    frame.ray_results.resize(result_count);
    for (int i = 0; i < result_count; i++) {
        frame.ray_results[i] = (results[i] < 0.5f);  // blocked
        if (frame.ray_results[i]) blocked_count++;
    }

    if (debug) {
        std::cout << "[APPLY LIGHTING] results=" << result_count
                  << " blocked=" << blocked_count
                  << " lights=" << frame.lights.size() << "\n";
    }

    // GPU LIGHTING (Option B): Compute lighting on GPU (separate queue to avoid deadlock)
    const int pixel_count = frame.pixels.size();
    const int light_count = frame.lights.size();
    float total_lighting = 0.0f;

    if (debug) {
        std::cout << "[APPLY LIGHTING] Starting GPU lighting: pixels=" << pixel_count << " lights=" << light_count << "\n" << std::flush;
    }

    // Early exit if no lights (skip GPU work entirely)
    if (light_count == 0) {
        // Zero out all pixel lighting
        for (auto& pixel : frame.pixels) {
            pixel.lighting = 0.0f;
        }
    } else {
        if (debug) std::cout << "[APPLY LIGHTING] Allocating pixel buffer...\n" << std::flush;

        // Convert pixels to GPU format (position + normal)
        std::vector<Logosphere::PixelData> gpu_pixels(pixel_count);

        if (debug) std::cout << "[APPLY LIGHTING] Converting " << pixel_count << " pixels...\n" << std::flush;
        for (int i = 0; i < pixel_count; i++) {
            const auto& pixel = frame.pixels[i];
            gpu_pixels[i] = Logosphere::PixelData(
                pixel.world_x, pixel.world_y, pixel.world_z,
                pixel.normal_x, pixel.normal_y, pixel.normal_z
            );
            // Debug first pixel
            if (debug && i == 0) {
                std::cout << "[APPLY LIGHTING] Pixel[0]: pos=(" << pixel.world_x << "," << pixel.world_y << "," << pixel.world_z
                          << ") normal=(" << pixel.normal_x << "," << pixel.normal_y << "," << pixel.normal_z << ")\n" << std::flush;
            }
        }
        if (debug) std::cout << "[APPLY LIGHTING] Pixel conversion done\n" << std::flush;

        // Convert lights to GPU format (position + strength + radius)
        if (debug) std::cout << "[APPLY LIGHTING] Converting " << light_count << " lights...\n" << std::flush;
        std::vector<Logosphere::LightData> gpu_lights(light_count);
        for (int i = 0; i < light_count; i++) {
            const auto& light = frame.lights[i];  // Reference to LightSnapshot, not pointer
            gpu_lights[i] = Logosphere::LightData(
                light.x, light.y, light.z,  // Direct member access
                light.emission_strength,
                light.emission_radius,
                light.light_size  // Physical size for soft shadows
            );
            if (debug) {
                std::cout << "[APPLY LIGHTING] Light[" << i << "]: pos=(" << light.x << "," << light.y << "," << light.z
                          << ") strength=" << light.emission_strength << " radius=" << light.emission_radius << "\n" << std::flush;
            }
        }
        if (debug) std::cout << "[APPLY LIGHTING] Light conversion done\n" << std::flush;

        // Allocate output buffer
        if (debug) std::cout << "[APPLY LIGHTING] Allocating output buffer...\n" << std::flush;
        std::vector<float> lighting_output(pixel_count);

        // Dispatch GPU lighting kernel (distance²/sqrt/dot/intensity on GPU)
        if (debug) std::cout << "[APPLY LIGHTING] Calling gpu_bridge->compute_lighting()...\n" << std::flush;
        if (gpu_bridge_) {
            gpu_bridge_->compute_lighting(
                gpu_pixels.data(),
                pixel_count,
                gpu_lights.data(),
                light_count,
                results,  // Shadow results from GPU
                lighting_output.data()
            );
        }
        if (debug) std::cout << "[APPLY LIGHTING] GPU compute_lighting() returned\n" << std::flush;

        // Copy GPU results back to pixels
        for (int i = 0; i < pixel_count; i++) {
            frame.pixels[i].lighting = lighting_output[i];
            total_lighting += lighting_output[i];
            if (debug && i < 5 && lighting_output[i] > 0.0f) {
                std::cout << "[APPLY LIGHTING] Pixel[" << i << "] lighting=" << lighting_output[i] << "\n" << std::flush;
            }
        }
        if (debug) {
            // Check shadow results too
            int unblocked_first_100 = 0;
            int unblocked_all = 0;
            int total_rays = pixel_count * light_count;
            for (int i = 0; i < total_rays; i++) {
                if (results[i] >= 0.5f) {
                    unblocked_all++;
                    if (i < 100) unblocked_first_100++;
                }
            }
            std::cout << "[APPLY LIGHTING] Shadow rays: " << unblocked_all << "/" << total_rays
                      << " unblocked (" << (100.0f * unblocked_all / total_rays) << "%)\n" << std::flush;
            std::cout << "[APPLY LIGHTING] First 100 rays: " << unblocked_first_100 << " unblocked\n" << std::flush;
        }
    }

    if (debug) {
        std::cout << "[APPLY LIGHTING] total_lighting=" << total_lighting
                  << " avg=" << (total_lighting / frame.pixels.size()) << "\n";
    }

    auto lighting_end = Clock::now();
    double lighting_time = Duration(lighting_end - lighting_start).count();

    // PROFILING: Record lighting calculation time for granular report
    LightingMetrics::get().lighting_calculation_time = lighting_time;

    // Mark frame ready for writing
    frame.ready_to_write = true;
}