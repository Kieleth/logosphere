#ifndef PROJECTION_SYSTEM_H
#define PROJECTION_SYSTEM_H

#include <cmath>
#include <memory>

// Base class for all projection systems
// Each projection type (Isometric, Perspective, etc.) is a separate class
// This follows the Open-Closed Principle: open for extension, closed for modification
class ProjectionSystem {
public:
    virtual ~ProjectionSystem() = default;
    
    // Core projection method - transforms 3D world point to 2D screen point
    // Each derived class implements its own mathematical transformation
    virtual void project(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z,
                        int viewport_width, int viewport_height,
                        float pixels_per_unit,
                        int& screen_x, int& screen_y) const = 0;
    
    // Get the name of this projection for debugging/UI
    virtual const char* get_name() const = 0;
    
    // Some projections may need configuration
    virtual void configure(float param1 = 0, float param2 = 0, float param3 = 0) {}
    
    // Validate that a projected quadrilateral has the expected geometric properties
    // Returns true if the quad matches this projection's expected characteristics
    // quad_x, quad_y are the 4 corners in screen space
    virtual bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const = 0;
    
    // Get expected properties for this projection (for debugging/testing)
    struct ProjectionProperties {
        bool preserves_parallel_lines;     // Do parallel lines stay parallel?
        bool preserves_angles;              // Are angles preserved?
        bool uniform_scaling;               // Is scaling uniform in all directions?
        bool depth_affects_size;            // Do objects get smaller with distance?
        const char* expected_quad_shape;   // Description of expected quad shape
    };
    virtual ProjectionProperties get_properties() const = 0;

    // Query if this is a parallel projection (for backface culling)
    // Parallel projections: use fixed viewing direction (forward vector)
    // Point projections: use camera-to-surface vector
    virtual bool is_parallel_projection() const = 0;

    // Depth metric used by the rasterizer's depth buffer.
    //
    // Each projection defines what "further from the viewer" means.
    // - Perspective: true 3D Euclidean distance from the camera point.
    // - Parallel projections (iso, bird's-eye, cabinet): signed distance
    //   along the projection's fixed view direction. Depth ordering
    //   must NOT depend on camera X/Y in a parallel projection; only
    //   on where the point sits along the view axis.
    //
    // Contract: smaller return value == closer to the viewer. Absolute
    // scale is not required; only ordering matters for the depth buffer.
    // Implementations must be linear in (world - camera) so that
    // barycentric interpolation of corner depths is exact.
    //
    // See tests/test_iso_depth_ordering.cpp for the invariants this
    // method must satisfy.
    virtual float compute_depth(float world_x, float world_y, float world_z,
                                float camera_x, float camera_y, float camera_z) const = 0;

    // Default camera position for this projection type
    // Each projection defines its natural viewing position
    struct CameraDefaults {
        float position_x, position_y, position_z;
        float look_at_x, look_at_y, look_at_z;
    };
    virtual CameraDefaults get_default_camera() const = 0;

    // View azimuth: rotation of the view basis around world +Z,
    // clockwise-positive seen from above (the engine's compass
    // convention, same sign as Particle::rotation_z). 0 keeps each
    // projection's classic orientation. Parallel projections that
    // support orbiting override; the default is a fixed view.
    virtual void set_view_azimuth(float radians) { (void)radians; }
    virtual float get_view_azimuth() const { return 0.0f; }
};

// Classic 2.5D isometric projection
// Rotates 45° around Z axis, then tilts 30° for the classic iso look
class IsometricProjection : public ProjectionSystem {
public:
    void project(float world_x, float world_y, float world_z,
                float camera_x, float camera_y, float camera_z,
                int viewport_width, int viewport_height,
                float pixels_per_unit,
                int& screen_x, int& screen_y) const override;
    
    const char* get_name() const override { return "Isometric"; }
    
    void set_height_scale(float scale) { height_scale_ = scale; }
    
    bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const override;
    ProjectionProperties get_properties() const override {
        return {
            true,   // preserves_parallel_lines (key feature of isometric)
            false,  // preserves_angles (angles are distorted to 120°)
            true,   // uniform_scaling (no perspective distortion)
            false,  // depth_affects_size (no perspective)
            "Parallelogram with specific angle ratios"
        };
    }

    bool is_parallel_projection() const override { return true; }  // Isometric uses parallel rays

    // Azimuth orbit: pre-rotates view-space XY around world +Z before
    // the fixed 45° projection, so azimuth = 0 reproduces the classic
    // view exactly and any azimuth is the same scene seen from a
    // rotated compass bearing. CW-positive from above (compass
    // convention). project() and compute_depth() share this rotation;
    // consumers doing their own iso math (inverse transform, culling
    // probes, compass widget) must apply the same rotation — they
    // read it from here via the camera.
    void set_view_azimuth(float radians) override {
        azimuth_ = radians;
        azimuth_cos_ = std::cos(radians);
        azimuth_sin_ = std::sin(radians);
    }
    float get_view_azimuth() const override { return azimuth_; }

