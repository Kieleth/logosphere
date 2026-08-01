#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles

// Forward declarations
class CameraSystem;
class InputSystem;
class ParticleSystem;

// =============================================================================
// CameraController - Input handling for camera movement and zoom
// =============================================================================
//
// DESIGN PRINCIPLE: Separation of Concerns
//
// This controller handles:
//   - Free camera movement (WASD+QE, mouse look)
//   - Zoom (semicolon/apostrophe keys)
//
// It does NOT handle:
//   - Camera follow/positioning (that's Engine's responsibility)
//
// ZOOM ARCHITECTURE:
//   Zoom only changes magnification (pixels_per_unit), not camera position.
//   The Engine's camera follow system handles keeping the target centered.
//   This separation eliminates the "jump" bug where zoom and follow fought
//   for camera position control.
//
// See handle_zoom() in camera_controller.cpp for detailed explanation.
// =============================================================================
class CameraController {
public:
    enum class CameraMode {
        Fixed,  // Camera stays at fixed position/orientation
        Free,   // WASD+QE movement, mouse look
        Follow  // Follow a specific particle
    };

    CameraController();
    ~CameraController() = default;

    // Initialize with required systems
    void initialize(CameraSystem* camera, InputSystem* input, GLFWwindow* window, ParticleSystem* particles = nullptr);

    // Update camera based on current mode and input
    void update(float delta_time);

    // Mode control
    void set_mode(CameraMode mode);
    CameraMode get_mode() const { return current_mode_; }
    void toggle_free_camera();

    // Follow mode - embody a particle
    void follow_particle(int particle_id, ParticleSystem* particle_system);
    void stop_following();

    // Check if actively zooming (for external queries)
    bool is_zooming() const { return was_zooming_; }
    
    // Mouse input (called from input callbacks)
    void on_mouse_move(double x, double y);
    void on_mouse_button(int button, int action, int mods);
    
    // Configuration
    void set_move_speed(float speed) { move_speed_ = speed; }
    void set_rotation_sensitivity(float sensitivity) { rotation_sensitivity_ = sensitivity; }
    
private:
    // System references (not owned)
    CameraSystem* camera_system_ = nullptr;
    InputSystem* input_system_ = nullptr;
    GLFWwindow* window_ = nullptr;
    float orbit_speed_ = 1.05f;   // rad/s: a full lap in ~6 s
    float pan_speed_ = 18.0f;     // m/s at the default zoom (SPACE + arrows)
    
    // Current state
    CameraMode current_mode_ = CameraMode::Fixed;
    
    // Free camera state
    bool mouse_captured_ = false;  // Is mouse being used for camera rotation?
    double last_mouse_x_ = 0.0;
    double last_mouse_y_ = 0.0;
    
    // Movement settings
    float move_speed_ = 10.0f;  // meters per second
    float rotation_sensitivity_ = 0.002f;  // radians per pixel
    float zoom_speed_ = 10.0f;  // zoom speed (meters per second)

    // Zoom state
    bool was_zooming_ = false;  // True if zooming last frame

    // Follow mode state
    int followed_particle_id_ = -1;  // ID of particle being followed (-1 = none)
    ParticleSystem* particle_system_ = nullptr;  // Reference to particle system
    float follow_distance_ = 10.0f;  // Distance to maintain from followed particle
    float follow_height_ = 5.0f;     // Height offset above particle

    // Update methods for each mode
    void update_free_camera(float delta_time);
    void update_follow_camera(float delta_time);
    void handle_free_camera_movement(float delta_time);
    void handle_free_camera_rotation(double dx, double dy);
    void handle_zoom(float delta_time);
    void handle_orbit(float delta_time);  // Works in all camera modes
};

#endif // CAMERA_CONTROLLER_H