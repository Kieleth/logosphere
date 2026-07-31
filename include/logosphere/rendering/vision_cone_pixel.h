// CPU implementation of the per-pixel vision-cone visibility
// algorithm — the EXACT semantic the Pass-4 Metal kernel
// (`vision_cone_postprocess.metal`) implements per pixel.
//
// **Why this exists, not just the shader**: the Metal kernel is
// hard to test headless (needs Metal device, GPU framebuffer,
// dispatch). This function takes the same inputs and produces the
// same single-pixel output, so headless tests + Logotron ATs can
// drive the contract. When the shader and this function disagree,
// the SHADER is wrong (CPU function is the spec).
//
// All inputs are plain data — no Metal types, no engine state. The
// function is pure: same inputs → same output. Trivially unit-
// testable from any toolchain.

#pragma once

namespace logosphere::rendering {

// Mirror of VisionConeParams (gpu_types.metal). Same field order,
// same semantics. Only carries the fields the per-pixel logic
// actually reads.
struct VisionConePixelParams {
    float viewer_x;
    float viewer_y;
    float look_direction;   // radians, 0 = +Y (engine convention)
    float half_fov;
    float range;
    float inner_falloff;    // 0..1 of half_fov
    float darkness;         // visibility multiplier outside cone (0 = pitch black)

    int   occlusion_count;          // 0 = no LOS occlusion check
    const float* occlusion_distance; // length = occlusion_count

    int   memory_enabled;           // 0 / 1
    int   memory_width;             // grid columns
    int   memory_height;            // grid rows
    float memory_origin_x;          // world coord of cell (0,0)'s SW corner
    float memory_origin_y;
    float memory_cell_size;         // meters per cell side
    float memory_dim;               // brightness cap for memory blend (0..1)
    const float* memory_grid;       // length = memory_width * memory_height

    int   dynamic_map_size;         // 0 = no dynamic-skip check
    const unsigned char* is_dynamic_map;  // length = dynamic_map_size
};

// Sentinel particle-id for sky / no surface (mirrors GBUFFER_SKY_ID
// in gbuffer_types.metal). Pass any pixel.particle_id == this to
// trigger the sky branch (visibility = darkness).
constexpr unsigned int kVisionConeSkyId = 0xFFFFFFFFu;

// Per-pixel inputs that the Metal shader reads from gbuffer +
// thread position. `world_x/world_y` come from gbuf.world_pos.
struct VisionConePixelInput {
    float        world_x;
    float        world_y;
    unsigned int particle_id;   // gbuffer's particle_id (== array index, or kVisionConeSkyId)
};

// Returns the visibility multiplier in [0, 1] that the shader
// applies to the pixel's color. Identical math to
// `apply_vision_cone` in `vision_cone_postprocess.metal`. The
// shader additionally does a foveal blur — that's a separate
// concern, not part of the cone's visibility model.
float compute_vision_cone_visibility(
    const VisionConePixelParams& params,
    const VisionConePixelInput& pixel);

} // namespace logosphere::rendering
