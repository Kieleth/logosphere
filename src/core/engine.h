#ifndef ENGINE_H
#define ENGINE_H

#include "particle_system.h"
#include "vision_system.h"
#include "light_system.h"
#include "input_system.h"
#include "logosphere/physics/physics_system.h"
#include "camera_system.h"
#include "camera_director.h"
#include "time_system.h"
#include "entity_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/animation/serpent_locomotion.h"
#include "logosphere/animation/butterfly_flight.h"
#include "logosphere/kg/kg_types.h"
#include "unified_test_runner.h"
#include "engine_metrics.h"
#include "core/telemetry.h"
#include "debug_overlay.h"  // NEW: Extracted debug visualization
// engine.h only ever names GLFWwindow through a pointer, so the opaque
// typedef (identical to the one glfw3.h itself makes) is enough. TUs
// that call GLFW functions include <GLFW/glfw3.h> themselves. This
// keeps engine.h compilable in the GLFW-less physics profile.
typedef struct GLFWwindow GLFWwindow;
#include "core/projection_mode.h"                             // ProjectionMode enum
#include "projection_system.h"                                // *Projection objects (members)
#include "object_id.h"                                        // ObjectID constants
#include "logosphere/rendering/font_renderer.h"               // FontRenderer (member)
#include "logosphere/rendering/primitive_renderer.h"          // PrimitiveRenderer (member)
#include "logosphere/rendering/coordinate_transformer.h"      // CoordinateTransformer (member)
#include "logosphere/rendering/depth_buffer.h"                // DepthBuffer (member)
#include "logosphere/rendering/pixel_buffer.h"                // PixelBuffer (members)
#include "logosphere/rendering/pixel_shader.h"                // LightingShader (member)
#include "logosphere/rendering/render_pipeline.h"             // RenderPipeline (member)
#include "logosphere/rendering/resolution_manager.h"          // ResolutionManager (member)
#include "logosphere/rendering/sparse_object_map.h"           // SparseObjectMap (member)
#include "logosphere/rendering/surface_rasterizer.h"          // SurfaceRasterizer (member)
#include "scene_manager.h"  // NEW: Extracted scene management
#include "logosphere/kg/kg_module.h"  // Knowledge Graph - core engine system
#include "logosphere/worldgen/worldgen_system.h"  // Procedural world generation
#include "celestial/celestial_system.h"  // Sun, moon, stars - celestial body management
#include "entity_manager.h"  // Entity type polymorphism system
#include "logosphere/llm/llm_system.h"  // LLM text generation (optional, heavy resource)
#include "logosphere/llm/llm_system_http.h"  // HTTP LLM backend (MLX-LM, OpenAI, Anthropic)
#include "spatial/entity_spatial.h"  // Entity-aware spatial queries
#include "player_controller.h"  // Player input handling (mouse look, WASD, abilities)
#include "logosphere/events/event_bus.h"  // Two-tier event system (signals + log)
#include "logosphere/capability/capability_store.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/entity_physical_state.h"
#include "logosphere/core/ground_locator.h"  // KG-declared contact policy
#include "humanoid_integrity_monitor.h"  // Opt-in dismemberment/collapse monitor
#include "deep_probe.h"                  // Custom live-state probes (watchpoints)
#include "particle_tracer.h"             // Correlation-ID write tracing for particles
#include "logosphere/rendering/i_renderer.h"  // IRenderer interface (Phase 4)
#include "logosphere/display/i_display.h"     // IDisplay interface (Phase 4)
#include "logosphere/rendering/draw_surface.h"  // Engine-owned IDrawSurface
#include "logosphere/rendering/pixel_picker.h"   // mouse hit-testing
#include "logosphere/effects/mutation_playback.h"  // KGOp rez-in plays
#include <chrono>
#include <memory>
#include <string>

// Forward declarations
class Engine;
class CameraController;

namespace Logosphere {
    class IApplication;
}

namespace Platform {
    class IPlatformSystem;
}

// EDUCATIONAL NOTE: The Engine Class
//
// This is the main coordinator for our game engine. In professional games,
// this class would be called "GameEngine", "Engine", or "Application".
//
// The Engine's responsibilities:
// 1. System Management - Creating and coordinating all subsystems
// 2. Main Loop - Running the game's update/render cycle
// 3. Window Management - Handling GLFW window creation and events
// 4. Resource Management - Eventually loading/unloading game assets
// 5. Scene Management - Eventually managing different game states/levels
//
// Benefits of this architecture:
// - Clear separation of concerns (each system handles one aspect)
// - Easy to test individual systems in isolation
// - Systems can be easily swapped or upgraded
// - Reduces coupling between different game components
// - Makes debugging much easier (know which system to check)
//
// This is similar to Unity's GameObject system or Unreal's Actor system,
// but simplified for educational purposes.


