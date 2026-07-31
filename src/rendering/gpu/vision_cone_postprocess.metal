//
// vision_cone_postprocess.metal
// Vision cone post-process with foveal blur (Pass 4 of 4)
//
// ARCHITECTURE: Deferred Rendering - 4-pass system
// Pass 1: Rasterize geometry, output to G-buffer
// Pass 2: Trace shadow rays, output lighting intensity
// Pass 3: Apply lighting → Intermediate framebuffer
// Pass 4 (THIS FILE): Foveal blur + vision cone mask → Final framebuffer
//
// PURPOSE:
// 1. Blur pixels outside focus point (simulates human foveal vision)
// 2. Darken pixels outside field of view (fog-of-war effect)
//
// PERFORMANCE: Optimized 5-tap cross blur (~2-3ms at 1080p)

#include <metal_stdlib>
#include "gpu_types.metal"
#include "gbuffer_types.metal"
using namespace metal;

// =========================================================================
// PASS 4: FOVEAL BLUR + VISION CONE
// =========================================================================
// Efficient variable blur based on distance from focus point
// Uses 5-tap cross pattern (not full kernel) for performance

kernel void apply_vision_cone(
    device uint32_t* pixel_buffer [[buffer(0)]],      // Input/Output: framebuffer (BGRA)
    constant GBufferPixel* gbuffer [[buffer(1)]],     // G-buffer (world positions)
    constant VisionConeParams& params [[buffer(2)]],  // Vision cone parameters
    constant uint& width [[buffer(3)]],               // Framebuffer width
    constant uint& height [[buffer(4)]],              // Framebuffer height
    device const float* vision_memory [[buffer(5)]],  // Vision-memory grid (NxN floats, world-space)
    constant uint8_t* is_dynamic_map [[buffer(6)]],   // Per-particle is_dynamic flag, indexed by particle_id
    constant uint& dynamic_map_size [[buffer(7)]],    // # entries in is_dynamic_map (0 = no map / disabled)
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    // Bounds check
    if (px >= (int)width || py >= (int)height) {
        return;
    }

    // Early exit if vision cone disabled
    if (params.enabled == 0) {
        return;
    }

    uint pixel_index = py * width + px;
    GBufferPixel gbuf = gbuffer[pixel_index];

    // Sky pixels: just apply darkness
    if (gbuf.particle_id == GBUFFER_SKY_ID) {
        uint32_t current = pixel_buffer[pixel_index];
        uint b = (current >> 0) & 0xFF;
        uint g = (current >> 8) & 0xFF;
        uint r = (current >> 16) & 0xFF;
        uint a = (current >> 24) & 0xFF;

        float dark = params.darkness;
        r = (uint)(r * dark);
        g = (uint)(g * dark);
        b = (uint)(b * dark);

        pixel_buffer[pixel_index] = (a << 24) | (r << 16) | (g << 8) | b;
        return;
    }

    // Get world position
    float world_x = gbuf.world_pos.x;
    float world_y = gbuf.world_pos.y;

    // =========================================================================
    // VISION CONE: Calculate visibility based on angle from look direction
    // =========================================================================
    // TRUNCATED CONE: Move apex behind viewer so character is inside cone
    // This prevents the character body from falling outside cone edges
    const float APEX_OFFSET = 0.3f;  // 30cm behind viewer (adjust as needed)

    // Calculate offset position (backwards along look direction)
    // look_direction: 0 = +Y (North), sin/cos give direction vector
    float offset_x = params.viewer_x - sin(params.look_direction) * APEX_OFFSET;
    float offset_y = params.viewer_y - cos(params.look_direction) * APEX_OFFSET;

    // Use offset position for cone calculations instead of viewer position
    float dx = world_x - offset_x;
    float dy = world_y - offset_y;
    float distance = sqrt(dx * dx + dy * dy);

    float visibility = 1.0f;

    // Beyond range check
    if (distance > params.range) {
        visibility = params.darkness;
    } else {
        // Angle check
        float angle_to_pixel = atan2(dx, dy);
        float angle_diff = angle_to_pixel - params.look_direction;

        const float PI = 3.14159265f;
        while (angle_diff > PI) angle_diff -= 2.0f * PI;
        while (angle_diff < -PI) angle_diff += 2.0f * PI;

        float abs_angle = abs(angle_diff);

        if (abs_angle <= params.half_fov * params.inner_falloff) {
            visibility = 1.0f;
        } else if (abs_angle <= params.half_fov) {
            float falloff_start = params.half_fov * params.inner_falloff;
            float falloff_range = params.half_fov - falloff_start;
            float t = (abs_angle - falloff_start) / falloff_range;
            visibility = mix(1.0f, params.darkness, t * t);
        } else {
            visibility = params.darkness;
        }

        // Range falloff
        float range_falloff_start = params.range * 0.8f;
        if (distance > range_falloff_start) {
            float range_t = (distance - range_falloff_start) / (params.range - range_falloff_start);
            visibility *= mix(1.0f, params.darkness, range_t);
        }

        // ===========================================================
        // LOS occlusion (Pass 4b). When the host pre-computes a
        // per-bin nearest-occluder distance array, darken any pixel
        // whose distance from the viewer is greater than the bin's
        // distance — i.e. it's behind a trail/wall. This is what
        // makes the cone "trail aware" instead of just an angular
        // dimmer.
        if (params.occlusion_count > 0 && abs_angle <= params.half_fov) {
            // Map angle_diff in [-half_fov, +half_fov] → bin in
            // [0, occlusion_count - 1].
            float t_bin = (angle_diff + params.half_fov)
                          / (2.0f * params.half_fov);
            int bin = (int)(t_bin * (float)params.occlusion_count);
            if (bin < 0) bin = 0;
            if (bin >= params.occlusion_count) bin = params.occlusion_count - 1;
            float occ_dist = params.occlusion_distance[bin];
            // Bias the comparison so the wall pixels themselves
            // don't get darkened. Two terms:
            //   - APEX_OFFSET (0.3 m): the shader's `distance` is
            //     measured from the cone apex (behind the viewer),
            //     while CPU bin distances are from the actual
            //     viewer. Without this bias, every pixel reads as
            //     0.3 m "deeper" than it really is.
            //   - 0.4 m wall-thickness slack: trail walls have
            //     ~15 cm projected thickness, plus some give for
            //     diagonals where the projected extent is larger.
            //     Without it the wall's own near face beats its own
            //     far face and the wall self-darkens.
            const float OCC_BIAS = APEX_OFFSET + 0.4f;
            if (distance > occ_dist + OCC_BIAS) {
                visibility = params.darkness;
            }
        }
    }

    // =========================================================================
    // VISION MEMORY: blend in any cached visibility from the
    // world-space memory grid. Cells the cone illuminated recently
    // stay dimly lit (decaying CPU-side over `decay_seconds`) so
    // pixels just outside the live cone don't snap to black. Look
    // up the cell whose center is nearest the pixel's world_pos.
    // =========================================================================
    if (params.memory_enabled != 0 && params.memory_width > 0 && params.memory_height > 0) {
        // Skip dynamic pixels — bikes / NPCs / live trail heads
        // shouldn't appear in memory just because the cell they're
        // currently in was previously illuminated. Static pixels
        // (walls, floor, sealed trails) get the dim memory blend
        // and persist as ghosts. The is_dynamic_map is built each
        // frame in render_pipeline.cpp from Particle::is_dynamic;
        // particles outside the map (id >= map_size) are treated
        // as static (the safe default).
        bool pixel_is_dynamic = false;
        if (gbuf.particle_id < dynamic_map_size && dynamic_map_size > 0) {
            pixel_is_dynamic = (is_dynamic_map[gbuf.particle_id] != 0);
        }
        if (!pixel_is_dynamic) {
            int cx = (int)((world_x - params.memory_origin_x) / params.memory_cell_size);
            int cy = (int)((world_y - params.memory_origin_y) / params.memory_cell_size);
            if (cx >= 0 && cx < params.memory_width &&
                cy >= 0 && cy < params.memory_height) {
                float mem = vision_memory[cy * params.memory_width + cx];   // 0..1
                // memory_dim caps how bright a remembered pixel can
                // get. Live cone visibility (computed above) wins
                // via max(); memory only fills in below it.
                float mem_visibility = params.memory_dim * mem;
                if (mem_visibility > visibility) visibility = mem_visibility;
            }
        }
    }

    // =========================================================================
    // FOVEAL BLUR: Variable blur based on distance from focus point
    // =========================================================================
    // Calculate blur amount (0 = sharp, 1 = max blur)
    float focus_dx = world_x - params.focus_x;
    float focus_dy = world_y - params.focus_y;
    float focus_dist = sqrt(focus_dx * focus_dx + focus_dy * focus_dy);

    float blur_amount = 0.0f;
    if (focus_dist > params.focus_radius) {
        // Gradual falloff over 5x focus radius
        float falloff_range = params.focus_radius * 5.0f;
        float t = (focus_dist - params.focus_radius) / falloff_range;
        t = clamp(t, 0.0f, 1.0f);
        // Smooth quintic for gradual transition
        float t2 = t * t;
        float t3 = t2 * t;
        blur_amount = 6.0f * t3 * t2 - 15.0f * t2 * t2 + 10.0f * t3;
    }

    // =========================================================================
    // VIEWER SAFE ZONE: Never blur pixels close to the viewer (character)
    // =========================================================================
    // This prevents the player character from being blurred regardless of focus
    const float VIEWER_BLUR_SAFE_RADIUS = 1.5f;  // meters - covers full humanoid

    float viewer_dx = world_x - params.viewer_x;
    float viewer_dy = world_y - params.viewer_y;
    float viewer_dist = sqrt(viewer_dx * viewer_dx + viewer_dy * viewer_dy);

    if (viewer_dist < VIEWER_BLUR_SAFE_RADIUS) {
        blur_amount = 0.0f;
    }

    // Read center pixel
    uint32_t current = pixel_buffer[pixel_index];
    uint b = (current >> 0) & 0xFF;
    uint g = (current >> 8) & 0xFF;
    uint r = (current >> 16) & 0xFF;
    uint a = (current >> 24) & 0xFF;

    // =========================================================================
    // BLUR: 5-tap cross pattern (efficient, good quality)
    // =========================================================================
    // Only blur if blur_amount > 0 and we're inside bounds for sampling
    if (blur_amount > 0.01f) {
        // Calculate blur radius in pixels (max 8 pixels at full blur)
        int blur_radius = (int)(blur_amount * 8.0f);
        blur_radius = max(1, min(blur_radius, 8));

        // Accumulate weighted samples
        float sum_r = float(r);
        float sum_g = float(g);
        float sum_b = float(b);
        float weight = 1.0f;

        // Sample 4 neighbors (cross pattern: left, right, up, down)
        // This is much faster than full kernel while giving good blur
        int offsets[4][2] = {{-blur_radius, 0}, {blur_radius, 0}, {0, -blur_radius}, {0, blur_radius}};

        for (int i = 0; i < 4; i++) {
            int nx = px + offsets[i][0];
            int ny = py + offsets[i][1];

            // Bounds check
            if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                uint neighbor_idx = ny * width + nx;
                uint32_t neighbor = pixel_buffer[neighbor_idx];

                float nb = float((neighbor >> 0) & 0xFF);
                float ng = float((neighbor >> 8) & 0xFF);
                float nr = float((neighbor >> 16) & 0xFF);

                // Equal weight for cross samples
                sum_r += nr;
                sum_g += ng;
                sum_b += nb;
                weight += 1.0f;
            }
        }

        // Average
        float avg_r = sum_r / weight;
        float avg_g = sum_g / weight;
        float avg_b = sum_b / weight;

        // Blend between sharp and blurred based on blur_amount
        r = (uint)mix(float(r), avg_r, blur_amount);
        g = (uint)mix(float(g), avg_g, blur_amount);
        b = (uint)mix(float(b), avg_b, blur_amount);
    }

    // Apply visibility (cone edge darkening)
    if (visibility < 0.999f) {
        r = (uint)(float(r) * visibility);
        g = (uint)(float(g) * visibility);
        b = (uint)(float(b) * visibility);
    }

    pixel_buffer[pixel_index] = (a << 24) | (r << 16) | (g << 8) | b;
}
