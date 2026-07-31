#ifndef CAMERA_SYSTEM_H
#define CAMERA_SYSTEM_H

#include <cmath>
#include <memory>
#include "projection_system.h"

// Forward declarations
struct Particle;
struct Surface;

class CameraSystem {
public:
    
    CameraSystem();
    ~CameraSystem() = default;
    
    // Configuration
    void set_viewport(int width, int height);
    void set_projection_system(std::unique_ptr<ProjectionSystem> projection);
    void set_pixels_per_unit(float pixels);
    float get_pixels_per_unit() const { return pixels_per_unit_; }

    // Zoom control (adjusts pixels_per_unit for zoom effect)
    void adjust_zoom(float delta);  // Positive = zoom in, negative = zoom out
    
    // Position control
    void set_position(float x, float y, float z);
    void get_position(float& x, float& y, float& z) const;

    // Camera follow with deadzone
    void set_camera_deadzone(float size);  // Set deadzone box size (0 = immediate follow)
    void set_camera_follow_offset(float offset_y);  // Offset target position (positive = target appears lower on screen)
    float get_camera_follow_offset() const { return camera_follow_offset_; }  // Get current offset (for zoom centering to use same target)
    void update_follow_target(float target_x, float target_y, float target_z);  // Update camera to follow target with deadzone

    // Get the last follow target position (with offset applied) - used for zoom centering
    bool get_last_follow_target(float& out_x, float& out_y) const {
        if (!has_follow_target_) return false;
        out_x = last_follow_target_x_;
        out_y = last_follow_target_y_;
        return true;
    }

    // Velocity tracking for chunk pre-loading prediction
    // Call update_velocity() each frame before using get_velocity()
    void update_velocity(float dt);
    void get_velocity(float& vx, float& vy) const { vx = velocity_x_; vy = velocity_y_; }
    float get_velocity_magnitude() const;
    
    // Orientation control
    void look_at(float target_x, float target_y, float target_z);
    void set_forward_direction(float x, float y, float z);

    // Get the current look-at target position (used for shadow distance culling)
    void get_look_at_target(float& out_x, float& out_y) const {
        out_x = look_at_target_x_;
        out_y = look_at_target_y_;
    }
    void set_up_direction(float x, float y, float z);
    
    // Rotation
    void rotate_yaw(float angle);    // Horizontal rotation (around up vector)
    void rotate_pitch(float angle);  // Vertical rotation (around right vector)
    void rotate_roll(float angle);   // Barrel roll (around forward vector)
    
    // View calculations for culling
    void get_view_direction_to_point(float world_x, float world_y, float world_z, 
                                   float view_direction[3]) const;
    
    // =========================================================================
    // UNIFIED CULLING SYSTEM
    // =========================================================================
    // Determines if a surface should be rendered based on various culling criteria
    // 
    // Currently implements:
    //   - BACKFACE CULLING: Skip surfaces facing away from camera
    // 
    // Future culling types (TODO):
    //   - FRUSTUM CULLING: Skip surfaces outside camera view volume
    //   - OCCLUSION CULLING: Skip surfaces hidden behind others
    //   - DISTANCE/LOD CULLING: Skip or simplify distant surfaces
    // 
    // Returns: true if surface should be rendered, false if culled
    // =========================================================================
    bool should_render_surface(float surface_x, float surface_y, float surface_z,
                               float normal_x, float normal_y, float normal_z,
                               float width = 1.0f, float height = 1.0f) const;
    
    // Individual culling checks (for testing/debugging)
    bool passes_backface_culling(float surface_x, float surface_y, float surface_z,
                                 float normal_x, float normal_y, float normal_z) const;
    bool passes_frustum_culling(float surface_x, float surface_y, float surface_z,
                                float width, float height) const;  // TODO: Implement
    bool passes_distance_culling(float surface_x, float surface_y, float surface_z) const;  // TODO: Implement
    
    // Access to orientation vectors (for projections)
    void get_forward_vector(float& x, float& y, float& z) const;
    void get_up_vector(float& x, float& y, float& z) const;
    void get_right_vector(float& x, float& y, float& z) const;
    
    // Core 3D to 2D projection using the projection system
    void world_to_screen(float world_x, float world_y, float world_z,
                        int& screen_x, int& screen_y) const;

    // Projection-aware depth for the z-buffer. Forwards to the active
    // ProjectionSystem so that parallel projections (iso, bird's-eye,
    // cabinet) get view-direction depth and perspective gets true 3D
    // distance. Never compute depth as distance_3d() yourself — always
    // call this so the metric tracks whatever projection is active.
    float compute_depth(float world_x, float world_y, float world_z) const;
    
