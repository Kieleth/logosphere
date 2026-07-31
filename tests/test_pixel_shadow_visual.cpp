// Test pixel-accurate shadows with visual output
#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include <iostream>
#include <iomanip>

bool test_pixel_shadow_visual(TestContext& ctx) {
    std::cout << "\n=== Testing Pixel-Accurate Shadow Visualization ===" << std::endl;
    
    ctx.clear_particles();
    
    // Eden-like setup
    int light_id = ctx.add_light_particle(0, -2, 0, 0.2f, 1, 0.85f, 0.6f, 10000000, 50);
    int eva_id = ctx.add_cube_particle(0, 1, 0, 1.0f, 0.8f, 0.3f, 0.3f);
    int wall_id = ctx.add_cube_particle(0, 6, 0, 4.0f, 0.9f, 0.9f, 0.9f);
    
    std::cout << "Setup: Light at (0,-2,0), Eva at (0,1,0), Wall at (0,6,0)" << std::endl;
    
    // Set camera to see the scene
    ctx.set_camera_position(-5.0f, -5.0f, 10.0f);
    ctx.set_camera_look_at(0.0f, 3.0f, 0.0f);
    
    // Update lighting (calculates shadows)
    ctx.update_lighting();
    
    // RENDER the scene
    ctx.render();
    
    // Sample the rendered image to see what's visible
    int width, height;
    ctx.get_render_size(width, height);
    
    std::cout << "\n=== Sampling Rendered Pixels ===" << std::endl;
    std::cout << "Render size: " << width << "x" << height << std::endl;
    
    // Sample a grid across the screen
    int sample_points = 10;
    int dark_pixels = 0;
    int lit_pixels = 0;
    int total_samples = 0;
    
    // Focus on center area where wall should be
    int center_x = width / 2;
    int center_y = height / 2;
    int sample_radius = width / 4;
    
    std::cout << "\nSampling around screen center (" << center_x << "," << center_y << "):" << std::endl;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy += sample_radius/5) {
        for (int dx = -sample_radius; dx <= sample_radius; dx += sample_radius/5) {
            int x = center_x + dx;
            int y = center_y + dy;
            
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            
            uint8_t r, g, b, a;
            int object_id;
            ctx.get_pixel(x, y, r, g, b, a, object_id);
            
            total_samples++;
            
            // Check if pixel is dark (near black)
            if (r < 10 && g < 10 && b < 10) {
                dark_pixels++;
                if (dark_pixels == 1) {
                    std::cout << "  First dark pixel at (" << x << "," << y << "): "
                              << "rgb(" << (int)r << "," << (int)g << "," << (int)b << ") "
                              << "object=" << object_id << std::endl;
                }
            } else {
                lit_pixels++;
                if (lit_pixels == 1) {
                    std::cout << "  First lit pixel at (" << x << "," << y << "): "
                              << "rgb(" << (int)r << "," << (int)g << "," << (int)b << ") "
                              << "object=" << object_id << std::endl;
                }
            }
        }
    }
    
    std::cout << "\nPixel statistics:" << std::endl;
    std::cout << "  Dark pixels: " << dark_pixels << "/" << total_samples 
              << " (" << (100*dark_pixels/total_samples) << "%)" << std::endl;
    std::cout << "  Lit pixels: " << lit_pixels << "/" << total_samples 
              << " (" << (100*lit_pixels/total_samples) << "%)" << std::endl;
    
    // Now check the wall's south face directly
    std::cout << "\n=== Wall South Face Shadow Pattern ===" << std::endl;
    std::cout << "Visualizing shadow on 10x10 grid:" << std::endl;
    
    // Sample the south face in a grid pattern
    int south_tri = 2;  // First triangle of south face
    
    std::cout << "\n";
    for (float v = 0.9f; v >= 0.0f; v -= 0.1f) {
        std::cout << "v=" << std::fixed << std::setprecision(1) << v << " ";
        for (float u = 0.0f; u <= 0.9f; u += 0.1f) {
            auto color = ctx.get_surface_light_color(wall_id, south_tri, u, v);
            
            // Visualize intensity as ASCII
            if (color.intensity < 1.0f) {
                std::cout << "█";  // Full shadow
            } else if (color.intensity < 1000.0f) {
                std::cout << "▓";  // Deep shadow
            } else if (color.intensity < 5000.0f) {
                std::cout << "▒";  // Partial shadow
            } else if (color.intensity < 10000.0f) {
                std::cout << "░";  // Light shadow
            } else {
                std::cout << " ";  // Fully lit
            }
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nLegend: █=shadow ▓=deep ▒=partial ░=light  =lit" << std::endl;
    
    // Check if Eva is actually blocking anything
    std::cout << "\n=== Shadow Ray Analysis ===" << std::endl;
    
    // Test ray from wall center to light
    float wall_center_x = 0, wall_center_y = 4, wall_center_z = 0;
    float light_x = 0, light_y = -2, light_z = 0;
    
    // Check what happens between wall and light
    std::cout << "Ray from wall center (0,4,0) to light (0,-2,0):" << std::endl;
    
    // At what Y does ray intersect Eva's volume?
    // Eva is at (0,1,0) with size 1, so spans y=[0.5, 1.5]
    float t_enter = (1.5f - wall_center_y) / (light_y - wall_center_y);
    float t_exit = (0.5f - wall_center_y) / (light_y - wall_center_y);
    
    std::cout << "  Ray enters Eva at t=" << t_enter << " (y=1.5)" << std::endl;
    std::cout << "  Ray exits Eva at t=" << t_exit << " (y=0.5)" << std::endl;
    
    if (t_enter > 0 && t_enter < 1) {
        std::cout << "  ✓ Ray DOES pass through Eva - shadow expected" << std::endl;
    } else {
        std::cout << "  ✗ Ray does NOT pass through Eva - no shadow expected" << std::endl;
    }
    
    return true;
}