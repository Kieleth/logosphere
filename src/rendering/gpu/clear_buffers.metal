#include <metal_stdlib>
using namespace metal;

// Clear framebuffer to a solid color
// Used to eliminate CPU->GPU memcpy of pre-cleared buffer
kernel void clear_framebuffer(
    device uint32_t* pixel_buffer [[buffer(0)]],  // BGRA framebuffer
    constant uint* width [[buffer(1)]],
    constant uint* height [[buffer(2)]],
    constant uint32_t* clear_color [[buffer(3)]],  // BGRA color value
    uint2 gid [[thread_position_in_grid]]
) {
    uint w = *width;
    uint h = *height;

    // Bounds check
    if (gid.x >= w || gid.y >= h) return;

    // Write clear color
    uint pixel_index = gid.y * w + gid.x;
    pixel_buffer[pixel_index] = *clear_color;
}
