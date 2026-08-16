#include "logosphere/worldgen/fallen_tree_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <string>

// Species presets
FallenTreeSpec FallenTreeSpec::fallen_trunk()
{
    FallenTreeSpec spec;
    spec.type = FallenType::TRUNK;
    spec.length = 6.0f;
    spec.diameter = 0.8f;
    spec.num_segments = 5;
    spec.stiffness = 95000.0f;
    spec.bark_r = 0.30f;
    spec.bark_g = 0.20f;
    spec.bark_b = 0.10f;
    spec.description = "A massive fallen tree trunk lies here, its bark thick and weathered. "
                       "It provides shelter for countless insects.";
    return spec;
}

FallenTreeSpec FallenTreeSpec::fallen_log()
{
    FallenTreeSpec spec;
    spec.type = FallenType::LOG;
    spec.length = 2.5f;
    spec.diameter = 0.5f;
    spec.num_segments = 3;
    spec.stiffness = 85000.0f;
    spec.bark_r = 0.35f;
    spec.bark_g = 0.22f;
    spec.bark_b = 0.12f;
    spec.description = "A fallen log, moss beginning to creep across its surface.";
    return spec;
}

FallenTreeSpec FallenTreeSpec::fallen_branch()
{
    FallenTreeSpec spec;
    spec.type = FallenType::BRANCH;
    spec.length = 1.5f;
    spec.diameter = 0.25f; // 25cm - above 15cm minimum for visibility
    spec.num_segments = 2;
    spec.stiffness = 70000.0f;
    spec.bark_r = 0.40f;
    spec.bark_g = 0.25f;
    spec.bark_b = 0.15f;
    spec.description = "A fallen branch from one of the trees above.";
    return spec;
}

FallenTreeSpec FallenTreeSpec::twig()
{
    FallenTreeSpec spec;
    spec.type = FallenType::TWIG;
    spec.length = 0.6f;
    spec.diameter = 0.18f; // 18cm - above 15cm minimum for visibility
    spec.num_segments = 1;
    spec.stiffness = 50000.0f;
    spec.bark_r = 0.45f;
    spec.bark_g = 0.30f;
    spec.bark_b = 0.18f;
    spec.description = "A small twig.";
    return spec;
}

void FallenTreeSpec::apply_natural_variation(float variance_amount, unsigned int seed)
{
    // Simple LCG for deterministic randomness
    unsigned int rng_state = seed;
    auto rng_next = [&rng_state]() -> float
    {
        rng_state = (1103515245 * rng_state + 12345) % 2147483648;
        return (float)rng_state / 2147483648.0f;
    };

    auto vary = [&rng_next, variance_amount](float base, float max_variance) -> float
    {
        float random = rng_next();
        float normalized = (random * 2.0f - 1.0f);
        float curved = normalized * normalized * normalized;
        return base + base * max_variance * variance_amount * curved;
    };

    // Vary dimensions
    length = vary(length, 0.35f);
    diameter = vary(diameter, 0.30f);

    // Vary color
    bark_r = std::max(0.1f, std::min(0.6f, vary(bark_r, 0.2f)));
    bark_g = std::max(0.1f, std::min(0.4f, vary(bark_g, 0.2f)));
    bark_b = std::max(0.05f, std::min(0.3f, vary(bark_b, 0.2f)));

    // Random rotation (0-360 degrees)
    rotation_angle = rng_next() * 360.0f;

    // Clamp dimensions to reasonable ranges
    switch (type)
    {
    case FallenType::TRUNK:
        length = std::max(3.0f, std::min(10.0f, length));
        diameter = std::max(0.4f, std::min(1.2f, diameter));
        break;
    case FallenType::LOG:
        length = std::max(1.0f, std::min(4.0f, length));
        diameter = std::max(0.2f, std::min(0.8f, diameter));
        break;
    case FallenType::BRANCH:
        length = std::max(0.5f, std::min(2.5f, length));
        diameter = std::max(0.18f, std::min(0.35f, diameter)); // Min 18cm for visibility
        break;
    case FallenType::TWIG:
        length = std::max(0.4f, std::min(0.8f, length));
        diameter = std::max(0.15f, std::min(0.22f, diameter)); // Min 15cm for visibility
        break;
    }
}

FallenTreeGenerator::FallenTreeGenerator()
    : EntityGenerator(), rng_state_(0)
{
}

FallenTreeGenerator::~FallenTreeGenerator()
{
}

