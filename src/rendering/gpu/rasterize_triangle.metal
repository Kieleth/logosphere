// rasterize_triangle.metal
// GPU compute kernel for triangle rasterization
//
// PHASE III: Full GPU Rasterization - Step by step implementation
// STEP 1: Minimal kernel - write 1 pixel for 1 hardcoded triangle
// STEP 2: Full triangle rasterization - all pixels in bbox
// KISS: Prove GPU can write to framebuffer, then iterate
//
// ARCHITECTURE (matches CPU):
// - CPU: Uses PixelBuffer (RGBA uint8_t) + DepthBuffer (float)
// - GPU: Writes to same buffers via Metal compute
// - Integration point: Same as MetalComputeBridge pattern
//
// CRITICAL: Math matches CPU exactly (surface_rasterizer.cpp, rasterization_math.h)
// - Edge equation: a = y1-y0, b = x0-x1, c = x1*y0-x0*y1
// - Edge evaluation: value = a*px + b*py + c
// - Inside test: ALL 3 edges >= 0

#include <metal_stdlib>
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gpu_math.metal"  // For ray intersection functions (BVH shadow rays)
#include "depth_encoding.metal"
using namespace metal;

// =========================================================================
// STEP 1: MINIMAL GPU RASTERIZATION KERNEL
// =========================================================================
// Goal: Write 1 red pixel at (150, 150) to prove GPU can write
// Matches CPU PixelBuffer format: RGBA as uint8_t (packed into uint32_t)

kernel void rasterize_minimal(
    device uint32_t* pixel_buffer [[buffer(0)]],  // RGBA pixels (BGRA packed, matches PixelBuffer)
    constant uint& width [[buffer(1)]],           // Framebuffer width
    constant uint& height [[buffer(2)]],          // Framebuffer height
    uint2 gid [[thread_position_in_grid]])        // Pixel coordinates (x, y)
{
    // STEP 1: Hardcoded test - write red pixel at (150, 150)
    // CPU format: PixelBuffer stores BGRA as uint32_t (macOS native format)
    if (gid.x == 150 && gid.y == 150) {
        uint pixel_index = gid.y * width + gid.x;
        // BGRA format in little-endian: addr+0=B, addr+1=G, addr+2=R, addr+3=A
        // Red pixel: B=0, G=0, R=255, A=255
        // Memory: 00 00 FF FF → uint32_t = 0xFFFF0000
        pixel_buffer[pixel_index] = 0xFFFF0000;
    }
}

// =========================================================================
// STEP 4: DEPTH BUFFER WITH ATOMIC OPERATIONS
// =========================================================================
// Goal: Handle overlapping triangles with proper depth testing
// Uses Metal atomic operations for thread-safe depth buffer updates

