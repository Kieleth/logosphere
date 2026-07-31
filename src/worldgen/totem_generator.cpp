#include "logosphere/worldgen/totem_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include <iostream>

// TotemSpec presets
TotemSpec TotemSpec::single_trunk() {
    TotemSpec spec;
    // Defaults are already set for single trunk
    return spec;
}

TotemSpec TotemSpec::two_trunk() {
    TotemSpec spec;
    // For Phase 2.3 implementation
    return spec;
}

TotemGenerator::TotemGenerator()
    : EntityGenerator()
{
}

TotemGenerator::~TotemGenerator() {
    // Cleanup handled by KG
}

kg::EntityID TotemGenerator::generate_totem(float world_x, float world_y, float world_z,
                                            const TotemSpec& spec) {
    if (!engine_ || !kg_) {
        std::cerr << "[TotemGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    std::cout << "[TotemGenerator] Generating totem at (" << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;

    // Create root KG entity for totem (with automatic chunk assignment)
    kg::EntityID totem_entity = kg_->createEntityAtPosition("Totem", world_x, world_y);
    kg_->setProperty(totem_entity, "z", std::to_string(world_z));
    kg_->setProperty(totem_entity, "trunk_size", std::to_string(spec.trunk_size));
    kg_->setProperty(totem_entity, "trunk_mass", std::to_string(spec.trunk_mass));

    // Phase 2.2: Create EIGHT trunk particles + horizontal top (complex stacking test)
    // Test: different masses, multiple layers, collision cascading, constraints
    const float TRUNK_GAP = 0.002f;  // 2mm gap - small enough to fall into place immediately

    // Lower trunk: bottom surface touching floor (physics-correct initial condition)
    float lower_thickness = spec.trunk_size * 0.8f;  // 2.4m vertical
    float lower_z = world_z + (lower_thickness / 2.0f);  // Center = floor + half_thickness → bottom touches floor
    std::cout << "[TOTEM BUILD] Lower trunk: thickness=" << lower_thickness
              << " center_z=" << lower_z << " (floor=" << world_z
              << " + half=" << (lower_thickness/2.0f) << ") bottom=" << (lower_z - lower_thickness/2.0f) << std::endl;
    Particle lower_trunk = create_trunk_particle(
        world_x, world_y, lower_z,
        spec.trunk_size, spec.trunk_mass,
        spec.r, spec.g, spec.b
    );

    // CRITICAL: All upper trunks use SAME base size for stable stacking!
    // Only thickness (vertical dimension) varies
    const float STACK_BASE_SIZE = 0.5f;  // All trunks 0.5m×0.5m horizontal cross-section

    // Middle trunk: gap above lower (medium mass)
    float middle_thickness = 0.64f;  // 0.64m vertical
    float lower_top = lower_z + (lower_thickness / 2.0f);
    float middle_z = lower_top + TRUNK_GAP + (middle_thickness / 2.0f);
    std::cout << "[TOTEM BUILD] Middle trunk: thickness=" << middle_thickness
              << " center_z=" << middle_z << " (lower_top=" << lower_top << " + gap=" << TRUNK_GAP
              << " + half=" << (middle_thickness/2.0f) << ")" << std::endl;
    Particle middle_trunk = create_trunk_particle(
        world_x, world_y, middle_z,
        STACK_BASE_SIZE * 2.0f,  // Creates 0.4m×0.4m×0.64m trunk
        spec.upper_trunk_mass,
        spec.r * 0.9f, spec.g * 0.9f, spec.b * 0.9f
    );
    // Explicitly set all BOX dimensions (width×height×thickness)
    middle_trunk.width = 0.4f;
    middle_trunk.height = 0.4f;
    middle_trunk.thickness = middle_thickness;  // 0.64m vertical

    // Top trunk: long vertical (uses same horizontal size as all others)
    float top_thickness = 1.8f;  // 1.8m vertical (thicker)
    float middle_top = middle_z + (middle_thickness / 2.0f);
    float top_z = middle_top + TRUNK_GAP + (top_thickness / 2.0f);
    Particle top_trunk = create_trunk_particle(
        world_x, world_y, top_z,
        STACK_BASE_SIZE * 2.0f,  // Same 0.4m×0.4m as all others
        spec.upper_trunk_mass * 0.5f,
        spec.r * 0.8f, spec.g * 0.8f, spec.b * 0.8f
    );
    // Explicitly set all BOX dimensions (width×height×thickness)
    top_trunk.width = 0.8f;   // THICKER horizontally (X dimension)
    top_trunk.height = 0.8f;  // THICKER horizontally (Y dimension)
    top_trunk.thickness = top_thickness;  // 1.8m vertical

    // 4th trunk: vertical segment
    float trunk4_thickness = 0.8f;  // 0.8m vertical
    float top_top = top_z + (top_thickness / 2.0f);
    float trunk4_z = top_top + TRUNK_GAP + (trunk4_thickness / 2.0f);
    Particle trunk4 = create_trunk_particle(
        world_x, world_y, trunk4_z,
        STACK_BASE_SIZE * 2.0f,  // Same 0.4m×0.4m
        spec.upper_trunk_mass * 0.4f,
        spec.r * 0.7f, spec.g * 0.7f, spec.b * 0.7f
    );
    // Explicitly set all BOX dimensions
    trunk4.width = 0.4f;
    trunk4.height = 0.4f;
    trunk4.thickness = trunk4_thickness;  // 0.8m vertical

    // 5th trunk: vertical segment
    float trunk5_thickness = 1.0f;  // 1.0m vertical
    float trunk4_top = trunk4_z + (trunk4_thickness / 2.0f);
    float trunk5_z = trunk4_top + TRUNK_GAP + (trunk5_thickness / 2.0f);
    Particle trunk5 = create_trunk_particle(
        world_x, world_y, trunk5_z,
        STACK_BASE_SIZE * 2.0f,  // Same 0.4m×0.4m
        spec.upper_trunk_mass * 0.5f,
        spec.r * 0.6f, spec.g * 0.6f, spec.b * 0.6f
    );
    // Explicitly set all BOX dimensions
    trunk5.width = 0.8f;   // THICKER horizontally (X dimension)
    trunk5.height = 0.8f;  // THICKER horizontally (Y dimension)
    trunk5.thickness = trunk5_thickness;  // 1.0m vertical

    // 6th trunk: vertical segment
    float trunk6_thickness = 0.7f;  // 0.7m vertical
    float trunk5_top = trunk5_z + (trunk5_thickness / 2.0f);
    float trunk6_z = trunk5_top + TRUNK_GAP + (trunk6_thickness / 2.0f);
    Particle trunk6 = create_trunk_particle(
        world_x, world_y, trunk6_z,
        STACK_BASE_SIZE * 2.0f,  // Same 0.4m×0.4m
        spec.upper_trunk_mass * 0.35f,
        spec.r * 0.5f, spec.g * 0.5f, spec.b * 0.5f
    );
    // Explicitly set all BOX dimensions
    trunk6.width = 0.4f;
    trunk6.height = 0.4f;
    trunk6.thickness = trunk6_thickness;  // 0.7m vertical

    // 7th trunk: vertical segment
    float trunk7_thickness = 0.9f;  // 0.9m vertical
    float trunk6_top = trunk6_z + (trunk6_thickness / 2.0f);
    float trunk7_z = trunk6_top + TRUNK_GAP + (trunk7_thickness / 2.0f);
    Particle trunk7 = create_trunk_particle(
        world_x, world_y, trunk7_z,
        STACK_BASE_SIZE * 2.0f,  // Same 0.4m×0.4m
        spec.upper_trunk_mass * 0.45f,
        spec.r * 0.4f, spec.g * 0.4f, spec.b * 0.4f
    );

    // Horizontal beam (viga) on top: long wooden beam
    float beam_thickness = 0.4f;  // 40cm vertical height
    float trunk7_top = trunk7_z + (trunk7_thickness / 2.0f);
    float beam_z = trunk7_top + TRUNK_GAP + (beam_thickness / 2.0f);
    Particle horizontal_beam = {};
    horizontal_beam.x = world_x;
    horizontal_beam.y = world_y;
    horizontal_beam.z = beam_z;
    horizontal_beam.vx = 0.0f;
    horizontal_beam.vy = 0.0f;
    horizontal_beam.vz = 0.0f;
    horizontal_beam.r = spec.r * 0.3f;
    horizontal_beam.g = spec.g * 0.3f;
    horizontal_beam.b = spec.b * 0.3f;
    horizontal_beam.a = 1.0f;
    horizontal_beam.shape = ParticleShape::BOX;
    horizontal_beam.width = 6.0f;   // 6m long beam (X direction)
    horizontal_beam.height = 0.5f;  // 50cm deep (Y direction)
    horizontal_beam.thickness = beam_thickness;  // 40cm tall (Z)
    horizontal_beam.size = 6.0f;
    horizontal_beam.SetMaterial(Materials::Type::WOOD_HARD);
    horizontal_beam.reflectivity = 0.3f;
    horizontal_beam.fx = 0.0f;
    horizontal_beam.fy = 0.0f;
    horizontal_beam.fz = 0.0f;

    std::cout << "[TOTEM] Horizontal beam: 6m×0.5m×0.4m centered at (" << world_x << "," << world_y << "," << beam_z << ")" << std::endl;

    // Hanging chunk: suspended from beam's end via constraint (like a nail)
    float hanging_width = 0.6f;   // 60cm wide
    float hanging_height = 0.6f;  // 60cm deep
    float hanging_thickness = 0.8f; // 80cm tall

    // Position hanging chunk at beam's right end
    // Beam is 6m wide (horizontal_beam.width), centered at world_x
    float beam_half_width = horizontal_beam.width / 2.0f;  // 3m
    float hanging_x = world_x + beam_half_width - 0.3f;  // Right end, inset 30cm from edge
    float hanging_y = world_y;

    // Vertical: Position chunk so its TOP aligns with beam's BOTTOM (surfaces touching)
    float beam_half_thickness = beam_thickness / 2.0f;  // 0.2m
    float hanging_half_thickness = hanging_thickness / 2.0f;  // 0.4m
    float beam_bottom_z = beam_z - beam_half_thickness;  // 8.554 - 0.2 = 8.354m
    float hanging_top_z = beam_bottom_z - 0.001f;  // Just below beam (1mm gap)
    float hanging_z = hanging_top_z - hanging_half_thickness;  // Center is half-thickness below top

    // Calculate attachment offsets (local space from particle centers)
    // Beam attachment: right end (2.7m right), bottom surface (0.2m down)
    float offset_a_x = beam_half_width - 0.3f;  // 2.7m right from beam center
    float offset_a_y = 0.0f;  // No Y offset
    float offset_a_z = -beam_half_thickness;  // -0.2m (bottom surface)

    // Hanging chunk attachment: top center
    float offset_b_x = 0.0f;  // Center
    float offset_b_y = 0.0f;  // Center
    float offset_b_z = hanging_half_thickness;  // +0.4m (top surface)

    // Calculate 3D distance between attachment points
    float attach_a_world_x = world_x + offset_a_x;
    float attach_a_world_y = world_y + offset_a_y;
    float attach_a_world_z = beam_z + offset_a_z;
    float attach_b_world_x = hanging_x + offset_b_x;
    float attach_b_world_y = hanging_y + offset_b_y;
    float attach_b_world_z = hanging_z + offset_b_z;

    float dx = attach_b_world_x - attach_a_world_x;
    float dy = attach_b_world_y - attach_a_world_y;
    float dz = attach_b_world_z - attach_a_world_z;
    float constraint_length = std::sqrt(dx*dx + dy*dy + dz*dz);

    Particle hanging_chunk = {};
    hanging_chunk.x = hanging_x;
    hanging_chunk.y = hanging_y;
    hanging_chunk.z = hanging_z;
    hanging_chunk.vx = 0.0f;
    hanging_chunk.vy = 0.0f;
    hanging_chunk.vz = 0.0f;
    hanging_chunk.r = spec.r * 0.5f;
    hanging_chunk.g = spec.g * 0.5f;
    hanging_chunk.b = spec.b * 0.5f;
    hanging_chunk.a = 1.0f;
    hanging_chunk.shape = ParticleShape::BOX;
    hanging_chunk.width = hanging_width;
    hanging_chunk.height = hanging_height;
    hanging_chunk.thickness = hanging_thickness;
    hanging_chunk.size = 0.8f;
    hanging_chunk.SetMaterial(Materials::Type::WOOD_HARD);
    hanging_chunk.reflectivity = 0.3f;
    hanging_chunk.fx = 0.0f;
    hanging_chunk.fy = 0.0f;
    hanging_chunk.fz = 0.0f;

    std::cout << "[TOTEM] Hanging chunk: " << hanging_width << "m×" << hanging_height << "m×" << hanging_thickness
              << "m at (" << hanging_x << "," << hanging_y << "," << hanging_z << ") - suspended by constraint" << std::endl;

    // Set entity_id for all trunks (collision filtering DISABLED for test)
    lower_trunk.entity_id = totem_entity;
    middle_trunk.entity_id = totem_entity;
    top_trunk.entity_id = totem_entity;
    trunk4.entity_id = totem_entity;
    trunk5.entity_id = totem_entity;
    trunk6.entity_id = totem_entity;
    trunk7.entity_id = totem_entity;
    horizontal_beam.entity_id = totem_entity;
    hanging_chunk.entity_id = totem_entity;

    // Verify all trunks are aligned on same axis
    std::cout << "\n[TOTEM ALIGNMENT CHECK - Target position: x=" << world_x << ", y=" << world_y << "]" << std::endl;

    float lower_bottom = lower_z - lower_thickness/2.0f;
    float lower_top_val = lower_z + lower_thickness/2.0f;
    std::cout << "  Lower:  ACTUAL x=" << lower_trunk.x << " y=" << lower_trunk.y << " z=" << lower_z
              << " | dims=" << lower_trunk.width << "×" << lower_trunk.height << "×" << lower_trunk.thickness
              << " | bottom=" << lower_bottom << " top=" << lower_top_val << std::endl;

    float middle_bottom = middle_z - middle_thickness/2.0f;
    float middle_top_val = middle_z + middle_thickness/2.0f;
    float gap_lower_middle = middle_bottom - lower_top_val;
    std::cout << "  Middle: ACTUAL x=" << middle_trunk.x << " y=" << middle_trunk.y << " z=" << middle_z
              << " | dims=" << middle_trunk.width << "×" << middle_trunk.height << "×" << middle_trunk.thickness
              << " | gap=" << gap_lower_middle << "m" << std::endl;

    float top_bottom = top_z - top_thickness/2.0f;
    float top_top_val = top_z + top_thickness/2.0f;
    float gap_middle_top = top_bottom - middle_top_val;
    std::cout << "  Top:    ACTUAL x=" << top_trunk.x << " y=" << top_trunk.y << " z=" << top_z
              << " | dims=" << top_trunk.width << "×" << top_trunk.height << "×" << top_trunk.thickness
              << " | gap=" << gap_middle_top << "m" << std::endl;

    float trunk4_bottom = trunk4_z - trunk4_thickness/2.0f;
    float trunk4_top_val = trunk4_z + trunk4_thickness/2.0f;
    float gap_top_4 = trunk4_bottom - top_top_val;
    std::cout << "  Trunk4: ACTUAL x=" << trunk4.x << " y=" << trunk4.y << " z=" << trunk4_z
              << " | dims=" << trunk4.width << "×" << trunk4.height << "×" << trunk4.thickness
              << " | gap=" << gap_top_4 << "m" << std::endl;

    float trunk5_bottom = trunk5_z - trunk5_thickness/2.0f;
    float trunk5_top_val = trunk5_z + trunk5_thickness/2.0f;
    float gap_4_5 = trunk5_bottom - trunk4_top_val;
    std::cout << "  Trunk5: ACTUAL x=" << trunk5.x << " y=" << trunk5.y << " z=" << trunk5_z
              << " | dims=" << trunk5.width << "×" << trunk5.height << "×" << trunk5.thickness
              << " | gap=" << gap_4_5 << "m" << std::endl;

    float trunk6_bottom = trunk6_z - trunk6_thickness/2.0f;
    float trunk6_top_val = trunk6_z + trunk6_thickness/2.0f;
    float gap_5_6 = trunk6_bottom - trunk5_top_val;
    std::cout << "  Trunk6: ACTUAL x=" << trunk6.x << " y=" << trunk6.y << " z=" << trunk6_z
              << " | dims=" << trunk6.width << "×" << trunk6.height << "×" << trunk6.thickness
              << " | gap=" << gap_5_6 << "m" << std::endl;

    float trunk7_bottom = trunk7_z - trunk7_thickness/2.0f;
    float trunk7_top_val = trunk7_z + trunk7_thickness/2.0f;
    float gap_6_7 = trunk7_bottom - trunk6_top_val;
    std::cout << "  Trunk7: ACTUAL x=" << trunk7.x << " y=" << trunk7.y << " z=" << trunk7_z
              << " | dims=" << trunk7.width << "×" << trunk7.height << "×" << trunk7.thickness
              << " | gap=" << gap_6_7 << "m" << std::endl;

    float beam_bottom = beam_z - beam_thickness/2.0f;
    float beam_top_val = beam_z + beam_thickness/2.0f;
    float gap_7_beam = beam_bottom - trunk7_top_val;
    std::cout << "  Beam:   ACTUAL x=" << horizontal_beam.x << " y=" << horizontal_beam.y << " z=" << beam_z
              << " | dims=" << horizontal_beam.width << "×" << horizontal_beam.height << "×" << horizontal_beam.thickness
              << " | gap=" << gap_7_beam << "m" << std::endl;
    std::cout << std::endl;

    // TEST: Store lower + middle + top + trunk4 + trunk5 + trunk6 particles - verify six-particle stacking
    std::cout << "[TOTEM TEST] Creating 6-trunk totem (lower + middle + top + trunk4 + trunk5 + trunk6)" << std::endl;
    kg::KGParticleID lower_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(lower_kg_id, lower_trunk);

    kg::KGParticleID middle_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(middle_kg_id, middle_trunk);

    kg::KGParticleID top_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(top_kg_id, top_trunk);

    kg::KGParticleID trunk4_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(trunk4_kg_id, trunk4);

    kg::KGParticleID trunk5_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(trunk5_kg_id, trunk5);

    kg::KGParticleID trunk6_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(trunk6_kg_id, trunk6);

    kg::KGParticleID trunk7_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(trunk7_kg_id, trunk7);

    kg::KGParticleID horizontal_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(horizontal_kg_id, horizontal_beam);

    kg::KGParticleID hanging_kg_id = kg_->createKGParticle(totem_entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(hanging_kg_id, hanging_chunk);

    // NOTE: getRenderIndex() won't work here - particles are queued, not immediately added
    // PhysicsSystem must reload constraints AFTER particles are processed
    // See physics_system.cpp - it loads constraints twice (init + after world gen)

    // Create constraint entity linking beam and hanging chunk (simulates nail connection)
    kg::EntityID constraint_entity = kg_->createEntity("Constraint");
    kg_->setProperty(constraint_entity, "entity_id", std::to_string(totem_entity));
    kg_->setProperty(constraint_entity, "particle_a", std::to_string(horizontal_kg_id));
    kg_->setProperty(constraint_entity, "particle_b", std::to_string(hanging_kg_id));
    kg_->setProperty(constraint_entity, "rest_distance", std::to_string(constraint_length));
    // Physics-based stiffness: 100,000 N/m (oak wood beam range: 100k-500k N/m)
    // Lower end allows slight flex, realistic for wooden totem structure
    kg_->setProperty(constraint_entity, "base_stiffness", "100000.0");

    // Surface attachment offsets (local space from particle centers)
    kg_->setProperty(constraint_entity, "offset_a_x", std::to_string(offset_a_x));
    kg_->setProperty(constraint_entity, "offset_a_y", std::to_string(offset_a_y));
    kg_->setProperty(constraint_entity, "offset_a_z", std::to_string(offset_a_z));
    kg_->setProperty(constraint_entity, "offset_b_x", std::to_string(offset_b_x));
    kg_->setProperty(constraint_entity, "offset_b_y", std::to_string(offset_b_y));
    kg_->setProperty(constraint_entity, "offset_b_z", std::to_string(offset_b_z));

    kg_->createRelation(totem_entity, "HAS_CONSTRAINT", constraint_entity);

    std::cout << "[TOTEM] Created constraint between beam (KG " << horizontal_kg_id << ") and hanging chunk (KG " << hanging_kg_id << ")" << std::endl;
    std::cout << "  Beam attachment: offset=(" << offset_a_x << ", " << offset_a_y << ", " << offset_a_z << ")" << std::endl;
    std::cout << "  Chunk attachment: offset=(" << offset_b_x << ", " << offset_b_y << ", " << offset_b_z << ")" << std::endl;
    std::cout << "  rest_distance=" << constraint_length << "m stiffness=100000 N/m" << std::endl;

    // Track totem using base class
    on_entity_created(totem_entity);

    std::cout << "[TotemGenerator] TEST: Six-trunk totem created with entity ID " << totem_entity << std::endl;
    std::cout << "  POSITION: world_x=" << world_x << " world_y=" << world_y << " floor_z=" << world_z << std::endl;
    std::cout << "  Lower trunk: center_z=" << lower_z << " bottom=" << (lower_z - lower_thickness/2.0f) << " top=" << (lower_z + lower_thickness/2.0f) << std::endl;
    std::cout << "  Middle trunk: center_z=" << middle_z << " bottom=" << (middle_z - middle_thickness/2.0f) << " top=" << (middle_z + middle_thickness/2.0f) << std::endl;
    std::cout << "  Top trunk: center_z=" << top_z << " bottom=" << (top_z - top_thickness/2.0f) << " top=" << (top_z + top_thickness/2.0f) << std::endl;
    std::cout << "  Trunk4: center_z=" << trunk4_z << " bottom=" << (trunk4_z - trunk4_thickness/2.0f) << " top=" << (trunk4_z + trunk4_thickness/2.0f) << std::endl;
    std::cout << "  Trunk5: center_z=" << trunk5_z << " bottom=" << (trunk5_z - trunk5_thickness/2.0f) << " top=" << (trunk5_z + trunk5_thickness/2.0f) << std::endl;
    std::cout << "  Trunk6: center_z=" << trunk6_z << " bottom=" << (trunk6_z - trunk6_thickness/2.0f) << " top=" << (trunk6_z + trunk6_thickness/2.0f) << std::endl;

    // TODO(PHYSICS_v2): Re-implement collision filtering after physics rebuild
    // Set collision mode to ALLOW_SAME_ENTITY (for later when we add more trunks)
    // engine_->get_physics_system().set_entity_collision_mode(totem_entity, CollisionMode::ALLOW_SAME_ENTITY);

    // Create constraints between stacked trunk segments for structural integrity
    create_constraint(totem_entity, lower_kg_id, middle_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, middle_kg_id, top_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, top_kg_id, trunk4_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, trunk4_kg_id, trunk5_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, trunk5_kg_id, trunk6_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, trunk6_kg_id, trunk7_kg_id, spec.constraint_stiffness);
    create_constraint(totem_entity, trunk7_kg_id, horizontal_kg_id, spec.constraint_stiffness);

    // Queue for activation on main thread (thread-safe)
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(totem_entity);

    return totem_entity;
}

Particle TotemGenerator::create_trunk_particle(float x, float y, float z,
                                               float size, float mass,
                                               float r, float g, float b) {
    Particle trunk = {};
    trunk.x = x;
    trunk.y = y;
    trunk.z = z;
    trunk.vx = 0.0f;
    trunk.vy = 0.0f;
    trunk.vz = 0.0f;
    trunk.r = r;
    trunk.g = g;
    trunk.b = b;
    trunk.a = 1.0f;

    // BOX shape: vertical trunk (narrow width/depth, moderate height)
    trunk.shape = ParticleShape::BOX;
    trunk.width = size * 0.4f;      // X dimension (narrow trunk)
    trunk.height = size * 0.4f;     // Y dimension (narrow trunk)
    trunk.thickness = size * 0.8f;  // Z dimension (moderately tall vertical trunk)
    trunk.size = size;              // Keep for backward compatibility
    trunk.SetMaterial(Materials::Type::WOOD_HARD);
    trunk.reflectivity = 0.3f;      // Wood reflects some light
    trunk.pattern_id = 1;           // PATTERN_WOOD

    // Physics properties (Phase 1 already implemented)
    trunk.fx = 0.0f;
    trunk.fy = 0.0f;
    trunk.fz = 0.0f;

    std::cout << "[TOTEM] Created trunk particle: mass=" << trunk.GetMass()
              << " dimensions=" << trunk.width << "×" << trunk.height << "×" << trunk.thickness
              << " GetMass()=" << trunk.GetMass() << std::endl;

    return trunk;
}
