#include "input_system.h"
#include "engine.h"           // For accessing camera controller through engine
#include "particle_system.h"  // For access to global particle system
#include "vision_system.h"    // For vision debug toggles
#include "light_system.h"     // For light debug toggles
#include "key_mapper.h"       // For centralized key handling
#include "debug_control.h"    // Centralized debug control
#include "camera_controller.h"  // For camera control
#include "logosphere/kg/kg_module.h"     // For entity selection via KG
#include "object_id.h"        // For ObjectID encoding/decoding
#include "platform/platform_layer.h"  // For IApplication interface
#include <cmath>
#include <iostream>
#include <cassert>

// EDUCATIONAL NOTE: Input System Implementation
//
// This system converts raw GLFW input events into game actions.
// Key concepts:
// 1. Event-driven input (callbacks for discrete events like key presses)
// 2. State-based input (polling current state for continuous input like movement)
// 3. Input processing (converting raw input to meaningful game actions)
// 4. Callback pattern (GLFW calls our functions when events occur)

// All globals have been properly encapsulated:
// - Particles are accessed through g_particle_system.get_particles()
// - Character is accessed through g_physics_system.get_character()
// - Framebuffer is accessed through g_render_system.get_framebuffer()

// EDUCATIONAL NOTE: Getting Configuration from RenderSystem
//
// Instead of duplicating constants, we access them from RenderSystem.
// This ensures all systems use the same values and makes changes easier.
#include "engine.h"  // For g_engine access (Phase 2)
#include "logosphere/physics/physics_system.h"  // For PhysicsSystem type

// Global instance and singleton pointer
InputSystem* InputSystem::instance = nullptr;

InputSystem::InputSystem() {
    // Set up singleton instance for GLFW callbacks
    instance = this;
    
    // Initialize input state
    for (int i = 0; i < GLFW_KEY_LAST; ++i) {
        input_state.keys[i] = false;
    }
    input_state.mouse_x = 0.0;
    input_state.mouse_y = 0.0;
    for (int i = 0; i < 3; ++i) {
        input_state.mouse_buttons[i] = false;
    }
}

InputSystem::~InputSystem() {
    // Clear singleton pointer
    if (instance == this) {
        instance = nullptr;
    }
}

void InputSystem::initialize(GLFWwindow* window) {
    // EDUCATIONAL NOTE: DPI Scale Factor Calculation
    //
    // On high-DPI displays, the framebuffer size differs from window size.
    // We need to calculate the scale factor to convert mouse coordinates
    // from window space to framebuffer space.
    int window_width, window_height;
    glfwGetWindowSize(window, &window_width, &window_height);

    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);

    dpi_scale_x_ = static_cast<float>(fb_width) / window_width;
    dpi_scale_y_ = static_cast<float>(fb_height) / window_height;

    DEBUG_LOG("InputSystem: DPI scale factor: " << dpi_scale_x_ << "x" << dpi_scale_y_);

    // REMOVED: Direct GLFW callback registration
    // InputSystem now receives events through Platform abstraction (IInputCallbacks)
    // Platform's callbacks are registered in PlatformMacOS::create_window()
    // Engine connects InputSystem to Platform via platform->set_input_callbacks(&input_system)
    //
    // OLD CODE (removed to avoid double registration):
    // glfwSetKeyCallback(window, key_callback_static);
    // glfwSetCursorPosCallback(window, cursor_position_callback_static);
    // glfwSetMouseButtonCallback(window, mouse_button_callback_static);
}

// EDUCATIONAL NOTE: Static Callback Functions
//
// GLFW requires C-style function pointers for callbacks.
// Since C++ member functions have a hidden 'this' parameter,
// they can't be used directly as C callbacks.
// 
// Solution: Static functions that call instance methods

void InputSystem::key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (instance) {
        instance->handle_key_callback(window, key, scancode, action, mods);
    }
}

void InputSystem::cursor_position_callback_static(GLFWwindow* window, double xpos, double ypos) {
    if (instance) {
        instance->handle_cursor_position_callback(window, xpos, ypos);
    }
}

void InputSystem::mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods) {
    if (instance) {
        instance->handle_mouse_button_callback(window, button, action, mods);
    }
}

