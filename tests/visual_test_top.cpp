#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "../src/core/light_system.h"
#include <iostream>

bool visual_test_top(TestContext& ctx) {
    std::cout << "\n=== Visual Test: Light Above Cubes ===" << std::endl;

    // Set up camera looking at scene from SW
    ctx.set_camera_position(-10.0f, -10.0f, 20.0f);
    ctx.set_camera_look_at(0.0f, 0.0f, 0.0f);

    // Add light at origin, elevated above the scene
    float light_height = 5.0f;  // Position light 5 units above ground
    int light_id = ctx.add_light_particle(
        0.0f, 0.0f, light_height,  // position at origin, elevated
        0.2f,                      // size (will be increased to 0.5 minimum)
        1.0f, 0.85f, 0.6f,        // warm white color
        10000000.0f,              // intensity (10M)
        50.0f                     // radius
    );

    // Debug: Verify light was added (scoped to release lock before update_lighting)
    {
        auto particles_view = ctx.particle_system.lock_particles_for_read();
        if (light_id >= 0 && light_id < static_cast<int>(particles_view.size())) {
            const auto& light = particles_view[light_id];
            std::cout << "  - Light particle " << light_id << " added: "
                      << "pos(" << light.x << "," << light.y << "," << light.z << ") "
                      << "size=" << light.size << " "
                      << "color(" << light.r << "," << light.g << "," << light.b << ") "
                      << "is_light=" << light.is_light_source << std::endl;
        }
    }  // Lock released here

    // Add small blue cube closer to light (like Eva in triangle test)
    ctx.add_cube_particle(
        0.0f, 1.0f, 0.0f,    // position slightly north on Y axis
        1.0f,                // small size
        0.3f, 0.3f, 0.8f     // blue color
    );

    // Add large gray cube further north (like wall in triangle test)
    ctx.add_cube_particle(
        0.0f, 6.0f, 0.0f,    // position further north on Y axis
        4.0f,                // large size
        0.7f, 0.7f, 0.7f     // gray color
    );

    std::cout << "Scene setup:" << std::endl;
    std::cout << "  - Light at origin (0, 0, " << light_height << ")" << std::endl;
    std::cout << "  - Small blue cube at (0, 1, 0)" << std::endl;
    std::cout << "  - Large gray cube at (0, 6, 0)" << std::endl;
    std::cout << "  - Camera at SW position looking NE" << std::endl;

    // Update lighting
    ctx.update_lighting();

    // Render the scene
    ctx.render();

    std::cout << "✓ Scene setup complete" << std::endl;
    return true;
}
