// =========================================================================
// SCREEN-SPACE DIRECTIONAL OCCLUSION (SSDO)
// =========================================================================
// Unified ambient occlusion + colored bounce in one pass.
// For each pixel, samples screen-space neighbors:
//   - Occluders above the surface contribute AO (darkening)
//   - Occluders also contribute their surface color as indirect bounce
//
// Output: float4 per pixel:
//   .xyz = indirect bounce color (RGB lux from nearby lit surfaces)
//   .w   = AO factor (0 = fully occluded, 1 = fully open)
//
// Replaces both SSAO and GI with a single deterministic pass.
// No ray tracing, no temporal accumulation, no Monte Carlo noise.
// =========================================================================

#include <metal_stdlib>
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gbuffer_types.metal"

using namespace metal;

inline float ssao_halton(uint index, uint base) {
    float f = 1.0f;
    float r = 0.0f;
    uint i = index;
    while (i > 0) {
        f /= float(base);
        r += f * float(i % base);
        i /= base;
    }
    return r;
}

// =========================================================================
// PASS 2.7: COMPUTE SSDO (AO + colored bounce)
// =========================================================================

kernel void compute_ssao(
    device const GBufferPixel* gbuffer [[buffer(0)]],
    device ssdo_store_t* ssao_results [[buffer(1)]],  // Output: RGB bounce + AO in alpha
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant SSAOParams& params [[buffer(4)]],
    constant float* shadow_results [[buffer(5)]],     // Direct lighting per pixel (for bounce color)
    constant float4* light_color [[buffer(6)]],       // Per-pixel light color ratio (optional)
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;
    if (px >= (int)width || py >= (int)height) return;

    uint idx = py * width + px;
    GBufferPixel center = gbuffer[idx];

    if (center.particle_id == GBUFFER_SKY_ID) {
        ssao_results[idx] = ssdo_pack(float4(0.0f, 0.0f, 0.0f, 1.0f));
        return;
    }

    float3 P = float3(center.world_pos);
    float3 N = normalize(float3(center.normal));

    float pixel_hash = fract(float(idx) * 0.6180339887f);

    float occlusion = 0.0f;
    float3 bounce_color = float3(0.0f);
    uint sample_count = params.sample_count;

    for (uint i = 0; i < sample_count; i++) {
        float h1 = fract(ssao_halton(i + 1, 2) + pixel_hash);
        float h2 = fract(ssao_halton(i + 1, 3) + pixel_hash);

        float angle = h1 * 2.0f * M_PI_F;
        float r = h2 * params.screen_radius;

        int sx = px + (int)(cos(angle) * r);
        int sy = py + (int)(sin(angle) * r);

        if (sx < 0 || sx >= (int)width || sy < 0 || sy >= (int)height) continue;

        uint sidx = sy * width + sx;
        GBufferPixel sample_pixel = gbuffer[sidx];

        if (sample_pixel.particle_id == GBUFFER_SKY_ID) continue;

        float3 Q = float3(sample_pixel.world_pos);
        float3 v = Q - P;
        float dist = length(v);

        if (dist > params.world_radius || dist < params.bias) continue;

        float NdotV = dot(N, normalize(v));

        if (NdotV > 0.0f) {
            float falloff = 1.0f - (dist / params.world_radius);
            float weight = falloff * NdotV;

            // AO: this sample occludes
            occlusion += weight;

            // SSDO bounce: read the occluder's lit surface color.
            // The occluder reflects its base_color * direct_lighting toward us.
            float3 sample_base = float3(
                sample_pixel.base_color.z / 255.0f,  // R (BGRA layout)
                sample_pixel.base_color.y / 255.0f,  // G
                sample_pixel.base_color.x / 255.0f   // B
            );

            // Read how much direct light the sample receives
            float sample_lux = (shadow_results != nullptr) ? shadow_results[sidx] : 50.0f;

            // The bounce contribution: lit surface color * falloff
            // Normalize lux to a reasonable range (tone-map-like)
            float lux_factor = sample_lux / (sample_lux + 100.0f);  // 0-1, soft knee
            bounce_color += sample_base * lux_factor * weight;
        }
    }

    // Normalize
    float ao = 1.0f - clamp(occlusion * params.intensity / float(sample_count), 0.0f, 1.0f);
    float3 bounce = bounce_color * params.intensity / float(sample_count);

    ssao_results[idx] = ssdo_pack(float4(bounce, ao));
}

// =========================================================================
// PASS 2.8: DENOISE SSDO (A-trous bilateral, float4: RGB bounce + AO)
// =========================================================================

kernel void denoise_ssao_atrous(
    device const ssdo_store_t* ssao_input [[buffer(0)]],
    device ssdo_store_t* ssao_output [[buffer(1)]],
    device const GBufferPixel* gbuffer [[buffer(2)]],
    constant uint& width [[buffer(3)]],
    constant uint& height [[buffer(4)]],
    constant int& step_size [[buffer(5)]],
    uint2 gid [[thread_position_in_grid]])
{
    int px = (int)gid.x;
    int py = (int)gid.y;
    if (px >= (int)width || py >= (int)height) return;

    uint idx = py * width + px;
    GBufferPixel center_pixel = gbuffer[idx];

    if (center_pixel.particle_id == GBUFFER_SKY_ID) {
        ssao_output[idx] = ssdo_pack(float4(0.0f, 0.0f, 0.0f, 1.0f));
        return;
    }

    float3 center_normal = normalize(float3(center_pixel.normal));
    float3 center_pos = float3(center_pixel.world_pos);
    float4 center_val = ssdo_unpack(ssao_input[idx]);

    const float h[5] = {1.0f/16.0f, 4.0f/16.0f, 6.0f/16.0f, 4.0f/16.0f, 1.0f/16.0f};

    const float SIGMA_NORMAL = 128.0f;
    const float SIGMA_POSITION = 1.0f;
    const float SIGMA_VALUE = 0.2f;

    float4 sum = float4(0.0f);
    float weight_total = 0.0f;

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int nx = px + dx * step_size;
            int ny = py + dy * step_size;
            if (nx < 0 || nx >= (int)width || ny < 0 || ny >= (int)height) continue;

            uint n_idx = ny * width + nx;
            GBufferPixel neighbor = gbuffer[n_idx];
            if (neighbor.particle_id == GBUFFER_SKY_ID) continue;

            float4 n_val = ssdo_unpack(ssao_input[n_idx]);

            float spatial_w = h[dx + 2] * h[dy + 2];

            float3 n_normal = normalize(float3(neighbor.normal));
            float ndot = max(dot(center_normal, n_normal), 0.0f);
            float normal_w = pow(ndot, SIGMA_NORMAL);

            float3 n_pos = float3(neighbor.world_pos);
            float pos_dist = length(center_pos - n_pos);
            float pos_sigma = SIGMA_POSITION * float(max(step_size, 1));
            float position_w = exp(-pos_dist / pos_sigma);

            float val_diff = abs(center_val.w - n_val.w);
            float value_w = exp(-val_diff / SIGMA_VALUE);

            float w = spatial_w * normal_w * position_w * value_w;
            sum += n_val * w;
            weight_total += w;
        }
    }

    ssao_output[idx] = ssdo_pack(sum / max(weight_total, 1e-6f));
}