// Input isolation helper - determines if key is a "system key" that always passes through
bool InputSystem::is_system_key(int key) const {
    return key == GLFW_KEY_ESCAPE ||
           key == GLFW_KEY_GRAVE_ACCENT ||  // ` key for debug overlay
           (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12);
}

void InputSystem::handle_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // EDUCATIONAL NOTE: Key Event Processing
    //
    // GLFW sends us three types of key events:
    // - GLFW_PRESS: Key was just pressed
    // - GLFW_RELEASE: Key was just released
    // - GLFW_REPEAT: Key is being held down (auto-repeat)
    //
    // For most games, we care about PRESS and RELEASE

    // Update key state array
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        input_state.keys[key] = true;
    } else if (action == GLFW_RELEASE) {
        input_state.keys[key] = false;
    }

    // ARCH-017 COMPLETED: Assert pattern applied to all systems with stored dependencies
    assert(engine_ && "InputSystem requires Engine to be set via set_engine()");

    // Phase 3 Input Isolation: Check if UI has exclusive focus
    auto* ui = engine_->get_ui_system();

    // System keys (ESC, `, F-keys) always pass to both UI and game
    if (is_system_key(key)) {
        if (ui) {
            ui->handle_key_down(key);
        }
        engine_->get_key_mapper().process_key_event(key, scancode, action, mods);
        return;
    }

    // Regular keys: check UI exclusive focus
    bool has_focus = ui && ui->has_exclusive_input_focus();

    // DEBUG: Log focus state and key presses
    if (key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD) {
        std::cout << "[INPUT_FOCUS_DEBUG] Key=" << (key == GLFW_KEY_COMMA ? "COMMA" : "PERIOD")
                  << " has_focus=" << has_focus << std::endl;
    }

    // DEBUG: Log SPACE key routing
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        std::cout << "[InputSystem] SPACE press, has_focus=" << has_focus << std::endl;
    }

    if (has_focus) {
        // UI has focus - route key to UI for special keys (Enter, Backspace, Escape, etc.)
        // but BLOCK all keys from reaching KeyMapper (prevents comma/period triggering zoom, etc.)
        ui->handle_key_down(key);
        return;  // Block ALL keys from game when chat is active
    }

    // No UI focus - route to game via KeyMapper
    engine_->get_key_mapper().process_key_event(key, scancode, action, mods);
}

// IInputCallbacks interface implementation - receives events from Platform layer
bool InputSystem::has_ui_exclusive_focus() const {
    if (!engine_) return false;
    auto* ui = engine_->get_ui_system();
    return ui ? ui->has_exclusive_input_focus() : false;
}

void InputSystem::on_key_event(Platform::KeyCode key, int scancode, bool pressed, const Platform::ModifierKeys& mods) {
    // Convert Platform types back to GLFW for now (temporary during migration)
    // TODO: Update KeyMapper to use Platform::KeyCode directly
    int glfw_key = static_cast<int>(key);  // Platform::KeyCode values match GLFW
    int glfw_action = pressed ? GLFW_PRESS : GLFW_RELEASE;
    int glfw_mods = 0;
    if (mods.shift) glfw_mods |= GLFW_MOD_SHIFT;
    if (mods.control) glfw_mods |= GLFW_MOD_CONTROL;
    if (mods.alt) glfw_mods |= GLFW_MOD_ALT;
    if (mods.super) glfw_mods |= GLFW_MOD_SUPER;

    // Check UI exclusive focus - when chat is open, ALL keyboard input goes to UI only
    if (engine_) {
        auto* ui = engine_->get_ui_system();
        if (ui && ui->has_exclusive_input_focus()) {
            // UI has focus - forward ALL key presses to UI for text input
            if (pressed) {
                ui->handle_text_input_key(glfw_key);
            }
            return;  // Block from game completely
        }
    }

    // No UI focus - route keys to game
    handle_key_callback(nullptr, glfw_key, scancode, glfw_action, glfw_mods);
}

void InputSystem::on_char_input(unsigned int codepoint) {
    // DEBUG: Log all character input
    std::cout << "[INPUT_DEBUG] on_char_input: codepoint=" << codepoint
              << " char='" << (char)codepoint << "'" << std::endl;

    // Forward character input directly to UISystem (engine-level text input)
    if (engine_) {
        auto* ui = engine_->get_ui_system();
        if (ui) {
            // Only handle printable ASCII/Unicode characters
            if (codepoint >= 32 && codepoint < 127) {
                ui->handle_char_input(codepoint);
            } else {
                std::cout << "[INPUT_DEBUG] Rejected codepoint " << codepoint
                          << " (out of range 32-127)" << std::endl;
            }
        }
    }
}

