#ifndef RENDER_PIPELINE_H
#define RENDER_PIPELINE_H

#include <vector>
#include <deque>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "particle.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "core/light_system.h"
#include "engine_metrics.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/depth_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"  // For object_map parameter
#include "logosphere/rendering/surface_rasterizer.h"
#include "logosphere/rendering/pixel_shader.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"  // GPU rasterization
#include "logosphere/rendering/triangle_bvh.h"  // For async GPU_PREP triple-buffering
#include "logosphere/rendering/entity_bvh.h"    // Entity-level BVH with directional culling
#include "logosphere/rendering/render_spatial_grid.h"  // Micro-chunk spatial grid for render culling
#include "logosphere/kg/kg_types.h"  // For kg::EntityID

// Forward declarations
class BVH;
namespace kg { class KGModule; }

/**
 * RenderPipeline - High-level orchestration of the rendering process
 * 
 * Following KISS & UNIX philosophy: This class does ONE thing - orchestrates
 * the rendering pipeline from particles to pixels. It coordinates the various
 * rendering stages but delegates actual work to specialized components.
 * 
 * Part of the rendering system refactoring to break down the monolithic RenderSystem.
 * 
 * Historical note: Early 3D engines like Quake had similar pipeline architectures,
 * with clear stages: transformation, culling, sorting, rasterization. This modular
 * approach allowed id Software to optimize each stage independently.
 */
// Minimum frames between shadow-BVH full rebuilds. See
// Optimizations::SHADOW_BVH_MIN_REBUILD_FRAMES for what it trades. Settable at
// runtime so the quality/perf choice can be flipped on a live scene.
namespace logosphere {
void   set_shadow_bvh_rebuild_frames(size_t n);
size_t get_shadow_bvh_rebuild_frames();

// DIAGNOSTIC: suppress either shadow acceleration structure at dispatch, to
// prove which one the Metal kernel traverses. Both default true.
void set_flat_shadow_bvh_enabled(bool on);
void set_entity_shadow_bvh_enabled(bool on);
bool get_flat_shadow_bvh_enabled();
bool get_entity_shadow_bvh_enabled();

// Async GPU prep: run prepare_gpu_data on a worker for frame N+1 while the GPU
// renders frame N, instead of synchronously on the main thread.
//
// DEFAULT OFF, and that is a leftover rather than a decision.
// Optimizations::USE_ASYNC_GPU_PREP was set false on 2026-04-04 "(for
// testing)" during an Eva-shadow investigation, which concluded the shadows
// were correct and that the bug it chased reproduced in BOTH modes. It was
// never restored. Runtime-switchable so sync and async can be A/B'd in one
// binary, which is what the equivalence test needs.
//
// While it is off, telemetry's RenderPrepWait / RenderHandoff / RenderSlotWait
// read 0.00 because their code never runs. That is not evidence of a pipeline
// that never stalls.
//
// LOGOSPHERE_ASYNC_PREP=1 turns it on at startup.
void set_async_gpu_prep(bool on);
bool get_async_gpu_prep();
}

class RenderPipeline {
public:
    // Constructor - takes reference to shared SurfaceRasterizer from RenderSystem
    RenderPipeline();  // GPU: Owns both rasterizers
    ~RenderPipeline() = default;
    
    // Main rendering entry point
    // Renders all particles from the particle system to the pixel buffer
    void render(
        const ParticleSystem& particle_system,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        SparseObjectMap& object_map,  // NEW: RenderSystem's unified object map
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        EngineMetrics* metrics = nullptr
    );

    // Configuration
    void set_debug_mode(bool enable) { debug_mode_ = enable; }
    bool get_debug_mode() const { return debug_mode_; }

    void set_show_lights_as_white(bool enable) { show_lights_as_white_ = enable; }
    bool get_show_lights_as_white() const { return show_lights_as_white_; }

    // KG Module for entity grouping (required for proper entity BVH)
    void set_kg_module(kg::KGModule* kg) { kg_module_ = kg; }