// Engine execution modes. Signals BEHAVIOR (verbose logging, debug
// overlays), not capability. Whether the engine has a presentation
// surface is controlled by EngineConfig::create_display, not by
// mode. "Headless" is no longer a mode — it's simply
// create_display == false.
enum class EngineMode {
    Interactive,  // Normal windowed gameplay.
    Debug         // Full introspection, state dumps, debug overlays.
};

// Engine configuration
struct EngineConfig {
    int window_width = 1600;  // Doubled for Retina displays
    int window_height = 1200; // Doubled for Retina displays
    const char* window_title = "Logosphere Engine";
    double target_fps = 60.0;
    bool show_debug_overlay = false;
    bool show_shadowcasting_debug = false;
    bool show_performance_metrics = false;
    bool show_kg_inspector = false;  // Knowledge Graph Inspector overlay
    bool show_time_display = false;   // Game time HUD overlay (\ key)
    bool enable_chat_window = false;  // LLM chat window (requires LLM system)
    EngineMode mode = EngineMode::Interactive;  // Default to normal gameplay
    // Whether the Engine should create a Display (window + Metal
    // swapchain). false = fully offscreen rendering, no window. Pair
    // with Engine::read_latest_framebuffer for GPU acceptance tests.
    bool create_display = true;
    
    // Global debug settings for lighting system
    bool debug_light_casting = false;     // Log light casting start/end
    bool debug_light_tracing = false;     // Log individual ray directions  
    bool debug_light_intersections = false;   // Log ray-surface intersections
    bool debug_light_accumulation = false;    // Log light accumulation on surfaces
    bool debug_origin_lights = false;         // Enable debug for lights at origin
    bool debug_light_verbose = false;         // Enable verbose output
};

class Engine {
public:
    // Constructor and destructor
    Engine();
    explicit Engine(Logosphere::IApplication* app);
    ~Engine();
    
    // Engine lifecycle - returns primordial observer particle ID (-1 on failure)
    int initialize(const EngineConfig& config = EngineConfig());
    // REMOVED: run() - Engine is a library, applications control the loop
    void shutdown();
    
    // Core engine services - applications drive these
    void update(double delta_time);  // Update all systems with delta time
    void render();                   // Render the current frame
    
    // Convenience methods
    void present();  // Present framebuffer to window
    // Windowed frame chain instrumentation. A windowed app must drive
    // update() -> render() -> present(); forgetting present() shows an
    // unpainted (white) window while the framebuffer renders perfectly
    // offscreen — a failure mode that cost a full debugging session.
    // render() warns loudly when a display exists and nothing has been
    // presented after 60 renders; tests assert the counters directly.
    uint64_t renders_completed() const { return renders_completed_; }
    uint64_t presents_completed() const { return presents_completed_; }
    void stop() { is_running_ = false; }
    bool should_continue() const;  // Check if engine should keep running
    
    // Window handle access (for GLFW operations)
    void* get_window_handle() const;
    
    // System access (for external tools like tests)
    ParticleSystem& get_particle_system() { return particle_system_; }

    // Internal particle creation (for tests and legacy code)
    // Prefer entity-bound API: ps.add_particle_to_entity() for production code
    int add_particle(const Particle& p);
    VisionSystem& get_vision_system() { return vision_system_; }
    LightSystem& get_light_system() { return light_system_; }
    InputSystem& get_input_system() { return input_system_; }
    PhysicsSystem& get_physics_system() { return physics_system_; }
    CameraSystem& get_camera_system() { return camera_system_; }
    CameraDirector& get_camera_director() { return camera_director_; }
    TimeSystem& get_time_system() { return time_system_; }

