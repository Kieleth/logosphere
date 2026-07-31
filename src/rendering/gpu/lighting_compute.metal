#include <metal_stdlib>
#include "gpu_constants.metal"
#include "gpu_types.metal"
using namespace metal;

// Calculate light intensity at given distance squared (physics-based)
// Formula: intensity_lux = lumens / (4π * distance²)
inline float calculate_intensity(float emission_strength_lumens, float distance_sq) {
    float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
    return emission_strength_lumens / (FOUR_PI * safe_distance_sq);
}

// Compute lighting for a batch of pixels
// One thread per pixel, processes all lights for that pixel
kernel void compute_lighting(
    device const PixelData* pixels [[buffer(0)]],        // Input: pixel positions/normals
    device const LightData* lights [[buffer(1)]],        // Input: light data
    device const float* shadow_results [[buffer(2)]],    // Input: shadow results (0.0=blocked, 1.0=clear)
    device float* lighting_output [[buffer(3)]],         // Output: lighting value per pixel
    constant uint& pixel_count [[buffer(4)]],            // Number of pixels
    constant uint& light_count [[buffer(5)]],            // Number of lights
    uint pixel_idx [[thread_position_in_grid]]           // Which pixel am I?
) {
    // Bounds check
    if (pixel_idx >= pixel_count) return;

    // Get pixel data
    PixelData pixel = pixels[pixel_idx];
    float total_lighting = 0.0f;

    // Process each light
    for (uint light_idx = 0; light_idx < light_count; light_idx++) {
        // Calculate shadow ray index (pixel × lights + light_idx)
        uint ray_idx = pixel_idx * light_count + light_idx;

        // Check if ray was blocked (shadow_results: 0.0=blocked, 1.0=clear)
        if (shadow_results[ray_idx] >= 0.5f) {  // Not blocked
            LightData light = lights[light_idx];

            // Vector from pixel to light
            float3 to_light = light.position - pixel.position;
            float distance_sq = dot(to_light, to_light);

            // Check if within light radius
            if (distance_sq <= light.emission_radius * light.emission_radius) {
                // Normalize direction (using rsqrt for speed)
                // Clamp to MIN_DISTANCE_SQ to avoid rsqrt(0) = infinity
                float safe_distance_sq = max(distance_sq, MIN_DISTANCE_SQ);
                float inv_dist = rsqrt(safe_distance_sq);  // 1/sqrt(safe_distance_sq)
                float3 light_dir = to_light * inv_dist;

                // Lambertian lighting (dot product with normal)
                float lambertian = max(dot(pixel.normal, light_dir), 0.0f);

                if (lambertian > 0.0f) {
                    // Calculate intensity (physics-based inverse square law)
                    float intensity_lux = calculate_intensity(light.emission_strength, safe_distance_sq);
                    total_lighting += intensity_lux * lambertian;
                }
            }
        }
    }

    // Write final lighting value for this pixel
    lighting_output[pixel_idx] = total_lighting;
}
