#include "engine.h"
#include "physics_trace.h"
#include "rendering/metal_renderer.h"         // Phase 4: concrete IRenderer
#include "display/macos_display.h"            // Phase 4: concrete IDisplay
#include "logosphere_ontology_registry.h"
#include "game_time.h"
#include "lighting_metrics.h"
#include "frame_metrics.h"
#include "particle_system.h"
#include "vision_system.h"
#include "light_system.h"
#include "lighting_primitives.h"
#include "lighting_config.h"
#include "ui/ui_system.h"
#include "test_projections.h"
#include "key_mapper.h"
#include "main_key_handler.h"
#include "application.h"
#include "platform/platform_system.h"
#include "particle_geometry_v2.h"
#include "test_context.h"
#include "lighting_metrics.h"
#include "debug_control.h"  // Centralized debug control
#include "frame_metrics.h"  // Frame-based metrics accumulation
#include "camera_controller.h"  // Camera movement controller
#include "optimization_flags.h"  // For ENABLE_PROFILING flag
#include "logosphere/physics/physics_flags.h"       // For PhysicsSolver forensic logging flags
#include "entities/entity_activators.h"  // Entity type polymorphism
#include <cassert>
#include <iostream>
#include <iomanip>    // For std::setprecision - formatting profiling output
#include <thread>
#include <cmath>
#include <chrono>
#include <algorithm>  // For std::sort() - sorting surfaces by distance
#include <limits>     // For std::numeric_limits<float>::max() - finding minimum distance

// ============================================================================
// STAGE 3: Fixed Timestep Feature Flag
// ============================================================================
// Set to true to use fixed timestep physics (60 Hz, FPS-independent)
// Set to false to use old variable timestep (for comparison/safety)
//
// Fixed timestep BENEFITS:
//   - Deterministic: Same inputs → same outputs (regardless of FPS)
//   - No tunneling: Fast objects can't skip collision detection
//   - Stable: Constraint solvers converge reliably
//
// See docs/physics_and_time.md for full explanation
// ============================================================================
constexpr bool USE_FIXED_TIMESTEP = true;  // Stage 3: Enable new behavior
#include <vector>     // For std::vector - collecting surfaces before sorting

// EDUCATIONAL NOTE: Engine Implementation
//
// This file contains the main game loop and system coordination logic.
// Key concepts demonstrated:
//
// 1. RAII (Resource Acquisition Is Initialization)
//    - Constructor/destructor handle resource management
//    - Automatic cleanup when Engine goes out of scope
//
// 2. Main Game Loop Architecture
//    - Input -> Update -> Render -> Display cycle
//    - Fixed timestep for consistent physics
//    - Performance monitoring and FPS limiting
//
// 3. System Coordination
//    - Each system handles its own domain
//    - Engine orchestrates communication between systems
//    - Clean separation of concerns
//
// 4. Error Handling
//    - Graceful failure and resource cleanup
//    - Clear error messages for debugging

// Note: particles access now through particle_system_.get_particles() 
// Note: all other globals moved to proper systems
// Note: framebuffer access now through render_system_->get_framebuffer()
// Note: Using physics_system_.get_character() instead of global player_character
// Note: Debug flags now managed by Engine config (show_debug_overlay, show_shadowcasting_debug)

// Temporary access to main.mm functions during transition
// Note: All rendering functions moved to EngineRenderState

// Forward declarations for functions still in main.mm
// Note: display_framebuffer_macos() now handled through EngineRenderState
// Note: draw_string and draw_number are now accessed through EngineRenderState

// Global engine pointer (Phase 2 camera control)
// Forward declarations to avoid header dependencies
class EngineRenderState;
class KeyMapper;

// Frame profiling data (shared between update() and present())
namespace {
    double g_last_update_time = 0.0;
    double g_last_poll_time = 0.0;
    double g_last_movement_time = 0.0;
}

Engine::Engine()
    : Engine(nullptr)  // Delegate to main constructor
{
}

Engine::Engine(Logosphere::IApplication* app)
    : is_running_(false)
    , is_initialized_(false)
    , mode_(EngineMode::Interactive)        // Default mode
    , platform_(nullptr)
    , application_(app)  // Store application interface
    // Systems are value members, initialized by their constructors
    , kg_module_(logosphere::ontology::registry())  // KG defined by ontology
    , key_mapper_(nullptr)                  // Will be created in initialize
    , ui_system_(nullptr)                   // Will be created in initialize
    // MetricsCollector is a direct member, initialized automatically
{
    // ENGINE OWNS ALL SYSTEMS
    //
    // Engine creates and owns all systems as value members (not pointers).
    // This ensures proper initialization order, automatic cleanup,
    // and eliminates all global system instances.
    //
    // Systems that need special initialization are created in initialize()
    
    // Create heap-allocated systems
    key_mapper_ = new KeyMapper();
    camera_controller_ = new CameraController();
    entity_system_ = nullptr;  // Created later when we have KG module
    entity_manager_ = nullptr;  // Created later when we have KG module
    llm_system_ = nullptr;  // Optional - only created if game calls initialize_llm()
    spatial_query_ = new spatial::EntitySpatialQuery(particle_system_, kg_module_);
}

Engine::~Engine() {
    // Destructor ensures proper cleanup
    shutdown();
    
    // Clean up heap-allocated systems
    delete key_mapper_;
    delete ui_system_;
    delete camera_controller_;
    delete entity_system_;
    delete entity_manager_;
#ifdef HAS_LLAMA
    delete llm_system_;  // Cleanup LLM if it was initialized
#endif
    delete spatial_query_;  // Entity spatial query cleanup
}

