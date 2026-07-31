#include "test_context.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/light_system.h"
#include "core/camera_system.h"
#include "vision_system.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"  // For entity creation
#include <algorithm>  // For std::max

TestContext::TestContext(Engine& engine)
    : particle_system(engine.get_particle_system()),
      light_system(engine.get_light_system()),
      render_system(engine),
      camera_system(engine.get_camera_system()),
      vision_system(engine.get_vision_system()),
      engine_(engine),
      kg_(engine.get_kg()) {
    // Initialize clean state for testing
    clear_state();
}

void TestContext::clear_state() {
    // Reset particle system to clean state
    particle_system.clear_particles();
    
    // Note: Other systems don't need explicit clearing as they react to particle changes
    // The light system will recalculate when particles change
    // The render system will render whatever particles exist
}

void TestContext::render() {
    // In Interactive mode (visual tests), the test harness handles rendering.
    // Skip here to avoid double-render which causes GPU resource conflicts.
    if (engine_.get_mode() == EngineMode::Interactive) {
        return;
    }
    // In Headless mode, tests must call render() to see results
    engine_.render();
}

void TestContext::update_lighting() {
    // In Interactive mode, the engine's update loop handles BVH and lighting.
    // Skip here to avoid conflicts with GPU resources.
    if (engine_.get_mode() == EngineMode::Interactive) {
        return;
    }
    // CRITICAL: Update BVH before lighting calculations (shadows need this!)
    particle_system.update_bvh();

    // Update lighting calculations based on current particles
    light_system.update_lighting(particle_system);
}

void TestContext::get_render_size(int& width, int& height) {
    // Get current render dimensions from the resolution manager.
    auto& res = engine_.get_resolution_manager();
    width = res.get_window_width();
    height = res.get_window_height();
}

int TestContext::add_light_particle(float x, float y, float z, float size,
                                   float r, float g, float b,
                                   float emission_strength, float emission_radius) {
    // Create entity via KG to get valid entity_id
    kg::EntityID entity_id = kg_.createEntity("LightSource");

    Particle light = {};
    light.x = x;
    light.y = y;
    light.z = z;
    // Make lights more visible - use minimum size of 0.5
    light.size = std::max(size, 0.5f);
    // Use the specified color for the light
    light.r = r;
    light.g = g;
    light.b = b;
    light.a = 1.0f;
    light.is_light_source = true;
    light.emission_strength = emission_strength;
    light.emission_radius = emission_radius;
    light.entity_id = entity_id;  // Required: valid entity_id
    return particle_system.add_particle(light);  // Return the particle ID
}

int TestContext::add_cube_particle(float x, float y, float z, float size,
                                  float r, float g, float b) {
    // Create entity via KG to get valid entity_id
    kg::EntityID entity_id = kg_.createEntity("Rock");

    Particle cube = {};  // Zero-initialize all fields
    cube.x = x;
    cube.y = y;
    cube.z = z;
    cube.size = size;
    cube.r = r;  // Already in 0-1 range
    cube.g = g;
    cube.b = b;
    cube.a = 1.0f;  // Fully opaque
    cube.is_light_source = false;
    cube.entity_id = entity_id;  // Required: valid entity_id
    // Mass auto-calculates from volume and material_density in add_particle()
    cube.SetMaterial(Materials::Type::FLESH);  // Default material for test cubes
    return particle_system.add_particle(cube);  // Return the particle ID
}

int TestContext::add_test_particle(const Particle& p) {
    return particle_system.add_particle(p);
}

int TestContext::find_surface_by_normal(int particle_idx, float nx, float ny, float nz) {
    return particle_system.find_surface_by_normal(particle_idx, nx, ny, nz);
}

void TestContext::set_camera_position(float x, float y, float z) {
    camera_system.set_position(x, y, z);
}

void TestContext::set_camera_look_at(float x, float y, float z) {
    camera_system.look_at(x, y, z);
}

ProjectionSystem* TestContext::get_current_projection() const {
    return engine_.get_current_projection();
}

void TestContext::clear_particles() {
    particle_system.clear_particles();
}

int TestContext::get_particle_count() const {
    return particle_system.count();
}

void TestContext::get_particle_info(int idx, float& x, float& y, float& z,
                                   float& size, float& r, float& g, float& b) const {
    // THREAD SAFETY: Use ReadView for automatic lock management
    auto particles_view = particle_system.lock_particles_for_read();
    if (idx >= 0 && idx < static_cast<int>(particles_view.size())) {
        const auto& p = particles_view[idx];
        x = p.x;
        y = p.y;
        z = p.z;
        size = p.size;
        r = p.r;
        g = p.g;
        b = p.b;
    }
}

TestContext::LightColor TestContext::get_surface_light_color(int particle_idx, int surface_idx,
                                                            float u, float v) {
    LightColor result;

    // THREAD SAFETY: Use ReadView for automatic lock management
    auto particles_view = particle_system.lock_particles_for_read();
    if (particle_idx >= 0 && particle_idx < static_cast<int>(particles_view.size())) {
        const auto& p = particles_view[particle_idx];
        
        // Get lit color from light system
        // Pass particle colors as uint8_t (0-255) as expected by light system
        auto color = light_system.get_surface_color_at(
            particle_idx, surface_idx, u, v, 
            static_cast<uint8_t>(p.r * 255),
            static_cast<uint8_t>(p.g * 255),
            static_cast<uint8_t>(p.b * 255));
        
        // Fixed: RGB values now returned correctly
        
        result.r = color.r;
        result.g = color.g;
        result.b = color.b;
        result.intensity = color.intensity;
    } else {
        result.r = result.g = result.b = 0;
        result.intensity = 0.0f;
    }
    
    return result;
}

void TestContext::get_pixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, int& object_id) const {
    // Access the render buffer to get pixel data
    auto pixel = engine_.get_render_buffer().get_pixel(x, y);
    r = pixel.r;
    g = pixel.g;
    b = pixel.b;
    a = pixel.a;
    object_id = pixel.object_id;
}

void TestContext::analyze_shadows() {
    // CRITICAL: Wait for GPU rendering to complete before reading framebuffer
    // Without this, we may read stale/background pixels
    render_system.get_render_pipeline().wait_for_gpu_completion();

    // Get framebuffer dimensions and data
    // NOTE: Use get_render_buffer(), not get_framebuffer()!
    // - render_buffer_ is where GPU writes (the actual rendered scene)
    // - framebuffer_ is final display buffer (different resolution, not used here)
    int width = render_system.get_resolution_manager().get_render_width();
    int height = render_system.get_resolution_manager().get_render_height();

    // Get raw pixel data from RENDER buffer (BGRA format)
    const uint8_t* native_data = render_system.get_render_buffer().get_native_data();
    if (!native_data) {
        std::cout << "[SHADOW_PROBE] ERROR: No native render buffer data available" << std::endl;
        return;
    }

    // Cast to uint32_t for BGRA pixels
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(native_data);

    // Run screen-space analysis
    shadow_probe_.analyze_screen(pixels, width, height);

    // Log results
    shadow_probe_.log_results();
}