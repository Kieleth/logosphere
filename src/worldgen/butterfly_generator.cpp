#include "logosphere/worldgen/butterfly_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"
#include <iostream>
#include <string>

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

    p.SetMaterial(Materials::Type::FLESH);   // INV-38: named at birth (density below stays the generator's)
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

    p.SetMaterial(Materials::Type::LEAVES);   // INV-38: named at birth (density below stays the generator's)
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
    // "type" stamp removed (Malleus H1): Entity::type already carries it.

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

    // A REFUSED PART IS NOT WRITTEN INTO THE KG. add_particle answers -1 when
    // the creation door refuses a body (INV-37); stored as an unsigned it
    // becomes 4294967295, and the flight system's std::stoi on that string
    // threw std::out_of_range and took the process down one system away from
    // the defect. A butterfly missing a part keeps its entity and loses its
    // flight properties, so the dynamics system skips it and the refusal
    // line stays the only thing to read.
    constexpr unsigned int NO_PARTICLE = static_cast<unsigned int>(-1);
    bool part_refused = false;
    auto born = [&](unsigned int id) {
        if (id == NO_PARTICLE) part_refused = true;
        return id != NO_PARTICLE;
    };

    // Body layout along Y-axis (flight direction):
    // HEAD -> THORAX segments (variable, each with wings) -> ABDOMEN
    //
    // THE SEGMENTS ABUT, THEY DO NOT NEST (INV-37). create_body_segment
    // elongates each segment along Y (height = size * 1.2), and the layout
    // used to advance by `size`, so every segment was born 20% of its own
    // length inside the one in front of it. The run now advances by the
    // extent the body actually has.
    const float SEGMENT_STRETCH = 1.2f;   // create_body_segment's own Y factor
    auto y_extent = [&](float size) { return size * SEGMENT_STRETCH; };

    float body_back = 0.0f;   // Y of the back face of the last segment placed

    // HEAD - Front of butterfly
    const float head_len = y_extent(spec.head_size);
    unsigned int head = create_body_segment(
        world_x, world_y + body_back + head_len * 0.5f, world_z + spec.head_size/2,
        spec.head_size,
        spec.body_r, spec.body_g, spec.body_b,
        butterfly_entity
    );
    all_particles.push_back(head);
    if (born(head))
        kg_->setProperty(butterfly_entity, "head_particle", std::to_string(head));
    body_back += head_len;

    // THORAX SEGMENTS - Variable number (1-4), each with its own wing pair
    int num_thorax = spec.num_thorax_segments;

    // Comma-separated particle lists (same pattern as the humanoid's
    // left_arm_particles). Replaces the old per-index thorax_<i> /
    // left_wing_<i> / right_wing_<i> keys, whose unbounded names the
    // ontology could not declare and the KG gate (Malleus H1) rejects.
    std::string thorax_list, left_wing_list, right_wing_list;
    unsigned int first_thorax = 0, first_left_wing = 0, first_right_wing = 0;

    // THE WING PAIRS TILE ALONG THE BODY, THEY DO NOT SHARE ITS Y (INV-37).
    // Each segment's wings used to be centred on their own segment while
    // being wing_height long, so with more than one segment consecutive
    // wings were born through each other (20 mm on Eden's butterflies). The
    // pairs now abut, laid over the thorax run and centred on it: the
    // silhouette keeps its span and its chord, and no two wings share space.
    const float thorax_len   = y_extent(spec.thorax_size);
    const float thorax_run   = thorax_len * (float)num_thorax;
    const float wing_run     = spec.wing_height * (float)num_thorax;
    const float wing_front_y = world_y + body_back + (thorax_run - wing_run) * 0.5f;

    // Create each thorax segment with its own wing pair
    for (int i = 0; i < num_thorax; i++) {
        // Create thorax segment
        unsigned int thorax = create_body_segment(
            world_x, world_y + body_back + thorax_len * 0.5f,
            world_z + spec.thorax_size/2,
            spec.thorax_size,
            spec.body_r, spec.body_g, spec.body_b,
            butterfly_entity
        );
        all_particles.push_back(thorax);
        if (born(thorax)) {
            if (i == 0) first_thorax = thorax;
            if (!thorax_list.empty()) thorax_list += ",";
            thorax_list += std::to_string(thorax);
        }

        float wing_attach_y = wing_front_y + spec.wing_height * ((float)i + 0.5f);
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
        if (born(left_wing)) {
            if (i == 0) first_left_wing = left_wing;
            if (!left_wing_list.empty()) left_wing_list += ",";
            left_wing_list += std::to_string(left_wing);
        }

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
        if (born(right_wing)) {
            if (i == 0) first_right_wing = right_wing;
            if (!right_wing_list.empty()) right_wing_list += ",";
            right_wing_list += std::to_string(right_wing);
        }

        body_back += thorax_len;
    }

    // ABDOMEN (tail) - Rear segment
    unsigned int abdomen = create_body_segment(
        world_x, world_y + body_back + y_extent(spec.abdomen_size) * 0.5f,
        world_z + spec.abdomen_size/2,
        spec.abdomen_size,
        spec.body_r, spec.body_g, spec.body_b,
        butterfly_entity
    );
    all_particles.push_back(abdomen);
    if (born(abdomen))
        kg_->setProperty(butterfly_entity, "abdomen_particle", std::to_string(abdomen));

    // Full segment lists plus primary references (first segment) for
    // the dynamics system, which expects single particles under the
    // *_particle names.
    if (num_thorax > 0 && !part_refused) {
        kg_->setProperty(butterfly_entity, "thorax_particles", thorax_list);
        kg_->setProperty(butterfly_entity, "left_wing_particles", left_wing_list);
        kg_->setProperty(butterfly_entity, "right_wing_particles", right_wing_list);

        kg_->setProperty(butterfly_entity, "thorax_particle", std::to_string(first_thorax));
        kg_->setProperty(butterfly_entity, "left_wing_particle", std::to_string(first_left_wing));
        kg_->setProperty(butterfly_entity, "right_wing_particle", std::to_string(first_right_wing));
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