int Engine::initialize(const EngineConfig& config) {
    if (is_initialized_) {
        DEBUG_LOG("Engine already initialized!");
        return -1;
    }

    config_ = config;
    mode_ = config.mode;
    create_display_ = config.create_display;

    // Only print initialization in interactive/debug modes
    if (mode_ == EngineMode::Interactive || mode_ == EngineMode::Debug) {
        DEBUG_LOG("Initializing Logosphere Engine in " << 
                  (mode_ == EngineMode::Interactive ? "Interactive" : "Debug") << 
                  " mode...");
    }

    // CRITICAL: Platform ALWAYS exists, even in headless mode
    // Platform provides consistent interface for framebuffer size, event polling, etc.
    // Headless platform simply returns configured sizes and no-ops for display
    std::cout << "[ENGINE] Creating platform system (required for ALL modes)..." << std::endl;
    
    // Create platform-specific implementation via the factory. The full
    // profile links create_platform_system() from platform_macos.mm
    // (PlatformMacOS); GLFW-less profiles link it from null_platform.cpp
    // (NullPlatform). Engine owns the raw pointer either way.
    platform_ = Platform::create_platform_system().release();
    std::cout << "[ENGINE] Created platform: " << platform_->get_platform_name() << std::endl;
    
    // Create window for non-headless modes
    if (create_display_) {
        std::cout << "[ENGINE] Mode is " << (mode_ == EngineMode::Interactive ? "Interactive" : "Debug") 
                  << " - creating window..." << std::endl;
        
        // Create window through platform system
        Platform::WindowConfig window_config;
        window_config.width = config_.window_width;
        window_config.height = config_.window_height;
        window_config.title = config_.window_title;
        window_config.vsync = true;
        
        std::cout << "[ENGINE] Calling platform->create_window() with size " 
                  << window_config.width << "x" << window_config.height 
                  << " title: " << window_config.title << std::endl;
        
        if (!platform_->create_window(window_config)) {
            std::cout << "[ENGINE] ERROR: Failed to create window through platform system!" << std::endl;
            DEBUG_LOG("Failed to create window through platform system");
            delete platform_;
            platform_ = nullptr;
            return -1;
        }
        
        std::cout << "[ENGINE] Window created successfully" << std::endl;
    } else {
        std::cout << "[ENGINE] Mode is Headless - skipping window creation" << std::endl;
    }

    // Initialize all systems
    if (mode_ == EngineMode::Interactive || mode_ == EngineMode::Debug) {
        DEBUG_LOG("Initializing engine systems...");
        
        // Print optimization flags
        std::cout << "\n=== OPTIMIZATION FLAGS STATUS ===" << std::endl;
        std::cout << "USE_DEPTH_BUFFER: " << (Optimizations::USE_DEPTH_BUFFER ? "ON" : "OFF") << std::endl;
        std::cout << "USE_SQUARED_DEPTH: " << (Optimizations::USE_SQUARED_DEPTH ? "ON" : "OFF") << std::endl;
        std::cout << "USE_SCANLINE_OPT: " << (Optimizations::USE_SCANLINE_OPT ? "ON" : "OFF") << std::endl;
        std::cout << "USE_INCREMENTAL_EDGES: " << (Optimizations::USE_INCREMENTAL_EDGES ? "ON" : "OFF") << std::endl;
        std::cout << "USE_FAST_RECT_PATH: " << (Optimizations::USE_FAST_RECT_PATH ? "ON" : "OFF") << std::endl;
        std::cout << "USE_DISTANCE_CULLING: " << (Optimizations::USE_DISTANCE_CULLING ? "ON" : "OFF") << std::endl;
        std::cout << "USE_SURFACE_CACHE: " << (Optimizations::USE_SURFACE_CACHE ? "ON" : "OFF") << std::endl;
        std::cout << "=================================\n" << std::endl;
    }

    // Initialize ResolutionManager with window and framebuffer dimensions
    if (platform_) {
        // Get actual framebuffer size from platform
        int fb_width, fb_height;
        platform_->get_framebuffer_size(fb_width, fb_height);
        
        // ResolutionManager setup moved to EngineRenderState
        // EngineRenderState now manages its own ResolutionManager with aspect ratio adjustments
    }
    
    // Initialize render system with actual framebuffer size
    // IMPORTANT: On Retina displays, framebuffer is larger than window
    // EngineRenderState needs to work with framebuffer dimensions, not window dimensions
    // Render-state initialization (was EngineRenderState::initialize). All
    // members are direct on Engine now; this block sets up the resolution
    // manager, sizes the buffers, wires the coordinate transformer, and
    // primes the camera with the chosen render resolution.
    {
        const bool is_headless = !create_display_;
        const float viewport_width_units = 80.0f;
        const float aspect_ratio = static_cast<float>(config_.window_width)
                                  / static_cast<float>(config_.window_height);
        const float viewport_height_units = viewport_width_units / aspect_ratio;

        if (is_headless) {
            // Feed the resolution manager like the windowed branch does;
            // the render size is read back from it at the bottom of this
            // block. (Direct render_width_ writes here were dead — the
            // manager's defaults overwrote them at read-back, pinning
            // every headless run to 1600x1200 regardless of config.)
            framebuffer_width_  = config_.window_width;
            framebuffer_height_ = config_.window_height;
            resolution_manager_.set_framebuffer_size(framebuffer_width_, framebuffer_height_);
            resolution_manager_.set_window_size(config_.window_width, config_.window_height);
            resolution_manager_.set_resolution_preset(ResolutionManager::ResolutionPreset::NATIVE);
            std::cout << "[ENGINE] Headless render-state: "
                      << config_.window_width << "x" << config_.window_height << std::endl;
        } else {
            int fb_w = 0, fb_h = 0;
            platform_->get_framebuffer_size(fb_w, fb_h);
            framebuffer_width_  = fb_w;
            framebuffer_height_ = fb_h;

            int win_w = 0, win_h = 0;
            platform_->get_window_size(win_w, win_h);

            std::cout << "[ENGINE] Window: " << win_w << "×" << win_h
                      << " points | Framebuffer: " << fb_w << "×" << fb_h
                      << " pixels" << std::endl;

            resolution_manager_.set_framebuffer_size(framebuffer_width_, framebuffer_height_);
            resolution_manager_.set_window_size(win_w, win_h);
            resolution_manager_.set_resolution_preset(ResolutionManager::ResolutionPreset::NATIVE);
        }

        render_width_  = resolution_manager_.get_render_width();
        render_height_ = resolution_manager_.get_render_height();
        std::cout << "[ENGINE] Quality preset: " << render_width_
                  << "×" << render_height_ << " (render)" << std::endl;

        const int window_w = resolution_manager_.get_window_width();
        const int window_h = resolution_manager_.get_window_height();
        pixels_per_world_unit_ = window_w / viewport_width_units;
        (void)viewport_height_units;  // captured for documentation; not stored

        // CoordinateTransformer setup (was inside EngineRenderState).
        coord_transformer_.set_viewport(render_width_, render_height_);
        coord_transformer_.set_pixels_per_unit(render_width_ / viewport_width_units);
        coord_transformer_.set_camera_system(&camera_system_);
        coord_transformer_.set_projection_mode(projection_mode_);
        const float dpi_scale =
            std::max(static_cast<float>(framebuffer_width_)  / window_w,
                     static_cast<float>(framebuffer_height_) / window_h);
        coord_transformer_.set_dpi_scale(dpi_scale);
        std::cout << "[ENGINE] DPI scale: " << dpi_scale
                  << " (fb " << framebuffer_width_ << "×" << framebuffer_height_
                  << " → win " << window_w << "×" << window_h << ")" << std::endl;

        // Camera viewport mirrors render resolution (NOT framebuffer
        // resolution — the projection math is in render-pixels).
        camera_system_.set_viewport(render_width_, render_height_);
        camera_system_.set_pixels_per_unit(pixels_per_world_unit_);
        camera_system_.set_projection_system(
            ProjectionFactory::create(ProjectionFactory::Type::Isometric));

        // Cinematic camera dolly. Bound here so it can drive the
        // CameraSystem during cutscenes; ticked from update() with
        // real time so it survives Engine::set_cinematic_pause(true).
        camera_director_.bind(&camera_system_);

        // Display-side buffers (framebuffer-resolution).
        framebuffer_.resize(framebuffer_width_, framebuffer_height_);
        background_buffer_.resize(framebuffer_width_, framebuffer_height_);
        // UI OVERLAY PLANE: transparent (alpha 0 = untouched) and
        // dirty-tracked, so clear/composite touch only what the HUD
        // painted rather than the whole render-resolution buffer.
        ui_buffer_.resize(render_width_, render_height_);
        ui_buffer_.set_track_dirty_bounds(true);
        ui_buffer_.clear(0, 0, 0, 0, 0);
        ui_buffer_.reset_dirty_bounds();
        background_buffer_.clear(10, 10, 15, 255, ObjectID::BACKGROUND);
        framebuffer_.clear(10, 10, 15, 255, ObjectID::BACKGROUND);

        // set_render_resolution wires the render-side buffers + object map.
        set_render_resolution(render_width_, render_height_);

        render_initialized_ = true;
    }

    // Phase 4 of Renderer/Display split: wrap the now-initialized
    // engine state behind the IRenderer + IDisplay interfaces.
    // Display is nullptr in headless mode (no presentation surface).
    renderer_ = std::make_unique<Logosphere::Rendering::MetalRenderer>(*this);
    if (create_display_) {
        display_ = std::make_unique<Logosphere::Display::MacOSDisplay>(*platform_);
    }
    draw_surface_ = std::make_unique<Logosphere::Rendering::DrawSurface>(
        render_buffer_, object_map_, font_renderer_, primitive_renderer_);
    // Widgets draw through this one: same primitives, overlay plane target.
    overlay_surface_ = std::make_unique<Logosphere::Rendering::DrawSurface>(
        ui_buffer_, object_map_, font_renderer_, primitive_renderer_);
    pixel_picker_ = std::make_unique<Logosphere::Rendering::PixelPicker>(
        render_buffer_, object_map_, resolution_manager_);

    // REMOVED: Camera viewport is set by EngineRenderState during initialize()
    // DO NOT overwrite it here - EngineRenderState sets it to RENDER size, not FRAMEBUFFER size
    // Overwriting to framebuffer size causes projection mismatch (particles project off-screen)
    //
    // Camera configuration happens in EngineRenderState::initialize():
    // - camera_system_->set_viewport(render_width_, render_height_)  // RENDER size
    // - camera_system_->set_pixels_per_unit(pixels_per_world_unit_)
    // - Projection system set to Isometric
    //
    // This was the root cause of black screen bug - camera thought viewport was 3200×2102
    // but render buffer was only 1600×1051, so all particles projected off-screen

    // Platform system already set above (before initialize)
    if (create_display_) {
        // Initialize input system (calculates DPI scale, no longer registers callbacks)
        void* native_window = platform_->get_native_window_handle();
        GLFWwindow* glfw_window = static_cast<GLFWwindow*>(native_window);
        input_system_.initialize(glfw_window);
        input_system_.set_engine(this);  // InputSystem needs Engine for KeyMapper access

        // CRITICAL: Connect InputSystem to Platform abstraction layer
        // Platform's GLFW callbacks will now route to InputSystem via IInputCallbacks interface
        // This fixes mouse motion callbacks not being invoked (hover detection bug)
        platform_->set_input_callbacks(&input_system_);
        std::cout << "[ENGINE] Connected InputSystem to Platform abstraction layer" << std::endl;

        // Initialize camera controller with required systems
        camera_controller_->initialize(&camera_system_, &input_system_, glfw_window, &particle_system_);
    } else {
        // For headless mode, still set the engine reference but skip window-specific initialization
        input_system_.set_engine(this);
    }
    
    // Always initialize KeyMapper and register handlers - needed for tests too!
    key_mapper_->initialize();
    // Connect the application so it gets first chance at handling keys
    std::cout << "[ENGINE] application_=" << (application_ ? "valid" : "NULL") << std::endl;
    if (application_) {
        key_mapper_->set_application(application_);
        std::cout << "[ENGINE] KeyMapper application set" << std::endl;
    }
    register_key_action_handlers(this);
    if (mode_ == EngineMode::Interactive || mode_ == EngineMode::Debug) {
        DEBUG_LOG("KeyMapper initialized");
    }

    // Initialize physics system with engine reference
    physics_system_.initialize(this);
    
    // Initialize light system with engine reference for SimplePixelToLight
    // Telemetry sink (LOGOSPHERE_METRICS=<path.jsonl>). Absent env means no
    // sink and no behavior change. docs/PERFORMANCE_RESEARCH.md
    logosphere::telemetry::init_from_env();

    light_system_.set_engine(this);
    light_system_.initialize();

    // Connect ParticleSystem to LightSystem
    particle_system_.set_light_system(&light_system_);

    // Initialize Particle Dynamics System (entity behaviors)
    dynamics_system_.initialize(this);
    DEBUG_LOG("Particle Dynamics System initialized");

    // Humanoid locomotion subsystem (B0 scaffold — no behavior yet,
    // but instantiated alongside dynamics so the wire-up shape is in
    // place. B1 onwards relocates HumanoidParts and the post-physics
    // humanoid loop here. See animation/humanoid_locomotion.{h,cpp}.)
    humanoid_locomotion_.initialize(this);
    serpent_locomotion_.initialize(this);
    logosphere::animation::serpent_install_engine_input_hooks(serpent_locomotion_, this);
    butterfly_flight_.initialize(this);

    // Initialize Player Controller (mouse look, WASD, abilities)
    player_controller_.initialize(this);
    DEBUG_LOG("Player Controller initialized");

    // Initialize Humanoid Integrity Monitor (opt-in; games enable and
    // register entities via engine.get_humanoid_integrity_monitor()).
    humanoid_integrity_monitor_.initialize(this);
    DEBUG_LOG("Humanoid Integrity Monitor initialized (disabled by default)");

    // Initialize Knowledge Graph (core system)
    kg_module_.setMode(kg::KGMode::MINIMAL);
    kg_module_.set_event_bus(&event_bus_);
    // Capability store rides the same bus the KG just joined: tracked
    // entities recompute on STATE_CHANGE and write capability.* back.
    capability_store_.initialize(&kg_module_, &event_bus_);
    DEBUG_LOG("Knowledge Graph initialized (MINIMAL mode)");

    // Particle interaction model: the solver consults the (game-filled)
    // profile policy at contact broad phase. Empty registry = today's
    // behavior.
    physics_system_.set_interaction_system(&interaction_system_);

    // Arm the ontology levers. From here a property written to the KG
    // (solver_authority, BONDED_TO) lands on that entity's bodies
    // straight away, rather than waiting for something to re-read it
    // at materialisation. The graph becomes a control surface.
    ground_locator_.initialize(&particle_system_);

    entity_physical_state_.initialize(&particle_system_, &physics_system_,
                                      &kg_module_);
    entity_physical_state_.watch(event_bus_);

    // Connect ParticleSystem to KG for index tracking
    particle_system_.set_kg_module(&kg_module_);
    // Connect RenderPipeline to KG for entity grouping (Entity BVH optimization)
    renderer_->set_kg_module(&kg_module_);
    // Connect ParticleSystem to PhysicsSystem for gluon cleanup on particle deletion
    particle_system_.set_physics_system(&physics_system_);
    particle_system_.set_dynamics_system(&dynamics_system_);
    
    if (mode_ == EngineMode::Interactive || mode_ == EngineMode::Debug) {
        if (config_.debug_light_casting || config_.debug_light_tracing || 
            config_.debug_light_intersections || config_.debug_light_accumulation ||
            config_.debug_origin_lights || config_.debug_light_verbose) {
            DEBUG_LOG("Light system debug modes configured");
        }
    }
    
    // Camera is auto-configured for the projection type (e.g., isometric gets SW above origin)
    // Applications can override camera position if they need custom views
    // Each projection type defines its own natural viewing position

    // Initialize UI system (after EngineRenderState)
    ui_system_ = new UISystem();
    
    UISystem::Config ui_config;
    ui_config.screen_width = config_.window_width;
    ui_config.screen_height = config_.window_height;
    if (platform_) {
        ui_config.dpi_scale = platform_->get_dpi_scale_x();
    }
    ui_config.enable_debug_overlays = config_.show_debug_overlay;
    ui_config.enable_tooltips = true;
    ui_config.enable_chat_window = config_.enable_chat_window;
    
    if (!ui_system_->initialize(this, ui_config)) {
        DEBUG_LOG("Failed to initialize UI system");
        return false;
    }
    // Hand UISystem the engine-owned IDrawSurface. Widgets receive
    // this (not render_system_) during their render() pass.
    ui_system_->set_draw_surface(overlay_surface_.get());
    ui_system_->set_pixel_picker(pixel_picker_.get());
    // set_render_resolution ran before the UISystem existed; feed the
    // current HUD content scale (render px per logical window point).
    if (const int win_w = resolution_manager_.get_window_width(); win_w > 0) {
        ui_system_->set_content_scale(static_cast<float>(render_width_) / win_w);
    }
    
    // Initialize debug overlay
    debug_overlay_.set_enabled(config_.show_debug_overlay);
    debug_overlay_.set_show_metrics(true);
    debug_overlay_.set_show_grid(false);  // Grid can be toggled separately
    debug_overlay_.set_show_compass(false);  // Compass can be toggled separately
    
    // Initialize scene manager
    scene_manager_.set_particle_system(&particle_system_);
    
    // All systems are now direct members or pointers in Engine
    // No need for a separate coordinator - Engine IS the coordinator

    // Other systems don't have explicit initialize methods yet
    // They use their constructors for initialization

    // Set up initial game state
    // In visual test mode, show the test menu and start with empty scene
    if (mode_ == EngineMode::Debug && config_.show_debug_overlay) {
        // Debug mode enabled
        // Start with empty scene - user will select test
        // Clear all particles except player
        while (particle_system_.count() > 1) {
            particle_system_.remove_particle(particle_system_.count() - 1);
        }

        // Don't add default light - let test scenarios control their own lighting
        // This was causing interference with test scenarios
        /*
        Particle default_light;
        default_light.x = 0.0f;
        default_light.y = 0.0f;
        default_light.z = 5.0f;
        default_light.size = 0.5f;
        default_light.is_light_source = true;
        default_light.emission_strength = 2.0f;
        default_light.r = 1.0f;
        default_light.g = 1.0f;
        default_light.b = 1.0f;
        particle_system_.add_particle(default_light);
        */

        DEBUG_LOG("\nVisual Test Mode: Press keys to select tests");
        DEBUG_LOG("Press 'H' to toggle help menu");
        DEBUG_LOG("Press F1-F4 to select category, then 1-9 for test scenario");
        DEBUG_LOG("Initial particles: " << particle_system_.count());
        
        // Enable debug overlay in debug mode
        debug_overlay_.set_enabled(true);
        debug_overlay_.set_show_metrics(true);
        
        // Start with empty scene in debug mode
        scene_manager_.load_empty_scene();
    } else {
        // Normal mode - create initial scene
        scene_manager_.create_initial_scene();

        // Core engine systems: EntityManager (registers its own entity types)
        // TODO: Port tests to use EntityManager instead of bypassing with direct particle creation
        entity_manager_ = new EntityManager(&kg_module_, particle_system_, &physics_system_, &event_bus_);
        DEBUG_LOG("EntityManager initialized with PhysicsSystem for gluon creation");

        // Core engine systems: WorldGenSystem (depends on EntityManager)
        worldgen_system_.initialize(this, &kg_module_, &event_bus_);
        DEBUG_LOG("WorldGenSystem initialized");

        // Core engine systems: CelestialSystem (sun, moon, stars)
        celestial_system_.initialize(&particle_system_, &kg_module_);
        DEBUG_LOG("CelestialSystem initialized with KG for Entity BVH");

        // Core engine systems: EntitySystem (always available, not application-specific)
        entity_system_ = new EntitySystem(particle_system_, &kg_module_, &dynamics_system_);
        DEBUG_LOG("EntitySystem initialized with KG and dynamics integration");

        // Let the application initialize its game-specific content
        if (application_) {
            application_->initialize_game(this);  // Pass engine pointer for now

            // Let the application create its initial scene
            // Pass a lambda that captures the particle system for adding particles
            // The lambda returns the particle ID that was added
            unsigned int input_entity = application_->create_initial_scene(
                [this](const Particle& p) -> int {
                    std::cout << "[ADD_PARTICLE_CALLBACK] Adding particle at (" << p.x << "," << p.y << "," << p.z
                              << ") is_light=" << p.is_light_source << std::endl;
                    particle_system_.add_particle(p);
                    int id = particle_system_.count() - 1;
                    std::cout << "[ADD_PARTICLE_CALLBACK] Particle added with ID=" << id << std::endl;
                    return id;  // Return the ID of the just-added particle
                }
            );

            // PHASE 2.3: Reload constraints after entities are created
            // Problem: PhysicsSystem::initialize() loads constraints BEFORE entities exist
            // Solution: Reload constraints after create_initial_scene() completes
            std::cout << "[ENGINE] Scene created, reloading particle constraints..." << std::endl;
            physics_system_.load_constraints_from_kg();
            std::cout << "[ENGINE] Particle constraints reloaded" << std::endl;
            
            // Set the input target entity for this game
            if (input_entity != kg::INVALID_ENTITY) {
                set_input_target_entity(static_cast<kg::EntityID>(input_entity));
                std::cout << "[ENGINE] Input target set to entity " << input_entity << std::endl;

                // Verify particles are bound
                auto bound_particles = kg_module_.getEntityKGParticles(static_cast<kg::EntityID>(input_entity));
                std::cout << "[ENGINE] Input entity has " << bound_particles.size() << " KG particles directly bound" << std::endl;
            } else {
                std::cout << "[ENGINE] WARNING: No input entity specified by game!" << std::endl;
            }
        }
    }

    // No test execution in Engine - tests use Engine as a library
    // No primordial observer particle - applications create their own particles
    
    // CRITICAL: After all particles are created, do initial BVH build and lighting update
    // This ensures shadows work on the FIRST frame, not just after movement
    std::cout << "[ENGINE] Performing initial BVH and lighting setup..." << std::endl;
    std::cout << "[ENGINE DEBUG] Particle count before BVH: " << particle_system_.count() << std::endl;
    particle_system_.mark_bvh_dirty();
    particle_system_.update_bvh();
    
    // CRITICAL: Force a second BVH update to ensure frame counter is properly propagated
    // This is needed because worker threads cache the BVH and need the frame counter
    // to know when to refresh their cache. The first update builds the BVH, the second
    // propagates the frame counter signal.
    std::cout << "[ENGINE] Forcing second BVH update for frame counter sync" << std::endl;
    particle_system_.mark_bvh_dirty();
    particle_system_.update_bvh();
    
    light_system_.update_lighting(particle_system_);
    std::cout << "[ENGINE] Initial lighting setup complete." << std::endl;

    // NO default sun - Engine is a library, applications create their own lights
    // Games (like Eden) should call create_sun() if they want one

    is_initialized_ = true;
    
    // For interactive mode, set running flag so custom loops work
    if (mode_ == EngineMode::Interactive) {
        is_running_ = true;
    }
    
    DEBUG_LOG("Engine initialization complete!");
    return 0;  // Return success code
}