    // How many times THIS engine has been updated. Public because it
    // is the only way to observe that two engines in one process are
    // independent, which they were not until recently.
    uint64_t update_count() const { return update_count_; }
    EntitySystem& get_entity_system() { return *entity_system_; }
    // Renderer/Display split (Phase 4). Engine composes both; headless
    // mode is simply display_ == nullptr. Accessors return by
    // reference because the Engine always owns a Renderer; a Display
    // may or may not exist depending on mode.
    Logosphere::Rendering::IRenderer& get_renderer() { return *renderer_; }
    Logosphere::Display::IDisplay* get_display() { return display_.get(); }
    // Engine-owned drawing surface for UI widgets, HUDs, and debug
    // overlays. Callers receive it as the IDrawSurface interface so
    // the concrete implementation can change without touching them.
    IDrawSurface& get_draw_surface() { return *draw_surface_; }
    Logosphere::Rendering::PixelPicker& get_pixel_picker() { return *pixel_picker_; }
    Logosphere::Effects::MutationPlaybackRegistry& get_mutation_playback_registry() {
        return mutation_playback_registry_;
    }

    // Render-state accessors. These used to forward through
    // EngineRenderState; the state now lives directly on Engine.
    PixelBuffer& get_render_buffer()           { return render_buffer_; }
    // The UI overlay plane (see ui_buffer_). UISystem rasters here.
    PixelBuffer& get_ui_overlay_buffer()       { return ui_buffer_; }

    // Builds (scene OVER ui) for the overlay's dirty rect into
    // overlay_staging_ WITHOUT mutating the scene buffer — exactly the
    // pixels present() uploads as its second sub-region. Returns false
    // when the UI painted nothing. Callers on the GPU path must hold the
    // frame mutex (this reads the scene buffer).
    // Public so the regression lock can assert on presentable content
    // without a window (test_ui_overlay_survives_frames).
    bool build_overlay_staging(int& x, int& y, int& w, int& h);
    const std::vector<uint32_t>& get_overlay_staging() const { return overlay_staging_; }
    PixelBuffer& get_framebuffer_buffer()      { return framebuffer_; }
    PixelBuffer& get_background_buffer()       { return background_buffer_; }
    RenderPipeline& get_render_pipeline()      { return render_pipeline_; }
    DepthBuffer& get_depth_buffer()            { return depth_buffer_; }
    LightingShader& get_default_lighting_shader() { return default_lighting_shader_; }
    SurfaceRasterizer& get_surface_rasterizer() { return surface_rasterizer_; }
    SparseObjectMap& get_object_map()          { return object_map_; }
    FontRenderer& get_font_renderer()          { return font_renderer_; }
    PrimitiveRenderer& get_primitive_renderer() { return primitive_renderer_; }
    CoordinateTransformer& get_coord_transformer() { return coord_transformer_; }
    ResolutionManager& get_resolution_manager() { return resolution_manager_; }
    float get_pixels_per_world_unit() const    { return pixels_per_world_unit_; }
    void  toggle_light_visualization()         { show_lights_as_white_ = !show_lights_as_white_; }
    bool  get_show_lights_as_white() const     { return show_lights_as_white_; }

    // Projection control
    void cycle_projection_mode();
    void set_projection_mode(ProjectionMode mode);
    ProjectionMode get_projection_mode() const { return projection_mode_; }
    ProjectionSystem* get_current_projection() const;

