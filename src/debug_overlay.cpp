#include "debug_overlay.h"
#include "core/particle_system.h"
#include "vision_system.h"
#include "core/engine.h"
#include "core/camera_system.h"
#include "logosphere/rendering/resolution_manager.h"
#include "logosphere/rendering/i_renderer.h"

DebugOverlay::DebugOverlay()
    : enabled_(false)
    , show_metrics_(true)
    , show_grid_(false)
    , show_compass_(false)
    , show_memory_(false)
    , projection_mode_name_("Unknown")
{
    // Initialize debug info with sensible defaults
    debug_info_ = {};
}

void DebugOverlay::collect_metrics(const EngineMetrics& em) {
    
    // FPS metrics
    debug_info_.current_fps = em.current_fps;
    debug_info_.min_fps = em.min_fps;
    debug_info_.max_fps = em.max_fps;
    debug_info_.total_frame_time = em.total_frame_time;
    
    // System timings
    debug_info_.input_time = em.input_time;
    debug_info_.physics_time = em.physics_time;
    debug_info_.vision_time = em.vision_time;
    debug_info_.lighting_time = em.lighting_time;
    debug_info_.render_time = em.render_time;
    debug_info_.render_clear_time = em.render_clear_time;
    debug_info_.render_particles_time = em.render_particles_time;
    debug_info_.ui_time = em.ui_time;
    
    // Render pipeline timings
    debug_info_.render_culling_time = em.render_culling_time;
    debug_info_.render_projection_time = em.render_projection_time;
    debug_info_.render_sorting_time = em.render_sorting_time;
    debug_info_.render_rasterization_time = em.render_rasterization_time;
    
    // Per-pixel breakdown
    debug_info_.pixel_edge_test_time = em.pixel_edge_test_time;
    debug_info_.pixel_uv_calc_time = em.pixel_uv_calc_time;
    debug_info_.pixel_depth_time = em.pixel_depth_time;
    debug_info_.pixel_lighting_time = em.pixel_lighting_time;
    debug_info_.pixel_write_time = em.pixel_write_time;
    
    // Lighting subsystem metrics
    debug_info_.light_uv_to_world_time = em.light_uv_to_world_time;
    debug_info_.light_intensity_calc_time = em.light_intensity_calc_time;
    debug_info_.light_shadow_ray_time = em.light_shadow_ray_time;
    debug_info_.light_surface_fetch_time = em.light_surface_fetch_time;
    debug_info_.light_tone_mapping_time = em.light_tone_mapping_time;
    debug_info_.light_color_calc_time = em.light_color_calc_time;
    debug_info_.light_ray_count = em.light_ray_count;
    debug_info_.light_call_count = em.light_call_count;
    
    // Game state metrics
    debug_info_.particle_count = em.particle_count;
    debug_info_.surfaces_rendered = em.surfaces_rendered;
    debug_info_.surfaces_culled = em.surfaces_culled;
    debug_info_.surfaces_projected = em.surfaces_projected;
    debug_info_.pixels_tested = em.pixels_tested;
    debug_info_.pixels_drawn = em.pixels_drawn;
    debug_info_.rays_cast = em.rays_cast;
}

void DebugOverlay::collect_particle_info(const ParticleSystem& particles) {
    debug_info_.particle_count = static_cast<int>(particles.count());
    
    // Character information (if available)
    debug_info_.has_character = particles.count() > 0;
    if (debug_info_.has_character) {
        // THREAD SAFETY: Use get_particle_copy() for safe access
        Particle char_particle = particles.get_particle_copy(0);
        debug_info_.char_x = char_particle.x;
        debug_info_.char_y = char_particle.y;
        debug_info_.char_z = char_particle.z;
    }
}

void DebugOverlay::collect_vision_info(const VisionSystem& vision) {
    debug_info_.memory_size = static_cast<int>(vision.get_spatial_memory().size());
}

void DebugOverlay::collect_camera_info(const CameraSystem& camera) {
    // Get pixel to UV conversion metrics
    auto pixel_uv_metrics = camera.get_pixel_uv_metrics();
    debug_info_.pixel_uv_calls_per_frame = pixel_uv_metrics.calls_this_frame;
    
    // With triangles, we don't have fast/newton paths anymore
    debug_info_.pixel_uv_fast_path = 0;
    debug_info_.pixel_uv_newton_path = 0;
    debug_info_.pixel_uv_fast_rate = 0.0f;
    debug_info_.pixel_uv_newton_rate = 0.0f;
}