bool Engine::should_continue() const {
    return is_running_ && (!platform_ || !platform_->should_close());
}

// Engine is a pure library - applications control the main loop.
// No run() method. Apps call update(dt) and render() directly.

void Engine::set_input_target_entity(kg::EntityID entity_id) {
    input_target_entity_id_ = entity_id;
}

void Engine::set_camera_deadzone(float size) {
    camera_system_.set_camera_deadzone(size);
}

void Engine::set_camera_follow_offset(float offset_y) {
    camera_system_.set_camera_follow_offset(offset_y);
}

// Vision cone + memory + read-back: all forward to the IRenderer
// (Phase 4). The Engine-side surface is kept for game back-compat;
// the body is a one-line pass-through.
void Engine::set_vision_cone_enabled(bool enabled) {
    if (renderer_) renderer_->set_vision_cone_enabled(enabled);
}

bool Engine::get_vision_cone_enabled() const {
    return renderer_ ? renderer_->get_vision_cone_enabled() : false;
}

void Engine::set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                              float fov_radians, float range) {
    if (renderer_) {
        renderer_->set_vision_cone(viewer_x, viewer_y, look_direction,
                                   fov_radians, range);
    }
}

void Engine::set_vision_cone_style(float inner_falloff, float darkness) {
    if (renderer_) renderer_->set_vision_cone_style(inner_falloff, darkness);
}

void Engine::set_vision_cone_focus(float focus_x, float focus_y, float focus_radius) {
    if (renderer_) {
        renderer_->set_vision_cone_focus(focus_x, focus_y, focus_radius);
    }
}

void Engine::set_vision_cone_occlusion(const float* distances, int count) {
    if (renderer_) renderer_->set_vision_cone_occlusion(distances, count);
}

void Engine::clear_vision_cone_occlusion() {
    if (renderer_) renderer_->clear_vision_cone_occlusion();
}

bool Engine::read_latest_framebuffer(uint32_t* out_pixels,
                                     int& out_width, int& out_height) {
    if (!renderer_) return false;
    if (!renderer_->read_framebuffer(out_pixels, out_width, out_height)) return false;
    if (!ui_system_ || !out_pixels) return true;

    // COMPOSITE THE UI OVERLAY PLANE ONTO THE READ-BACK.
    //
    // What renderer_->read_framebuffer returns is the GPU's scene buffer
    // (framebuffer_buffer_async_), and it is scene-only BY CONSTRUCTION:
    // pixels flow GPU -> render_buffer_ through the completion-callback memcpy
    // and never the other way. So the HUD that composite_ui_overlay() blends
    // into render_buffer_ could never appear here, and every framebuffer-reading
    // test saw a UI-free frame. That is why four visual tests shipped with
    // readouts nobody could see, and why docs/VISUAL_TESTS.md recommended a
    // widget route that could not work. Proven by
    // tests/test_ui_label_actually_renders.cpp: a bright red Label moved ZERO
    // pixels by >=32 before this.
    //
    // Compositing HERE, from ui_buffer_ directly, also steps around a second
    // and independent bug rather than depending on it being fixed: headlessly
    // the overlay is blended into render_buffer_ while that buffer still holds
    // frame N-1, and frame N's completion callback then memcpys straight over
    // it, so wait_for_completion() guarantees the HUD is erased. ui_buffer_
    // itself is never touched by that callback, so reading it at the last
    // moment is correct and race-free.
    const int w = std::min(out_width,  ui_buffer_.width());
    const int h = std::min(out_height, ui_buffer_.height());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const EnhancedPixel ui = ui_buffer_.get_pixel(x, y);
            if (ui.a == 0) continue;                       // untouched
            uint32_t& dst = out_pixels[(size_t)y * out_width + x];
            if (ui.a == 255) {
                dst = (uint32_t(255) << 24) | (uint32_t(ui.r) << 16) |
                      (uint32_t(ui.g) << 8) | uint32_t(ui.b);
            } else {
                const uint8_t sr = uint8_t(dst >> 16);
                const uint8_t sg = uint8_t(dst >> 8);
                const uint8_t sb = uint8_t(dst);
                const float a = ui.a / 255.0f;
                const uint8_t r = uint8_t(ui.r * a + sr * (1.0f - a));
                const uint8_t g = uint8_t(ui.g * a + sg * (1.0f - a));
                const uint8_t b = uint8_t(ui.b * a + sb * (1.0f - a));
                dst = (uint32_t(255) << 24) | (uint32_t(r) << 16) |
                      (uint32_t(g) << 8) | uint32_t(b);
            }
        }
    }
    return true;
}

// Render-state mutators that touch multiple sub-systems live here
// (was EngineRenderState::{clear_framebuffer, copy_buffer,
// set_render_resolution, set_projection_mode, cycle_projection_mode,
// get_current_projection}).

void Engine::clear_framebuffer(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    depth_buffer_.clear();
    object_map_.clear();
    render_buffer_.clear(r, g, b, a, ObjectID::BACKGROUND);
    framebuffer_.clear(r, g, b, a, ObjectID::BACKGROUND);
}

void Engine::copy_buffer(const PixelBuffer& source, PixelBuffer& dest) {
    dest.copy_from(source);
}

void Engine::set_render_resolution(int width, int height) {
    render_width_  = width;
    render_height_ = height;

    if constexpr (!Optimizations::USE_GPU_RASTERIZATION) {
        surface_rasterizer_.init_frame_buffers(render_width_, render_height_);
    }

    render_buffer_.resize(render_width_, render_height_);
    depth_buffer_.resize(render_width_, render_height_);
    // Overlay plane follows the render resolution: composite reads it
    // pixel-for-pixel against the scene buffer.
    ui_buffer_.resize(render_width_, render_height_);
    ui_buffer_.set_track_dirty_bounds(true);
    ui_buffer_.clear(0, 0, 0, 0, 0);
    ui_buffer_.reset_dirty_bounds();

    // SparseObjectMap stores dimensions internally; reconstruct on resize.
    object_map_ = SparseObjectMap(render_width_, render_height_);

    camera_system_.set_viewport(render_width_, render_height_);

    // Clear after resize to avoid uninitialized content (pink-screen bug).
    render_buffer_.clear(10, 10, 15, 255, ObjectID::BACKGROUND);

    // HUD content scale: render px per logical window point. Keeps UI
    // text/rects on-screen size constant across render resolutions
    // (retina-native was shrinking the HUD to half size).
    if (ui_system_) {
        const int window_w = resolution_manager_.get_window_width();
        if (window_w > 0) {
            ui_system_->set_content_scale(
                static_cast<float>(render_width_) / window_w);
        }
    }
}