    // Rotate a world/view XY delta into the azimuth frame (the frame
    // the fixed 45° formulas run in). CW-positive around +Z: at
    // azimuth a, world east rotates toward south on screen.
    inline void rotate_into_view(float x, float y,
                                 float& out_x, float& out_y) const {
        out_x = x * azimuth_cos_ + y * azimuth_sin_;
        out_y = -x * azimuth_sin_ + y * azimuth_cos_;
    }
    // Inverse: azimuth-frame XY back to world frame.
    inline void rotate_out_of_view(float x, float y,
                                   float& out_x, float& out_y) const {
        out_x = x * azimuth_cos_ - y * azimuth_sin_;
        out_y = x * azimuth_sin_ + y * azimuth_cos_;
    }

    // Orthographic depth along the iso view direction.
    // In the azimuth frame, iso_x is driven by (1,-1,0), iso_y by
    // (0.5, 0.5, height_scale). Their cross product — the view axis —
    // is proportional to (height_scale, height_scale, -1). Depth along
    // the into-scene direction is then (height_scale*(dx'+dy') - dz),
    // unnormalized, with (dx', dy') the azimuth-rotated delta. Higher
    // world Z (dz > 0 when camera is below) yields smaller depth →
    // drawn in front. Independent of camera X/Y, which is the
    // parallel-projection invariant.
    float compute_depth(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z) const override {
        float dx, dy;
        rotate_into_view(world_x - camera_x, world_y - camera_y, dx, dy);
        const float dz = world_z - camera_z;
        return height_scale_ * (dx + dy) - dz;
    }

    CameraDefaults get_default_camera() const override {
        // Classic isometric view: SW above origin looking at world center
        return {-10.0f, -10.0f, 20.0f,  // Camera position (SW, above)
                0.0f, 0.0f, 0.0f};       // Look at origin
    }

private:
    float height_scale_ = 1.0f;  // How much Z affects the projection
    float azimuth_ = 0.0f;       // View orbit angle, CW from classic view
    float azimuth_cos_ = 1.0f;
    float azimuth_sin_ = 0.0f;
};

// Isometric with depth-based scaling
// Objects further away appear smaller
class IsometricDepthProjection : public ProjectionSystem {
public:
    void project(float world_x, float world_y, float world_z,
                float camera_x, float camera_y, float camera_z,
                int viewport_width, int viewport_height,
                float pixels_per_unit,
                int& screen_x, int& screen_y) const override;
    
    const char* get_name() const override { return "Isometric with Depth"; }
    
    void configure(float depth_factor = 30.0f, float min_scale = 0.5f, float max_scale = 1.5f) override;
    
    bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const override;
    ProjectionProperties get_properties() const override {
        return {
            false,  // preserves_parallel_lines (depth scaling breaks this)
            false,  // preserves_angles
            false,  // uniform_scaling (depth-based scaling)
            true,   // depth_affects_size (main feature)
            "Trapezoid due to depth scaling"
        };
    }

    bool is_parallel_projection() const override { return true; }  // Still uses parallel rays

    // Same orthographic view axis as the base isometric. The depth
    // scaling this projection applies is a SCREEN-space effect (makes
    // distant objects smaller); the depth-BUFFER metric is still the
    // parallel view-direction distance.
    float compute_depth(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z) const override {
        const float dx = world_x - camera_x;
        const float dy = world_y - camera_y;
        const float dz = world_z - camera_z;
        return (dx + dy) - dz;
    }

    CameraDefaults get_default_camera() const override {
        // Same as standard isometric
        return {-10.0f, -10.0f, 20.0f,  // Camera position (SW, above)
                0.0f, 0.0f, 0.0f};       // Look at origin
    }

private:
    float depth_factor_ = 30.0f;
    float min_scale_ = 0.5f;
    float max_scale_ = 1.5f;
};

// True 3D perspective projection
// Uses focal length and field of view for realistic 3D
class PerspectiveProjection : public ProjectionSystem {
public:
    void project(float world_x, float world_y, float world_z,
                float camera_x, float camera_y, float camera_z,
                int viewport_width, int viewport_height,
                float pixels_per_unit,
                int& screen_x, int& screen_y) const override;
    
    const char* get_name() const override { return "Perspective"; }
    
    void configure(float fov_degrees = 60.0f, float near_plane = 0.1f, float far_plane = 1000.0f) override;
    
    bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const override;
    ProjectionProperties get_properties() const override {
        return {
            false,  // preserves_parallel_lines (converge at vanishing point)
            false,  // preserves_angles
            false,  // uniform_scaling
            true,   // depth_affects_size (perspective foreshortening)
            "Trapezoid with vanishing point convergence"
        };
    }

    bool is_parallel_projection() const override { return false; }  // Perspective uses point projection

    // True 3D Euclidean distance from the camera point. This is the
    // natural depth metric for a point (perspective) projection — the
    // z-buffer test wants "which point is closer to the eye", and for
    // a finite camera that's just radial distance.
    float compute_depth(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z) const override {
        const float dx = world_x - camera_x;
        const float dy = world_y - camera_y;
        const float dz = world_z - camera_z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    CameraDefaults get_default_camera() const override {
        // Perspective: camera behind origin looking forward (north)
        return {0.0f, -20.0f, 10.0f,  // Camera position (behind, elevated)
                0.0f, 0.0f, 0.0f};     // Look at origin
    }

private:
    float fov_degrees_ = 60.0f;
    float near_plane_ = 0.1f;
    float far_plane_ = 1000.0f;
};

// Bird's eye view projection - looking straight down from above
// Simple orthographic projection from top
class BirdsEyeProjection : public ProjectionSystem {
public:
    void project(float world_x, float world_y, float world_z,
                float camera_x, float camera_y, float camera_z,
                int viewport_width, int viewport_height,
                float pixels_per_unit,
                int& screen_x, int& screen_y) const override;
    
    const char* get_name() const override { return "Bird's Eye"; }
    
    bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const override;
    ProjectionProperties get_properties() const override {
        return {
            true,   // preserves_parallel_lines
            true,   // preserves_angles (orthographic)
            true,   // uniform_scaling
            false,  // depth_affects_size (orthographic)
            "Square or rectangle (undistorted top view)"
        };
    }

    bool is_parallel_projection() const override { return true; }  // Orthographic parallel projection

    // Bird's eye looks straight down along -Z. Higher Z is closer.
    // No XY dependence — classic orthographic top-down depth.
    float compute_depth(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z) const override {
        (void)world_x; (void)world_y; (void)camera_x; (void)camera_y;
        return -(world_z - camera_z);
    }

    CameraDefaults get_default_camera() const override {
        // Bird's eye: directly above origin looking straight down
        return {0.0f, 0.0f, 20.0f,  // Camera position (directly above)
                0.0f, 0.0f, 0.0f};   // Look at origin
    }
};

// Cabinet oblique projection
// Front face is undistorted, depth is shown at an angle
class CabinetProjection : public ProjectionSystem {
public:
    void project(float world_x, float world_y, float world_z,
                float camera_x, float camera_y, float camera_z,
                int viewport_width, int viewport_height,
                float pixels_per_unit,
                int& screen_x, int& screen_y) const override;
    
    const char* get_name() const override { return "Cabinet"; }
    
    void configure(float angle_degrees = 45.0f, float depth_ratio = 0.5f, float unused = 0) override;
    
    bool validate_projected_quad(const float quad_x[4], const float quad_y[4]) const override;
    ProjectionProperties get_properties() const override {
        return {
            true,   // preserves_parallel_lines (oblique projection property)
            false,  // preserves_angles (oblique distortion)
            false,  // uniform_scaling (depth is scaled differently)
            false,  // depth_affects_size (parallel projection)
            "Parallelogram with oblique distortion"
        };
    }

    bool is_parallel_projection() const override { return true; }  // Oblique parallel projection

    // Cabinet is parallel oblique: the front face is untouched, depth
    // is shown along an angled axis. We use the same view-axis formula
    // as iso (height_scale=1 equivalent) for depth ordering — the
    // oblique skew affects SCREEN placement, not z-buffer comparison.
    float compute_depth(float world_x, float world_y, float world_z,
                        float camera_x, float camera_y, float camera_z) const override {
        const float dx = world_x - camera_x;
        const float dy = world_y - camera_y;
        const float dz = world_z - camera_z;
        return (dx + dy) - dz;
    }

    CameraDefaults get_default_camera() const override {
        // Cabinet oblique: slightly offset for 3/4 view
        return {-5.0f, -10.0f, 15.0f,  // Camera position (oblique angle)
                0.0f, 0.0f, 0.0f};      // Look at origin
    }

private:
    float angle_degrees_ = 45.0f;
    float depth_ratio_ = 0.5f;
};

// Factory to create projection systems
class ProjectionFactory {
public:
    enum class Type {
        Isometric,
        IsometricDepth,
        Perspective,
        Cabinet,
        BirdsEye
    };
    
    static std::unique_ptr<ProjectionSystem> create(Type type);
};

#endif // PROJECTION_SYSTEM_H