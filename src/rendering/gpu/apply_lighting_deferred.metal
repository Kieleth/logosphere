//
// apply_lighting_deferred.metal
// Apply lighting to final framebuffer (Pass 3 of 3)
//
// ARCHITECTURE: Deferred Rendering - 3-pass system
// Pass 1: Rasterize geometry, output to G-buffer
// Pass 2: Trace shadow rays, output lighting intensity
// Pass 3 (THIS FILE): Apply lighting (simple math with pre-computed shadows)
//
// PURPOSE: Extract lighting application from rasterize_triangles_lit
// Combines G-buffer + shadow results to produce final framebuffer
//
// PERFORMANCE: ~25ms estimated (simple math, no BVH traversal, no atomics)

#include <metal_stdlib>
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gbuffer_types.metal"
#include "procedural_patterns.metal"
using namespace metal;

// =========================================================================
// DDGI PROBE SAMPLING
// =========================================================================

// Octahedral decode (same as ddgi.metal)
inline float3 ddgi_oct_decode(float2 uv) {
    uv = uv * 2.0f - 1.0f;
    float3 n = float3(uv.x, uv.y, 1.0f - abs(uv.x) - abs(uv.y));
    if (n.z < 0.0f) {
        float2 s = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * s;
    }
    return normalize(n);
}

// Octahedral encode
inline float2 ddgi_oct_encode(float3 n) {
    float3 a = abs(n);
    float sum = a.x + a.y + a.z;
    float2 o = n.xy / sum;
    if (n.z < 0.0f) {
        float2 s = float2(o.x >= 0.0f ? 1.0f : -1.0f, o.y >= 0.0f ? 1.0f : -1.0f);
        o = (1.0f - abs(o.yx)) * s;
    }
    return o * 0.5f + 0.5f;
}

// Sample one probe's irradiance at a given direction
inline float3 ddgi_sample_probe(constant float4* irradiance, uint probe_id, float3 dir) {
    float2 uv = ddgi_oct_encode(dir);
    // Bilinear sample the 8x8 octahedral map
    float fx = uv.x * 8.0f - 0.5f;
    float fy = uv.y * 8.0f - 0.5f;
    int ix0 = clamp((int)floor(fx), 0, 7);
    int iy0 = clamp((int)floor(fy), 0, 7);
    int ix1 = clamp(ix0 + 1, 0, 7);
    int iy1 = clamp(iy0 + 1, 0, 7);
    float wx = fx - floor(fx);
    float wy = fy - floor(fy);

    uint base = probe_id * 64;
    float3 s00 = irradiance[base + iy0 * 8 + ix0].xyz;
    float3 s10 = irradiance[base + iy0 * 8 + ix1].xyz;
    float3 s01 = irradiance[base + iy1 * 8 + ix0].xyz;
    float3 s11 = irradiance[base + iy1 * 8 + ix1].xyz;
    return mix(mix(s00, s10, wx), mix(s01, s11, wx), wy);
}

