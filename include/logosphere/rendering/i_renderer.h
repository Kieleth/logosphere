#ifndef LOGOSPHERE_I_RENDERER_H
#define LOGOSPHERE_I_RENDERER_H

#include <cstdint>
#include <mutex>

// IRenderer: the platform-agnostic half of the Logosphere engine's
// rendering path. A renderer takes a scene (ParticleSystem +
// camera + lights) and produces BGRA pixels on the GPU. It has no
// knowledge of windows, swapchains, or presentation.
//
// Implementations:
//   MetalRenderer       — macOS, deferred rendering via MTLBuffer
//                         compute pipelines + post-process.
//   (future)            — Vulkan, D3D12, software reference, etc.
//
// Consumers of pixels are the Display (for on-screen presentation)
// and the read_framebuffer() caller (for offline / AT / asset bake).
// The same Renderer can feed both. "Headless" is not a mode on the
// Renderer; it's simply "the Engine has no Display wired in."

// Forward declarations: avoid pulling the whole engine into every
// translation unit that only wants the interface. Concrete impls
// include the full headers in their .cpp / .mm files.
class ParticleSystem;
class CameraSystem;
class LightSystem;
struct EngineMetrics;
class PixelBuffer;  // read-only access for the Display to pull pixels
namespace kg { class KGModule; }

namespace Logosphere {
namespace Rendering {

// Configuration handed to IRenderer::initialize. Kept small and POD
// so it can be built anywhere without pulling the full engine.
struct RendererConfig {
    // Internal render target resolution. Unrelated to window size;
    // the Display is responsible for scaling this to its surface.
    int render_width  = 1600;
    int render_height = 1200;

    // World-space zoom level; pixels_per_unit = window_w / viewport.
    // Forwarded to the camera system. Games tune this.
    float viewport_width_units  = 80.0f;
    float viewport_height_units = 60.0f;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Lifecycle. initialize() returns false on GPU device / pipeline
    // creation failure. shutdown() drains the GPU and releases
    // resources; must be called before destruction if initialize()
    // succeeded.
    virtual bool initialize(const RendererConfig& config) = 0;
    virtual void shutdown() = 0;

    // Submit one frame of GPU work. Synchronous from the caller's
    // POV (returns once the command buffer is enqueued), but GPU
    // execution is async; pixels are not guaranteed ready until a
    // subsequent wait_for_completion() or read_framebuffer() call.
    // metrics is optional; nullptr to skip profiling capture.
    virtual void draw(const ParticleSystem& particles,
                      CameraSystem& camera,
                      LightSystem& lights,
                      EngineMetrics* metrics = nullptr) = 0;

    // Block until every in-flight GPU frame has completed. Used
    // before read_framebuffer() and during teardown.
    virtual void wait_for_completion() = 0;

    // Synchronous read-back of the most recently dispatched frame.
    // Drains GPU work, then memcpys the last-written framebuffer
    // slot into caller-owned out_pixels. Writes the Renderer's
    // internal render dimensions to out_width / out_height. Returns
    // false if no frame has completed yet (e.g. scene was empty
    // and GPU took the early-exit path).
    //
    // Format: BGRA uint32_t per pixel, alpha in top byte:
    // (A<<24) | (R<<16) | (G<<8) | B.
    virtual bool read_framebuffer(uint32_t* out_pixels,
                                  int& out_width,
                                  int& out_height) = 0;

    // Access the current CPU-side framebuffer for Display
    // presentation. Returned pointer is owned by the Renderer and
    // valid until the next draw() call. Size is (render_width *
    // render_height * sizeof(uint32_t)) bytes.
    virtual const PixelBuffer& get_framebuffer() const = 0;

    // Render resolution this Renderer is configured for. May differ
    // from any window; the Display scales pixels as needed.
    virtual int render_width()  const = 0;
    virtual int render_height() const = 0;

