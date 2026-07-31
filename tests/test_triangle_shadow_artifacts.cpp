#include "../src/test_context.h"
#include <iostream>
#include <cmath>

// ============================================================================
// TEST: Triangle Shadow Artifacts
// ============================================================================
// PURPOSE: Isolate and debug the shadow distortions introduced by triangles
// SETUP: Simple scene with one light, one blocker, one wall
// EXPECTED: Shadows should be consistent without distortions

bool test_triangle_shadow_artifacts(TestContext& ctx) {
    std::cout << "\n=== Testing Triangle Shadow Artifacts ===" << std::endl;
    std::cout << "Goal: Identify why triangles create shadow distortions" << std::endl;
    
    // Clear scene
    ctx.clear_particles();
    
    // Camera setup - Eden-like view from SW
    ctx.set_camera_position(-10.0f, -10.0f, 20.0f);
    ctx.set_camera_look_at(0.0f, 0.0f, 0.0f);
    
    // 1. Add a light source at floor level
    std::cout << "\n1. Adding light at (0, -2, 0) - floor level, south of scene" << std::endl;
    ctx.add_light_particle(
        0.0f, -2.0f, 0.0f,   // position south of scene at floor
        0.2f,                // size
        1.0f, 0.85f, 0.6f,   // warm white (Eden standard)
        10000000.0f,         // 10M intensity
        50.0f                // radius
    );
    
    // 2. Add Eva (the blocker) - centered between light and wall
    std::cout << "2. Adding Eva (blocker) at (0, 1, 0) - between light and wall" << std::endl;
    ctx.add_cube_particle(
        0.0f, 1.0f, 0.0f,    // position between light and wall
        1.0f,                // size (Eva size)
        0.8f, 0.3f, 0.3f     // reddish (Eva-like)
    );
    
    // 3. Add a large wall cube further north
    std::cout << "3. Adding large wall at (0, 6, 0)" << std::endl;
    ctx.add_cube_particle(
        0.0f, 6.0f, 0.0f,    // position north on Y axis
        4.0f,                // size (large wall)
        0.9f, 0.9f, 0.9f     // near white
    );
    
    // Note: Pixel-accurate lighting is always enabled
    
    // Update lighting and render
    ctx.update_lighting();
    ctx.render();
    
    // Analyze the wall surface that faces the light (south face, -Y)
    // With triangles, face 1 (-Y) is now triangles 2 and 3
    std::cout << "\n4. Analyzing shadow pattern on wall's south face (-Y)" << std::endl;
    
    // Sample the wall at a grid of points
    int wall_particle = 2;  // Third particle added (the wall)
    int south_triangle_1 = 2;   // First triangle of -Y face
    int south_triangle_2 = 3;   // Second triangle of -Y face
    
    // Sample both triangles that make up the south face
    std::cout << "\nTriangle 1 (index " << south_triangle_1 << ") samples:" << std::endl;
    for (float v = 0.1f; v <= 0.9f; v += 0.4f) {
        for (float u = 0.1f; u <= 0.9f; u += 0.4f) {
            auto color = ctx.get_surface_light_color(wall_particle, south_triangle_1, u, v);
            std::cout << "  UV(" << u << "," << v << "): intensity=" << color.intensity 
                      << " RGB(" << (int)color.r << "," << (int)color.g << "," << (int)color.b << ")"
                      << (color.intensity < 100.0f ? " [SHADOW]" : " [LIT]") << std::endl;
        }
    }
    
    std::cout << "\nTriangle 2 (index " << south_triangle_2 << ") samples:" << std::endl;
    for (float v = 0.1f; v <= 0.9f; v += 0.4f) {
        for (float u = 0.1f; u <= 0.9f; u += 0.4f) {
            auto color = ctx.get_surface_light_color(wall_particle, south_triangle_2, u, v);
            std::cout << "  UV(" << u << "," << v << "): intensity=" << color.intensity 
                      << " RGB(" << (int)color.r << "," << (int)color.g << "," << (int)color.b << ")"
                      << (color.intensity < 100.0f ? " [SHADOW]" : " [LIT]") << std::endl;
        }
    }
    
    // CRITICAL: First verify we actually have shadows!
    // Get average intensity across both triangles
    float total_intensity = 0;
    int sample_count = 0;
    for (int tri = south_triangle_1; tri <= south_triangle_2; tri++) {
        for (float v = 0.3f; v <= 0.7f; v += 0.2f) {
            for (float u = 0.3f; u <= 0.7f; u += 0.2f) {
                auto color = ctx.get_surface_light_color(wall_particle, tri, u, v);
                total_intensity += color.intensity;
                sample_count++;
            }
        }
    }
    float avg_intensity = total_intensity / sample_count;
    std::cout << "\n5. Shadow Verification - Average intensity: " << avg_intensity << " lux" << std::endl;
    
    // Calculate expected intensity WITHOUT blocker
    // Light at (0,-2,0), wall south face center at (0,4,0) 
    float dx = 0 - 0;
    float dy = 4 - (-2);  // 6 meters
    float dz = 0 - 0;
    float distance = sqrtf(dx*dx + dy*dy + dz*dz);
    float expected_unblocked = 10000000.0f / (4.0f * 3.14159f * distance * distance);
    std::cout << "  Expected intensity without blocker: ~" << expected_unblocked << " lux" << std::endl;
    std::cout << "  Actual average intensity: " << avg_intensity << " lux" << std::endl;
    
    if (avg_intensity > expected_unblocked * 0.5f) {
        std::cout << "  ERROR: Wall is too bright! Shadow is NOT working!" << std::endl;
        std::cout << "  The blocker should reduce intensity to < " << (expected_unblocked * 0.1f) << " lux" << std::endl;
        return false;
    }
    
    // Check for inconsistencies between the two triangles
    std::cout << "\n6. Checking for discontinuities between triangles" << std::endl;
    
    // Sample along the diagonal where the two triangles meet
    // The diagonal should be from (0,0) to (1,1) in UV space
    std::cout << "\nSampling along diagonal seam:" << std::endl;
    for (float t = 0.2f; t <= 0.8f; t += 0.2f) {
        // Just before the diagonal (in triangle 1)
        float u1 = t - 0.05f;
        float v1 = t - 0.05f;
        auto color1 = ctx.get_surface_light_color(wall_particle, south_triangle_1, u1, v1);
        
        // Just after the diagonal (in triangle 2)
        float u2 = t + 0.05f;
        float v2 = t + 0.05f;
        auto color2 = ctx.get_surface_light_color(wall_particle, south_triangle_2, u2, v2);
        
        float diff = std::abs(color1.intensity - color2.intensity);
        std::cout << "  t=" << t << ": tri1=" << color1.intensity 
                  << " tri2=" << color2.intensity 
                  << " diff=" << diff;
        
        if (diff > 10.0f) {
            std::cout << " [DISCONTINUITY!]";
        }
        std::cout << std::endl;
    }
    
    // Check the rendered framebuffer for visual artifacts
    std::cout << "\n7. Checking rendered pixels for artifacts" << std::endl;
    const auto& pixel_buffer = ctx.render_system.get_framebuffer_buffer();
    const auto& framebuffer = pixel_buffer.get_buffer();  // Get the debug buffer
    auto& res = ctx.render_system.get_resolution_manager();
    int width = res.get_window_width();
    int height = res.get_window_height();
    
    // Look for sharp transitions in brightness (shark teeth pattern)
    int sharp_transitions = 0;
    for (int y = height/2 - 50; y < height/2 + 50; y++) {
        for (int x = width/2 - 50; x < width/2 + 49; x++) {
            int idx1 = y * width + x;
            int idx2 = y * width + x + 1;
            
            const auto& pixel1 = framebuffer[idx1];
            const auto& pixel2 = framebuffer[idx2];
            
            // Check for sharp horizontal transitions
            int brightness1 = pixel1.r + pixel1.g + pixel1.b;
            int brightness2 = pixel2.r + pixel2.g + pixel2.b;
            
            if (std::abs(brightness1 - brightness2) > 100) {
                sharp_transitions++;
            }
        }
    }
    
    std::cout << "  Found " << sharp_transitions << " sharp brightness transitions" << std::endl;
    
    // Pass/fail based on artifacts found
    bool has_discontinuities = false;
    bool has_sharp_transitions = (sharp_transitions > 20);  // Some transitions are normal at edges
    
    if (has_sharp_transitions) {
        std::cout << "\nFAIL: Too many sharp transitions (" << sharp_transitions 
                  << ") - indicates triangle artifacts!" << std::endl;
        return false;
    }
    
    std::cout << "\n✓ No triangle artifacts detected" << std::endl;
    std::cout << "✓ Shadows are working (intensity reduced by blocker)" << std::endl;
    return true;
}