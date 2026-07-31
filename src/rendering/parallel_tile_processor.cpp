#include "logosphere/rendering/parallel_tile_processor.h"
#include "logosphere/rendering/surface_rasterizer.h"
#include "../core/camera_system.h"
#include "../core/light_system.h"
#include "logosphere/rendering/pixel_shader.h"
#include "../object_id.h"
#include <algorithm>
#include <cmath>

/**
 * ParallelTileProcessor Implementation
 * 
 * THREADING NOTES (2025-01-22):
 * =============================
 * This implementation is designed for zero contention between threads.
 * Each thread processes tiles independently with no shared mutable state.
 * 
 * The only shared data are read-only structures:
 * - Surface geometry (read-only)
 * - Camera matrices (read-only)  
 * - Light data (read-only)
 * - BVH tree for shadows (read-only)
 * 
 * All writes go to thread-local TileBuffer, eliminating race conditions.
 */

namespace ParallelTile {

/**
 * process_tile_pure - Main tile processing function (thread-safe)
 * 
 * This function processes all surfaces that overlap with a single tile,
 * writing the results to a thread-local buffer. No global state access.
 * 
 * PERFORMANCE NOTES:
 * - Entire TileBuffer (2KB) fits in L1 cache
 * - No heap allocations (everything on stack)
 * - Depth testing done locally within tile
 * - Shadow rays are read-only (safe for parallel access)
 */
int process_tile_pure(const TileInput& input, TileBuffer& output) {
    // Initialize output
    output.tile_x = input.tile_x;
    output.tile_y = input.tile_y;
    output.pixels_written = 0;
    output.has_content = false;
    
    // Safety check
    if (!input.surfaces || input.surfaces->empty()) {
        return 0;
    }
    
    // Cast system pointers back to proper types
    // NOTE: In real implementation, we'd use proper typed pointers in TileInput
    const CameraSystem* camera = static_cast<const CameraSystem*>(input.camera_system);
    const LightSystem* lights = static_cast<const LightSystem*>(input.light_system);
    const IPixelShader* shader = static_cast<const IPixelShader*>(input.pixel_shader);
    
    // Calculate tile bounds in screen space
    int tile_start_x = input.tile_x * TILE_SIZE;
    int tile_start_y = input.tile_y * TILE_SIZE;
    int tile_end_x = std::min(tile_start_x + TILE_SIZE, input.screen_width);
    int tile_end_y = std::min(tile_start_y + TILE_SIZE, input.screen_height);
    
    // Local depth buffer for this tile (no sharing between threads)
    float tile_depth[TILE_SIZE][TILE_SIZE];
    for (int y = 0; y < TILE_SIZE; ++y) {
        for (int x = 0; x < TILE_SIZE; ++x) {
            tile_depth[y][x] = 1.0f;  // Initialize to far plane
        }
    }
    
    // Process each surface that overlaps this tile
    for (const void* surface_ptr : *input.surfaces) {
        const auto* surf_data = static_cast<const SurfaceRasterizer::SurfaceData*>(surface_ptr);

        // Get surface vertices (already projected to screen space)
        const Surface& surface = surf_data->surface;
        
        // Quick bounds check - skip if surface doesn't overlap tile
        // vertices is float[3][3] array, not a struct with .x, .y
        float min_x = std::min({surface.vertices[0][0], surface.vertices[1][0], 
                                surface.vertices[2][0]});
        float max_x = std::max({surface.vertices[0][0], surface.vertices[1][0], 
                                surface.vertices[2][0]});
        float min_y = std::min({surface.vertices[0][1], surface.vertices[1][1], 
                                surface.vertices[2][1]});
        float max_y = std::max({surface.vertices[0][1], surface.vertices[1][1], 
                                surface.vertices[2][1]});
        
        // Skip if surface is completely outside tile
        if (max_x < tile_start_x || min_x > tile_end_x ||
            max_y < tile_start_y || min_y > tile_end_y) {
            continue;
        }
        
        // Rasterize surface within tile bounds
        // This is simplified - real implementation would use edge equations
        for (int y = tile_start_y; y < tile_end_y; ++y) {
            for (int x = tile_start_x; x < tile_end_x; ++x) {
                // Tile-local coordinates
                int local_x = x - tile_start_x;
                int local_y = y - tile_start_y;
                
                // TODO: Proper triangle rasterization with edge equations
                // For now, simplified point-in-triangle test
                // In real implementation, would compute barycentric coordinates
                
                // Compute depth at this pixel (interpolated from vertices)
                float pixel_depth = 0.5f;  // Placeholder - would interpolate
                
                // Depth test against tile-local depth buffer
                if (pixel_depth < tile_depth[local_y][local_x]) {
                    // Update local depth buffer
                    tile_depth[local_y][local_x] = pixel_depth;
                    
                    // Calculate color (this is where shadow rays happen)
                    // Shadow rays are READ-ONLY operations on BVH, safe for threading
                    uint8_t r = 128, g = 128, b = 128;  // Placeholder color
                    
                    // Write to tile buffer
                    int pixel_index = local_y * TILE_SIZE + local_x;
                    TilePixel& pixel = output.pixels[pixel_index];
                    pixel.r = r;
                    pixel.g = g;
                    pixel.b = b;
                    pixel.a = 255;
                    pixel.depth = pixel_depth;
                    pixel.object_id = ObjectID::encode_particle_surface(
                        surf_data->particle_index, 
                        surf_data->surface_index
                    );
                    pixel.valid = true;
                    
                    output.pixels_written++;
                    output.has_content = true;
                }
            }
        }
    }
    
    return output.pixels_written;
}

/**
 * copy_tile_to_framebuffer - Copy completed tile to global framebuffer
 * 
 * This function should be called after tile processing is complete.
 * It copies the tile-local buffer to the appropriate region of the
 * global framebuffer.
 * 
 * SYNCHRONIZATION:
 * - Can be called by main thread after workers complete
 * - Can be parallelized if tiles are guaranteed not to overlap
 * - No two threads should write to same framebuffer region
 */
void copy_tile_to_framebuffer(const TileBuffer& tile, 
                              uint32_t* framebuffer,
                              int fb_width, 
                              int fb_height) {
    // Skip empty tiles
    if (!tile.has_content) {
        return;
    }
    
    // Calculate tile position in framebuffer
    int tile_start_x = tile.tile_x * TILE_SIZE;
    int tile_start_y = tile.tile_y * TILE_SIZE;
    int tile_end_x = std::min(tile_start_x + TILE_SIZE, fb_width);
    int tile_end_y = std::min(tile_start_y + TILE_SIZE, fb_height);
    
    // Copy each valid pixel from tile buffer to framebuffer
    for (int y = tile_start_y; y < tile_end_y; ++y) {
        for (int x = tile_start_x; x < tile_end_x; ++x) {
            // Tile-local coordinates
            int local_x = x - tile_start_x;
            int local_y = y - tile_start_y;
            int pixel_index = local_y * TILE_SIZE + local_x;
            
            const TilePixel& src = tile.pixels[pixel_index];
            if (src.valid) {
                // Pack RGBA into 32-bit value
                int fb_index = y * fb_width + x;
                framebuffer[fb_index] = (src.a << 24) | (src.b << 16) | (src.g << 8) | src.r;
            }
        }
    }
}

} // namespace ParallelTile