    // Entity hover highlighting (debug mode)
    // Takes int directly: -1 for "no hover", or entity_id (including 0 for Entity 0)
    // CRITICAL: Caller must convert kg::INVALID_ENTITY to -1 before calling
    void set_hovered_entity(int entity_id_or_sentinel) {
        hovered_entity_id_ = entity_id_or_sentinel;
    }
    int get_hovered_entity() const { return hovered_entity_id_; }

    // Wait for GPU to complete all pending work (async rasterization)
    // CRITICAL: Must call before shutdown or resolution change
    void wait_for_gpu_completion();

    // Reset all temporal GPU state for scene transitions.
    // Clears GI temporal, shadow temporal, sample counts, and resets GI frame counter.
    // Automatically calls wait_for_gpu_completion() first.
    void reset_temporal_state();

    // Split acquire/release for resolution changes (holds GPU idle while changing drawable)
    int acquire_all_gpu_slots();
    void release_all_gpu_slots(int slots);

    // Check if GPU has finished rendering a frame (async rasterization)
    // Used by Engine::present() to know when it's safe to display
    bool is_frame_ready() const { return gpu_frame_ready_.load(std::memory_order_acquire); }

    // Mark frame as presented (resets ready flag)
    void mark_frame_presented() { gpu_frame_ready_.store(false, std::memory_order_release); }

    // Pipeline statistics (for debugging/profiling)
    struct PipelineStats {
        int total_particles;
        int total_surfaces;
        int surfaces_culled;
        int surfaces_projected;
        int surfaces_rendered;
        int pixels_drawn;
    };
    
    const PipelineStats& get_last_frame_stats() const { return last_frame_stats_; }

    // Vision cone post-process (Pass 4)
    // Fog-of-war effect that darkens pixels outside the viewer's field of view
    void set_vision_cone_enabled(bool enabled);
    bool get_vision_cone_enabled() const;

    // Set vision cone parameters
    // viewer_x, viewer_y: World position of viewer
    // look_direction: Direction facing (radians, 0 = +Y/North)
    // fov_radians: Total field of view in radians
    // range: Maximum vision distance in world units
    void set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                         float fov_radians, float range);

    // Fine-tune vision cone appearance
    void set_vision_cone_style(float inner_falloff, float darkness);

    // Set focus point for foveal vision simulation
    void set_vision_cone_focus(float focus_x, float focus_y, float focus_radius = 3.0f);

    // LOS occlusion mask for the vision cone — see GpuRasterizer's
    // header for the contract. count must equal
    // GPURasterizer::kVisionConeOcclusionBins (64) to enable.
    void set_vision_cone_occlusion(const float* distances, int count);
    void clear_vision_cone_occlusion();

    // Vision memory grid — world-space "what the viewer just saw"
    // buffer that decays over time. See
    // include/logosphere/rendering/vision_memory.h for details.
    void set_vision_memory_enabled(bool enabled);
    void set_vision_memory_extent(float min_x, float min_y,
                                  float max_x, float max_y,
                                  int cells_per_side);
    void set_vision_memory_decay(float decay_seconds, float memory_dim);
    void update_vision_memory(float dt);

    // Synchronous framebuffer read-back. Passes through to
    // GPURasterizer::read_latest_framebuffer. See header there for
    // the contract; used by headless acceptance tests to read real
    // GPU-rendered pixels without a window.
    bool read_latest_framebuffer(uint32_t* out_pixels,
                                 int& out_width, int& out_height) {
        return gpu_rasterizer_.read_latest_framebuffer(out_pixels,
                                                       out_width,
                                                       out_height);
    }

    // Diagnostic: read shadow pipeline internal state at a screen pixel
    bool read_shadow_debug(int screen_x, int screen_y,
                           uint32_t& out_sample_count,
                           float& out_temporal_lux,
                           uint32_t& out_prev_particle_id) const {
        return gpu_rasterizer_.read_shadow_debug(screen_x, screen_y,
            out_sample_count, out_temporal_lux, out_prev_particle_id);
    }

    bool read_gi_debug(int screen_x, int screen_y,
                       float& out_gi_r, float& out_gi_g, float& out_gi_b,
                       float& out_shadow_lux) const {
        return gpu_rasterizer_.read_gi_debug(screen_x, screen_y,
            out_gi_r, out_gi_g, out_gi_b, out_shadow_lux);
    }

    bool read_gbuffer_debug(int screen_x, int screen_y,
                            uint8_t& out_r, uint8_t& out_g, uint8_t& out_b,
                            uint32_t& out_particle_id) const {
        return gpu_rasterizer_.read_gbuffer_debug(screen_x, screen_y,
            out_r, out_g, out_b, out_particle_id);
    }

    bool read_ssdo_debug(int screen_x, int screen_y,
                         float& out_r, float& out_g, float& out_b,
                         float& out_ao) const {
        return gpu_rasterizer_.read_ssdo_debug(screen_x, screen_y,
            out_r, out_g, out_b, out_ao);
    }

    bool read_gpu_framebuffer(int screen_x, int screen_y,
                              uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) const {
        return gpu_rasterizer_.read_gpu_framebuffer(screen_x, screen_y,
            out_r, out_g, out_b);
    }

