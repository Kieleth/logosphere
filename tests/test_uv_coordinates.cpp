// Test UV coordinate calculation for pixel-to-surface mapping
// This test verifies that different pixels on a surface get different UV coordinates
// Critical for per-pixel lighting and shadows to work correctly

#include "../src/test_context.h"
#include "../src/core/camera_system.h"
#include "../src/core/particle_system.h"
#include <iostream>
#include <set>
#include <cmath>
#include <algorithm>

bool test_uv_coordinates(TestContext& ctx) {
    std::cout << "\n=== Testing UV Coordinate Variation ===\n" << std::endl;
    
    // Clear any existing particles
    ctx.clear_particles();
    
    // Create a simple cube at origin
    std::cout << "Creating test cube at origin..." << std::endl;
    int cube_id = ctx.add_cube_particle(0, 0, 0, 2.0f,  // 2x2x2 cube at origin
                                        1.0f, 1.0f, 1.0f); // white color
    
    // Get the camera system directly from context
    CameraSystem& camera = ctx.camera_system;
    
    // Set up camera for proper isometric view
    // From southwest-below looking northeast-up (standard isometric)
    camera.set_position(-10, -10, 20);
    camera.look_at(0, 0, 0);
    
    // Get the cube's surfaces
    auto particles_view = ctx.particle_system.lock_particles_for_read();
    auto surfaces = particles_view[cube_id].GetSurfaces();
    
    // Find a south face triangle (should be visible from our camera position)
    // With triangles, we have 2 triangles per face
    int south_triangle_idx = -1;
    for (size_t i = 0; i < surfaces.size(); i++) {
        if (surfaces[i].ny < -0.9f && surfaces[i].vertex_count == 3) {  // South face triangle has normal pointing -Y
            south_triangle_idx = i;
            break;
        }
    }
    
    if (south_triangle_idx < 0) {
        std::cout << "ERROR: Could not find south face triangle!" << std::endl;
        return false;
    }
    
    std::cout << "Found south face triangle at index " << south_triangle_idx << std::endl;
    
    // Project the south triangle to screen
    const Surface& south_triangle = surfaces[south_triangle_idx];
    
    // Get the 3 vertices of the triangle in world space
    float vertices[3][3];
    for (int i = 0; i < 3; i++) {
        vertices[i][0] = south_triangle.vertices[i][0];
        vertices[i][1] = south_triangle.vertices[i][1];
        vertices[i][2] = south_triangle.vertices[i][2];
    }
    
    // Project the triangle to screen
    CameraSystem::ProjectedTriangle projected = camera.project_triangle(vertices);
    std::cout << "Triangle projected to screen:" << std::endl;
    for (int i = 0; i < 3; i++) {
        std::cout << "  Vertex " << i << ": world(" << vertices[i][0] << "," << vertices[i][1] << "," << vertices[i][2] 
                  << ") -> screen(" << projected.screen_corners[i][0] << "," << projected.screen_corners[i][1] << ")" << std::endl;
    }
    
    if (!projected.on_screen) {
        std::cout << "ERROR: South face not visible on screen!" << std::endl;
        return false;
    }
    
    std::cout << "South face projected to screen bounds: ("
              << projected.min_x << "," << projected.min_y << ") to ("
              << projected.max_x << "," << projected.max_y << ")" << std::endl;
    
    // Sample UV coordinates at different pixels
    std::cout << "\nSampling UV coordinates across surface:" << std::endl;
    
    std::set<std::pair<float, float>> unique_uvs;
    int samples = 0;
    
    // Sample a 5x5 grid of pixels across the projected surface
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            // Calculate pixel position within the projected bounds
            int pixel_x = projected.min_x + (projected.max_x - projected.min_x) * col / 4;
            int pixel_y = projected.min_y + (projected.max_y - projected.min_y) * row / 4;
            
            float u, v;
            if (camera.pixel_to_triangle_uv(pixel_x, pixel_y, projected, u, v)) {
                std::cout << "  Pixel(" << pixel_x << "," << pixel_y << ") -> UV(" 
                          << u << "," << v << ")" << std::endl;
                
                // Round to 2 decimal places for uniqueness check
                float rounded_u = std::round(u * 100) / 100;
                float rounded_v = std::round(v * 100) / 100;
                unique_uvs.insert({rounded_u, rounded_v});
                samples++;
            }
        }
    }
    
    std::cout << "\nResults:" << std::endl;
    std::cout << "  Total samples: " << samples << std::endl;
    std::cout << "  Unique UV coordinates: " << unique_uvs.size() << std::endl;
    
    // We should have most samples produce unique UV coordinates
    // If we have less than 50% unique, something is wrong
    size_t min_unique = samples / 2;
    if (unique_uvs.size() < min_unique) {
        std::cout << "ERROR: Not enough UV variation! Only " << unique_uvs.size() 
                  << " unique coordinates found out of " << samples << " samples." << std::endl;
        std::cout << "This means pixels are getting duplicate UV coordinates," << std::endl;
        std::cout << "which breaks per-pixel lighting and shadows!" << std::endl;
        return false;
    }
    
    // Also check that we're getting a good spread of values
    float min_u = 1.0f, max_u = 0.0f;
    float min_v = 1.0f, max_v = 0.0f;
    for (const auto& uv : unique_uvs) {
        min_u = std::min(min_u, uv.first);
        max_u = std::max(max_u, uv.first);
        min_v = std::min(min_v, uv.second);
        max_v = std::max(max_v, uv.second);
    }
    
    float u_range = max_u - min_u;
    float v_range = max_v - min_v;
    
    std::cout << "  U range: " << min_u << " to " << max_u << " (spread: " << u_range << ")" << std::endl;
    std::cout << "  V range: " << min_v << " to " << max_v << " (spread: " << v_range << ")" << std::endl;
    
    // We should see reasonable spread in both U and V
    // For a projected quad, we expect at least 0.3 range
    if (u_range < 0.3f || v_range < 0.3f) {
        std::cout << "ERROR: UV coordinates not spreading across surface!" << std::endl;
        std::cout << "U range: " << u_range << ", V range: " << v_range << std::endl;
        std::cout << "This indicates the UV calculation is broken." << std::endl;
        return false;
    }
    
    std::cout << "\n✅ UV coordinates vary correctly across surface!" << std::endl;
    std::cout << "✅ Per-pixel lighting should work properly." << std::endl;
    
    return true;
}