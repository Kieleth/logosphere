// test_gpu_barycentric.cpp
// STEP 3: Test barycentric interpolation - verify coordinates
//
// Goal: Verify GPU computes barycentric coordinates matching CPU
// Test: Visualize as RGB (w=R, u=G, v=B) and check specific pixels

#include "test_context.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include <iostream>
#include <cmath>

bool test_gpu_barycentric(TestContext& ctx) {
    // Test resolution
    const int width = 400;
    const int height = 300;

    // Create PixelBuffer
    PixelBuffer pixel_buffer(width, height);
    pixel_buffer.clear(0, 0, 0, 255);  // Black background

    // Initialize GPU rasterizer
    Logosphere::GPURasterizer gpu_rasterizer;
    if (!gpu_rasterizer.initialize(width, height)) {
        std::cout << "[FAIL] GPU rasterizer failed to initialize" << std::endl;
        return false;
    }

    // Define triangle vertices in screen space (2D)
    // Triangle: (100,100), (300,100), (200,250)
    float triangle_verts[6] = {
        100.0f, 100.0f,  // v0
        300.0f, 100.0f,  // v1
        200.0f, 250.0f   // v2
    };

    // Get native buffer
    auto& native_buffer = pixel_buffer.get_native_buffer();
    if (native_buffer.empty()) {
        std::cout << "[FAIL] PixelBuffer not using native format" << std::endl;
        return false;
    }

    // Rasterize triangle with barycentric visualization
    gpu_rasterizer.rasterize_triangle_barycentric(native_buffer.data(), triangle_verts);

    // Test 1: Vertex 0 (100,100) should be RED (w=1, u=0, v=0)
    EnhancedPixel v0 = pixel_buffer.get_pixel(100, 100);

    bool v0_red = (v0.r > 200 && v0.g < 50 && v0.b < 50);
    if (!v0_red) {
        std::cout << "[FAIL] Vertex 0 (100,100) = ("
                  << (int)v0.r << ", " << (int)v0.g << ", " << (int)v0.b
                  << ") expected ~(255,0,0)" << std::endl;
        return false;
    }

    // Test 2: Vertex 1 (300,100) should be GREEN (w=0, u=1, v=0)
    EnhancedPixel v1 = pixel_buffer.get_pixel(300, 100);
    bool v1_green = (v1.r < 50 && v1.g > 200 && v1.b < 50);
    if (!v1_green) {
        std::cout << "[FAIL] Vertex 1 (300,100) = ("
                  << (int)v1.r << ", " << (int)v1.g << ", " << (int)v1.b
                  << ") expected ~(0,255,0)" << std::endl;
        return false;
    }

    // Test 3: Vertex 2 (200,250) should be BLUE (w=0, u=0, v=1)
    EnhancedPixel v2 = pixel_buffer.get_pixel(200, 250);
    bool v2_blue = (v2.r < 50 && v2.g < 50 && v2.b > 200);
    if (!v2_blue) {
        std::cout << "[FAIL] Vertex 2 (200,250) = ("
                  << (int)v2.r << ", " << (int)v2.g << ", " << (int)v2.b
                  << ") expected ~(0,0,255)" << std::endl;
        return false;
    }

    // Test 4: Center of triangle should be GRAY (w≈0.33, u≈0.33, v≈0.33)
    EnhancedPixel center = pixel_buffer.get_pixel(200, 150);
    bool center_gray = (
        abs((int)center.r - (int)center.g) < 30 &&
        abs((int)center.g - (int)center.b) < 30 &&
        center.r > 50 && center.r < 200
    );
    if (!center_gray) {
        std::cout << "[FAIL] Triangle center (200,150) = ("
                  << (int)center.r << ", " << (int)center.g << ", " << (int)center.b
                  << ") expected roughly equal gray" << std::endl;
        return false;
    }

    // Test 5: Edge midpoint v0→v1 should be YELLOW (w≈0.5, u≈0.5, v≈0)
    EnhancedPixel edge01 = pixel_buffer.get_pixel(200, 100);
    bool edge01_yellow = (edge01.r > 100 && edge01.g > 100 && edge01.b < 50);
    if (!edge01_yellow) {
        std::cout << "[FAIL] Edge v0→v1 midpoint (200,100) = ("
                  << (int)edge01.r << ", " << (int)edge01.g << ", " << (int)edge01.b
                  << ") expected ~(127,127,0)" << std::endl;
        return false;
    }

    // Test 6: Verify barycentric weights sum to 1.0 (±5% tolerance)
    // Sample multiple pixels and check w+u+v ≈ 255
    int samples_tested = 0;
    int samples_valid = 0;
    for (int y = 110; y < 240; y += 20) {
        for (int x = 110; x < 290; x += 20) {
            EnhancedPixel p = pixel_buffer.get_pixel(x, y);
            // Skip black pixels (outside triangle)
            if (p.r == 0 && p.g == 0 && p.b == 0) continue;

            int sum = (int)p.r + (int)p.g + (int)p.b;
            samples_tested++;

            // Sum should be ~255 (±5% = ±13)
            if (sum >= 242 && sum <= 268) {
                samples_valid++;
            }
        }
    }

    if (samples_tested > 0) {
        float valid_ratio = (float)samples_valid / samples_tested;
        if (valid_ratio < 0.9f) {  // 90% of samples should have valid sums
            std::cout << "[FAIL] Only " << samples_valid << "/" << samples_tested
                      << " pixels had w+u+v ≈ 1.0 (expected >90%)" << std::endl;
            return false;
        }
    }

    std::cout << "[PASS] GPU barycentric interpolation: vertices correct, "
              << samples_valid << "/" << samples_tested << " pixels sum to 1.0" << std::endl;
    return true;
}
