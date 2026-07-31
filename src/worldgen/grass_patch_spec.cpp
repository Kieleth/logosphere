#include "logosphere/worldgen/grass_patch_spec.h"

// Short lawn grass preset (minimal particles)
GrassPatchSpec GrassPatchSpec::short_grass() {
    GrassPatchSpec spec;

    // Patch
    spec.patch_width = 8.0f;   // 8m x 8m patch
    spec.patch_depth = 8.0f;
    spec.blade_count = 20;     // Minimal blades

    // Blade (use grass_blade preset as base)
    spec.blade_spec = OrganicSpec::grass_blade();
    spec.blade_spec.height = 0.15f;           // 15cm short lawn grass
    spec.blade_spec.base_thickness = 0.008f;  // 8mm base
    spec.blade_spec.segment_length = 0.03f;   // 3cm segments
    spec.blade_spec.max_iterations = 5;       // Few iterations
    spec.blade_spec.attractor_count = 10;     // Minimal attractors
    spec.blade_spec.attraction_range = 0.4f;  // 40cm range
    spec.blade_spec.kill_distance = 0.06f;    // 6cm kill distance
    spec.blade_spec.foliage_base_width = 0.03f;    // 3cm wide blades
    spec.blade_spec.foliage_base_height = 0.04f;   // 4cm tall foliage segments

    // Variation (Gaussian)
    spec.height_variance = 0.25f;   // ±25% height
    spec.width_variance = 0.15f;    // ±15% width
    spec.tilt_max = 15.0f;          // 0-15° tilt

    // Distribution
    spec.distribution = DistributionType::UNIFORM;
    spec.min_blade_distance = 0.3f;  // 30cm between blades (more space)

    return spec;
}

// Tall wild grass preset
GrassPatchSpec GrassPatchSpec::tall_grass() {
    GrassPatchSpec spec;

    // Patch - Tall grass
    spec.patch_width = 10.0f;  // 10m x 10m patch
    spec.patch_depth = 10.0f;
    spec.blade_count = 15;     // Sparse tall grass

    // Blade - VERY TALL
    spec.blade_spec = OrganicSpec::grass_blade();
    spec.blade_spec.height = 0.8f;            // 80cm tall wild grass
    spec.blade_spec.base_thickness = 0.012f;  // 12mm base (thick stalks)
    spec.blade_spec.segment_length = 0.08f;   // 8cm segments
    spec.blade_spec.max_iterations = 8;       // Few iterations
    spec.blade_spec.attractor_count = 15;     // Minimal attractors
    spec.blade_spec.attraction_range = 1.5f;  // 1.5m range
    spec.blade_spec.kill_distance = 0.15f;    // 15cm kill distance
    spec.blade_spec.foliage_base_width = 0.12f;    // 12cm wide foliage (dramatic blades)
    spec.blade_spec.foliage_base_height = 0.25f;   // 25cm tall foliage segments (long flowing leaves)

    // Variation (Gaussian)
    spec.height_variance = 0.35f;   // ±35% height (more variation)
    spec.width_variance = 0.2f;     // ±20% width
    spec.tilt_max = 30.0f;          // 0-30° tilt (some droop)

    // Distribution
    spec.distribution = DistributionType::UNIFORM;
    spec.min_blade_distance = 0.4f;  // 40cm between blades

    return spec;
}

// Dense ground cover preset
GrassPatchSpec GrassPatchSpec::dense_grass() {
    GrassPatchSpec spec;

    // Patch - Dense carpet
    spec.patch_width = 6.0f;   // 6m x 6m carpet
    spec.patch_depth = 6.0f;
    spec.blade_count = 30;     // Dense but reasonable

    // Blade - Medium height but thick
    spec.blade_spec = OrganicSpec::grass_blade();
    spec.blade_spec.height = 0.25f;           // 25cm medium dense grass
    spec.blade_spec.base_thickness = 0.010f;  // 10mm base
    spec.blade_spec.segment_length = 0.05f;   // 5cm segments
    spec.blade_spec.max_iterations = 5;       // Few iterations
    spec.blade_spec.attractor_count = 10;     // Minimal attractors
    spec.blade_spec.attraction_range = 0.6f;  // 60cm range
    spec.blade_spec.kill_distance = 0.08f;    // 8cm kill distance
    spec.blade_spec.foliage_base_width = 0.035f;   // 3.5cm wide foliage
    spec.blade_spec.foliage_base_height = 0.05f;   // 5cm tall foliage

    // Variation (Gaussian)
    spec.height_variance = 0.2f;    // ±20% height (uniform appearance)
    spec.width_variance = 0.1f;     // ±10% width
    spec.tilt_max = 10.0f;          // 0-10° tilt (upright)

    // Distribution
    spec.distribution = DistributionType::UNIFORM;
    spec.min_blade_distance = 0.15f;  // 15cm between blades (dense but not overlapping)

    return spec;
}

// Sparse dry grass preset
GrassPatchSpec GrassPatchSpec::sparse_grass() {
    GrassPatchSpec spec;

    // Patch - Sparse field
    spec.patch_width = 12.0f;  // 12m x 12m sparse field
    spec.patch_depth = 12.0f;
    spec.blade_count = 12;     // Very sparse

    // Blade - TALL dry stalks
    spec.blade_spec = OrganicSpec::grass_blade();
    spec.blade_spec.height = 0.6f;            // 60cm tall sparse dry grass
    spec.blade_spec.base_thickness = 0.010f;  // 10mm base
    spec.blade_spec.segment_length = 0.06f;   // 6cm segments
    spec.blade_spec.max_iterations = 6;       // Few iterations
    spec.blade_spec.attractor_count = 12;     // Minimal attractors
    spec.blade_spec.attraction_range = 1.2f;  // 1.2m range
    spec.blade_spec.kill_distance = 0.12f;    // 12cm kill distance
    spec.blade_spec.foliage_base_width = 0.04f;    // 4cm wide foliage
    spec.blade_spec.foliage_base_height = 0.06f;   // 6cm tall foliage

    // Appearance (dry grass colors)
    spec.blade_spec.stem_r = 0.6f; spec.blade_spec.stem_g = 0.55f; spec.blade_spec.stem_b = 0.3f;
    spec.blade_spec.foliage_r = 0.7f; spec.blade_spec.foliage_g = 0.65f; spec.blade_spec.foliage_b = 0.4f;

    // Variation (Gaussian)
    spec.height_variance = 0.4f;    // ±40% height (very irregular)
    spec.width_variance = 0.25f;    // ±25% width
    spec.tilt_max = 40.0f;          // 0-40° tilt (drooping)

    // Distribution
    spec.distribution = DistributionType::UNIFORM;
    spec.min_blade_distance = 0.6f;   // 60cm between blades (sparse)

    return spec;
}
