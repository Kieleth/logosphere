#ifndef INPUT_SYSTEM_H
#define INPUT_SYSTEM_H

#include "particle.h"
#include "platform/platform_types.h"  // For IInputCallbacks interface
#include "platform/glfw_compat.h"     // real GLFW, or no-op shim in GLFW-less profiles
#include <vector>

// EDUCATIONAL NOTE: The Input System
//
// This module handles all user input for our engine:
// 1. Keyboard input - WASD movement, debug toggles, scenario keys
// 2. Mouse input - look direction, painting, interaction
// 3. Input state management - tracking pressed keys and mouse position
// 4. Input processing - converting raw input to game actions
//
// Real games typically have much more complex input systems:
// - Input mapping (customizable controls)
// - Gamepad support (Xbox, PlayStation controllers)
// - Input buffering (for fighting games)
// - Context-sensitive input (menus vs gameplay)
//
// Our system is educational: direct mapping from GLFW to game actions

// Input state structure - tracks all keyboard and mouse state
struct InputState {
    bool keys[GLFW_KEY_LAST] = {false};    // Array for all possible keys
    double mouse_x = 0.0;                   // Mouse X position in window
    double mouse_y = 0.0;                   // Mouse Y position in window  
    bool mouse_buttons[3] = {false};        // Left, Right, Middle buttons
};

// Movement constants - tuned for responsive but not twitchy controls
const float MOVEMENT_SPEED = 20.0f;        // World units per second (20cm/sec at 1cm particles)
const float MOVEMENT_ACCELERATION = 80.0f;  // How fast we reach max speed
const float MOVEMENT_FRICTION = 60.0f;     // How fast we slow down when not pressing keys

// InputSystem now implements Platform::IInputCallbacks to receive input events
// from the platform abstraction layer instead of directly from GLFW
class InputSystem : public Platform::IInputCallbacks {
public:
    // Constructor and destructor
    InputSystem();
    ~InputSystem();
    
    // Initialize the input system
    void initialize(GLFWwindow* window);
    
    // Set the engine pointer for accessing other systems
    void set_engine(class Engine* engine) { engine_ = engine; }

    // Check if UI has exclusive focus (chat is open, etc.) - blocks game input
    bool has_ui_exclusive_focus() const;

    // Get current input state
    const InputState& get_input_state() const { return input_state; }
    InputState& get_input_state() { return input_state; }

    // Update hover detection (called once per frame from game loop)
    // Frame-rate decoupled from mouse callbacks for performance
    void update_hover_detection();
    
    // EDUCATIONAL NOTE: GLFW Callbacks
    //
    // GLFW uses callbacks to notify us about input events.
    // These are C-style function pointers, so they must be static.
    // We use a singleton pattern to access the InputSystem instance.
    
    // IInputCallbacks interface implementation (receives events from Platform layer)
    void on_key_event(Platform::KeyCode key, int scancode, bool pressed, const Platform::ModifierKeys& mods) override;
    void on_char_input(unsigned int codepoint) override;
    void on_mouse_move(double x, double y) override;
    void on_mouse_button(Platform::MouseButton button, bool pressed, const Platform::ModifierKeys& mods) override;
    void on_mouse_scroll(double x_offset, double y_offset) override;

    // Trackpad camera feel. Zoom is in pixels-per-unit per scroll
    // unit (ppu range 5..200); orbit in radians per unit. Tuned on a
    // macOS trackpad; flip a sign here if a device feels inverted.
    static constexpr float SCROLL_ZOOM_SCALE = 3.0f;
    static constexpr float SCROLL_ORBIT_SCALE = 0.05f;
    void on_window_resize(int width, int height) override;
    void on_window_close() override;

    // DEPRECATED: Old GLFW callback functions (will be removed)
    // These are kept temporarily for compatibility during migration
    static void key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursor_position_callback_static(GLFWwindow* window, double xpos, double ypos);
    static void mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods);
    
    // Character-based input removed - use particle input target system instead
    float calculate_look_direction(float world_x, float world_y, Engine& engine) const;  // For any position
    
    // Mouse interaction with world
    void mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y, Engine& engine) const;
    void paint_at_mouse_position(Engine& engine);
    
    // 3D Cube Mouse Detection - finds particle under mouse cursor using 3D cube intersection
    int get_particle_at_mouse(Engine& engine, class ParticleSystem& particle_system) const;
    
    // DEPRECATED: Character-based input processing removed
    
    // New clean interface for non-movement input (mouse, paint, etc.)
    void process_non_movement_input(double delta_time, Engine& engine, class PhysicsSystem& physics_system);
    
private:
    // EDUCATIONAL NOTE: Singleton Pattern
    //
    // Since GLFW callbacks must be static functions, we need a way
    // to access the InputSystem instance from static context.
    // The singleton pattern provides this access.
    
    // Instance methods called by static callbacks
    void handle_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    void handle_cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    void handle_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    
    // 3D Cube intersection helper - tests if mouse ray intersects particle's 3D cube
    bool ray_intersects_cube(const Particle& particle, float ray_world_x, float ray_world_y,
                            Engine& engine, float& distance) const;

    // Input isolation (Phase 3)
    bool is_system_key(int key) const;

    // Input state
    InputState input_state;
    
    // DPI scale factor for Retina displays
    float dpi_scale_x_ = 1.0f;
    float dpi_scale_y_ = 1.0f;
    
    // Singleton instance (for GLFW callbacks)
    static InputSystem* instance;

    // Engine pointer for accessing other systems (like KeyMapper)
    class Engine* engine_ = nullptr;
};

// Global input system instance (temporary until we have Engine class)

#endif // INPUT_SYSTEM_H