#include "logosphere/worldgen/organic_spec.h"
#include <algorithm>
#include <cmath>

// Oak tree preset
OrganicSpec OrganicSpec::oak() {
    OrganicSpec spec;
    spec.type = OrganicType::TREE;
    spec.species = "oak";

    // Size
    spec.height = 12.0f;
    spec.width = 80.0f;  // Very wide canopy
    spec.base_thickness = 0.5f;

    // Space Colonization
    spec.attractor_shape = AttractorShape::SPHERE;
    spec.attractor_count = 120;
    spec.attraction_range = 5.0f;
    spec.kill_distance = 2.5f;
    spec.segment_length = 0.5f;
    spec.max_iterations = 80;

    // Growth direction
    spec.upward_bias = 0.3f;  // Slight upward bias
    spec.outward_bias = 0.2f;  // Spread outward

    // Branching
    spec.thickness_taper = 0.7f;
    spec.branch_probability = 0.3f;

    // Trunk
    spec.trunk_ratio = 0.45f;
    spec.trunk_taper = 0.4f;
    spec.trunk_wobble = 0.02f;

    // Appearance
    spec.stem_r = 0.4f; spec.stem_g = 0.25f; spec.stem_b = 0.15f;  // Brown bark
    spec.foliage_r = 0.2f; spec.foliage_g = 0.6f; spec.foliage_b = 0.2f;  // Green leaves

    // Foliage
    spec.foliage_thickness_threshold = 0.4f;
    spec.foliage_count_min = 2;
    spec.foliage_count_max = 2;
    spec.foliage_offset_radius = 0.6f;
    spec.foliage_offset_variance = 0.3f;
    spec.foliage_base_width = 0.35f;
    spec.foliage_base_height = 0.25f;
    spec.foliage_tilt_probability = 0.2f;
    spec.foliage_tilt_angle_min = 10.0f;
    spec.foliage_tilt_angle_max = 30.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 15.0f;
    spec.years_to_maturity = 20.0f;

    return spec;
}

// Pine tree preset
OrganicSpec OrganicSpec::pine() {
    OrganicSpec spec;
    spec.type = OrganicType::TREE;
    spec.species = "pine";

    // Size
    spec.height = 15.0f;
    spec.width = 6.0f;  // Narrow conical
    spec.base_thickness = 0.8f;

    // Space Colonization
    spec.attractor_shape = AttractorShape::CONE;
    spec.attractor_count = 100;
    spec.attraction_range = 4.0f;
    spec.kill_distance = 2.0f;
    spec.segment_length = 0.4f;
    spec.max_iterations = 100;

    // Growth direction
    spec.upward_bias = 0.7f;  // Strong upward growth
    spec.outward_bias = 0.1f;  // Minimal spreading

    // Branching
    spec.thickness_taper = 0.75f;
    spec.branch_probability = 0.4f;

    // Trunk
    spec.trunk_ratio = 0.6f;  // Taller trunk
    spec.trunk_taper = 0.3f;
    spec.trunk_wobble = 0.01f;

    // Appearance
    spec.stem_r = 0.35f; spec.stem_g = 0.2f; spec.stem_b = 0.1f;  // Dark bark
    spec.foliage_r = 0.1f; spec.foliage_g = 0.4f; spec.foliage_b = 0.15f;  // Dark green needles

    // Foliage (needles)
    spec.foliage_thickness_threshold = 0.5f;
    spec.foliage_count_min = 3;
    spec.foliage_count_max = 4;
    spec.foliage_offset_radius = 0.3f;
    spec.foliage_offset_variance = 0.15f;
    spec.foliage_base_width = 0.15f;  // Small needles
    spec.foliage_base_height = 0.1f;
    spec.foliage_tilt_probability = 0.5f;  // Many needles droop
    spec.foliage_tilt_angle_min = 20.0f;
    spec.foliage_tilt_angle_max = 45.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 25.0f;  // Fast growing
    spec.years_to_maturity = 8.0f;

    return spec;
}

// Willow tree preset
OrganicSpec OrganicSpec::willow() {
    OrganicSpec spec;
    spec.type = OrganicType::TREE;
    spec.species = "willow";

    // Size
    spec.height = 10.0f;
    spec.width = 24.0f;  // Wide drooping canopy
    spec.base_thickness = 1.2f;

    // Space Colonization
    spec.attractor_shape = AttractorShape::HEMISPHERE;
    spec.attractor_count = 150;
    spec.attraction_range = 6.0f;
    spec.kill_distance = 3.0f;
    spec.segment_length = 0.6f;
    spec.max_iterations = 100;

    // Growth direction
    spec.upward_bias = 0.1f;  // Minimal upward (drooping)
    spec.outward_bias = 0.4f;  // Strong outward spreading

    // Branching
    spec.thickness_taper = 0.8f;
    spec.branch_probability = 0.5f;  // Dense branching

    // Trunk
    spec.trunk_ratio = 0.3f;  // Short trunk
    spec.trunk_taper = 0.35f;
    spec.trunk_wobble = 0.03f;

    // Appearance
    spec.stem_r = 0.5f; spec.stem_g = 0.35f; spec.stem_b = 0.2f;  // Light bark
    spec.foliage_r = 0.3f; spec.foliage_g = 0.7f; spec.foliage_b = 0.3f;  // Bright green

    // Foliage (drooping leaves)
    spec.foliage_thickness_threshold = 0.35f;
    spec.foliage_count_min = 2;
    spec.foliage_count_max = 3;
    spec.foliage_offset_radius = 0.8f;
    spec.foliage_offset_variance = 0.4f;
    spec.foliage_base_width = 0.4f;  // Long drooping leaves
    spec.foliage_base_height = 0.15f;
    spec.foliage_tilt_probability = 0.7f;  // Most leaves droop
    spec.foliage_tilt_angle_min = 30.0f;
    spec.foliage_tilt_angle_max = 60.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 20.0f;
    spec.years_to_maturity = 15.0f;

    return spec;
}

