#pragma once

#include "ui/ui_system.h"
#include "frame_metrics.h"
#include "engine_metrics.h"
#include "lighting_metrics.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class ParticleSystem;
class VisionSystem;
class CameraSystem;
class Engine;
namespace Logosphere { namespace Rendering { class IRenderer; } }

// DebugOverlay: All debug visualization and overlay logic
//
// Following principles from successful refactoring:
// - Single responsibility: ONLY handles debug visualization
// - Collects data from various systems and prepares for UI
// - No game logic, just presentation
//
// This was extracted from Engine.cpp where debug rendering was
// scattered throughout, especially in render_debug_overlay().

class DebugOverlay {
public:
    DebugOverlay();

    // Enable/disable control
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    // Hover state (for entity hover highlighting in debug mode)
    // Takes int: -1 for "no hover", or entity_id (including 0 for Entity 0)
    void set_hovered_entity(int entity_id_or_sentinel) { hovered_entity_ = entity_id_or_sentinel; }
    int get_hovered_entity() const { return hovered_entity_; }
    
    // Feature toggles
    void set_show_metrics(bool show) { show_metrics_ = show; }
    void set_show_grid(bool show) { show_grid_ = show; }
    void set_show_compass(bool show) { show_compass_ = show; }
    void set_show_memory(bool show) { show_memory_ = show; }
    
    // Data collection from systems
    void collect_metrics(const EngineMetrics& em);
    void collect_particle_info(const ParticleSystem& particles);
    void collect_vision_info(const VisionSystem& vision);
    void collect_camera_info(const CameraSystem& camera);
    void collect_resolution_info(const class ResolutionManager& res_mgr);
    void collect_lighting_metrics();  // From global LightingMetrics
    void collect_frame_metrics();     // From global FrameMetrics
    
    // Set projection mode name
    void set_projection_mode(const char* mode_name) { projection_mode_name_ = mode_name; }
    
    // Main render call - prepares and sends all debug info to UISystem
    void render(UISystem* ui_system, Engine* engine,
                Logosphere::Rendering::IRenderer* renderer);
    
private:
    bool enabled_;
    bool show_metrics_;
    bool show_grid_;
    bool show_compass_;
    bool show_memory_;

    // Hover state (-1 = no hover, allows Entity 0 to be valid)
    int hovered_entity_ = -1;

    // Cached debug information
    UISystem::EngineDebugInfo debug_info_;
    const char* projection_mode_name_;
    
    // Helper methods
    void prepare_performance_metrics();
    void prepare_system_timings();
    void prepare_render_pipeline_metrics();
    void prepare_lighting_metrics();
    void prepare_bvh_metrics();
    void prepare_game_state();
    
    // Get current projection mode name
    const char* get_projection_mode_name(int mode) const;
};