//
// forward_transparent.metal
// Forward rendering pass for semi-transparent surfaces (Pass 3.5)
//
// ARCHITECTURE: Hybrid Forward+Deferred transparency
// Pass 1:   G-buffer rasterization (opaque only)
// Pass 2:   Shadow rays (opaque BVH)
// Pass 3:   Apply lighting (deferred, opaque framebuffer)
// Pass 3.5 (THIS FILE): Forward transparent rendering
// Pass 4:   Vision cone post-process
//
// PURPOSE: Render semi-transparent surfaces with inline lighting and alpha blending.
// Transparent triangles are sorted back-to-front on CPU and submitted in order.
// Each fragment is lit inline (shadow ray per light), then blended with the
// existing framebuffer (which already contains the lit opaque scene from Pass 3).
//
// PERFORMANCE: Only runs on transparent geometry (typically few surfaces).
// Forward lighting is per-fragment (slower per pixel than deferred) but the
// transparent surface count is expected to be small (<100 triangles).

#include <metal_stdlib>
#include <metal_raytracing>  // RT variant: any-hit shadow queries on the driver-built accel
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gpu_math.metal"
#include "depth_encoding.metal"
using namespace metal;
using namespace raytracing;

// Tone mapping (must match apply_lighting_deferred.metal and CPU lighting_config.h)
inline int tone_map_transparent(float raw_intensity_lux) {
    const float shadow_threshold = 10.0f;
    const float midtone_threshold = 100.0f;
    const float shadow_rgb_max = 75.0f;
    const float midtone_rgb_max = 200.0f;

    if (raw_intensity_lux < 0.001f) return 0;
    if (raw_intensity_lux <= shadow_threshold) {
        return (int)((raw_intensity_lux / shadow_threshold) * shadow_rgb_max);
    } else if (raw_intensity_lux <= midtone_threshold) {
        float midtone_ratio = (raw_intensity_lux - shadow_threshold) / (midtone_threshold - shadow_threshold);
        return (int)(shadow_rgb_max + midtone_ratio * (midtone_rgb_max - shadow_rgb_max));
    } else {
        float highlight_excess = raw_intensity_lux - midtone_threshold;
        float compressed = highlight_excess * 0.55f;
        int result = (int)(midtone_rgb_max + min(compressed, 255.0f - midtone_rgb_max));
        return min(result, 255);
    }
}

// =========================================================================
// PASS 3.5: FORWARD TRANSPARENT RENDERING
// =========================================================================
// Rasterize transparent triangles with inline lighting and alpha blending.
// Triangles are pre-sorted back-to-front on CPU — processed in submission order.
//
// INPUT:  Transparent triangles (sorted back-to-front), opaque depth buffer,
//         lights, BVH for shadow rays, existing framebuffer from Pass 3
// OUTPUT: Framebuffer with transparent surfaces blended on top