void Engine::set_projection_mode(ProjectionMode mode) {
    projection_mode_ = mode;
    coord_transformer_.set_projection_mode(mode);
    switch (mode) {
        case ProjectionMode::Isometric:
            camera_system_.set_projection_system(
                ProjectionFactory::create(ProjectionFactory::Type::Isometric));
            break;
        case ProjectionMode::IsometricWithDepth:
            camera_system_.set_projection_system(
                ProjectionFactory::create(ProjectionFactory::Type::IsometricDepth));
            break;
        case ProjectionMode::Perspective:
            camera_system_.set_projection_system(
                ProjectionFactory::create(ProjectionFactory::Type::Perspective));
            break;
        case ProjectionMode::Cabinet:
            camera_system_.set_projection_system(
                ProjectionFactory::create(ProjectionFactory::Type::Cabinet));
            break;
        case ProjectionMode::BirdsEye:
            camera_system_.set_projection_system(
                ProjectionFactory::create(ProjectionFactory::Type::BirdsEye));
            break;
    }
}

void Engine::cycle_projection_mode() {
    switch (projection_mode_) {
        case ProjectionMode::Isometric:
            set_projection_mode(ProjectionMode::IsometricWithDepth);
            std::cout << "Switched to: Isometric with Depth" << std::endl;
            break;
        case ProjectionMode::IsometricWithDepth:
            set_projection_mode(ProjectionMode::Perspective);
            std::cout << "Switched to: Perspective" << std::endl;
            break;
        case ProjectionMode::Perspective:
            set_projection_mode(ProjectionMode::Cabinet);
            std::cout << "Switched to: Cabinet" << std::endl;
            break;
        case ProjectionMode::Cabinet:
            set_projection_mode(ProjectionMode::BirdsEye);
            std::cout << "Switched to: Bird's Eye" << std::endl;
            break;
        case ProjectionMode::BirdsEye:
            set_projection_mode(ProjectionMode::Isometric);
            std::cout << "Switched to: Isometric" << std::endl;
            break;
    }
}

ProjectionSystem* Engine::get_current_projection() const {
    switch (projection_mode_) {
        case ProjectionMode::Isometric:
            return const_cast<IsometricProjection*>(&isometric_projection_);
        case ProjectionMode::IsometricWithDepth:
            return const_cast<IsometricDepthProjection*>(&isometric_depth_projection_);
        case ProjectionMode::Perspective:
            return const_cast<PerspectiveProjection*>(&perspective_projection_);
        case ProjectionMode::Cabinet:
            return const_cast<CabinetProjection*>(&cabinet_projection_);
        case ProjectionMode::BirdsEye:
            return const_cast<BirdsEyeProjection*>(&birds_eye_projection_);
    }
    return const_cast<IsometricProjection*>(&isometric_projection_);
}

void Engine::set_vision_memory_enabled(bool enabled) {
    if (renderer_) renderer_->set_vision_memory_enabled(enabled);
}

void Engine::set_vision_memory_extent(float min_x, float min_y,
                                      float max_x, float max_y,
                                      int cells_per_side) {
    if (renderer_) {
        renderer_->set_vision_memory_extent(min_x, min_y, max_x, max_y,
                                            cells_per_side);
    }
}

void Engine::set_vision_memory_decay(float decay_seconds, float memory_dim) {
    if (renderer_) renderer_->set_vision_memory_decay(decay_seconds, memory_dim);
}

void Engine::update_vision_memory(float dt) {
    if (renderer_) renderer_->update_vision_memory(dt);
}

// Cinematic pause — engine-driven freeze for cutscenes / wow-moments.
// Wraps TimeSystem so any system that ticks on game_delta_time
// (physics, AI, particle dynamics) freezes; renderer + cinematic
// effects keep going on real_delta_time. The flag is exposed so
// game-side input handlers can suppress game-affecting actions
// without touching engine-internal pause state.
void Engine::set_cinematic_pause(bool paused) {
    if (paused == cinematic_paused_) return;
    cinematic_paused_ = paused;
    if (paused) {
        time_system_.pause();
    } else {
        time_system_.resume();
    }
}

// Physics enable/disable with automatic settling on re-enable
void Engine::set_physics_enabled(bool enabled) {
    // Detect false→true transition: need to settle constraints
    if (enabled && !physics_enabled_) {
        size_t particle_count = particle_system_.count();
        std::cout << "[Engine] Physics re-enabled, running settling frames..." << std::endl;
        std::cout << "[Engine] Settling stats: " << particle_count << " particles" << std::endl;

        // DEBUG: Count at-rest particles BEFORE settling
        {
            auto particles = particle_system_.lock_particles_for_read();
            size_t at_rest_count = 0;
            size_t floor_tile_count = 0;  // Particles with thickness < 0.3 and z < 0.3 are likely floor tiles
            size_t floor_at_rest = 0;
            size_t non_floor_at_rest = 0;
            for (const auto& p : particles) {
                if (p.is_at_rest) at_rest_count++;
                bool is_floor = (p.thickness < 0.3f && p.z < 0.3f);
                if (is_floor) {
                    floor_tile_count++;
                    if (p.is_at_rest) floor_at_rest++;
                } else {
                    if (p.is_at_rest) non_floor_at_rest++;
                }
            }
            std::cout << "[Engine] PRE-SETTLE: at_rest=" << at_rest_count << "/" << particle_count
                      << " (floor=" << floor_at_rest << "/" << floor_tile_count
                      << " non-floor=" << non_floor_at_rest << "/" << (particle_count - floor_tile_count) << ")" << std::endl;
        }

        // Run up to 60 physics frames (1 second at 60Hz) to settle gluon constraints
        // Early exit if max velocity drops below threshold
        const double settle_dt = 1.0 / 60.0;
        const int max_settle_frames = 60;
        const float settle_velocity_threshold = 0.5f;  // m/s - exit early if below this

        float max_v_start = 0.0f;
        float max_v_end = 0.0f;
        int actual_frames = 0;

        for (int i = 0; i < max_settle_frames; i++) {
            particle_system_.update_bvh();
            physics_system_.update(settle_dt);
            actual_frames++;

            // Check velocity every 10 frames for early exit
            if (i % 10 == 9 || i == 0) {
                auto particles = particle_system_.lock_particles_for_read();
                float frame_max = 0.0f;
                for (const auto& p : particles) {
                    float v = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    frame_max = std::max(frame_max, v);
                }
                if (i == 0) max_v_start = frame_max;
                max_v_end = frame_max;

                // Early exit if settled
                if (i > 0 && frame_max < settle_velocity_threshold) {
                    std::cout << "[Engine] Settling early exit at frame " << i
                              << " (max_v=" << frame_max << " < " << settle_velocity_threshold << ")" << std::endl;
                    break;
                }
            }
        }

        std::cout << "[Engine] Settling complete in " << actual_frames << " frames: max_v "
                  << max_v_start << " → " << max_v_end << " m/s" << std::endl;
    }

    physics_enabled_ = enabled;
}

// Chunk pre-loading
void Engine::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    worldgen_system_.preload_chunks_around(world_x, world_y, radius_chunks);
}

void Engine::shutdown() {
    if (!is_initialized_) return;

    DEBUG_LOG("Shutting down engine...");

    is_running_ = false;

    // CRITICAL: Wait for GPU to finish all pending work (async rasterization)
    // Must do this BEFORE destroying window/platform or Metal resources will be invalid
    if (renderer_) {
        renderer_->wait_for_completion();
    }

    // Clean up UI system
    if (ui_system_) {
        ui_system_->shutdown();
        delete ui_system_;
        ui_system_ = nullptr;
    }

    // Clean up platform system
    if (platform_) {
        platform_->destroy_window();
        delete platform_;
        platform_ = nullptr;
    }

    is_initialized_ = false;
    DEBUG_LOG("Engine shutdown complete.");
}