// Sample DDGI irradiance at a world position with trilinear probe interpolation
inline float3 ddgi_sample_irradiance(
    float3 world_pos, float3 normal,
    constant float4* irradiance, constant float2* depth_buf,
    constant DDGIParams& params)
{
    float3 origin = float3(params.grid_origin);
    uint3 dims = uint3(params.grid_dimensions);
    float spacing = params.probe_spacing;

    // Grid-space coordinates
    float3 gpos = (world_pos - origin) / spacing;
    int3 base = int3(floor(gpos));
    float3 frac_pos = gpos - float3(base);

    float3 result = float3(0.0f);
    float total_weight = 0.0f;

    // Trilinear interpolation over 8 surrounding probes
    for (int dz = 0; dz < 2; dz++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                int3 idx = base + int3(dx, dy, dz);

                // Clamp to grid bounds
                if (idx.x < 0 || idx.x >= (int)dims.x ||
                    idx.y < 0 || idx.y >= (int)dims.y ||
                    idx.z < 0 || idx.z >= (int)dims.z) continue;

                uint probe_id = idx.z * dims.x * dims.y + idx.y * dims.x + idx.x;
                float3 probe_pos = origin + float3(idx) * spacing;

                // Trilinear weight
                float3 w3 = float3(dx ? frac_pos.x : 1.0f - frac_pos.x,
                                   dy ? frac_pos.y : 1.0f - frac_pos.y,
                                   dz ? frac_pos.z : 1.0f - frac_pos.z);
                float trilinear_w = w3.x * w3.y * w3.z;
                if (trilinear_w < 0.0001f) continue;

                // Normal weight: avoid back-face probes
                float3 dir_to_probe = normalize(probe_pos - world_pos);
                float normal_w = max(0.0001f, dot(normal, dir_to_probe));

                // Sample irradiance in normal direction
                float3 irr = ddgi_sample_probe(irradiance, probe_id, normal);

                float w = trilinear_w * normal_w;
                result += irr * w;
                total_weight += w;
            }
        }
    }

    return total_weight > 0.0001f ? result / total_weight : float3(0.0f);
}

// =========================================================================
// TONE MAPPING: ZONE SYSTEM
// =========================================================================
// Maps lux values to RGB 0-255 using Ansel Adams-inspired zone system
// Matches CPU: lighting_config.h::tone_map_zone_system

