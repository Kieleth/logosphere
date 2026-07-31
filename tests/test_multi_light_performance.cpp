// Multi-light performance test - simplified version
// Tests the FPS impact of multiple light sources with a dense particle wall

#include "../src/test_context.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>

bool test_multi_light_performance(TestContext& ctx) {
    std::cout << "\n=== Multi-Light Performance Test ===\n";
    std::cout << "Testing FPS impact of multiple light sources\n\n";
    
    // Test configurations
    struct TestConfig {
        int num_lights;
        int wall_size;  // NxN grid
        const char* description;
    };
    
    TestConfig configs[] = {
        {1, 10, "10x10 wall, 1 light"},
        {2, 10, "10x10 wall, 2 lights"},
        {3, 10, "10x10 wall, 3 lights"},
        {1, 15, "15x15 wall, 1 light"},
        {2, 15, "15x15 wall, 2 lights"},
        {3, 15, "15x15 wall, 3 lights"},
    };
    
    // Results header
    std::cout << std::setw(30) << "Configuration" 
              << std::setw(12) << "Particles"
              << std::setw(10) << "Lights"
              << std::setw(25) << "Notes" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    for (const auto& config : configs) {
        // Clear particles for each test
        ctx.clear_particles();
        
        // Create particle wall
        float spacing = 0.8f;
        int total_particles = 0;
        
        for (int row = 0; row < config.wall_size; row++) {
            for (int col = 0; col < config.wall_size; col++) {
                float x = (col - config.wall_size/2.0f) * spacing;
                float y = (row - config.wall_size/2.0f) * spacing;
                ctx.add_cube_particle(x, y, 0, 0.3f, 150, 100, 100);
                total_particles++;
            }
        }
        
        // Add lights in triangle/square formation
        float light_distance = 8.0f;
        float light_z = 5.0f;
        
        for (int i = 0; i < config.num_lights; i++) {
            float angle = (2.0f * M_PI * i) / std::max(3, config.num_lights);
            float light_x = light_distance * std::cos(angle);
            float light_y = light_distance * std::sin(angle);
            
            ctx.add_light_particle(light_x, light_y, light_z, 2.0f, 
                                  1.0f, 1.0f, 0.8f, 50000.0f, 50.0f);
        }
        
        // Position camera
        ctx.set_camera_position(15, 10, 15);
        ctx.set_camera_look_at(0, 0, 0);
        
        // Print configuration
        std::cout << std::setw(30) << config.description
                  << std::setw(12) << total_particles
                  << std::setw(10) << config.num_lights
                  << std::setw(25) << "Check FPS counter" << std::endl;
    }
    
    std::cout << "\n=== Performance Impact Analysis ===" << std::endl;
    std::cout << "Each additional light source causes FPS drop due to:" << std::endl;
    std::cout << "1. Shadow ray testing (every pixel tests occlusion to every light)" << std::endl;
    std::cout << "2. Light accumulation calculations" << std::endl;
    std::cout << "3. Multiple BVH traversals per pixel" << std::endl;
    std::cout << "\nRun with visual test mode to see actual FPS for each configuration" << std::endl;
    
    return true;
}