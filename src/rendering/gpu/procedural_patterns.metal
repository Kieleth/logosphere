//
// procedural_patterns.metal
// Procedural texture patterns via light absorption
//
// ARCHITECTURE: Modular pattern system
// - Noise primitives (hash, value noise, fbm)
// - Building blocks (composable Lego-style primitives)
// - Material patterns (wood, stone, fabrics)
// - Pattern dispatcher
//
// USAGE: #include "procedural_patterns.metal" in lighting pass
// Call compute_pattern() with world_pos, normal, center, axis, pattern_id
//

#ifndef PROCEDURAL_PATTERNS_METAL
#define PROCEDURAL_PATTERNS_METAL

#include <metal_stdlib>
using namespace metal;

// ============================================================================
// SECTION 1: PATTERN IDS
// ============================================================================
// Used by CPU to assign materials, GPU to dispatch pattern functions

constant uint8_t PATTERN_NONE        = 0;
constant uint8_t PATTERN_WOOD        = 1;
constant uint8_t PATTERN_STONE       = 2;
constant uint8_t PATTERN_LEATHER     = 3;
constant uint8_t PATTERN_LINEN       = 4;
constant uint8_t PATTERN_COTTON      = 5;
constant uint8_t PATTERN_SILK        = 6;
constant uint8_t PATTERN_EMBROIDERY  = 7;
// Organic patterns
constant uint8_t PATTERN_SKIN        = 8;   // Human skin (pores, veins, subtle variation)
constant uint8_t PATTERN_SKIN_REPTILE= 9;   // Scales
constant uint8_t PATTERN_HAIR        = 10;  // Hair strands/fur

// ============================================================================
// SECTION 2: NOISE PRIMITIVES
// ============================================================================
// Foundation functions for procedural generation

// Fast deterministic hash
inline float hash3d(float3 p) {
    p = fract(p * float3(443.8975f, 397.2973f, 491.1871f));
    p += dot(p, p.yxz + 19.19f);
    return fract((p.x + p.y) * p.z);
}

// Smooth interpolated noise (quintic Hermite)
inline float value_noise(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float c000 = hash3d(i + float3(0, 0, 0));
    float c100 = hash3d(i + float3(1, 0, 0));
    float c010 = hash3d(i + float3(0, 1, 0));
    float c110 = hash3d(i + float3(1, 1, 0));
    float c001 = hash3d(i + float3(0, 0, 1));
    float c101 = hash3d(i + float3(1, 0, 1));
    float c011 = hash3d(i + float3(0, 1, 1));
    float c111 = hash3d(i + float3(1, 1, 1));

    float c00 = mix(c000, c100, u.x);
    float c10 = mix(c010, c110, u.x);
    float c01 = mix(c001, c101, u.x);
    float c11 = mix(c011, c111, u.x);

    return mix(mix(c00, c10, u.y), mix(c01, c11, u.y), u.z);
}

// Fractal Brownian Motion - layered noise for natural patterns
inline float fbm(float3 p, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * value_noise(p * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value;
}

// ============================================================================
// SECTION 3: BUILDING BLOCKS - ORGANIC
// ============================================================================
// Composable primitives for natural materials (wood, stone)

inline float block_ridges(float3 p, float frequency, float sharpness) {
    float n = value_noise(p * frequency);
    return pow(1.0f - abs(n * 2.0f - 1.0f), sharpness);
}

inline float block_cracks(float3 p, float scale, float width) {
    float3 cell = floor(p * scale);
    float min_dist = 1.0f;

    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                float3 neighbor = cell + float3(dx, dy, dz);
                float3 point = neighbor + float3(
                    hash3d(neighbor),
                    hash3d(neighbor + float3(1,0,0)),
                    hash3d(neighbor + float3(0,1,0))
                );
                min_dist = min(min_dist, length(p * scale - point));
            }
        }
    }
    return smoothstep(0.0f, width, min_dist);
}

inline float block_waves(float coord, float frequency, float distort) {
    return sin(coord * frequency * 6.28318f + distort) * 0.5f + 0.5f;
}

