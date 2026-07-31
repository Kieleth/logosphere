#ifndef ORGANIC_SPEC_H
#define ORGANIC_SPEC_H

#include "logosphere/worldgen/growth_state.h"
#include <string>

// Attractor shape types for Space Colonization
// Different shapes produce different organic forms
enum class AttractorShape {
    SPHERE,           // Tree canopy (radial spreading)
    CYLINDER,         // Grass blade (vertical growth)
    CONE,             // Pine tree (conical canopy)
    INVERTED_CONE,    // Vine (hanging downward)
    HEMISPHERE,       // Bush (ground-level spreading)
    TORUS            // Wreath/ring shape
};

// OrganicSpec - Unified parameters for all organic entity types
//
// Design: Single specification that works for trees, grass, bushes, vines, etc.
// Different organic types achieved by varying attractor shape and growth parameters
//
// Examples:
// - Tree: Sphere attractors, upward bias, moderate branching
// - Grass: Cylinder attractors, strong upward bias, minimal branching
// - Bush: Hemisphere attractors, low height, dense branching
// - Vine: Inverted cone attractors, downward bias, thin branches
struct OrganicSpec {
    // === Identity ===
    OrganicType type = OrganicType::TREE;
    std::string species = "generic";

    // === Size Parameters ===
    float height = 10.0f;              // Total entity height (m)
    float width = 5.0f;                // Horizontal spread (m)
    float base_thickness = 1.0f;       // Starting branch/stem thickness (m)

    // === Space Colonization Parameters ===
    AttractorShape attractor_shape = AttractorShape::SPHERE;
    int attractor_count = 120;         // Number of target points
    float attraction_range = 5.0f;     // How far branches sense attractors (m)
    float kill_distance = 2.5f;        // Distance at which attractors are consumed (m)
    float segment_length = 0.5f;       // Length of each growth step (m)
    int max_iterations = 80;           // Maximum growth steps

    // === Growth Direction Bias ===
    float upward_bias = 0.5f;          // 0.0=no bias, 1.0=strongly upward (gravity counteraction)
    float outward_bias = 0.0f;         // 0.0=no bias, 1.0=strongly radial

    // === Branching Parameters ===
    float thickness_taper = 0.7f;      // Child thickness = parent * taper (0.6-0.9)
    float branch_probability = 0.3f;   // Probability of side branches (0.0-1.0)

    // === Trunk/Stem Parameters ===
    float trunk_ratio = 0.45f;         // Trunk height as ratio of total height (0.3-0.6)
    float trunk_taper = 0.4f;          // Trunk diameter reduction from base to top (0.0-0.5)
    float trunk_wobble = 0.02f;        // Organic variation in trunk direction (m)

    // === Stem Cross-Section Shape ===
    float stem_width_multiplier = 1.0f;   // Width relative to thickness (1.0=round, >1.0=flat/wide)
    float stem_height_multiplier = 1.0f;  // Height relative to thickness (1.0=round, <1.0=flat/thin)

    // === Appearance ===
    float stem_r = 0.4f, stem_g = 0.25f, stem_b = 0.15f;  // Brown/green stem color
    float foliage_r = 0.2f, foliage_g = 0.6f, foliage_b = 0.2f;  // Green foliage color

    // === Foliage Parameters ===
    float foliage_thickness_threshold = 0.4f;  // Branches thinner than this get foliage
    int foliage_count_min = 2;                 // Min foliage elements per branch endpoint
    int foliage_count_max = 2;                 // Max foliage elements per branch endpoint
    float foliage_offset_radius = 0.6f;        // Center of foliage spread (m)
    float foliage_offset_variance = 0.3f;      // Spread variance (m)
    float foliage_base_width = 0.35f;          // Base foliage width (m)
    float foliage_base_height = 0.25f;         // Base foliage height (m)
    float foliage_maturity_size_min = 0.5f;    // Min size factor for young foliage (0.5=half size, 1.0=full size)
    float foliage_tilt_probability = 0.2f;     // Probability of downward tilt (0.0-1.0)
    float foliage_tilt_angle_min = 10.0f;      // Min tilt angle (degrees)
    float foliage_tilt_angle_max = 30.0f;      // Max tilt angle (degrees)

    // === Stochastic Variation ===
    float size_variance = 0.15f;       // Natural size variation per entity (0.0-0.5)
    unsigned int random_seed = 0;      // Seed for reproducibility

    // === Temporal Growth (for Phase 2) ===
    float growth_rate_iterations_per_year = 15.0f;  // Growth speed
    float years_to_maturity = 20.0f;                 // Time to reach full size

    // === Species Presets ===
    static OrganicSpec oak();
    static OrganicSpec pine();
    static OrganicSpec willow();
    static OrganicSpec grass_blade();
    static OrganicSpec bush();
    static OrganicSpec vine();

    // Apply natural variation (modifies in place)
    void apply_natural_variation(float variance_amount = 0.3f, unsigned int seed = 0);
};

// Helper: Convert AttractorShape to string
inline std::string attractor_shape_to_string(AttractorShape shape) {
    switch (shape) {
        case AttractorShape::SPHERE: return "sphere";
        case AttractorShape::CYLINDER: return "cylinder";
        case AttractorShape::CONE: return "cone";
        case AttractorShape::INVERTED_CONE: return "inverted_cone";
        case AttractorShape::HEMISPHERE: return "hemisphere";
        case AttractorShape::TORUS: return "torus";
        default: return "unknown";
    }
}

#endif // ORGANIC_SPEC_H