void Engine::update(double delta_time) {
    // FORENSICS: Log dt value passed to Engine::update() (detect if test is passing wrong value)
    static int update_call_count = 0;
    if constexpr (PhysicsSolver::ENABLE_FORENSIC_LOGGING && PhysicsSolver::LOG_ENGINE_UPDATE_DT) {
        std::cout << "[ENGINE_UPDATE_DT call #" << update_call_count << "] "
                  << "delta_time parameter = " << std::fixed << std::setprecision(10)
                  << delta_time << "s" << std::endl;
    }
    update_call_count++;

    // DEBUG: Check gluon at START of engine update
    if (update_call_count >= 60 && update_call_count <= 65) {
        const auto* g = physics_system_.get_gluon(21, 25);
        if (g) {
            std::cout << "[ENG_START] frame=" << update_call_count
                      << " gluon offset_b.z=" << g->offset_b.z << std::endl;
        }
    }

    // Advance the event journal stamp clock (retention is
    // capacity-based; this only timestamps subsequent emits).
    event_bus_.advance_frame(time_system_.get_total_game_time());

    // Humanoid Integrity Monitor (opt-in). update() is a cheap no-op when
    // disabled; when enabled, scans registered humanoids for pair-separation,
    // hips-drop, and bounding-box-spread violations.
    humanoid_integrity_monitor_.update(static_cast<int>(update_call_count));

    // Deep Probes (opt-in watchpoints). No-op when no probes registered.
    // Games register probes via engine.get_deep_probe_manager().register_probe().
    // If a probe fires with halt_on_trigger, is_running_ is cleared so the
    // main loop exits after this frame.
    deep_probe_manager_.update(*this, static_cast<int>(update_call_count));
    if (deep_probe_manager_.halt_requested()) {
        is_running_ = false;
    }

    // Advance frame counter for triple buffering (particle deletion safety)
    // Must happen at start of frame so both update() and render() see same frame number
    renderer_->advance_frame_counter();

    // Headless deletion flush: if this session has never rendered, no GPU
    // or worker thread can hold particle references, and render()'s flush
    // will never run — drain ready deletions here or they leak forever
    // (contract a5 in tests/test_position_authority.cpp).
    if (!has_rendered_) {
        int headless_frame = renderer_->current_frame_number();
        if (particle_system_.has_ready_deletions(headless_frame)) {
            particle_system_.flush_safe_deletions(headless_frame);
        }
    }

    // Spike detection timing (frame-level, following PERFORMANCE_RESEARCH.md)
    auto frame_start = std::chrono::high_resolution_clock::now();
    auto update_start = frame_start;

    // Frame boundary. Finalizing the PREVIOUS frame here rather than in
    // present() is deliberate: headless runs never call present(), and a
    // measurement kit that only records in windowed mode is useless.
    if (telemetry_frame_open_) {
        logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Frame);
        publish_metrics();
        logosphere::telemetry::frame_end();
    }
    logosphere::telemetry::frame_begin(logosphere::telemetry::frame_index() + 1);
    // Physics trace shares the frame clock so a record can be lined up against
    // a telemetry row. init_from_env is idempotent and cheap; doing it here
    // means the trace works in any host (engine, test, example) without each
    // one remembering to wire it up.
    static bool phystrace_inited = false;
    if (!phystrace_inited) { logosphere::phystrace::init_from_env(); phystrace_inited = true; }
    logosphere::phystrace::frame_begin(logosphere::telemetry::frame_index());
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Frame);
    telemetry_frame_open_ = true;
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Update);

    // FIRST: Update time system (single source of truth for all time)
    // This tracks real time and produces scaled game time
    time_system_.tick(delta_time);
    double game_delta = time_system_.get_game_delta_time();

    // Cinematic camera dolly. Drives on REAL delta time so dollies
    // keep playing during cinematic pause (game_delta == 0). Cheap
    // when idle: early-returns inside CameraDirector::update.
    camera_director_.update(static_cast<float>(time_system_.get_real_delta_time()));

    // KGOp playback registry (Phase D). Same real-time contract
    // as the dolly so rez-in plays survive cinematic pause. Cheap
    // when nothing is active.
    mutation_playback_registry_.update(
        static_cast<float>(time_system_.get_real_delta_time()));

    // Advance game world time (calendar, day/night cycle, etc.)
    // GameTime is the simulation clock - used by CelestialSystem for sun/moon
    GameTime::advance(game_delta);

    // Update celestial bodies (sun, moon, stars) based on game time
    celestial_system_.update(GameTime::get_current_time());

    // Poll platform events (window, input, etc.)
    // This is required for the window to appear and respond
    // Platform ALWAYS exists - even headless has a platform (which no-ops poll_events)
    auto poll_start = std::chrono::high_resolution_clock::now();
    platform_->poll_events();
    auto poll_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - poll_start).count();

    // Handle continuous movement first (uses game time, not real time)
    auto movement_start = std::chrono::high_resolution_clock::now();
    handle_continuous_movement(this, game_delta);
    auto movement_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - movement_start).count();

    // Call application update (for game-specific logic like direct particle control)
    if (application_) {
        application_->update_game(static_cast<float>(game_delta));
    }

    // Reset per-frame metrics at the start of each frame
    camera_system_.reset_frame_metrics();

    // EDUCATIONAL NOTE: System Update Order
    //
    // The order of system updates matters:
    // 1. Input - process user commands first
    // 2. Physics/Movement - update positions based on input
    // 3. Vision - calculate what character can see
    // 4. AI/Logic - make decisions based on visible information
    // 5. Lighting - calculate illumination for rendering

    // Update Input System (uses game time for movement)
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Input);

    // When chat has focus, skip ALL game input processing
    // Keyboard goes to chat only, mouse still works for UI interaction
    if (!ui_system_->has_exclusive_input_focus()) {
        input_system_.process_non_movement_input(game_delta, *this, physics_system_);
        input_system_.update_hover_detection();  // Frame-rate decoupled hover detection (60-120 Hz, not 1000+ Hz)
        camera_controller_->update(game_delta);
    }

    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Input);

    // TEMP TIMING: Find the bottleneck
    static int timing_frame = 0;
    timing_frame++;
    auto t0 = std::chrono::high_resolution_clock::now();

    // Update Player Controller FIRST (sets look-at target and velocity)
    // PlayerController reads input and sets targets for dynamics system
    player_controller_.update(static_cast<float>(game_delta));

    // Update Particle Dynamics (animations apply desired offsets)
    // Then physics can correct/constrain the movements via gluons
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Dynamics);
    dynamics_system_.update(game_delta);
    humanoid_locomotion_.update_pre_physics(game_delta);
    serpent_locomotion_.update(game_delta, get_input_target_entity());
    butterfly_flight_.update(game_delta);
    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Dynamics);

    auto t1 = std::chrono::high_resolution_clock::now();

    // CRITICAL: Update BVH BEFORE physics runs
    // Physics needs BVH for collision detection - must always be ready
    // Dynamics system marks BVH dirty after particle movements
    particle_system_.update_bvh();

    auto t2 = std::chrono::high_resolution_clock::now();

    // Update Physics System (gluon constraints correct animation movements)
    // Skip during loading to prevent BVH stack overflow from rapid particle addition
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Physics);

    if (physics_enabled_) {
        if constexpr (USE_FIXED_TIMESTEP) {
            // STAGE 3: Fixed timestep physics (60 Hz, FPS-independent)
            // Physics ticks at constant rate regardless of rendering framerate
            // Lambda captures 'this' to access physics_system_
            time_system_.tick_fixed_physics([this](double fixed_dt) {
                physics_system_.update(fixed_dt);  // Always 0.0167s (60 Hz)
            });
        } else {
            // OLD: Variable timestep (FPS-dependent, causes tunneling)
            physics_system_.update(game_delta);
        }
    }

    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Physics);

    // Interaction model: consume this frame's filtered broad-phase
    // overlaps — medium forces first (velocity increments the next
    // physics frame integrates; positions never touched), then episode
    // events (contact-filtered + volume enter/exit).
    {
        Vec3 g = physics_system_.get_solver_gravity();
        auto view = particle_system_.lock_particles_for_write();
        interaction_system_.apply_volume_forces(
            view, physics_system_.get_filtered_overlaps(),
            g.x, g.y, g.z, static_cast<float>(game_delta));
    }
    interaction_system_.process_filtered_overlaps(
        physics_system_.get_filtered_overlaps(), &event_bus_);
    // The contact producer (issue #36). Until this existed the
    // ontological CollisionEvent was a class with no instances: declared
    // in the schema, given a bus channel, emitted by nothing outside
    // tests. Physics computes the geometry every frame and it died at
    // the layer boundary.
    //
    // The translation is here because the interaction system is
    // headless-core safe and may not include physics, the same split
    // FilteredOverlap already uses. wants_contacts() gates the copy as
    // well as the resolution: a tiled floor generates contacts by the
    // thousand and none of it is built when nobody listens.
    if (interaction_system_.wants_contacts(&event_bus_)) {
        const auto& raw = physics_system_.get_collision_events();
        std::vector<logosphere::interaction::RigidContact> contacts;
        contacts.reserve(raw.size());
        for (const auto& e : raw) {
            contacts.push_back({
                static_cast<uint32_t>(e.particle_a),
                static_cast<uint32_t>(e.particle_b),
                e.normal_x, e.normal_y, e.normal_z,
                e.contact_x, e.contact_y, e.contact_z,
                e.penetration, e.relative_velocity});
        }
        interaction_system_.process_contacts(contacts, kg_module_, &event_bus_);
    }
    // Declarative transformations: rules fire off this frame's episode
    // opens + armed timers. The tick reports deletions; the engine owns
    // queueing them (deferred, triple-buffer safe) — the interaction
    // system never touches render state.
    {
        std::vector<uint32_t> transform_deletes;
        auto view = particle_system_.lock_particles_for_write();
        interaction_system_.tick_transformations(
            view, kg_module_, &event_bus_,
            static_cast<float>(game_delta), transform_deletes);
        for (uint32_t idx : transform_deletes) {
            particle_system_.queue_particle_deletion(
                idx, renderer_->current_frame_number());
        }
    }

    // Apply kinematic animations AFTER physics to prevent gluon solver from undoing them
    dynamics_system_.update_post_physics(game_delta);
    humanoid_locomotion_.update_post_physics(game_delta);

    // DEBUG: Check gluon values after dynamics
    static int eng_dyn_frame = 0;
    eng_dyn_frame++;
    if (eng_dyn_frame >= 58 && eng_dyn_frame <= 65) {
        const auto* g = physics_system_.get_gluon(21, 25);
        if (g) {
            std::cout << "[ENG_AFTER_DYN] frame=" << eng_dyn_frame
                      << " ptr=" << (void*)g
                      << " offset_a=(" << g->offset_a.x << "," << g->offset_a.y << "," << g->offset_a.z << ")"
                      << " offset_b=(" << g->offset_b.x << "," << g->offset_b.y << "," << g->offset_b.z << ")"
                      << std::endl;
        }
    }

    // Call application post-physics update (for collision detection that needs animated positions)
    // Combat system needs this to detect punch hits at animated hand positions
    if (application_) {
        application_->update_game_post_physics(static_cast<float>(game_delta));
    }

    auto t3 = std::chrono::high_resolution_clock::now();

    // TEMP: Log timing every 30 frames
    if (timing_frame % 30 == 0) {
        auto dyn_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        auto bvh_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        auto phys_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::cout << "[TIMING] Frame " << timing_frame << " | Dynamics=" << dyn_ms
                  << "ms BVH=" << bvh_ms << "ms Physics=" << phys_ms << "ms" << std::endl;
    }

    // Update Camera Follow (track input target entity with deadzone)
    // Camera follow runs even during zoom - zoom only changes magnification, not position
    if (camera_follow_enabled_ && input_target_entity_id_ != kg::INVALID_ENTITY) {
        // Ask KG which particle to follow (stored in "camera_follow_particle" property)
        std::string follow_str = kg_module_.getProperty(input_target_entity_id_, "camera_follow_particle");
        if (!follow_str.empty()) {
            unsigned int follow_particle_id = static_cast<unsigned int>(std::stoul(follow_str));
            auto particle = particle_system_.get_particle_copy(follow_particle_id);
            camera_system_.update_follow_target(particle.x, particle.y, particle.z);
            // DEBUG: Print once per second
            static int cam_debug_counter = 0;
            if (cam_debug_counter++ % 60 == 0) {
                std::cout << "[CAM_FOLLOW] entity=" << input_target_entity_id_ << " particle=" << follow_particle_id
                          << " pos=(" << particle.x << "," << particle.y << "," << particle.z << ")" << std::endl;
            }
        }
    }

    // Update World Generation (creates/destroys chunks based on camera position)
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Worldgen);
    float camera_x, camera_y, camera_z;
    camera_system_.get_position(camera_x, camera_y, camera_z);

    // Update camera velocity for chunk pre-loading prediction (Phase 3)
    camera_system_.update_velocity(static_cast<float>(game_delta));
    float vel_x, vel_y;
    camera_system_.get_velocity(vel_x, vel_y);

    // Pass velocity to worldgen for predictive chunk loading
    worldgen_system_.update(camera_x, camera_y, vel_x, vel_y);
    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Worldgen);

    // Update UI System (uses game time for animations)
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::UI);
    ui_system_->update(game_delta);

    // Update UI input state for hover detection (only in non-headless mode)
    if (create_display_ && platform_) {
        double mouse_x, mouse_y;
        glfwGetCursorPos(static_cast<GLFWwindow*>(platform_->get_native_window_handle()), &mouse_x, &mouse_y);
        const InputState& input = input_system_.get_input_state();
        ui_system_->update_input_state(mouse_x, mouse_y, input.mouse_buttons[0], input.mouse_buttons[1]);
    }

    ui_system_->update_hover_detection(this);  // Throttled hover detection (10Hz)
    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::UI);

    // Poll LLM system for completed responses (async callbacks)
    // This invokes game callbacks when LLM generation completes
    // Overhead: ~0.1ms per frame when empty queue, zero blocking
#ifdef HAS_LLAMA
    if (llm_system_ && llm_system_->is_initialized()) {
        auto llm_start = std::chrono::high_resolution_clock::now();
        llm_system_->process_completed_responses();
        auto llm_end = std::chrono::high_resolution_clock::now();
        auto llm_ms = std::chrono::duration<double, std::milli>(llm_end - llm_start).count();
        if (llm_ms > 1.0) {
            std::cout << "[STALL-LLM-POLL] " << llm_ms << "ms" << std::endl;
        }
    }
#endif

    // REMOVED: Redundant particle flush, BVH update, and lighting update
    // These are now ONLY done in render() where they're actually needed
    // This eliminates multi-second hang with 500+ particles

    // The legacy universal particle integrator (ParticleSystem::update)
    // was deleted 2026-07-09: it advanced every particle's x/y by v·dt a
    // second time each frame. Position authority is per-owner now; see
    // tests/test_position_authority.cpp.

    // EDUCATIONAL NOTE: Single Source of Truth
    //
    // ParticleSystem is the ONLY source of truth for particles.
    // All systems must go through ParticleSystem to access/modify particles.
    // This eliminates synchronization bugs and duplicate state.
    //
    // Key principle: One authoritative data location with defined interfaces.


    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Update);

    // DETAILED UPDATE PROFILING: Log every 60 frames to track ALL update time
    auto update_duration = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - update_start).count();

    if constexpr (Optimizations::ENABLE_PROFILING) {
        // Store for consolidated frame summary in present()
        g_last_update_time = update_duration;
        g_last_poll_time = poll_ms;
        g_last_movement_time = movement_ms;
    }
}