inline float block_fissures(float3 p, float density, float depth) {
    float crack1 = block_waves(p.x + fbm(p * 2.0f, 2) * 0.5f, density, p.y * 3.0f);
    float crack2 = block_waves(p.y + fbm(p * 1.5f, 2) * 0.4f, density * 0.7f, p.x * 2.0f);
    return pow(min(crack1, crack2), depth);
}

inline float3 block_warp(float3 p, float amount) {
    return p + float3(
        value_noise(p * 2.0f + float3(0, 0, 0)) * amount,
        value_noise(p * 2.0f + float3(5, 3, 1)) * amount,
        value_noise(p * 2.0f + float3(9, 7, 2)) * amount
    );
}

inline float block_knot(float3 p, float3 knot_center, float radius) {
    float dist = length(p - knot_center);
    float influence = smoothstep(radius, 0.0f, dist);
    float swirl = sin(dist * 15.0f - atan2(p.y - knot_center.y, p.x - knot_center.x) * 3.0f);
    return influence * (swirl * 0.5f + 0.5f);
}

// ============================================================================
// SECTION 4: BUILDING BLOCKS - FABRIC
// ============================================================================
// Composable primitives for textiles (cloth, leather)

inline float block_weave(float2 uv, float thread_freq, float tightness) {
    float warp = sin(uv.x * thread_freq * 6.28318f);
    float weft = sin(uv.y * thread_freq * 6.28318f);
    float checker = fract(floor(uv.x * thread_freq) + floor(uv.y * thread_freq));
    float over_under = (checker < 0.5f ? warp : weft) * 0.5f + 0.5f;
    return pow(over_under, tightness);
}

inline float block_stitch(float2 uv, float stitch_len, float stitch_width) {
    float2 cell = fract(uv * stitch_len);
    float stitch = smoothstep(0.0f, stitch_width, cell.x) *
                   smoothstep(1.0f, 1.0f - stitch_width, cell.x);
    return stitch * (sin(cell.x * 20.0f) * 0.1f + 0.9f);
}

inline float block_pores(float3 p, float density, float depth) {
    return smoothstep(0.4f, 0.6f, value_noise(p * density)) * depth;
}

inline float block_wrinkles(float3 p, float scale, float intensity) {
    float3 warped = block_warp(p * scale, 0.2f);
    float w1 = block_ridges(warped, 3.0f, 1.5f);
    float w2 = block_ridges(warped * 1.7f + float3(5,3,1), 2.0f, 2.0f);
    return (w1 * 0.6f + w2 * 0.4f) * intensity;
}

// ============================================================================
// SECTION 5: HELPER - UV PROJECTION
// ============================================================================
// Project 3D position to 2D UV based on surface normal

inline float2 project_to_uv(float3 local_pos, float3 normal) {
    if (abs(normal.z) > 0.7f) {
        return float2(local_pos.x, local_pos.y);
    } else if (abs(normal.y) > 0.7f) {
        return float2(local_pos.x, local_pos.z);
    } else {
        return float2(local_pos.y, local_pos.z);
    }
}

// ============================================================================
// SECTION 6: MATERIAL PATTERNS - NATURAL
// ============================================================================

