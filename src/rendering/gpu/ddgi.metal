// =========================================================================
// DDGI: Dynamic Diffuse Global Illumination (Probe-Based)
// =========================================================================
// Majercik et al. 2019: "Dynamic Diffuse Global Illumination with
// Ray-Traced Irradiance Fields"
//
// Pass 2.5b: ddgi_trace_probes — trace rays from each probe, evaluate
//            direct lighting at hit points
// Pass 2.5c: ddgi_update_probes — blend ray results into octahedral
//            irradiance/depth maps with temporal hysteresis
// =========================================================================

#include <metal_stdlib>
#include <metal_raytracing>
#include "gpu_types.metal"
#include "gpu_constants.metal"
#include "gbuffer_types.metal"

using namespace metal;
using namespace raytracing;

// =========================================================================
// HELPERS
// =========================================================================

// Octahedral encoding: unit direction -> [0,1]^2
// Cigolle et al. 2014
inline float2 oct_encode(float3 n) {
    float3 a = abs(n);
    float sum = a.x + a.y + a.z;
    float2 o = n.xy / sum;
    if (n.z < 0.0f) {
        float2 s = float2(o.x >= 0.0f ? 1.0f : -1.0f,
                          o.y >= 0.0f ? 1.0f : -1.0f);
        o = (1.0f - abs(o.yx)) * s;
    }
    return o * 0.5f + 0.5f;
}

