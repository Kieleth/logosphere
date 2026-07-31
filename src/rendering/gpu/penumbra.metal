//
// penumbra.metal
// Post-process penumbra computation (Pass 2.5 — between shadow rays and lighting)
//
// ARCHITECTURE: kernels that soften hard shadow boundaries:
//   C: penumbra_blocker_analysis — Uses blocker distance neighborhood to estimate
//      penumbra width. Closer blockers → sharper edges, farther → wider blur.
//   A+C (SHIPPING): jfa_seed + jfa_propagate + blur_h/blur_v — jump-flood the
//      penumbra width outward, then separable edge-preserving blur.
//   (Approach A alone, penumbra_screen_gradient, was retired 2026-07-29 —
//    never selected once A+C existed.)
//
// BUFFER STRATEGY: kernels read from shadow_input (copy of hard shadow lux)
// and write to shadow_output (the actual shadow_results buffer that Pass 3 reads).
// The CPU dispatch copies shadow_results → temp before each kernel invocation.
//
// For A+C combined (BLOCKER_GRADIENT): C runs first writing shadow_results,
// then shadow_results is copied to temp, then A runs reading temp → shadow_results.
//

#include <metal_stdlib>
#include "gbuffer_types.metal"
using namespace metal;

// =========================================================================
// CONFIGURABLE PARAMETERS (compile-time)
// =========================================================================

// Screen gradient (Approach A) parameters
constant float GRADIENT_THRESHOLD = 0.05f;      // Min gradient magnitude to trigger blur
constant int   GRADIENT_BLUR_RADIUS = 8;        // Max blur kernel radius (pixels) → 17x17 kernel
constant float GRADIENT_BLUR_SCALE = 8.0f;      // Gradient-to-radius multiplier

// Blocker analysis (Approach C) parameters
constant float BLOCKER_MIN_DISTANCE = 0.01f;    // Minimum blocker distance (avoid div/0)
constant float BLOCKER_PENUMBRA_SCALE = 2.0f;   // Penumbra width = blocker_dist * light_size * scale.
                                                 // ⚠ DO NOT BUMP THIS WITHOUT FIRST FIXING THE BLUR KERNEL.
                                                 // Bumping to 80 was tried (commit pending revert) — it
                                                 // softened shadow edges but destroyed body lighting because
                                                 // the Gaussian blur on lines 273-294 has NO edge-awareness:
                                                 // it averages shadow+lit pixels together inside the kernel.
                                                 // At scale=2 the kernel collapses to ≤1 px so the bug was
                                                 // invisible. At scale=80 the kernel is 8-32 px and any lit
                                                 // pixel within that radius of a shadow gets dragged dark.
                                                 // Bike top went black even though its normal pointed
                                                 // straight at the only light. Real fix is either edge-aware
                                                 // blur (skip mixing across lux-discontinuities) or true PCSS
                                                 // with pixels-per-meter plumbed through. Until then, scale=2
                                                 // is the only safe value. See tests/test_shadow_penumbra_softness.cpp
                                                 // (which currently FAILS for this scene — that's expected;
                                                 // it's the architectural defect, not a regression).
constant int   BLOCKER_SEARCH_RADIUS = 32;      // Neighborhood search radius (pixels) → 65x65 kernel

