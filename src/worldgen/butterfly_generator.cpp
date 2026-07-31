#include "logosphere/worldgen/butterfly_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"
#include <iostream>

ButterflyGenerator::ButterflyGenerator()
    : engine_(nullptr)
    , kg_(nullptr) {
}

ButterflyGenerator::~ButterflyGenerator() {
}

void ButterflyGenerator::initialize(Engine* engine, kg::KGModule* kg) {
    engine_ = engine;
    kg_ = kg;
}

// ============================================================================
// BUTTERFLY SPEC PRESETS
// ============================================================================

ButterflySpec ButterflySpec::monarch() {
    ButterflySpec spec;
    // Natural base size: 10cm wingspan (realistic for butterflies)
    spec.wing_span = 0.10f;   // 10cm base (will vary 0.05-0.20m = 5-20cm)
    spec.wing_height = 0.06f;  // Proportional height
    spec.wing_r = 1.0f;
    spec.wing_g = 0.5f;
    spec.wing_b = 0.1f;  // Orange wings
    spec.body_r = 0.1f;
    spec.body_g = 0.1f;
    spec.body_b = 0.1f;  // Black body
    spec.wing_beat_frequency = 6.0f;  // Slower, graceful
    spec.flight_speed = 1.5f;
    return spec;
}

ButterflySpec ButterflySpec::blue_morpho() {
    ButterflySpec spec;
    spec.wing_span = 0.35f;
    spec.wing_height = 0.20f;
    spec.wing_r = 0.2f;
    spec.wing_g = 0.5f;
    spec.wing_b = 1.0f;  // Bright blue wings
    spec.body_r = 0.15f;
    spec.body_g = 0.15f;
    spec.body_b = 0.15f;
    spec.wing_beat_frequency = 5.0f;  // Slow, gliding
    spec.flight_speed = 2.0f;
    return spec;
}

ButterflySpec ButterflySpec::small_white() {
    ButterflySpec spec;
    spec.wing_span = 0.12f;
    spec.wing_height = 0.08f;
    spec.wing_r = 0.95f;
    spec.wing_g = 0.95f;
    spec.wing_b = 0.95f;  // White wings
    spec.body_r = 0.3f;
    spec.body_g = 0.3f;
    spec.body_b = 0.3f;
    spec.head_size = 0.015f;
    spec.thorax_size = 0.02f;
    spec.abdomen_size = 0.015f;
    spec.wing_beat_frequency = 12.0f;  // Fast flutter
    spec.flight_speed = 0.8f;
    return spec;
}

// ============================================================================
// HELPER: CREATE BODY SEGMENT
// ============================================================================

unsigned int ButterflyGenerator::create_body_segment(float x, float y, float z, float size,
                                                      float r, float g, float b,
                                                      kg::EntityID entity) {
    Particle p = {};
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
    p.shape = ParticleShape::BOX;

    // Spherical body segment
    p.width = size;
    p.height = size * 1.2f;  // Slightly elongated along flight direction
    p.thickness = size;
    p.size = size;
    p.reflectivity = 0.2f;

    p.material_density = 300.0f;  // Insect density
    p.CalculateMass();

    return engine_->get_particle_system().add_particle_to_entity(p, kg_, entity);
}

// ============================================================================
// HELPER: CREATE WING
// ============================================================================

unsigned int ButterflyGenerator::create_wing(float x, float y, float z,
                                             float width, float height, float thickness,
                                             float r, float g, float b,
                                             kg::EntityID entity) {
    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 0.9f;  // Slightly transparent wings
    p.shape = ParticleShape::BOX;

    // Flat wing - horizontal plane parallel to ground
    p.width = width;      // Wing width (X dimension, extends left/right)
    p.height = height;    // Wing length (Y dimension, extends front/back)
    p.thickness = thickness; // Wing thickness (Z dimension, very thin vertical)
    p.size = std::max({width, thickness, height});
    p.reflectivity = 0.6f;  // Wings are shiny

    p.material_density = 200.0f;  // Very light wing membrane
    p.CalculateMass();

    return engine_->get_particle_system().add_particle_to_entity(p, kg_, entity);
}

// ============================================================================
// MAIN GENERATION
// ============================================================================

