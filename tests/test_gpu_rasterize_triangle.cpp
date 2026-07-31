// test_gpu_rasterize_triangle.cpp
// STEP 2: Test full triangle rasterization - all pixels in bbox
//
// Goal: Verify GPU rasterizes all pixels inside triangle using edge equations
// Test: Hardcoded triangle should be filled with green color

#include "test_context.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include <iostream>

bool test_gpu_rasterize_triangle(TestContext& ctx) {
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

    // Green color (RGBA)
    uint8_t r = 0, g = 255, b = 0, a = 255;

    // Get native buffer
    auto& native_buffer = pixel_buffer.get_native_buffer();
    if (native_buffer.empty()) {
        std::cout << "[FAIL] PixelBuffer not using native format" << std::endl;
        return false;
    }

    // Rasterize triangle on GPU
    gpu_rasterizer.rasterize_triangle(native_buffer.data(), triangle_verts, r, g, b, a);

    // Test 1: Center of triangle should be green
    // Center is approximately at (200, 150)
    EnhancedPixel center = pixel_buffer.get_pixel(200, 150);
    bool center_green = (center.r == 0 && center.g == 255 && center.b == 0 && center.a == 255);

    if (!center_green) {
        std::cout << "[FAIL] Triangle center (200,150) = ("
                  << (int)center.r << ", " << (int)center.g << ", "
                  << (int)center.b << ", " << (int)center.a << ") expected (0,255,0,255)" << std::endl;
        return false;
    }

    // Test 2: Top-left vertex should be green
    EnhancedPixel v0 = pixel_buffer.get_pixel(100, 100);
    bool v0_green = (v0.r == 0 && v0.g == 255 && v0.b == 0 && v0.a == 255);

    if (!v0_green) {
        std::cout << "[FAIL] Vertex (100,100) = ("
                  << (int)v0.r << ", " << (int)v0.g << ", "
                  << (int)v0.b << ", " << (int)v0.a << ") expected (0,255,0,255)" << std::endl;
        return false;
    }

    // Test 3: Point outside triangle should be black
    EnhancedPixel outside = pixel_buffer.get_pixel(50, 50);
    bool outside_black = (outside.r == 0 && outside.g == 0 && outside.b == 0 && outside.a == 255);

    if (!outside_black) {
        std::cout << "[FAIL] Outside point (50,50) = ("
                  << (int)outside.r << ", " << (int)outside.g << ", "
                  << (int)outside.b << ", " << (int)outside.a << ") expected (0,0,0,255)" << std::endl;
        return false;
    }

    // Test 4: Count green pixels (should be > 0 and reasonable)
    int green_count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            EnhancedPixel p = pixel_buffer.get_pixel(x, y);
            if (p.r == 0 && p.g == 255 && p.b == 0 && p.a == 255) {
                green_count++;
            }
        }
    }

    // Triangle area ≈ 0.5 * base * height = 0.5 * 200 * 150 = 15000 pixels
    if (green_count < 10000 || green_count > 20000) {
        std::cout << "[FAIL] Green pixel count = " << green_count
                  << " expected ~15000 (reasonable triangle area)" << std::endl;
        return false;
    }

    std::cout << "[PASS] GPU rasterized triangle: " << green_count << " pixels" << std::endl;
    return true;
}
