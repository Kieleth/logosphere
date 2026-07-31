#ifndef PARALLEL_TILE_PROCESSOR_H
#define PARALLEL_TILE_PROCESSOR_H

#include <vector>
#include <cstdint>
#include "particle.h"

/**
 * ParallelTileProcessor - Thread-safe tile processing for parallel rendering
 * 
 * DESIGN PRINCIPLES (2025-01-22):
 * ================================
 * 1. PURE FUNCTION: No global state, all data passed as parameters
 * 2. THREAD ISOLATION: Each tile has its own depth buffer
 * 3. OUTPUT BUFFER: Results written to tile-local buffer, not global framebuffer
 * 4. KISS PRINCIPLE: Simple, straightforward, no clever tricks
 * 
 * THREADING ARCHITECTURE:
 * ======================
 * - Each thread calls process_tile_pure() with a tile index
 * - Function allocates its own TileBuffer on stack (no heap allocation)
 * - Processes all surfaces for that tile into local buffer
 * - Returns buffer to be copied to framebuffer by coordinator
 * 
 * MEMORY LAYOUT:
 * =============
 * TileBuffer (16x16 pixels):
 * - Color: 256 pixels x 4 bytes = 1KB
 * - Depth: 256 pixels x 4 bytes = 1KB  
 * - Total: 2KB per tile (fits in L1 cache)
 */

// Forward declarations to avoid dependencies
class CameraSystem;
class LightSystem;
class IPixelShader;

namespace ParallelTile {

// Constants for tile processing
constexpr int TILE_SIZE = 16;  // Must match optimization_flags.h
constexpr int MAX_PIXELS_PER_TILE = TILE_SIZE * TILE_SIZE;

/**
 * TilePixel - Single pixel data within a tile
 * Packed structure for cache efficiency
 */
struct TilePixel {
    uint8_t r, g, b, a;     // Color (4 bytes)
    float depth;             // Depth value (4 bytes)
    uint32_t object_id;      // Object ID for picking (4 bytes)
    bool valid;              // Whether pixel was written (1 byte)
    // Padding for alignment (3 bytes)
    uint8_t _padding[3];
};
static_assert(sizeof(TilePixel) == 16, "TilePixel must be 16 bytes for alignment");

/**
 * TileBuffer - Complete rendered tile data
 * Stack-allocated, no dynamic memory
 */
struct alignas(64) TileBuffer {  // Cache line aligned
    TilePixel pixels[MAX_PIXELS_PER_TILE];
    int tile_x;              // Tile coordinate in grid
    int tile_y;
    int pixels_written;      // Count of valid pixels
    bool has_content;        // Whether any pixels were written
    
    // Constructor for initialization
    TileBuffer() : tile_x(0), tile_y(0), pixels_written(0), has_content(false) {
        // Initialize all pixels as invalid
        for (int i = 0; i < MAX_PIXELS_PER_TILE; ++i) {
            pixels[i].valid = false;
            pixels[i].depth = 1.0f;  // Far plane
        }
    }
};

/**
 * TileInput - Read-only input data for tile processing
 * All pointers to shared read-only data
 */
struct TileInput {
    // Tile identification
    int tile_x;
    int tile_y;
    
    // Screen dimensions
    int screen_width;
    int screen_height;
    
    // Surface data for this tile
    const std::vector<const void*>* surfaces;  // Pointers to SurfaceData
    
    // Read-only system references (these will need proper types)
    const void* camera_system;
    const void* light_system;
    const void* pixel_shader;
    
    // Camera position (cached for performance)
    float cam_x, cam_y, cam_z;
};

/**
 * process_tile_pure - Thread-safe tile processing function
 * 
 * This is THE function that will be called by worker threads.
 * It's completely pure: given the same input, produces the same output.
 * No side effects, no global state access.
 * 
 * @param input Read-only input data for this tile
 * @param output Pre-allocated output buffer (stack allocated by caller)
 * @return Number of pixels processed
 * 
 * THREAD SAFETY:
 * - Input is read-only shared data (safe)
 * - Output is thread-local buffer (safe)
 * - No static variables or global state
 * - All allocations on stack
 */
int process_tile_pure(const TileInput& input, TileBuffer& output);

/**
 * copy_tile_to_framebuffer - Copy tile buffer to global framebuffer
 * 
 * This function handles the final copy from thread-local tile buffer
 * to the global framebuffer. Should be called by main thread or
 * with proper synchronization.
 * 
 * @param tile Completed tile buffer
 * @param framebuffer Global framebuffer pointer
 * @param fb_width Framebuffer width
 * @param fb_height Framebuffer height
 * 
 * SYNCHRONIZATION:
 * - Only called when tile processing is complete
 * - No two threads write to same framebuffer region
 * - Can be parallelized if tiles don't overlap
 */
void copy_tile_to_framebuffer(const TileBuffer& tile, 
                              uint32_t* framebuffer,
                              int fb_width, 
                              int fb_height);

} // namespace ParallelTile

#endif // PARALLEL_TILE_PROCESSOR_H