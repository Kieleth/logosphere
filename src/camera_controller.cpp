#include "camera_controller.h"
#include "core/camera_system.h"
#include "core/input_system.h"
#include "core/particle_system.h"
#include <iostream>
#include <cmath>

CameraController::CameraController() {
    // Default state set in header
}

void CameraController::initialize(CameraSystem* camera, InputSystem* input, GLFWwindow* window, ParticleSystem* particles) {
    camera_system_ = camera;
    input_system_ = input;
    window_ = window;
    particle_system_ = particles;
}

void CameraController::update(float delta_time) {
    if (!camera_system_ || !input_system_ || !window_) {
        return;  // Not initialized
    }

    // Update based on current mode FIRST (sets camera position)
    switch (current_mode_) {
        case CameraMode::Free:
            update_free_camera(delta_time);
            break;
        case CameraMode::Follow:
            update_follow_camera(delta_time);
            break;
        case CameraMode::Fixed:
            // Fixed camera doesn't need updates
            break;
    }

    // Zoom runs LAST so it can override camera position for centering
    // This ensures zooming keeps the followed entity centered on screen
    handle_zoom(delta_time);
    handle_orbit(delta_time);
}

void CameraController::set_mode(CameraMode mode) {
    // Clean up previous mode
    if (current_mode_ == CameraMode::Free && mode != CameraMode::Free) {
        // Release mouse capture
        mouse_captured_ = false;
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    
    current_mode_ = mode;
    
    // Initialize new mode
    if (mode == CameraMode::Free) {
        // Free camera mode activated
    }
}

void CameraController::toggle_free_camera() {
    if (current_mode_ == CameraMode::Free) {
        set_mode(CameraMode::Fixed);
    } else {
        set_mode(CameraMode::Free);
    }
}

void CameraController::update_free_camera(float delta_time) {
    if (!window_) return;
    
    // Handle keyboard movement
    handle_free_camera_movement(delta_time);
}

void CameraController::handle_free_camera_movement(float delta_time) {
    if (current_mode_ != CameraMode::Free) return;
    
    float distance = move_speed_ * delta_time;
    
    // Get camera orientation vectors
    float forward_x, forward_y, forward_z;
    float right_x, right_y, right_z;
    float up_x, up_y, up_z;
    
    camera_system_->get_forward_vector(forward_x, forward_y, forward_z);
    camera_system_->get_right_vector(right_x, right_y, right_z);
    camera_system_->get_up_vector(up_x, up_y, up_z);
    
    // Get current position
    float pos_x, pos_y, pos_z;
    camera_system_->get_position(pos_x, pos_y, pos_z);
    
    // Check keys and accumulate movement
    float move_x = 0, move_y = 0, move_z = 0;
    
    // Forward/backward (W/S) - FLIPPED
    if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) {
        move_x -= forward_x * distance;  // W moves backward (opposite of forward vector)
        move_y -= forward_y * distance;
        move_z -= forward_z * distance;
    }
    if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) {
        move_x += forward_x * distance;  // S moves forward (along forward vector)
        move_y += forward_y * distance;
        move_z += forward_z * distance;
    }
    
    // Strafe left/right (A/D)
    if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) {
        move_x -= right_x * distance;
        move_y -= right_y * distance;
        move_z -= right_z * distance;
    }
    if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) {
        move_x += right_x * distance;
        move_y += right_y * distance;
        move_z += right_z * distance;
    }
    
    // Up/down (Q/E)
    if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS) {
        move_z -= distance;  // Move down in world space
    }
    if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS) {
        move_z += distance;  // Move up in world space
    }
    
    // Apply movement
    if (move_x != 0 || move_y != 0 || move_z != 0) {
        camera_system_->set_position(pos_x + move_x, pos_y + move_y, pos_z + move_z);
    }
}

void CameraController::on_mouse_move(double x, double y) {
    if (current_mode_ != CameraMode::Free || !mouse_captured_) {
        last_mouse_x_ = x;
        last_mouse_y_ = y;
        return;
    }
    
    // Calculate mouse delta
    double dx = x - last_mouse_x_;
    double dy = y - last_mouse_y_;
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    
    // Apply rotation
    handle_free_camera_rotation(dx, dy);
}

void CameraController::on_mouse_button(int button, int action, int mods) {
    if (current_mode_ != CameraMode::Free) return;
    
    // Left mouse button controls camera rotation
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouse_captured_ = true;
            // Get current mouse position as starting point
            double x, y;
            glfwGetCursorPos(window_, &x, &y);
            last_mouse_x_ = x;
            last_mouse_y_ = y;
        } else if (action == GLFW_RELEASE) {
            mouse_captured_ = false;
        }
    }
}

void CameraController::handle_free_camera_rotation(double dx, double dy) {
    // Convert pixel movement to rotation angles
    float yaw_delta = -dx * rotation_sensitivity_;  // Negative for intuitive movement
    float pitch_delta = -dy * rotation_sensitivity_;  // Negative for intuitive movement


    // Apply rotations
    camera_system_->rotate_yaw(yaw_delta);
    camera_system_->rotate_pitch(pitch_delta);
}

