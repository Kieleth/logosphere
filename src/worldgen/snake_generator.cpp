#include "logosphere/worldgen/snake_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"
#include <cmath>
#include <iostream>
#include <string>

SnakeGenerator::SnakeGenerator()
    : engine_(nullptr)
    , kg_(nullptr) {
}

SnakeGenerator::~SnakeGenerator() {
}

void SnakeGenerator::initialize(Engine* engine, kg::KGModule* kg) {
    engine_ = engine;
    kg_ = kg;
}

// ============================================================================
// SNAKE SPEC PRESETS
// ============================================================================

SnakeSpec SnakeSpec::garden_snake() {
    SnakeSpec spec;
    spec.total_length = 1.5f;       // 1.5m long
    spec.num_segments = 15;
    spec.head_size = 0.06f;
    spec.body_thickness = 0.04f;
    spec.scale_r = 0.2f;
    spec.scale_g = 0.6f;
    spec.scale_b = 0.2f;
    spec.head_r = 0.8f;
    spec.head_g = 0.6f;
    spec.head_b = 0.2f;
    return spec;
}

SnakeSpec SnakeSpec::python() {
    SnakeSpec spec;
    spec.total_length = 4.0f;       // 4m long (large constrictor)
    spec.num_segments = 30;
    spec.head_size = 0.15f;
    spec.body_thickness = 0.12f;
    spec.taper_factor = 0.7f;
    spec.scale_r = 0.4f;
    spec.scale_g = 0.3f;
    spec.scale_b = 0.2f;            // Brown scales
    spec.head_r = 0.5f;
    spec.head_g = 0.4f;
    spec.head_b = 0.3f;
    return spec;
}

SnakeSpec SnakeSpec::coral_snake() {
    SnakeSpec spec;
    spec.total_length = 1.0f;       // 1m long (small)
    spec.num_segments = 12;
    spec.head_size = 0.05f;
    spec.body_thickness = 0.03f;
    spec.scale_r = 0.9f;
    spec.scale_g = 0.1f;
    spec.scale_b = 0.1f;            // Red scales (banding done via segments)
    spec.head_r = 0.0f;
    spec.head_g = 0.0f;
    spec.head_b = 0.0f;             // Black head
    return spec;
}

// ============================================================================
// HELPER: CREATE SEGMENT PARTICLE
// ============================================================================

unsigned int SnakeGenerator::create_segment(float x, float y, float z,
                                             float width, float depth, float height,
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
    p.width = width;          // X dimension
    p.height = depth;         // Y dimension (length along body)
    p.thickness = height;     // Z dimension (vertical)
    p.size = std::max({width, depth, height});
    p.reflectivity = 0.3f;    // Scales have some shine

    // Material properties (applied to all snake segments)
    // Material densities should be stored in KG per material type (not hardcoded)
    // but for now we stub with reptile scales density
    p.SetMaterial(Materials::Type::FLESH);   // INV-38: named at birth (density below stays the generator's)
    p.material_density = 1100.0f;  // Reptile scales ≈ 1100 kg/m³
    p.CalculateMass();             // Calculate mass = volume × density

    return engine_->get_particle_system().add_particle_to_entity(p, kg_, entity);
}

// ============================================================================
// HELPER: CREATE HEAD PARTICLE
// ============================================================================

unsigned int SnakeGenerator::create_head(float x, float y, float z, const SnakeSpec& spec,
                                          kg::EntityID entity) {
    // Head is slightly larger and distinct color
    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;
    p.r = spec.head_r;
    p.g = spec.head_g;
    p.b = spec.head_b;
    p.a = 1.0f;
    p.shape = ParticleShape::BOX;

    // Head is slightly larger than body segments
    float head_width = spec.head_size;
    float head_depth = spec.head_size * 1.2f;  // Slightly elongated
    float head_height = spec.head_size * 0.8f; // Flatter

    p.width = head_width;
    p.height = head_depth;
    p.thickness = head_height;
    p.size = std::max({head_width, head_depth, head_height});
    p.reflectivity = 0.3f;

    // Material properties
    p.SetMaterial(Materials::Type::FLESH);   // INV-38: named at birth (density below stays the generator's)
    p.material_density = 1100.0f;  // Same as body
    p.CalculateMass();

    return engine_->get_particle_system().add_particle_to_entity(p, kg_, entity);
}

