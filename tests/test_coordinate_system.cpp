// Test to verify our coordinate system is consistent
// X = East-West (positive X = East)
// Y = North-South (positive Y = North) 
// Z = Up-Down (positive Z = Up)

#include <iostream>
#include "../src/test_context.h"
#include "../src/core/camera_system.h"
#include "../src/core/particle_system.h"

bool test_coordinate_system(TestContext& ctx) {
    std::cout << "\n=== Testing Coordinate System Consistency ===" << std::endl;
    std::cout << "Expected coordinate system:" << std::endl;
    std::cout << "  X-axis: West(-) to East(+)" << std::endl;
    std::cout << "  Y-axis: South(-) to North(+)" << std::endl;
    std::cout << "  Z-axis: Down(-) to Up(+)" << std::endl;
    
    auto& particle_system = ctx.particle_system;
    auto& camera_system = ctx.camera_system;
    auto& render_system = ctx.render_system;
    
    // Position camera looking at origin from southeast
    // Camera at (5, -5, 5) looking at (0, 0, 0)
    ctx.set_camera_position(5.0f, -5.0f, 5.0f);
    ctx.set_camera_look_at(0.0f, 0.0f, 0.0f);
    
    std::cout << "\nCamera positioned at (5, -5, 5) - SOUTHEAST and UP" << std::endl;
    std::cout << "Looking at origin (0, 0, 0)" << std::endl;
    
    // Create three test particles at key positions
    // Each particle is a different color to identify them
    
    // Particle 1: EAST of origin (red)
    int east_idx = ctx.add_cube_particle(
        3.0f, 0.0f, 0.0f,  // 3 meters EAST
        1.0f,              // 1x1x1 meter cube
        1.0f, 0.0f, 0.0f   // RED
    );
    std::cout << "\nParticle " << east_idx << " (RED): at (3, 0, 0) - 3m EAST of origin" << std::endl;
    
    // Particle 2: NORTH of origin (green)  
    int north_idx = ctx.add_cube_particle(
        0.0f, 3.0f, 0.0f,  // 3 meters NORTH
        1.0f,              // 1x1x1 meter cube
        0.0f, 1.0f, 0.0f   // GREEN
    );
    std::cout << "Particle " << north_idx << " (GREEN): at (0, 3, 0) - 3m NORTH of origin" << std::endl;
    
    // Particle 3: UP from origin (blue)
    int up_idx = ctx.add_cube_particle(
        0.0f, 0.0f, 3.0f,  // 3 meters UP
        1.0f,              // 1x1x1 meter cube
        0.0f, 0.0f, 1.0f   // BLUE
    );
    std::cout << "Particle " << up_idx << " (BLUE): at (0, 0, 3) - 3m UP from origin" << std::endl;
    
    // Also add origin marker (white, smaller)
    int origin_idx = ctx.add_cube_particle(
        0.0f, 0.0f, 0.0f,  // At origin
        0.5f,              // 0.5x0.5x0.5 meter cube
        1.0f, 1.0f, 1.0f   // WHITE
    );
    std::cout << "Particle " << origin_idx << " (WHITE): at (0, 0, 0) - ORIGIN marker" << std::endl;
    
    // Now test by projecting particles directly through camera
    std::cout << "\n=== Testing coordinate projections ===" << std::endl;

    // Get particles and project them
    auto particles_view = ctx.particle_system.lock_particles_for_read();

    // Test projection of each particle
    for (size_t i = 0; i < particles_view.size(); i++) {
        const Particle& p = particles_view[i];
        
        // Project using camera
        int screen_x, screen_y;
        ctx.camera_system.world_to_screen(p.x, p.y, p.z, screen_x, screen_y);
        bool on_screen = (screen_x >= 0 && screen_x < 800 && screen_y >= 0 && screen_y < 600);
        
        std::cout << "Particle " << i << " at (" << p.x << ", " << p.y << ", " << p.z << ")";
        if (on_screen) {
            std::cout << " -> screen (" << screen_x << ", " << screen_y << ")" << std::endl;
        } else {
            std::cout << " -> OFF SCREEN" << std::endl;
        }
    }
    
    // Verify expected relationships by checking projection positions
    std::cout << "\n=== Verifying coordinate relationships ===" << std::endl;
    
    // Project our key particles
    int origin_sx, origin_sy, east_sx, east_sy, north_sx, north_sy, up_sx, up_sy;
    ctx.camera_system.world_to_screen(0, 0, 0, origin_sx, origin_sy);
    ctx.camera_system.world_to_screen(3, 0, 0, east_sx, east_sy);
    ctx.camera_system.world_to_screen(0, 3, 0, north_sx, north_sy); 
    ctx.camera_system.world_to_screen(0, 0, 3, up_sx, up_sy);
    
    // From SOUTHEAST camera view:
    // - EAST particle should be to the RIGHT of origin
    // - NORTH particle should be to the LEFT of origin  
    // - UP particle should be ABOVE origin (lower Y value)
    
    bool east_correct = (east_sx > origin_sx);
    bool north_correct = (north_sx < origin_sx);
    bool up_correct = (up_sy < origin_sy);  // Lower Y = higher on screen
    
    std::cout << "Origin projects to: (" << origin_sx << ", " << origin_sy << ")" << std::endl;
    std::cout << "EAST (3,0,0) projects to: (" << east_sx << ", " << east_sy << ")" << std::endl;
    std::cout << "  -> " << (east_correct ? "✓ CORRECT" : "✗ WRONG") << " (should be RIGHT of origin)" << std::endl;
    
    std::cout << "NORTH (0,3,0) projects to: (" << north_sx << ", " << north_sy << ")" << std::endl;
    std::cout << "  -> " << (north_correct ? "✓ CORRECT" : "✗ WRONG") << " (should be LEFT of origin)" << std::endl;
    
    std::cout << "UP (0,0,3) projects to: (" << up_sx << ", " << up_sy << ")" << std::endl;
    std::cout << "  -> " << (up_correct ? "✓ CORRECT" : "✗ WRONG") << " (should be ABOVE origin)" << std::endl;
    
    bool all_correct = east_correct && north_correct && up_correct;
    
    if (!all_correct) {
        std::cout << "\nFAIL: Coordinate system appears inconsistent!" << std::endl;
        
        // Let's debug what might be wrong
        std::cout << "\nDebug analysis:" << std::endl;
        if (!east_correct) {
            std::cout << "- X-axis might be inverted (East should be positive X)" << std::endl;
        }
        if (!north_correct) {
            std::cout << "- Y-axis might be inverted (North should be positive Y)" << std::endl;
        }
        if (!up_correct) {
            std::cout << "- Z-axis or screen Y might be inverted" << std::endl;
        }
        
        return false;
    }
    
    std::cout << "\n✓ SUCCESS: Coordinate system is consistent!" << std::endl;
    std::cout << "  X-axis points EAST (positive right)" << std::endl;
    std::cout << "  Y-axis points NORTH (positive forward)" << std::endl;
    std::cout << "  Z-axis points UP (positive up)" << std::endl;
    
    return true;
}