// =============================================================================
// ZOOM DESIGN: Separation of Concerns
// =============================================================================
//
// PROBLEM (before fix):
//   Zoom was trying to do two things at once:
//   1. Change magnification (pixels_per_unit)
//   2. Recenter camera on target entity
//
//   This caused "jumps" because:
//   - Camera follow system positions camera relative to target with offset
//   - Zoom centering was snapping camera directly to target position
//   - Two systems fighting = jerky, unpredictable behavior
//
// SOLUTION (current):
//   Single Responsibility - zoom ONLY changes magnification.
//
//   The camera follow system (in Engine::update) already keeps the target
//   centered on screen. By letting it continue running during zoom, the
//   target naturally stays centered as magnification changes.
//
// WHY THIS WORKS:
//   - Zoom = how big things appear (pixels_per_unit)
//   - Position = where camera is in world space (handled by follow system)
//   - These are orthogonal concerns - separating them eliminates conflicts
//
// RESULT:
//   - No jumps when starting to zoom
//   - Target stays centered throughout zoom
//   - Simpler code (removed ~20 lines of centering logic)
//   - Removed kg_module dependency from CameraController
// =============================================================================
void CameraController::handle_zoom(float delta_time) {
    if (!window_) return;

    float zoom_delta = zoom_speed_ * delta_time;

    // Check zoom keys (semicolon = zoom in, apostrophe = zoom out)
    bool zoom_in = glfwGetKey(window_, GLFW_KEY_SEMICOLON) == GLFW_PRESS;
    bool zoom_out = glfwGetKey(window_, GLFW_KEY_APOSTROPHE) == GLFW_PRESS;

    // Track zoom state (Engine may query this)
    was_zooming_ = (zoom_in || zoom_out);

    // Zoom only adjusts magnification - camera follow handles positioning
    if (zoom_in) {
        camera_system_->adjust_zoom(zoom_delta);
    } else if (zoom_out) {
        camera_system_->adjust_zoom(-zoom_delta);
    }
}

void CameraController::handle_orbit(float delta_time) {
    if (!window_) return;

    // Arrow keys: left/right orbit the view azimuth, up/down zoom.
    // Same polling pattern as handle_zoom; the mouse stays free to
    // steer the avatar's gaze.
    bool orbit_cw = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS;
    bool orbit_ccw = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS;
    bool zoom_in = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS;
    bool zoom_out = glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS;

    if (orbit_cw != orbit_ccw) {
        float az = camera_system_->get_view_azimuth();
        float delta = orbit_speed_ * delta_time;
        camera_system_->set_view_azimuth(az + (orbit_cw ? delta : -delta));
    }
    if (zoom_in != zoom_out) {
        camera_system_->adjust_zoom(zoom_in ? zoom_speed_ * delta_time
                                            : -zoom_speed_ * delta_time);
    }
}

void CameraController::follow_particle(int particle_id, ParticleSystem* particle_system) {
    followed_particle_id_ = particle_id;
    particle_system_ = particle_system;
    set_mode(CameraMode::Follow);
    std::cout << "[CAMERA] Following particle ID " << particle_id << std::endl;
}

void CameraController::stop_following() {
    followed_particle_id_ = -1;
    particle_system_ = nullptr;
    set_mode(CameraMode::Fixed);
    std::cout << "[CAMERA] Stopped following" << std::endl;
}

void CameraController::update_follow_camera(float delta_time) {
    (void)delta_time;  // Unused for now

    if (!camera_system_ || !particle_system_ || followed_particle_id_ < 0) {
        return;  // Invalid state
    }

    // Lock particles for reading
    auto particles_view = particle_system_->lock_particles_for_read();
    const auto& particles = particles_view.get();

    // Find the particle we're following
    const Particle* target = nullptr;
    for (const auto& p : particles) {
        if (p.particle_id == static_cast<uint32_t>(followed_particle_id_)) {
            target = &p;
            break;
        }
    }

    if (!target) {
        std::cout << "[CAMERA] Lost particle ID " << followed_particle_id_ << ", stopping follow" << std::endl;
        stop_following();
        return;
    }

    // DEBUG: Print every 60 frames (roughly once per second at 60 FPS)
    static int debug_frame_counter = 0;
    bool print_debug = (debug_frame_counter++ % 60 == 0);

    if (print_debug) {
        std::cout << "\n[FOLLOW UPDATE]" << std::endl;
        std::cout << "  Target: (" << target->x << ", " << target->y << ", " << target->z << ")" << std::endl;
    }

    // Position camera behind and above the particle
    float cam_x = target->x - follow_distance_;
    float cam_y = target->y - follow_distance_;
    float cam_z = target->z + follow_height_;

    if (print_debug) {
        std::cout << "  Camera (offset): (" << cam_x << ", " << cam_y << ", " << cam_z << ")" << std::endl;
        std::cout << "  Follow offset: -" << follow_distance_ << "m XY, +" << follow_height_ << "m Z" << std::endl;
    }

    camera_system_->set_position(cam_x, cam_y, cam_z);

    // Point camera at the particle
    camera_system_->look_at(target->x, target->y, target->z);
}