#include "logosphere/worldgen/rock_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "entity_manager.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"
#include <cmath>
#include <iostream>
#include <ctime>
#include <string>

RockGenerator::RockGenerator()
    : rng_state_(static_cast<unsigned int>(time(nullptr))) {
}

RockGenerator::~RockGenerator() {
}

// ============================================================================
// ROCK SPEC PRESETS
// ============================================================================

RockSpec RockSpec::pebble() {
    RockSpec spec;
    spec.type = RockType::PEBBLE;
    spec.size = 0.5f;                // 50cm - min for 15cm sub-boxes to render
    spec.core_stiffness = 98000.0f;  // Very rigid (pebbles don't break apart)
    spec.detail_stiffness = 98000.0f; // Same for pebbles (all rigid)
    spec.core_boxes = 3;             // Egg shape: 1 center + 2 sides
    spec.detail_boxes = 0;           // No detail boxes for simple pebbles

    // Gray rock color with subtle variance
    spec.base_r = 0.5f;
    spec.base_g = 0.5f;
    spec.base_b = 0.5f;
    spec.color_variance = 0.1f;

    spec.description = "A smooth, egg-shaped pebble. Its surface is worn from centuries of weather.";

    return spec;
}

RockSpec RockSpec::small_rock() {
    RockSpec spec;
    spec.type = RockType::SMALL_ROCK;
    spec.size = 0.4f;                // 40cm fist-sized
    spec.core_stiffness = 95000.0f;  // Still very rigid
    spec.detail_stiffness = 50000.0f; // Detail can break off
    spec.core_boxes = 4;
    spec.detail_boxes = 4;
    spec.base_r = 0.5f;
    spec.base_g = 0.5f;
    spec.base_b = 0.5f;
    spec.color_variance = 0.15f;
    spec.description = "A fist-sized rock with rough edges. Good for throwing or building a fire ring.";
    return spec;
}

RockSpec RockSpec::medium_rock() {
    RockSpec spec;
    spec.type = RockType::MEDIUM_ROCK;
    spec.size = 0.8f;                // 80cm head-sized
    spec.core_stiffness = 90000.0f;
    spec.detail_stiffness = 45000.0f;
    spec.core_boxes = 6;
    spec.detail_boxes = 6;
    spec.base_r = 0.5f;
    spec.base_g = 0.5f;
    spec.base_b = 0.5f;
    spec.color_variance = 0.2f;
    spec.description = "A head-sized rock, partially covered in lichen. Heavy enough to anchor a tent.";
    return spec;
}

RockSpec RockSpec::large_boulder() {
    RockSpec spec;
    spec.type = RockType::LARGE_BOULDER;
    spec.size = 4.0f;                // 4m massive boulder (3-5m with variance)
    spec.core_stiffness = 98000.0f;  // Very rigid
    spec.detail_stiffness = 98000.0f;
    spec.core_boxes = 6;             // Main mass: 6 overlapping boxes
    spec.detail_boxes = 0;
    spec.base_r = 0.45f;             // Slightly darker gray
    spec.base_g = 0.45f;
    spec.base_b = 0.45f;
    spec.color_variance = 0.15f;     // Subtle color variation
    spec.description = "A massive granite boulder, ancient and immovable. Moss clings to its shaded side.";
    return spec;
}

void RockSpec::apply_natural_variation(float variance_amount, unsigned int seed) {
    // Simple LCG for reproducible randomness
    auto next_random = [&seed]() -> float {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return (seed % 1000) / 1000.0f;
    };

    // Apply variance to size and color
    auto vary = [&](float base, float max_variance) -> float {
        float random = next_random();
        float normalized = (random * 2.0f - 1.0f);
        return base + base * max_variance * variance_amount * normalized;
    };

    size = vary(size, 0.20f);  // ±20% size variance
    base_r = std::max(0.3f, std::min(0.7f, vary(base_r, 0.3f)));  // Clamp to reasonable gray range
    base_g = std::max(0.3f, std::min(0.7f, vary(base_g, 0.3f)));
    base_b = std::max(0.3f, std::min(0.7f, vary(base_b, 0.3f)));
}