// ============================================================================
// MAIN GENERATION
// ============================================================================

kg::EntityID SnakeGenerator::generate_snake(float world_x, float world_y, float world_z, const SnakeSpec& spec) {
    std::cout << "[SnakeGenerator] Generating snake at (" << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;
    std::cout << "[SnakeGenerator] Segments: " << spec.num_segments << ", Length: " << spec.total_length << "m" << std::endl;

    // Create root entity
    kg::EntityID snake_entity = kg_->createEntity("Snake");
    // "type" stamp removed (Malleus H1): Entity::type already carries it.
    kg_->setProperty(snake_entity, "length", std::to_string(spec.total_length));
    kg_->setProperty(snake_entity, "num_segments", std::to_string(spec.num_segments));

    // Store base position in KG
    kg_->setProperty(snake_entity, "position_x", std::to_string(world_x));
    kg_->setProperty(snake_entity, "position_y", std::to_string(world_y));
    kg_->setProperty(snake_entity, "position_z", std::to_string(world_z));

    // Calculate segment dimensions
    // Snake lays along X-axis initially (can be rotated by dynamics)
    float segment_length = spec.total_length / (spec.num_segments + 2);  // +2 for head and tail space
    float segment_height = spec.body_thickness * 0.7f;  // Flatter (snake on ground)

    std::vector<unsigned int> all_particles;
    std::vector<unsigned int> body_segments;

    // HEAD - At start position
    float head_x = world_x;
    float head_y = world_y;
    // The head's Z extent is head_size * 0.8 (see create_head), not the body's
    // segment_height. Deriving its z from the BODY buried every preset's head:
    // python -0.018, garden_snake -0.010.
    float head_z = world_z + (spec.head_size * 0.8f) / 2.0f;  // Bottom touches ground

    unsigned int head_particle = create_head(head_x, head_y, head_z, spec, snake_entity);
    all_particles.push_back(head_particle);
    kg_->setProperty(snake_entity, "head_particle", std::to_string(head_particle));

    std::cout << "[SnakeGenerator] Created head at (" << head_x << ", " << head_y << ", " << head_z << ")" << std::endl;

    // BODY SEGMENTS - Along X-axis
    //
    // A SEGMENT'S LENGTH LIES ALONG THE BODY, AND THE SEGMENTS ABUT.
    // The layout stepped by segment_length in X while each segment's X
    // extent was its GIRTH (0.118 m on a 0.1 m step), so every consecutive
    // pair was born 18 mm inside the one before it, and the first segment
    // 35 mm inside the head. Under INV-37 those are refused births, and the
    // snake lost 9 of its 30 segments. The girth now lies across the body
    // (Y) where it belongs and the length along it (X), so the step and the
    // extent are the same number and consecutive segments touch.
    float current_x = head_x + (spec.head_size * 0.5f) + segment_length * 0.5f;

    for (int i = 0; i < spec.num_segments; i++) {
        // Taper from body thickness to tail thickness
        float taper_progress = static_cast<float>(i) / spec.num_segments;
        float current_thickness = spec.body_thickness +
                                  (spec.body_thickness * spec.taper_factor - spec.body_thickness) * taper_progress;

        float seg_girth = current_thickness;
        float seg_height = current_thickness * 0.7f;
        float seg_z = world_z + seg_height / 2.0f;

        unsigned int segment = create_segment(
            current_x, world_y, seg_z,
            segment_length, seg_girth, seg_height,
            spec.scale_r, spec.scale_g, spec.scale_b,
            snake_entity
        );

        all_particles.push_back(segment);
        body_segments.push_back(segment);

        current_x += segment_length;  // Move to next segment position
    }

    std::cout << "[SnakeGenerator] Created " << body_segments.size() << " body segments" << std::endl;

    // TAIL - Small tapered end
    float tail_size = spec.body_thickness * spec.taper_factor * 0.5f;  // Half of final taper
    // current_x is the centre of the slot AFTER the last body segment; the
    // tail is half as long, so its own centre sits a quarter-length in.
    float tail_x = current_x - segment_length * 0.25f;
    float tail_z = world_z + tail_size / 2.0f;

    unsigned int tail_particle = create_segment(
        tail_x, world_y, tail_z,
        segment_length * 0.5f, tail_size, tail_size,
        spec.scale_r * 0.8f, spec.scale_g * 0.8f, spec.scale_b * 0.8f,  // Slightly darker
        snake_entity
    );
    all_particles.push_back(tail_particle);
    kg_->setProperty(snake_entity, "tail_particle", std::to_string(tail_particle));

    std::cout << "[SnakeGenerator] Created tail at (" << tail_x << ", " << world_y << ", " << tail_z << ")" << std::endl;

    // Create body segments child entity
    // "body" is not an ontology type; Segment is. This child
    // entity was silently never created.
    kg_->createChildEntityWithParticles(snake_entity, "Segment",
                                        body_segments);

    // Store segment spacing for dynamics system
    kg_->setProperty(snake_entity, "segment_length", std::to_string(segment_length));
    kg_->setProperty(snake_entity, "segment_spacing", std::to_string(segment_length));

    // Store all segment IDs as comma-separated string for dynamics
    std::string segments_str;
    for (size_t i = 0; i < body_segments.size(); i++) {
        segments_str += std::to_string(body_segments[i]);
        if (i < body_segments.size() - 1) segments_str += ",";
    }
    kg_->setProperty(snake_entity, "body_segments", segments_str);

    // === KG-driven capability bindings ===
    // Body plan: undulation from all segments, perception from head
    int total_parts = spec.num_segments + 2;  // head + body + tail
    kg_->setProperty(snake_entity, "cap.undulation.expected_count", std::to_string(total_parts));
    kg_->setProperty(snake_entity, "cap.undulation.default_mode", "weighted_sum");
    kg_->setProperty(snake_entity, "cap.undulation.normalize", "1.0");
    kg_->setProperty(snake_entity, "cap.perception.expected_count", "1");
    kg_->setProperty(snake_entity, "cap.perception.default_mode", "minimum");

    // Head segment entity: high undulation weight + perception
    {
        kg::EntityID head_seg = kg_->createEntity("Segment");
        kg_->setProperty(head_seg, "body_part_name", "head");
        kg_->setProperty(head_seg, "health", "80");
        kg_->setProperty(head_seg, "max_health", "80");
        kg_->setProperty(head_seg, "cap_list", "undulation,perception");
        float head_weight = 2.0f / total_parts;  // head matters more
        kg_->setProperty(head_seg, "cap.undulation.weight", std::to_string(head_weight));
        kg_->setProperty(head_seg, "cap.perception.weight", "1.0");
        kg_->createRelation(snake_entity, "HAS_PART", head_seg);
    }

    // Body segment entities: distributed undulation weight
    float body_weight_each = 0.8f / std::max(1, spec.num_segments);  // 80% of total across body
    for (int i = 0; i < spec.num_segments; i++) {
        kg::EntityID seg = kg_->createEntity("Segment");
        kg_->setProperty(seg, "body_part_name", "segment_" + std::to_string(i));
        kg_->setProperty(seg, "health", "100");
        kg_->setProperty(seg, "max_health", "100");
        kg_->setProperty(seg, "cap_list", "undulation");
        kg_->setProperty(seg, "cap.undulation.weight", std::to_string(body_weight_each));
        kg_->createRelation(snake_entity, "HAS_PART", seg);
    }

    // Tail segment entity: low undulation weight
    {
        kg::EntityID tail_seg = kg_->createEntity("Segment");
        kg_->setProperty(tail_seg, "body_part_name", "tail");
        kg_->setProperty(tail_seg, "health", "60");
        kg_->setProperty(tail_seg, "max_health", "60");
        kg_->setProperty(tail_seg, "cap_list", "undulation");
        float tail_weight = 0.5f / total_parts;  // tail matters least
        kg_->setProperty(tail_seg, "cap.undulation.weight", std::to_string(tail_weight));
        kg_->createRelation(snake_entity, "HAS_PART", tail_seg);
    }

    std::cout << "[SnakeGenerator] Generated snake entity (ID=" << snake_entity << ") with "
              << all_particles.size() << " total particles" << std::endl;

    return snake_entity;
}
