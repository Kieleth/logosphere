#ifndef LOGOSPHERE_METAL_RENDERER_H
#define LOGOSPHERE_METAL_RENDERER_H

#include "logosphere/rendering/i_renderer.h"

// Forward decl: Engine is defined in src/core/engine.h.
class Engine;

namespace Logosphere {
namespace Rendering {

// MetalRenderer: macOS implementation of IRenderer on top of the
// existing RenderPipeline + GPURasterizer stack.
//
// Holds a non-owning Engine* and reaches the rendering primitives
// (render pipeline, framebuffer, depth buffer, object map, lighting
// shader, surface rasterizer, etc.) through Engine accessors. The
// Engine outlives the renderer.
class MetalRenderer : public IRenderer {
public:
    explicit MetalRenderer(Engine& engine);
    ~MetalRenderer() override = default;

    // Lifecycle. Today initialize() is a no-op because the wrapped
    // RenderSystem is initialized by Engine directly; shutdown() is
    // likewise owned by Engine. Both return success to satisfy the
    // interface contract.
    bool initialize(const RendererConfig& config) override;
    void shutdown() override;

    // Rendering
    void draw(const ParticleSystem& particles,
              CameraSystem& camera,
              LightSystem& lights,
              EngineMetrics* metrics = nullptr) override;

    void wait_for_completion() override;

    bool read_framebuffer(uint32_t* out_pixels,
                          int& out_width,
                          int& out_height) override;

    const PixelBuffer& get_framebuffer() const override;

    int render_width() const override;
    int render_height() const override;

    // Vision-cone post-process
    void set_vision_cone_enabled(bool enabled) override;
    bool get_vision_cone_enabled() const override;
    void set_vision_cone(float viewer_x, float viewer_y,
                         float look_direction,
                         float fov_radians,
                         float range) override;
    void set_vision_cone_style(float inner_falloff, float darkness) override;
    void set_vision_cone_focus(float focus_x, float focus_y,
                               float focus_radius) override;
    void set_vision_cone_occlusion(const float* distances, int count) override;
    void clear_vision_cone_occlusion() override;

    // Vision-memory post-process
    void set_vision_memory_enabled(bool enabled) override;
    void set_vision_memory_extent(float min_x, float min_y,
                                  float max_x, float max_y,
                                  int cells_per_side) override;
    void set_vision_memory_decay(float decay_seconds, float memory_dim) override;
    void update_vision_memory(float dt) override;

    // Frame lifecycle
    bool is_frame_ready() const override;
    void mark_frame_presented() override;
    void reset_temporal_state() override;
    std::mutex& get_frame_mutex() override;
    void advance_frame_counter() override;
    int  current_frame_number() const override;
    void wait_for_workers_completion() override;
    int  acquire_all_gpu_slots() override;
    void release_all_gpu_slots(int slots) override;

    // Scene wiring
    void set_kg_module(kg::KGModule* kg) override;
    void set_hovered_entity(int entity_id_or_sentinel) override;

    // Diagnostics
    bool read_shadow_debug(int x, int y,
                           uint32_t& out_sample_count,
                           float& out_temporal_lux,
                           uint32_t& out_prev_particle_id) const override;
    bool read_gbuffer_debug(int x, int y,
                            uint8_t& out_r, uint8_t& out_g,
                            uint8_t& out_b,
                            uint32_t& out_particle_id) const override;
    bool read_gpu_framebuffer(int x, int y,
                              uint8_t& out_r, uint8_t& out_g,
                              uint8_t& out_b) const override;
    bool read_gi_debug(int x, int y,
                       float& out_gi_r, float& out_gi_g,
                       float& out_gi_b,
                       float& out_shadow_lux) const override;

private:
    Engine* eng_;  // non-owning
};

} // namespace Rendering
} // namespace Logosphere

#endif // LOGOSPHERE_METAL_RENDERER_H