// =========================================================================
// KERNEL A: Screen-Space Gradient Penumbra
// =========================================================================
//
// Algorithm:
//   1. Compute 3x3 Sobel gradient magnitude of shadow lux values
//   2. If gradient < threshold → pass through unchanged (sharp interior)
//   3. If gradient >= threshold → weighted average of NxN neighborhood
//      where N = clamp(gradient * scale, 1, GRADIENT_BLUR_RADIUS)
//   4. Weights: Gaussian-ish (1/distance²) with edge-aware clamping
//
// This produces smooth penumbra at shadow edges without blurring interiors.
//
// =========================================================================
//
// Algorithm:
//   1. For each pixel, read its blocker_distance and shadow lux
//   2. If pixel is fully lit (blocker_distance == 0 AND lux > 0) → pass through
//   3. If pixel is in shadow boundary region:
//      a. Search NxN neighborhood for lit pixels (those with lux > 0)
//      b. Weight lit neighbor contributions by:
//         - Spatial distance (Gaussian falloff)
//         - Blocker distance ratio (closer blocker → less spread)
//      c. Blend: shadow_pixel gets partial illumination from nearby lit pixels
//
// The key insight: blocker_distance tells us HOW FAR the shadow-casting
// geometry is. A blocker at 0.1m casts a crisp shadow (tiny penumbra).
// A blocker at 5m casts a wide penumbra (light wraps around more).
//
// Penumbra width ≈ light_size × (receiver_to_blocker_dist / blocker_to_light_dist)
// But we don't have blocker-to-light distance separately. We approximate:
//   penumbra_radius ∝ blocker_distance × light_size_factor
//
kernel void penumbra_blocker_analysis(
    constant float* shadow_input [[buffer(0)]],      // Read: copy of hard shadow lux
    device float* shadow_output [[buffer(1)]],        // Write: softened shadow lux
    constant float* blocker_distance [[buffer(2)]],   // Read: per-pixel closest blocker distance
    constant uint& width [[buffer(3)]],
    constant uint& height [[buffer(4)]],
    constant float& light_size [[buffer(5)]],         // Light source apparent size (meters)
    uint gid [[thread_position_in_grid]])
{
    if (gid >= width * height) return;

    uint px = gid % width;
    uint py = gid / width;

    float center_lux = shadow_input[gid];
    float center_blocker = blocker_distance[gid];

    // ---------------------------------------------------------------
    // Deep shadow interior optimization: if this pixel and all 4
    // neighbors are fully dark, no amount of blurring will change it.
    // ---------------------------------------------------------------
    if (center_lux < 0.001f && px >= 1 && px < width - 1 && py >= 1 && py < height - 1) {
        float n_lux = shadow_input[(py - 1) * width + px] +
                      shadow_input[(py + 1) * width + px] +
                      shadow_input[py * width + (px - 1)] +
                      shadow_input[py * width + (px + 1)];
        if (n_lux < 0.001f) {
            shadow_output[gid] = center_lux;
            return;
        }
    }

    // ---------------------------------------------------------------
    // Compute penumbra width from blocker distance.
    // For lit pixels (center_blocker ≈ 0), scan a small neighborhood
    // for the maximum blocker-derived penumbra width. This allows
    // lit pixels near shadow edges to participate in the penumbra
    // blur — essential for large area lights that need WIDER penumbra
    // extending into the lit region.
    // ---------------------------------------------------------------
    float penumbra_width;
    if (center_blocker >= BLOCKER_MIN_DISTANCE) {
        // Shadowed pixel: compute directly from its blocker distance
        penumbra_width = center_blocker * light_size * BLOCKER_PENUMBRA_SCALE;
    } else {
        // Lit pixel: check nearby pixels for shadow influence.
        // Scan BLOCKER_SEARCH_RADIUS to find if any shadowed pixel's penumbra
        // reaches this lit pixel. Use early-exit spiral: check inner rings first,
        // bail if nothing found within growing radius.
        float max_nearby_pw = 0.0f;
        // Two-phase: fast 4px check, then full if needed
        int probe_radius = BLOCKER_SEARCH_RADIUS;
        bool found_blocker = false;
        for (int ring = 1; ring <= probe_radius && !found_blocker; ring++) {
            // Only check the ring perimeter (not filled square) for efficiency
            for (int dy = -ring; dy <= ring; dy++) {
                // Skip interior — only check perimeter of this ring
                if (abs(dy) != ring) {
                    // Only check the two ends of this row
                    for (int dx_sign = -1; dx_sign <= 1; dx_sign += 2) {
                        int dx = dx_sign * ring;
                        int sx = (int)px + dx;
                        int sy = (int)py + dy;
                        if (sx < 0 || sx >= (int)width || sy < 0 || sy >= (int)height) continue;
                        float nb = blocker_distance[sy * (int)width + sx];
                        if (nb >= BLOCKER_MIN_DISTANCE) {
                            float pw = nb * light_size * BLOCKER_PENUMBRA_SCALE;
                            float dist = sqrt((float)(dx * dx + dy * dy));
                            if (dist < pw) {
                                max_nearby_pw = max(max_nearby_pw, pw);
                                found_blocker = true;
                            }
                        }
                    }
                } else {
                    // Top/bottom row: check all columns
                    for (int dx = -ring; dx <= ring; dx++) {
                        int sx = (int)px + dx;
                        int sy = (int)py + dy;
                        if (sx < 0 || sx >= (int)width || sy < 0 || sy >= (int)height) continue;
                        float nb = blocker_distance[sy * (int)width + sx];
                        if (nb >= BLOCKER_MIN_DISTANCE) {
                            float pw = nb * light_size * BLOCKER_PENUMBRA_SCALE;
                            float dist = sqrt((float)(dx * dx + dy * dy));
                            if (dist < pw) {
                                max_nearby_pw = max(max_nearby_pw, pw);
                                found_blocker = true;
                            }
                        }
                    }
                }
            }
        }
        if (max_nearby_pw < 1.0f) {
            // No nearby shadow influence — pass through unchanged
            shadow_output[gid] = center_lux;
            return;
        }
        penumbra_width = max_nearby_pw;
    }

    int search_radius = clamp((int)(penumbra_width + 0.5f), 1, BLOCKER_SEARCH_RADIUS);

    float weighted_sum = 0.0f;
    float weight_total = 0.0f;

    for (int dy = -search_radius; dy <= search_radius; dy++) {
        int sy = (int)py + dy;
        if (sy < 0 || sy >= (int)height) continue;

        for (int dx = -search_radius; dx <= search_radius; dx++) {
            int sx = (int)px + dx;
            if (sx < 0 || sx >= (int)width) continue;

            uint neighbor_idx = sy * (int)width + sx;
            float neighbor_lux = shadow_input[neighbor_idx];

            // Spatial weight: Gaussian falloff with penumbra_width as sigma
            float dist_sq = (float)(dx * dx + dy * dy);
            float sigma = max(penumbra_width * 0.5f, 0.5f);
            float spatial_w = exp(-dist_sq / (2.0f * sigma * sigma));

            weighted_sum += neighbor_lux * spatial_w;
            weight_total += spatial_w;
        }
    }

    shadow_output[gid] = (weight_total > 0.0f) ? (weighted_sum / weight_total) : center_lux;
}