void InputSystem::on_mouse_move(double x, double y) {
    // DEBUG: Verify callbacks are working (will remove after testing)
    static int call_count = 0;
    if (call_count < 5) {
        std::cout << "[INPUT_CALLBACK] on_mouse_move called: (" << x << ", " << y << ")" << std::endl;
        call_count++;
    }

    // Directly call existing handler (window parameter is unused)
    handle_cursor_position_callback(nullptr, x, y);
}

void InputSystem::on_mouse_button(Platform::MouseButton button, bool pressed, const Platform::ModifierKeys& mods) {
    // Convert Platform types to GLFW
    int glfw_button = static_cast<int>(button);
    int glfw_action = pressed ? GLFW_PRESS : GLFW_RELEASE;
    int glfw_mods = 0;
    if (mods.shift) glfw_mods |= GLFW_MOD_SHIFT;
    if (mods.control) glfw_mods |= GLFW_MOD_CONTROL;
    if (mods.alt) glfw_mods |= GLFW_MOD_ALT;
    if (mods.super) glfw_mods |= GLFW_MOD_SUPER;

    handle_mouse_button_callback(nullptr, glfw_button, glfw_action, glfw_mods);
}

void InputSystem::on_mouse_scroll(double x_offset, double y_offset) {
    if (!engine_) return;

    // UI first: the widget under the cursor may want the scroll
    // (chat scrollback). Widgets that don't care return false.
    auto* ui = engine_->get_ui_system();
    if (ui && ui->handle_mouse_scroll(static_cast<int>(input_state.mouse_x),
                                      static_cast<int>(input_state.mouse_y),
                                      x_offset, y_offset)) {
        return;
    }

    // Debug-overlay mode (backtick) keeps the pointer for inspection;
    // camera gestures apply only in play mode.
    if (engine_->get_show_debug_overlay()) return;

    // Trackpad camera: vertical scroll zooms, horizontal orbits.
    auto& cam = engine_->get_camera_system();
    if (y_offset != 0.0) {
        cam.adjust_zoom(static_cast<float>(y_offset) * SCROLL_ZOOM_SCALE);
    }
    if (x_offset != 0.0) {
        cam.set_view_azimuth(cam.get_view_azimuth() +
                             static_cast<float>(x_offset) * SCROLL_ORBIT_SCALE);
    }
}

void InputSystem::on_window_resize(int width, int height) {
    // Handle window resize events if needed
}

void InputSystem::on_window_close() {
    // Handle window close events if needed
}

void InputSystem::handle_cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    // Update mouse position in input state
    input_state.mouse_x = xpos;
    input_state.mouse_y = ypos;

    // PERFORMANCE FIX: Hover detection moved to update_hover_detection()
    // This decouples from mouse callback (1000+ Hz) to game loop (60-120 Hz)
    // See docs/mouse_hover_performance.md for rationale

    // Route to UISystem for widget interactions (dragging, hover, etc.)
    if (engine_ && engine_->get_ui_system()) {
        engine_->get_ui_system()->handle_mouse_move(static_cast<int>(xpos), static_cast<int>(ypos));
    }

    // Forward to camera controller if available
    if (engine_ && engine_->get_camera_controller()) {
        engine_->get_camera_controller()->on_mouse_move(xpos, ypos);
    }
}

