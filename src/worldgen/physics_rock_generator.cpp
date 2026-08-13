#include "logosphere/worldgen/physics_rock_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/kg/kg_module.h"
#include <cmath>
#include <iostream>
#include <ctime>
#include <string>

// ============================================================================
// ROCK SPEC PRESETS
// ============================================================================

PhysicsRockSpec PhysicsRockSpec::pebble() {
    PhysicsRockSpec spec;
    spec.type = Type::PEBBLE;
    spec.size = 0.5f;  // 50cm - min for 15cm sub-boxes to render
    spec.density = 2500.0f;
    spec.material_strength = 50e6f;
    spec.r = 0.5f; spec.g = 0.5f; spec.b = 0.5f;
    spec.color_variance = 0.1f;
    return spec;
}

PhysicsRockSpec PhysicsRockSpec::small_rock() {
    PhysicsRockSpec spec;
    spec.type = Type::SMALL_ROCK;
    spec.size = 0.4f;
    spec.density = 2500.0f;
    spec.material_strength = 45e6f;
    spec.r = 0.5f; spec.g = 0.5f; spec.b = 0.5f;
    spec.color_variance = 0.15f;
    return spec;
}

PhysicsRockSpec PhysicsRockSpec::medium_rock() {
    PhysicsRockSpec spec;
    spec.type = Type::MEDIUM_ROCK;
    spec.size = 0.8f;
    spec.density = 2500.0f;
    spec.material_strength = 40e6f;
    spec.r = 0.5f; spec.g = 0.5f; spec.b = 0.5f;
    spec.color_variance = 0.2f;
    return spec;
}