    // Render-state mutators that touch multiple buffers / camera.
    void clear_framebuffer(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void copy_buffer(const PixelBuffer& source, PixelBuffer& dest);
    void set_render_resolution(int width, int height);
    ParticleDynamicsSystem& get_dynamics_system() { return dynamics_system_; }
    logosphere::animation::HumanoidLocomotion& get_humanoid_locomotion() { return humanoid_locomotion_; }
    logosphere::animation::SerpentLocomotion& get_serpent_locomotion() { return serpent_locomotion_; }
    logosphere::animation::ButterflyFlight& get_butterfly_flight() { return butterfly_flight_; }
    PlayerController& get_player_controller() { return player_controller_; }
    class KeyMapper& get_key_mapper() { return *key_mapper_; }
    class UISystem* get_ui_system() { return ui_system_; }
    class CameraController* get_camera_controller() { return camera_controller_; }
    kg::KGModule& get_kg() { return kg_module_; }
    WorldGenSystem& get_worldgen_system() { return worldgen_system_; }
    Celestial::CelestialSystem& get_celestial_system() { return celestial_system_; }
    EntityManager& get_entity_manager() { return *entity_manager_; }
    Logosphere::LLMSystemHTTP* get_llm_system() { return llm_system_; }  // May be null if not initialized
    spatial::EntitySpatialQuery& get_spatial_query() { return *spatial_query_; }
    logosphere::EventBus& get_event_bus() { return event_bus_; }
    // Particle interaction model: games register/load profiles here;
    // the physics broad phase consults it (empty = today's behavior).
    logosphere::interaction::ParticleInteractionSystem& get_interaction_system() {
        return interaction_system_;
    }

    // Capability store: tracked entities' profiles, recomputed on
    // state change, written back to the KG as capability.* (#37).
    capability::CapabilityStore& get_capability_store() {
        return capability_store_;
    }
    // Where a body can be. Ask this instead of assuming a height:
    // there is no floor at zero, the ground is particles, and it
    // moves. See logosphere/core/ground_locator.h.
    logosphere::GroundLocator& get_ground_locator() {
        return ground_locator_;
    }
    // Ontology levers -> bodies. Armed during initialize(), so a
    // property set on an entity (solver_authority, BONDED_TO) reaches
    // its particles immediately, in every game, without game code.
    logosphere::EntityPhysicalState& get_entity_physical_state() {
        return entity_physical_state_;
    }
    Logosphere::HumanoidIntegrityMonitor& get_humanoid_integrity_monitor() {
        return humanoid_integrity_monitor_;
    }
    Logosphere::DeepProbeManager& get_deep_probe_manager() {
        return deep_probe_manager_;
    }
    ParticleTracer& get_particle_tracer() { return particle_tracer_; }

    // Application access (for systems that need to call application handlers)
    Logosphere::IApplication* getApplication() { return application_; }

    // Engine state queries
    bool is_running() const { return is_running_; }
    const EngineMetrics& get_metrics() const { return metrics_; }
    Platform::IPlatformSystem* get_platform() const { return platform_; }
    
    // Test context creation
    class TestContext create_test_context();
    
    // Debug flag access (delegates to DebugOverlay)
    bool get_show_debug_overlay() const { return debug_overlay_.is_enabled(); }
    bool get_show_shadowcasting_debug() const { return config_.show_shadowcasting_debug; }
    void toggle_debug_overlay();  // Implementation in engine.cpp for debug possession mode
    void toggle_shadowcasting_debug() { config_.show_shadowcasting_debug = !config_.show_shadowcasting_debug; }
    void toggle_sun();  // Toggle sun on/off
    void create_sun();  // Create sun (called at init)
    DebugOverlay& get_debug_overlay() { return debug_overlay_; }
    
    // Resolution switching (delegates to EngineRenderState's ResolutionManager)
    void decrease_resolution();  // Implemented in engine.cpp
    void increase_resolution();  // Implemented in engine.cpp
    
    // KG Inspector
    bool get_show_kg_inspector() const { return config_.show_kg_inspector; }
    void toggle_kg_inspector() { config_.show_kg_inspector = !config_.show_kg_inspector; }
    bool get_show_time_display() const { return config_.show_time_display; }
    void toggle_time_display() { config_.show_time_display = !config_.show_time_display; }
    
    // Camera control removed - belongs in CameraSystem
    
    
    // Mode accessor
    EngineMode get_mode() const { return mode_; }
    
    // Input target system - games specify which entity receives input
    void set_input_target_entity(kg::EntityID entity_id);
    kg::EntityID get_input_target_entity() const { return input_target_entity_id_; }

    // Camera follow system - camera tracks input target entity with optional deadzone
    void set_camera_follow_enabled(bool enabled) { camera_follow_enabled_ = enabled; }
    void set_camera_deadzone(float size);  // Set deadzone box size (0 = immediate follow)
    void set_camera_follow_offset(float offset_y);  // Offset target on screen (positive = target appears lower)

    // Vision cone - fog-of-war effect that darkens pixels outside viewer's FOV
    // Enabled per-app, runs as GPU Pass 4 after lighting
    void set_vision_cone_enabled(bool enabled);
    bool get_vision_cone_enabled() const;
    void set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                         float fov_radians, float range);
    void set_vision_cone_style(float inner_falloff, float darkness);
    void set_vision_cone_focus(float focus_x, float focus_y, float focus_radius = 3.0f);
    // LOS occlusion mask. count must equal
    // GPURasterizer::kVisionConeOcclusionBins (64) to enable.
    void set_vision_cone_occlusion(const float* distances, int count);
    void clear_vision_cone_occlusion();