kg::EntityID ButterflyGenerator::generate_butterfly(float world_x, float world_y, float world_z,
                                                     const ButterflySpec& spec) {
    std::cout << "[ButterflyGenerator] Generating butterfly at (" << world_x << ", "
              << world_y << ", " << world_z << ")" << std::endl;

    // Create root entity
    kg::EntityID butterfly_entity = kg_->createEntity("Butterfly");
    kg_->setProperty(butterfly_entity, "type", "Butterfly");

    // Store base position
    kg_->setProperty(butterfly_entity, "position_x", std::to_string(world_x));
    kg_->setProperty(butterfly_entity, "position_y", std::to_string(world_y));
    kg_->setProperty(butterfly_entity, "position_z", std::to_string(world_z));

    // Store flight behavior parameters (for dynamics system)
    kg_->setProperty(butterfly_entity, "wing_beat_frequency", std::to_string(spec.wing_beat_frequency));
    kg_->setProperty(butterfly_entity, "wing_beat_amplitude", std::to_string(spec.wing_beat_amplitude));
    kg_->setProperty(butterfly_entity, "flight_speed", std::to_string(spec.flight_speed));
    kg_->setProperty(butterfly_entity, "glide_probability", std::to_string(spec.glide_probability));

    std::vector<unsigned int> all_particles;

    // Body layout along Y-axis (flight direction):
    // HEAD -> THORAX segments (variable, each with wings) -> ABDOMEN

    float body_offset = 0.0f;  // Start from base position

    // HEAD - Front of butterfly
    unsigned int head = create_body_segment(
        world_x, world_y + body_offset, world_z + spec.head_size/2,
        spec.head_size,
        spec.body_r, spec.body_g, spec.body_b,
        butterfly_entity
    );
    all_particles.push_back(head);
    kg_->setProperty(butterfly_entity, "head_particle", std::to_string(head));
    body_offset += spec.head_size;

    // THORAX SEGMENTS - Variable number (1-4), each with its own wing pair
    int num_thorax = spec.num_thorax_segments;

    // Create each thorax segment with its own wing pair
    for (int i = 0; i < num_thorax; i++) {
        // Create thorax segment
        unsigned int thorax = create_body_segment(
            world_x, world_y + body_offset, world_z + spec.thorax_size/2,
            spec.thorax_size,
            spec.body_r, spec.body_g, spec.body_b,
            butterfly_entity
        );
        all_particles.push_back(thorax);
        kg_->setProperty(butterfly_entity, "thorax_" + std::to_string(i), std::to_string(thorax));

        float wing_attach_y = world_y + body_offset;
        float wing_attach_z = world_z + spec.thorax_size/2;

        // Get wing colors for this segment (from spec or fallback)
        float wing_r = spec.wing_r;  // Default fallback
        float wing_g = spec.wing_g;
        float wing_b = spec.wing_b;

        // Use per-segment colors if provided in spec
        if (i < spec.segment_wing_colors.size()) {
            wing_r = spec.segment_wing_colors[i][0];
            wing_g = spec.segment_wing_colors[i][1];
            wing_b = spec.segment_wing_colors[i][2];
        }

        // Left wing for this segment
        float left_wing_x = world_x - spec.wing_span / 2.0f;
        unsigned int left_wing = create_wing(
            left_wing_x, wing_attach_y, wing_attach_z,
            spec.wing_span / 2.0f,
            spec.wing_height,
            0.00001f,  // Ultra-thin
            wing_r, wing_g, wing_b,
            butterfly_entity
        );
        all_particles.push_back(left_wing);
        kg_->setProperty(butterfly_entity, "left_wing_" + std::to_string(i), std::to_string(left_wing));

        // Right wing for this segment
        float right_wing_x = world_x + spec.wing_span / 2.0f;
        unsigned int right_wing = create_wing(
            right_wing_x, wing_attach_y, wing_attach_z,
            spec.wing_span / 2.0f,
            spec.wing_height,
            0.00001f,  // Ultra-thin
            wing_r, wing_g, wing_b,
            butterfly_entity
        );
        all_particles.push_back(right_wing);
        kg_->setProperty(butterfly_entity, "right_wing_" + std::to_string(i), std::to_string(right_wing));

        body_offset += spec.thorax_size;
    }

    // ABDOMEN (tail) - Rear segment
    unsigned int abdomen = create_body_segment(
        world_x, world_y + body_offset, world_z + spec.abdomen_size/2,
        spec.abdomen_size,
        spec.body_r, spec.body_g, spec.body_b,
        butterfly_entity
    );
    all_particles.push_back(abdomen);
    kg_->setProperty(butterfly_entity, "abdomen_particle", std::to_string(abdomen));

    // Set primary particle references for dynamics system (use first segment)
    // Dynamics system expects single particles with these property names
    if (num_thorax > 0) {
        auto thorax_0_str = kg_->getProperty(butterfly_entity, "thorax_0");
        auto left_wing_0_str = kg_->getProperty(butterfly_entity, "left_wing_0");
        auto right_wing_0_str = kg_->getProperty(butterfly_entity, "right_wing_0");

        kg_->setProperty(butterfly_entity, "thorax_particle", thorax_0_str);
        kg_->setProperty(butterfly_entity, "left_wing_particle", left_wing_0_str);
        kg_->setProperty(butterfly_entity, "right_wing_particle", right_wing_0_str);
    }

    // Store wing span for dynamics (distance between wing attach points)
    kg_->setProperty(butterfly_entity, "wing_span", std::to_string(spec.wing_span / 2.0f));

    // === KG-driven capability bindings ===
    // Body plan: flight (binary, all wings + thorax required), perception from head
    int flight_parts = num_thorax * 2 + 1;  // wings + thorax (binary: all must be healthy)
    kg_->setProperty(butterfly_entity, "cap.flight.expected_count", std::to_string(flight_parts));
    kg_->setProperty(butterfly_entity, "cap.flight.default_mode", "binary");
    kg_->setProperty(butterfly_entity, "cap.perception.expected_count", "1");
    kg_->setProperty(butterfly_entity, "cap.perception.default_mode", "minimum");

    // Head entity: perception
    {
        kg::EntityID head_ent = kg_->createEntity("Head");
        kg_->setProperty(head_ent, "body_part_name", "head");
        kg_->setProperty(head_ent, "health", "50");
        kg_->setProperty(head_ent, "max_health", "50");
        kg_->setProperty(head_ent, "cap_list", "perception");
        kg_->setProperty(head_ent, "cap.perception.weight", "1.0");
        kg_->createRelation(butterfly_entity, "HAS_PART", head_ent);
    }

    // Thorax entity: flight control
    {
        kg::EntityID thorax_ent = kg_->createEntity("Thorax");
        kg_->setProperty(thorax_ent, "body_part_name", "thorax");
        kg_->setProperty(thorax_ent, "health", "80");
        kg_->setProperty(thorax_ent, "max_health", "80");
        kg_->setProperty(thorax_ent, "cap_list", "flight");
        kg_->setProperty(thorax_ent, "cap.flight.weight", "1.0");
        kg_->createRelation(butterfly_entity, "HAS_PART", thorax_ent);
    }

    // Wing entities: flight (binary, each must be healthy)
    for (int i = 0; i < num_thorax; i++) {
        for (const auto& side : {"left", "right"}) {
            kg::EntityID wing_ent = kg_->createEntity("Wing");
            kg_->setProperty(wing_ent, "body_part_name",
                std::string(side) + "_wing_" + std::to_string(i));
            kg_->setProperty(wing_ent, "health", "40");
            kg_->setProperty(wing_ent, "max_health", "40");
            kg_->setProperty(wing_ent, "cap_list", "flight");
            kg_->setProperty(wing_ent, "cap.flight.weight", "1.0");
            kg_->setProperty(wing_ent, "cap.flight.side", side);
            kg_->createRelation(butterfly_entity, "HAS_PART", wing_ent);
        }
    }

    std::cout << "[ButterflyGenerator] Generated butterfly entity (ID=" << butterfly_entity
              << ") with " << all_particles.size() << " particles" << std::endl;
    std::cout << "[ButterflyGenerator]   " << num_thorax << " thorax segments with "
              << (num_thorax * 2) << " wings (multi-colored)" << std::endl;

    return butterfly_entity;
}