    // Result of projecting a 3D surface to 2D screen
    // =========================================================================
    // OPTIMIZATION: Incremental Edge Functions (Pineda Algorithm)
    // WHY: Replace 12 multiplies per pixel with 3 adds per pixel
    // HOW: Edge functions are linear - moving 1 pixel changes value predictably
    // IMPACT: Expected 2-3x speedup in rasterization (~10ms savings)
    // =========================================================================
    struct EdgeEquation {
        float a, b, c;  // Coefficients: ax + by + c = 0
        float dx_step;  // Value change when moving right (+1 in x)
        float dy_step;  // Value change when moving down (+1 in y)
        
        // Compute coefficients from two points defining the edge
        void setup(float x0, float y0, float x1, float y1) {
            // Edge equation coefficients (perpendicular to edge direction)
            a = y0 - y1;  // dy (but inverted)
            b = x1 - x0;  // -dx (but inverted)
            c = x0 * y1 - x1 * y0;

            // Incremental steps
            dx_step = a;  // Moving right changes value by 'a'
            dy_step = b;  // Moving down changes value by 'b'
        }
        
        // Evaluate edge function at a point
        float evaluate(float x, float y) const {
            return a * x + b * y + c;
        }
    };

    struct ProjectedTriangle {
        int screen_corners[3][2];     // 2D screen positions of 3 vertices
        float world_corners[3][3];    // Corresponding 3D world positions
        int min_x, max_x, min_y, max_y; // Bounding box for iteration
        bool on_screen;               // False if completely off-screen
        float surface_normal[3];      // Normal vector of the surface (for culling)
        
        // Precomputed edge equations for fast pixel testing
        EdgeEquation edges[3];        // Three edges for triangle
        bool edges_computed;          // Whether edge equations are ready
    };
    
    
    // Project a 3D triangle to 2D screen space
    // Takes the 3 world-space vertices of the triangle
    ProjectedTriangle project_triangle(const float world_vertices[3][3]) const;
    
    
    // Map a screen pixel within a projected triangle to barycentric UV coordinates
    // Returns false if the pixel is outside the triangle
    // u,v are barycentric coordinates (u for vertex 1, v for vertex 2)
    bool pixel_to_triangle_uv(int pixel_x, int pixel_y,
                              const ProjectedTriangle& tri,
                              float& out_u, float& out_v) const;
    
    
    // Performance metrics
    struct PixelUVMetrics {
        uint32_t calls_this_frame = 0;
        // No more fast/newton paths - triangles use direct calculation
        
        void reset() {
            calls_this_frame = 0;
        }
    };
    
    PixelUVMetrics get_pixel_uv_metrics() const;
    void reset_frame_metrics();
    
private:
    // Position in 3D space
    float position_x_, position_y_, position_z_;
    
    // Orientation vectors (always normalized)
    float forward_x_, forward_y_, forward_z_;   // Where camera looks
    float up_x_, up_y_, up_z_;                 // Camera's up direction
    float right_x_, right_y_, right_z_;        // Camera's right direction
    
    // Projection configuration
    std::unique_ptr<ProjectionSystem> projection_system_;
    int viewport_width_ = 800;
    int viewport_height_ = 600;
    float pixels_per_unit_ = 20.0f;

    // Camera follow deadzone (0 = immediate follow, >0 = box size before camera moves)
    float camera_deadzone_size_ = 0.0f;

    // Camera follow offset for isometric (applied equally to X and Y for diagonal offset)
    // Positive = camera southwest of target = target appears higher on screen
    float camera_follow_offset_ = 0.0f;

    // Last follow target position (with offset applied) - for zoom centering
    float last_follow_target_x_ = 0.0f;
    float last_follow_target_y_ = 0.0f;
    bool has_follow_target_ = false;

    // Look-at target position (updated by look_at() and update_follow_target())
    // Used for shadow distance culling to center effects on player, not camera offset
    float look_at_target_x_ = 0.0f;
    float look_at_target_y_ = 0.0f;

    // Velocity tracking for chunk pre-loading (Phase 3: Camera Prediction)
    float prev_position_x_ = 0.0f;
    float prev_position_y_ = 0.0f;
    float velocity_x_ = 0.0f;
    float velocity_y_ = 0.0f;
    bool velocity_initialized_ = false;

    // Internal helpers
    void normalize_vector(float& x, float& y, float& z);
    void calculate_right_vector();  // right = forward × up
    void orthonormalize_vectors();  // Ensure vectors are orthogonal and normalized
};

#endif // CAMERA_SYSTEM_H