void Engine::render() {
    // From here on, the render path owns deferred-deletion flushing
    // (behind its GPU/worker completion waits) — see update()'s headless
    // flush.
    has_rendered_ = true;

    // 🔧 GUARD: Increment frame counter for resolution stabilization
    // This ensures GPU triple-buffers fully cycle before allowing another resolution change
    if (frames_since_resolution_change_ < 999) {  // Cap at 999 to avoid overflow
        frames_since_resolution_change_++;
    }

    // TODO[ARCH-002]: Make rendering asynchronous
    // Current architecture is synchronous: main thread calls render() and BLOCKS
    // waiting for worker threads to complete all tiles. This prevents the main
    // thread from running engine updates independently of rendering.
    //
    // For true engine/render separation we'd need:
    // - Double/triple buffering for frame data
    // - Command queue between engine and renderer
    // - Frame-behind rendering (engine on frame N+1 while rendering frame N)
    // - Complex synchronization for particle updates
    //
    // Current impact: Main thread idle during rendering (wastes ~70% of frame time)
    // Expected speedup: 1.5-2x with proper async (engine and render in parallel)

    #if ENABLE_PROFILING
    auto frame_start = std::chrono::high_resolution_clock::now();
    #endif

    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::Render);
    
    // Note: Particles are now flushed in update() before lighting calculations
    // This ensures shadows work immediately when particles are created
    
    // Clear framebuffer (GPU will clear if GPU rasterization is active)
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::RenderClear);
    if (!Optimizations::USE_GPU_RASTERIZATION) {
        clear_framebuffer(10, 10, 15);  // Dark background
    }
    // GPU path: GPU clears framebuffer in rasterizer (blit encoder + compute shader)
    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::RenderClear);

    // Render scene
    logosphere::telemetry::phase_begin(logosphere::telemetry::Phase::RenderParticles);

    // Copy background (painted pixels) to main framebuffer.
    // CPU-path only: with GPU rasterization the presented pixels come from
    // render_buffer_ (renderer_->get_framebuffer()), framebuffer_ is not in
    // the present path, and background_buffer_ is never painted — nothing
    // writes it after its initial clear. This ran unconditionally every
    // frame, copying a constant-colour buffer at framebuffer resolution
    // (~27 MB per frame at retina) into a buffer the GPU path never reads.
    if constexpr (!Optimizations::USE_GPU_RASTERIZATION) {
        copy_buffer(background_buffer_, framebuffer_);
    }
    
    // Reset lighting metrics for this frame
    LightingMetrics::get().reset();

    // Flush safe particle deletions (GPU triple buffering + WORKER THREAD safety)
    // ONLY wait if we have deletions READY (not just pending) - eliminates synchronous GPU wait
    int current_frame = renderer_->current_frame_number();
    if (particle_system_.has_ready_deletions(current_frame)) {
        // CRITICAL ORDER:
        // 1. Wait for GPU (frame N-3 async shadow rays must complete)
        // 2. Wait for WORKERS (frame N-1 tile rendering must complete - they hold raw particle pointers!)
        // 3. THEN safe to delete particles

        // STALL INSTRUMENTATION: Measure each phase
        auto stall_start = std::chrono::high_resolution_clock::now();

        renderer_->wait_for_completion();
        auto gpu_done = std::chrono::high_resolution_clock::now();

        renderer_->wait_for_workers_completion();
        auto workers_done = std::chrono::high_resolution_clock::now();

        particle_system_.flush_safe_deletions(current_frame);
        auto delete_done = std::chrono::high_resolution_clock::now();

        auto gpu_ms = std::chrono::duration<double, std::milli>(gpu_done - stall_start).count();
        auto workers_ms = std::chrono::duration<double, std::milli>(workers_done - gpu_done).count();
        auto delete_ms = std::chrono::duration<double, std::milli>(delete_done - workers_done).count();

        std::cout << "[STALL-BREAKDOWN] GPU wait: " << gpu_ms << "ms, Workers: " << workers_ms
                  << "ms, Delete: " << delete_ms << "ms" << std::endl;
    }

    // CRITICAL: Flush any pending particles BEFORE lighting calculations
    // This ensures particles added via queue_particle_addition() are included in shadows
    particle_system_.flush_pending_particles();

    // BVH update moved to Engine::update() before physics (BVH must be ready for collision detection)

    // Update lighting before rendering (BVH is now up-to-date)
    light_system_.update_lighting(particle_system_);

    // Get mutable metrics to pass to render_scene
    EngineMetrics* metrics = &metrics_;

    // Engine orchestrates, the IRenderer implements. MetalRenderer
    // forwards to EngineRenderState::render_scene today (Phase 4 facade).
    renderer_->draw(particle_system_, camera_system_, light_system_, metrics);

    // Copy lighting subsystem metrics
    metrics->light_uv_to_world_time = LightingMetrics::get().uv_to_world_time;
    metrics->light_intensity_calc_time = LightingMetrics::get().intensity_calc_time;
    metrics->light_shadow_ray_time = LightingMetrics::get().shadow_ray_time;
    metrics->light_surface_fetch_time = LightingMetrics::get().surface_fetch_time;
    metrics->light_tone_mapping_time = LightingMetrics::get().tone_mapping_time;
    metrics->light_color_calc_time = LightingMetrics::get().color_calc_time;
    metrics->light_ray_count = LightingMetrics::get().ray_count;
    metrics->light_call_count = LightingMetrics::get().lighting_calls;

    // Copy BVH traversal metrics (for shadow ray optimization analysis)
    metrics->bvh_aabb_tests = LightingMetrics::get().bvh_aabb_tests;
    metrics->bvh_aabb_hits = LightingMetrics::get().bvh_aabb_hits;
    metrics->bvh_nodes_visited = LightingMetrics::get().bvh_nodes_visited;
    metrics->bvh_leaf_tests = LightingMetrics::get().bvh_leaf_tests;
    metrics->bvh_rays_traced = LightingMetrics::get().bvh_rays_traced;

    // Print granular profiling report if this was a profiling frame
    LightingMetrics::get().print_granular_report();

    // Update particle count for metrics
    metrics_.particle_count = static_cast<int>(particle_system_.count());
    
    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::RenderParticles);

    // Particle detection removed - no longer needed for inspector

    // Collect debug overlay data if enabled (rendering happens after GPU sync)
    if (debug_overlay_.is_enabled()) {
        debug_overlay_.collect_metrics(metrics_);
        debug_overlay_.collect_particle_info(particle_system_);
        debug_overlay_.collect_vision_info(vision_system_);
        debug_overlay_.collect_camera_info(camera_system_);
        debug_overlay_.collect_resolution_info(get_resolution_manager());
        debug_overlay_.collect_lighting_metrics();
        debug_overlay_.collect_frame_metrics();
        // NOTE: Actual rendering moved to after GPU sync (lines 958-961)
    }

    // Render UI layer (KG inspector, debug overlays, etc.)
    // TODO[ARCH-001]: This should be handled by Context Manager, not if-statement
    if (ui_system_) {
        // Handle KG Inspector if enabled (K key toggles this)
        if (config_.show_kg_inspector) {
            // Get current mouse position FIRST
            double mouse_x, mouse_y;
            glfwGetCursorPos(platform_ ? static_cast<GLFWwindow*>(platform_->get_native_window_handle()) : nullptr, &mouse_x, &mouse_y);

            // Update input state with current mouse position
            const InputState& input = input_system_.get_input_state();
            ui_system_->update_input_state(
                mouse_x,  // Raw window coordinates - UISystem handles DPI
                mouse_y,  // Raw window coordinates - UISystem handles DPI
                input.mouse_buttons[0],  // Left button
                input.mouse_buttons[1]   // Right button
            );

            // NOW detect which particle is under the mouse (with updated position)
            int hovered_particle = ui_system_->get_particle_at_mouse();

            // Render KG inspector (use engine's core KG module)
            ui_system_->render_kg_inspector(
                &kg_module_,  // KG is now a core engine system
                hovered_particle,
                -1,   // Anchoring is handled internally by UISystem
                this  // Pass Engine for data access (Data Broker pattern)
            );
        }

        // Render entity highlighting (must be before ui_system_->render())
        ui_system_->render_entity_highlighting(this);
    }

    logosphere::telemetry::phase_end(logosphere::telemetry::Phase::Render);

    // UI rendering with GPU synchronization — NON-BLOCKING (2026-07 GPU
    // audit, experiment B). The old busy-wait here blocked the main
    // thread on the ENTIRE GPU frame every frame, serializing CPU and
    // GPU: the same workload ran 29 FPS windowed vs 60+ headless. If the
    // GPU frame isn't finished yet, skip UI drawing for this iteration:
    // present() already FRAME_SKIPs un-ready frames, the loop proceeds
    // into the next update, and the dispatch semaphore (GPU_BUFFER_SLOTS)
    // provides the pacing. UI draws once per completed frame, under the
    // frame mutex that excludes the GPU callback's framebuffer memcpy —
    // the race the old wait guarded against. The 5s watchdog/event-pump
    // machinery died with the wait (nothing blocks anymore).
    if (ui_system_ && Optimizations::USE_GPU_RASTERIZATION) {
        if (display_) {
            // Windowed: UI draws in present(), inside the SAME
            // critical section as the blit. Drawing it here left a
            // gap where the next GPU completion callback memcpy'd
            // over the UI pixels before present() blitted — one
            // HUD-less frame on screen, the post-GPU-audit flicker
            // (RCA 2026-07-30, the UI overlay design notes).
        } else if (renderer_->is_frame_ready()) {
            // Headless: no present loop, so framebuffer-reading tests
            // get their UI pixels here. Raster into the overlay plane
            // unlocked (nothing else writes it), then composite into
            // the scene buffer under the mutex.
            draw_ui_overlays();
            std::lock_guard<std::mutex> lock(renderer_->get_frame_mutex());
            composite_ui_overlay();
        }
    } else if (ui_system_) {
        // Non-GPU path: no async writer to race with.
        draw_ui_overlays();
        composite_ui_overlay();
    }

    // SPIKE DETECTION: Log render spikes only when exceeding threshold
    #if ENABLE_PROFILING
    auto render_duration = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - frame_start).count();
    if (render_duration > 16.0) {  // Log renders taking >1 frame @ 60fps
        const auto& metrics = metrics_;
        std::cout << "\n[FRAME SPIKE] Render took " << render_duration << "ms (particles: "
                  << particle_system_.count() << ")\n"
                  << "  Breakdown:\n"
                  << "    Flush particles: (included in BVH/lighting)\n"
                  << "    BVH update:      (included in lighting)\n"
                  << "    Lighting update: (included in render_particles)\n"
                  << "    Render scene:    " << metrics.render_time << "ms\n"
                  << "    GPU rasterize:   " << (render_duration - metrics.render_time) << "ms\n"
                  << std::endl;
    }
    #endif
}



TestContext Engine::create_test_context() {
    return TestContext(*this);
}

// Raster every UI overlay into the shared framebuffer. GPU path
// callers MUST hold the frame mutex and have a completed frame in
// the buffer; present() is the one windowed call site (UI + blit in
// one critical section — flicker RCA 2026-07-30, see
// the UI overlay design notes).
void Engine::draw_ui_overlays() {
    if (!ui_system_) return;

    // Clear last frame's UI, then raster this frame's. Only the region
    // the HUD actually painted is touched — a full clear of a
    // render-resolution plane would be ~27 MB of writes at retina for a
    // few thousand HUD pixels.
    if (ui_buffer_.has_dirty_bounds()) {
        int min_x, min_y, max_x, max_y;
        ui_buffer_.get_dirty_bounds(min_x, min_y, max_x, max_y);
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                ui_buffer_.set_pixel(x, y, 0, 0, 0, 0);  // alpha 0 = untouched
            }
        }
    }
    ui_buffer_.reset_dirty_bounds();

    if (debug_overlay_.is_enabled()) {
        debug_overlay_.render(ui_system_, this, renderer_.get());
    }
    ui_system_->render();
    if (mode_ == EngineMode::Debug) {
        ui_system_->render_sandbox_test_menu(true);
    }
    if (config_.show_time_display) {
        render_time_display();
    }
}

