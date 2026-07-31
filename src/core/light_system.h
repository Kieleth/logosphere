#ifndef LIGHT_SYSTEM_H
#define LIGHT_SYSTEM_H

#include "particle.h"
#include <vector>

// Forward declarations
class ILightingStrategy;
class Engine;
class ParticleSystem;

// Light System - NOW USING SimplePixelToLight
// 
// Old ray tracing approach: REMOVED (only achieved 80% coverage)
// New approach: Simple pixel-to-light shadows (100% accurate)
//
// For each surface, we check if it can see any light source
// If yes = lit, if no = shadow. That's it!

class LightSystem {
public:
    LightSystem();
    ~LightSystem();
    
    // Initialize with engine reference
    void initialize();
    void set_engine(Engine* engine) { engine_ = engine; }
    
    // Main update - calculates which surfaces are lit/shadowed
    void update_lighting(ParticleSystem& particle_system);
    
    // Get the color at a point on a surface
    struct SurfaceColor {
        uint8_t r, g, b;
        float intensity;
        bool in_shadow;
    };
    
    SurfaceColor get_surface_color_at(
        int particle_index, int surface_index,
        float u, float v,
        uint8_t base_r, uint8_t base_g, uint8_t base_b
    ) const;
    
    // We only use PixelLightingStrategy now - no mode switching needed
    
private:
    ILightingStrategy* lighting_strategy_;  // Always PixelLightingStrategy
    Engine* engine_;                        // Reference to engine for camera access
};

#endif // LIGHT_SYSTEM_H