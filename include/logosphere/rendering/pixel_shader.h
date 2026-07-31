#ifndef PIXEL_SHADER_H
#define PIXEL_SHADER_H

#include "core/light_system.h"
#include "particle.h"

/**
 * PixelShader - Interface for modular per-pixel shading operations
 * 
 * Following KISS & UNIX philosophy: This interface defines ONE thing - 
 * how to calculate the color of a pixel on a surface.
 * 
 * Part of the rendering system refactoring to break down the monolithic RenderSystem.
 * Allows swappable shading algorithms without changing the rasterization logic.
 * 
 * Historical note: The concept of programmable shaders was introduced with 
 * NVIDIA's GeForce 3 (2001) and ATI's Radeon 8500. Before that, all shading
 * was fixed-function. This software implementation follows the same principle
 * of separating rasterization from shading.
 * 
 * Design pattern: Strategy Pattern - different shading algorithms can be
 * swapped at runtime without changing the rasterizer.
 */

// Forward declarations
class LightSystem;
struct Particle;

/**
 * Base interface for all pixel shaders
 * Pure virtual - must be implemented by concrete shaders
 */
class IPixelShader {
public:
    virtual ~IPixelShader() = default;
    
    /**
     * Calculate the color for a pixel on a surface
     * 
     * @param particle The particle being rendered
     * @param particle_index Index of the particle in the system
     * @param surface_index Index of the surface on the particle
     * @param u Barycentric coordinate U (0-1)
     * @param v Barycentric coordinate V (0-1)
     * @param light_system Light system for lighting calculations
     * @return RGB color for the pixel
     */
    virtual LightSystem::SurfaceColor shade(
        float particle_r, float particle_g, float particle_b, float particle_a,
        bool is_light_source, float emission_strength,
        int particle_index,
        int surface_index,
        float u, float v,
        LightSystem& light_system
    ) = 0;
    
    /**
     * Get the name of this shader for debugging
     */
    virtual const char* get_name() const = 0;
};

/**
 * LightingShader - Full lighting calculation with shadows
 * This is the main shader used for normal rendering
 */
class LightingShader : public IPixelShader {
public:
    LightingShader() = default;
    
    LightSystem::SurfaceColor shade(
        float particle_r, float particle_g, float particle_b, float particle_a,
        bool is_light_source, float emission_strength,
        int particle_index,
        int surface_index,
        float u, float v,
        LightSystem& light_system
    ) override;
    
    const char* get_name() const override { return "LightingShader"; }
};

/**
 * FlatColorShader - Simple flat color without lighting
 * Useful for debug visualization or unlit objects
 */
class FlatColorShader : public IPixelShader {
public:
    // Constructor with RGB color
    FlatColorShader(uint8_t r, uint8_t g, uint8_t b)
        : color_{r, g, b, 0.0f, false} {}
    
    // Constructor with particle's base color
    FlatColorShader() : use_particle_color_(true) {}
    
    LightSystem::SurfaceColor shade(
        float particle_r, float particle_g, float particle_b, float particle_a,
        bool is_light_source, float emission_strength,
        int particle_index,
        int surface_index,
        float u, float v,
        LightSystem& light_system
    ) override;
    
    const char* get_name() const override { return "FlatColorShader"; }
    
private:
    LightSystem::SurfaceColor color_;
    bool use_particle_color_ = false;
};

/**
 * EmissiveShader - For light-emitting particles
 * Shows particles that are light sources as bright white
 */
class EmissiveShader : public IPixelShader {
public:
    EmissiveShader() = default;
    
    LightSystem::SurfaceColor shade(
        float particle_r, float particle_g, float particle_b, float particle_a,
        bool is_light_source, float emission_strength,
        int particle_index,
        int surface_index,
        float u, float v,
        LightSystem& light_system
    ) override;
    
    const char* get_name() const override { return "EmissiveShader"; }
};

/**
 * DebugUVShader - Visualizes UV coordinates for debugging
 * U maps to red, V maps to green
 */
class DebugUVShader : public IPixelShader {
public:
    DebugUVShader() = default;
    
    LightSystem::SurfaceColor shade(
        float particle_r, float particle_g, float particle_b, float particle_a,
        bool is_light_source, float emission_strength,
        int particle_index,
        int surface_index,
        float u, float v,
        LightSystem& light_system
    ) override;
    
    const char* get_name() const override { return "DebugUVShader"; }
};

#endif // PIXEL_SHADER_H