kernel void rasterize_transparent_forward(
    device uint32_t* pixel_buffer [[buffer(0)]],          // Read-write: framebuffer (already has opaque scene)
    constant int*  opaque_depth [[buffer(1)]],            // Read-only: SIGNED depth from Pass 1 (see depth_encoding.metal)
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant TriangleLit* triangles [[buffer(4)]],        // Transparent triangles (sorted back-to-front)
    constant uint& triangle_count [[buffer(5)]],
    constant LightData* lights [[buffer(6)]],
    constant uint& light_count [[buffer(7)]],
    constant BVHNode* bvh_nodes [[buffer(8)]],            // Opaque BVH for shadow rays
    constant uint& bvh_node_count [[buffer(9)]],
    constant Triangle* bvh_triangles [[buffer(10)]],
    constant uint& bvh_triangle_count [[buffer(11)]],
    constant int2& dispatch_origin [[buffer(12)]],        // Screen offset of the bounded dispatch
    uint2 gid [[thread_position_in_grid]])
{
    // The host dispatches only the union screen bbox of the transparent
    // triangles (they are few); gid is relative to that region.
    int px = dispatch_origin.x + (int)gid.x;
    int py = dispatch_origin.y + (int)gid.y;

    if (px >= (int)width || py >= (int)height) return;

    uint pixel_index = py * width + px;
    float px_float = float(px);
    float py_float = float(py);

    // Read existing opaque depth for this pixel (signed).
    int opaque_depth_val = opaque_depth[pixel_index];

    // Track blended color — start from existing framebuffer pixel
    uint32_t current_pixel = pixel_buffer[pixel_index];
    float dst_r = float((current_pixel >> 16) & 0xFF) / 255.0f;
    float dst_g = float((current_pixel >> 8) & 0xFF) / 255.0f;
    float dst_b = float(current_pixel & 0xFF) / 255.0f;
    bool any_hit = false;

    // BVH stack (shared across all triangles for this pixel)
    int stack[32];

    // Process ALL transparent triangles for this pixel (back-to-front order)
    for (uint tri_idx = 0; tri_idx < triangle_count; ++tri_idx) {
        TriangleLit tri = triangles[tri_idx];

        // Bounding box early reject
        float min_x = min(tri.x0, min(tri.x1, tri.x2));
        float max_x = max(tri.x0, max(tri.x1, tri.x2));
        float min_y = min(tri.y0, min(tri.y1, tri.y2));
        float max_y = max(tri.y0, max(tri.y1, tri.y2));

        if (px_float < min_x || px_float > max_x || py_float < min_y || py_float > max_y) {
            continue;
        }

        // Edge test (same as rasterize_gbuffer.metal)
        float a0 = -(tri.y1 - tri.y0), b0 = -(tri.x0 - tri.x1), c0 = -(tri.x1 * tri.y0 - tri.x0 * tri.y1);
        float a1 = -(tri.y2 - tri.y1), b1 = -(tri.x1 - tri.x2), c1 = -(tri.x2 * tri.y1 - tri.x1 * tri.y2);
        float a2 = -(tri.y0 - tri.y2), b2 = -(tri.x2 - tri.x0), c2 = -(tri.x0 * tri.y2 - tri.x2 * tri.y0);

        float edge0 = a0 * px_float + b0 * py_float + c0;
        float edge1 = a1 * px_float + b1 * py_float + c1;
        float edge2 = a2 * px_float + b2 * py_float + c2;

        if (edge0 < 0.0f || edge1 < 0.0f || edge2 < 0.0f) continue;

        // Barycentric coordinates
        float v0x = tri.x1 - tri.x0, v0y = tri.y1 - tri.y0;
        float v1x = tri.x2 - tri.x0, v1y = tri.y2 - tri.y0;
        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;
        float denom = d00 * d11 - d01 * d01;

        float u = 0.5f, v = 0.5f;
        if (denom >= 0.001f || denom <= -0.001f) {
            float inv_denom = 1.0f / denom;
            float v2x = px_float - tri.x0, v2y = py_float - tri.y0;
            float d20 = v2x * v0x + v2y * v0y;
            float d21 = v2x * v1x + v2y * v1y;
            u = (d11 * d20 - d01 * d21) * inv_denom;
            v = (d00 * d21 - d01 * d20) * inv_denom;
        }
        float w = 1.0f - u - v;

        // Depth test against opaque geometry (transparent must be IN FRONT).
        // Signed encoding — depth can be negative when the camera sits at scene level.
        float depth = w * tri.z0 + u * tri.z1 + v * tri.z2;
        int depth_enc = encode_depth(depth);
        if (opaque_depth_val != DEPTH_INIT_VALUE &&
            depth_enc > opaque_depth_val + DEPTH_EPSILON_ENCODED) {
            continue;  // Behind opaque surface
        }

        // Interpolate world position
        float3 world_pos = w * float3(tri.world_pos0[0], tri.world_pos0[1], tri.world_pos0[2]) +
                           u * float3(tri.world_pos1[0], tri.world_pos1[1], tri.world_pos1[2]) +
                           v * float3(tri.world_pos2[0], tri.world_pos2[1], tri.world_pos2[2]);
        float3 normal = float3(tri.normal[0], tri.normal[1], tri.normal[2]);

        // === INLINE FORWARD LIGHTING ===
        float total_lux = 0.0f;

        for (uint li = 0; li < light_count; ++li) {
            LightData light = lights[li];
            float3 to_light = float3(light.position) - world_pos;
            float dist_sq = dot(to_light, to_light);

            if (dist_sq > light.emission_radius * light.emission_radius) continue;

            float safe_dist_sq = max(dist_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_dist_sq);
            float3 light_dir = to_light * inv_dist;
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian <= 0.0f) continue;

            // Shadow ray against OPAQUE BVH (transparent surfaces don't self-shadow)
            bool in_shadow = false;
            if (bvh_node_count > 0) {
                float3 ray_origin = world_pos + normal * 0.05f;
                float max_distance = sqrt(safe_dist_sq);
                int stack_ptr = 0;
                stack[stack_ptr++] = 0;

                while (stack_ptr > 0 && !in_shadow) {
                    int node_idx = stack[--stack_ptr];
                    BVHNode node = bvh_nodes[node_idx];

                    if (!ray_intersects_aabb(ray_origin, light_dir,
                                            float3(node.bbox_min), float3(node.bbox_max))) {
                        continue;
                    }

                    if (node.triangle_idx >= 0) {
                        Triangle bvh_tri = bvh_triangles[node.triangle_idx];
                        if (ray_intersects_triangle(ray_origin, light_dir, max_distance,
                                                    float3(bvh_tri.v0), float3(bvh_tri.v1), float3(bvh_tri.v2))) {
                            in_shadow = true;
                        }
                    } else {
                        if (node.left_child >= 0 && stack_ptr < 32) stack[stack_ptr++] = node.left_child;
                        if (node.right_child >= 0 && stack_ptr < 32) stack[stack_ptr++] = node.right_child;
                    }
                }
            }

            if (!in_shadow) {
                float intensity_lux = light.emission_strength / (FOUR_PI * safe_dist_sq);
                total_lux += intensity_lux * lambertian;
            }
        }

        // Tone map and apply material color
        int brightness = tone_map_transparent(total_lux);
        float frag_r = tri.r * float(brightness) / 255.0f;
        float frag_g = tri.g * float(brightness) / 255.0f;
        float frag_b = tri.b * float(brightness) / 255.0f;

        // Alpha blend: src_over (pre-sorted back-to-front)
        float alpha = tri.a;
        dst_r = frag_r * alpha + dst_r * (1.0f - alpha);
        dst_g = frag_g * alpha + dst_g * (1.0f - alpha);
        dst_b = frag_b * alpha + dst_b * (1.0f - alpha);
        any_hit = true;
    }

    // Write blended result if any transparent fragment was processed
    if (any_hit) {
        uint r_val = min((uint)(dst_r * 255.0f), 255u);
        uint g_val = min((uint)(dst_g * 255.0f), 255u);
        uint b_val = min((uint)(dst_b * 255.0f), 255u);
        pixel_buffer[pixel_index] = (0xFF << 24) | (r_val << 16) | (g_val << 8) | b_val;
    }
}