inline int tone_map_zone_system(float raw_intensity_lux) {
    const float shadow_threshold = 10.0f;
    const float midtone_threshold = 100.0f;
    const float shadow_rgb_max = 75.0f;
    const float midtone_rgb_max = 200.0f;

    if (raw_intensity_lux < 0.001f) {
        return 0;
    }

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
// PASS 3: APPLY LIGHTING (Deferred)
// =========================================================================
// Combine G-buffer + shadow results to produce final framebuffer
// NO rasterization, NO shadow rays - just simple math!
//
// INPUT: G-buffer (base color, particle ID), shadow results (lighting lux)
// OUTPUT: Final framebuffer (BGRA pixels)

kernel void apply_lighting_deferred(
    device uint32_t* pixel_buffer [[buffer(0)]],
    constant GBufferPixel* gbuffer [[buffer(1)]],
    constant float* shadow_results [[buffer(2)]],
    constant uint& width [[buffer(3)]],
    constant uint& height [[buffer(4)]],
    constant uint& shadow_width [[buffer(5)]],
    constant uint& shadow_height [[buffer(6)]],
    constant LightData* lights [[buffer(7)]],
    constant uint& light_count [[buffer(8)]],
    constant uint8_t* is_light_source_map [[buffer(9)]],
    constant uint& map_size [[buffer(10)]],
    constant ParticleTransform* particle_transforms [[buffer(11)]],
    constant uint& particle_count [[buffer(12)]],
    constant float2& camera_pos [[buffer(13)]],           // Shadow distance culling: camera X,Y
    constant float& shadow_cull_radius [[buffer(14)]],    // Shadow distance culling: radius (30m)
    constant float2& shadow_fade_range [[buffer(15)]],     // Shadow distance fade: (start_m, end_m)
    // buffer(16), (18), (19) retired 2026-07-29 with the screen-space GI
    // path (USE_SSGI / USE_BVH_INDIRECT_GI). Indices deliberately left
    // unused rather than renumbered, so every other binding keeps its slot.
    constant float4* light_color [[buffer(17)]],           // Per-pixel light color ratio (RGB, 0-1)
    constant ssdo_store_t* ssao_results [[buffer(20)]],      // SSDO: RGB bounce (.xyz) + AO (.w)
    constant float4* ddgi_irradiance [[buffer(21)]],        // DDGI: probe irradiance (optional)
    constant float2* ddgi_depth [[buffer(22)]],             // DDGI: probe depth (optional)
    constant DDGIParams* ddgi_params_ptr [[buffer(23)]],    // DDGI: grid configuration (optional)
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;

    if (px >= (int)width || py >= (int)height) {
        return;
    }

    uint pixel_index = py * width + px;
    GBufferPixel pixel = gbuffer[pixel_index];

    // Sky pixels
    if (pixel.particle_id == GBUFFER_SKY_ID) {
        pixel_buffer[pixel_index] = 0xFF0A0A0F;
        return;
    }

    // Sample shadow buffer (with bilinear upscaling if needed)
    float total_lighting_lux;
    if (shadow_width == width && shadow_height == height) {
        total_lighting_lux = shadow_results[pixel_index];
    } else {
        float shadow_u = ((float)px + 0.5f) / (float)width * (float)shadow_width;
        float shadow_v = ((float)py + 0.5f) / (float)height * (float)shadow_height;

        int sx0 = max((int)floor(shadow_u - 0.5f), 0);
        int sy0 = max((int)floor(shadow_v - 0.5f), 0);
        int sx1 = min(sx0 + 1, (int)shadow_width - 1);
        int sy1 = min(sy0 + 1, (int)shadow_height - 1);

        float fx = (shadow_u - 0.5f) - (float)sx0;
        float fy = (shadow_v - 0.5f) - (float)sy0;

        float s00 = shadow_results[sy0 * shadow_width + sx0];
        float s10 = shadow_results[sy0 * shadow_width + sx1];
        float s01 = shadow_results[sy1 * shadow_width + sx0];
        float s11 = shadow_results[sy1 * shadow_width + sx1];

        total_lighting_lux = mix(mix(s00, s10, fx), mix(s01, s11, fx), fy);
    }

    // =========================================================================
    // PER-CHANNEL DIRECT LIGHTING: Split scalar lux by light color ratio
    // color_ratio = (r_fraction, g_fraction, b_fraction) from shadow kernel
    // For white lights: ratio = (1,1,1), so lux_r = lux_g = lux_b = total_lux
    // For red light only: ratio = (1,0,0), so lux_r = total_lux, lux_g = lux_b = 0
    // =========================================================================
    float3 color_ratio = float3(1.0f);
    if (light_color != nullptr) {
        // Use same coordinate as shadow buffer (full res = pixel_index)
        if (shadow_width == width && shadow_height == height) {
            color_ratio = light_color[pixel_index].xyz;
        } else {
            // Bilinear upscale the color ratio to match shadow buffer resolution
            float shadow_u = ((float)px + 0.5f) / (float)width * (float)shadow_width;
            float shadow_v = ((float)py + 0.5f) / (float)height * (float)shadow_height;
            int sx0 = max((int)floor(shadow_u - 0.5f), 0);
            int sy0 = max((int)floor(shadow_v - 0.5f), 0);
            int sx1 = min(sx0 + 1, (int)shadow_width - 1);
            int sy1 = min(sy0 + 1, (int)shadow_height - 1);
            float fx = (shadow_u - 0.5f) - (float)sx0;
            float fy = (shadow_v - 0.5f) - (float)sy0;
            float4 c00 = light_color[sy0 * shadow_width + sx0];
            float4 c10 = light_color[sy0 * shadow_width + sx1];
            float4 c01 = light_color[sy1 * shadow_width + sx0];
            float4 c11 = light_color[sy1 * shadow_width + sx1];
            color_ratio = mix(mix(c00, c10, fx), mix(c01, c11, fx), fy).xyz;
        }
    }
    float3 direct_lux_rgb = total_lighting_lux * color_ratio;

    // =========================================================================
    // SHADOW DISTANCE FADE
    // Gradual smoothstep fade from shadow_fade_range.x (20m) to .y (40m).
    // CPU culls shadow triangles at 30m, so the fade starts 10m before the
    // cull boundary for a seamless transition. Smoothstep avoids the visible
    // linear boundary of the old 10m fade zone.
    // =========================================================================
    if (shadow_cull_radius > 0.0f) {
        float3 world_pos = float3(pixel.world_pos);
        float dx = world_pos.x - camera_pos.x;
        float dy = world_pos.y - camera_pos.y;
        float dist_sq = dx * dx + dy * dy;

        float fade_start = shadow_fade_range.x;
        float fade_start_sq = fade_start * fade_start;

        if (dist_sq > fade_start_sq) {
            float dist = sqrt(dist_sq);
            float fade_end = shadow_fade_range.y;
            float t = clamp((dist - fade_start) / (fade_end - fade_start), 0.0f, 1.0f);
            float fade = 1.0f - t * t * (3.0f - 2.0f * t);  // smoothstep

            float ambient = total_lighting_lux * 0.15f;
            total_lighting_lux = mix(ambient, total_lighting_lux, fade);
        }
    }

    // Check emissive
    bool is_emissive = false;
    if (pixel.particle_id < map_size && is_light_source_map != nullptr) {
        is_emissive = (is_light_source_map[pixel.particle_id] == 1);
    }

    // Procedural pattern absorption factor
    float pattern_factor = 1.0f;
    if (!is_emissive && pixel.particle_id < particle_count) {
        ParticleTransform transform = particle_transforms[pixel.particle_id];
        float absorption = compute_pattern(
            float3(pixel.world_pos),
            float3(pixel.normal),
            float3(transform.center),
            float3(transform.primary_axis),
            transform.pattern_id
        );
        pattern_factor = 1.0f - absorption;
    }

    // DDGI: probe-based indirect diffuse. Stored for post-tonemapping tint
    float3 ddgi_bounce = float3(0.0f);
    if (ddgi_irradiance != nullptr && ddgi_params_ptr != nullptr) {
        float3 wp = float3(pixel.world_pos);
        float3 n = normalize(float3(pixel.normal));
        ddgi_bounce = ddgi_sample_irradiance(
            wp, n, ddgi_irradiance, ddgi_depth, *ddgi_params_ptr);
        float3 albedo = float3(pixel.base_color.z, pixel.base_color.y, pixel.base_color.x) / 255.0f;
        ddgi_bounce *= albedo * ddgi_params_ptr->intensity;
    }

    // SSDO: ambient occlusion (contact shadows) + colored bounce from nearby surfaces.
    // .xyz = indirect bounce color from nearby lit surfaces
    // .w   = AO factor (0 = fully occluded, 1 = fully open)
    float ao = 1.0f;
    if (ssao_results != nullptr) {
        float4 ssdo = ssdo_unpack(ssao_results[pixel_index]);
        ao = ssdo.w;
        // SSDO bounce applied as color tint after tone mapping (not additive lux).
        // This bypasses highlight compression so bounce is visible even on bright surfaces.
    }
    direct_lux_rgb *= mix(0.3f, 1.0f, ao);
    total_lighting_lux *= mix(0.3f, 1.0f, ao);

    // Tone mapping: hybrid per-channel brightness + color tinting for GI
    uint r_val, g_val, b_val;
    if (is_emissive) {
        r_val = min((uint)(pixel.base_color.z * pattern_factor), 255u);
        g_val = min((uint)(pixel.base_color.y * pattern_factor), 255u);
        b_val = min((uint)(pixel.base_color.x * pattern_factor), 255u);
    } else {
        // Approach A: Per-channel tone mapping using light color
        // direct_lux_rgb has per-channel direct lighting (e.g., red light → (lux,0,0))
        int br_r = tone_map_zone_system(direct_lux_rgb.r);
        int br_g = tone_map_zone_system(direct_lux_rgb.g);
        int br_b = tone_map_zone_system(direct_lux_rgb.b);
        float pch_r = pixel.base_color.z * br_r / 255.0f * pattern_factor;
        float pch_g = pixel.base_color.y * br_g / 255.0f * pattern_factor;
        float pch_b = pixel.base_color.x * br_b / 255.0f * pattern_factor;

        // Approach B: Color tinting (preserves GI + light color in bright areas)
        // Use per-channel direct brightness for the tinting base
        int brightness_direct_r = tone_map_zone_system(direct_lux_rgb.r);
        int brightness_direct_g = tone_map_zone_system(direct_lux_rgb.g);
        int brightness_direct_b = tone_map_zone_system(direct_lux_rgb.b);
        int brightness_direct = max(max(brightness_direct_r, brightness_direct_g), brightness_direct_b);
        float base_r = pixel.base_color.z / 255.0f;
        float base_g = pixel.base_color.y / 255.0f;
        float base_b = pixel.base_color.x / 255.0f;
        // GI hue tint retired 2026-07-29 with the screen-space GI path:
        // it was gated on gi_total > 1.0 and indirect was always zero.
        float tnt_r = base_r * brightness_direct_r * pattern_factor;
        float tnt_g = base_g * brightness_direct_g * pattern_factor;
        float tnt_b = base_b * brightness_direct_b * pattern_factor;

        // Blend: dark areas → per-channel (A), bright areas → tinting (B)
        float saturation = min(brightness_direct / 255.0f, 1.0f);
        r_val = min((uint)mix(pch_r, tnt_r, saturation), 255u);
        g_val = min((uint)mix(pch_g, tnt_g, saturation), 255u);
        b_val = min((uint)mix(pch_b, tnt_b, saturation), 255u);
    }

    // DDGI bounce: apply probe-based color tint AFTER tone mapping.
    if (!is_emissive) {
        float ddgi_lum = dot(ddgi_bounce, float3(0.2126f, 0.7152f, 0.0722f));
        if (ddgi_lum > 0.001f) {
            float ddgi_strength = ddgi_lum / (ddgi_lum + 0.01f) * 0.20f;
            float3 ddgi_dir = ddgi_bounce / (ddgi_lum + 0.0001f);
            float ddgi_max = max(max(ddgi_dir.r, ddgi_dir.g), ddgi_dir.b);
            if (ddgi_max > 0.001f) ddgi_dir /= ddgi_max;
            r_val = min((uint)(r_val * mix(1.0f, ddgi_dir.r, ddgi_strength)), 255u);
            g_val = min((uint)(g_val * mix(1.0f, ddgi_dir.g, ddgi_strength)), 255u);
            b_val = min((uint)(b_val * mix(1.0f, ddgi_dir.b, ddgi_strength)), 255u);
        }
    }

    // SSDO bounce: apply colored tint from nearby surfaces AFTER tone mapping.
    // Proportional tint: stronger bounce = stronger color shift, no hard cap.
    if (ssao_results != nullptr && !is_emissive) {
        float4 ssdo = ssdo_unpack(ssao_results[pixel_index]);
        float3 bounce = ssdo.xyz;
        float bounce_lum = dot(bounce, float3(0.2126f, 0.7152f, 0.0722f));
        if (bounce_lum > 0.0001f) {
            // Tint proportional to bounce magnitude, smooth rolloff
            // bounce_lum is typically 0.001-0.05 from the SSDO kernel
            float tint_strength = bounce_lum / (bounce_lum + 0.002f) * 0.12f;
            float3 tint_dir = bounce / (bounce_lum + 0.0001f);  // preserve color ratio
            // Normalize tint_dir so max component = 1
            float tint_max = max(max(tint_dir.r, tint_dir.g), tint_dir.b);
            if (tint_max > 0.001f) tint_dir /= tint_max;
            r_val = min((uint)(r_val * mix(1.0f, tint_dir.r, tint_strength)), 255u);
            g_val = min((uint)(g_val * mix(1.0f, tint_dir.g, tint_strength)), 255u);
            b_val = min((uint)(b_val * mix(1.0f, tint_dir.b, tint_strength)), 255u);
        }
    }

    pixel_buffer[pixel_index] = (pixel.base_color.w << 24) | (r_val << 16) | (g_val << 8) | b_val;
}