    // Vision memory grid — world-space "what the viewer just saw"
    // buffer that decays over time. Pairs with set_vision_cone(...)
    // and (optionally) set_vision_cone_occlusion(...).
    void set_vision_memory_enabled(bool enabled);
    void set_vision_memory_extent(float min_x, float min_y,
                                  float max_x, float max_y,
                                  int cells_per_side);
    void set_vision_memory_decay(float decay_seconds, float memory_dim);
    void update_vision_memory(float dt);

    // Synchronous framebuffer read-back for headless acceptance tests.
    // Blocks until the most recent GPU frame is complete, then copies
    // width*height BGRA pixels into out_pixels. Caller owns the buffer.
    // Returns false in headless modes that have no render system, or
    // before the first frame has been dispatched.
    bool read_latest_framebuffer(uint32_t* out_pixels,
                                 int& out_width, int& out_height);

    // Physics enable/disable - use during loading screens to prevent BVH queries on incomplete scenes
    // When re-enabling (false→true), runs internal settling frames to prevent constraint explosions
    void set_physics_enabled(bool enabled);
    bool get_physics_enabled() const { return physics_enabled_; }

    // Cinematic pause — freezes simulation (physics, AI, anything
    // that ticks on game time) while keeping the renderer and any
    // real-time-driven effects (camera dollies, particle
    // animations, HUD updates) running. Conceptually distinct
    // from a user-pressed pause: cinematic pause is engine-driven
    // for cutscenes / wow-moments while a remote agent is
    // thinking. Wraps TimeSystem::pause/resume; systems that need
    // to keep animating during the pause should drive themselves
    // with real_delta_time, not game_delta_time. Game-affecting
    // input is suppressed (consumers check is_cinematic_paused()
    // and short-circuit). Generic engine capability — any game
    // can use it.
    void set_cinematic_pause(bool paused);
    bool is_cinematic_paused() const { return cinematic_paused_; }

    // Chunk pre-loading - load chunks around a position at startup
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // LLM system initialization (optional - only call if game needs text generation)
    // Returns true on success, false if already initialized or load failed
    bool initialize_llm(const std::string& model_path);

    // REMOVED: Observer system - will be reimplemented when vision system is designed
    
    // Data access for systems (Engine as Data Broker pattern)
    // THREAD SAFETY: Use get_particle_copy() for safe access
    Particle get_particle_copy(int id) const { return particle_system_.get_particle_copy(id); }
    int get_particle_count() const { return static_cast<int>(particle_system_.count()); }
    
    // Main loop phases already declared above
    
private:
    // Render helpers
    // Copies the telemetry phases consumers read into metrics_. Called once
    // per frame, at frame end. Keeps EngineMetrics as the published
    // consumer-facing snapshot while collection stays enum-keyed.
    void publish_metrics();

    void render_time_display();  // Draws game time HUD when enabled
    // Rasters all UI into the OVERLAY PLANE (ui_buffer_), not the scene
    // buffer. Needs no lock: nothing else writes that plane.
    void draw_ui_overlays();
    // Blends the overlay plane's dirty region over the scene buffer.
    // GPU path callers MUST hold the frame mutex (the completion
    // callback memcpys the scene under it).
    void composite_ui_overlay();

    // EDUCATIONAL NOTE: Engine State Management
    //
    // The engine maintains its own state separate from the subsystems.
    // This includes:
    // - Window handle (GLFW window)
    // - Running state (should the main loop continue?)
    // - Configuration (window size, target FPS, etc.)
    // - Performance metrics (for optimization and debugging)
    // - Test exit code (for CI/CD integration)
    
    // REMOVED: setup_initial_scene() - now handled by SceneManager class
    
    // Engine state
    bool is_running_;
    bool is_initialized_;
    uint64_t renders_completed_ = 0;    // frame-chain instrumentation
    uint64_t presents_completed_ = 0;
    EngineConfig config_;
    EngineMode mode_;  // Current execution mode
    bool create_display_ = true;  // Whether the Engine created a Display.
    
    // Input target system
    kg::EntityID input_target_entity_id_ = kg::INVALID_ENTITY;  // Which entity receives input
    kg::EntityID saved_input_target_for_debug_ = kg::INVALID_ENTITY;  // Saved input target when entering debug mode

    // Camera follow system
    bool camera_follow_enabled_ = false;  // Whether camera should follow input target entity

    // Physics system toggle (disabled during loading to prevent BVH stack overflow)
    bool physics_enabled_ = true;