// =============================================================================
// RT VARIANT — identical shading, shadow test via the Metal RT intersector
// =============================================================================
// The software-BVH walk above (32-deep stack over the full opaque triangle
// set, per covered pixel per in-range light) measured ~13 ms/frame for
// Eden's 96 butterfly-wing triangles (2026-07 GPU audit). This variant
// asks the same occlusion question as an ANY-HIT query against the
// driver-built acceleration structure the deterministic shadow pass
// already traces. The host selects it whenever Metal RT is supported;
// the kernel above remains the no-RT fallback.
// =============================================================================

kernel void rasterize_transparent_forward_rt(
    device uint32_t* pixel_buffer [[buffer(0)]],
    constant int*  opaque_depth [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant TriangleLit* triangles [[buffer(4)]],
    constant uint& triangle_count [[buffer(5)]],
    constant LightData* lights [[buffer(6)]],
    constant uint& light_count [[buffer(7)]],
    constant int2& dispatch_origin [[buffer(12)]],
    primitive_acceleration_structure accel [[buffer(13)]],
    uint2 gid [[thread_position_in_grid]])
{
    int px = dispatch_origin.x + (int)gid.x;
    int py = dispatch_origin.y + (int)gid.y;

    if (px >= (int)width || py >= (int)height) return;

    uint pixel_index = py * width + px;
    float px_float = float(px);
    float py_float = float(py);

    int opaque_depth_val = opaque_depth[pixel_index];

    uint32_t current_pixel = pixel_buffer[pixel_index];
    float dst_r = float((current_pixel >> 16) & 0xFF) / 255.0f;
    float dst_g = float((current_pixel >> 8) & 0xFF) / 255.0f;
    float dst_b = float(current_pixel & 0xFF) / 255.0f;
    bool any_hit = false;

    for (uint tri_idx = 0; tri_idx < triangle_count; ++tri_idx) {
        TriangleLit tri = triangles[tri_idx];

        float min_x = min(tri.x0, min(tri.x1, tri.x2));
        float max_x = max(tri.x0, max(tri.x1, tri.x2));
        float min_y = min(tri.y0, min(tri.y1, tri.y2));
        float max_y = max(tri.y0, max(tri.y1, tri.y2));

        if (px_float < min_x || px_float > max_x || py_float < min_y || py_float > max_y) {
            continue;
        }

        float a0 = -(tri.y1 - tri.y0), b0 = -(tri.x0 - tri.x1), c0 = -(tri.x1 * tri.y0 - tri.x0 * tri.y1);
        float a1 = -(tri.y2 - tri.y1), b1 = -(tri.x1 - tri.x2), c1 = -(tri.x2 * tri.y1 - tri.x1 * tri.y2);
        float a2 = -(tri.y0 - tri.y2), b2 = -(tri.x2 - tri.x0), c2 = -(tri.x0 * tri.y2 - tri.x2 * tri.y0);

        float edge0 = a0 * px_float + b0 * py_float + c0;
        float edge1 = a1 * px_float + b1 * py_float + c1;
        float edge2 = a2 * px_float + b2 * py_float + c2;

        if (edge0 < 0.0f || edge1 < 0.0f || edge2 < 0.0f) continue;

        float v0x = tri.x1 - tri.x0, v0y = tri.y1 - tri.y0;
        float v1x = tri.x2 - tri.x0, v1y = tri.y2 - tri.y0;
        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;
        float denom = d00 * d11 - d01 * d01;

        float u = 0.5f, v = 0.5f;
        if (denom >= 0.001f || denom <= -0.001f) {
            float inv_denom = 1.0f / denom;
            float v2x = px_float - tri.x0, v2y = py_float - tri.y0;
            float d20 = v2x * v0x + v2y * v0y;
            float d21 = v2x * v1x + v2y * v1y;
            u = (d11 * d20 - d01 * d21) * inv_denom;
            v = (d00 * d21 - d01 * d20) * inv_denom;
        }
        float w = 1.0f - u - v;

        float depth = w * tri.z0 + u * tri.z1 + v * tri.z2;
        int depth_enc = encode_depth(depth);
        if (opaque_depth_val != DEPTH_INIT_VALUE &&
            depth_enc > opaque_depth_val + DEPTH_EPSILON_ENCODED) {
            continue;
        }

        float3 world_pos = w * float3(tri.world_pos0[0], tri.world_pos0[1], tri.world_pos0[2]) +
                           u * float3(tri.world_pos1[0], tri.world_pos1[1], tri.world_pos1[2]) +
                           v * float3(tri.world_pos2[0], tri.world_pos2[1], tri.world_pos2[2]);
        float3 normal = float3(tri.normal[0], tri.normal[1], tri.normal[2]);

        float total_lux = 0.0f;

        for (uint li = 0; li < light_count; ++li) {
            LightData light = lights[li];
            float3 to_light = float3(light.position) - world_pos;
            float dist_sq = dot(to_light, to_light);

            if (dist_sq > light.emission_radius * light.emission_radius) continue;

            float safe_dist_sq = max(dist_sq, MIN_DISTANCE_SQ);
            float inv_dist = rsqrt(safe_dist_sq);
            float3 light_dir = to_light * inv_dist;
            float lambertian = max(dot(normal, light_dir), 0.0f);

            if (lambertian <= 0.0f) continue;

            // ANY-HIT occlusion query on the driver-built accel structure
            // (transparent surfaces don't self-shadow: they are not in it).
            ray shadow_ray;
            shadow_ray.origin = world_pos + normal * 0.05f;
            shadow_ray.direction = light_dir;
            shadow_ray.min_distance = 0.0f;
            shadow_ray.max_distance = sqrt(safe_dist_sq);

            intersector<triangle_data> occ;
            occ.accept_any_intersection(true);
            intersector<triangle_data>::result_type hit = occ.intersect(shadow_ray, accel);
            bool in_shadow = (hit.type != intersection_type::none);

            if (!in_shadow) {
                float intensity_lux = light.emission_strength / (FOUR_PI * safe_dist_sq);
                total_lux += intensity_lux * lambertian;
            }
        }

        int brightness = tone_map_transparent(total_lux);
        float frag_r = tri.r * float(brightness) / 255.0f;
        float frag_g = tri.g * float(brightness) / 255.0f;
        float frag_b = tri.b * float(brightness) / 255.0f;

        float alpha = tri.a;
        dst_r = frag_r * alpha + dst_r * (1.0f - alpha);
        dst_g = frag_g * alpha + dst_g * (1.0f - alpha);
        dst_b = frag_b * alpha + dst_b * (1.0f - alpha);
        any_hit = true;
    }

    if (any_hit) {
        uint r_val = min((uint)(dst_r * 255.0f), 255u);
        uint g_val = min((uint)(dst_g * 255.0f), 255u);
        uint b_val = min((uint)(dst_b * 255.0f), 255u);
        pixel_buffer[pixel_index] = (0xFF << 24) | (r_val << 16) | (g_val << 8) | b_val;
    }
}
