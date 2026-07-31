// test_gpu_rasterize_minimal.cpp
// STEP 1: Test minimal GPU rasterization - write 1 pixel
//
// Goal: Verify GPU can write to PixelBuffer
// Test: Pixel (150, 150) should be red after GPU kernel runs

#include "test_context.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include <iostream>

bool test_gpu_rasterize_minimal(TestContext& ctx) {
    // Test resolution
    const int width = 800;
    const int height = 600;

    // Create PixelBuffer (same as CPU rendering uses)
    PixelBuffer pixel_buffer(width, height);
    pixel_buffer.clear(0, 0, 0, 255);  // Black background

    // Initialize GPU rasterizer
    Logosphere::GPURasterizer gpu_rasterizer;
    if (!gpu_rasterizer.initialize(width, height)) {
        std::cout << "[FAIL] GPU rasterizer failed to initialize (Metal not available?)" << std::endl;
        return false;
    }

    // Get native buffer pointer (uint32_t BGRA format)
    auto& native_buffer = pixel_buffer.get_native_buffer();
    if (native_buffer.empty()) {
        std::cout << "[FAIL] PixelBuffer not using native format" << std::endl;
        return false;
    }

    // Call minimal GPU rasterization (should write red pixel at 150, 150)
    gpu_rasterizer.rasterize_minimal(native_buffer.data());

    // Verify pixel (150, 150) is red
    EnhancedPixel pixel = pixel_buffer.get_pixel(150, 150);

    // Red pixel: R=255, G=0, B=0, A=255
    bool is_red = (pixel.r == 255 && pixel.g == 0 && pixel.b == 0 && pixel.a == 255);

    if (is_red) {
        std::cout << "[PASS] GPU wrote red pixel at (150, 150)" << std::endl;
        return true;
    } else {
        std::cout << "[FAIL] Pixel (150, 150) = ("
                  << (int)pixel.r << ", "
                  << (int)pixel.g << ", "
                  << (int)pixel.b << ", "
                  << (int)pixel.a << ") expected (255, 0, 0, 255)" << std::endl;
        return false;
    }
}