private:
    // Pipeline stages

    // Pre-stage: Entity-level frustum culling (before per-particle culling)
    void pre_cull_entities(
        const std::vector<Particle>& particles,
        CameraSystem& camera_system,
        int render_width,
        int render_height,
        std::vector<bool>& entity_culled,
        int& entities_culled_count,
        int& entities_visible_count
    );

    // Stage 1: Surface collection - gather all surfaces from particles
    // visible_particle_indices: Flattened list of particle indices from visible entities only
    //                          If empty, falls back to iterating all particles (entity culling disabled)
    void collect_surfaces(
        const std::vector<Particle>& particles,
        std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        CameraSystem& camera_system,
        int render_width,
        int render_height,
        DepthBuffer& depth_buffer,
        const std::vector<bool>& entity_culled,
        const std::vector<int>& visible_particle_indices
    );

    // Stage 2: Culling - remove surfaces that won't be visible
    void cull_surfaces(
        std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        CameraSystem& camera_system,
        EngineMetrics* metrics
    );
    
    // Stage 3: Distance calculation - compute distance from camera for sorting
    void calculate_distances(
        std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        CameraSystem& camera_system
    );
    
    // Stage 4: Sorting - order surfaces for proper rendering
    void sort_surfaces(
        std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        EngineMetrics* metrics
    );
    
    // Stage 5: Rasterization - convert surfaces to pixels
    void rasterize_surfaces(
        const std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        PixelBuffer& pixel_buffer,
        DepthBuffer& depth_buffer,
        SparseObjectMap& object_map,  // NEW: RenderSystem's unified object map
        CameraSystem& camera_system,
        LightSystem& light_system,
        IPixelShader& pixel_shader,
        const std::vector<Particle>& particles,
        const BVH* shadow_bvh,
        EngineMetrics* metrics
    );

    // =========================================================================
    // ASYNC GPU_PREP HELPER (2025-01-24)
    // =========================================================================
    // Prepare GPU data for deferred rendering (can run async)
    // Writes to indexed buffers to enable triple-buffering
    void prepare_gpu_data(
        int buffer_index,
        const std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
        const std::vector<Particle>& particles,
        CameraSystem& camera_system
    );
    
    // Configuration
    bool debug_mode_;
    bool show_lights_as_white_;
    kg::KGModule* kg_module_ = nullptr;  // For entity grouping (particle index → entity_id)
    // Hover highlighting: Use -1 as sentinel (not kg::INVALID_ENTITY which is 0)
    // This allows Entity 0 to be highlighted
    int hovered_entity_id_ = -1;  // Entity to highlight in debug mode (-1 = none)
    
    // Statistics
    PipelineStats last_frame_stats_;
    
    // Surface cache - single cache (GPU triple-buffering handled inside SurfaceRasterizer)
    // Using deque for pointer stability when growing (prevents crashes with large tiles)
    std::deque<SurfaceRasterizer::SurfaceData> surface_cache_;

    // Render index -> owning entity, refreshed once per frame under a single
    // KG lock so the shadow-triangle workers never touch the KG mutex.
    std::vector<kg::EntityID> shadow_entity_ids_;

    // Per-particle generated geometry, reused while the particle has not
    // moved (USE_RENDER_SURFACE_CACHE). Indexed by particle index; the stored
    // key makes it safe against swap-and-pop index reuse — see the flag
    // comment in optimization_flags.h.
    struct CachedSurfaces {
        // Everything GetSurfacesInto() reads. If all of it matches, the
        // geometry it would produce is identical.
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
        float w = 0, h = 0, t = 0, size = 0;
        int   shape = -1;
        bool  valid = false;
        std::vector<Surface> surfaces;

        bool matches(const Particle& p) const;
        void store_key(const Particle& p);
    };
    std::vector<CachedSurfaces> surface_geom_cache_;

    // Rasterizers (composition) - RenderPipeline owns both
    SurfaceRasterizer surface_rasterizer_;           // CPU rasterization (has internal triple-buffering)
    Logosphere::GPURasterizer gpu_rasterizer_;       // GPU rasterization

    // Async GPU frame synchronization (fixes data race bug)
    // The callback writes to pixel_buffer on Metal's background thread while
    // the main thread may be reading it for display. These synchronization
    // primitives ensure safe access.
    std::atomic<bool> gpu_frame_ready_{false};       // True when GPU frame is ready to present
    mutable std::mutex gpu_frame_mutex_;             // Protects pixel_buffer during memcpy

    // =========================================================================
    // ASYNC GPU_PREP: Triple-buffered prep data (2025-01-24)
    // =========================================================================
    // PURPOSE: Overlap CPU GPU_PREP with GPU execution
    // STRATEGY: While GPU renders Frame N, CPU preps Frame N+1 async
    // EXPECTED: FPS 22.7 → 31+ (GPU time limited, not CPU+GPU serial)
    //
    // Triple-buffered data structures (indexed 0, 1, 2):
    static constexpr int PREP_BUFFER_SLOTS = 3;

    // Spare surface buffers for the async prep handoff. The main thread SWAPS
    // its filled surface_cache_ with one of these (O(1)) instead of copying
    // 103,914 elements, and gets back a deque that already owns its blocks, so
    // next frame's collect_surfaces still reuses allocations. A plain move
    // would avoid the copy too but leave an empty deque to reallocate every
    // frame, trading one cost for another.
    //
    // Pool rather than a single spare because the worker reads its slot while
    // later frames fill others. Safe at PREP_BUFFER_SLOTS: skip_next_async_prep_
    // keeps at most one worker live, so a slot is always free by the time the
    // index wraps back to it.
    std::deque<SurfaceRasterizer::SurfaceData> surface_pool_[PREP_BUFFER_SLOTS];
    std::vector<Logosphere::GPURasterizer::TriangleLit> gpu_triangles_[PREP_BUFFER_SLOTS];
    std::vector<Logosphere::GPURasterizer::TriangleLit> gpu_transparent_triangles_[PREP_BUFFER_SLOTS];  // Transparency: forward pass
    std::vector<Logosphere::ShadowTriangle> shadow_triangles_[PREP_BUFFER_SLOTS];
    std::vector<kg::EntityID> triangle_entity_ids_[PREP_BUFFER_SLOTS];  // Entity ID per shadow triangle (for entity grouping)
    TriangleBVH shadow_bvh_[PREP_BUFFER_SLOTS];
    EntityBVH entity_bvh_[PREP_BUFFER_SLOTS];  // Entity-level BVH with directional culling
    std::vector<EntityTriangleData> entity_triangle_data_[PREP_BUFFER_SLOTS];  // Per-entity triangle groups
    std::vector<uint8_t> light_data_[PREP_BUFFER_SLOTS];  // Packed light data (32 bytes per light)
    std::vector<uint8_t> is_light_source_map_[PREP_BUFFER_SLOTS];  // Maps entity_id -> is_light_source
    std::vector<uint8_t> is_dynamic_map_[PREP_BUFFER_SLOTS];       // Maps entity_id -> is_dynamic (vision-memory exclusion)
    std::vector<uint8_t> particle_transforms_[PREP_BUFFER_SLOTS];  // Maps entity_id -> ParticleTransform (32 bytes each)

    // BVH state tracking per buffer
    bool bvh_built_[PREP_BUFFER_SLOTS] = {false, false, false};
    size_t last_triangle_count_[PREP_BUFFER_SLOTS] = {0, 0, 0};
    size_t frames_since_rebuild_[PREP_BUFFER_SLOTS] = {0, 0, 0};  // BVH quality optimization

    // Dirty-refit tracking: previous particle poses for change detection.
    // Stored per-buffer to avoid cross-buffer race conditions with async prep.
    // Each entry in prev_positions_ is {x, y, z}; prev_rotations_ is
    // {rot_x, rot_y, rot_z}. Rotation is checked too because a particle that
    // rotates in place (center unchanged) still invalidates its shadow
    // triangles — missing that lets rotating bodies keep stale cast shadows.
    std::vector<float> prev_positions_[PREP_BUFFER_SLOTS];
    std::vector<float> prev_rotations_[PREP_BUFFER_SLOTS];
    // Per-particle shadow triangle range: [start, count] pairs.
    // Built during shadow tri generation, used to map particle→dirty triangles.
    std::vector<int> particle_tri_start_[PREP_BUFFER_SLOTS];
    std::vector<int> particle_tri_count_[PREP_BUFFER_SLOTS];

    // Buffer indices for async prep
    std::atomic<int> prep_buffer_index_{0};    // Which buffer async worker writes to
    std::atomic<int> render_buffer_index_{0};  // Which buffer GPU reads from

    // =========================================================================
    // ASYNC PREP SYNCHRONIZATION
    // =========================================================================
    // Async prep runs BVH build in background thread while GPU renders.
    // When prep is slow (580ms BVH rebuild during chunk load), we fallback
    // to previous buffer instead of stalling. Race condition prevention:
    //   - last_good_buffer_idx_: Buffer to reuse if prep times out
    //   - skip_next_async_prep_: Prevents spawning new prep while old runs
    // Without skip flag, we'd write to buffer while old prep still writes it.
    // =========================================================================
    std::atomic<bool> prep_ready_{false};      // True when async prep complete
    std::mutex prep_mutex_;                    // Protects prep completion
    std::condition_variable prep_cv_;          // Signals prep completion
    int last_good_buffer_idx_ = 0;             // Fallback buffer when async prep times out
    bool skip_next_async_prep_ = false;        // Prevents race: don't start new prep if old still running
    bool first_frame_ = true;                  // Forces synchronous prep on first frame (and after scene reset)

    // =========================================================================
    // SPATIAL GRID: Micro-chunk culling (Two-Tier Chunk Streaming Phase 1)
    // =========================================================================
    // Replaces entity iteration with cell-based spatial queries.
    // Grid is rebuilt when particle count changes significantly.
    RenderSpatialGrid render_spatial_grid_;
    size_t last_grid_particle_count_ = 0;      // Track for rebuild decision

public:
    // Expose mutex for callback to use (callback needs to lock during memcpy)
    std::mutex& get_frame_mutex() { return gpu_frame_mutex_; }
};

#endif // RENDER_PIPELINE_H