    // Cinematic pause flag (set via set_cinematic_pause). Physics
    // and AI freeze; renderer and cinematic-driven effects keep
    // running on real time. Game-side input handlers should
    // short-circuit when this is true.
    bool cinematic_paused_ = false;

    // Sun system
    size_t sun_particle_id_ = static_cast<size_t>(-1);
    bool sun_exists_ = false;

    // Resolution change debouncing (prevents GPU crashes from rapid key presses)
    std::chrono::steady_clock::time_point last_resolution_change_ = {};
    static constexpr double RESOLUTION_CHANGE_COOLDOWN_MS = 500.0;  // 500ms cooldown

    // Frame-based stabilization (ensures GPU triple-buffers cycle before next change)
    int frames_since_resolution_change_ = 999;  // Start high (stable state)
    static constexpr int RESOLUTION_STABILIZATION_FRAMES = 5;  // Wait 5 frames after change

    // True once render() has ever run. A purely headless session (tests,
    // render-free profile, servers) must flush deferred particle
    // deletions from update() — render()'s flush never runs and queued
    // deletions would leak forever (foot-plant pin anchors accumulate at
    // ~2 per walk second). Once anything has rendered, the render path
    // owns the flush behind its GPU/worker completion waits.
    bool has_rendered_ = false;

    // Platform system (owns window and OS interaction)
    Platform::IPlatformSystem* platform_;
    
    // Application interface (for platform-specific code)
    Logosphere::IApplication* application_;

    // DEPENDENCY INJECTION SOLUTION - Engine owns all systems
    //
    // Engine creates and owns all systems, eliminating globals.
    // Systems can access each other through Engine if needed.
    // This ensures proper initialization order and cleanup.
    
    // System instances (owned by Engine)
    ParticleSystem particle_system_;
    VisionSystem vision_system_;
    LightSystem light_system_;
    InputSystem input_system_;
    PhysicsSystem physics_system_;
    CameraSystem camera_system_;  // Camera extracted from EngineRenderState
    TimeSystem time_system_;  // Centralized time control for FPS-independent gameplay
    CameraDirector camera_director_;  // Cinematic dolly wrapper around CameraSystem
    ParticleDynamicsSystem dynamics_system_;  // Entity behaviors and animations
    logosphere::animation::HumanoidLocomotion humanoid_locomotion_;  // Refactor B0: scaffold for humanoid policy moving out of dynamics
    logosphere::animation::SerpentLocomotion serpent_locomotion_;    // Refactor B19: serpent module split
    logosphere::animation::ButterflyFlight butterfly_flight_;        // Refactor B20: butterfly module split
    PlayerController player_controller_;  // Player input (mouse look, WASD, abilities)
    kg::KGModule kg_module_;  // Knowledge Graph - core engine system
    class EntitySystem* entity_system_;  // Entity management system
    class EntityManager* entity_manager_;  // Entity type polymorphism
    // ---- Render-state members (formerly EngineRenderState) -------------
    // Init flag, debug toggles, projection selector
    bool render_initialized_ = false;
    bool show_lights_as_white_ = false;
    ProjectionMode projection_mode_ = ProjectionMode::Isometric;

    // Render / framebuffer dimensions cached at init / set_render_resolution
    int render_width_       = 0;
    int render_height_      = 0;
    int framebuffer_width_  = 0;
    int framebuffer_height_ = 0;
    float pixels_per_world_unit_ = 0.0f;

    // Projection objects — one of each, get_current_projection picks
    IsometricProjection      isometric_projection_;
    IsometricDepthProjection isometric_depth_projection_;
    PerspectiveProjection    perspective_projection_;
    CabinetProjection        cabinet_projection_;
    BirdsEyeProjection       birds_eye_projection_;

    // Pixel buffers — render-side and display-side
    PixelBuffer framebuffer_;        // Final output at framebuffer resolution
    PixelBuffer render_buffer_;      // Game render at chosen render resolution
    PixelBuffer ui_buffer_;          // UI OVERLAY PLANE (ARGB, alpha 0 = untouched),
                                     // render resolution. Rastered on the UI's own
                                     // schedule, composited at present.
                                     // the UI overlay design notes
    PixelBuffer background_buffer_;  // Persistent background

    DepthBuffer depth_buffer_;

    // Rendering primitives — direct value members, no null checks
    FontRenderer          font_renderer_;
    PrimitiveRenderer     primitive_renderer_;
    CoordinateTransformer coord_transformer_;
    SurfaceRasterizer     surface_rasterizer_;
    RenderPipeline        render_pipeline_;
    ResolutionManager     resolution_manager_;
    SparseObjectMap       object_map_{0, 0};
    LightingShader        default_lighting_shader_;