inline float pattern_wood(float3 world_pos, float3 normal, float3 center, float3 primary_axis) {
    float3 local_pos = world_pos - center;
    float axis_alignment = abs(dot(normal, primary_axis));
    bool is_end_grain = axis_alignment > 0.7f;

    // Compute position along axis and radial position perpendicular to axis
    float along_axis = dot(local_pos, primary_axis);
    float3 radial_vec = local_pos - along_axis * primary_axis;
    float radial_dist = length(radial_vec);

    // Build orthonormal basis perpendicular to primary_axis for consistent 2D projection
    // Pick a reference vector not parallel to axis
    float3 ref = abs(primary_axis.z) < 0.9f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent1 = normalize(cross(primary_axis, ref));
    float3 tangent2 = cross(primary_axis, tangent1);

    // Project radial position to 2D in the plane perpendicular to axis
    float2 radial_2d = float2(dot(radial_vec, tangent1), dot(radial_vec, tangent2));

    if (is_end_grain) {
        // DEBUG: Return constant 0.85 absorption to make end faces very dark
        // If you see very dark faces on log ends = is_end_grain detection works
        // If all faces have bark pattern = is_end_grain NEVER triggers

        // Tree rings on cut surface (ends of log)
        float dist = length(radial_2d);

        // Very bold concentric rings
        float ring_freq = 12.0f;
        float ring = sin(dist * ring_freq * 6.28318f) * 0.5f + 0.5f;
        float sharp_ring = pow(ring, 0.4f);

        // Medullary rays
        float angle = atan2(radial_2d.y, radial_2d.x);
        float rays = smoothstep(0.7f, 1.0f, sin(angle * 12.0f)) * 0.15f;

        // STRONG contrast - end faces will be obviously darker than bark
        return 0.25f + sharp_ring * 0.5f + rays;  // Base 0.25 + rings
    } else {
        // Bark on sides - fissures run ALONG the log's length (parallel to primary_axis)
        // Use coordinate perpendicular to both primary_axis and face normal
        float3 stripe_dir = cross(primary_axis, normal);
        float stripe_len = length(stripe_dir);
        if (stripe_len > 0.001f) {
            stripe_dir /= stripe_len;
        } else {
            // Fallback if normal is parallel to axis (shouldn't happen for bark faces)
            stripe_dir = cross(primary_axis, float3(0, 0, 1));
            stripe_dir = normalize(stripe_dir);
        }
        // Position along the stripe direction (perpendicular to log axis on this face)
        float stripe_coord = dot(local_pos, stripe_dir);
        float radial_angle = atan2(radial_2d.y, radial_2d.x);

        float fissure_freq = 8.0f + value_noise(float3(along_axis * 0.5f, stripe_coord, 0)) * 4.0f;
        // Fissures oscillate with stripe_coord, creating stripes parallel to primary_axis
        float fissures = pow(block_waves(stripe_coord * fissure_freq, 1.0f, along_axis * 2.0f), 0.3f);
        float h_cracks = pow(block_waves(along_axis * 3.0f, 2.0f, stripe_coord * 5.0f), 2.0f) * 0.3f;
        float rough = fbm(float3(stripe_coord * 10.0f, along_axis * 2.0f, 0), 3) * 0.2f;

        float knot = 0.0f;
        if (hash3d(floor(local_pos * 2.0f)) > 0.85f) {
            float3 knot_center = floor(local_pos * 2.0f) * 0.5f + 0.25f;
            knot = block_knot(local_pos, knot_center, 0.15f) * 0.25f;
        }

        return (fissures * (1.0f - h_cracks) + rough + knot) * 0.45f;
    }
}

inline float pattern_stone(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float stone_type = hash3d(floor(center * 0.5f));

    if (stone_type < 0.33f) {
        // Cracked stone
        float3 warped = block_warp(local_pos, 0.15f);
        float cracks = pow(block_cracks(warped, 4.0f, 0.15f), 0.5f);
        float weather = fbm(local_pos * 8.0f, 3) * 0.2f;
        float grain = value_noise(local_pos * 20.0f) * 0.1f;
        return (1.0f - cracks) * 0.4f + weather + grain;

    } else if (stone_type < 0.66f) {
        // River stone
        float3 warped = block_warp(local_pos * 0.5f, 0.1f);
        float undulate = fbm(warped * 3.0f, 4);
        float bands = block_waves(local_pos.z * 2.0f + fbm(local_pos, 2) * 0.3f, 1.5f, 0) * 0.15f;
        float pits = (1.0f - smoothstep(0.6f, 0.8f, value_noise(local_pos * 15.0f))) * 0.1f;
        return undulate * 0.2f + bands + pits;

    } else {
        // Crystalline
        float facets = block_cracks(local_pos, 6.0f, 0.3f);
        float ridges = block_ridges(local_pos, 5.0f, 2.0f);
        float sparkle = smoothstep(0.85f, 0.95f, value_noise(local_pos * 30.0f)) * 0.3f;
        float internal = fbm(local_pos * 4.0f, 2) * 0.15f;
        return (1.0f - facets) * 0.25f + ridges * 0.2f + internal - sparkle;
    }
}