kernel void rasterize_triangle_with_depth(
    device uint32_t* pixel_buffer [[buffer(0)]],
    device atomic_int* depth_buffer [[buffer(1)]],   // Atomic depth buffer (SIGNED)
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant float* triangle_verts [[buffer(4)]],    // 9 floats: (x0,y0,z0, x1,y1,z1, x2,y2,z2)
    constant float4& color [[buffer(5)]],            // RGBA color
    uint2 gid [[thread_position_in_grid]])
{
    // Get triangle vertices (screen space 2D + depth)
    float x0 = triangle_verts[0];
    float y0 = triangle_verts[1];
    float z0 = triangle_verts[2];
    float x1 = triangle_verts[3];
    float y1 = triangle_verts[4];
    float z1 = triangle_verts[5];
    float x2 = triangle_verts[6];
    float y2 = triangle_verts[7];
    float z2 = triangle_verts[8];

    // Compute bounding box (2D only)
    int min_x = (int)min(x0, min(x1, x2));
    int max_x = (int)max(x0, max(x1, x2));
    int min_y = (int)min(y0, min(y1, y2));
    int max_y = (int)max(y0, max(y1, y2));

    // Clip to framebuffer bounds
    min_x = max(min_x, 0);
    max_x = min(max_x, (int)width - 1);
    min_y = max(min_y, 0);
    max_y = min(max_y, (int)height - 1);

    int px = (int)gid.x;
    int py = (int)gid.y;

    if (px < min_x || px > max_x || py < min_y || py > max_y) {
        return;
    }

    // Compute edge equation coefficients (2D)
    float a0 = y1 - y0;
    float b0 = x0 - x1;
    float c0 = x1 * y0 - x0 * y1;

    float a1 = y2 - y1;
    float b1 = x1 - x2;
    float c1 = x2 * y1 - x1 * y2;

    float a2 = y0 - y2;
    float b2 = x2 - x0;
    float c2 = x0 * y2 - x2 * y0;

    // Negate edge coefficients (matches CPU)
    a0 = -a0; b0 = -b0; c0 = -c0;
    a1 = -a1; b1 = -b1; c1 = -c1;
    a2 = -a2; b2 = -b2; c2 = -c2;

    // Use integer pixel coordinates (matches CPU)
    float px_float = float(px);
    float py_float = float(py);

    float edge0 = a0 * px_float + b0 * py_float + c0;
    float edge1 = a1 * px_float + b1 * py_float + c1;
    float edge2 = a2 * px_float + b2 * py_float + c2;

    // Two-sided inside test — accepts either screen winding. The
    // single-sided `>= 0` variant silently dropped front-facing triangles
    // under iso projection's Y-inversion. See rasterize_gbuffer.metal for
    // the diagnosis writeup; mirror this pattern in every kernel that does
    // its own edge-equation test.
    if ((edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
        (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f)) {
        // Compute barycentric coordinates for depth interpolation
        float v0x = x1 - x0;
        float v0y = y1 - y0;
        float v1x = x2 - x0;
        float v1y = y2 - y0;

        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;
        float denom = d00 * d11 - d01 * d01;

        float u = 0.5f;
        float v = 0.5f;

        if (denom >= 0.001f || denom <= -0.001f) {
            float inv_denom = 1.0f / denom;
            float v2x = px_float - x0;
            float v2y = py_float - y0;
            float d20 = v2x * v0x + v2y * v0y;
            float d21 = v2x * v1x + v2y * v1y;
            u = (d11 * d20 - d01 * d21) * inv_denom;
            v = (d00 * d21 - d01 * d20) * inv_denom;
        }

        float w = 1.0f - u - v;

        // Interpolate depth and encode as SIGNED int32.
        float depth = w * z0 + u * z1 + v * z2;
        int depth_enc = encode_depth(depth);

        uint pixel_index = py * width + px;

        int old_depth = atomic_load_explicit(&depth_buffer[pixel_index], memory_order_relaxed);

        bool depth_test_passes = false;
        if (old_depth == DEPTH_INIT_VALUE) {
            depth_test_passes = true;
        } else if (old_depth > DEPTH_INIT_VALUE - DEPTH_EPSILON_ENCODED) {
            depth_test_passes = (depth_enc <= old_depth);
        } else {
            depth_test_passes = (depth_enc <= old_depth + DEPTH_EPSILON_ENCODED);
        }

        if (depth_test_passes) {
            atomic_store_explicit(&depth_buffer[pixel_index], depth_enc, memory_order_relaxed);

            // Write pixel
            uint b = (uint)(color.b * 255.0f);
            uint g = (uint)(color.g * 255.0f);
            uint r = (uint)(color.r * 255.0f);
            uint a = (uint)(color.a * 255.0f);

            uint32_t bgra = (a << 24) | (r << 16) | (g << 8) | b;
            pixel_buffer[pixel_index] = bgra;
        }

    }
}

// =========================================================================
// STEP 5: MULTIPLE TRIANGLES BATCH RASTERIZATION
// =========================================================================
// Goal: Rasterize N triangles in single GPU call
// Each thread (pixel) loops over all triangles and renders closest one

kernel void rasterize_triangles_batch(
    device uint32_t* pixel_buffer [[buffer(0)]],
    device atomic_int* depth_buffer [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant TriangleGPU* triangles [[buffer(4)]],  // Array of triangles
    constant uint& triangle_count [[buffer(5)]],     // Number of triangles
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    // Bounds check
    if (px >= (int)width || py >= (int)height) {
        return;
    }

    uint pixel_index = py * width + px;
    float px_float = float(px);
    float py_float = float(py);

    // Loop over all triangles
    for (uint tri_idx = 0; tri_idx < triangle_count; ++tri_idx) {
        TriangleGPU tri = triangles[tri_idx];

        // Get triangle vertices
        float x0 = tri.x0, y0 = tri.y0, z0 = tri.z0;
        float x1 = tri.x1, y1 = tri.y1, z1 = tri.z1;
        float x2 = tri.x2, y2 = tri.y2, z2 = tri.z2;

        // Compute bounding box
        int min_x = (int)min(x0, min(x1, x2));
        int max_x = (int)max(x0, max(x1, x2));
        int min_y = (int)min(y0, min(y1, y2));
        int max_y = (int)max(y0, max(y1, y2));

        // Early reject if pixel outside triangle bbox
        if (px < min_x || px > max_x || py < min_y || py > max_y) {
            continue;
        }

        // Compute edge equations
        float a0 = y1 - y0;
        float b0 = x0 - x1;
        float c0 = x1 * y0 - x0 * y1;

        float a1 = y2 - y1;
        float b1 = x1 - x2;
        float c1 = x2 * y1 - x1 * y2;

        float a2 = y0 - y2;
        float b2 = x2 - x0;
        float c2 = x0 * y2 - x2 * y0;

        // Negate coefficients (matches CPU)
        a0 = -a0; b0 = -b0; c0 = -c0;
        a1 = -a1; b1 = -b1; c1 = -c1;
        a2 = -a2; b2 = -b2; c2 = -c2;

        // Evaluate edges at pixel
        float edge0 = a0 * px_float + b0 * py_float + c0;
        float edge1 = a1 * px_float + b1 * py_float + c1;
        float edge2 = a2 * px_float + b2 * py_float + c2;

        // Inside test
        if ((edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
        (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f)) {
            // Compute barycentric coordinates for depth
            float v0x = x1 - x0;
            float v0y = y1 - y0;
            float v1x = x2 - x0;
            float v1y = y2 - y0;

            float d00 = v0x * v0x + v0y * v0y;
            float d01 = v0x * v1x + v0y * v1y;
            float d11 = v1x * v1x + v1y * v1y;
            float denom = d00 * d11 - d01 * d01;

            float u = 0.5f;
            float v = 0.5f;

            if (denom >= 0.001f || denom <= -0.001f) {
                float inv_denom = 1.0f / denom;
                float v2x = px_float - x0;
                float v2y = py_float - y0;
                float d20 = v2x * v0x + v2y * v0y;
                float d21 = v2x * v1x + v2y * v1y;
                u = (d11 * d20 - d01 * d21) * inv_denom;
                v = (d00 * d21 - d01 * d20) * inv_denom;
            }

            float w = 1.0f - u - v;

            // Signed depth encoding — see depth_encoding.metal.
            float depth = w * z0 + u * z1 + v * z2;
            int depth_enc = encode_depth(depth);

            int old_depth = atomic_load_explicit(&depth_buffer[pixel_index], memory_order_relaxed);

            bool depth_test_passes = false;
            if (old_depth == DEPTH_INIT_VALUE) {
                depth_test_passes = true;
            } else if (old_depth > DEPTH_INIT_VALUE - DEPTH_EPSILON_ENCODED) {
                depth_test_passes = (depth_enc <= old_depth);
            } else {
                depth_test_passes = (depth_enc <= old_depth + DEPTH_EPSILON_ENCODED);
            }

            if (depth_test_passes) {
                atomic_store_explicit(&depth_buffer[pixel_index], depth_enc, memory_order_relaxed);

                // Write pixel
                uint b_val = (uint)(tri.b * 255.0f);
                uint g_val = (uint)(tri.g * 255.0f);
                uint r_val = (uint)(tri.r * 255.0f);
                uint a_val = (uint)(tri.a * 255.0f);

                uint32_t bgra = (a_val << 24) | (r_val << 16) | (g_val << 8) | b_val;
                pixel_buffer[pixel_index] = bgra;
            }
        }
    }
}

// =========================================================================
// STEP 3: BARYCENTRIC INTERPOLATION TEST
// =========================================================================
// Goal: Visualize barycentric coordinates as RGB (w=R, u=G, v=B)
// Verifies barycentric math matches CPU before adding depth

kernel void rasterize_triangle_barycentric(
    device uint32_t* pixel_buffer [[buffer(0)]],
    constant uint& width [[buffer(1)]],
    constant uint& height [[buffer(2)]],
    constant float* triangle_verts [[buffer(3)]],
    uint2 gid [[thread_position_in_grid]])
{
    // Get triangle vertices (screen space 2D)
    float x0 = triangle_verts[0];
    float y0 = triangle_verts[1];
    float x1 = triangle_verts[2];
    float y1 = triangle_verts[3];
    float x2 = triangle_verts[4];
    float y2 = triangle_verts[5];

    // Compute bounding box
    int min_x = (int)min(x0, min(x1, x2));
    int max_x = (int)max(x0, max(x1, x2));
    int min_y = (int)min(y0, min(y1, y2));
    int max_y = (int)max(y0, max(y1, y2));

    // Clip to framebuffer bounds
    min_x = max(min_x, 0);
    max_x = min(max_x, (int)width - 1);
    min_y = max(min_y, 0);
    max_y = min(max_y, (int)height - 1);

    int px = (int)gid.x;
    int py = (int)gid.y;

    if (px < min_x || px > max_x || py < min_y || py > max_y) {
        return;
    }

    // Compute edge equation coefficients
    float a0 = y1 - y0;
    float b0 = x0 - x1;
    float c0 = x1 * y0 - x0 * y1;

    float a1 = y2 - y1;
    float b1 = x1 - x2;
    float c1 = x2 * y1 - x1 * y2;

    float a2 = y0 - y2;
    float b2 = x2 - x0;
    float c2 = x0 * y2 - x2 * y0;

    // Negate edge coefficients (matches CPU)
    a0 = -a0; b0 = -b0; c0 = -c0;
    a1 = -a1; b1 = -b1; c1 = -c1;
    a2 = -a2; b2 = -b2; c2 = -c2;

    // CRITICAL: CPU uses INTEGER pixel coordinates, not pixel centers!
    // See surface_rasterizer.cpp:1052 and :1068 - uses float(x) and float(y)
    float px_float = float(px);
    float py_float = float(py);

    float edge0 = a0 * px_float + b0 * py_float + c0;
    float edge1 = a1 * px_float + b1 * py_float + c1;
    float edge2 = a2 * px_float + b2 * py_float + c2;

    // Inside test
    if ((edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
        (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f)) {
        // Compute barycentric coordinates (matches CPU: simd_uv.h)
        float v0x = x1 - x0;
        float v0y = y1 - y0;
        float v1x = x2 - x0;
        float v1y = y2 - y0;

        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;

        float denom = d00 * d11 - d01 * d01;

        float u = 0.5f;
        float v = 0.5f;

        if (denom >= 0.001f || denom <= -0.001f) {
            float inv_denom = 1.0f / denom;

            // Vector from vertex 0 to pixel (use integer coordinates)
            float v2x = px_float - x0;
            float v2y = py_float - y0;

            float d20 = v2x * v0x + v2y * v0y;
            float d21 = v2x * v1x + v2y * v1y;

            u = (d11 * d20 - d01 * d21) * inv_denom;
            v = (d00 * d21 - d01 * d20) * inv_denom;
        }

        float w = 1.0f - u - v;

        // Write w, u, v as R, G, B
        uint r_val = (uint)clamp(w * 255.0f, 0.0f, 255.0f);
        uint g_val = (uint)clamp(u * 255.0f, 0.0f, 255.0f);
        uint b_val = (uint)clamp(v * 255.0f, 0.0f, 255.0f);

        uint32_t bgra = (255 << 24) | (r_val << 16) | (g_val << 8) | b_val;
        uint pixel_index = py * width + px;
        pixel_buffer[pixel_index] = bgra;
    }
}

// =========================================================================
// STEP 2: FULL TRIANGLE RASTERIZATION
// =========================================================================
// Goal: Rasterize all pixels inside triangle bbox
// Matches CPU: surface_rasterizer.cpp edge equation logic

kernel void rasterize_triangle(
    device uint32_t* pixel_buffer [[buffer(0)]],  // BGRA pixels
    constant uint& width [[buffer(1)]],           // Framebuffer width
    constant uint& height [[buffer(2)]],          // Framebuffer height
    constant float* triangle_verts [[buffer(3)]], // 6 floats: (x0,y0, x1,y1, x2,y2)
    constant float4& color [[buffer(4)]],         // RGBA color (0-1)
    uint2 gid [[thread_position_in_grid]])        // Pixel coordinates
{
    // Get triangle vertices (screen space 2D)
    float x0 = triangle_verts[0];
    float y0 = triangle_verts[1];
    float x1 = triangle_verts[2];
    float y1 = triangle_verts[3];
    float x2 = triangle_verts[4];
    float y2 = triangle_verts[5];

    // Compute bounding box (matches CPU: rasterization_math.h::compute_bbox_2d)
    int min_x = (int)min(x0, min(x1, x2));
    int max_x = (int)max(x0, max(x1, x2));
    int min_y = (int)min(y0, min(y1, y2));
    int max_y = (int)max(y0, max(y1, y2));

    // Clip to framebuffer bounds
    min_x = max(min_x, 0);
    max_x = min(max_x, (int)width - 1);
    min_y = max(min_y, 0);
    max_y = min(max_y, (int)height - 1);

    // Get pixel coordinates
    int px = (int)gid.x;
    int py = (int)gid.y;

    // Early exit if outside bbox
    if (px < min_x || px > max_x || py < min_y || py > max_y) {
        return;
    }

    // Compute edge equation coefficients (matches CPU: rasterization_math.h)
    // Edge 0: v0 → v1
    float a0 = y1 - y0;
    float b0 = x0 - x1;
    float c0 = x1 * y0 - x0 * y1;

    // Edge 1: v1 → v2
    float a1 = y2 - y1;
    float b1 = x1 - x2;
    float c1 = x2 * y1 - x1 * y2;

    // Edge 2: v2 → v0
    float a2 = y0 - y2;
    float b2 = x2 - x0;
    float c2 = x0 * y2 - x2 * y0;

    // CRITICAL: Negate edge coefficients (matches CPU: surface_rasterizer.cpp:1041-1043)
    // The edge equations have opposite sign convention than test_inside expects
    a0 = -a0; b0 = -b0; c0 = -c0;
    a1 = -a1; b1 = -b1; c1 = -c1;
    a2 = -a2; b2 = -b2; c2 = -c2;

    // CRITICAL: CPU uses INTEGER pixel coordinates, not pixel centers!
    // See surface_rasterizer.cpp:1052 and :1068 - uses float(x) and float(y)
    float px_float = float(px);
    float py_float = float(py);

    float edge0 = a0 * px_float + b0 * py_float + c0;
    float edge1 = a1 * px_float + b1 * py_float + c1;
    float edge2 = a2 * px_float + b2 * py_float + c2;

    // Inside test (matches CPU: ALL edges >= 0)
    if ((edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
        (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f)) {
        // STEP 3: Compute barycentric coordinates (matches CPU: simd_uv.h)
        // Using dot product method (same as CPU compute_uv_batch_scalar)

        // Vectors from vertex 0 to vertices 1 and 2
        float v0x = x1 - x0;
        float v0y = y1 - y0;
        float v1x = x2 - x0;
        float v1y = y2 - y0;

        // Triangle-specific dot products
        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;

        // Denominator
        float denom = d00 * d11 - d01 * d01;

        // Barycentric coordinates u, v (default for degenerate triangle)
        float u = 0.5f;
        float v = 0.5f;

        if (denom >= 0.001f || denom <= -0.001f) {
            float inv_denom = 1.0f / denom;

            // Vector from vertex 0 to pixel (use integer coordinates)
            float v2x = px_float - x0;
            float v2y = py_float - y0;

            // Pixel-specific dot products
            float d20 = v2x * v0x + v2y * v0y;
            float d21 = v2x * v1x + v2y * v1y;

            // Barycentric coordinates (matches CPU: simd_uv.h:81-82)
            u = (d11 * d20 - d01 * d21) * inv_denom;
            v = (d00 * d21 - d01 * d20) * inv_denom;
        }

        // Weight for vertex 0
        float w = 1.0f - u - v;

        // Convert RGBA float4 (0-1) to BGRA uint32_t (0-255)
        uint b = (uint)(color.b * 255.0f);
        uint g = (uint)(color.g * 255.0f);
        uint r = (uint)(color.r * 255.0f);
        uint a = (uint)(color.a * 255.0f);

        // Pack to BGRA format (little-endian)
        uint32_t bgra = (a << 24) | (r << 16) | (g << 8) | b;

        // Write pixel
        uint pixel_index = py * width + px;
        pixel_buffer[pixel_index] = bgra;
    }
}

// =========================================================================
// STEP 7: LIGHTING INTEGRATION
// =========================================================================
// Tone mapping: Zone System (matches CPU lighting_config.h::tone_map_zone_system)
// Maps lux values to RGB 0-255 using Ansel Adams-inspired zone system
inline int tone_map_zone_system(float raw_intensity_lux) {
    // Zone boundaries (lux values)
    const float shadow_threshold = 10.0f;     // 0-10 lux = Zone I-III
    const float midtone_threshold = 100.0f;   // 10-100 lux = Zone IV-VI

    // Zone boundaries in RGB space
    const float shadow_rgb_max = 75.0f;       // Shadows: 0-75 RGB
    const float midtone_rgb_max = 200.0f;     // Midtones: 75-200 RGB
                                              // Highlights: 200-255 RGB

    // Ensure true darkness - 0 lux = 0 RGB
    if (raw_intensity_lux < 0.001f) {
        return 0;
    }

    if (raw_intensity_lux <= shadow_threshold) {
        // Zone I-III: Deep shadows (0-10 lux → 0-75 RGB)
        float shadow_ratio = raw_intensity_lux / shadow_threshold;
        return (int)(shadow_ratio * shadow_rgb_max);

    } else if (raw_intensity_lux <= midtone_threshold) {
        // Zone IV-VI: Normal lighting (10-100 lux → 75-200 RGB)
        float midtone_ratio = (raw_intensity_lux - shadow_threshold) /
                             (midtone_threshold - shadow_threshold);
        float midtone_range = midtone_rgb_max - shadow_rgb_max;
        return (int)(shadow_rgb_max + midtone_ratio * midtone_range);

    } else {
        // Zone VII-X: Bright highlights (100+ lux → 200-255 RGB)
        const float highlight_multiplier = 0.55f;
        float highlight_excess = raw_intensity_lux - midtone_threshold;
        float compressed_excess = highlight_excess * highlight_multiplier;
        float highlight_range = 255.0f - midtone_rgb_max;
        int result = (int)(midtone_rgb_max + min(compressed_excess, highlight_range));
        return min(result, 255);
    }
}

// STEP 7: Rasterize triangles with GPU lighting + BVH shadows
// Combines rasterization + lighting computation + shadow ray tracing
// OPTIMIZATION: Tile-based binning to reduce bandwidth (120x reduction expected)
kernel void rasterize_triangles_lit(
    device uint32_t* pixel_buffer [[buffer(0)]],
    device atomic_int* depth_buffer [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant TriangleLit* triangles [[buffer(4)]],     // Triangles with lighting data
    constant uint& triangle_count [[buffer(5)]],
    constant LightData* lights [[buffer(6)]],          // Light sources
    constant uint& light_count [[buffer(7)]],
    constant BVHNode* bvh_nodes [[buffer(8)]],         // BVH tree for shadow rays
    constant uint& bvh_node_count [[buffer(9)]],       // Number of BVH nodes
    constant Triangle* bvh_triangles [[buffer(10)]],   // Shadow geometry triangles
    constant uint& bvh_triangle_count [[buffer(11)]],  // Number of shadow triangles
    // Tile binning optimization (buffers 12-16)
    constant uint* tile_indices [[buffer(12)]],        // Flattened triangle indices
    constant uint* tile_offsets [[buffer(13)]],        // Offset per tile into tile_indices
    constant uint* tile_counts [[buffer(14)]],         // Triangle count per tile
    constant uint& tiles_x [[buffer(15)]],             // Tile grid width
    constant uint& tiles_y [[buffer(16)]],             // Tile grid height
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    // Bounds check
    if (px >= (int)width || py >= (int)height) {
        return;
    }

    uint pixel_index = py * width + px;
    float px_float = float(px);
    float py_float = float(py);

    // TILE-BASED OPTIMIZATION: Only test triangles assigned to this pixel's tile
    // Without binning: test ALL 7,200 triangles (12.1B tests/frame, 1,900 GB bandwidth)
    // With binning: test ~20 triangles per tile (100M tests/frame, 16 GB bandwidth)
    // Expected: 120x reduction in triangle tests and memory bandwidth

    uint tri_start = 0;
    uint tri_end = triangle_count;

    if (tile_indices && tile_offsets && tile_counts && tiles_x > 0 && tiles_y > 0) {
        // Tile binning enabled - use per-tile triangle lists
        // Compute which tile this pixel belongs to (matches CPU: tile_idx = ty * tiles_x + tx)
        const int TILE_SIZE = 64;  // Must match GPU_BINNING_TILE_SIZE in optimization_flags.h
        int tile_x = px / TILE_SIZE;
        int tile_y = py / TILE_SIZE;
        int tile_idx = tile_y * tiles_x + tile_x;

        // Bounds check tile index
        int total_tiles = tiles_x * tiles_y;
        if (tile_idx >= 0 && tile_idx < total_tiles) {
            // Get this tile's triangle list range
            uint offset = tile_offsets[tile_idx];
            uint count = tile_counts[tile_idx];
            tri_start = offset;
            tri_end = offset + count;
        } else {
            // Pixel outside valid tiles - skip rendering
            return;
        }
    }
    // else: Binning disabled - test all triangles (naive path)

    // Loop over triangles assigned to this pixel's tile
    // Naive: tri_start=0, tri_end=triangle_count (all triangles)
    // Tiled: tri_start=offset, tri_end=offset+count (only overlapping triangles)
    for (uint list_idx = tri_start; list_idx < tri_end; ++list_idx) {
        // Get actual triangle index from tile list (or use index directly if naive)
        uint tri_idx = (tile_indices && tile_offsets && tile_counts) ? tile_indices[list_idx] : list_idx;

        TriangleLit tri = triangles[tri_idx];

        // Get triangle screen vertices
        float x0 = tri.x0, y0 = tri.y0, z0 = tri.z0;
        float x1 = tri.x1, y1 = tri.y1, z1 = tri.z1;
        float x2 = tri.x2, y2 = tri.y2, z2 = tri.z2;

        // Compute bounding box
        int min_x = (int)min(x0, min(x1, x2));
        int max_x = (int)max(x0, max(x1, x2));
        int min_y = (int)min(y0, min(y1, y2));
        int max_y = (int)max(y0, max(y1, y2));

        // Early reject if pixel outside triangle bbox
        if (px < min_x || px > max_x || py < min_y || py > max_y) {
            continue;
        }

        // Compute edge equations
        float a0 = y1 - y0;
        float b0 = x0 - x1;
        float c0 = x1 * y0 - x0 * y1;

        float a1 = y2 - y1;
        float b1 = x1 - x2;
        float c1 = x2 * y1 - x1 * y2;

        float a2 = y0 - y2;
        float b2 = x2 - x0;
        float c2 = x0 * y2 - x2 * y0;

        // Negate coefficients (matches CPU)
        a0 = -a0; b0 = -b0; c0 = -c0;
        a1 = -a1; b1 = -b1; c1 = -c1;
        a2 = -a2; b2 = -b2; c2 = -c2;

        // Evaluate edges at pixel
        float edge0 = a0 * px_float + b0 * py_float + c0;
        float edge1 = a1 * px_float + b1 * py_float + c1;
        float edge2 = a2 * px_float + b2 * py_float + c2;

        // Inside test
        if ((edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
        (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f)) {
            // Compute barycentric coordinates
            float v0x = x1 - x0;
            float v0y = y1 - y0;
            float v1x = x2 - x0;
            float v1y = y2 - y0;

            float d00 = v0x * v0x + v0y * v0y;
            float d01 = v0x * v1x + v0y * v1y;
            float d11 = v1x * v1x + v1y * v1y;
            float denom = d00 * d11 - d01 * d01;

            float u = 0.5f;
            float v = 0.5f;

            if (denom >= 0.001f || denom <= -0.001f) {
                float inv_denom = 1.0f / denom;
                float v2x = px_float - x0;
                float v2y = py_float - y0;
                float d20 = v2x * v0x + v2y * v0y;
                float d21 = v2x * v1x + v2y * v1y;
                u = (d11 * d20 - d01 * d21) * inv_denom;
                v = (d00 * d21 - d01 * d20) * inv_denom;
            }

            float w = 1.0f - u - v;

            // Signed depth encoding — see depth_encoding.metal.
            float depth = w * z0 + u * z1 + v * z2;
            int depth_enc = encode_depth(depth);

            int old_depth = atomic_load_explicit(&depth_buffer[pixel_index], memory_order_relaxed);

            bool depth_test_passes = false;
            if (old_depth == DEPTH_INIT_VALUE) {
                depth_test_passes = true;
            } else if (old_depth > DEPTH_INIT_VALUE - DEPTH_EPSILON_ENCODED) {
                depth_test_passes = (depth_enc <= old_depth);
            } else {
                depth_test_passes = (depth_enc <= old_depth + DEPTH_EPSILON_ENCODED);
            }

            if (depth_test_passes) {
                atomic_store_explicit(&depth_buffer[pixel_index], depth_enc, memory_order_relaxed);

                // Interpolate world position
                float3 world_pos = w * float3(tri.world_pos0[0], tri.world_pos0[1], tri.world_pos0[2]) +
                                  u * float3(tri.world_pos1[0], tri.world_pos1[1], tri.world_pos1[2]) +
                                  v * float3(tri.world_pos2[0], tri.world_pos2[1], tri.world_pos2[2]);

                // Surface normal (flat shading - shared across triangle)
                float3 normal = float3(tri.normal[0], tri.normal[1], tri.normal[2]);

                // Compute lighting from all lights
                float total_lighting_lux = 0.0f;

                for (uint light_idx = 0; light_idx < light_count; ++light_idx) {
                    LightData light = lights[light_idx];

                    // Vector from pixel to light
                    float3 to_light = light.position - world_pos;
                    float distance_sq = dot(to_light, to_light);

                    // Check if within light radius
                    if (distance_sq <= light.emission_radius * light.emission_radius) {
                        // Normalize direction
                        float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
                        float inv_dist = rsqrt(safe_distance_sq);
                        float3 light_dir = to_light * inv_dist;

                        // Lambertian lighting (dot product with normal)
                        float lambertian = max(dot(normal, light_dir), 0.0f);

                        if (lambertian > 0.0f) {
                            // BVH Shadow ray test (Phase I integration)
                            bool in_shadow = false;

                            if (bvh_node_count > 0) {
                                // Setup shadow ray: from surface point to light
                                float3 ray_origin = world_pos + normal * 0.01f;  // Offset to avoid self-intersection
                                float3 ray_direction = light_dir;
                                float max_distance = sqrt(safe_distance_sq);

                                // Stack-based BVH traversal (from trace_shadow_rays_batched_bvh)
                                int stack[32];
                                int stack_ptr = 0;
                                stack[stack_ptr++] = 0;  // Start at root

                                while (stack_ptr > 0 && !in_shadow) {
                                    int node_idx = stack[--stack_ptr];
                                    BVHNode node = bvh_nodes[node_idx];

                                    // Test ray vs AABB
                                    if (!ray_intersects_aabb(ray_origin, ray_direction,
                                                            float3(node.bbox_min), float3(node.bbox_max))) {
                                        continue;  // Skip subtree
                                    }

                                    // Leaf? Test triangle
                                    if (node.triangle_idx >= 0) {
                                        Triangle tri = bvh_triangles[node.triangle_idx];
                                        if (ray_intersects_triangle(ray_origin, ray_direction, max_distance,
                                                                   float3(tri.v0), float3(tri.v1), float3(tri.v2))) {
                                            in_shadow = true;  // Hit! Ray blocked
                                        }
                                    } else {
                                        // Internal node - push children
                                        if (node.left_child >= 0 && stack_ptr < 32) {
                                            stack[stack_ptr++] = node.left_child;
                                        }
                                        if (node.right_child >= 0 && stack_ptr < 32) {
                                            stack[stack_ptr++] = node.right_child;
                                        }
                                    }
                                }
                            }

                            // Only accumulate light if NOT in shadow
                            if (!in_shadow) {
                                // Calculate intensity (inverse square law)
                                float intensity_lux = light.emission_strength / (FOUR_PI * safe_distance_sq);
                                total_lighting_lux += intensity_lux * lambertian;
                            }
                        }
                    }
                }

                // Apply tone mapping (zone system)
                int brightness_rgb = tone_map_zone_system(total_lighting_lux);

                // Modulate base color by lighting
                // Base color is material color (tri.r, tri.g, tri.b)
                float brightness_factor = brightness_rgb / 255.0f;
                uint r_val = (uint)(tri.r * brightness_factor * 255.0f);
                uint g_val = (uint)(tri.g * brightness_factor * 255.0f);
                uint b_val = (uint)(tri.b * brightness_factor * 255.0f);
                uint a_val = (uint)(tri.a * 255.0f);

                // Clamp to 0-255
                r_val = min(r_val, 255u);
                g_val = min(g_val, 255u);
                b_val = min(b_val, 255u);

                // Write pixel (BGRA format)
                uint32_t bgra = (a_val << 24) | (r_val << 16) | (g_val << 8) | b_val;
                pixel_buffer[pixel_index] = bgra;
            }
        }
    }
}
