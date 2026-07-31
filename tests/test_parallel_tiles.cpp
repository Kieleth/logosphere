#include "../src/test_context.h"
#include "logosphere/rendering/tile_thread_pool.h"
#include "logosphere/rendering/parallel_tile_processor.h"
#include "logosphere/rendering/surface_rasterizer.h"
#include <chrono>
#include <iostream>
#include <vector>

/**
 * Test parallel tile rendering
 * 
 * This test verifies that the thread pool correctly processes tiles
 * and measures the speedup compared to single-threaded processing.
 */

bool test_parallel_tiles(TestContext& ctx) {
    std::cout << "\n[TEST] Parallel Tile Rendering\n";
    std::cout << "================================\n";
    
    // Create a simple test scenario with mock data
    const int screen_width = 320;   // Small for testing
    const int screen_height = 240;
    const int tile_size = 16;
    const int tiles_x = (screen_width + tile_size - 1) / tile_size;
    const int tiles_y = (screen_height + tile_size - 1) / tile_size;
    const int total_tiles = tiles_x * tiles_y;
    
    std::cout << "Screen: " << screen_width << "x" << screen_height << "\n";
    std::cout << "Tiles: " << tiles_x << "x" << tiles_y << " = " << total_tiles << " tiles\n";
    
    // Create framebuffer
    std::vector<uint32_t> framebuffer(screen_width * screen_height, 0);
    
    // Create mock tile inputs
    std::vector<ParallelTile::TileInput> tile_inputs;
    tile_inputs.reserve(total_tiles);
    
    // Create a simple mock surface for testing
    std::vector<const void*> mock_surfaces;
    SurfaceRasterizer::SurfaceData test_surface;
    test_surface.particle_index = 0;
    test_surface.surface_index = 0;
    mock_surfaces.push_back(&test_surface);
    
    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            ParallelTile::TileInput input;
            input.tile_x = tx;
            input.tile_y = ty;
            input.screen_width = screen_width;
            input.screen_height = screen_height;
            input.surfaces = &mock_surfaces;
            input.camera_system = nullptr;  // Mock - not used in simple test
            input.light_system = nullptr;   // Mock - not used in simple test
            input.pixel_shader = nullptr;   // Mock - not used in simple test
            input.cam_x = 0;
            input.cam_y = 0;
            input.cam_z = 10;
            tile_inputs.push_back(input);
        }
    }
    
    // Test 1: Process with thread pool
    std::cout << "\n1. Testing parallel processing with thread pool...\n";
    TileThreadPool thread_pool;
    
    auto parallel_start = std::chrono::high_resolution_clock::now();
    thread_pool.submit_tiles(tile_inputs, framebuffer.data(), screen_width, screen_height);
    thread_pool.wait_for_completion();
    auto parallel_end = std::chrono::high_resolution_clock::now();
    
    auto parallel_time = std::chrono::duration<double, std::milli>(parallel_end - parallel_start).count();
    std::cout << "   Parallel time: " << parallel_time << "ms\n";
    std::cout << "   Tiles completed: " << thread_pool.get_tiles_completed() 
              << "/" << thread_pool.get_total_tiles() << "\n";
    
    // Test 2: Process single-threaded for comparison
    std::cout << "\n2. Testing single-threaded processing...\n";
    std::fill(framebuffer.begin(), framebuffer.end(), 0);  // Clear framebuffer
    
    auto single_start = std::chrono::high_resolution_clock::now();
    for (const auto& input : tile_inputs) {
        ParallelTile::TileBuffer buffer;
        ParallelTile::process_tile_pure(input, buffer);
        ParallelTile::copy_tile_to_framebuffer(
            buffer, framebuffer.data(), screen_width, screen_height
        );
    }
    auto single_end = std::chrono::high_resolution_clock::now();
    
    auto single_time = std::chrono::duration<double, std::milli>(single_end - single_start).count();
    std::cout << "   Single-threaded time: " << single_time << "ms\n";
    
    // Calculate speedup
    double speedup = single_time / parallel_time;
    std::cout << "\n3. Results:\n";
    std::cout << "   Speedup: " << speedup << "x\n";
    std::cout << "   Efficiency: " << (speedup / 4.0 * 100) << "% (4 threads)\n";
    
    // Basic validation
    if (thread_pool.get_tiles_completed() != total_tiles) {
        std::cerr << "ERROR: Not all tiles were processed!\n";
        return false;
    }
    
    if (speedup < 1.5) {
        std::cerr << "WARNING: Speedup less than 1.5x - threading may not be effective\n";
        // Don't fail the test, just warn
    }
    
    std::cout << "\n✅ Parallel tile rendering test PASSED\n";
    return true;
}