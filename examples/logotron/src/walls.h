// Wall particle styling — set_wall_geometry + set_wall_color.
//
// Extracted from LogotronApplication so headless ATs can verify
// wall properties (is_light_source, emission, color) without
// pulling the entire game class (and GLFW + the engine) into the
// test TU. The live game still calls these via the same names; the
// class methods now forward to these free functions.

#pragma once

#include "particle.h"

#include <algorithm>
#include <cmath>

namespace logotron {

// Per-wall pillar dims (must match the constants in logotron_app.h's
// anonymous namespace). Sub-meter so a 50 m arena holding 30+ walls
// reads cleanly.
inline constexpr float kWallTrailZ        = 0.5f;
inline constexpr float kWallPillarThin    = 0.10f;
inline constexpr float kWallPillarTall    = 0.40f;

// Render properties:
//   - is_self_emissive = true  → deferred shader treats the wall's
//     pixels as emissive (color * emission_strength). The flag was
//     added to Particle precisely so trail walls can glow without
//     entering the scene-lights array — see
//     the self-emissive field design notes.
//   - is_light_source  = false → wall does NOT join the scene-
//     lights array, doesn't contribute to other pixels' lighting,
//     doesn't trigger PARTICLE_SWAP cascades on delete. The 2026-
//     04-27 GPU command-queue crash with is_light_source=true on
//     20+ walls is what motivated the new flag.
//   - emission_strength → punchy enough to read against the dark
//     Tron floor without overwhelming the boundary's neon wash.
//   - emission_radius   → unused in the self-emissive draw path
//     (only matters for light contribution, which we don't have);
//     left at 0 for clarity.
// Enemy Program palette (Tron enemy family), indexed by rider hue.
inline constexpr float kEnemyPalette[4][3] = {
    {1.00f, 0.45f, 0.05f},   // orange (the classic)
    {1.00f, 0.20f, 0.15f},   // red
    {1.00f, 0.75f, 0.10f},   // amber
    {0.70f, 0.30f, 1.00f},   // violet
};

inline void set_wall_color(Particle& p, bool is_player, bool is_director = false,
                           int hue = 0) {
    if (is_director)    { p.r = 0.85f; p.g = 0.30f; p.b = 1.00f; }  // weirden magenta
    else if (is_player) { p.r = 0.15f; p.g = 0.95f; p.b = 1.00f; }
    else if (hue < 0) { p.r = 0.55f; p.g = 1.00f; p.b = 0.85f; }  // ally teal
    else {
        const auto& c = kEnemyPalette[hue % 4];
        p.r = c[0]; p.g = c[1]; p.b = c[2];
    }
    p.a = 1.0f;
    p.is_light_source   = false;
    p.is_self_emissive  = true;
    p.emission_strength = is_director ? 3.0f : 2.5f;
    p.emission_radius   = 0.0f;
}

// Active-run head — the live segment between the cycle's last
// turn and its current head. sync_active_runs deletes + re-adds
// this particle EVERY FRAME so it grows with the bike.
//
// is_self_emissive=true is safe here: the emissive map is rebuilt
// from scratch every frame anyway (render_pipeline.cpp:730), and
// is_self_emissive does NOT add the particle to the scene-lights
// array (the array that crashed under per-frame churn with
// is_light_source=true on 2026-04-27). Net cost is one extra bit
// flip per frame in the map build — negligible.
inline void style_active_run_head(Particle& p, bool is_player, int hue = 0) {
    set_wall_color(p, is_player, /*is_director=*/false, hue);
    // set_wall_color already sets is_self_emissive=true and
    // is_light_source=false, both of which we want for the head.
    p.is_dynamic = true;  // moving in vision-memory terms
}

inline void set_wall_geometry(Particle& p, float sx, float sy,
                              float ex, float ey) {
    float mid_x = (sx + ex) * 0.5f;
    float mid_y = (sy + ey) * 0.5f;
    float span_x = std::fabs(ex - sx);
    float span_y = std::fabs(ey - sy);
    bool horizontal = span_x >= span_y;
    p.shape = ParticleShape::BOX;
    p.x = mid_x;
    p.y = mid_y;
    p.z = kWallTrailZ;
    p.width  = horizontal ? std::max(span_x, 0.1f) : kWallPillarThin;
    p.height = horizontal ? kWallPillarThin       : std::max(span_y, 0.1f);
    p.thickness = kWallPillarTall;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
}

}  // namespace logotron