// =========================================================================
// JFA + SEPARABLE BLUR PENUMBRA (Tier 2 — replaces Kernel C)
// =========================================================================
//
// Architecture:
//   1. Seed: Convert blocker_distance to penumbra_width per pixel
//   2. JFA propagate: Flood penumbra_width from shadow pixels to nearby lit
//      pixels in O(log N) passes, 9 neighbors each
//   3. Separable blur H + V: two 1D Gaussian passes using per-pixel width
//
// Total: ~8 cheap dispatches instead of 1 expensive O(N²) dispatch.
// =========================================================================

constant int JFA_MAX_PENUMBRA = 32;

// Step 1: Seed penumbra width from blocker distance
kernel void penumbra_jfa_seed(
    constant float* blocker_distance [[buffer(0)]],
    device float* penumbra_width_out [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant float& light_size [[buffer(4)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= width * height) return;
    float bd = blocker_distance[gid];
    if (bd >= BLOCKER_MIN_DISTANCE) {
        float pw = bd * light_size * BLOCKER_PENUMBRA_SCALE;
        // Minimum 3px penumbra for any shadow boundary (prevents razor-sharp edges
        // from small/close lights while avoiding the massive blur from large minimums)
        penumbra_width_out[gid] = max(pw, 3.0f);
    } else {
        penumbra_width_out[gid] = 0.0f;
    }
}

// Step 2: JFA propagation (run with step sizes 32, 16, 8, 4, 2, 1)
kernel void penumbra_jfa_propagate(
    constant float* pw_in [[buffer(0)]],
    device float* pw_out [[buffer(1)]],
    constant uint& width [[buffer(2)]],
    constant uint& height [[buffer(3)]],
    constant int& step_size [[buffer(4)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= width * height) return;

    int px = (int)(gid % width);
    int py = (int)(gid / width);
    float best_pw = pw_in[gid];

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = px + dx * step_size;
            int ny = py + dy * step_size;
            if (nx < 0 || nx >= (int)width || ny < 0 || ny >= (int)height) continue;

            float neighbor_pw = pw_in[ny * (int)width + nx];
            if (neighbor_pw <= 0.0f) continue;

            float dist = sqrt((float)((px - nx) * (px - nx) + (py - ny) * (py - ny)));
            if (dist < neighbor_pw) {
                // Attenuate: further from shadow source = narrower effective penumbra
                float effective = neighbor_pw - dist;
                best_pw = max(best_pw, effective);
            }
        }
    }
    pw_out[gid] = best_pw;
}

// Step 3: Edge-preserving separable Gaussian blur (horizontal)
// Skips neighbors with different particle_id to prevent cross-object bleeding.
kernel void penumbra_blur_h(
    constant float* shadow_input [[buffer(0)]],
    device float* shadow_output [[buffer(1)]],
    constant float* penumbra_width [[buffer(2)]],
    constant uint& width [[buffer(3)]],
    constant uint& height [[buffer(4)]],
    constant GBufferPixel* gbuffer [[buffer(5)]],
#if PENUMBRA_COMPACT_IDS
    device uint* id_out [[buffer(7)]],
#endif
    uint gid [[thread_position_in_grid]])
{
    if (gid >= width * height) return;

    int px = (int)(gid % width);
    int py = (int)(gid / width);
#if PENUMBRA_COMPACT_IDS
    uint center_id = gbuffer[gid].particle_id;
    id_out[gid] = center_id;   // packed id stream for the V-blur taps
#endif
    float pw = penumbra_width[gid];

    if (pw < 1.0f) {
        shadow_output[gid] = shadow_input[gid];
        return;
    }

#if !PENUMBRA_COMPACT_IDS
    uint center_id = gbuffer[gid].particle_id;
#endif
    float center_lux = shadow_input[gid];
    int radius = min((int)(pw + 0.5f), JFA_MAX_PENUMBRA);
    float sigma = max(pw * 0.5f, 0.5f);
    float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    float sum = 0.0f, wt = 0.0f;

    for (int dx = -radius; dx <= radius; dx++) {
        int sx = px + dx;
        if (sx < 0 || sx >= (int)width) continue;
        uint nidx = py * (int)width + sx;
        if (gbuffer[nidx].particle_id != center_id) continue;

        float neighbor_lux = shadow_input[nidx];
        float spatial_w = exp(-(float)(dx * dx) * inv2s2);

        // Range weight: neighbors with very different brightness contribute less.
        // Prevents lit floor from averaging with shadow (dark halo artifact).
        float lux_diff = center_lux - neighbor_lux;
        float range_sigma = max(center_lux * 0.3f, 5.0f);  // 30% of center brightness
        float range_w = exp(-(lux_diff * lux_diff) / (2.0f * range_sigma * range_sigma));

        float w = spatial_w * range_w;
        sum += neighbor_lux * w;
        wt += w;
    }
    shadow_output[gid] = (wt > 0.0f) ? (sum / wt) : shadow_input[gid];
}

// Step 4: Edge-preserving separable Gaussian blur (vertical)
kernel void penumbra_blur_v(
    constant float* shadow_input [[buffer(0)]],
    device float* shadow_output [[buffer(1)]],
    constant float* penumbra_width [[buffer(2)]],
    constant uint& width [[buffer(3)]],
    constant uint& height [[buffer(4)]],
#if PENUMBRA_COMPACT_IDS
    constant uint* particle_ids [[buffer(5)]],
#else
    constant GBufferPixel* gbuffer [[buffer(5)]],
#endif
    uint gid [[thread_position_in_grid]])
{
    if (gid >= width * height) return;

    int px = (int)(gid % width);
    int py = (int)(gid / width);
    float pw = penumbra_width[gid];

    if (pw < 1.0f) {
        shadow_output[gid] = shadow_input[gid];
        return;
    }

#if PENUMBRA_COMPACT_IDS
    uint center_id = particle_ids[gid];
#else
    uint center_id = gbuffer[gid].particle_id;
#endif
    float center_lux = shadow_input[gid];
    int radius = min((int)(pw + 0.5f), JFA_MAX_PENUMBRA);
    float sigma = max(pw * 0.5f, 0.5f);
    float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    float sum = 0.0f, wt = 0.0f;

    for (int dy = -radius; dy <= radius; dy++) {
        int sy = py + dy;
        if (sy < 0 || sy >= (int)height) continue;
        uint nidx = sy * (int)width + px;
#if PENUMBRA_COMPACT_IDS
        if (particle_ids[nidx] != center_id) continue;
#else
        if (gbuffer[nidx].particle_id != center_id) continue;
#endif

        float neighbor_lux = shadow_input[nidx];
        float spatial_w = exp(-(float)(dy * dy) * inv2s2);
        float lux_diff = center_lux - neighbor_lux;
        float range_sigma = max(center_lux * 0.3f, 5.0f);
        float range_w = exp(-(lux_diff * lux_diff) / (2.0f * range_sigma * range_sigma));

        float w = spatial_w * range_w;
        sum += neighbor_lux * w;
        wt += w;
    }
    shadow_output[gid] = (wt > 0.0f) ? (sum / wt) : shadow_input[gid];
}