// Octahedral decoding: [0,1]^2 -> unit direction
inline float3 oct_decode(float2 uv) {
    uv = uv * 2.0f - 1.0f;
    float3 n = float3(uv.x, uv.y, 1.0f - abs(uv.x) - abs(uv.y));
    if (n.z < 0.0f) {
        float2 s = float2(n.x >= 0.0f ? 1.0f : -1.0f,
                          n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * s;
    }
    return normalize(n);
}

// Fibonacci sphere: deterministic uniform distribution on unit sphere
inline float3 fibonacci_sphere(uint index, uint total) {
    float golden_ratio = (1.0f + sqrt(5.0f)) / 2.0f;
    float theta = 2.0f * M_PI_F * float(index) / golden_ratio;
    float phi = acos(1.0f - 2.0f * (float(index) + 0.5f) / float(total));
    return float3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

// Per-frame rotation matrix around Y axis
inline float3 rotate_y(float3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

// Octahedral map: 8x8 = 64 texels per probe
constant uint DDGI_TEXELS_PER_PROBE = 64;

// Probe grid helpers
inline uint3 probe_grid_idx(uint probe_id, uint3 dims) {
    uint z = probe_id / (dims.x * dims.y);
    uint rem = probe_id % (dims.x * dims.y);
    uint y = rem / dims.x;
    uint x = rem % dims.x;
    return uint3(x, y, z);
}

inline float3 probe_world_pos(uint3 idx, float3 origin, float spacing) {
    return origin + float3(idx) * spacing;
}

// =========================================================================
// PASS 2.5b: DDGI PROBE TRACING
// =========================================================================
// Each thread traces one ray from one probe. Evaluates direct lighting
// at the hit point to determine bounced radiance.

kernel void ddgi_trace_probes(
    primitive_acceleration_structure accel [[buffer(0)]],
    constant Triangle* bvh_triangles [[buffer(1)]],
    constant uint& bvh_triangle_count [[buffer(2)]],
    constant LightData* lights [[buffer(3)]],
    constant uint& light_count [[buffer(4)]],
    constant DDGIParams& params [[buffer(5)]],
    device float4* ray_results [[buffer(6)]],
    uint tid [[thread_position_in_grid]])
{
    uint total_rays = uint3(params.grid_dimensions).x * uint3(params.grid_dimensions).y *
                      uint3(params.grid_dimensions).z * params.rays_per_probe;
    if (tid >= total_rays) return;

    uint probe_id = tid / params.rays_per_probe;
    uint ray_idx = tid % params.rays_per_probe;

    uint3 dims = uint3(uint3(params.grid_dimensions).x, uint3(params.grid_dimensions).y, uint3(params.grid_dimensions).z);
    uint3 grid_idx = probe_grid_idx(probe_id, dims);
    float3 probe_pos = probe_world_pos(grid_idx, float3(params.grid_origin), params.probe_spacing);

    // Deterministic ray direction: fibonacci sphere rotated per frame
    float3 dir = fibonacci_sphere(ray_idx, params.rays_per_probe);
    float golden_angle = 2.0f * M_PI_F / ((1.0f + sqrt(5.0f)) / 2.0f);
    dir = rotate_y(dir, float(params.frame_index) * golden_angle);

    // Trace ray
    intersector<triangle_data> probe_intersector;
    probe_intersector.accept_any_intersection(false); // closest hit

    ray probe_ray;
    probe_ray.origin = probe_pos;
    probe_ray.direction = dir;
    probe_ray.min_distance = 0.01f;
    probe_ray.max_distance = 20.0f; // Max probe range

    auto result = probe_intersector.intersect(probe_ray, accel);

    float3 radiance = float3(0.0f);
    float hit_dist = 20.0f;

    if (result.type != intersection_type::none) {
        hit_dist = result.distance;
        Triangle hit_tri = bvh_triangles[result.primitive_id];

        // Hit albedo
        float3 hit_albedo = float3(
            hit_tri.base_color[2] / 255.0f,
            hit_tri.base_color[1] / 255.0f,
            hit_tri.base_color[0] / 255.0f
        );

        // Hit normal (flat shading)
        float3 e1 = float3(hit_tri.v1) - float3(hit_tri.v0);
        float3 e2 = float3(hit_tri.v2) - float3(hit_tri.v0);
        float3 hit_normal = normalize(cross(e1, e2));
        if (dot(dir, hit_normal) > 0.0f) hit_normal = -hit_normal;

        // Hit position
        float3 hit_pos = probe_pos + dir * hit_dist;

        // Evaluate direct lighting at hit point
        float3 hit_direct = float3(0.0f);
        for (uint li = 0; li < light_count; li++) {
            float3 to_light = float3(lights[li].position) - hit_pos;
            float ldist_sq = dot(to_light, to_light);
            if (ldist_sq > lights[li].emission_radius * lights[li].emission_radius) continue;

            float3 ldir = normalize(to_light);
            float NdotL = max(dot(hit_normal, ldir), 0.0f);
            if (NdotL > 0.0f) {
                float lux = lights[li].emission_strength / (FOUR_PI * max(ldist_sq, 0.01f)) * NdotL;
                hit_direct += lux * float3(lights[li].emission_color);
            }
        }

        // Bounced radiance = albedo * direct illumination at hit
        radiance = hit_albedo * hit_direct;

        // Clamp to prevent fireflies
        float max_comp = max(max(radiance.r, radiance.g), radiance.b);
        if (max_comp > 100.0f) radiance *= 100.0f / max_comp;
    }

    ray_results[tid] = float4(radiance, hit_dist);
}

// =========================================================================
// PASS 2.5c: DDGI PROBE UPDATE
// =========================================================================
// Each thread updates one texel of one probe's octahedral irradiance map.
// Blends new ray data with previous frame via hysteresis.

kernel void ddgi_update_probes(
    constant float4* ray_results [[buffer(0)]],
    device float4* irradiance [[buffer(1)]],
    device float2* depth [[buffer(2)]],
    constant DDGIParams& params [[buffer(3)]],
    uint tid [[thread_position_in_grid]])
{
    uint total_probes = uint3(params.grid_dimensions).x * uint3(params.grid_dimensions).y * uint3(params.grid_dimensions).z;
    uint total_texels = total_probes * DDGI_TEXELS_PER_PROBE;
    if (tid >= total_texels) return;

    uint probe_id = tid / DDGI_TEXELS_PER_PROBE;
    uint texel_id = tid % DDGI_TEXELS_PER_PROBE;

    // Texel UV in octahedral map
    uint tx = texel_id % 8;
    uint ty = texel_id / 8;
    float2 uv = (float2(tx, ty) + 0.5f) / 8.0f;
    float3 texel_dir = oct_decode(uv);

    // Regenerate ray directions (same as trace kernel)
    float golden_angle = 2.0f * M_PI_F / ((1.0f + sqrt(5.0f)) / 2.0f);

    // Accumulate weighted ray contributions
    float3 irr_sum = float3(0.0f);
    float irr_weight = 0.0f;
    float depth_sum = 0.0f;
    float depth2_sum = 0.0f;
    float depth_weight = 0.0f;

    uint ray_base = probe_id * params.rays_per_probe;

    for (uint ri = 0; ri < params.rays_per_probe; ri++) {
        float3 ray_dir = fibonacci_sphere(ri, params.rays_per_probe);
        ray_dir = rotate_y(ray_dir, float(params.frame_index) * golden_angle);

        float4 rd = ray_results[ray_base + ri];
        float3 ray_radiance = rd.xyz;
        float ray_dist = rd.w;

        // Cosine lobe weight for irradiance
        float cos_weight = max(0.0f, dot(ray_dir, texel_dir));
        if (cos_weight > 0.0001f) {
            irr_sum += ray_radiance * cos_weight;
            irr_weight += cos_weight;
        }

        // Depth weight (sharper lobe for depth precision)
        float depth_cos = max(0.0f, dot(ray_dir, texel_dir));
        if (depth_cos > 0.0001f) {
            depth_sum += ray_dist * depth_cos;
            depth2_sum += ray_dist * ray_dist * depth_cos;
            depth_weight += depth_cos;
        }
    }

    // Normalize
    float3 new_irradiance = irr_weight > 0.0001f ? irr_sum / irr_weight : float3(0.0f);
    float new_depth = depth_weight > 0.0001f ? depth_sum / depth_weight : 20.0f;
    float new_depth2 = depth_weight > 0.0001f ? depth2_sum / depth_weight : 400.0f;

    // Hysteresis blend with previous frame
    uint buf_idx = probe_id * 64 + texel_id;
    float4 prev_irr = irradiance[buf_idx];
    float2 prev_depth = depth[buf_idx];

    float alpha = params.hysteresis;
    irradiance[buf_idx] = float4(
        mix(prev_irr.xyz, new_irradiance, alpha),
        1.0f
    );
    depth[buf_idx] = float2(
        mix(prev_depth.x, new_depth, alpha),
        mix(prev_depth.y, new_depth2, alpha)
    );
}