kg::EntityID FallenTreeGenerator::generate_fallen_tree(float world_x, float world_y, float world_z,
                                                       const FallenTreeSpec &spec)
{
    if (!engine_ || !kg_)
    {
        std::cerr << "[FallenTreeGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Seed RNG for reproducible variation - use primes to avoid nearby logs having similar angles
    seed_rng(static_cast<unsigned int>(world_x * 73856093 + world_y * 19349663 + world_z * 83492791));

    // Create KG entity (with automatic chunk assignment)
    kg::EntityID entity = kg_->createEntityAtPosition("FallenTree", world_x, world_y);
    kg_->setProperty(entity, "z", std::to_string(world_z));
    kg_->setProperty(entity, "length", std::to_string(spec.length));
    kg_->setProperty(entity, "diameter", std::to_string(spec.diameter));
    kg_->setProperty(entity, "description", spec.description);

    // Calculate direction from rotation angle (lying horizontal)
    float angle_rad = spec.rotation_angle * M_PI / 180.0f;
    float dir_x = std::cos(angle_rad);
    float dir_y = std::sin(angle_rad);

    // Segment length
    float segment_length = spec.length / spec.num_segments;

    // Store particle IDs for constraint creation
    std::vector<kg::KGParticleID> segment_particles;

    // Create segments along the length
    for (int i = 0; i < spec.num_segments; ++i)
    {
        float t = (i + 0.5f) / spec.num_segments; // Center of segment

        // Position along the log
        float seg_x = world_x + dir_x * spec.length * (t - 0.5f); // Center at world position
        float seg_y = world_y + dir_y * spec.length * (t - 0.5f);
        // SIT ON THE GROUND MEANS SIT ON THE Z EXTENT. The log is laid
        // horizontal by rotation_y, but collision bounds use `thickness` as the
        // world-Z extent without applying rotation, and create_log_segment sets
        // thickness = length. Offsetting by half the DIAMETER therefore buried
        // every preset: fallen_trunk -0.26, fallen_branch -0.2875, twig -0.24.
        // The half-extent is half the segment length, plus the 1.1 overlap the
        // caller applies.
        const float seg_len_z = (spec.length / std::max(1, spec.num_segments)) * 1.1f;
        float seg_z = world_z + seg_len_z * 0.5f; // Sit on ground

        // Vary diameter slightly along length (taper at ends)
        float taper = 1.0f - 0.15f * std::abs(t - 0.5f) * 2.0f; // Slight taper at ends
        float seg_diameter = spec.diameter * taper;

        // Color variation per segment
        float r = spec.bark_r + random_variance(0.0f, spec.color_variance);
        float g = spec.bark_g + random_variance(0.0f, spec.color_variance);
        float b = spec.bark_b + random_variance(0.0f, spec.color_variance);
        r = std::max(0.0f, std::min(1.0f, r));
        g = std::max(0.0f, std::min(1.0f, g));
        b = std::max(0.0f, std::min(1.0f, b));

        kg::KGParticleID particle = create_segment(
            seg_x, seg_y, seg_z,
            segment_length * 1.1f, // Slight overlap for seamless appearance
            seg_diameter,
            spec.rotation_angle,
            0.0f, // Horizontal (lying on ground)
            r, g, b,
            entity);

        segment_particles.push_back(particle);
    }

    // Create constraints between adjacent segments
    for (size_t i = 1; i < segment_particles.size(); ++i)
    {
        create_constraint(entity, segment_particles[i - 1], segment_particles[i], spec.stiffness);
    }

    // Track entity
    on_entity_created(entity);

    std::cout << "[FallenTreeGenerator] Created fallen tree at (" << world_x << ", " << world_y
              << ") length=" << spec.length << "m, diameter=" << spec.diameter << "m"
              << " segments=" << spec.num_segments << std::endl;

    // Queue for activation
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(entity);

    return entity;
}

kg::KGParticleID FallenTreeGenerator::create_segment(float x, float y, float z,
                                                     float length, float diameter,
                                                     float rotation_h, float rotation_v,
                                                     float r, float g, float b,
                                                     kg::EntityID entity_id)
{
    Particle p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 1.0f;

    // BOX shape lying horizontal
    p.shape = ParticleShape::BOX;
    p.width = diameter;   // Cross-section
    p.height = diameter;  // Cross-section
    p.thickness = length; // Length of segment

    // Rotation: log lies horizontal pointing in rotation_h direction
    // BOX thickness axis points UP by default, we rotate to horizontal
    float rad_h = rotation_h * M_PI / 180.0f;

    // We want the thickness (length) to point horizontal in direction rotation_h
    // Default BOX: thickness axis = Z (up)
    // After rotation: thickness axis should be in XY plane at angle rotation_h
    //
    // Rotation sequence (for primary_axis calculation in render_pipeline):
    // 1. Start: (0, 0, 1) = Z up
    // 2. Rotate 90° around Y: (0, 0, 1) → (1, 0, 0) = East
    // 3. Rotate around Z by rad_h: (cos(rad_h), sin(rad_h), 0) = desired direction
    p.rotation_x = 0.0f;
    p.rotation_y = M_PI / 2.0f; // Tilt 90° so thickness is horizontal (pointing East)
    p.rotation_z = rad_h;       // Rotate to point in desired direction

    p.reflectivity = 0.25f;
    p.pattern_id = 1; // PATTERN_WOOD

    // Physics properties - wood density ~600 kg/m³
    // Mass auto-calculates from volume and material_density
    p.material_density = 600.0f;

    // Store in KG
    kg::KGParticleID kg_id = kg_->createKGParticle(entity_id, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(kg_id, p);

    return kg_id;
}

void FallenTreeGenerator::seed_rng(unsigned int seed)
{
    rng_state_ = seed;
}

float FallenTreeGenerator::random_variance(float base, float variance)
{
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float normalized = (float)rng_state_ / 2147483648.0f;
    float offset = (normalized * 2.0f - 1.0f) * variance;
    return base + offset;
}

float FallenTreeGenerator::random_range(float min, float max)
{
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float normalized = (float)rng_state_ / 2147483648.0f;
    return min + normalized * (max - min);
}