// Grass blade preset
OrganicSpec OrganicSpec::grass_blade() {
    OrganicSpec spec;
    spec.type = OrganicType::GRASS;
    spec.species = "grass";

    // Size
    spec.height = 0.4f;  // 40cm tall grass
    spec.width = 0.05f;  // Very narrow (5cm cylinder)
    spec.base_thickness = 0.01f;  // Thin blade (1cm)

    // Space Colonization
    spec.attractor_shape = AttractorShape::CYLINDER;
    spec.attractor_count = 40;  // More attractors for taller grass
    spec.attraction_range = 3.0f;  // Larger range for bigger segments
    spec.kill_distance = 0.8f;     // Larger kill distance for growth
    spec.segment_length = 0.1f;    // Base segment size (overridden by grass_patch_spec)
    spec.max_iterations = 20;      // More iterations for taller blades

    // Growth direction
    spec.upward_bias = 0.9f;  // Very strong upward growth
    spec.outward_bias = 0.0f;  // No spreading

    // Branching
    spec.thickness_taper = 0.9f;  // Minimal taper
    spec.branch_probability = 0.0f;  // No branching (single blade)

    // Trunk (base of blade)
    spec.trunk_ratio = 0.1f;  // Very short base
    spec.trunk_taper = 0.2f;
    spec.trunk_wobble = 0.005f;  // Slight sway

    // Stem cross-section: FLAT BLADE (wide and thin)
    spec.stem_width_multiplier = 8.0f;   // 8x wider
    spec.stem_height_multiplier = 0.1f;  // 10x thinner

    // Appearance
    spec.stem_r = 0.2f; spec.stem_g = 0.5f; spec.stem_b = 0.1f;  // Green stem
    spec.foliage_r = 0.3f; spec.foliage_g = 0.7f; spec.foliage_b = 0.2f;  // Bright green blade

    // Foliage (blade segments)
    spec.foliage_thickness_threshold = 1.0f;  // All segments get foliage
    spec.foliage_count_min = 1;
    spec.foliage_count_max = 1;
    spec.foliage_offset_radius = 0.02f;
    spec.foliage_offset_variance = 0.01f;
    spec.foliage_base_width = 0.015f;  // Thin blade width
    spec.foliage_base_height = 0.05f;  // Blade segment height
    spec.foliage_maturity_size_min = 1.0f;  // Grass blades always full size (no maturity scaling)
    // BLADES STAND UP, visually. The owner's eye caught the pagoda look in
    // the divergence microscope: near-flat tilt left every blade sheet lying
    // HORIZONTAL, stacked cards instead of blades. 75-88 degrees = upright
    // with a natural droop. (A first attempt at this fix was inserted twelve
    // lines up and silently clobbered by these very lines; the probe that
    // caught it printed rot_x=0-13 degrees on live blades. One authoritative
    // block, at the end, where the last write wins.)
    //
    // NOTE WHAT THIS DOES NOT FIX: the physics narrow phase is pure AABB and
    // reads no rotation at all (narrow_phase.cpp), so the solver still
    // collides every blade as a horizontal 1 mm slab whatever rotation_x
    // says. Verified: the walk-through-grass gate's numbers are IDENTICAL to
    // the digit with and without this tilt. Render fixed here; the
    // contact-shape mismatch is recorded on issue #47 as a root constraint.
    // Soft plant tissue: with the blade's ~1.2e-5 m2 tear section this
    // gives ~60 N joints. A footstep's transient load (~3 N measured on the
    // single-blade study) survives; a stomp or a grab tears.
    spec.material_strength = 5e6f;
    spec.foliage_tilt_probability = 1.0f;
    spec.foliage_tilt_angle_min = 75.0f;
    spec.foliage_tilt_angle_max = 88.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 100.0f;  // Very fast
    spec.years_to_maturity = 0.1f;  // ~1 month

    return spec;
}

