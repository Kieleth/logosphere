#include "metal_renderer.h"

#include "core/engine.h"
#include "logosphere/rendering/render_pipeline.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/depth_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "logosphere/rendering/pixel_shader.h"
#include "logosphere/rendering/resolution_manager.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "core/light_system.h"
#include "engine_metrics.h"
#include "optimization_flags.h"

#include <chrono>
#include <iostream>

namespace Logosphere {
namespace Rendering {

MetalRenderer::MetalRenderer(Engine& engine)
    : eng_(&engine) {}

bool MetalRenderer::initialize(const RendererConfig& /*config*/) {
    // Initialization handled by Engine; nothing to do here.
    return true;
}

void MetalRenderer::shutdown() {
    // Owned by Engine.
}

void MetalRenderer::draw(const ParticleSystem& particles,
                         CameraSystem& camera,
                         LightSystem& lights,
                         EngineMetrics* metrics) {
    auto& pipeline = eng_->get_render_pipeline();
    pipeline.set_show_lights_as_white(eng_->get_show_lights_as_white());

    auto render_start = std::chrono::high_resolution_clock::now();

    pipeline.render(
        particles,
        eng_->get_render_buffer(),
        eng_->get_depth_buffer(),
        eng_->get_object_map(),
        camera,
        lights,
        eng_->get_default_lighting_shader(),
        metrics);

    if (Optimizations::ENABLE_PROFILING) {
        auto render_end = std::chrono::high_resolution_clock::now();
        auto render_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            render_end - render_start).count();
        static int sample = 0;
        if (sample++ % Optimizations::PROFILE_SAMPLE_RATE == 0) {
            std::cout << "[RENDER] total: " << (render_duration / 1000.0f) << "ms" << std::endl;
        }
    }
}

void MetalRenderer::wait_for_completion() {
    eng_->get_render_pipeline().wait_for_gpu_completion();
}

bool MetalRenderer::read_framebuffer(uint32_t* out_pixels,
                                     int& out_width,
                                     int& out_height) {
    return eng_->get_render_pipeline().read_latest_framebuffer(
        out_pixels, out_width, out_height);
}

const PixelBuffer& MetalRenderer::get_framebuffer() const {
    return eng_->get_render_buffer();
}

int MetalRenderer::render_width()  const { return eng_->get_resolution_manager().get_render_width(); }
int MetalRenderer::render_height() const { return eng_->get_resolution_manager().get_render_height(); }

void MetalRenderer::set_vision_cone_enabled(bool enabled) {
    eng_->get_render_pipeline().set_vision_cone_enabled(enabled);
}

bool MetalRenderer::get_vision_cone_enabled() const {
    return eng_->get_render_pipeline().get_vision_cone_enabled();
}

void MetalRenderer::set_vision_cone(float viewer_x, float viewer_y,
                                    float look_direction,
                                    float fov_radians,
                                    float range) {
    eng_->get_render_pipeline().set_vision_cone(viewer_x, viewer_y,
                                                look_direction,
                                                fov_radians, range);
}

void MetalRenderer::set_vision_cone_style(float inner_falloff,
                                          float darkness) {
    eng_->get_render_pipeline().set_vision_cone_style(inner_falloff, darkness);
}

void MetalRenderer::set_vision_cone_focus(float focus_x, float focus_y,
                                          float focus_radius) {
    eng_->get_render_pipeline().set_vision_cone_focus(focus_x, focus_y, focus_radius);
}

void MetalRenderer::set_vision_cone_occlusion(const float* distances, int count) {
    eng_->get_render_pipeline().set_vision_cone_occlusion(distances, count);
}

void MetalRenderer::clear_vision_cone_occlusion() {
    eng_->get_render_pipeline().clear_vision_cone_occlusion();
}

void MetalRenderer::set_vision_memory_enabled(bool enabled) {
    eng_->get_render_pipeline().set_vision_memory_enabled(enabled);
}

void MetalRenderer::set_vision_memory_extent(float min_x, float min_y,
                                             float max_x, float max_y,
                                             int cells_per_side) {
    eng_->get_render_pipeline().set_vision_memory_extent(
        min_x, min_y, max_x, max_y, cells_per_side);
}

void MetalRenderer::set_vision_memory_decay(float decay_seconds, float memory_dim) {
    eng_->get_render_pipeline().set_vision_memory_decay(decay_seconds, memory_dim);
}

void MetalRenderer::update_vision_memory(float dt) {
    eng_->get_render_pipeline().update_vision_memory(dt);
}

bool MetalRenderer::is_frame_ready() const {
    return eng_->get_render_pipeline().is_frame_ready();
}

void MetalRenderer::mark_frame_presented() {
    eng_->get_render_pipeline().mark_frame_presented();
}

void MetalRenderer::reset_temporal_state() {
    eng_->get_render_pipeline().reset_temporal_state();
}

std::mutex& MetalRenderer::get_frame_mutex() {
    return eng_->get_render_pipeline().get_frame_mutex();
}

void MetalRenderer::advance_frame_counter() {
    eng_->get_surface_rasterizer().advance_frame();
}

int MetalRenderer::current_frame_number() const {
    return eng_->get_surface_rasterizer().get_frame_counter();
}

void MetalRenderer::wait_for_workers_completion() {
    eng_->get_surface_rasterizer().wait_for_workers_completion();
}

int MetalRenderer::acquire_all_gpu_slots() {
    return eng_->get_render_pipeline().acquire_all_gpu_slots();
}

void MetalRenderer::release_all_gpu_slots(int slots) {
    eng_->get_render_pipeline().release_all_gpu_slots(slots);
}

void MetalRenderer::set_kg_module(kg::KGModule* kg) {
    eng_->get_render_pipeline().set_kg_module(kg);
}

void MetalRenderer::set_hovered_entity(int entity_id_or_sentinel) {
    eng_->get_render_pipeline().set_hovered_entity(entity_id_or_sentinel);
}

bool MetalRenderer::read_gi_debug(int x, int y,
                                  float& out_gi_r, float& out_gi_g,
                                  float& out_gi_b,
                                  float& out_shadow_lux) const {
    return eng_->get_render_pipeline().read_gi_debug(x, y,
        out_gi_r, out_gi_g, out_gi_b, out_shadow_lux);
}

bool MetalRenderer::read_shadow_debug(int x, int y,
                                      uint32_t& out_sample_count,
                                      float& out_temporal_lux,
                                      uint32_t& out_prev_particle_id) const {
    return eng_->get_render_pipeline().read_shadow_debug(x, y,
        out_sample_count, out_temporal_lux, out_prev_particle_id);
}

bool MetalRenderer::read_gbuffer_debug(int x, int y,
                                       uint8_t& out_r, uint8_t& out_g,
                                       uint8_t& out_b,
                                       uint32_t& out_particle_id) const {
    return eng_->get_render_pipeline().read_gbuffer_debug(x, y,
        out_r, out_g, out_b, out_particle_id);
}

bool MetalRenderer::read_gpu_framebuffer(int x, int y,
                                         uint8_t& out_r, uint8_t& out_g,
                                         uint8_t& out_b) const {
    return eng_->get_render_pipeline().read_gpu_framebuffer(x, y,
        out_r, out_g, out_b);
}

} // namespace Rendering
} // namespace Logosphere