// Compose (scene OVER ui) for the overlay's dirty rect into a staging
// buffer, leaving the scene buffer untouched. Presenting must not mutate
// the scene: the same scene is re-presented on every UI-only refresh, and
// blending into it would accumulate UI over UI.
bool Engine::build_overlay_staging(int& out_x, int& out_y, int& out_w, int& out_h) {
    if (!ui_buffer_.has_dirty_bounds()) return false;

    int min_x, min_y, max_x, max_y;
    ui_buffer_.get_dirty_bounds(min_x, min_y, max_x, max_y);
    min_x = std::max(0, min_x);
    min_y = std::max(0, min_y);
    max_x = std::min(max_x, render_buffer_.width()  - 1);
    max_y = std::min(max_y, render_buffer_.height() - 1);
    if (max_x < min_x || max_y < min_y) return false;

    const int w = max_x - min_x + 1;
    const int h = max_y - min_y + 1;
    overlay_staging_.resize(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            EnhancedPixel s = render_buffer_.get_pixel(min_x + x, min_y + y);
            EnhancedPixel u = ui_buffer_.get_pixel(min_x + x, min_y + y);
            uint8_t r, g, b;
            if (u.a == 0) {                 // UI did not paint here
                r = s.r; g = s.g; b = s.b;
            } else if (u.a == 255) {
                r = u.r; g = u.g; b = u.b;
            } else {                        // src-over
                const int ia = 255 - u.a;
                r = static_cast<uint8_t>((u.r * u.a + s.r * ia) / 255);
                g = static_cast<uint8_t>((u.g * u.a + s.g * ia) / 255);
                b = static_cast<uint8_t>((u.b * u.a + s.b * ia) / 255);
            }
            // Match PixelBuffer's native packing: (a<<24)|(r<<16)|(g<<8)|b
            overlay_staging_[static_cast<size_t>(y) * w + x] =
                (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
    out_x = min_x; out_y = min_y; out_w = w; out_h = h;
    return true;
}

// Blend the overlay plane over the scene buffer, dirty region only.
// The scene buffer is what present() uploads; the GPU completion
// callback memcpys over it, so GPU-path callers must hold the frame
// mutex. Alpha 0 in the plane means "UI did not paint here".
void Engine::composite_ui_overlay() {
    if (!ui_buffer_.has_dirty_bounds()) return;

    int min_x, min_y, max_x, max_y;
    ui_buffer_.get_dirty_bounds(min_x, min_y, max_x, max_y);
    max_x = std::min(max_x, render_buffer_.width()  - 1);
    max_y = std::min(max_y, render_buffer_.height() - 1);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            EnhancedPixel ui = ui_buffer_.get_pixel(x, y);
            if (ui.a == 0) continue;                 // untouched
            if (ui.a == 255) {
                render_buffer_.set_pixel(x, y, ui.r, ui.g, ui.b, 255);
            } else {
                render_buffer_.blend_pixel(x, y, ui.r, ui.g, ui.b, ui.a, 0);
            }
        }
    }
}

// Publish the telemetry phases consumers read (debug overlay, UI, tests)
// into the EngineMetrics snapshot. The string-keyed dispatch this replaces
// ran a hash lookup per phase per frame — see docs/PERFORMANCE_RESEARCH.md.
void Engine::publish_metrics() {
    namespace T = logosphere::telemetry;
    metrics_.input_time            = T::phase_ms(T::Phase::Input);
    metrics_.physics_time          = T::phase_ms(T::Phase::Physics);
    metrics_.ui_time               = T::phase_ms(T::Phase::UI);
    metrics_.render_time           = T::phase_ms(T::Phase::Render);
    metrics_.render_clear_time     = T::phase_ms(T::Phase::RenderClear);
    metrics_.render_particles_time = T::phase_ms(T::Phase::RenderParticles);
    metrics_.total_frame_time      = T::phase_ms(T::Phase::Frame);

    // FPS. Nothing populated these before: MetricsCollector::update_fps had
    // no callers, so every consumer (debug overlay, UI) read a hard 0. Derive
    // from the measured frame instead, smoothed so the HUD is readable, with
    // min/max over a rolling second.
    if (metrics_.total_frame_time > 0.0) {
        const double inst = 1000.0 / metrics_.total_frame_time;
        metrics_.current_fps = (metrics_.current_fps > 0.0)
            ? (metrics_.current_fps * 0.9 + inst * 0.1)   // ~10-frame EMA
            : inst;
        fps_window_accum_ += metrics_.total_frame_time;
        metrics_.min_fps = std::min(metrics_.min_fps, inst);
        metrics_.max_fps = std::max(metrics_.max_fps, inst);
        if (fps_window_accum_ >= 1000.0) {   // reset the window each second
            fps_window_accum_ = 0.0;
            metrics_.min_fps = inst;
            metrics_.max_fps = inst;
        }
    }
    // Light and triangle counts are published by the render pipeline, which
    // is where they are actually known.
    T::set_particle_count(static_cast<uint32_t>(particle_system_.count()));
}

// Render game time HUD overlay
void Engine::render_time_display() {
    if (!ui_system_) return;

    // Get game time info
    double time = GameTime::get_current_time();
    double day_frac = GameTime::get_day_fraction(time);
    int hour = static_cast<int>(day_frac * 24.0);
    int minute = static_cast<int>((day_frac * 24.0 - hour) * 60.0);
    double scale = GameTime::get_time_scale();

    char time_str[64];
    snprintf(time_str, sizeof(time_str), "Day %d  %02d:%02d  %.2fx",
             GameTime::get_day(time), hour, minute, scale);

    // Draw in middle-right of screen (vertically centered)
    int screen_w = get_resolution_manager().get_render_width();
    int screen_h = get_resolution_manager().get_render_height();
    ui_system_->draw_text(screen_w - 150, screen_h / 2, time_str, 255, 255, 100);
}

// Present framebuffer to window (for custom loops)
void Engine::present() {

    // PERFORMANCE DIAGNOSIS: Time present() call
    auto present_start = std::chrono::high_resolution_clock::now();

    if (display_ && renderer_) {
        // ASYNC GPU FIX: Only present if GPU frame is ready (fixes data race bug)
        // When using async GPU rasterization, the completion callback writes to pixel_buffer
        // on Metal's background thread. We must check if it's safe to read before presenting.
        if (Optimizations::USE_GPU_RASTERIZATION) {
            // GPU path: Check if frame is ready before presenting
            static int total_present_calls = 0;
            static int skipped_frames = 0;
            static auto last_actual_present = std::chrono::high_resolution_clock::now();

            total_present_calls++;

            // FPS-INDEPENDENT UI: present on a NEW GPU frame, or on the UI's
            // own cadence when the GPU has nothing new. Previously this
            // returned without drawing anything unless a frame was ready, so
            // the HUD could only refresh as fast as the engine rendered.
            // Each present fills a fresh drawable, so UI-only refreshes are
            // capped (UI_PRESENT_MAX_HZ); frame-driven presents never are.
            const bool new_frame = renderer_->is_frame_ready();
            const auto now_check = std::chrono::high_resolution_clock::now();
            const double since_present_ms = std::chrono::duration<double, std::milli>(
                now_check - last_actual_present).count();
            const bool ui_refresh_due =
                since_present_ms >= (1000.0 / Optimizations::UI_PRESENT_MAX_HZ);

            if (new_frame || ui_refresh_due) {
                auto now = std::chrono::high_resolution_clock::now();
                auto time_since_last_present = std::chrono::duration<double, std::milli>(now - last_actual_present).count();

                // Log if it's been more than 100ms since last actual present
                if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
                    if (time_since_last_present > 100.0) {
                        std::cout << "[PRESENT_STALL] " << time_since_last_present << "ms since last screen update ("
                                  << skipped_frames << " frames skipped, " << particle_system_.count() << " particles)" << std::endl;
                    }
                }

                // UI rasters into its OWN plane, outside the lock —
                // nothing else writes that plane, and keeping the
                // expensive part out of the critical section stops the
                // GPU completion callback queueing behind text
                // rasterization.
                draw_ui_overlays();

                // Staging composite + upload stay ONE critical section: the
                // GPU completion callback memcpys fresh frames over the
                // scene buffer under the same mutex, so reading the scene
                // and uploading it must be atomic with respect to it or the
                // screen tears / shows HUD-less frames (the flicker RCA,
                // the UI overlay design notes).
                //
                // The composite goes to a STAGING rect, never into the scene
                // buffer: the same scene is re-presented on every UI-only
                // refresh, and blending into it would stack UI over UI.
                std::lock_guard<std::mutex> lock(renderer_->get_frame_mutex());

                const PixelBuffer& fb = renderer_->get_framebuffer();
                const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.get_native_data());
                int render_w = fb.width();
                int render_h = fb.height();
                int window_w = 0, window_h = 0;
                display_->get_window_size(window_w, window_h);

                int ox = 0, oy = 0, ow = 0, oh = 0;
                if (build_overlay_staging(ox, oy, ow, oh)) {
                    display_->present_with_overlay(pixels, render_w, render_h,
                                                   window_w, window_h,
                                                   overlay_staging_.data(),
                                                   ox, oy, ow, oh);
                } else {
                    display_->present(pixels, render_w, render_h, window_w, window_h);
                }

                // Only a frame we actually consumed may be marked presented;
                // UI-only refreshes must leave the flag alone or the next
                // real frame is dropped.
                if (new_frame) {
                    renderer_->mark_frame_presented();
                }

                last_actual_present = now;
                skipped_frames = 0;
            } else {
                // Neither a new frame nor a UI refresh due — nothing to draw.
                skipped_frames++;

                // Log occasional frame skipping for diagnosis
                if (skipped_frames % 100 == 0) {
                    std::cout << "[FRAME_SKIP] " << skipped_frames << " consecutive frames skipped (GPU not ready, "
                              << particle_system_.count() << " particles)" << std::endl;
                }
            }
        } else {
            // CPU path: Always safe to present immediately (no async)
            const PixelBuffer& fb = renderer_->get_framebuffer();
            const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.get_native_data());
            int window_w = 0, window_h = 0;
            display_->get_window_size(window_w, window_h);
            display_->present(pixels, fb.width(), fb.height(), window_w, window_h);
        }
    }

    // End frame timing measurement

    if constexpr (Optimizations::ENABLE_PROFILING) {
        auto present_duration = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - present_start).count();

        // Consolidated frame profiling - sample every 60 frames
        static int frame_counter = 0;
        frame_counter++;

        // Stall detection - check EVERY frame (not just every 60)
        {
            const auto& m = metrics_;
            double total_frame = g_last_update_time + m.render_time + present_duration;
            if (total_frame > Optimizations::STALL_FRAME_THRESHOLD_MS) {
                // Attribution inline: a stall total without the split is
                // unactionable (task #21 — live-stall RCA).
                double update_accounted = g_last_poll_time + g_last_movement_time + m.input_time
                                        + m.physics_time + m.ui_time;
                std::cout << "[STALL-FRAME] Frame " << frame_counter
                          << " took " << total_frame << "ms (FPS: " << (1000.0/total_frame) << ")"
                          << " | update=" << g_last_update_time
                          << " (poll=" << g_last_poll_time
                          << " move=" << g_last_movement_time
                          << " input=" << m.input_time
                          << " physics=" << m.physics_time
                          << " ui=" << m.ui_time
                          << " unacc=" << (g_last_update_time - update_accounted) << ")"
                          << " render=" << m.render_time
                          << " present=" << present_duration
                          << " particles=" << particle_system_.count() << std::endl;
            }

            // Physics spike detection, independent of the frame total: a
            // 5x physics outlier (22.9 vs 4.3 ms median, measured 2026-07-24)
            // does not by itself cross the stall threshold, so it would never
            // print. Separate detector, same zero-cost-when-quiet shape.
            if (m.physics_time > Optimizations::PHYSICS_SPIKE_THRESHOLD_MS) {
                std::cout << "[PHYSICS-SPIKE] Frame " << frame_counter
                          << " physics=" << m.physics_time << "ms"
                          << " | frame_total=" << total_frame
                          << " particles=" << particle_system_.count() << std::endl;
            }
        }

        // Log comprehensive frame summary every 60 frames
        if (frame_counter % 60 == 0) {
            const auto& m = metrics_;

            // Calculate totals from existing metrics (using globals from update())
            double update_accounted = g_last_poll_time + g_last_movement_time + m.input_time
                                    + m.physics_time + m.ui_time;
            double update_unaccounted = g_last_update_time - update_accounted;
            double total_frame = g_last_update_time + m.render_time + present_duration;

            std::cout << "\n[FRAME_SUMMARY] Frame " << frame_counter
                      << " | " << particle_system_.count() << " particles | "
                      << std::fixed << std::setprecision(1)
                      << (1000.0 / total_frame) << " FPS (" << total_frame << "ms)\n"
                      << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                      << "UPDATE:  " << g_last_update_time << "ms\n"
                      << "  Poll events:     " << g_last_poll_time << "ms\n"
                      << "  Movement:        " << g_last_movement_time << "ms\n"
                      << "  Input:           " << m.input_time << "ms\n"
                      << "  Physics:         " << m.physics_time << "ms\n"
                      << "  UI:              " << m.ui_time << "ms\n"
                      << "  Unaccounted:     " << update_unaccounted << "ms\n"
                      << "RENDER:  " << m.render_time << "ms\n"
                      << "  Collect:         " << m.render_culling_time << "ms\n"
                      << "  Surfaces:        " << m.surfaces_rendered << "\n"
                      << "PRESENT: " << present_duration << "ms\n"
                      << "TOTAL:   " << total_frame << "ms\n"
                      << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                      << std::endl;
        }

        // Spike detection for present
        if (present_duration > 16.0) {
            if (Optimizations::USE_GPU_RASTERIZATION) {
                bool was_ready = renderer_->is_frame_ready();
                std::cout << "\n[FRAME SPIKE] Present took " << present_duration << "ms\n"
                          << "  GPU frame ready: " << (was_ready ? "YES" : "NO") << "\n"
                          << "  Context: " << (was_ready ? "Presented frame" : "Skipped (GPU not ready)") << "\n"
                          << std::endl;
            } else {
                std::cout << "\n[FRAME SPIKE] Present took " << present_duration << "ms (CPU path)\n" << std::endl;
            }
        }
    }
}