void InputSystem::handle_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    // Update mouse button state
    if (button >= 0 && button < 3) {
        input_state.mouse_buttons[button] = (action == GLFW_PRESS);
    }

    // Entity selection in debug mode (left click)
    // Uses object ID buffer for pixel-perfect picking (DRY with KG inspector hover)
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && engine_) {
        // Only allow entity selection in debug mode and when KG inspector is off
        bool debug_mode = engine_->get_show_debug_overlay();
        bool kg_inspector_active = engine_->get_show_kg_inspector();

        if (debug_mode && !kg_inspector_active) {
            // TODO: GPU rasterization doesn't populate object_map - need to read G-buffer
            // For now, clicking won't work with GPU rasterization enabled
            std::cout << "[INPUT] Entity selection by clicking not yet supported with GPU rasterization" << std::endl;
        }
    }

    // CRITICAL: Give application first chance to handle mouse buttons
    // This allows games to override engine behavior with custom handlers
    if (engine_ && engine_->getApplication()) {
        if (engine_->getApplication()->handle_mouse_button(button, action, mods)) {
            // Application handled the click, don't process further
            return;
        }
    }

    // Debug: On left mouse click, show coordinate transformation
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && engine_) {
        std::cout << "\n[MOUSE CLICK DEBUG]" << std::endl;

        // Get current mouse position (window may be null from Platform callbacks)
        GLFWwindow* glfw_window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
        double mouse_x, mouse_y;
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);
        std::cout << "  Window coords: (" << mouse_x << ", " << mouse_y << ")" << std::endl;

        // Convert to framebuffer and world coordinates
        if (engine_) {
            int fb_x, fb_y;
            engine_->get_coord_transformer().mouse_to_framebuffer(mouse_x, mouse_y, fb_x, fb_y);
            std::cout << "  Framebuffer coords: (" << fb_x << ", " << fb_y << ")" << std::endl;

            // Convert to world (using render coords, not framebuffer!)
            int render_x = static_cast<int>(mouse_x);
            int render_y = static_cast<int>(mouse_y);
            float world_x, world_y;
            engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);
            std::cout << "  World coords (raw): (" << world_x << ", " << world_y << ")" << std::endl;

            // Show what negation would give
            std::cout << "  World coords (negated): (" << -world_x << ", " << -world_y << ")" << std::endl;

            // Get camera position for reference
            float cam_x = 0, cam_y = 0, cam_z = 0;
            engine_->get_camera_system().get_position(cam_x, cam_y, cam_z);
            std::cout << "  Camera position: (" << cam_x << ", " << cam_y << ", " << cam_z << ")" << std::endl;

            // Show projection mode
            // std::cout << "  Projection mode: " << engine_->get_render_system().get_current_projection_name() << std::endl;
        }
    }

    // Route to UISystem FIRST (Phase 3: UI gets priority for mouse events)
    if (engine_ && engine_->get_ui_system()) {
        // Get mouse position in window coordinates
        GLFWwindow* glfw_window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
        double mouse_x, mouse_y;
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);

        // Route to UISystem
        if (action == GLFW_PRESS) {
            if (engine_->get_ui_system()->handle_mouse_down(static_cast<int>(mouse_x), static_cast<int>(mouse_y), button)) {
                // UI handled the click - don't forward to game
                return;
            }
        } else if (action == GLFW_RELEASE) {
            if (engine_->get_ui_system()->handle_mouse_up(static_cast<int>(mouse_x), static_cast<int>(mouse_y), button)) {
                // UI handled the release - don't forward to game
                return;
            }
        }
    }

    // Forward to camera controller if UI didn't handle it
    if (engine_ && engine_->get_camera_controller()) {
        engine_->get_camera_controller()->on_mouse_button(button, action, mods);
    }
}

// handle_wasd_input removed - use handle_continuous_movement() instead

// PERFORMANCE FIX: Frame-rate decoupled hover detection
// Called once per frame from Engine::update(), not from mouse callbacks
// See docs/mouse_hover_performance.md for rationale
void InputSystem::update_hover_detection() {
    // Only query in debug mode when KG inspector is off
    if (!engine_ || !engine_->get_show_debug_overlay() || engine_->get_show_kg_inspector()) {
        return;
    }

    // Convert mouse position to framebuffer coordinates (DPI scaling)
    float dpi_scale_x = engine_->get_platform()->get_dpi_scale_x();
    float dpi_scale_y = engine_->get_platform()->get_dpi_scale_y();
    int fb_x = static_cast<int>(input_state.mouse_x * dpi_scale_x);
    int fb_y = static_cast<int>(input_state.mouse_y * dpi_scale_y);

    // Query object at mouse position (once per frame, not per mouse movement!)
    int object_id = engine_->get_pixel_picker().get_object_at_pixel(fb_x, fb_y);

    // DEBUG: Log hover detection (throttled to every 60 frames)
    static int hover_frame_count = 0;
    hover_frame_count++;
    if (hover_frame_count % 60 == 0) {
        std::cout << "[HOVER_DEBUG] Mouse: (" << input_state.mouse_x << "," << input_state.mouse_y << ") "
                  << "→ FB: (" << fb_x << "," << fb_y << ") "
                  << "→ ObjectID: " << object_id << " ";
        if (object_id > 0) {
            if (ObjectID::is_particle(object_id)) {
                std::cout << "(particle " << ObjectID::get_particle_id(object_id) << ")";
            } else {
                std::cout << "(non-particle)";
            }
        } else {
            std::cout << "(background)";
        }
        std::cout << std::endl;
    }

    // Check if we found a particle and get its entity
    int entity_to_set = -1;  // Default: no entity

    if (object_id > 0 && ObjectID::is_particle(object_id)) {
        int particle_id = ObjectID::get_particle_id(object_id);
        Particle p = engine_->get_particle_system().get_particle_copy(particle_id);
        entity_to_set = static_cast<int>(p.entity_id);  // Use particle's entity (including 0!)

        // DEBUG: Log entity detection
        if (hover_frame_count % 60 == 0) {
            std::cout << "[HOVER_DEBUG] Particle " << particle_id
                      << " belongs to Entity " << entity_to_set << std::endl;
        }
    }

    // Update debug overlay with hovered entity (-1 if no particle, otherwise entity_id)
    engine_->get_debug_overlay().set_hovered_entity(entity_to_set);
}