    // ======================================================
    // Post-process configuration
    //
    // These mirror the Engine::set_vision_* surface 1:1 (see
    // src/core/engine.h:206-225). The Engine-side methods become
    // pass-throughs to the Renderer.
    //
    // Vision cone: a darkening post-process applied after lighting
    // that models the viewer's field of view. Pixels outside the
    // cone are dimmed toward `darkness`.
    // ======================================================
    virtual void set_vision_cone_enabled(bool enabled) = 0;
    virtual bool get_vision_cone_enabled() const = 0;
    virtual void set_vision_cone(float viewer_x, float viewer_y,
                                 float look_direction,
                                 float fov_radians,
                                 float range) = 0;
    virtual void set_vision_cone_style(float inner_falloff,
                                       float darkness) = 0;
    virtual void set_vision_cone_focus(float focus_x, float focus_y,
                                       float focus_radius) = 0;

    // LOS occlusion mask. `count` must equal the Renderer's
    // per-frame bin count (64 today) or the call is ignored.
    virtual void set_vision_cone_occlusion(const float* distances,
                                           int count) = 0;
    virtual void clear_vision_cone_occlusion() = 0;

    // Vision memory: a decaying world-space grid of "what the
    // viewer has recently seen." Pairs with vision cone + LOS.
    virtual void set_vision_memory_enabled(bool enabled) = 0;
    virtual void set_vision_memory_extent(float min_x, float min_y,
                                          float max_x, float max_y,
                                          int cells_per_side) = 0;
    virtual void set_vision_memory_decay(float decay_seconds,
                                         float memory_dim) = 0;
    virtual void update_vision_memory(float dt) = 0;

    // ======================================================
    // Frame lifecycle (async GPU, resolution changes, scene reset)
    // ======================================================
    // Ready flag for the async GPU path. Engine::present polls
    // is_frame_ready() and calls mark_frame_presented() after
    // handing pixels to the Display.
    virtual bool is_frame_ready() const = 0;
    virtual void mark_frame_presented() = 0;

    // Drain all in-flight GPU work and clear temporal accumulators
    // (GI, shadow, sample counts). Call on scene transitions to
    // prevent bleeding across loads.
    virtual void reset_temporal_state() = 0;

    // Mutex guarding the GPU completion callback. Acquired by the
    // Engine during UI rendering to prevent the Metal background
    // thread from overwriting the framebuffer mid-compose.
    virtual std::mutex& get_frame_mutex() = 0;

    // CPU worker-pool sync and triple-buffered frame counter, owned
    // today by the CPU `SurfaceRasterizer` inside the Renderer. Used
    // by the Engine to coordinate particle deletion with in-flight
    // tile workers that still hold raw pointers into the particle
    // vector.
    virtual void advance_frame_counter() = 0;
    virtual int  current_frame_number() const = 0;
    virtual void wait_for_workers_completion() = 0;

    // Hold the GPU idle while the drawable / resolution changes.
    // acquire returns the number of slots actually acquired; the
    // caller passes that count back to release_all_gpu_slots.
    virtual int  acquire_all_gpu_slots() = 0;
    virtual void release_all_gpu_slots(int slots) = 0;

    // ======================================================
    // Scene wiring
    // ======================================================
    // Non-owning pointer to the KG module for entity-aware grouping
    // in the BVH. Safe to pass nullptr.
    virtual void set_kg_module(kg::KGModule* kg) = 0;

    // Entity highlighted by mouse hover, used by the debug overlay
    // to draw a selection ring. -1 means "no hover."
    virtual void set_hovered_entity(int entity_id_or_sentinel) = 0;

    // ======================================================
    // Diagnostics
    // ======================================================
    // Per-pixel gbuffer / shadow / GI readbacks used by engine
    // debug overlays. Coordinates are in render-space pixels.
    virtual bool read_shadow_debug(int x, int y,
                                   uint32_t& out_sample_count,
                                   float& out_temporal_lux,
                                   uint32_t& out_prev_particle_id) const = 0;
    virtual bool read_gbuffer_debug(int x, int y,
                                    uint8_t& out_r, uint8_t& out_g,
                                    uint8_t& out_b,
                                    uint32_t& out_particle_id) const = 0;
    virtual bool read_gpu_framebuffer(int x, int y,
                                      uint8_t& out_r, uint8_t& out_g,
                                      uint8_t& out_b) const = 0;
    virtual bool read_gi_debug(int x, int y,
                               float& out_gi_r, float& out_gi_g,
                               float& out_gi_b,
                               float& out_shadow_lux) const = 0;
};

} // namespace Rendering
} // namespace Logosphere

#endif // LOGOSPHERE_I_RENDERER_H