// Bush preset
OrganicSpec OrganicSpec::bush() {
    OrganicSpec spec;
    spec.type = OrganicType::BUSH;
    spec.species = "bush";

    // Size
    spec.height = 2.0f;  // Low to ground
    spec.width = 4.0f;  // Wide spreading
    spec.base_thickness = 0.3f;

    // Space Colonization
    spec.attractor_shape = AttractorShape::HEMISPHERE;
    spec.attractor_count = 80;
    spec.attraction_range = 2.0f;
    spec.kill_distance = 1.0f;
    spec.segment_length = 0.3f;
    spec.max_iterations = 60;

    // Growth direction
    spec.upward_bias = 0.2f;  // Slight upward
    spec.outward_bias = 0.5f;  // Strong outward spreading

    // Branching
    spec.thickness_taper = 0.65f;
    spec.branch_probability = 0.6f;  // Very dense branching

    // Trunk
    spec.trunk_ratio = 0.15f;  // Very short trunk
    spec.trunk_taper = 0.5f;
    spec.trunk_wobble = 0.02f;

    // Appearance
    spec.stem_r = 0.3f; spec.stem_g = 0.2f; spec.stem_b = 0.1f;  // Brown stems
    spec.foliage_r = 0.2f; spec.foliage_g = 0.5f; spec.foliage_b = 0.2f;  // Green leaves

    // Foliage (dense leaves)
    spec.foliage_thickness_threshold = 0.5f;
    spec.foliage_count_min = 3;
    spec.foliage_count_max = 4;
    spec.foliage_offset_radius = 0.4f;
    spec.foliage_offset_variance = 0.2f;
    spec.foliage_base_width = 0.2f;
    spec.foliage_base_height = 0.15f;
    spec.foliage_tilt_probability = 0.3f;
    spec.foliage_tilt_angle_min = 10.0f;
    spec.foliage_tilt_angle_max = 25.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 30.0f;
    spec.years_to_maturity = 3.0f;

    return spec;
}

// Vine preset
OrganicSpec OrganicSpec::vine() {
    OrganicSpec spec;
    spec.type = OrganicType::VINE;
    spec.species = "vine";

    // Size
    spec.height = 3.0f;  // Hanging length
    spec.width = 2.0f;  // Spread at base
    spec.base_thickness = 0.05f;  // Thin vine

    // Space Colonization
    spec.attractor_shape = AttractorShape::INVERTED_CONE;
    spec.attractor_count = 50;
    spec.attraction_range = 1.5f;
    spec.kill_distance = 0.8f;
    spec.segment_length = 0.2f;
    spec.max_iterations = 40;

    // Growth direction
    spec.upward_bias = -0.8f;  // Strong downward growth (negative = downward)
    spec.outward_bias = 0.1f;  // Minimal spreading

    // Branching
    spec.thickness_taper = 0.85f;
    spec.branch_probability = 0.2f;  // Sparse branching

    // Trunk (hanging base)
    spec.trunk_ratio = 0.2f;
    spec.trunk_taper = 0.2f;
    spec.trunk_wobble = 0.01f;

    // Appearance
    spec.stem_r = 0.25f; spec.stem_g = 0.4f; spec.stem_b = 0.15f;  // Green-brown
    spec.foliage_r = 0.3f; spec.foliage_g = 0.6f; spec.foliage_b = 0.25f;  // Green leaves

    // Foliage (hanging leaves)
    spec.foliage_thickness_threshold = 0.6f;
    spec.foliage_count_min = 1;
    spec.foliage_count_max = 2;
    spec.foliage_offset_radius = 0.3f;
    spec.foliage_offset_variance = 0.15f;
    spec.foliage_base_width = 0.25f;
    spec.foliage_base_height = 0.2f;
    spec.foliage_tilt_probability = 0.8f;  // Most leaves droop
    spec.foliage_tilt_angle_min = 20.0f;
    spec.foliage_tilt_angle_max = 50.0f;

    // Growth
    spec.growth_rate_iterations_per_year = 40.0f;
    spec.years_to_maturity = 2.0f;

    return spec;
}

void OrganicSpec::apply_natural_variation(float variance_amount, unsigned int seed) {
    // Simple LCG for deterministic randomness
    unsigned int rng_state = seed;
    auto rng_next = [&rng_state]() -> float {
        rng_state = (1103515245 * rng_state + 12345) % 2147483648;
        return (float)rng_state / 2147483648.0f;  // [0, 1]
    };

    auto vary = [&rng_next, variance_amount](float base, float max_variance) -> float {
        float random = rng_next();  // [0, 1]
        float normalized = (random * 2.0f - 1.0f);  // [-1, 1]
        float curved = normalized * normalized * normalized;  // Bell curve
        return base + base * max_variance * variance_amount * curved;
    };

    // Vary size parameters
    height = vary(height, 0.40f);
    width = vary(width, 0.50f);
    base_thickness = vary(base_thickness, 0.40f);

    // Vary attractor count (integer)
    float count_vary = rng_next() - 0.5f;  // [-0.5, 0.5]
    attractor_count = std::max(10, std::min(300,
        attractor_count + static_cast<int>(count_vary * 50.0f * variance_amount)));

    // Keep values in reasonable ranges
    height = std::max(0.1f, std::min(30.0f, height));
    width = std::max(0.05f, std::min(120.0f, width));
    base_thickness = std::max(0.01f, std::min(2.0f, base_thickness));
}