// Get window handle for GLFW operations
void* Engine::get_window_handle() const {
    if (platform_) {
        return platform_->get_native_window_handle();
    }
    return nullptr;
}

// Internal particle creation (for tests and legacy code)
// Engine is a friend of ParticleSystem, so this provides access to protected add_particle
int Engine::add_particle(const Particle& p) {
    return particle_system_.add_particle(p);
}

// Debug Possession Mode: Save/restore input target when toggling debug overlay
// This generic mechanism works for ANY game type (RPG, RTS, puzzle, etc.)
void Engine::toggle_debug_overlay() {
    bool entering_debug = !debug_overlay_.is_enabled();

    if (entering_debug) {
        // Save current input target when entering debug mode
        saved_input_target_for_debug_ = input_target_entity_id_;
        std::cout << "[DEBUG_POSSESSION] Entering debug mode, saved input target: "
                  << saved_input_target_for_debug_ << std::endl;
    } else {
        // Restore saved input target when exiting debug mode
        input_target_entity_id_ = saved_input_target_for_debug_;
        std::cout << "[DEBUG_POSSESSION] Exiting debug mode, restored input target: "
                  << input_target_entity_id_ << std::endl;
    }

    debug_overlay_.set_enabled(!debug_overlay_.is_enabled());
    debug_overlay_.set_show_compass(debug_overlay_.is_enabled());  // Show compass with debug overlay
    config_.show_debug_overlay = debug_overlay_.is_enabled();
}

bool Engine::initialize_llm(const std::string& model_path) {
#ifdef HAS_LLAMA
    if (llm_system_) {
        std::cout << "[Engine] LLM already initialized" << std::endl;
        return false;  // Already initialized
    }

    std::cout << "[Engine] Initializing LLM system (HTTP backend)..." << std::endl;

    llm_system_ = new Logosphere::LLMSystemHTTP();

    // Use mlx_lm.server at localhost:8080 (its default port).
    // Run with: mlx_lm.server --model mlx-community/Qwen2.5-32B-Instruct-4bit
    // Model name MUST match exactly, or MLX will try to download from HuggingFace.
    if (!llm_system_->initialize_mlx("http://localhost:8080", "mlx-community/Qwen2.5-32B-Instruct-4bit")) {
        std::cerr << "[Engine] ERROR: Failed to initialize LLM HTTP backend: "
                  << llm_system_->get_last_error() << std::endl;
        std::cerr << "[Engine] Make sure MLX server is running (./scripts/start_mlx_server.sh)" << std::endl;
        delete llm_system_;
        llm_system_ = nullptr;
        return false;
    }

    std::cout << "[Engine] LLM HTTP backend initialized successfully" << std::endl;
    return true;
#else
    (void)model_path;  // Suppress unused parameter warning
    std::cerr << "[Engine] ERROR: LLM not available - rebuild with llama.cpp" << std::endl;
    return false;
#endif
}

void Engine::create_sun() {
    if (!sun_exists_) {
        float sun_distance = 500.0f;
        float sun_elevation_angle = 15.0f;
        float elevation_radians = sun_elevation_angle * 3.14159f / 180.0f;
        float sun_x = sun_distance * std::cos(elevation_radians);
        float sun_y = 0.0f;
        float sun_z = sun_distance * std::sin(elevation_radians);

        auto& config = LightingConfig::get();
        float sun_strength = config.light_sources.game_scale.stadium_light * 250000.0f;
        float sun_radius = 1000.0f;

        sun_particle_id_ = particle_system_.queue_light(
            sun_x, sun_y, sun_z, sun_strength, sun_radius, 1.0f, 0.8f, 0.5f);
        sun_exists_ = true;
        std::cout << "[Engine] Sun created at (" << sun_x << ", " << sun_y << ", " << sun_z
                  << ") elevation=" << sun_elevation_angle << "°" << std::endl;
    }
}

void Engine::toggle_sun() {
    if (sun_exists_) {
        std::cout << "[Engine] Removing sun (darkness falls)" << std::endl;
        particle_system_.remove_particle(sun_particle_id_);
        sun_exists_ = false;
    } else {
        create_sun();
    }
}

// ============================================================================
// RESOLUTION SWITCHING
// ============================================================================
// Safe resolution change that holds all GPU semaphores while changing drawable.
// This prevents the kernel panic caused by changing CAMetalLayer.drawableSize
// while GPU is rendering.

void Engine::decrease_resolution() {
    std::cout << "[ENGINE] decrease_resolution: starting..." << std::endl;

    // GUARD 1: Time-based debounce (prevents rapid key presses)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - last_resolution_change_).count();

    if (elapsed < RESOLUTION_CHANGE_COOLDOWN_MS) {
        std::cout << "[ENGINE] Resolution change on cooldown ("
                  << (RESOLUTION_CHANGE_COOLDOWN_MS - elapsed) << "ms remaining)" << std::endl;
        return;
    }

    // GUARD 2: Frame-based stabilization
    if (frames_since_resolution_change_ < RESOLUTION_STABILIZATION_FRAMES) {
        std::cout << "[ENGINE] Waiting for frame stabilization ("
                  << (RESOLUTION_STABILIZATION_FRAMES - frames_since_resolution_change_)
                  << " frames remaining)" << std::endl;
        return;
    }

    last_resolution_change_ = now;
    frames_since_resolution_change_ = 0;

    // Get new resolution from manager
    auto& res_mgr = get_resolution_manager();
    float old_width = res_mgr.get_render_width();
    float old_zoom = camera_system_.get_pixels_per_unit();

    res_mgr.cycle_resolution_down();
    int new_width = res_mgr.get_render_width();
    int new_height = res_mgr.get_render_height();

    // Calculate drawable size (with DPI scaling)
    float dpi_scale = platform_->get_dpi_scale_x();
    int drawable_width = static_cast<int>(new_width * dpi_scale);
    int drawable_height = static_cast<int>(new_height * dpi_scale);

    std::cout << "[ENGINE] Resolution: " << old_width << " -> " << new_width
              << " (drawable: " << drawable_width << "x" << drawable_height << ")" << std::endl;

    // CRITICAL: Acquire all GPU semaphores BEFORE changing drawable
    // This ensures GPU is completely idle while we change the drawable size
    std::cout << "[ENGINE] Acquiring GPU slots..." << std::endl;
    int slots = renderer_->acquire_all_gpu_slots();
    if (slots == 0) {
        std::cerr << "[ENGINE] Failed to acquire GPU slots - aborting resolution change" << std::endl;
        return;
    }

    // Change drawable size WHILE holding semaphores (GPU is idle)
    std::cout << "[ENGINE] Changing drawable size while GPU is idle..." << std::endl;
    platform_->force_drawable_resize(drawable_width, drawable_height);

    // Update internal buffers
    std::cout << "[ENGINE] Updating internal buffers..." << std::endl;
    set_render_resolution(new_width, new_height);

    // Release GPU semaphores - rendering can resume
    std::cout << "[ENGINE] Releasing GPU slots..." << std::endl;
    renderer_->release_all_gpu_slots(slots);

    // Adjust zoom to maintain field of view
    float scale_factor = static_cast<float>(new_width) / old_width;
    camera_system_.set_pixels_per_unit(old_zoom * scale_factor);

    std::cout << "[ENGINE] Resolution change complete" << std::endl;
}

void Engine::increase_resolution() {
    std::cout << "[ENGINE] increase_resolution: starting..." << std::endl;

    // GUARD 1: Time-based debounce
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - last_resolution_change_).count();

    if (elapsed < RESOLUTION_CHANGE_COOLDOWN_MS) {
        std::cout << "[ENGINE] Resolution change on cooldown ("
                  << (RESOLUTION_CHANGE_COOLDOWN_MS - elapsed) << "ms remaining)" << std::endl;
        return;
    }

    // GUARD 2: Frame-based stabilization
    if (frames_since_resolution_change_ < RESOLUTION_STABILIZATION_FRAMES) {
        std::cout << "[ENGINE] Waiting for frame stabilization ("
                  << (RESOLUTION_STABILIZATION_FRAMES - frames_since_resolution_change_)
                  << " frames remaining)" << std::endl;
        return;
    }

    last_resolution_change_ = now;
    frames_since_resolution_change_ = 0;

    // Get new resolution from manager
    auto& res_mgr = get_resolution_manager();
    float old_width = res_mgr.get_render_width();
    float old_zoom = camera_system_.get_pixels_per_unit();

    res_mgr.cycle_resolution_up();
    int new_width = res_mgr.get_render_width();
    int new_height = res_mgr.get_render_height();

    // Calculate drawable size (with DPI scaling)
    float dpi_scale = platform_->get_dpi_scale_x();
    int drawable_width = static_cast<int>(new_width * dpi_scale);
    int drawable_height = static_cast<int>(new_height * dpi_scale);

    std::cout << "[ENGINE] Resolution: " << old_width << " -> " << new_width
              << " (drawable: " << drawable_width << "x" << drawable_height << ")" << std::endl;

    // CRITICAL: Acquire all GPU semaphores BEFORE changing drawable
    std::cout << "[ENGINE] Acquiring GPU slots..." << std::endl;
    int slots = renderer_->acquire_all_gpu_slots();
    if (slots == 0) {
        std::cerr << "[ENGINE] Failed to acquire GPU slots - aborting resolution change" << std::endl;
        return;
    }

    // Change drawable size WHILE holding semaphores (GPU is idle)
    std::cout << "[ENGINE] Changing drawable size while GPU is idle..." << std::endl;
    platform_->force_drawable_resize(drawable_width, drawable_height);

    // Update internal buffers
    std::cout << "[ENGINE] Updating internal buffers..." << std::endl;
    set_render_resolution(new_width, new_height);

    // Release GPU semaphores - rendering can resume
    std::cout << "[ENGINE] Releasing GPU slots..." << std::endl;
    renderer_->release_all_gpu_slots(slots);

    // Adjust zoom to maintain field of view
    float scale_factor = static_cast<float>(new_width) / old_width;
    camera_system_.set_pixels_per_unit(old_zoom * scale_factor);

    std::cout << "[ENGINE] Resolution change complete" << std::endl;
}