void RockSpec::update_composition_from_type() {
    switch (type) {
        case RockType::PEBBLE:
            core_boxes = 3;
            detail_boxes = 0;
            break;
        case RockType::SMALL_ROCK:
            core_boxes = 4;
            detail_boxes = 4;
            break;
        case RockType::MEDIUM_ROCK:
            core_boxes = 6;
            detail_boxes = 6;
            break;
        case RockType::LARGE_BOULDER:
            core_boxes = 9;
            detail_boxes = 9;
            break;
    }
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

void RockGenerator::seed_rng(unsigned int seed) {
    rng_state_ = seed;
}

float RockGenerator::random_range(float min, float max) {
    rng_state_ = (rng_state_ * 1103515245 + 12345) & 0x7fffffff;
    float random_01 = (rng_state_ % 1000) / 1000.0f;
    return min + random_01 * (max - min);
}

float RockGenerator::random_variance(float base, float variance) {
    rng_state_ = (rng_state_ * 1103515245 + 12345) & 0x7fffffff;
    float random_01 = (rng_state_ % 1000) / 1000.0f;
    float offset = (random_01 * 2.0f - 1.0f) * variance;
    return base + offset;
}

// ============================================================================
// HELPER: CREATE ROCK BOX PARTICLE
// ============================================================================

kg::KGParticleID RockGenerator::create_rock_box(
    float x, float y, float z,
    float width, float height, float depth,
    float rotation_x, float rotation_y, float rotation_z,
    float r, float g, float b,
    kg::EntityID entity_id) {

    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;

    // BOX shape with independent dimensions
    p.shape = ParticleShape::BOX;
    p.width = width;
    p.height = height;
    p.thickness = depth;

    // 3D rotation for organic appearance
    p.rotation_x = rotation_x;
    p.rotation_y = rotation_y;
    p.rotation_z = rotation_z;

    // Appearance
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 1.0f;

    // Physics properties
    // Rock density: ~2500 kg/m³ (typical granite/basalt)
    // Mass auto-calculates from volume and material_density
    p.SetMaterial(Materials::Type::STONE);

    // No light emission
    p.is_light_source = false;
    p.emission_strength = 0.0f;
    p.emission_radius = 0.0f;

    // Material pattern
    p.pattern_id = 2;  // PATTERN_STONE for rock texture

    // CHUNK-BASED: Store particle data in KG (not ParticleSystem yet)
    // Chunk system will activate later when entity is in view
    kg::KGParticleID kg_id = kg_->createKGParticle(entity_id, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(kg_id, p);

    return kg_id;
}

// ============================================================================
// MAIN GENERATION ENTRY POINT
// ============================================================================

kg::EntityID RockGenerator::generate_rock(float world_x, float world_y, float world_z,
                                          const RockSpec& spec) {
    // Dispatch to appropriate generator based on rock type
    switch (spec.type) {
        case RockSpec::RockType::PEBBLE:
            return generate_pebble(world_x, world_y, world_z, spec);

        case RockSpec::RockType::SMALL_ROCK:
        case RockSpec::RockType::MEDIUM_ROCK:
        case RockSpec::RockType::LARGE_BOULDER:
            return generate_complex_rock(world_x, world_y, world_z, spec);

        default:
            std::cerr << "[RockGenerator] Unknown rock type!" << std::endl;
            return 0;
    }
}

// ============================================================================
// PHASE 1: SIMPLE PEBBLE GENERATION
// ============================================================================

kg::EntityID RockGenerator::generate_pebble(float x, float y, float z, const RockSpec& spec) {
    if (!kg_) {
        std::cerr << "[RockGenerator] Cannot generate pebble - KG not initialized!" << std::endl;
        return 0;
    }

    std::cout << "[ROCK] Generating pebble at (" << x << ", " << y << ", " << z << ")" << std::endl;
    std::cout << "[ROCK]   Size: " << spec.size << "m, Stiffness: " << spec.core_stiffness << "N/m" << std::endl;

    // Create rock entity in KG ("Rock": ontology name + activator key)
    kg::EntityID rock_entity = kg_->createEntityAtPosition("Rock", x, y);
    kg_->setProperty(rock_entity, "rock_type", "pebble");
    kg_->setProperty(rock_entity, "size", std::to_string(spec.size));
    kg_->setProperty(rock_entity, "description", spec.description);

    // Pebble: 3 overlapping boxes scattered randomly around center
    // Creates natural lumpy rock appearance

    float half_size = spec.size * 0.5f;

    // Box dimensions (varied sizes for organic look)
    float box1_size = half_size * random_range(0.7f, 1.0f);
    float box2_size = half_size * random_range(0.5f, 0.8f);
    float box3_size = half_size * random_range(0.5f, 0.8f);

    // Apply color variance per box for organic look
    float r1 = random_variance(spec.base_r, spec.color_variance);
    float g1 = random_variance(spec.base_g, spec.color_variance);
    float b1 = random_variance(spec.base_b, spec.color_variance);

    float r2 = random_variance(spec.base_r, spec.color_variance);
    float g2 = random_variance(spec.base_g, spec.color_variance);
    float b2 = random_variance(spec.base_b, spec.color_variance);

    float r3 = random_variance(spec.base_r, spec.color_variance);
    float g3 = random_variance(spec.base_g, spec.color_variance);
    float b3 = random_variance(spec.base_b, spec.color_variance);

    // Create 3 boxes scattered randomly around center
    std::cout << "[ROCK] Creating 3 boxes (scattered pebble)" << std::endl;

    // Central box (at origin)
    kg::KGParticleID center_id = create_rock_box(
        x, y, z + box1_size * 0.5f,
        box1_size, box1_size * 0.75f, box1_size,
        random_range(-0.3f, 0.3f),
        random_range(-0.3f, 0.3f),
        random_range(-0.3f, 0.3f),
        r1, g1, b1,
        rock_entity
    );

    // Second box: random angle from center
    float angle1 = random_range(0.0f, 6.28318f);
    float dist1 = half_size * random_range(0.3f, 0.6f);
    kg::KGParticleID box2_id = create_rock_box(
        x + std::cos(angle1) * dist1,
        y + std::sin(angle1) * dist1,
        z + box2_size * 0.5f,
        box2_size, box2_size * 0.8f, box2_size,
        random_range(-0.4f, 0.4f),
        random_range(-0.4f, 0.4f),
        random_range(-0.4f, 0.4f),
        r2, g2, b2,
        rock_entity
    );

    // Third box: different random angle from center
    float angle2 = random_range(0.0f, 6.28318f);
    float dist2 = half_size * random_range(0.3f, 0.6f);
    kg::KGParticleID box3_id = create_rock_box(
        x + std::cos(angle2) * dist2,
        y + std::sin(angle2) * dist2,
        z + box3_size * 0.5f,
        box3_size, box3_size * 0.8f, box3_size,
        random_range(-0.4f, 0.4f),
        random_range(-0.4f, 0.4f),
        random_range(-0.4f, 0.4f),
        r3, g3, b3,
        rock_entity
    );

    // Rename for constraint creation
    kg::KGParticleID left_id = box2_id;
    kg::KGParticleID right_id = box3_id;

    std::cout << "[ROCK] Created particles - Center: " << center_id
              << ", Left: " << left_id << ", Right: " << right_id << std::endl;

    // Create constraints connecting all boxes (fully connected triangle)
    // Very high stiffness (98,000 N/m) makes pebble rigid
    // Use base class create_constraint() (entity-level API)
    std::cout << "[ROCK] Creating 3 constraints (fully connected)" << std::endl;

    create_constraint(rock_entity, center_id, left_id, spec.core_stiffness);
    create_constraint(rock_entity, center_id, right_id, spec.core_stiffness);
    create_constraint(rock_entity, left_id, right_id, spec.core_stiffness);

    std::cout << "[ROCK] Pebble complete! Entity: " << rock_entity << " with 3 particles and 3 constraints" << std::endl;

    // Queue for activation on main thread (thread-safe)
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(rock_entity);

    return rock_entity;
}

// ============================================================================
// PHASE 2: COMPLEX ROCK GENERATION (BOULDERS)
// ============================================================================

kg::EntityID RockGenerator::generate_complex_rock(float x, float y, float z, const RockSpec& spec) {
    if (!kg_) {
        std::cerr << "[RockGenerator] Cannot generate boulder - KG not initialized!" << std::endl;
        return 0;
    }

    std::cout << "[ROCK] Generating boulder at (" << x << ", " << y << ", " << z << ")" << std::endl;
    std::cout << "[ROCK]   Size: " << spec.size << "m" << std::endl;

    // Create rock entity in KG ("Rock": ontology name + activator key)
    kg::EntityID rock_entity = kg_->createEntityAtPosition("Rock", x, y);
    kg_->setProperty(rock_entity, "rock_type", "boulder");
    kg_->setProperty(rock_entity, "size", std::to_string(spec.size));
    kg_->setProperty(rock_entity, "description", spec.description);

    float half_size = spec.size * 0.5f;

    // Boulder composition: 1 large core + 5 smaller overlapping masses
    // Creates organic lumpy appearance
    std::vector<kg::KGParticleID> particle_ids;

    // Core: large central mass (slightly flattened)
    float core_w = half_size * random_range(0.9f, 1.1f);
    float core_h = half_size * random_range(0.6f, 0.8f);  // Flatter
    float core_d = half_size * random_range(0.9f, 1.1f);

    float r_core = random_variance(spec.base_r, spec.color_variance);
    float g_core = random_variance(spec.base_g, spec.color_variance);
    float b_core = random_variance(spec.base_b, spec.color_variance);

    kg::KGParticleID core_id = create_rock_box(
        // core_h is the Y extent; create_rock_box sets thickness = depth, so
        // the Z half-extent is core_d. Offsetting by core_h put every boulder
        // core under the floor (core_h < core_d always).
        x, y, z + core_d * 0.5f,
        core_w, core_h, core_d,
        random_range(-0.15f, 0.15f),
        random_range(-0.15f, 0.15f),
        random_range(-0.15f, 0.15f),
        r_core, g_core, b_core,
        rock_entity
    );
    particle_ids.push_back(core_id);

    // 5 secondary masses scattered around core at varying heights
    for (int i = 0; i < 5; i++) {
        // Random angle and distance from center
        float angle = random_range(0.0f, 6.28318f);
        float dist = half_size * random_range(0.2f, 0.5f);

        // Position offset
        float px = x + std::cos(angle) * dist;
        float py = y + std::sin(angle) * dist;

        // Varied height (some peek above core, some below)
        float height_offset = half_size * random_range(-0.2f, 0.4f);

        // Varied box size (50-80% of core)
        float scale = random_range(0.5f, 0.8f);
        float bw = core_w * scale * random_range(0.8f, 1.2f);
        float bh = core_h * scale * random_range(0.7f, 1.0f);
        float bd = core_d * scale * random_range(0.8f, 1.2f);

        // Slight color variation (same base, slight shift)
        float r = random_variance(spec.base_r, spec.color_variance * 0.5f);
        float g = random_variance(spec.base_g, spec.color_variance * 0.5f);
        float b = random_variance(spec.base_b, spec.color_variance * 0.5f);

        kg::KGParticleID box_id = create_rock_box(
            // Same axis mix-up as the core, plus height_offset can be
            // negative. Offset by the Z extent and never let the box's
            // underside drop below the boulder's own base.
            px, py, z + std::max(bd * 0.5f, bd * 0.5f + height_offset),
            bw, bh, bd,
            random_range(-0.25f, 0.25f),
            random_range(-0.25f, 0.25f),
            random_range(-0.25f, 0.25f),
            r, g, b,
            rock_entity
        );
        particle_ids.push_back(box_id);
    }

    std::cout << "[ROCK] Created " << particle_ids.size() << " boxes for boulder" << std::endl;

    // Connect all boxes to core with high stiffness (rigid boulder)
    for (size_t i = 1; i < particle_ids.size(); i++) {
        create_constraint(rock_entity, core_id, particle_ids[i], spec.core_stiffness);
    }

    // Also connect adjacent secondary boxes for extra rigidity
    for (size_t i = 1; i < particle_ids.size(); i++) {
        size_t next = (i % (particle_ids.size() - 1)) + 1;
        if (next != i) {
            create_constraint(rock_entity, particle_ids[i], particle_ids[next], spec.core_stiffness);
        }
    }

    std::cout << "[ROCK] Boulder complete! Entity: " << rock_entity << std::endl;

    // Queue for activation
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(rock_entity);

    return rock_entity;
}