// ============================================================================
// SECTION 7: MATERIAL PATTERNS - FABRIC
// ============================================================================

inline float pattern_leather(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = float2(local_pos.x + local_pos.z * 0.5f, local_pos.y + local_pos.z * 0.3f);

    float pores = block_pores(local_pos * 8.0f, 25.0f, 0.4f);
    float creases = block_wrinkles(local_pos * 2.0f, 1.5f, 0.35f);
    float grain_dir = pow(block_waves(uv.x * 15.0f + value_noise(local_pos * 3.0f) * 0.8f, 1.0f, uv.y * 2.0f), 3.0f) * 0.15f;
    float imperfections = value_noise(local_pos * 30.0f) * 0.1f;

    return pores + creases * 0.8f + grain_dir + imperfections;
}

inline float pattern_linen(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 15.0f;

    float weave = block_weave(uv, 8.0f, 1.2f);
    float irregularity = value_noise(local_pos * 40.0f) * 0.15f;
    float thread_thick = value_noise(float3(uv * 2.0f, 0)) * 0.1f;
    float wrinkle = block_wrinkles(local_pos * 0.8f, 2.0f, 0.2f);

    return weave * 0.4f + irregularity + thread_thick + wrinkle;
}

inline float pattern_cotton(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 25.0f;

    float weave = block_weave(uv, 12.0f, 2.0f);
    float fuzz = fbm(local_pos * 50.0f, 2) * 0.08f;
    float nap = block_waves(uv.y + value_noise(local_pos * 5.0f) * 0.3f, 0.5f, 0) * 0.05f;

    return weave * 0.25f + fuzz + nap;
}

inline float pattern_silk(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 30.0f;

    float weave = block_weave(uv, 20.0f, 3.0f);
    float flow = pow(block_waves(uv.y * 0.3f + fbm(local_pos * 2.0f, 2) * 0.5f, 0.8f, uv.x * 0.1f), 2.0f) * 0.12f;
    float shimmer = value_noise(local_pos * 100.0f) * 0.03f;
    float drape = sin(uv.y * 0.5f + value_noise(local_pos) * 2.0f) * 0.04f + 0.04f;

    return weave * 0.1f + flow + shimmer + drape;
}

inline float pattern_embroidery(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 10.0f;

    float base = block_weave(uv * 2.0f, 10.0f, 2.0f) * 0.15f;

    float2 stitch_cell = fract(uv);
    float cross = max(
        block_stitch(stitch_cell, 4.0f, 0.15f),
        block_stitch(float2(stitch_cell.y, stitch_cell.x), 4.0f, 0.15f)
    );

    float2 tile = fract(uv * 0.5f) - 0.5f;
    float diamond = 1.0f - smoothstep(0.2f, 0.25f, abs(tile.x) + abs(tile.y));
    float flower = smoothstep(0.15f, 0.1f, length(tile)) * (sin(atan2(tile.y, tile.x) * 6.0f) * 0.5f + 0.5f) * 0.3f;

    float raised = (cross * 0.3f + diamond * 0.2f + flower) * (1.0f + value_noise(local_pos * 20.0f) * 0.2f);

    return base + raised;
}

// ============================================================================
// SECTION 7B: MATERIAL PATTERNS - ORGANIC
// ============================================================================

// Human skin - subtle pores, subsurface veins, natural variation
inline float pattern_skin(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 40.0f;

    // Fine pores (tiny dark spots)
    float pores = block_pores(local_pos * 25.0f, 50.0f, 0.2f) * 0.12f;

    // Subsurface vein hints (very subtle blue-ish absorption)
    float3 vein_warp = block_warp(local_pos * 3.0f, 0.1f);
    float veins = smoothstep(0.4f, 0.6f, fbm(vein_warp * 8.0f, 2)) * 0.08f;

    // Natural skin variation (mottled, subtle)
    float mottling = fbm(local_pos * 12.0f, 2) * 0.06f;

    // Micro-texture (barely visible roughness)
    float micro = value_noise(local_pos * 80.0f) * 0.03f;

    return pores + veins + mottling + micro;
}

