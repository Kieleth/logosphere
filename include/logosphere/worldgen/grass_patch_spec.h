#ifndef GRASS_PATCH_SPEC_H
#define GRASS_PATCH_SPEC_H

#include "logosphere/worldgen/organic_spec.h"

// Spatial distribution types for grass blades
enum class DistributionType {
    UNIFORM,    // Random uniform (KISS - start with this)
    POISSON,    // Poisson disc sampling (even spacing, future)
    CLUSTERED   // Natural clumping (future)
};

// GrassPatchSpec - Configuration for grass patch generation
//
// Design: Parent entity (grass_patch) with child entities (individual blades)
// Each blade is tracked in KG for temporal growth (1 → 2 → 3 particles over time)
//
// Philosophy:
// - Configurable size, density, and blade characteristics
// - Gaussian distribution for natural variation
// - Modular spatial distribution (start UNIFORM, expand later)
// - Minimal particles (1-3 per blade depending on maturity)
struct GrassPatchSpec {
    // === Patch Dimensions ===
    float patch_width = 1.0f;   // Patch width (m) - X axis
    float patch_depth = 1.0f;   // Patch depth (m) - Y axis
    int blade_count = 25;       // Number of blades in patch

    // === Per-Blade Specification ===
    OrganicSpec blade_spec;     // Base specification for each blade

    // === Variation (Gaussian Distribution) ===
    float height_variance = 0.25f;      // Height variation ±% (gaussian, natural)
    float width_variance = 0.15f;       // Width variation ±% (gaussian)
    float tilt_max = 20.0f;             // Max blade tilt angle (degrees, 0-tilt_max)
    float color_variance = 0.0f;        // Per-blade hue jitter (0 = uniform legacy)

    // === Spatial Distribution ===
    DistributionType distribution = DistributionType::UNIFORM;  // Start with UNIFORM (KISS)
    float min_blade_distance = 0.05f;   // Minimum distance between blades (m) to avoid overlap

    // === Temporal Growth (Future - Phase 2) ===
    bool enable_growth = false;         // Enable temporal growth (blade particles increase over time)
    float growth_rate = 1.0f;           // Growth speed multiplier

    // === Preset Patches ===
    static GrassPatchSpec short_grass();     // Short lawn grass (1-3 particles per blade)
    static GrassPatchSpec tall_grass();      // Tall wild grass (2-5 particles per blade)
    static GrassPatchSpec dense_grass();     // Dense ground cover (many small blades)
    static GrassPatchSpec sparse_grass();    // Sparse dry grass (few tall blades)
};

// Helper: Convert DistributionType to string
inline std::string distribution_type_to_string(DistributionType type) {
    switch (type) {
        case DistributionType::UNIFORM: return "uniform";
        case DistributionType::POISSON: return "poisson";
        case DistributionType::CLUSTERED: return "clustered";
        default: return "unknown";
    }
}

#endif // GRASS_PATCH_SPEC_H
