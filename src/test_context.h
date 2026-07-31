#ifndef TEST_CONTEXT_H
#define TEST_CONTEXT_H

#include <memory>
#include "diagnostics/shadow_probe.h"
#include "core/engine.h"  // render_system field is Engine&; tests use its accessors

// Forward declarations
class Engine;
class ParticleSystem;
class LightSystem;
class CameraSystem;
class VisionSystem;
namespace kg { class KGModule; }

// TestContext provides a controlled interface to engine systems for testing
// This eliminates the need for tests to create their own Engine instances
class TestContext {
public:
    // Core systems access (references to engine's systems)
    ParticleSystem& particle_system;
    LightSystem& light_system;
    Engine& render_system;  // type-alias trick: tests call ctx.render_system.X(),
                            // which now hits Engine accessors.
    CameraSystem& camera_system;
    VisionSystem& vision_system;
    
    // Test utilities
    void clear_state();                    // Reset all systems to clean state
    void render();                         // Trigger engine render pipeline
    void update_lighting();                // Update light calculations
    void get_render_size(int& width, int& height);  // Get current render dimensions
    
    // Particle creation helpers for tests
    int add_light_particle(float x, float y, float z, float size,
                          float r, float g, float b,
                          float emission_strength, float emission_radius);
    int add_cube_particle(float x, float y, float z, float size,
                         float r, float g, float b);

    // Raw particle addition for tests (bypasses entity binding - test use only)
    // Prefer add_light_particle/add_cube_particle for new tests
    int add_test_particle(const struct Particle& p);
    
    // Surface query helpers for tests
    int find_surface_by_normal(int particle_idx, float nx, float ny, float nz);
    float get_surface_camera_angle(int particle_idx, int surface_idx);  // Get angle between surface normal and camera direction (in radians)
    
    // Lighting query helpers for tests
    struct LightColor {
        uint8_t r, g, b;
        float intensity;  // Raw intensity in lux
    };
    LightColor get_surface_light_color(int particle_idx, int surface_idx, 
                                       float u, float v);  // Get lit color at surface point
    
    // Particle access helpers for tests
    void clear_particles();  // Clear all particles
    int get_particle_count() const;  // Get number of particles
    void get_particle_info(int idx, float& x, float& y, float& z, 
                          float& size, float& r, float& g, float& b) const;  // Get particle data
    
    // Camera control helpers for tests
    void set_camera_position(float x, float y, float z);
    void set_camera_look_at(float x, float y, float z);
    
    // Projection system access for tests
    class ProjectionSystem* get_current_projection() const;

    // Framebuffer access for tests
    void get_pixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, int& object_id) const;

    // Engine access for tests that need generators/dynamics
    Engine& get_engine() { return engine_; }

    // KG access for tests that need custom particle creation
    kg::KGModule& get_kg() { return kg_; }

    // Shadow probe for lighting diagnostics
    ShadowProbe& get_shadow_probe() { return shadow_probe_; }

    // Run shadow probe analysis on current framebuffer
    // Call after render() to analyze lighting quality
    void analyze_shadows();

private:
    Engine& engine_;  // Reference to parent engine
    kg::KGModule& kg_;  // Reference to KG module for entity creation
    ShadowProbe shadow_probe_;  // Reusable shadow diagnostic tool

public:
    // Public constructor - simple and direct
    explicit TestContext(Engine& engine);
};

#endif // TEST_CONTEXT_H