// Character-based mouse functions removed - use calculate_look_direction() instead

float InputSystem::calculate_look_direction(float world_x, float world_y, Engine& engine) const {
    // Calculate look direction from a world position to the mouse cursor
    // This replaces calculate_angle_to_mouse but works for any position

    // BUG FIX: screen_to_world() expects RENDER coordinates, not FRAMEBUFFER coordinates
    // When window size == render size, mouse coords ARE render coords
    int render_x = static_cast<int>(input_state.mouse_x);
    int render_y = static_cast<int>(input_state.mouse_y);

    // Convert to world coordinates
    float mouse_world_x, mouse_world_y;
    engine.get_coord_transformer().screen_to_world(render_x, render_y, mouse_world_x, mouse_world_y);
    
    // Calculate angle from position to mouse
    float dx = mouse_world_x - world_x;
    float dy = mouse_world_y - world_y;
    
    return atan2f(dy, dx);
}

void InputSystem::mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y, Engine& engine) const {
    // EDUCATIONAL NOTE: Coordinate Conversion with DPI Scaling
    //
    // We need to convert from:
    // - Mouse coordinates in window space (logical pixels)
    // To:
    // - Framebuffer coordinates (physical pixels)
    //
    // On Retina displays, we must apply the DPI scale factor.
    
    fb_x = static_cast<int>(mouse_x * dpi_scale_x_);
    fb_y = static_cast<int>(mouse_y * dpi_scale_y_);
    
    // Clamp to framebuffer bounds
    const auto& res = engine.get_resolution_manager();
    fb_x = std::max(0, std::min(fb_x, res.get_window_width() - 1));
    fb_y = std::max(0, std::min(fb_y, res.get_window_height() - 1));
}

void InputSystem::paint_at_mouse_position(Engine& engine) {
    // EDUCATIONAL NOTE: Mouse Painting
    //
    // This feature lets us paint directly on the framebuffer with the mouse.
    // It's useful for testing and comparing our particle rendering with
    // direct pixel manipulation.
    
    if (input_state.mouse_buttons[0]) {  // Left mouse button
        int fb_x, fb_y;
        mouse_to_framebuffer(input_state.mouse_x, input_state.mouse_y, fb_x, fb_y, engine);
        
        // Paint a small brush (3x3 pixels) in bright white
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int paint_x = fb_x + dx;
                int paint_y = fb_y + dy;
                
                const auto& res = engine.get_resolution_manager();
                if (paint_x >= 0 && paint_x < res.get_window_width() &&
                    paint_y >= 0 && paint_y < res.get_window_height()) {
                    engine_->get_draw_surface().set_pixel(paint_x, paint_y, 255, 255, 255);
                }
            }
        }
    }
}

// Character-based process_input removed

void InputSystem::process_non_movement_input(double delta_time, Engine& engine, PhysicsSystem& physics_system) {
    // Process non-movement input (mouse painting, etc.)
    // Mouse look is now handled by calculate_look_direction() and applied to particles directly
    
    // Mouse painting doesn't need character
    paint_at_mouse_position(engine);
}