PhysicsRockSpec PhysicsRockSpec::large_boulder() {
    PhysicsRockSpec spec;
    spec.type = Type::LARGE_BOULDER;
    spec.size = 1.5f;
    spec.density = 2500.0f;
    spec.material_strength = 35e6f;
    spec.r = 0.5f; spec.g = 0.5f; spec.b = 0.5f;
    spec.color_variance = 0.25f;
    return spec;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PhysicsRockGenerator::PhysicsRockGenerator()
    : engine_(nullptr)
    , physics_(nullptr)
    , particles_(nullptr)
    , kg_(nullptr)
    , rng_state_(static_cast<unsigned int>(time(nullptr)))
{
}

PhysicsRockGenerator::~PhysicsRockGenerator() {
}

void PhysicsRockGenerator::initialize(Engine* engine) {
    engine_ = engine;
    physics_ = &engine->get_physics_system();
    particles_ = &engine->get_particle_system();
    kg_ = &engine->get_kg();
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

void PhysicsRockGenerator::seed_rng(unsigned int seed) {
    rng_state_ = seed;
}

float PhysicsRockGenerator::random_range(float min, float max) {
    rng_state_ = (rng_state_ * 1103515245 + 12345) & 0x7fffffff;
    float random_01 = (rng_state_ % 1000) / 1000.0f;
    return min + random_01 * (max - min);
}

float PhysicsRockGenerator::random_variance(float base, float variance) {
    rng_state_ = (rng_state_ * 1103515245 + 12345) & 0x7fffffff;
    float random_01 = (rng_state_ % 1000) / 1000.0f;
    float offset = (random_01 * 2.0f - 1.0f) * variance;
    return base + offset;
}

// ============================================================================
// HELPER: CREATE ROCK BOX PARTICLE
// ============================================================================

int PhysicsRockGenerator::create_rock_box(
    float x, float y, float z,
    float width, float height, float depth,
    float rotation_x, float rotation_y, float rotation_z,
    float r, float g, float b,
    float density)
{
    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;

    // BOX shape
    p.shape = ParticleShape::BOX;
    p.width = width;
    p.height = height;
    p.thickness = depth;

    // Rotation for organic look
    p.rotation_x = rotation_x;
    p.rotation_y = rotation_y;
    p.rotation_z = rotation_z;

    // Appearance
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 1.0f;

    // Physics: mass auto-calculates from volume and material_density
    p.material_density = density;

    // High friction for rocks
    p.friction = 0.8f;

    // No light emission
    p.is_light_source = false;

    // Material pattern
    p.pattern_id = 2;  // PATTERN_STONE for rock texture

    // COLLISION CHECK: Verify box doesn't overlap existing particles
    // This is belt-and-suspenders with the geometric spacing in generate_rock_on_floor
    if (!particles_->try_place_with_retry(p, 10, 0.3f)) {
        std::cout << "[PhysicsRockGenerator] WARNING: Box placement collision, using best-effort position"
                  << std::endl;
        // Still add the particle at original position - geometry should have prevented overlap
    }

    return particles_->add_particle(p);
}

// ============================================================================
// HELPER: CREATE ROCK GLUON CONSTRAINT
// ============================================================================

void PhysicsRockGenerator::create_rock_gluon(int box_a, int box_b, float material_strength) {
    Vec3 offset_a, offset_b;
    float contact_area;

    // Get particle positions to calculate contact area (scoped read lock)
    {
        auto particles = particles_->lock_particles_for_read();
        const Particle& pa = particles[box_a];
        const Particle& pb = particles[box_b];

        // Contact area = average of smaller dimensions
        float area_a = std::min(pa.width, std::min(pa.height, pa.thickness));
        float area_b = std::min(pb.width, std::min(pb.height, pb.thickness));
        contact_area = (area_a + area_b) * 0.5f * (area_a + area_b) * 0.5f;

        // Calculate offsets (edge-to-edge connection)
        float dx = pb.x - pa.x;
        float dy = pb.y - pa.y;
        float dz = pb.z - pa.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 0.001f) dist = 0.001f;

        // Normalize direction
        dx /= dist; dy /= dist; dz /= dist;

        // Calculate half-extent in the connection direction for each box
        // This is how far from center to surface along direction (dx, dy, dz)
        float half_ext_a = std::abs(dx) * pa.width * 0.5f +
                           std::abs(dy) * pa.height * 0.5f +
                           std::abs(dz) * pa.thickness * 0.5f;
        float half_ext_b = std::abs(dx) * pb.width * 0.5f +
                           std::abs(dy) * pb.height * 0.5f +
                           std::abs(dz) * pb.thickness * 0.5f;

        // Offset from center to surface in the connection direction
        // When boxes are edge-to-edge touching, attach_a == attach_b (constraint satisfied)
        offset_a = Vec3(dx * half_ext_a, dy * half_ext_a, dz * half_ext_a);
        offset_b = Vec3(-dx * half_ext_b, -dy * half_ext_b, -dz * half_ext_b);
    }  // Read lock released here

    auto gluon = std::make_unique<OrganicGluon>();
    gluon->offset_a = offset_a;
    gluon->offset_b = offset_b;
    gluon->target_distance = 0.0f;
    gluon->contact_area = contact_area;
    // Force law derived at registration from materials (Phase C).

    // Set material strength for breaking calculation (now safe - no read lock held)
    {
        auto particles_write = particles_->lock_particles_for_write();
        particles_write[box_a].material_strength = material_strength;
        particles_write[box_b].material_strength = material_strength;
    }

    physics_->add_gluon_between(box_a, box_b, std::move(gluon));
}

// ============================================================================
// MAIN GENERATION ENTRY POINT
// ============================================================================

PhysicsRockResult PhysicsRockGenerator::generate_rock(
    float world_x, float world_y, float world_z,
    const PhysicsRockSpec& spec)
{
    if (!engine_ || !physics_ || !particles_) {
        std::cerr << "[PhysicsRockGenerator] ERROR: Not initialized!" << std::endl;
        return PhysicsRockResult();
    }

    std::cout << "[PhysicsRockGenerator] Generating rock at ("
              << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;
    std::cout << "  size=" << spec.size << "m, density=" << spec.density << "kg/m³" << std::endl;

    seed_rng(static_cast<unsigned int>(world_x * 1000 + world_y * 100 + world_z * 10));

    return generate_rock_on_floor(world_x, world_y, world_z, -1, spec);
}

PhysicsRockResult PhysicsRockGenerator::generate_rock_on_floor(
    float world_x, float world_y, float world_z,
    int floor_particle_id,
    const PhysicsRockSpec& spec)
{
    if (!engine_ || !physics_ || !particles_) {
        std::cerr << "[PhysicsRockGenerator] ERROR: Not initialized!" << std::endl;
        return PhysicsRockResult();
    }

    PhysicsRockResult result;
    float half_size = spec.size * 0.5f;

    // ========================================================================
    // PEBBLE: 3-box egg shape
    // ========================================================================
    if (spec.type == PhysicsRockSpec::Type::PEBBLE) {
        // Central box (larger)
        float center_w = half_size;
        float center_h = half_size * 0.75f;
        float center_d = half_size;

        float r1 = random_variance(spec.r, spec.color_variance);
        float g1 = random_variance(spec.g, spec.color_variance);
        float b1 = random_variance(spec.b, spec.color_variance);

        result.core_id = create_rock_box(
            // center_h is the Y extent; thickness comes from center_d. Offsetting
        // by center_h left every pebble 0.125*half_size under the floor.
        world_x, world_y, world_z + center_d * 0.5f,
            center_w, center_h, center_d,
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            r1, g1, b1, spec.density
        );
        result.box_ids.push_back(result.core_id);
        result.total_boxes++;

        // Side boxes (smaller) - NO OVERLAP with center
        // Center extends center_w/2 from origin. Side boxes must be placed
        // at distance >= center_half + side_half to avoid overlap.
        float side_size = half_size * 0.5f;  // Slightly smaller for clearance
        float center_half = center_w * 0.5f;
        float side_half = side_size * 0.5f;
        float offset = center_half + side_half + 0.02f;  // 2cm gap

        for (int side = -1; side <= 1; side += 2) {
            float r = random_variance(spec.r, spec.color_variance);
            float g = random_variance(spec.g, spec.color_variance);
            float b = random_variance(spec.b, spec.color_variance);

            int box_id = create_rock_box(
                world_x + side * offset, world_y, world_z + side_size * 0.5f,
                side_size, side_size * 0.8f, side_size,  // Slightly flatter
                random_range(-0.15f, 0.15f),
                random_range(-0.15f, 0.15f),
                random_range(-0.15f, 0.15f),
                r, g, b, spec.density
            );
            result.box_ids.push_back(box_id);
            result.total_boxes++;

            // Connect to center
            create_rock_gluon(result.core_id, box_id, spec.material_strength);
        }

        // Connect left to right for stability
        if (result.box_ids.size() >= 3) {
            create_rock_gluon(result.box_ids[1], result.box_ids[2], spec.material_strength);
        }
    }
    // ========================================================================
    // LARGER ROCKS: More boxes in cluster
    // ========================================================================
    else {
        int num_boxes = 3;
        if (spec.type == PhysicsRockSpec::Type::SMALL_ROCK) num_boxes = 5;
        else if (spec.type == PhysicsRockSpec::Type::MEDIUM_ROCK) num_boxes = 7;
        else if (spec.type == PhysicsRockSpec::Type::LARGE_BOULDER) num_boxes = 10;

        // Create central core
        float core_size = half_size * 0.8f;
        float r = random_variance(spec.r, spec.color_variance);
        float g = random_variance(spec.g, spec.color_variance);
        float b = random_variance(spec.b, spec.color_variance);

        result.core_id = create_rock_box(
            world_x, world_y, world_z + core_size * 0.5f,
            core_size, core_size * 0.7f, core_size,
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            r, g, b, spec.density
        );
        result.box_ids.push_back(result.core_id);
        result.total_boxes++;

        // Create surrounding boxes - NO OVERLAP with core
        // Core extends core_size/2 from center. Each box must be placed
        // at distance >= core_half + box_half to avoid overlap.
        float core_half = core_size * 0.5f;

        for (int i = 1; i < num_boxes; i++) {
            float angle = (float)i / (float)(num_boxes - 1) * 2.0f * 3.14159f;
            float box_size = half_size * random_range(0.3f, 0.5f);
            float box_half = box_size * 0.5f;

            // Minimum radius to avoid overlap + small gap for physics stability
            float min_radius = core_half + box_half + 0.02f;  // 2cm gap
            float radius = min_radius + random_range(0.0f, half_size * 0.15f);

            float bx = world_x + cosf(angle) * radius;
            float by = world_y + sinf(angle) * radius;
            // Z: place box so bottom is at or above ground, no Z overlap with core
            float bz = world_z + box_size * 0.5f;

            r = random_variance(spec.r, spec.color_variance);
            g = random_variance(spec.g, spec.color_variance);
            b = random_variance(spec.b, spec.color_variance);

            int box_id = create_rock_box(
                bx, by, bz,
                box_size, box_size * random_range(0.6f, 1.0f), box_size,
                random_range(-0.2f, 0.2f),
                random_range(-0.2f, 0.2f),
                random_range(-0.2f, 0.2f),
                r, g, b, spec.density
            );
            result.box_ids.push_back(box_id);
            result.total_boxes++;

            // Connect to core
            create_rock_gluon(result.core_id, box_id, spec.material_strength);
        }

        // Add some cross-connections for structural stability
        for (size_t i = 1; i < result.box_ids.size() - 1; i++) {
            create_rock_gluon(result.box_ids[i], result.box_ids[i + 1], spec.material_strength);
        }
    }

    std::cout << "[PhysicsRockGenerator] Created rock with " << result.total_boxes
              << " boxes" << std::endl;

    return result;
}

// ============================================================================
// HELPER: BUILD PARTICLE STRUCT (without adding to ParticleSystem)
// ============================================================================

Particle PhysicsRockGenerator::build_rock_box_particle(
    float x, float y, float z,
    float width, float height, float depth,
    float rotation_x, float rotation_y, float rotation_z,
    float r, float g, float b,
    float density)
{
    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;
    p.shape = ParticleShape::BOX;
    p.width = width;
    p.height = height;
    p.thickness = depth;
    p.rotation_x = rotation_x;
    p.rotation_y = rotation_y;
    p.rotation_z = rotation_z;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 1.0f;
    p.material_density = density;
    p.friction = 0.8f;
    p.is_light_source = false;
    p.pattern_id = 2;  // PATTERN_STONE
    return p;
}

// ============================================================================
// HELPER: BUILD GLUON DATA (for KG storage)
// ============================================================================

kg::KGGluonData PhysicsRockGenerator::build_gluon_data(
    const Particle& pa, const Particle& pb,
    float material_strength)
{
    kg::KGGluonData data;
    data.type = kg::KGGluonType::ORGANIC;

    // Calculate direction between particles
    float dx = pb.x - pa.x;
    float dy = pb.y - pa.y;
    float dz = pb.z - pa.z;
    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dist < 0.001f) dist = 0.001f;
    dx /= dist; dy /= dist; dz /= dist;

    // Calculate half-extent in connection direction
    float half_ext_a = std::abs(dx) * pa.width * 0.5f +
                       std::abs(dy) * pa.height * 0.5f +
                       std::abs(dz) * pa.thickness * 0.5f;
    float half_ext_b = std::abs(dx) * pb.width * 0.5f +
                       std::abs(dy) * pb.height * 0.5f +
                       std::abs(dz) * pb.thickness * 0.5f;

    // Offsets from center to surface
    data.offset_a_x = dx * half_ext_a;
    data.offset_a_y = dy * half_ext_a;
    data.offset_a_z = dz * half_ext_a;
    data.offset_b_x = -dx * half_ext_b;
    data.offset_b_y = -dy * half_ext_b;
    data.offset_b_z = -dz * half_ext_b;

    data.target_distance = 0.0f;

    // Contact area = average of smaller dimensions squared
    float area_a = std::min(pa.width, std::min(pa.height, pa.thickness));
    float area_b = std::min(pb.width, std::min(pb.height, pb.thickness));
    data.contact_area = (area_a + area_b) * 0.5f * (area_a + area_b) * 0.5f;

    data.stiffness = 50000.0f;
    data.damping = 1000.0f;

    // Store material strength in breaking_force for OrganicGluon
    // (actual breaking force = contact_area * material_strength)
    data.breaking_force = material_strength;

    return data;
}