    // Reusable buffer for framebuffer extraction (avoids alloc per frame)
    mutable std::vector<uint8_t> bgra_extraction_buffer_;
    // ---------------------------------------------------------------------

    // KGOp visual playback registry (Phase D). Games register
    // per-(type, on-create) and per-(type, property, on-set)
    // factories at init; the cinematic feeds each applied op
    // through it. Ticked from update() on real time so plays
    // survive cinematic pause.
    Logosphere::Effects::MutationPlaybackRegistry mutation_playback_registry_;

    // Renderer/Display split (Phase 4): owned IRenderer + optional
    // IDisplay. The IRenderer impl wraps the engine's render-state
    // members directly; the IDisplay impl wraps PlatformMacOS.
    std::unique_ptr<Logosphere::Rendering::IRenderer> renderer_;
    std::unique_ptr<Logosphere::Display::IDisplay>    display_;
    std::unique_ptr<Logosphere::Rendering::DrawSurface> draw_surface_;
    // Draw surface bound to the overlay plane; widgets render through this.
    std::unique_ptr<Logosphere::Rendering::DrawSurface> overlay_surface_;
    // (scene OVER ui) staging pixels for the overlay's dirty rect, BGRA.
    std::vector<uint32_t> overlay_staging_;
    std::unique_ptr<Logosphere::Rendering::PixelPicker> pixel_picker_;
    class KeyMapper* key_mapper_;  // Forward declaration
    class UISystem* ui_system_;  // Forward declaration
    class CameraController* camera_controller_;  // Camera movement controller
    
    // Published per-frame summary for consumers (debug overlay, UI, tests).
    // Collection itself lives in logosphere::telemetry — enum-keyed phase
    // timers and counters, no string hashing in the frame path. Engine
    // copies the phases consumers care about into this struct at frame end.
    // See docs/PERFORMANCE_RESEARCH.md.
    EngineMetrics metrics_;
    bool telemetry_frame_open_ = false;  // guards the frame-boundary finalize
    double fps_window_accum_ = 0.0;      // ms accumulated in the min/max window
    
    // Resolution management - REMOVED: Now using EngineRenderState's ResolutionManager
    // ResolutionManager resolution_manager_;  // REMOVED - use render_system_->get_resolution_manager()
    
    // Debug visualization - NOW using DebugOverlay (direct member, stack allocated)
    DebugOverlay debug_overlay_;  // Replaces scattered debug rendering code
    
    // Scene management - NOW using SceneManager (direct member, stack allocated)
    SceneManager scene_manager_;  // Replaces scattered scene setup code

    // World generation - Procedural world construction
    WorldGenSystem worldgen_system_;  // Manages all procedural generators (floor, trees, rocks, etc.)

    // Celestial system - Sun, moon, stars with orbital mechanics
    Celestial::CelestialSystem celestial_system_;

    // LLM system - Optional text generation (only initialized if game requests it)
    Logosphere::LLMSystemHTTP* llm_system_;  // HTTP LLM backend (MLX-LM/OpenAI/Anthropic), null until initialized

    // Spatial query system - Entity-aware spatial queries bridging ParticleSystem and KG
    spatial::EntitySpatialQuery* spatial_query_;  // Initialized in constructor

    // Event bus - Two-tier event system (synchronous signals + append-only log)
    logosphere::EventBus event_bus_;
    logosphere::interaction::ParticleInteractionSystem interaction_system_;
    capability::CapabilityStore capability_store_;
    logosphere::EntityPhysicalState entity_physical_state_;
    logosphere::GroundLocator ground_locator_;
    Logosphere::HumanoidIntegrityMonitor humanoid_integrity_monitor_;
    Logosphere::DeepProbeManager        deep_probe_manager_;
    // How many times THIS engine has been updated. It was a
    // function-local static, which made it shared by every Engine in
    // the process: a second engine started at the first one's frame
    // number, and it is handed to the integrity monitor and the deep
    // probes, so their windows landed somewhere arbitrary. Any test
    // that builds two engines in one process hit this.
    uint64_t                            update_count_ = 0;
    ParticleTracer                      particle_tracer_;

    // Timing
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using TimeDuration = std::chrono::duration<double>;
};

#endif // ENGINE_H