void DebugOverlay::collect_resolution_info(const ResolutionManager& res_mgr) {
    debug_info_.render_width = res_mgr.get_render_width();
    debug_info_.render_height = res_mgr.get_render_height();
    debug_info_.window_width = res_mgr.get_window_width();
    debug_info_.window_height = res_mgr.get_window_height();
    debug_info_.framebuffer_width = res_mgr.get_framebuffer_width();
    debug_info_.framebuffer_height = res_mgr.get_framebuffer_height();
    debug_info_.dpi_scale = res_mgr.get_window_to_framebuffer_scale();  // DPI scale
    debug_info_.render_scale = res_mgr.get_render_to_window_scale();    // Render scale
    
    // Get preset name
    switch (res_mgr.get_current_preset()) {
        case ResolutionManager::ResolutionPreset::ULTRA_LOW:
            debug_info_.resolution_preset = "Ultra Low (400x300)";
            break;
        case ResolutionManager::ResolutionPreset::LOW:
            debug_info_.resolution_preset = "Low (800x600)";
            break;
        case ResolutionManager::ResolutionPreset::MEDIUM:
            debug_info_.resolution_preset = "Medium (1280x960)";
            break;
        case ResolutionManager::ResolutionPreset::HIGH:
            debug_info_.resolution_preset = "High (1600x1200)";
            break;
        case ResolutionManager::ResolutionPreset::NATIVE:
            debug_info_.resolution_preset = "Native (Window Size)";
            break;
        case ResolutionManager::ResolutionPreset::RETINA:
            debug_info_.resolution_preset = "Retina (Framebuffer Size)";
            break;
        case ResolutionManager::ResolutionPreset::CUSTOM:
            debug_info_.resolution_preset = "Custom";
            break;
        default:
            debug_info_.resolution_preset = "Unknown";
            break;
    }
}

void DebugOverlay::collect_lighting_metrics() {
    LightingMetrics& lm = LightingMetrics::get();
    
    // BVH metrics
    debug_info_.bvh_enabled = lm.bvh_enabled;
    debug_info_.bvh_built = lm.bvh_built;
    debug_info_.bvh_rays_traced = lm.bvh_rays_traced;
    debug_info_.linear_rays_traced = lm.linear_rays_traced;
    debug_info_.bvh_nodes_visited = lm.bvh_nodes_visited;
    debug_info_.bvh_build_time = lm.bvh_build_time;
    
    // UV Coherence metrics
    debug_info_.uv_coherent_hits = lm.uv_coherent_hits;
    debug_info_.uv_coherent_misses = lm.uv_coherent_misses;
    
    // Calculate hit rate
    uint64_t total_uv = lm.uv_coherent_hits + lm.uv_coherent_misses;
    debug_info_.uv_coherence_rate = total_uv > 0 ? 
        (100.0f * lm.uv_coherent_hits / total_uv) : 0.0f;
    
    // Use sampled lighting metrics (1 in 10,000 samples)
    debug_info_.light_uv_to_world_time = lm.uv_to_world_time;
    debug_info_.light_intensity_calc_time = lm.intensity_calc_time;
    debug_info_.light_shadow_ray_time = lm.shadow_ray_time;
    debug_info_.light_surface_fetch_time = lm.surface_fetch_time;
    debug_info_.light_tone_mapping_time = lm.tone_mapping_time;
    debug_info_.light_color_calc_time = lm.color_calc_time;
}

void DebugOverlay::collect_frame_metrics() {
    const auto& fm = FrameMetrics::Get().display;
    
    // Use frame-based metrics for accurate timings
    debug_info_.render_time = fm.avg_render_time;
    
    // Use pixel_lighting_time for actual lighting duration
    if (debug_info_.pixel_lighting_time > 0.01) {
        debug_info_.lighting_time = debug_info_.pixel_lighting_time;
    } else if (fm.avg_lighting_time > 0) {
        debug_info_.lighting_time = fm.avg_lighting_time;
    }
    
    // Frame-based counters
    debug_info_.light_ray_count = static_cast<int>(fm.avg_shadow_rays);
    debug_info_.light_call_count = static_cast<int>(fm.avg_lighting_calls);
    debug_info_.pixels_tested = fm.avg_pixels;
    debug_info_.pixels_drawn = fm.avg_pixels;
}

void DebugOverlay::render(UISystem* ui_system, Engine* engine,
                          Logosphere::Rendering::IRenderer* renderer) {
    if (!enabled_ || !ui_system) return;

    // Configure hover highlighting through the Renderer (POLICY decision
    // by DebugOverlay). Engine provides MECHANISM (this call), DebugOverlay
    // decides WHAT to highlight. Always set (even INVALID) so the
    // highlight clears when the mouse moves off an entity.
    if (renderer) {
        renderer->set_hovered_entity(hovered_entity_);

        // DEBUG: Log when hovered entity changes
        static kg::EntityID last_logged_entity = kg::INVALID_ENTITY;
        if (hovered_entity_ != last_logged_entity) {
            std::cout << "[DEBUG_OVERLAY] Set hovered entity: " << hovered_entity_ << std::endl;
            last_logged_entity = hovered_entity_;
        }
    }

    // Set projection mode info
    if (engine) {
        int mode = static_cast<int>(engine->get_projection_mode());
        debug_info_.projection_mode = get_projection_mode_name(mode);
    }
    
    // Render hexagonal grid if enabled
    if (show_grid_) {
        ui_system->render_hexagonal_grid();
    }
    
    // Render world compass if enabled
    if (show_compass_) {
        ui_system->render_world_compass();
    }
    
    // Render the main debug overlay
    if (show_metrics_) {
        ui_system->render_engine_debug_overlay(debug_info_);
    }
}

const char* DebugOverlay::get_projection_mode_name(int mode) const {
    static const char* projection_names[] = {
        "Isometric", "Iso+Depth", "Perspective", "Cabinet", "Bird's Eye"
    };
    
    if (mode >= 0 && mode < 5) {
        return projection_names[mode];
    }
    return "Unknown";
}