// ============================================================================
// PERMAWORLD: STORE ROCK ENTITY IN KG (no particles created)
// ============================================================================

kg::EntityID PhysicsRockGenerator::store_rock_entity(
    float world_x, float world_y, float world_z,
    const PhysicsRockSpec& spec,
    float chunk_size)
{
    if (!kg_ || !kg_->isEnabled()) {
        std::cerr << "[PhysicsRockGenerator] ERROR: KG not enabled!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Seed RNG for deterministic particle generation
    seed_rng(static_cast<unsigned int>(world_x * 1000 + world_y * 100 + world_z * 10));

    // Create KG entity with chunk coordinates
    kg::EntityID entity_id = kg_->createEntityAtPosition("PhysicsRock", world_x, world_y, chunk_size);
    if (entity_id == kg::INVALID_ENTITY) {
        std::cerr << "[PhysicsRockGenerator] ERROR: Failed to create KG entity!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Store spec properties
    kg_->setProperty(entity_id, "rock_type", std::to_string(static_cast<int>(spec.type)));
    kg_->setProperty(entity_id, "size", std::to_string(spec.size));
    kg_->setProperty(entity_id, "density", std::to_string(spec.density));
    kg_->setProperty(entity_id, "material_strength", std::to_string(spec.material_strength));
    kg_->setProperty(entity_id, "color_r", std::to_string(spec.r));
    kg_->setProperty(entity_id, "color_g", std::to_string(spec.g));
    kg_->setProperty(entity_id, "color_b", std::to_string(spec.b));
    kg_->setProperty(entity_id, "color_variance", std::to_string(spec.color_variance));

    // Generate particles (store in KG, no ParticleSystem)
    std::vector<kg::KGParticleID> kg_particle_ids;
    std::vector<Particle> particles;

    float half_size = spec.size * 0.5f;

    if (spec.type == PhysicsRockSpec::Type::PEBBLE) {
        // Central box
        float center_w = half_size;
        float center_h = half_size * 0.75f;
        float center_d = half_size;

        Particle core = build_rock_box_particle(
            // center_h is the Y extent; thickness comes from center_d. Offsetting
        // by center_h left every pebble 0.125*half_size under the floor.
        world_x, world_y, world_z + center_d * 0.5f,
            center_w, center_h, center_d,
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_variance(spec.r, spec.color_variance),
            random_variance(spec.g, spec.color_variance),
            random_variance(spec.b, spec.color_variance),
            spec.density
        );
        core.material_strength = spec.material_strength;
        particles.push_back(core);

        // Side boxes
        float side_size = half_size * 0.5f;
        float center_half = center_w * 0.5f;
        float side_half = side_size * 0.5f;
        float offset = center_half + side_half + 0.02f;

        for (int side = -1; side <= 1; side += 2) {
            Particle p = build_rock_box_particle(
                world_x + side * offset, world_y, world_z + side_size * 0.5f,
                side_size, side_size * 0.8f, side_size,
                random_range(-0.15f, 0.15f),
                random_range(-0.15f, 0.15f),
                random_range(-0.15f, 0.15f),
                random_variance(spec.r, spec.color_variance),
                random_variance(spec.g, spec.color_variance),
                random_variance(spec.b, spec.color_variance),
                spec.density
            );
            p.material_strength = spec.material_strength;
            particles.push_back(p);
        }
    } else {
        // Larger rocks: multiple boxes in cluster
        int num_boxes = 3;
        if (spec.type == PhysicsRockSpec::Type::SMALL_ROCK) num_boxes = 5;
        else if (spec.type == PhysicsRockSpec::Type::MEDIUM_ROCK) num_boxes = 7;
        else if (spec.type == PhysicsRockSpec::Type::LARGE_BOULDER) num_boxes = 10;

        float core_size = half_size * 0.8f;
        Particle core = build_rock_box_particle(
            world_x, world_y, world_z + core_size * 0.5f,
            core_size, core_size * 0.7f, core_size,
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_range(-0.1f, 0.1f),
            random_variance(spec.r, spec.color_variance),
            random_variance(spec.g, spec.color_variance),
            random_variance(spec.b, spec.color_variance),
            spec.density
        );
        core.material_strength = spec.material_strength;
        particles.push_back(core);

        float core_half = core_size * 0.5f;
        for (int i = 1; i < num_boxes; i++) {
            float angle = (float)i / (float)(num_boxes - 1) * 2.0f * 3.14159f;
            float box_size = half_size * random_range(0.3f, 0.5f);
            float box_half = box_size * 0.5f;
            float min_radius = core_half + box_half + 0.02f;
            float radius = min_radius + random_range(0.0f, half_size * 0.15f);

            Particle p = build_rock_box_particle(
                world_x + cosf(angle) * radius,
                world_y + sinf(angle) * radius,
                world_z + box_size * 0.5f,
                box_size, box_size * random_range(0.6f, 1.0f), box_size,
                random_range(-0.2f, 0.2f),
                random_range(-0.2f, 0.2f),
                random_range(-0.2f, 0.2f),
                random_variance(spec.r, spec.color_variance),
                random_variance(spec.g, spec.color_variance),
                random_variance(spec.b, spec.color_variance),
                spec.density
            );
            p.material_strength = spec.material_strength;
            particles.push_back(p);
        }
    }

    // Store particles in KG
    for (const auto& p : particles) {
        // Create KGParticle with INVALID_RENDER_INDEX (not loaded yet)
        kg::KGParticleID kg_pid = kg_->createKGParticle(entity_id, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_pid, p);
        kg_particle_ids.push_back(kg_pid);
    }

    // Store gluon connections in KG
    // Same topology as generate_rock_on_floor()
    if (spec.type == PhysicsRockSpec::Type::PEBBLE && particles.size() >= 3) {
        // Core to left, core to right
        auto gluon1 = kg_->createKGGluon(entity_id, kg_particle_ids[0], kg_particle_ids[1]);
        kg_->setKGGluonData(gluon1, build_gluon_data(particles[0], particles[1], spec.material_strength));

        auto gluon2 = kg_->createKGGluon(entity_id, kg_particle_ids[0], kg_particle_ids[2]);
        kg_->setKGGluonData(gluon2, build_gluon_data(particles[0], particles[2], spec.material_strength));

        // Left to right
        auto gluon3 = kg_->createKGGluon(entity_id, kg_particle_ids[1], kg_particle_ids[2]);
        kg_->setKGGluonData(gluon3, build_gluon_data(particles[1], particles[2], spec.material_strength));
    } else {
        // Core to all surrounding boxes
        for (size_t i = 1; i < particles.size(); i++) {
            auto gluon = kg_->createKGGluon(entity_id, kg_particle_ids[0], kg_particle_ids[i]);
            kg_->setKGGluonData(gluon, build_gluon_data(particles[0], particles[i], spec.material_strength));
        }
        // Cross-connections for stability
        for (size_t i = 1; i < particles.size() - 1; i++) {
            auto gluon = kg_->createKGGluon(entity_id, kg_particle_ids[i], kg_particle_ids[i + 1]);
            kg_->setKGGluonData(gluon, build_gluon_data(particles[i], particles[i + 1], spec.material_strength));
        }
    }

    std::cout << "[PhysicsRockGenerator] Stored physics_rock entity " << entity_id
              << " with " << particles.size() << " particles and "
              << kg_->getEntityKGGluons(entity_id).size() << " gluons in KG" << std::endl;

    return entity_id;
}
