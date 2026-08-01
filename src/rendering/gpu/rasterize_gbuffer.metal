//
// rasterize_gbuffer.metal
// G-Buffer rasterization for deferred rendering (Pass 1 of 3)
//
// ARCHITECTURE: Deferred Rendering - 3-pass system
// Pass 1 (THIS FILE): Rasterize geometry, output to G-buffer
// Pass 2: Trace shadow rays (coherent BVH traversal)
// Pass 3: Apply lighting (simple math with pre-computed shadows)
//
// PURPOSE: Extract rasterization from rasterize_triangles_lit, remove inline lighting
// NO shadow ray tracing here - just identify visible pixels and store surface data
//
// PERFORMANCE: ~160ms estimated (simple rasterization, no BVH)

#include <metal_stdlib>
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gbuffer_types.metal"
#include "depth_encoding.metal"
using namespace metal;

// =========================================================================
// PASS 1: G-BUFFER RASTERIZATION (Geometry Only)
// =========================================================================
// Rasterize triangles and store surface data for visible pixels
// NO lighting, NO shadows - just identify what's visible
//
// INPUT: Triangles with world positions and normals
// OUTPUT: G-buffer (world pos, normal, color, particle ID) for visible pixels only

kernel void rasterize_gbuffer(
    device GBufferPixel* gbuffer [[buffer(0)]],           // G-buffer output
    device atomic_int* depth_buffer [[buffer(1)]],        // Depth buffer (SIGNED — see depth_encoding.metal)
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant TriangleLit* triangles [[buffer(4)]],        // Triangles with lighting data
    constant uint& triangle_count [[buffer(5)]],
    // Tile binning optimization (same as rasterize_triangles_lit)
    constant uint* tile_indices [[buffer(6)]],            // Flattened triangle indices
    constant uint* tile_offsets [[buffer(7)]],            // Offset per tile
    constant uint* tile_counts [[buffer(8)]],             // Triangle count per tile
    constant uint& tiles_x [[buffer(9)]],                 // Tile grid width
    constant uint& tiles_y [[buffer(10)]],                // Tile grid height
#if RASTER_BBOX_STREAM
    constant int4* tri_bboxes [[buffer(11)]],             // Precomputed screen bboxes (min_x,min_y,max_x,max_y)
#endif
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

    // Initialize G-buffer pixel to background/sky
    GBufferPixel pixel;
    pixel.world_pos = float3(0.0f, 0.0f, 0.0f);
    pixel.normal = float3(0.0f, 0.0f, 1.0f);
    pixel.base_color = uchar4(10, 10, 15, 255);  // Dark background
    pixel.particle_id = GBUFFER_SKY_ID;          // UINT_MAX = sky


    // TILE-BASED OPTIMIZATION: Only test triangles assigned to this pixel's tile
    uint tri_start = 0;
    uint tri_end = triangle_count;

    if (tile_indices && tile_offsets && tile_counts && tiles_x > 0 && tiles_y > 0) {
        // Tile binning enabled
        const int TILE_SIZE = GPU_BINNING_TILE_SIZE_METAL;  // mirrored from optimization_flags.h
        int tile_x = px / TILE_SIZE;
        int tile_y = py / TILE_SIZE;
        int tile_idx = tile_y * tiles_x + tile_x;

        int total_tiles = tiles_x * tiles_y;
        if (tile_idx >= 0 && tile_idx < total_tiles) {
            uint offset = tile_offsets[tile_idx];
            uint count = tile_counts[tile_idx];
            tri_start = offset;
            tri_end = offset + count;
        } else {
            // Pixel outside valid tiles - write sky pixel and return
            gbuffer[pixel_index] = pixel;
            return;
        }
    }

    // Loop over triangles assigned to this tile
    for (uint list_idx = tri_start; list_idx < tri_end; ++list_idx) {
        uint tri_idx = (tile_indices && tile_offsets && tile_counts) ? tile_indices[list_idx] : list_idx;

#if RASTER_BBOX_STREAM
        // Reject on the packed 16-byte bbox stream; the 176-byte
        // TriangleLit loads only for bbox-passing candidates.
        int4 bb = tri_bboxes[tri_idx];
        if (px < bb.x || px > bb.z || py < bb.y || py > bb.w) {
            continue;
        }

        TriangleLit tri = triangles[tri_idx];
        float x0 = tri.x0, y0 = tri.y0, z0 = tri.z0;
        float x1 = tri.x1, y1 = tri.y1, z1 = tri.z1;
        float x2 = tri.x2, y2 = tri.y2, z2 = tri.z2;
#else
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
#endif

        // Compute edge equations (matches CPU rasterization)
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

        // Two-sided inside test — accepts either screen winding. Matches
        // the canonical primitive at gpu_rasterization_math.metal::
        // point_in_triangle_2d. The previous one-sided `edges >= 0`
        // convention silently dropped front-facing triangles under iso
        // projection's Y-inversion; the depth test correctly picks the
        // nearest triangle when both windings rasterize. Explicit 3D
        // back-face culling (for perf) belongs at the emit step, not
        // here. See tests/test_ellipsoid_outward_normals.cpp for the
        // screen-winding × 3D-facing cross-tab that pinned the bug.
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

            // Interpolate depth. Signed encoding — see depth_encoding.metal
            // for why this must never become unsigned.
            float depth = w * z0 + u * z1 + v * z2;
            int depth_enc = encode_depth(depth);

            // ATOMIC DEPTH TEST AND UPDATE (fixes race condition)
            // Use compare-and-swap loop to atomically test and update depth
            // This prevents floor tiles from overwriting closer cube triangles
            int old_depth = atomic_load_explicit(&depth_buffer[pixel_index], memory_order_relaxed);
            bool depth_test_passes = false;

            do {
                // Test if new depth wins. First-write shortcut uses the
                // shared init sentinel so the encoding is consistent
                // everywhere it's referenced.
                if (old_depth == DEPTH_INIT_VALUE) {
                    depth_test_passes = true;
                } else if (old_depth > DEPTH_INIT_VALUE - DEPTH_EPSILON_ENCODED) {
                    depth_test_passes = (depth_enc <= old_depth);   // Near "infinity", no epsilon
                } else {
                    depth_test_passes = (depth_enc <= old_depth + DEPTH_EPSILON_ENCODED);
                }

                if (!depth_test_passes) {
                    break;  // Depth test failed, exit loop
                }

                // Try to update depth atomically
                // If another thread updated first, old_depth gets new value and loop retries
            } while (!atomic_compare_exchange_weak_explicit(
                &depth_buffer[pixel_index],
                &old_depth,   // Updated with current value if CAS fails
                depth_enc,    // New value to write if CAS succeeds
                memory_order_relaxed,
                memory_order_relaxed
            ));

            // Alpha-test discard: skip fully transparent fragments (gluons, markers)
            // Also rejects semi-transparent fragments when USE_TRANSPARENCY is enabled
            // on the CPU side (they'll be rendered in the forward transparent pass instead)
            if (depth_test_passes && tri.a < 0.01f) {
                depth_test_passes = false;
                // Restore old depth so this transparent fragment doesn't block opaques
                atomic_store_explicit(&depth_buffer[pixel_index], old_depth, memory_order_relaxed);
            }

            if (depth_test_passes) {
                // Depth test passed - update G-buffer pixel data
                // Interpolate world position (for lighting in pass 2)
                pixel.world_pos = w * float3(tri.world_pos0[0], tri.world_pos0[1], tri.world_pos0[2]) +
                                 u * float3(tri.world_pos1[0], tri.world_pos1[1], tri.world_pos1[2]) +
                                 v * float3(tri.world_pos2[0], tri.world_pos2[1], tri.world_pos2[2]);

                // Normal. Flat by default: one normal for the whole triangle.
                // With smooth_enable it is derived ANALYTICALLY from the sphere
                // centre, which is exact rather than interpolated and is
                // therefore the harshest possible test of the shadow
                // terminator: shading and geometry disagree by the full facet
                // angle, and shadow rays still hit the real triangles.
                if (tri.smooth_enable > 0.5f) {
                    pixel.normal = normalize(pixel.world_pos - float3(tri.smooth_center));
                } else {
                    pixel.normal = float3(tri.normal[0], tri.normal[1], tri.normal[2]);
                }

                // Base material color (before lighting)
                pixel.base_color = uchar4(
                    (uchar)(tri.b * 255.0f),  // B
                    (uchar)(tri.g * 255.0f),  // G
                    (uchar)(tri.r * 255.0f),  // R
                    (uchar)(tri.a * 255.0f)   // A
                );

                // Particle ID (for debug/self-shadowing exclusion)
                pixel.particle_id = tri.particle_id;

                // NO lighting computation here - that's pass 3!
                // NO shadow ray tracing here - that's pass 2!
                // Just store the surface data for later processing
            }
        }
    }

    // Write G-buffer pixel (either surface data or sky)
    gbuffer[pixel_index] = pixel;
}
