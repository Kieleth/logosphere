#include "logosphere/rendering/pixel_shader.h"
#include "../core/light_system.h"
#include "../particle.h"
#include <algorithm>

/**
 * LightingShader Implementation
 * Full lighting calculation with shadows - this is the main production shader
 */
LightSystem::SurfaceColor LightingShader::shade(
    float particle_r, float particle_g, float particle_b, float particle_a,
    bool is_light_source, float emission_strength,
    int particle_index,
    int surface_index,
    float u, float v,
    LightSystem& light_system) {

    (void)particle_a;  // Unused in this shader

    // Check if this is a light source
    if (is_light_source && emission_strength > 0.0f) {
        // Light sources are always bright white
        return LightSystem::SurfaceColor{255, 255, 255, 1.0f, false};
    }

    // Get the pre-calculated surface color from the light system
    // This includes shadow calculations and light accumulation
    return light_system.get_surface_color_at(
        particle_index, surface_index, u, v,
        static_cast<uint8_t>(particle_r * 255),
        static_cast<uint8_t>(particle_g * 255),
        static_cast<uint8_t>(particle_b * 255)
    );
}

/**
 * FlatColorShader Implementation
 * Simple flat color without any lighting calculations
 */
LightSystem::SurfaceColor FlatColorShader::shade(
    float particle_r, float particle_g, float particle_b, float particle_a,
    bool is_light_source, float emission_strength,
    int particle_index,
    int surface_index,
    float u, float v,
    LightSystem& light_system) {

    // Unused parameters - avoid compiler warnings
    (void)particle_a;
    (void)is_light_source;
    (void)emission_strength;
    (void)particle_index;
    (void)surface_index;
    (void)u;
    (void)v;
    (void)light_system;

    if (use_particle_color_) {
        // Use the particle's base color
        return LightSystem::SurfaceColor{
            static_cast<uint8_t>(particle_r * 255),
            static_cast<uint8_t>(particle_g * 255),
            static_cast<uint8_t>(particle_b * 255),
            0.0f,  // intensity
            false  // in_shadow
        };
    } else {
        // Use the fixed color set in constructor
        return color_;
    }
}

/**
 * EmissiveShader Implementation
 * For light-emitting particles - shows them as bright
 */
LightSystem::SurfaceColor EmissiveShader::shade(
    float particle_r, float particle_g, float particle_b, float particle_a,
    bool is_light_source, float emission_strength,
    int particle_index,
    int surface_index,
    float u, float v,
    LightSystem& light_system) {

    // Unused parameters
    (void)particle_a;
    (void)particle_index;
    (void)surface_index;
    (void)u;
    (void)v;
    (void)light_system;

    if (is_light_source && emission_strength > 0.0f) {
        // Light sources are bright white
        return LightSystem::SurfaceColor{255, 255, 255, 1.0f, false};
    } else {
        // Non-light particles use their base color
        return LightSystem::SurfaceColor{
            static_cast<uint8_t>(particle_r * 255),
            static_cast<uint8_t>(particle_g * 255),
            static_cast<uint8_t>(particle_b * 255),
            0.0f,  // intensity
            false  // in_shadow
        };
    }
}

/**
 * DebugUVShader Implementation
 * Visualizes UV coordinates as colors for debugging texture mapping
 */
LightSystem::SurfaceColor DebugUVShader::shade(
    float particle_r, float particle_g, float particle_b, float particle_a,
    bool is_light_source, float emission_strength,
    int particle_index,
    int surface_index,
    float u, float v,
    LightSystem& light_system) {

    // Unused parameters
    (void)particle_r;
    (void)particle_g;
    (void)particle_b;
    (void)particle_a;
    (void)is_light_source;
    (void)emission_strength;
    (void)particle_index;
    (void)surface_index;
    (void)light_system;

    // Map U to red channel, V to green channel
    // Blue channel shows the W coordinate (1 - u - v)
    float w = 1.0f - u - v;

    return LightSystem::SurfaceColor{
        static_cast<uint8_t>(u * 255),
        static_cast<uint8_t>(v * 255),
        static_cast<uint8_t>(w * 255),
        0.0f,  // intensity
        false  // in_shadow
    };
}