// Reptilian scales - overlapping hexagonal/diamond pattern
inline float pattern_skin_reptile(float3 world_pos, float3 normal, float3 center) {
    float3 local_pos = world_pos - center;
    float2 uv = project_to_uv(local_pos, normal) * 15.0f;

    // Hexagonal scale pattern
    float2 hex_uv = uv;
    hex_uv.x += floor(uv.y) * 0.5f;  // Offset rows for hex pattern
    float2 cell = fract(hex_uv) - 0.5f;

    // Scale edge (darker at edges, lighter in center)
    float dist = length(cell);
    float scale_edge = smoothstep(0.3f, 0.45f, dist) * 0.35f;

    // Scale ridge (raised center line)
    float ridge = smoothstep(0.1f, 0.0f, abs(cell.y)) * 0.15f;

    // Individual scale variation
    float scale_id = hash3d(floor(float3(hex_uv, 0)));
    float variation = scale_id * 0.1f;

    // Subtle iridescence (slight color shift based on viewing angle)
    float iridescence = abs(dot(normal, float3(0, 0, 1))) * 0.08f;

    return scale_edge + ridge + variation - iridescence;
}

// Hair/fur - parallel strand pattern with variation
inline float pattern_hair(float3 world_pos, float3 normal, float3 center, float3 primary_axis) {
    float3 local_pos = world_pos - center;

    // Hair strands run along primary_axis (head-to-tip direction)
    float along = dot(local_pos, primary_axis);
    float3 radial_dir = local_pos - primary_axis * along;
    float radial = length(radial_dir);
    float radial_angle = atan2(radial_dir.y, radial_dir.x);

    // Individual strands (high frequency perpendicular to growth)
    float strand_freq = 30.0f + value_noise(float3(radial_angle * 5.0f, along, 0)) * 10.0f;
    float strands = pow(block_waves(radial_angle * strand_freq, 1.0f, along * 0.5f), 0.5f) * 0.25f;

    // Strand thickness variation
    float thickness_var = value_noise(float3(radial_angle * 10.0f, along * 2.0f, 0)) * 0.1f;

    // Hair shine (anisotropic highlight along strand direction)
    float shine = pow(max(0.0f, dot(normal, primary_axis)), 4.0f) * 0.15f;

    // Natural color variation
    float color_var = fbm(local_pos * 5.0f, 2) * 0.12f;

    return strands + thickness_var + color_var - shine;
}

// ============================================================================
// SECTION 8: PATTERN DISPATCHER
// ============================================================================
// Single entry point - routes to appropriate pattern function

inline float compute_pattern(float3 world_pos, float3 normal, float3 center, float3 primary_axis, uint8_t pattern_id) {
    switch (pattern_id) {
        case PATTERN_WOOD:        return pattern_wood(world_pos, normal, center, primary_axis);
        case PATTERN_STONE:       return pattern_stone(world_pos, normal, center);
        case PATTERN_LEATHER:     return pattern_leather(world_pos, normal, center);
        case PATTERN_LINEN:       return pattern_linen(world_pos, normal, center);
        case PATTERN_COTTON:      return pattern_cotton(world_pos, normal, center);
        case PATTERN_SILK:        return pattern_silk(world_pos, normal, center);
        case PATTERN_EMBROIDERY:  return pattern_embroidery(world_pos, normal, center);
        case PATTERN_SKIN:        return pattern_skin(world_pos, normal, center);
        case PATTERN_SKIN_REPTILE:return pattern_skin_reptile(world_pos, normal, center);
        case PATTERN_HAIR:        return pattern_hair(world_pos, normal, center, primary_axis);
        default:                  return 0.0f;
    }
}

#endif // PROCEDURAL_PATTERNS_METAL
