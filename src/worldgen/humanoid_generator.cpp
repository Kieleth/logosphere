#include "logosphere/worldgen/humanoid_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "core/joint_types.h"  // Anatomical joint definitions
#include "entity_manager.h"
#include "particle.h"
#include "logosphere/kg/kg_module.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <ctime>
#include <cstdlib>
#include <cassert>
#include <string>

HumanoidGenerator::HumanoidGenerator()
    : rng_state_(static_cast<unsigned int>(time(nullptr))) {
}

HumanoidGenerator::~HumanoidGenerator() {
}

// ============================================================================
// HUMANOID SPEC PRESETS
// ============================================================================

HumanoidSpec HumanoidSpec::default_human() {
    HumanoidSpec spec;
    spec.total_height = 1.8f;
    spec.torso_height = 0.6f;
    spec.torso_width = 0.4f;
    spec.torso_depth = 0.25f;
    spec.head_size = 0.15f;
    spec.arm_length = 0.6f;
    spec.arm_thickness = 0.08f;
    spec.hip_width = 0.35f;
    spec.hip_height = 0.15f;
    spec.leg_length = 0.9f;
    spec.leg_thickness = 0.12f;

    // Neutral skin tone
    spec.skin_r = 0.8f;
    spec.skin_g = 0.6f;
    spec.skin_b = 0.5f;

    // Blue clothing
    spec.clothing_r = 0.3f;
    spec.clothing_g = 0.3f;
    spec.clothing_b = 0.7f;

    // Eyes: white sclera + random iris color
    spec.has_eyes = true;
    spec.eye_outer_r = 0.95f; spec.eye_outer_g = 0.95f; spec.eye_outer_b = 0.95f;  // White sclera
    // Random iris: pick a hue by boosting one channel
    float iris_r = 0.15f + (rand() % 100) / 200.0f;  // 0.15-0.65
    float iris_g = 0.15f + (rand() % 100) / 200.0f;
    float iris_b = 0.15f + (rand() % 100) / 200.0f;
    // Boost one channel for a distinct color
    int dominant = rand() % 3;
    if (dominant == 0) iris_r = 0.5f + (rand() % 100) / 200.0f;       // Brown/amber
    else if (dominant == 1) iris_g = 0.5f + (rand() % 100) / 200.0f;  // Green
    else iris_b = 0.5f + (rand() % 100) / 200.0f;                     // Blue
    spec.eye_inner_r = iris_r; spec.eye_inner_g = iris_g; spec.eye_inner_b = iris_b;

    return spec;
}

HumanoidSpec HumanoidSpec::eva() {
    HumanoidSpec spec = default_human();

    // Eva: slender but physically stable proportions
    spec.total_height = 1.7f;
    spec.head_size = 0.13f;
    spec.torso_width = 0.25f;
    spec.torso_depth = 0.16f;
    spec.torso_height = 0.50f;
    spec.hip_width = 0.22f;
    spec.hip_height = 0.12f;
    spec.arm_thickness = 0.06f;
    spec.leg_thickness = 0.08f;

    // Peach/tan skin tone
    spec.skin_r = 0.9f;
    spec.skin_g = 0.7f;
    spec.skin_b = 0.6f;

    // Purple/violet clothing
    spec.clothing_r = 0.6f;
    spec.clothing_g = 0.3f;
    spec.clothing_b = 0.7f;

    // Eva wears silk clothing (6) - flowing, elegant
    spec.torso_pattern = 6;   // PATTERN_SILK
    spec.legs_pattern = 6;    // PATTERN_SILK (dress/skirt)

    return spec;
}

HumanoidSpec HumanoidSpec::hunter() {
    HumanoidSpec spec = default_human();

    // Hunter: Male villager, stockier build, outdoor lifestyle
    spec.total_height = 1.85f;         // Slightly taller than default
    spec.head_size = 0.14f;            // Normal head
    spec.torso_width = 0.45f;          // Broader shoulders
    spec.torso_depth = 0.28f;          // Stockier build
    spec.torso_height = 0.65f;         // Slightly longer torso
    spec.hip_width = 0.38f;            // Wider hips
    spec.hip_height = 0.16f;           // Normal hip section
    spec.arm_thickness = 0.10f;        // Thicker arms (strong)
    spec.leg_thickness = 0.14f;        // Thicker legs (runner)
    spec.arm_length = 0.65f;           // Longer arms
    spec.leg_length = 0.95f;           // Longer legs

    // Tanned/weathered skin tone (outdoor lifestyle)
    spec.skin_r = 0.72f;
    spec.skin_g = 0.55f;
    spec.skin_b = 0.42f;

    // Earth-toned clothing (forest camouflage - brown/green)
    spec.clothing_r = 0.35f;
    spec.clothing_g = 0.30f;
    spec.clothing_b = 0.20f;

    // Hunter wears leather (3) - rugged, outdoor
    spec.torso_pattern = 3;   // PATTERN_LEATHER (leather vest/jacket)
    spec.legs_pattern = 3;    // PATTERN_LEATHER (leather pants)
    spec.feet_pattern = 3;    // PATTERN_LEATHER (boots)

    return spec;
}

HumanoidSpec HumanoidSpec::tall_human() {
    HumanoidSpec spec = default_human();
    spec.total_height = 2.0f;          // Tall
    spec.leg_length = 1.0f;            // Longer legs
    spec.torso_height = 0.7f;          // Taller torso
    return spec;
}

HumanoidSpec HumanoidSpec::short_human() {
    HumanoidSpec spec = default_human();
    spec.total_height = 1.5f;          // Short
    spec.leg_length = 0.7f;            // Shorter legs
    spec.torso_height = 0.5f;          // Shorter torso
    return spec;
}

void HumanoidSpec::apply_natural_variation(float variance_amount, unsigned int seed) {
    // Simple LCG for reproducible randomness
    auto next_random = [&seed]() -> float {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return (seed % 1000) / 1000.0f;
    };

    // Bell-curve distribution using x³
    auto vary = [&](float base, float max_variance) -> float {
        float random = next_random();
        float normalized = (random * 2.0f - 1.0f);
        float curved = normalized * normalized * normalized;
        return base + base * max_variance * variance_amount * curved;
    };

    total_height = vary(total_height, 0.10f);        // ±10%
    torso_width = vary(torso_width, 0.15f);          // ±15%
    arm_length = vary(arm_length, 0.10f);            // ±10%
    leg_length = vary(leg_length, 0.10f);            // ±10%
    arm_thickness = vary(arm_thickness, 0.20f);      // ±20%
    leg_thickness = vary(leg_thickness, 0.20f);      // ±20%
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

void HumanoidGenerator::seed_rng(unsigned int seed) {
    rng_state_ = seed;
}

float HumanoidGenerator::random_variance(float base, float variance) {
    // Simple Linear Congruential Generator (LCG)
    rng_state_ = (rng_state_ * 1103515245 + 12345) & 0x7fffffff;
    float random_01 = (rng_state_ % 1000) / 1000.0f;

    // Map to [-variance, +variance]
    float offset = (random_01 * 2.0f - 1.0f) * variance;
    return base + offset;
}

// ============================================================================
// HELPER: CREATE BODY PARTICLE
// ============================================================================

kg::KGParticleID HumanoidGenerator::create_body_particle(
    float x, float y, float z,
    float width, float depth, float height,
    float r, float g, float b,
    kg::EntityID entity_id,
    uint8_t pattern_id) {

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
    p.shape = ParticleShape::BOX;  // BOX uses width/height/thickness, CUBE only uses size
    p.width = width;          // X dimension
    p.height = depth;         // Y dimension (front-to-back)
    p.thickness = height;     // Z dimension (vertical)
    p.size = std::max({width, depth, height});  // For legacy compatibility
    p.reflectivity = 0.6f;
    p.entity_id = entity_id;  // CRITICAL: Set entity ID for collision filtering
    p.pattern_id = pattern_id;  // Procedural texture pattern

    // TODO[DYNAMICS-001]: Material densities should be stored in KG per material type
    // Architecture: KG stores base material properties with relationships:
    //   - kg_->createEntity("material_flesh") with density property
    //   - Entities reference materials, can apply "tilt" for variation
    //   - Centralized material database, not hardcoded per-entity
    // Current: Using Materials system for standardized density
    p.SetMaterial(Materials::Type::FLESH);  // Human flesh ≈ 1000 kg/m³
    p.CalculateMass();                       // Calculate mass = volume × density

    // Emit living flesh scent - undead can smell this!
    p.odor_type = OdorType::LIVING_FLESH;
    p.odor_radius = 15.0f;  // Scent carries about 15m
    p.odor_intensity = 1.0f;  // Full strength smell

    // ASSERTION: Body parts must use BOX shape for non-uniform dimensions
    assert(p.shape == ParticleShape::BOX &&
           "Humanoid body parts require BOX shape for anatomical dimensions");

    // Calculate bounds for debug output
    float x_min = x - width / 2.0f;
    float x_max = x + width / 2.0f;
    float y_min = y - depth / 2.0f;
    float y_max = y + depth / 2.0f;
    float z_min = z - height / 2.0f;
    float z_max = z + height / 2.0f;

    // CHUNK-BASED: Store particle data in KG (not ParticleSystem yet)
    // Chunk system will activate later when entity is in view
    kg::KGParticleID kg_id = kg_->createKGParticle(entity_id, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(kg_id, p);

    // DEBUG OUTPUT
    std::cout << "[EVA_PARTICLE_DEBUG] KG_ID=" << kg_id << std::endl;
    std::cout << "  Position: (" << x << ", " << y << ", " << z << ")" << std::endl;
    std::cout << "  Input: W=" << width << " × D=" << depth << " × H=" << height << std::endl;
    std::cout << "  Particle fields: p.width=" << p.width << " p.height=" << p.height << " p.thickness=" << p.thickness << std::endl;
    std::cout << "  X-bounds: [" << x_min << " to " << x_max << "]" << std::endl;
    std::cout << "  Y-bounds: [" << y_min << " to " << y_max << "]" << std::endl;
    std::cout << "  Z-bounds: [" << z_min << " to " << z_max << "]" << std::endl;
    std::cout << "  Color: RGB(" << r << ", " << g << ", " << b << ")" << std::endl;

    return kg_id;
}

// ============================================================================
// BODY PART GENERATORS
// ============================================================================

kg::EntityID HumanoidGenerator::create_hips(float x, float y, float z, const HumanoidSpec& spec) {
    // EVA EXPECTED: W=0.130 × D=0.080 × H=0.100, Z-bounds: [0.924 to 1.024]

    // Calculate actual leg top (accounting for foot height)
    float foot_height = spec.leg_thickness * 0.6f;  // Eva: 0.024
    float leg_top_z = z + foot_height + spec.leg_length;  // 0.000 + 0.024 + 0.900 = 0.924

    // Hips sit on top of legs
    float hip_z = leg_top_z + spec.hip_height / 2.0f;  // 0.924 + 0.05 = 0.974

    std::cout << "[HIPS] Creating at z=" << hip_z << " (leg_top=" << leg_top_z << ")" << std::endl;

    // Create entity FIRST so we have the ID for particle creation
    kg::EntityID hip_entity = kg_->createEntity("Hips");
    kg_->setProperty(hip_entity, "body_part", "Hips");

    // CHUNK-BASED: create_body_particle() now stores in KG internally
    kg::KGParticleID hip_kg_id = create_body_particle(
        x, y, hip_z,
        spec.hip_width, spec.torso_depth, spec.hip_height,
        spec.clothing_r, spec.clothing_g, spec.clothing_b,
        hip_entity,  // Pass entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );

    return hip_entity;
}

kg::EntityID HumanoidGenerator::create_torso(float x, float y, float z, const HumanoidSpec& spec) {
    // EVA EXPECTED: W=0.060 × D=0.040 × H=0.500, Z-bounds: [1.024 to 1.524]

    // Calculate where torso sits (on top of hips, which sit on top of legs)
    float foot_height = spec.leg_thickness * 0.6f;  // Eva: 0.024
    float leg_top_z = z + foot_height + spec.leg_length;  // 0.924
    float hip_top_z = leg_top_z + spec.hip_height;  // 0.924 + 0.100 = 1.024

    // Torso sits on top of hips
    float torso_z = hip_top_z + spec.torso_height / 2.0f;  // 1.024 + 0.250 = 1.274

    std::cout << "[TORSO] Creating at z=" << torso_z << " (hip_top=" << hip_top_z << ")" << std::endl;

    // Create entity FIRST so we have the ID for particle creation
    kg::EntityID torso_entity = kg_->createEntity("Torso");
    kg_->setProperty(torso_entity, "body_part", "Torso");

    unsigned int torso_particle = create_body_particle(
        x, y, torso_z,
        spec.torso_width * 0.5f, spec.torso_depth * 0.5f, spec.torso_height,  // Much thinner!
        0.2f, 0.9f, 0.3f,  // Green color
        torso_entity,  // Pass entity ID for collision filtering
        spec.torso_pattern  // Procedural pattern (shirt/clothing)
    );

    return torso_entity;
}

kg::EntityID HumanoidGenerator::create_head(float x, float y, float z, const HumanoidSpec& spec) {
    // EVA EXPECTED: W=0.200 × D=0.200 × H=0.200, Z-bounds: [1.524 to 1.724]

    // Calculate where head sits (on top of torso)
    float foot_height = spec.leg_thickness * 0.6f;  // 0.024
    float leg_top_z = z + foot_height + spec.leg_length;  // 0.924
    float hip_top_z = leg_top_z + spec.hip_height;  // 1.024
    float torso_top_z = hip_top_z + spec.torso_height;  // 1.024 + 0.500 = 1.524

    // Head sits on top of torso (head_size is radius, particle height is 2*radius)
    float head_z = torso_top_z + spec.head_size;  // 1.524 + 0.100 = 1.624

    std::cout << "[HEAD] Creating at z=" << head_z << " (torso_top=" << torso_top_z << ")" << std::endl;

    // Create entity FIRST so we have the ID for particle creation
    kg::EntityID head_entity = kg_->createEntity("Head");
    kg_->setProperty(head_entity, "body_part", "Head");

    unsigned int head_particle = create_body_particle(
        x, y, head_z,
        spec.head_size * 2.0f, spec.head_size * 2.0f, spec.head_size * 2.0f,
        spec.skin_r, spec.skin_g, spec.skin_b,
        head_entity,  // Pass entity ID for collision filtering
        spec.head_pattern  // Procedural pattern (skin)
    );

    return head_entity;
}

kg::EntityID HumanoidGenerator::create_arm(float x, float y, float z, bool is_left, const HumanoidSpec& spec) {
    // EVA EXPECTED PER ARM:
    // Upper Arm: W=0.030 × D=0.030 × H=0.330, Z-bounds: [1.194 to 1.524]
    // Forearm:   W=0.024 × D=0.024 × H=0.270, Z-bounds: [0.924 to 1.194]
    // Hand:      W=0.036 × D=0.036 × H=0.036, Z-bounds: [0.888 to 0.924]

    std::string side = is_left ? "left" : "right";
    // Position arms at torso edge PLUS half arm thickness so they don't overlap
    float side_offset = is_left ? -(spec.torso_width / 2.0f + spec.arm_thickness / 2.0f)
                                : (spec.torso_width / 2.0f + spec.arm_thickness / 2.0f);  // Eva: ±0.075

    // Calculate shoulder height (top of torso)
    float foot_height = spec.leg_thickness * 0.6f;  // 0.024
    float leg_top_z = z + foot_height + spec.leg_length;  // 0.924
    float hip_top_z = leg_top_z + spec.hip_height;  // 1.024
    float shoulder_z = hip_top_z + spec.torso_height;  // 1.524

    // Calculate segment lengths
    float upper_arm_length = spec.arm_length * spec.upper_arm_ratio;  // 0.600 × 0.55 = 0.330
    float forearm_length = spec.arm_length * (1.0f - spec.upper_arm_ratio);  // 0.600 × 0.45 = 0.270
    float hand_size = spec.arm_thickness * 1.2f;  // 0.030 × 1.2 = 0.036

    std::cout << "[ARM_" << side << "] Segments: upper=" << upper_arm_length
              << ", forearm=" << forearm_length << ", hand=" << hand_size
              << ", shoulder_z=" << shoulder_z << std::endl;

    // Stack from shoulder down (no overlap):
    float current_z = shoulder_z;

    // Upper arm - create separate entity
    float upper_arm_z = current_z - upper_arm_length / 2.0f;
    kg::EntityID upper_arm_entity = kg_->createEntity(side + "_upper_arm");
    kg_->setProperty(upper_arm_entity, "body_part", side + "_upper_arm");
    unsigned int upper_arm_particle = create_body_particle(
        x + side_offset, y, upper_arm_z,
        spec.arm_thickness, spec.arm_thickness, upper_arm_length,
        spec.skin_r, spec.skin_g, spec.skin_b,
        upper_arm_entity,  // Pass entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (bare skin or sleeves)
    );
    current_z -= upper_arm_length;  // Move down by upper arm length

    // Forearm - create separate entity
    float forearm_z = current_z - forearm_length / 2.0f;
    kg::EntityID forearm_entity = kg_->createEntity(side + "_forearm");
    kg_->setProperty(forearm_entity, "body_part", side + "_forearm");
    unsigned int forearm_particle = create_body_particle(
        x + side_offset, y, forearm_z,
        spec.arm_thickness * 0.8f, spec.arm_thickness * 0.8f, forearm_length,
        spec.skin_r, spec.skin_g, spec.skin_b,
        forearm_entity,  // Pass entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (bare skin or sleeves)
    );
    current_z -= forearm_length;  // Move down by forearm length

    // Hand - create separate entity
    float hand_z = current_z - hand_size / 2.0f;
    kg::EntityID hand_entity = kg_->createEntity(side + "_hand");
    kg_->setProperty(hand_entity, "body_part", side + "_hand");
    unsigned int hand_particle = create_body_particle(
        x + side_offset, y, hand_z,
        hand_size, hand_size, hand_size,
        spec.skin_r, spec.skin_g, spec.skin_b,
        hand_entity,  // Pass entity ID for collision filtering
        spec.hands_pattern  // Procedural pattern (skin)
    );

    // Create parent arm entity (no direct particles - uses relationships)
    kg::EntityID arm_entity = kg_->createEntity(side + "_arm");
    kg_->setProperty(arm_entity, "body_part", side + "_arm");

    // Create hierarchical relationships (particles accessed via children)
    kg_->createRelation(arm_entity, "HAS_PART", upper_arm_entity);
    kg_->createRelation(arm_entity, "HAS_PART", forearm_entity);
    kg_->createRelation(arm_entity, "HAS_PART", hand_entity);

    return arm_entity;
}

kg::EntityID HumanoidGenerator::create_leg(float x, float y, float z, bool is_left, const HumanoidSpec& spec) {
    // EVA EXPECTED PER LEG:
    // Foot:  W=0.040 × D=0.060 × H=0.024, Z-bounds: [0.000 to 0.024]
    // Shin:  W=0.032 × D=0.032 × H=0.405, Z-bounds: [0.024 to 0.429]
    // Thigh: W=0.040 × D=0.040 × H=0.495, Z-bounds: [0.429 to 0.924]

    std::string side = is_left ? "left" : "right";
    float side_offset = is_left ? -spec.hip_width / 3.0f : spec.hip_width / 3.0f;  // Eva: ±0.0433

    // Calculate segment lengths
    float thigh_length = spec.leg_length * spec.thigh_ratio;  // 0.900 × 0.55 = 0.495
    float shin_length = spec.leg_length * (1.0f - spec.thigh_ratio);  // 0.900 × 0.45 = 0.405
    float foot_height = spec.leg_thickness * 0.6f;  // 0.040 × 0.6 = 0.024
    float foot_length = spec.leg_thickness * 1.5f;  // 0.040 × 1.5 = 0.060

    std::cout << "[LEG_" << side << "] Segments: foot=" << foot_height
              << ", shin=" << shin_length << ", thigh=" << thigh_length << std::endl;

    // Stack from bottom up (no overlap):
    // Foot bottom touches ground at z
    float current_z = z;

    // Foot - create separate entity
    float foot_z = current_z + foot_height / 2.0f;
    kg::EntityID foot_entity = kg_->createEntity(side + "_foot");
    kg_->setProperty(foot_entity, "body_part", side + "_foot");
    unsigned int foot_particle = create_body_particle(
        x + side_offset, y + foot_length / 3.0f, foot_z,
        spec.leg_thickness, foot_length, foot_height,
        spec.skin_r * 0.8f, spec.skin_g * 0.8f, spec.skin_b * 0.8f,  // Darker for shoes/feet
        foot_entity,  // Pass entity ID for collision filtering
        spec.feet_pattern  // Procedural pattern (shoes/leather)
    );
    current_z += foot_height;  // Move up by foot height

    // Shin - create separate entity
    float shin_z = current_z + shin_length / 2.0f;
    kg::EntityID shin_entity = kg_->createEntity(side + "_shin");
    kg_->setProperty(shin_entity, "body_part", side + "_shin");
    unsigned int shin_particle = create_body_particle(
        x + side_offset, y, shin_z,
        spec.leg_thickness * 0.8f, spec.leg_thickness * 0.8f, shin_length,
        spec.skin_r, spec.skin_g, spec.skin_b,
        shin_entity,  // Pass entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    current_z += shin_length;  // Move up by shin length

    // Thigh - create separate entity
    float thigh_z = current_z + thigh_length / 2.0f;
    kg::EntityID thigh_entity = kg_->createEntity(side + "_thigh");
    kg_->setProperty(thigh_entity, "body_part", side + "_thigh");
    unsigned int thigh_particle = create_body_particle(
        x + side_offset, y, thigh_z,
        spec.leg_thickness, spec.leg_thickness, thigh_length,
        spec.clothing_r, spec.clothing_g, spec.clothing_b,
        thigh_entity,  // Pass entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );

    // Create parent leg entity (no direct particles - uses relationships)
    kg::EntityID leg_entity = kg_->createEntity(side + "_leg");
    kg_->setProperty(leg_entity, "body_part", side + "_leg");

    // Create hierarchical relationships (particles accessed via children)
    kg_->createRelation(leg_entity, "HAS_PART", thigh_entity);
    kg_->createRelation(leg_entity, "HAS_PART", shin_entity);
    kg_->createRelation(leg_entity, "HAS_PART", foot_entity);

    return leg_entity;
}

// ============================================================================
// MAIN GENERATION
// ============================================================================

kg::EntityID HumanoidGenerator::generate_humanoid(
    float world_x, float world_y, float world_z,
    const HumanoidSpec& spec) {

    std::cout << "[HUMANOID] Generating humanoid at (" << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;

    // Create root entity
    // No "type" property: Entity::type already carries it, the KG
    // gate (Malleus H1) rejects the shadow key, and nothing read it.
    kg::EntityID humanoid_entity = kg_->createEntity("Humanoid");
    kg_->setProperty(humanoid_entity, "height", std::to_string(spec.total_height));

    // Track this entity
    on_entity_created(humanoid_entity);

    // Store base position in KG for dynamics system
    kg_->setProperty(humanoid_entity, "position_x", std::to_string(world_x));
    kg_->setProperty(humanoid_entity, "position_y", std::to_string(world_y));
    kg_->setProperty(humanoid_entity, "position_z", std::to_string(world_z));
    // "behavior" = "look_at_mouse" removed (Malleus H1): input policy
    // is game territory, no engine or game code ever read the key.

    // ONLY CREATE FEET + SHINS
    std::vector<unsigned int> all_particles;
    std::vector<unsigned int> left_leg_particles;   // Left side leg IDs
    std::vector<unsigned int> right_leg_particles;  // Right side leg IDs
    std::vector<unsigned int> left_arm_particles;   // Left side arm IDs
    std::vector<unsigned int> right_arm_particles;  // Right side arm IDs
    std::vector<unsigned int> torso_particles;      // Torso (hips, abdomen, chest, neck)
    std::vector<unsigned int> head_particles;       // Head and hair (head, upper_hair, back_hair, ears)

    // Foot dimensions - shoe-shaped box (realistic human foot)
    float foot_width = 0.08f;   // 8cm wide
    float foot_length = 0.24f;  // 24cm long (front-to-back, ~size 38 EU)
    float foot_height = 0.08f;  // 8cm tall

    // 5cm gap above floor surface (same as totem) to prevent sinking
    const float FLOOR_GAP = 0.05f;
    float foot_z = world_z + FLOOR_GAP + foot_height / 2.0f;  // Bottom 5cm above floor

    // Mediterranean skin tone (warm olive)
    float foot_r = 0.85f;
    float foot_g = 0.68f;
    float foot_b = 0.55f;

    // Tanner hands and feet
    float tan_r = foot_r * 0.85f;
    float tan_g = foot_g * 0.85f;
    float tan_b = foot_b * 0.85f;

    // LEFT FOOT - SHOE-SHAPED BOX
    float leg_spacing = 0.15f;  // Narrower leg spacing (hip-width apart)
    float left_x = world_x - leg_spacing;  // Offset left

    std::cout << "[TEST] Creating LEFT FOOT at (" << left_x << ", " << world_y << ", " << foot_z << ")" << std::endl;
    unsigned int left_foot = create_body_particle(
        left_x, world_y, foot_z,
        foot_width, foot_length, foot_height,  // W×L×H = shoe shape
        tan_r, tan_g, tan_b,  // Tanner skin
        humanoid_entity,  // Entity ID for collision filtering
        spec.feet_pattern  // Procedural pattern (shoes/leather)
    );
    all_particles.push_back(left_foot);
    left_leg_particles.push_back(left_foot);

    // RIGHT FOOT - SHOE-SHAPED BOX
    float right_x = world_x + leg_spacing;  // Offset right

    std::cout << "[TEST] Creating RIGHT FOOT at (" << right_x << ", " << world_y << ", " << foot_z << ")" << std::endl;
    unsigned int right_foot = create_body_particle(
        right_x, world_y, foot_z,
        foot_width, foot_length, foot_height,  // W×L×H = shoe shape
        tan_r, tan_g, tan_b,  // Tanner skin
        humanoid_entity,  // Entity ID for collision filtering
        spec.feet_pattern  // Procedural pattern (shoes/leather)
    );
    all_particles.push_back(right_foot);
    right_leg_particles.push_back(right_foot);

    // LEFT SHIN - THIN VERTICAL STICK
    float shin_width = 0.05f;   // Thicker so we can see it clearly
    float shin_height = 0.4f;   // Tall vertical stick
    float shin_z = world_z + FLOOR_GAP + foot_height + shin_height / 2.0f;  // Sits on top of foot

    std::cout << "[TEST] Creating LEFT SHIN at (" << left_x << ", " << world_y << ", " << shin_z << ")" << std::endl;
    unsigned int left_shin = create_body_particle(
        left_x, world_y, shin_z,
        shin_width, shin_width, shin_height,  // Thin × Thin × TALL
        foot_r, foot_g, foot_b,  // Mediterranean skin tone (same as feet)
        humanoid_entity,  // Entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    all_particles.push_back(left_shin);
    left_leg_particles.push_back(left_shin);

    // RIGHT SHIN - THIN VERTICAL STICK
    std::cout << "[TEST] Creating RIGHT SHIN at (" << right_x << ", " << world_y << ", " << shin_z << ")" << std::endl;
    unsigned int right_shin = create_body_particle(
        right_x, world_y, shin_z,
        shin_width, shin_width, shin_height,  // Thin × Thin × TALL
        foot_r, foot_g, foot_b,  // Mediterranean skin tone (same as feet)
        humanoid_entity,  // Entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    all_particles.push_back(right_shin);
    right_leg_particles.push_back(right_shin);

    // LEFT THIGH - SLIGHTLY THICKER VERTICAL STICK
    float thigh_width = 0.06f;   // Slightly thicker than shin
    float thigh_height = 0.45f;  // Slightly taller than shin
    float thigh_z = world_z + FLOOR_GAP + foot_height + shin_height + thigh_height / 2.0f;  // Sits on top of shin

    std::cout << "[TEST] Creating LEFT THIGH at (" << left_x << ", " << world_y << ", " << thigh_z << ")" << std::endl;
    unsigned int left_thigh = create_body_particle(
        left_x, world_y, thigh_z,
        thigh_width, thigh_width, thigh_height,  // Slightly thicker × TALL
        foot_r, foot_g, foot_b,  // Mediterranean skin tone (same as feet)
        humanoid_entity,  // Entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    all_particles.push_back(left_thigh);
    left_leg_particles.push_back(left_thigh);

    // RIGHT THIGH - SLIGHTLY THICKER VERTICAL STICK
    std::cout << "[TEST] Creating RIGHT THIGH at (" << right_x << ", " << world_y << ", " << thigh_z << ")" << std::endl;
    unsigned int right_thigh = create_body_particle(
        right_x, world_y, thigh_z,
        thigh_width, thigh_width, thigh_height,  // Slightly thicker × TALL
        foot_r, foot_g, foot_b,  // Mediterranean skin tone (same as feet)
        humanoid_entity,  // Entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    all_particles.push_back(right_thigh);
    right_leg_particles.push_back(right_thigh);

    // HIPS - DARK GREEN HORIZONTAL BAR SPANNING LEGS
    float hip_width = (right_x - left_x) + thigh_width;  // Spans from left leg to right leg
    float hip_depth = 0.10f;   // Front-to-back depth
    float hip_height = 0.18f;  // Taller vertical height
    float hip_z = world_z + FLOOR_GAP + foot_height + shin_height + thigh_height + hip_height / 2.0f;  // Sits on top of thighs

    std::cout << "[TEST] Creating HIPS at (" << world_x << ", " << world_y << ", " << hip_z << ")" << std::endl;
    unsigned int hips = create_body_particle(
        world_x, world_y, hip_z,  // Centered between legs
        hip_width, hip_depth, hip_height,  // Wide × Thin × Short
        0.0f, 0.5f, 0.0f,  // Dark green
        humanoid_entity,  // Entity ID for collision filtering
        spec.legs_pattern  // Procedural pattern (pants)
    );
    all_particles.push_back(hips);
    torso_particles.push_back(hips);
    kg_->setProperty(humanoid_entity, "hips_particle", std::to_string(hips));

    // Generic center particle - used by any system that needs entity position
    // For humanoids, the hips represent the body center
    kg_->setProperty(humanoid_entity, "center_particle", std::to_string(hips));

    // ABDOMEN - TORSO/CHEST REGION
    float abdomen_width = hip_width * 0.9f;   // Slightly narrower than hips
    float abdomen_depth = 0.12f;   // Front-to-back depth
    float abdomen_height = 0.40f;  // Torso height
    float abdomen_z = world_z + FLOOR_GAP + foot_height + shin_height + thigh_height + hip_height + abdomen_height / 2.0f;

    std::cout << "[TEST] Creating ABDOMEN at (" << world_x << ", " << world_y << ", " << abdomen_z << ")" << std::endl;
    unsigned int abdomen = create_body_particle(
        world_x, world_y, abdomen_z,  // Centered, on top of hips
        abdomen_width, abdomen_depth, abdomen_height,  // Narrower × Thin × Tall
        0.8f, 0.6f, 0.4f,  // Tan/beige clothing
        humanoid_entity,  // Entity ID for collision filtering
        spec.torso_pattern  // Procedural pattern (shirt/clothing)
    );
    all_particles.push_back(abdomen);
    torso_particles.push_back(abdomen);
    kg_->setProperty(humanoid_entity, "abdomen_particle", std::to_string(abdomen));

    // TORSO - CHEST/SHOULDERS REGION (DARK GREEN)
    float torso_width = hip_width * 1.1f;   // Slightly wider than hips (shoulders) - reduced from 1.2
    float torso_depth = 0.14f;   // Thicker front-to-back - reduced from 0.16
    float torso_height = 0.30f;  // Chest height - reduced from 0.35
    float torso_z = world_z + FLOOR_GAP + foot_height + shin_height + thigh_height + hip_height + abdomen_height + torso_height / 2.0f;

    std::cout << "[TEST] Creating TORSO at (" << world_x << ", " << world_y << ", " << torso_z << ")" << std::endl;
    unsigned int torso = create_body_particle(
        world_x, world_y, torso_z,  // Centered, on top of abdomen
        torso_width, torso_depth, torso_height,  // Wider × Thicker × Tall
        0.0f, 0.4f, 0.0f,  // Dark green
        humanoid_entity,  // Entity ID for collision filtering
        spec.torso_pattern  // Procedural pattern (shirt/clothing)
    );
    all_particles.push_back(torso);
    torso_particles.push_back(torso);
    kg_->setProperty(humanoid_entity, "torso_particle", std::to_string(torso));
    kg_->setProperty(humanoid_entity, "camera_follow_particle", std::to_string(torso));  // Camera follows torso

    // SHOULDERS - CUBE PARTICLES (using CUBE shape, not BOX)
    float shoulder_size = 0.06f;  // 6cm cube shoulders
    float shoulder_z = world_z + FLOOR_GAP + foot_height + shin_height + thigh_height + hip_height + abdomen_height + torso_height;  // Top of torso
    float shoulder_offset = torso_width / 2.0f + 0.03f;  // At torso edges + small gap

    // LEFT SHOULDER
    float left_shoulder_x = world_x - shoulder_offset;
    float left_shoulder_y = world_y;
    float left_shoulder_z = shoulder_z;

    std::cout << "[TEST] Creating LEFT SHOULDER at (" << left_shoulder_x << ", " << left_shoulder_y << ", " << left_shoulder_z << ")" << std::endl;
    unsigned int left_shoulder_id = create_body_particle(
        left_shoulder_x, left_shoulder_y, left_shoulder_z,
        shoulder_size, shoulder_size, shoulder_size,  // Cube
        foot_r, foot_g, foot_b,
        humanoid_entity,
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(left_shoulder_id);
    left_arm_particles.push_back(left_shoulder_id);

    // RIGHT SHOULDER
    float right_shoulder_x = world_x + shoulder_offset;
    float right_shoulder_y = world_y;
    float right_shoulder_z = shoulder_z;

    std::cout << "[TEST] Creating RIGHT SHOULDER at (" << right_shoulder_x << ", " << right_shoulder_y << ", " << right_shoulder_z << ")" << std::endl;
    unsigned int right_shoulder_id = create_body_particle(
        right_shoulder_x, right_shoulder_y, right_shoulder_z,
        shoulder_size, shoulder_size, shoulder_size,  // Cube
        foot_r, foot_g, foot_b,
        humanoid_entity,
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(right_shoulder_id);
    right_arm_particles.push_back(right_shoulder_id);

    // NECK - Small, short cylindrical connector
    float neck_width = 0.04f;    // Thin neck
    float neck_height = 0.08f;   // Short neck
    float neck_z = shoulder_z + neck_height / 2.0f;  // Sits on top of torso/shoulders (shoulder_z already has FLOOR_GAP)

    std::cout << "[TEST] Creating NECK at (" << world_x << ", " << world_y << ", " << neck_z << ")" << std::endl;
    unsigned int neck = create_body_particle(
        world_x, world_y, neck_z,
        neck_width, neck_width, neck_height,  // Thin cylinder
        foot_r, foot_g, foot_b,  // Skin tone
        humanoid_entity,  // Entity ID for collision filtering
        spec.head_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(neck);
    torso_particles.push_back(neck);
    kg_->setProperty(humanoid_entity, "neck_particle", std::to_string(neck));

    // HEAD - Proportional to body
    float head_size = 0.20f;  // 20cm head (good proportion for ~1.5m body)
    float head_z = shoulder_z + neck_height + head_size / 2.0f;  // Sits on top of neck (shoulder_z already has FLOOR_GAP)

    std::cout << "[TEST] Creating HEAD at (" << world_x << ", " << world_y << ", " << head_z << ")" << std::endl;
    unsigned int head_id = create_body_particle(
        world_x, world_y, head_z,
        head_size, head_size, head_size,  // Cube
        foot_r, foot_g, foot_b,
        humanoid_entity,
        spec.head_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(head_id);
    head_particles.push_back(head_id);
    kg_->setProperty(humanoid_entity, "head_particle", std::to_string(head_id));

    // HAIR - Brown flat particles for upper and back
    float hair_r = 0.3f, hair_g = 0.2f, hair_b = 0.1f;  // Brown

    // UPPER HAIR - Flat on top of head
    // The cap's footprint is the HEAD's. At 1.1x it overhung the head at the
    // back, which is exactly where the back hair's front face sits: 9 mm of
    // one hair inside the other, a birth INV-37 refuses.
    float upper_hair_width = head_size;
    float upper_hair_depth = head_size;
    float upper_hair_thickness = 0.05f;          // Thicker flat layer
    float upper_hair_z = head_z + head_size / 2.0f + upper_hair_thickness / 2.0f;  // On top

    std::cout << "[TEST] Creating UPPER HAIR at (" << world_x << ", " << world_y << ", " << upper_hair_z << ")" << std::endl;
    unsigned int upper_hair = create_body_particle(
        world_x, world_y, upper_hair_z,
        upper_hair_width, upper_hair_depth, upper_hair_thickness,  // Wide × Deep × Thin
        hair_r, hair_g, hair_b,
        humanoid_entity,  // Entity ID for collision filtering
        spec.hair_pattern  // Procedural pattern (hair)
    );
    all_particles.push_back(upper_hair);
    head_particles.push_back(upper_hair);

    // BACK HAIR - Flat behind head
    float back_hair_width = head_size * 0.8f;    // Narrower than head
    float back_hair_height = head_size * 1.2f;   // Longer than head (flows down)
    float back_hair_thickness = 0.05f;           // Thicker flat layer
    float back_hair_y = world_y - head_size / 2.0f - back_hair_thickness / 2.0f;  // Behind head

    std::cout << "[TEST] Creating BACK HAIR at (" << world_x << ", " << back_hair_y << ", " << head_z << ")" << std::endl;
    unsigned int back_hair = create_body_particle(
        world_x, back_hair_y, head_z,
        back_hair_width, back_hair_thickness, back_hair_height,  // Wide × Thin × Tall
        hair_r, hair_g, hair_b,
        humanoid_entity,  // Entity ID for collision filtering
        spec.hair_pattern  // Procedural pattern (hair)
    );
    all_particles.push_back(back_hair);
    head_particles.push_back(back_hair);

    // EARS - Small cubes on sides of head, centered vertically
    float ear_size = 0.04f;  // Small ears
    // The ear's INNER face on the head's side face. At head_size/2 the ear's
    // centre was on that face and half of it was inside the head (20 mm each,
    // INV-37).
    float ear_offset = head_size / 2.0f + ear_size / 2.0f;

    // LEFT EAR
    std::cout << "[TEST] Creating LEFT EAR at (" << (world_x - ear_offset) << ", " << world_y << ", " << head_z << ")" << std::endl;
    unsigned int left_ear_id = create_body_particle(
        world_x - ear_offset, world_y, head_z,
        ear_size, ear_size, ear_size,  // Cube
        foot_r, foot_g, foot_b,
        humanoid_entity,
        spec.head_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(left_ear_id);
    head_particles.push_back(left_ear_id);

    // RIGHT EAR
    std::cout << "[TEST] Creating RIGHT EAR at (" << (world_x + ear_offset) << ", " << world_y << ", " << head_z << ")" << std::endl;
    unsigned int right_ear_id = create_body_particle(
        world_x + ear_offset, world_y, head_z,
        ear_size, ear_size, ear_size,  // Cube
        foot_r, foot_g, foot_b,
        humanoid_entity,
        spec.head_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(right_ear_id);
    head_particles.push_back(right_ear_id);

    // ARMS - UPPER ARM (BICEPS) BOX PARTICLES
    float arm_width = 0.05f;   // Upper arm thickness
    float arm_height = 0.30f;  // Upper arm length
    // FROM THE SHOULDER'S UNDERSIDE. shoulder_z is the shoulder cube's CENTRE,
    // so hanging the arm half its own length below that put its top half
    // inside the cube (30 mm, INV-37). The arm's top face is on the
    // shoulder's bottom face.
    float arm_z = shoulder_z - shoulder_size / 2.0f - arm_height / 2.0f;

    // LEFT UPPER ARM
    std::cout << "[TEST] Creating LEFT UPPER ARM at (" << (world_x - shoulder_offset) << ", " << world_y << ", " << arm_z << ")" << std::endl;
    unsigned int left_arm = create_body_particle(
        world_x - shoulder_offset, world_y, arm_z,
        arm_width, arm_width, arm_height,  // Thin × Thin × Long
        foot_r, foot_g, foot_b,  // Skin tone
        humanoid_entity,  // Entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(left_arm);
    left_arm_particles.push_back(left_arm);

    // RIGHT UPPER ARM
    std::cout << "[TEST] Creating RIGHT UPPER ARM at (" << (world_x + shoulder_offset) << ", " << world_y << ", " << arm_z << ")" << std::endl;
    unsigned int right_arm = create_body_particle(
        world_x + shoulder_offset, world_y, arm_z,
        arm_width, arm_width, arm_height,  // Thin × Thin × Long
        foot_r, foot_g, foot_b,  // Skin tone
        humanoid_entity,  // Entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(right_arm);
    right_arm_particles.push_back(right_arm);

    // FOREARMS - Slightly thinner, hanging from upper arms
    float forearm_width = arm_width * 0.8f;  // 0.032m - thinner than upper arm
    float forearm_height = 0.25f;  // Forearm length
    float forearm_z = shoulder_z - shoulder_size / 2.0f - arm_height
                    - forearm_height / 2.0f;  // Hangs from the upper arm

    // LEFT FOREARM
    std::cout << "[TEST] Creating LEFT FOREARM at (" << (world_x - shoulder_offset) << ", " << world_y << ", " << forearm_z << ")" << std::endl;
    unsigned int left_forearm = create_body_particle(
        world_x - shoulder_offset, world_y, forearm_z,
        forearm_width, forearm_width, forearm_height,  // Thin × Thin × Long
        foot_r, foot_g, foot_b,  // Skin tone
        humanoid_entity,  // Entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(left_forearm);
    left_arm_particles.push_back(left_forearm);

    // RIGHT FOREARM
    std::cout << "[TEST] Creating RIGHT FOREARM at (" << (world_x + shoulder_offset) << ", " << world_y << ", " << forearm_z << ")" << std::endl;
    unsigned int right_forearm = create_body_particle(
        world_x + shoulder_offset, world_y, forearm_z,
        forearm_width, forearm_width, forearm_height,  // Thin × Thin × Long
        foot_r, foot_g, foot_b,  // Skin tone
        humanoid_entity,  // Entity ID for collision filtering
        spec.arms_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(right_forearm);
    right_arm_particles.push_back(right_forearm);

    // HANDS - Rectangular, palm facing body (natural position)
    float hand_thickness = 0.03f;  // Palm thickness (X - thin, left-right)
    float hand_width = 0.10f;      // Palm width (Y - wide, facing body)
    float hand_length = 0.12f;     // Palm + fingers (Z - vertical)
    float hand_z = shoulder_z - shoulder_size / 2.0f - arm_height - forearm_height
                 - hand_length / 2.0f;  // Hangs from the forearm

    // LEFT HAND
    std::cout << "[TEST] Creating LEFT HAND at (" << (world_x - shoulder_offset) << ", " << world_y << ", " << hand_z << ")" << std::endl;
    unsigned int left_hand = create_body_particle(
        world_x - shoulder_offset, world_y, hand_z,
        hand_thickness, hand_width, hand_length,  // Thin × Wide × Long (palm facing body)
        tan_r, tan_g, tan_b,  // Tanner skin
        humanoid_entity,  // Entity ID for collision filtering
        spec.hands_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(left_hand);
    left_arm_particles.push_back(left_hand);

    // RIGHT HAND
    std::cout << "[TEST] Creating RIGHT HAND at (" << (world_x + shoulder_offset) << ", " << world_y << ", " << hand_z << ")" << std::endl;
    unsigned int right_hand = create_body_particle(
        world_x + shoulder_offset, world_y, hand_z,
        hand_thickness, hand_width, hand_length,  // Thin × Wide × Long (palm facing body)
        tan_r, tan_g, tan_b,  // Tanner skin
        humanoid_entity,  // Entity ID for collision filtering
        spec.hands_pattern  // Procedural pattern (skin)
    );
    all_particles.push_back(right_hand);
    right_arm_particles.push_back(right_hand);

    // Create body part entities using the unified helper
    // Each creates entity, binds particles, and links to parent
    std::cout << "[HUMANOID] Creating body parts: left_leg=" << left_leg_particles.size()
              << " right_leg=" << right_leg_particles.size()
              << " left_arm=" << left_arm_particles.size()
              << " right_arm=" << right_arm_particles.size()
              << " torso=" << torso_particles.size()
              << " head=" << head_particles.size() << std::endl;

    // A body part's TYPE comes from the ontology; which side it is on
    // is a property, not a type. Four of these six used to pass
    // "left_leg" and friends as types, which the KG rejected - so a
    // humanoid's arms and legs silently never existed as entities,
    // while its Torso and Head (correct types) did. Nothing checked
    // the return, so it read as working for as long as nobody asked
    // the graph about an arm.
    auto add_part = [&](const char* type, const char* name,
                        const std::vector<kg::RenderIndex>& parts) {
        kg::EntityID e = kg_->createChildEntityWithParticles(
            humanoid_entity, type, parts);
        if (e == kg::INVALID_ENTITY) {
            std::cerr << "[HumanoidGenerator] FAILED to create body part '"
                      << name << "' as type '" << type
                      << "' - is the ontology loaded?" << std::endl;
            return e;
        }
        kg_->setProperty(e, "body_part_name", name);
        return e;
    };
    add_part("Leg", "left_leg", left_leg_particles);
    add_part("Leg", "right_leg", right_leg_particles);
    add_part("Arm", "left_arm", left_arm_particles);
    add_part("Arm", "right_arm", right_arm_particles);
    add_part("Torso", "torso", torso_particles);
    add_part("Head", "head", head_particles);

    // Store body part offsets for pivot rotation
    kg_->setProperty(humanoid_entity, "leg_spacing", std::to_string(leg_spacing));
    kg_->setProperty(humanoid_entity, "shoulder_offset", std::to_string(shoulder_offset));
    kg_->setProperty(humanoid_entity, "ear_offset", std::to_string(ear_offset));

    // Movement speed properties (m/s) - per-entity, game-controlled
    // Realistic human speeds:
    //   Walk: 2.0 m/s (7.2 km/h, normal/brisk pace)
    //   Run: 4.0 m/s (14.4 km/h, jogging pace)
    // These can vary by entity (child slower, athlete faster)
    kg_->setProperty(humanoid_entity, "walk_speed", "2.0");    // Normal walking pace
    kg_->setProperty(humanoid_entity, "run_speed", "4.0");     // Jogging/running pace
    kg_->setProperty(humanoid_entity, "walk_frequency", "2.67"); // 2.67 Hz walk cycle (2.0/0.75 stride)
    kg_->setProperty(humanoid_entity, "run_frequency", "5.33");  // 5.33 Hz run cycle (4.0/0.75 stride)

    // Store left leg particles as comma-separated string
    std::string left_leg_str;
    for (size_t i = 0; i < left_leg_particles.size(); i++) {
        left_leg_str += std::to_string(left_leg_particles[i]);
        if (i < left_leg_particles.size() - 1) left_leg_str += ",";
    }
    kg_->setProperty(humanoid_entity, "left_leg_particles", left_leg_str);

    // Store right leg particles as comma-separated string
    std::string right_leg_str;
    for (size_t i = 0; i < right_leg_particles.size(); i++) {
        right_leg_str += std::to_string(right_leg_particles[i]);
        if (i < right_leg_particles.size() - 1) right_leg_str += ",";
    }
    kg_->setProperty(humanoid_entity, "right_leg_particles", right_leg_str);

    // Store left arm particles as comma-separated string
    std::string left_arm_str;
    for (size_t i = 0; i < left_arm_particles.size(); i++) {
        left_arm_str += std::to_string(left_arm_particles[i]);
        if (i < left_arm_particles.size() - 1) left_arm_str += ",";
    }
    kg_->setProperty(humanoid_entity, "left_arm_particles", left_arm_str);

    // Store right arm particles as comma-separated string
    std::string right_arm_str;
    for (size_t i = 0; i < right_arm_particles.size(); i++) {
        right_arm_str += std::to_string(right_arm_particles[i]);
        if (i < right_arm_particles.size() - 1) right_arm_str += ",";
    }
    kg_->setProperty(humanoid_entity, "right_arm_particles", right_arm_str);

    std::cout << "[TEST] Generated " << all_particles.size() << " particles total" << std::endl;
    std::cout << "[HUMANOID] Stored body parts in KG for dynamics system" << std::endl;

    // PHASE 2.5: Create particle constraints for structural integrity
    // Physics-based stiffness values (see docs/entity_structural_integrity_dynamics.md:161-237)
    // CORRECTED: Reduced from 75× too stiff values that caused numerical instability
    // Real biological joints: 5,000-20,000 N/m (adjusted for explicit Euler stability)

    // Load-bearing joints (support body weight)
    // 70kg body * 9.8 m/s² = 700N. To prevent >1cm stretch: need K > 70,000 N/m
    const float K_HIP      = 100000.0f;  // N/m - Full body weight transfer
    const float K_KNEE     = 80000.0f;   // N/m - High load during standing/walking
    const float K_ANKLE    = 80000.0f;   // N/m - Weight transfer to ground
    const float K_SPINE    = 50000.0f;   // N/m - Torso weight, allow slight flex
    const float K_NECK     = 30000.0f;   // N/m - Head weight, flexible

    // Non-load-bearing joints (arm weight only, but need some stiffness for punching)
    const float K_SHOULDER = 20000.0f; // N/m - Arm weight, punch stability
    const float K_ELBOW    = 15000.0f; // N/m - Forearm/hand weight
    const float K_WRIST    = 10000.0f; // N/m - Hand weight only

    std::cout << "[HUMANOID] Creating particle constraints with physics-based stiffness..." << std::endl;
    std::cout << "  Load-bearing: Hip=" << K_HIP << " Knee=" << K_KNEE << " Ankle=" << K_ANKLE << " N/m" << std::endl;
    std::cout << "  Non-load-bearing: Shoulder=" << K_SHOULDER << " Elbow=" << K_ELBOW << " Wrist=" << K_WRIST << " N/m" << std::endl;

    // Spine constraints: head → neck → torso → abdomen → hips (4 constraints)
    create_humanoid_constraint(humanoid_entity, head_id, neck, K_NECK);
    create_humanoid_constraint(humanoid_entity, neck, torso, K_SPINE);
    create_humanoid_constraint(humanoid_entity, torso, abdomen, K_SPINE);
    create_humanoid_constraint(humanoid_entity, abdomen, hips, K_SPINE);

    // Left leg constraints with semantic joint names
    create_humanoid_constraint(humanoid_entity, hips, left_thigh, K_HIP, "left_hip");
    create_humanoid_constraint(humanoid_entity, left_thigh, left_shin, K_KNEE, "left_knee");
    create_humanoid_constraint(humanoid_entity, left_shin, left_foot, K_ANKLE, "left_ankle");

    // Right leg constraints with semantic joint names
    create_humanoid_constraint(humanoid_entity, hips, right_thigh, K_HIP, "right_hip");
    create_humanoid_constraint(humanoid_entity, right_thigh, right_shin, K_KNEE, "right_knee");
    create_humanoid_constraint(humanoid_entity, right_shin, right_foot, K_ANKLE, "right_ankle");

    // Left arm constraints (spine anchor has no joint name - not animated)
    create_humanoid_constraint(humanoid_entity, torso, left_shoulder_id, K_SPINE);
    create_humanoid_constraint(humanoid_entity, left_shoulder_id, left_arm, K_SHOULDER, "left_shoulder");
    create_humanoid_constraint(humanoid_entity, left_arm, left_forearm, K_ELBOW, "left_elbow");
    create_humanoid_constraint(humanoid_entity, left_forearm, left_hand, K_WRIST, "left_wrist");

    // Right arm constraints (spine anchor has no joint name - not animated)
    create_humanoid_constraint(humanoid_entity, torso, right_shoulder_id, K_SPINE);
    create_humanoid_constraint(humanoid_entity, right_shoulder_id, right_arm, K_SHOULDER, "right_shoulder");
    create_humanoid_constraint(humanoid_entity, right_arm, right_forearm, K_ELBOW, "right_elbow");
    create_humanoid_constraint(humanoid_entity, right_forearm, right_hand, K_WRIST, "right_wrist");

    std::cout << "[HUMANOID] Created 22 particle constraints for skeletal structure" << std::endl;

    // TODO(PHYSICS_v2): Re-implement collision filtering after physics rebuild
    // Set collision mode to FILTER_SAME_ENTITY (Eva is animated, limbs can pass through each other)
    // Without this, animation-driven limb movements would cause self-collision explosions
    // See docs/dynamics_physics_integration.md:720-827
    // engine_->get_physics_system().set_entity_collision_mode(humanoid_entity, CollisionMode::FILTER_SAME_ENTITY);
    // std::cout << "[HUMANOID] Set collision mode to FILTER_SAME_ENTITY (animated entity)" << std::endl;

    return humanoid_entity;
}

// PHASE 2.3: Create particle constraint between two particles
// FIXED: Use KG particle data instead of ParticleSystem (particles not activated yet!)
void HumanoidGenerator::create_humanoid_constraint(kg::EntityID entity_id,
                                                   unsigned int kg_particle_a,
                                                   unsigned int kg_particle_b,
                                                   float stiffness,
                                                   const std::string& joint_name) {
    if (!kg_) {
        std::cerr << "[HumanoidGenerator] Cannot create constraint - KG not initialized!" << std::endl;
        return;
    }

    // Read particle data from KG (NOT ParticleSystem - particles not activated yet!)
    if (!kg_->hasKGParticleData(kg_particle_a) || !kg_->hasKGParticleData(kg_particle_b)) {
        std::cerr << "[HumanoidGenerator] Cannot create constraint - KG particles missing data: "
                  << kg_particle_a << ", " << kg_particle_b << std::endl;
        return;
    }

    Particle p_a = kg_->getKGParticleData(kg_particle_a);
    Particle p_b = kg_->getKGParticleData(kg_particle_b);

    // Calculate rest distance from KG particle positions
    float dx = p_b.x - p_a.x;
    float dy = p_b.y - p_a.y;
    float dz = p_b.z - p_a.z;
    float rest_distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    // Create constraint entity in KG
    kg::EntityID constraint_id = kg_->createEntity("Constraint");

    // Store constraint properties with KG particle IDs
    // These will be converted to render indices when constraints are loaded after activation
    kg_->setProperty(constraint_id, "particle_a", std::to_string(kg_particle_a));
    kg_->setProperty(constraint_id, "particle_b", std::to_string(kg_particle_b));
    kg_->setProperty(constraint_id, "rest_distance", std::to_string(rest_distance));
    kg_->setProperty(constraint_id, "base_stiffness", std::to_string(stiffness));

    // Store semantic joint name for FK animation (if provided)
    if (!joint_name.empty()) {
        kg_->setProperty(constraint_id, "joint_name", joint_name);
    }

    // Create relation: entity has_constraint constraint
    kg_->createRelation(entity_id, "HAS_CONSTRAINT", constraint_id);

    std::cout << "  [CONSTRAINT DEBUG] KG " << kg_particle_a << " ↔ KG " << kg_particle_b << std::endl;
    std::cout << "    Particle A pos=(" << p_a.x << "," << p_a.y << "," << p_a.z << ")" << std::endl;
    std::cout << "    Particle B pos=(" << p_b.x << "," << p_b.y << "," << p_b.z << ")" << std::endl;
    std::cout << "    REST_DISTANCE=" << rest_distance << "m (calculated from positions)" << std::endl;
    std::cout << "    STIFFNESS=" << stiffness << " N/m" << std::endl;
}

// ============================================================================
// SHARED HELPER: Create and Activate Eva (for Eden + tests)
// ============================================================================

HumanoidGenerator::EvaCreationResult HumanoidGenerator::create_and_activate_eva(
    float world_x, float world_y, float world_z, Engine* engine) {

    EvaCreationResult result;
    result.entity_id = kg::INVALID_ENTITY;
    result.particle_count = 0;

    // Generate Eva humanoid entity
    HumanoidSpec eva_spec = HumanoidSpec::eva();
    result.entity_id = generate_humanoid(world_x, world_y, world_z, eva_spec);

    std::cout << "[EVA_SHARED] Generated Eva entity " << result.entity_id << std::endl;

    // Activate Eva through EntityManager
    auto& entity_mgr = engine->get_entity_manager();
    auto& kg = engine->get_kg();
    auto& particle_system = engine->get_particle_system();

    ActivationResult activation = entity_mgr.activate_entity(result.entity_id);
    result.particle_count = static_cast<int>(activation.particles.size());

    std::cout << "[EVA_SHARED] Activation returned " << result.particle_count << " particles" << std::endl;

    // Process activation result - add particles to ParticleSystem and update KG bindings
    for (size_t i = 0; i < activation.particles.size(); i++) {
        const Particle& p = activation.particles[i];
        int render_idx = particle_system.add_particle(p);

        // Update KG particle with render index
        if (i < activation.kg_particle_ids.size()) {
            kg::KGParticleID kg_id = activation.kg_particle_ids[i];
            kg.updateRenderIndex(kg_id, static_cast<kg::RenderIndex>(render_idx));
            result.render_indices.push_back(static_cast<kg::RenderIndex>(render_idx));
        }
    }

    std::cout << "[EVA_SHARED] Added " << result.render_indices.size() << " particles to ParticleSystem" << std::endl;

    // Load constraints now that particles have render indices
    engine->get_physics_system().load_constraints_from_kg();
    std::cout << "[EVA_SHARED] Loaded constraints for Eva skeletal structure" << std::endl;

    return result;
}

HumanoidGenerator::HumanoidCreationResult HumanoidGenerator::create_and_activate_hunter(
    float world_x, float world_y, float world_z, Engine* engine) {

    HumanoidCreationResult result;
    result.entity_id = kg::INVALID_ENTITY;
    result.particle_count = 0;

    // Generate Hunter humanoid entity
    HumanoidSpec hunter_spec = HumanoidSpec::hunter();
    result.entity_id = generate_humanoid(world_x, world_y, world_z, hunter_spec);

    std::cout << "[HUNTER_SHARED] Generated Hunter entity " << result.entity_id << std::endl;

    // Activate Hunter through EntityManager
    auto& entity_mgr = engine->get_entity_manager();
    auto& kg = engine->get_kg();
    auto& particle_system = engine->get_particle_system();

    ActivationResult activation = entity_mgr.activate_entity(result.entity_id);
    result.particle_count = static_cast<int>(activation.particles.size());

    std::cout << "[HUNTER_SHARED] Activation returned " << result.particle_count << " particles" << std::endl;

    // Process activation result - add particles to ParticleSystem and update KG bindings
    for (size_t i = 0; i < activation.particles.size(); i++) {
        const Particle& p = activation.particles[i];
        int render_idx = particle_system.add_particle(p);

        // Update KG particle with render index
        if (i < activation.kg_particle_ids.size()) {
            kg::KGParticleID kg_id = activation.kg_particle_ids[i];
            kg.updateRenderIndex(kg_id, static_cast<kg::RenderIndex>(render_idx));
            result.render_indices.push_back(static_cast<kg::RenderIndex>(render_idx));
        }
    }

    std::cout << "[HUNTER_SHARED] Added " << result.render_indices.size() << " particles to ParticleSystem" << std::endl;

    // Load constraints now that particles have render indices
    engine->get_physics_system().load_constraints_from_kg();
    std::cout << "[HUNTER_SHARED] Loaded constraints for Hunter skeletal structure" << std::endl;

    return result;
}

// ============================================================================
// PHYSICS-BASED HUMANOID GENERATION (Gluon Constraints)
// ============================================================================
// Creates humanoid directly using PhysicsSystem gluon attachment.
// Proven to work with zero drift in test_eva_physics.cpp.

PhysicsHumanoidResult HumanoidGenerator::generate_humanoid_physics(
    float world_x, float world_y, float world_z,
    int floor_particle_id,
    const HumanoidSpec& spec,
    bool kg_support)
{
    PhysicsHumanoidResult result;

    // Need engine for physics access
    if (!engine_) {
        std::cerr << "[HumanoidGenerator] ERROR: engine_ not set" << std::endl;
        return result;
    }

    auto& physics = engine_->get_physics_system();
    auto& ps = engine_->get_particle_system();

    // Always create KG entity (needed for joint registration, even without full kg_support)
    auto& kg = engine_->get_kg();
    kg::EntityID entity_id = kg.createEntity("Humanoid");
    result.entity_id = entity_id;
    std::cout << "[PHYSICS_HUMANOID] Created KG entity " << entity_id << std::endl;

    std::cout << "\n[PHYSICS_HUMANOID] Generating at (" << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;

    // ========================================================================
    // DERIVE ALL DIMENSIONS FROM SPEC (no hardcoded body dimensions)
    // ========================================================================
    // Layout spreads (derived from body width)
    const float LEG_SPREAD = spec.hip_width * 0.45f;   // Legs spread relative to hips
    const float ARM_SPREAD = spec.torso_width * 0.55f; // Arms spread relative to torso

    // --- LEG DIMENSIONS ---
    const float full_foot_height = spec.leg_thickness * 2.0f;  // Total front-to-back length
    const float foot_width = spec.leg_thickness * 0.85f;
    const float foot_height = full_foot_height * 0.65f;    // Heel/midfoot: 65% of original
    const float foot_thickness = 0.08f;                     // Small constant (vertical)
    const float toe_width = foot_width;
    const float toe_height = full_foot_height * 0.35f;     // Ball/forefoot: 35% of original
    const float toe_thickness = foot_thickness * 0.7f;     // Toes are thinner vertically
    // -------------------------------------------------------------------------
    // LEG PROPORTIONS: Account for visual overlap at hip joint
    //
    // The thigh particle connects to hips, so part of the thigh is visually
    // "inside" the hip region. The shin has less hidden volume.
    // To get anatomically correct VISIBLE proportions:
    //   - Thigh: 55% of leg_length (longer particle, more hidden)
    //   - Shin:  38% of leg_length (shorter particle, less hidden)
    //   - Remaining 7% absorbed by foot/ankle region
    //
    // Anatomical reference (adult human):
    //   - Femur (thigh): ~54% of leg
    //   - Tibia (shin):  ~46% of leg
    // -------------------------------------------------------------------------
    const float shin_length = spec.leg_length * 0.38f;
    const float shin_width = spec.leg_thickness * 0.5f;
    const float thigh_length = spec.leg_length * 0.55f;
    const float thigh_width = spec.leg_thickness * 0.7f;

    // --- HIP DIMENSIONS ---
    const float hips_width = spec.hip_width;
    const float hips_depth = spec.hip_height;              // Using hip_height for front-to-back
    const float hips_thickness = spec.hip_height;          // Vertical thickness

    // --- TORSO DIMENSIONS ---
    const float abdomen_width = spec.torso_width * 0.85f;
    const float abdomen_depth = spec.torso_depth * 0.75f;
    const float abdomen_thickness = spec.torso_height * 0.35f;
    const float chest_width = spec.torso_width;
    const float chest_depth = spec.torso_depth;
    const float chest_thickness = spec.torso_height * 0.45f;

    // --- NECK/HEAD DIMENSIONS ---
    const float neck_width = spec.head_size * 0.55f;
    const float neck_thickness = spec.head_size * 0.65f;
    const float head_width = spec.head_size * 1.4f;        // Head slightly wider than head_size
    const float head_depth = spec.head_size * 1.4f;
    const float head_thickness = spec.head_size * 1.5f;

    // --- ARM DIMENSIONS ---
    const float shoulder_size = spec.arm_thickness * 0.75f;
    const float upper_arm_length = spec.arm_length * 0.50f;
    const float upper_arm_width = spec.arm_thickness;
    const float forearm_length = spec.arm_length * 0.45f;
    const float forearm_width = spec.arm_thickness * 0.70f;
    const float hand_width = spec.arm_thickness;
    const float hand_depth = spec.arm_thickness * 0.50f;
    const float hand_thickness = spec.arm_thickness * 1.25f;
    // HOW FAR OUTBOARD OF ITS SHOULDER THE ARM HANGS. The shoulder joint sits
    // ARM_SPREAD from the body's centre line, which for every spec in the
    // engine is INSIDE the body's own half-span plus half an arm: hang the
    // arm at the joint and it is born through whatever it passes - the chest
    // (17.5 mm on Eva) and then, once cleared of that, the thigh (2 mm).
    // Derived, not tuned: the widest thing the arm hangs beside decides it,
    // the arm's inner face lands exactly on that face, and the term is zero
    // for any spec whose shoulders are already outboard of its body.
    const float body_half_span = 0.5f * std::max(
        std::max(chest_width, abdomen_width),
        std::max(hips_width, 2.0f * (LEG_SPREAD + thigh_width * 0.5f)));
    const float arm_half_span = 0.5f * std::max(upper_arm_width, hand_width);
    const float arm_lateral_clearance =
        std::max(0.0f, (body_half_span + arm_half_span) - ARM_SPREAD);

    // --- HAIR DIMENSIONS (relative to head) ---
    // The cap's footprint is the HEAD's. At 1.1x it overhung the head by
    // 9 mm at the back, and the back hair - whose front face sits on the
    // head's back face - was born 9.1 mm inside that overhang (INV-37).
    const float upper_hair_width = head_width;
    const float upper_hair_thickness = 0.05f;              // Thin cap
    const float back_hair_width = head_width * 0.8f;
    const float back_hair_length = head_thickness * 1.1f;

    // --- EAR SIZE (relative to head) ---
    const float ear_size = spec.head_size * 0.25f;

    // ========================================================================
    // NO FLOOR - Eva is not gluoned to external surfaces
    // ========================================================================
    // Eva stands on world floor via CONTACT constraints (not gluons).
    // Gluons are for internal body connections only.
    result.floor_id = -1;
    (void)floor_particle_id;  // Unused, kept for API compatibility

    // Use colors from spec (allows customization per-humanoid)
    const float skin_r = spec.skin_r, skin_g = spec.skin_g, skin_b = spec.skin_b;
    const float clothing_r = spec.clothing_r, clothing_g = spec.clothing_g, clothing_b = spec.clothing_b;

    // ========================================================================
    // LEFT LEG: foot → shin → thigh
    // ========================================================================
    // LEFT FOOT - standalone particle (no gluon to external surfaces)
    Particle l_foot = {};
    l_foot.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_foot.x = world_x - LEG_SPREAD;
    l_foot.y = world_y;
    l_foot.z = world_z + foot_thickness / 2.0f;  // Foot center above floor level
    l_foot.shape = ParticleShape::BOX;
    l_foot.width = foot_width; l_foot.height = foot_height; l_foot.thickness = foot_thickness;
    l_foot.r = skin_r; l_foot.g = skin_g; l_foot.b = skin_b; l_foot.a = 1.0f;
    l_foot.material_strength = 50e6f;
    l_foot.friction = 0.8f;  // Floor friction: stops drift after collision, walk animation overcomes it
    int l_foot_id = ps.add_particle(l_foot);  // Direct add, NO gluon to floor
    result.body_ids.push_back(l_foot_id);
    std::cout << "  L_Foot: id=" << l_foot_id << " (standalone)" << std::endl;

    // LEFT TOE - gluoned to front of foot
    Particle l_toe = {};
    l_toe.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_toe.shape = ParticleShape::BOX;
    l_toe.width = toe_width; l_toe.height = toe_height; l_toe.thickness = toe_thickness;
    l_toe.r = skin_r; l_toe.g = skin_g; l_toe.b = skin_b; l_toe.a = 1.0f;
    l_toe.material_strength = 50e6f;
    l_toe.friction = 0.8f;

    auto l_toe_gluon = std::make_unique<NailGluon>();
    l_toe_gluon->offset_a = Vec3(0.0f, foot_height / 2.0f, 0.0f);    // Front edge of foot
    l_toe_gluon->offset_b = Vec3(0.0f, -toe_height / 2.0f, 0.0f);    // Back edge of toe
    l_toe_gluon->target_distance = 0.0f;
    l_toe_gluon->breaking_force = 100000.0f;
    l_toe_gluon->angular_stiffness = 100.0f;
    l_toe_gluon->angular_damping = 6.0f;
    l_toe_gluon->max_relative_rotation = 0.52f;  // ~30° toe flex range
    int l_toe_id = physics.add_particle_with_gluon_to(l_foot_id, l_toe, std::move(l_toe_gluon));
    result.body_ids.push_back(l_toe_id);
    std::cout << "  L_Toe: id=" << l_toe_id << std::endl;

    Particle l_shin = {};
    l_shin.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_shin.shape = ParticleShape::BOX;
    l_shin.width = shin_width; l_shin.height = shin_width; l_shin.thickness = shin_length;
    l_shin.r = skin_r; l_shin.g = skin_g; l_shin.b = skin_b; l_shin.a = 1.0f;
    l_shin.material_strength = 50e6f;

    auto l_shin_gluon = std::make_unique<NailGluon>();
    l_shin_gluon->offset_a = Vec3(0.0f, -foot_height * 0.35f, foot_thickness / 2.0f);  // Foot top, near heel (-Y = back in local frame)
    l_shin_gluon->offset_b = Vec3(0.0f, 0.0f, -shin_length / 2.0f);     // Shin bottom
    l_shin_gluon->target_distance = 0.0f;
    l_shin_gluon->breaking_force = 100000.0f;
    // Angular constraint: Ankle joint (reduced 2x)
    l_shin_gluon->angular_stiffness = 150.0f;
    l_shin_gluon->angular_damping = 8.0f;
    l_shin_gluon->max_relative_rotation = 0.175f;  // ±10° - ankles don't twist
    int l_shin_id = physics.add_particle_with_gluon_to(l_foot_id, l_shin, std::move(l_shin_gluon));
    result.body_ids.push_back(l_shin_id);
    std::cout << "  L_Shin: id=" << l_shin_id << std::endl;

    Particle l_thigh = {};
    l_thigh.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_thigh.shape = ParticleShape::BOX;
    l_thigh.width = thigh_width; l_thigh.height = thigh_width; l_thigh.thickness = thigh_length;
    l_thigh.r = skin_r; l_thigh.g = skin_g; l_thigh.b = skin_b; l_thigh.a = 1.0f;
    l_thigh.material_strength = 50e6f;

    auto l_thigh_gluon = std::make_unique<NailGluon>();
    // -------------------------------------------------------------------------
    // KNEE GLUON: Edge-to-edge connection (no overlap)
    //
    // Gluon offsets define WHERE on each particle the joint attaches:
    //
    //   THIGH PARTICLE          offset_b points to BOTTOM edge
    //   ┌─────────────┐         (thigh_center - thickness/2)
    //   │   ● center  │
    //   └──────○──────┘  ← offset_b = (0, 0, -thigh_length/2)
    //          ║
    //          ║  GLUON constrains these points to be coincident
    //          ║
    //   ┌──────○──────┐  ← offset_a = (0, 0, +shin_length/2)
    //   │   ● center  │
    //   └─────────────┘         (shin_center + thickness/2)
    //   SHIN PARTICLE           offset_a points to TOP edge
    //
    // With /2 offsets: particles meet exactly at boundary, no visual overlap.
    // The gluon sits at the "knee joint" where thigh ends and shin begins.
    // -------------------------------------------------------------------------
    l_thigh_gluon->offset_a = Vec3(0.0f, 0.0f, shin_length / 2.0f);    // Shin TOP edge
    l_thigh_gluon->offset_b = Vec3(0.0f, 0.0f, -thigh_length / 2.0f);  // Thigh BOTTOM edge
    l_thigh_gluon->target_distance = 0.0f;
    l_thigh_gluon->breaking_force = 100000.0f;
    // Angular constraint: Knee joint (reduced 2x)
    l_thigh_gluon->angular_stiffness = 250.0f;
    l_thigh_gluon->angular_damping = 10.0f;
    l_thigh_gluon->max_relative_rotation = 0.26f;  // ±15° - knees rotate with hips
    int l_thigh_id = physics.add_particle_with_gluon_to(l_shin_id, l_thigh, std::move(l_thigh_gluon));
    result.body_ids.push_back(l_thigh_id);
    std::cout << "  L_Thigh: id=" << l_thigh_id << std::endl;

    // Populate left leg IDs for animation: [foot(0), shin(1), thigh(2), toe(3)]
    result.left_leg_ids = {l_foot_id, l_shin_id, l_thigh_id, l_toe_id};

    // ========================================================================
    // RIGHT LEG: foot → shin → thigh
    // ========================================================================
    // RIGHT FOOT - standalone particle (no gluon to external surfaces)
    Particle r_foot = {};
    r_foot.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_foot.x = world_x + LEG_SPREAD;
    r_foot.y = world_y;
    r_foot.z = world_z + foot_thickness / 2.0f;  // Foot center above floor level
    r_foot.shape = ParticleShape::BOX;
    r_foot.width = foot_width; r_foot.height = foot_height; r_foot.thickness = foot_thickness;
    r_foot.r = skin_r; r_foot.g = skin_g; r_foot.b = skin_b; r_foot.a = 1.0f;
    r_foot.material_strength = 50e6f;
    r_foot.friction = 0.8f;  // Floor friction: stops drift after collision, walk animation overcomes it
    int r_foot_id = ps.add_particle(r_foot);  // Direct add, NO gluon to floor
    result.body_ids.push_back(r_foot_id);
    std::cout << "  R_Foot: id=" << r_foot_id << " (standalone)" << std::endl;

    // RIGHT TOE - gluoned to front of foot
    Particle r_toe = {};
    r_toe.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_toe.shape = ParticleShape::BOX;
    r_toe.width = toe_width; r_toe.height = toe_height; r_toe.thickness = toe_thickness;
    r_toe.r = skin_r; r_toe.g = skin_g; r_toe.b = skin_b; r_toe.a = 1.0f;
    r_toe.material_strength = 50e6f;
    r_toe.friction = 0.8f;

    auto r_toe_gluon = std::make_unique<NailGluon>();
    r_toe_gluon->offset_a = Vec3(0.0f, foot_height / 2.0f, 0.0f);    // Front edge of foot
    r_toe_gluon->offset_b = Vec3(0.0f, -toe_height / 2.0f, 0.0f);    // Back edge of toe
    r_toe_gluon->target_distance = 0.0f;
    r_toe_gluon->breaking_force = 100000.0f;
    r_toe_gluon->angular_stiffness = 100.0f;
    r_toe_gluon->angular_damping = 6.0f;
    r_toe_gluon->max_relative_rotation = 0.52f;  // ~30° toe flex range
    int r_toe_id = physics.add_particle_with_gluon_to(r_foot_id, r_toe, std::move(r_toe_gluon));
    result.body_ids.push_back(r_toe_id);
    std::cout << "  R_Toe: id=" << r_toe_id << std::endl;

    Particle r_shin = {};
    r_shin.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_shin.shape = ParticleShape::BOX;
    r_shin.width = shin_width; r_shin.height = shin_width; r_shin.thickness = shin_length;
    r_shin.r = skin_r; r_shin.g = skin_g; r_shin.b = skin_b; r_shin.a = 1.0f;
    r_shin.material_strength = 50e6f;

    auto r_shin_gluon = std::make_unique<NailGluon>();
    r_shin_gluon->offset_a = Vec3(0.0f, -foot_height * 0.35f, foot_thickness / 2.0f);  // Foot top, near heel (-Y = back in local frame)
    r_shin_gluon->offset_b = Vec3(0.0f, 0.0f, -shin_length / 2.0f);     // Shin bottom
    r_shin_gluon->target_distance = 0.0f;
    r_shin_gluon->breaking_force = 100000.0f;
    // Angular constraint: Ankle joint (reduced 2x)
    r_shin_gluon->angular_stiffness = 150.0f;
    r_shin_gluon->angular_damping = 8.0f;
    r_shin_gluon->max_relative_rotation = 0.175f;  // ±10° - ankles don't twist
    int r_shin_id = physics.add_particle_with_gluon_to(r_foot_id, r_shin, std::move(r_shin_gluon));
    result.body_ids.push_back(r_shin_id);
    std::cout << "  R_Shin: id=" << r_shin_id << std::endl;

    Particle r_thigh = {};
    r_thigh.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_thigh.shape = ParticleShape::BOX;
    r_thigh.width = thigh_width; r_thigh.height = thigh_width; r_thigh.thickness = thigh_length;
    r_thigh.r = skin_r; r_thigh.g = skin_g; r_thigh.b = skin_b; r_thigh.a = 1.0f;
    r_thigh.material_strength = 50e6f;

    auto r_thigh_gluon = std::make_unique<NailGluon>();
    // Knee gluon: edge-to-edge (see left knee for detailed explanation)
    r_thigh_gluon->offset_a = Vec3(0.0f, 0.0f, shin_length / 2.0f);    // Shin TOP edge
    r_thigh_gluon->offset_b = Vec3(0.0f, 0.0f, -thigh_length / 2.0f);  // Thigh BOTTOM edge
    r_thigh_gluon->target_distance = 0.0f;
    r_thigh_gluon->breaking_force = 100000.0f;
    // Angular constraint: Knee joint - rigid (stiffness reduced 2x to prevent oscillation)
    r_thigh_gluon->angular_stiffness = 250.0f;
    r_thigh_gluon->angular_damping = 10.0f;
    r_thigh_gluon->max_relative_rotation = 0.26f;  // ±15° - knees rotate with hips
    int r_thigh_id = physics.add_particle_with_gluon_to(r_shin_id, r_thigh, std::move(r_thigh_gluon));
    result.body_ids.push_back(r_thigh_id);
    std::cout << "  R_Thigh: id=" << r_thigh_id << std::endl;

    // Populate right leg IDs for animation: [foot(0), shin(1), thigh(2), toe(3)]
    result.right_leg_ids = {r_foot_id, r_shin_id, r_thigh_id, r_toe_id};

    // ========================================================================
    // TORSO: hips → abdomen → chest
    // ========================================================================
    Particle hips = {};
    hips.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    hips.shape = ParticleShape::BOX;
    hips.width = hips_width; hips.height = hips_depth; hips.thickness = hips_thickness;
    hips.r = clothing_r; hips.g = clothing_g; hips.b = clothing_b; hips.a = 1.0f;
    hips.material_strength = 50e6f;
    // Emit living flesh scent - undead creatures can smell this
    hips.odor_type = OdorType::LIVING_FLESH;
    hips.odor_radius = 15.0f;
    hips.odor_intensity = 1.0f;

    auto hips_gluon = std::make_unique<NailGluon>();
    hips_gluon->offset_a = Vec3(0.0f, 0.0f, thigh_length / 2.0f);              // Left thigh top
    hips_gluon->offset_b = Vec3(-LEG_SPREAD, 0.0f, -hips_thickness / 2.0f);    // Hips bottom left
    hips_gluon->target_distance = 0.0f;
    hips_gluon->breaking_force = 200000.0f;
    hips_gluon->rotate_offsets = true;  // SKELETON: orbit thighs around hips when rotating
    // Angular constraint: Hip joint - stiff (reduced 2x)
    hips_gluon->angular_stiffness = 150.0f;
    hips_gluon->angular_damping = 8.0f;
    hips_gluon->max_relative_rotation = 0.175f;  // ±10° - hips rotate only when spine at limit
    int hips_id = physics.add_particle_with_gluon_to(l_thigh_id, hips, std::move(hips_gluon));
    result.body_ids.push_back(hips_id);
    result.hips_id = hips_id;
    std::cout << "  Hips: id=" << hips_id << std::endl;

    // RIGHT THIGH → HIPS gluon (critical! without this, right leg floats free)
    auto r_thigh_hips_gluon = std::make_unique<NailGluon>();
    r_thigh_hips_gluon->offset_a = Vec3(0.0f, 0.0f, thigh_length / 2.0f);             // Right thigh top
    r_thigh_hips_gluon->offset_b = Vec3(LEG_SPREAD, 0.0f, -hips_thickness / 2.0f);    // Hips bottom right
    r_thigh_hips_gluon->target_distance = 0.0f;
    r_thigh_hips_gluon->breaking_force = 200000.0f;
    r_thigh_hips_gluon->rotate_offsets = true;  // SKELETON: orbit thighs around hips when rotating
    // Angular constraint: Hip joint - stiff (reduced 2x)
    r_thigh_hips_gluon->angular_stiffness = 150.0f;
    r_thigh_hips_gluon->angular_damping = 8.0f;
    r_thigh_hips_gluon->max_relative_rotation = 0.175f;  // ±10° - hips rotate only when spine at limit
    physics.add_gluon_between(r_thigh_id, hips_id, std::move(r_thigh_hips_gluon));
    std::cout << "  R_Thigh→Hips gluon added" << std::endl;

    Particle abdomen = {};
    abdomen.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    abdomen.shape = ParticleShape::BOX;
    abdomen.width = abdomen_width; abdomen.height = abdomen_depth; abdomen.thickness = abdomen_thickness;
    abdomen.r = clothing_r; abdomen.g = clothing_g + 0.1f; abdomen.b = clothing_b + 0.2f; abdomen.a = 1.0f;
    abdomen.material_strength = 50e6f;

    auto abdomen_gluon = std::make_unique<NailGluon>();
    abdomen_gluon->offset_a = Vec3(0.0f, 0.0f, hips_thickness / 2.0f);       // Hips top
    abdomen_gluon->offset_b = Vec3(0.0f, 0.0f, -abdomen_thickness / 2.0f);   // Abdomen bottom
    abdomen_gluon->target_distance = 0.0f;
    abdomen_gluon->breaking_force = 150000.0f;
    // Angular constraint: Spine joint (reduced 2x)
    abdomen_gluon->angular_stiffness = 100.0f;
    abdomen_gluon->angular_damping = 5.0f;
    abdomen_gluon->max_relative_rotation = 0.26f;  // ±15° - lower spine follows
    int abdomen_id = physics.add_particle_with_gluon_to(hips_id, abdomen, std::move(abdomen_gluon));
    result.body_ids.push_back(abdomen_id);
    std::cout << "  Abdomen: id=" << abdomen_id << std::endl;

    Particle chest = {};
    chest.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    chest.shape = ParticleShape::BOX;
    chest.width = chest_width; chest.height = chest_depth; chest.thickness = chest_thickness;
    chest.r = clothing_r; chest.g = clothing_g + 0.1f; chest.b = clothing_b + 0.2f; chest.a = 1.0f;
    chest.material_strength = 50e6f;
    // Emit living flesh scent - undead creatures can smell this
    chest.odor_type = OdorType::LIVING_FLESH;
    chest.odor_radius = 15.0f;
    chest.odor_intensity = 1.0f;  // Full strength smell

    auto chest_gluon = std::make_unique<NailGluon>();
    chest_gluon->offset_a = Vec3(0.0f, 0.0f, abdomen_thickness / 2.0f);   // Abdomen top
    chest_gluon->offset_b = Vec3(0.0f, 0.0f, -chest_thickness / 2.0f);    // Chest bottom
    chest_gluon->target_distance = 0.0f;
    chest_gluon->breaking_force = 150000.0f;
    // Angular constraint: Spine joint (reduced 2x)
    chest_gluon->angular_stiffness = 100.0f;
    chest_gluon->angular_damping = 5.0f;
    chest_gluon->max_relative_rotation = 0.35f;  // ±20° - chest follows after neck limit
    int chest_id = physics.add_particle_with_gluon_to(abdomen_id, chest, std::move(chest_gluon));
    result.body_ids.push_back(chest_id);
    std::cout << "  Chest: id=" << chest_id << std::endl;

    // ========================================================================
    // NECK
    // ========================================================================
    Particle neck = {};
    neck.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    neck.shape = ParticleShape::BOX;
    neck.width = neck_width; neck.height = neck_width; neck.thickness = neck_thickness;
    neck.r = skin_r; neck.g = skin_g; neck.b = skin_b; neck.a = 1.0f;
    neck.material_strength = 50e6f;

    auto neck_gluon = std::make_unique<NailGluon>();
    neck_gluon->offset_a = Vec3(0.0f, 0.0f, chest_thickness / 2.0f);    // Chest top
    neck_gluon->offset_b = Vec3(0.0f, 0.0f, -neck_thickness / 2.0f);    // Neck bottom
    neck_gluon->target_distance = 0.0f;
    neck_gluon->breaking_force = 40000.0f;
    neck_gluon->angular_stiffness = 25.0f;   // Loose (reduced 2x)
    neck_gluon->angular_damping = 1.0f;
    neck_gluon->max_relative_rotation = 0.52f;  // ±30° - neck follows head
    int neck_id = physics.add_particle_with_gluon_to(chest_id, neck, std::move(neck_gluon));
    result.body_ids.push_back(neck_id);
    std::cout << "  Neck: id=" << neck_id << std::endl;

    // ========================================================================
    // HEAD
    // ========================================================================
    Particle head = {};
    head.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    head.shape = ParticleShape::BOX;
    head.width = head_width; head.height = head_depth; head.thickness = head_thickness;
    head.r = skin_r; head.g = skin_g; head.b = skin_b; head.a = 1.0f;
    head.material_strength = 50e6f;
    // Emit living flesh scent - undead creatures can smell this
    head.odor_type = OdorType::LIVING_FLESH;
    head.odor_radius = 15.0f;
    head.odor_intensity = 1.0f;  // Full strength smell

    auto head_gluon = std::make_unique<NailGluon>();
    head_gluon->offset_a = Vec3(0.0f, 0.0f, neck_thickness / 2.0f);     // Neck top
    head_gluon->offset_b = Vec3(0.0f, 0.0f, -head_thickness / 2.0f);    // Head bottom
    head_gluon->target_distance = 0.0f;
    head_gluon->breaking_force = 50000.0f;
    head_gluon->angular_stiffness = 50.0f;  // Medium (reduced 2x)
    head_gluon->angular_damping = 2.0f;
    head_gluon->max_relative_rotation = 0.785f;  // ±45° - head turns independently until limit
    int head_id = physics.add_particle_with_gluon_to(neck_id, head, std::move(head_gluon));
    result.body_ids.push_back(head_id);
    result.head_id = head_id;
    std::cout << "  Head: id=" << head_id << std::endl;

    // ========================================================================
    // HAIR (flat particles on head)
    // ========================================================================
    const float hair_r = 0.3f, hair_g = 0.2f, hair_b = 0.1f;  // Brown

    // Upper hair (on top of head)
    Particle upper_hair = {};
    upper_hair.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    upper_hair.shape = ParticleShape::BOX;
    upper_hair.width = upper_hair_width; upper_hair.height = upper_hair_width; upper_hair.thickness = upper_hair_thickness;
    upper_hair.r = hair_r; upper_hair.g = hair_g; upper_hair.b = hair_b; upper_hair.a = 1.0f;
    upper_hair.material_strength = 50e6f;

    auto upper_hair_gluon = std::make_unique<NailGluon>();
    upper_hair_gluon->offset_a = Vec3(0.0f, 0.0f, head_thickness / 2.0f);          // Head top
    upper_hair_gluon->offset_b = Vec3(0.0f, 0.0f, -upper_hair_thickness / 2.0f);   // Hair bottom
    upper_hair_gluon->target_distance = 0.0f;
    upper_hair_gluon->breaking_force = 5000.0f;
    upper_hair_gluon->enable_angular_constraint = false;  // Hair swings freely
    int upper_hair_id = physics.add_particle_with_gluon_to(head_id, upper_hair, std::move(upper_hair_gluon));
    result.body_ids.push_back(upper_hair_id);
    result.upper_hair_id = upper_hair_id;
    std::cout << "  UpperHair: id=" << upper_hair_id << std::endl;

    // Back hair (behind head)
    Particle back_hair = {};
    back_hair.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    back_hair.shape = ParticleShape::BOX;
    back_hair.width = back_hair_width; back_hair.height = 0.05f; back_hair.thickness = back_hair_length;
    back_hair.r = hair_r; back_hair.g = hair_g; back_hair.b = hair_b; back_hair.a = 1.0f;
    back_hair.material_strength = 50e6f;

    auto back_hair_gluon = std::make_unique<NailGluon>();
    back_hair_gluon->offset_a = Vec3(0.0f, -head_depth / 2.0f, 0.0f);   // Head back
    back_hair_gluon->offset_b = Vec3(0.0f, 0.025f, 0.0f);               // Hair front
    back_hair_gluon->target_distance = 0.0f;
    back_hair_gluon->breaking_force = 5000.0f;
    back_hair_gluon->enable_angular_constraint = false;  // Ponytail swings freely
    int back_hair_id = physics.add_particle_with_gluon_to(head_id, back_hair, std::move(back_hair_gluon));
    result.body_ids.push_back(back_hair_id);
    result.back_hair_id = back_hair_id;
    std::cout << "  BackHair: id=" << back_hair_id << std::endl;

    // ========================================================================
    // EARS (small cubes on sides of head)
    // ========================================================================
    // ear_size is derived from spec.head_size at the top of this function

    // Left ear
    Particle l_ear = {};
    l_ear.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_ear.shape = ParticleShape::BOX;
    l_ear.width = ear_size; l_ear.height = ear_size; l_ear.thickness = ear_size;
    l_ear.r = skin_r; l_ear.g = skin_g; l_ear.b = skin_b; l_ear.a = 1.0f;
    l_ear.material_strength = 50e6f;

    auto l_ear_gluon = std::make_unique<NailGluon>();
    l_ear_gluon->offset_a = Vec3(-head_width / 2.0f, 0.0f, 0.0f);   // Head left side
    l_ear_gluon->offset_b = Vec3(ear_size / 2.0f, 0.0f, 0.0f);      // Ear right side
    l_ear_gluon->target_distance = 0.0f;
    l_ear_gluon->breaking_force = 3000.0f;
    l_ear_gluon->enable_angular_constraint = false;  // Ears swing freely
    int l_ear_id = physics.add_particle_with_gluon_to(head_id, l_ear, std::move(l_ear_gluon));
    result.body_ids.push_back(l_ear_id);
    std::cout << "  L_Ear: id=" << l_ear_id << std::endl;

    // Right ear
    Particle r_ear = {};
    r_ear.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_ear.shape = ParticleShape::BOX;
    r_ear.width = ear_size; r_ear.height = ear_size; r_ear.thickness = ear_size;
    r_ear.r = skin_r; r_ear.g = skin_g; r_ear.b = skin_b; r_ear.a = 1.0f;
    r_ear.material_strength = 50e6f;

    auto r_ear_gluon = std::make_unique<NailGluon>();
    r_ear_gluon->offset_a = Vec3(head_width / 2.0f, 0.0f, 0.0f);    // Head right side
    r_ear_gluon->offset_b = Vec3(-ear_size / 2.0f, 0.0f, 0.0f);     // Ear left side
    r_ear_gluon->target_distance = 0.0f;
    r_ear_gluon->breaking_force = 3000.0f;
    r_ear_gluon->enable_angular_constraint = false;  // Ears swing freely
    int r_ear_id = physics.add_particle_with_gluon_to(head_id, r_ear, std::move(r_ear_gluon));
    result.body_ids.push_back(r_ear_id);
    std::cout << "  R_Ear: id=" << r_ear_id << std::endl;

    // ========================================================================
    // EYES (optional - only if spec.has_eyes is true)
    // Each eye = outer socket (dark) + inner pupil (glowing), both attached to head
    // ========================================================================
    if (spec.has_eyes) {
        const float eye_outer_size = spec.eye_size;
        const float eye_inner_size = spec.eye_size * spec.eye_pupil_scale;
        // THE PLATES ARE THICKER THAN THE ENGINE'S OWN BOUNDARY EPSILON.
        // The socket was 2 mm and the pupil 1 mm, both at or under
        // BOUNDARY_EPSILON (0.002 m), which is the scale below which the
        // box-box SAT reads two stacked plates as "aligned" and answers with
        // the LATERAL overlap instead of the zero it has in depth: a pupil
        // resting exactly on its socket was refused at 14 mm (INV-37 through
        // INV-12's narrow phase). A body thinner than the epsilon that
        // resolves it cannot be placed unambiguously, so the socket is 6 mm
        // deep and the pupil 2 mm - still plates, now representable.
        const float eye_outer_depth = PhysicsV4::BOUNDARY_EPSILON * 3.0f;   // 6 mm
        const float eye_inner_depth = PhysicsV4::BOUNDARY_EPSILON;          // 2 mm
        const float eye_half_spacing = spec.eye_spacing / 2.0f;
        const float eye_forward = head_depth / 2.0f + spec.eye_forward_offset;  // Front of head
        const float eye_height = spec.eye_height_offset;

        // LEFT EYE - OUTER (socket) - very flat plate on face
        Particle l_eye_outer = {};
        l_eye_outer.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
        l_eye_outer.shape = ParticleShape::BOX;
        l_eye_outer.width = eye_outer_size; l_eye_outer.height = eye_outer_depth; l_eye_outer.thickness = eye_outer_size;
        l_eye_outer.r = spec.eye_outer_r; l_eye_outer.g = spec.eye_outer_g; l_eye_outer.b = spec.eye_outer_b; l_eye_outer.a = 1.0f;
        l_eye_outer.material_strength = 50e6f;

        auto l_eye_outer_gluon = std::make_unique<NailGluon>();
        l_eye_outer_gluon->offset_a = Vec3(-eye_half_spacing, eye_forward, eye_height);  // On face
        l_eye_outer_gluon->offset_b = Vec3(0.0f, -eye_outer_depth * 0.5f, 0.0f);         // Back of the socket plate
        l_eye_outer_gluon->target_distance = 0.0f;
        l_eye_outer_gluon->breaking_force = 50000.0f;  // Match other body parts
        l_eye_outer_gluon->enable_angular_constraint = false;
        int l_eye_outer_id = physics.add_particle_with_gluon_to(head_id, l_eye_outer, std::move(l_eye_outer_gluon));
        result.body_ids.push_back(l_eye_outer_id);
        result.left_eye_outer_id = l_eye_outer_id;
        std::cout << "  L_Eye_Outer: id=" << l_eye_outer_id << std::endl;

        // LEFT EYE - INNER (pupil) - very flat, on outer eye
        Particle l_eye_inner = {};
        l_eye_inner.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
        l_eye_inner.shape = ParticleShape::BOX;
        l_eye_inner.width = eye_inner_size; l_eye_inner.height = eye_inner_depth; l_eye_inner.thickness = eye_inner_size;
        l_eye_inner.r = spec.eye_inner_r; l_eye_inner.g = spec.eye_inner_g; l_eye_inner.b = spec.eye_inner_b; l_eye_inner.a = 1.0f;
        l_eye_inner.material_strength = 50e6f;

        auto l_eye_inner_gluon = std::make_unique<NailGluon>();
        l_eye_inner_gluon->offset_a = Vec3(0.0f, eye_outer_depth * 0.5f, 0.0f);   // Front of the socket
        l_eye_inner_gluon->offset_b = Vec3(0.0f, -eye_inner_depth * 0.5f, 0.0f);  // Back of the pupil
        l_eye_inner_gluon->target_distance = 0.0f;
        l_eye_inner_gluon->breaking_force = 50000.0f;  // Match other body parts
        l_eye_inner_gluon->enable_angular_constraint = false;
        int l_eye_inner_id = physics.add_particle_with_gluon_to(l_eye_outer_id, l_eye_inner, std::move(l_eye_inner_gluon));
        result.body_ids.push_back(l_eye_inner_id);
        result.left_eye_inner_id = l_eye_inner_id;
        std::cout << "  L_Eye_Inner: id=" << l_eye_inner_id << std::endl;

        // RIGHT EYE - OUTER (socket) - very flat plate on face
        Particle r_eye_outer = {};
        r_eye_outer.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
        r_eye_outer.shape = ParticleShape::BOX;
        r_eye_outer.width = eye_outer_size; r_eye_outer.height = eye_outer_depth; r_eye_outer.thickness = eye_outer_size;
        r_eye_outer.r = spec.eye_outer_r; r_eye_outer.g = spec.eye_outer_g; r_eye_outer.b = spec.eye_outer_b; r_eye_outer.a = 1.0f;
        r_eye_outer.material_strength = 50e6f;

        auto r_eye_outer_gluon = std::make_unique<NailGluon>();
        r_eye_outer_gluon->offset_a = Vec3(eye_half_spacing, eye_forward, eye_height);   // On face
        r_eye_outer_gluon->offset_b = Vec3(0.0f, -eye_outer_depth * 0.5f, 0.0f);         // Back of the socket plate
        r_eye_outer_gluon->target_distance = 0.0f;
        r_eye_outer_gluon->breaking_force = 50000.0f;  // Match other body parts
        r_eye_outer_gluon->enable_angular_constraint = false;
        int r_eye_outer_id = physics.add_particle_with_gluon_to(head_id, r_eye_outer, std::move(r_eye_outer_gluon));
        result.body_ids.push_back(r_eye_outer_id);
        result.right_eye_outer_id = r_eye_outer_id;
        std::cout << "  R_Eye_Outer: id=" << r_eye_outer_id << std::endl;

        // RIGHT EYE - INNER (pupil) - very flat, on outer eye
        Particle r_eye_inner = {};
        r_eye_inner.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
        r_eye_inner.shape = ParticleShape::BOX;
        r_eye_inner.width = eye_inner_size; r_eye_inner.height = eye_inner_depth; r_eye_inner.thickness = eye_inner_size;
        r_eye_inner.r = spec.eye_inner_r; r_eye_inner.g = spec.eye_inner_g; r_eye_inner.b = spec.eye_inner_b; r_eye_inner.a = 1.0f;
        r_eye_inner.material_strength = 50e6f;

        auto r_eye_inner_gluon = std::make_unique<NailGluon>();
        r_eye_inner_gluon->offset_a = Vec3(0.0f, eye_outer_depth * 0.5f, 0.0f);   // Front of the socket
        r_eye_inner_gluon->offset_b = Vec3(0.0f, -eye_inner_depth * 0.5f, 0.0f);  // Back of the pupil
        r_eye_inner_gluon->target_distance = 0.0f;
        r_eye_inner_gluon->breaking_force = 50000.0f;  // Match other body parts
        r_eye_inner_gluon->enable_angular_constraint = false;
        int r_eye_inner_id = physics.add_particle_with_gluon_to(r_eye_outer_id, r_eye_inner, std::move(r_eye_inner_gluon));
        result.body_ids.push_back(r_eye_inner_id);
        result.right_eye_inner_id = r_eye_inner_id;
        std::cout << "  R_Eye_Inner: id=" << r_eye_inner_id << std::endl;
    }

    // Populate torso IDs (non-animated particles for stretch checking)
    // Indices 5+ become head_child_particles in dynamics system
    result.torso_ids = {hips_id, abdomen_id, chest_id, neck_id, head_id,
                        upper_hair_id, back_hair_id, l_ear_id, r_ear_id};

    // Add eyes to torso_ids if present (so they become head_children and move with head)
    if (spec.has_eyes) {
        result.torso_ids.push_back(result.left_eye_outer_id);
        result.torso_ids.push_back(result.left_eye_inner_id);
        result.torso_ids.push_back(result.right_eye_outer_id);
        result.torso_ids.push_back(result.right_eye_inner_id);
    }

    // ========================================================================
    // SHOULDERS (cube joints at top of chest)
    // ========================================================================
    // shoulder_size is derived from spec.arm_thickness at the top of this function

    // LEFT SHOULDER
    Particle l_shoulder = {};
    l_shoulder.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_shoulder.shape = ParticleShape::BOX;
    l_shoulder.width = shoulder_size; l_shoulder.height = shoulder_size; l_shoulder.thickness = shoulder_size;
    l_shoulder.r = skin_r; l_shoulder.g = skin_g; l_shoulder.b = skin_b; l_shoulder.a = 1.0f;
    l_shoulder.material_strength = 50e6f;

    auto l_shoulder_gluon = std::make_unique<NailGluon>();
    l_shoulder_gluon->offset_a = Vec3(-ARM_SPREAD, 0.0f, chest_thickness / 2.0f);  // Chest top left
    l_shoulder_gluon->offset_b = Vec3(0.0f, 0.0f, -shoulder_size / 2.0f);          // Shoulder bottom
    l_shoulder_gluon->target_distance = 0.0f;
    l_shoulder_gluon->breaking_force = 50000.0f;
    // rotate_offsets=true: When chest rotates, the attachment point (offset_a)
    // rotates WITH the chest. This causes the shoulder to orbit around the chest,
    // keeping the arm attached even as the torso turns. Without this, the shoulder
    // stays fixed in world space while the chest rotates away.
    l_shoulder_gluon->rotate_offsets = true;
    l_shoulder_gluon->angular_stiffness = 50.0f;  // Loose (reduced 2x)
    l_shoulder_gluon->angular_damping = 2.0f;
    l_shoulder_gluon->max_relative_rotation = 1.05f;  // ±60° - arms swing freely
    int l_shoulder_id = physics.add_particle_with_gluon_to(chest_id, l_shoulder, std::move(l_shoulder_gluon));
    result.body_ids.push_back(l_shoulder_id);
    std::cout << "  L_Shoulder: id=" << l_shoulder_id << std::endl;

    // RIGHT SHOULDER
    Particle r_shoulder = {};
    r_shoulder.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_shoulder.shape = ParticleShape::BOX;
    r_shoulder.width = shoulder_size; r_shoulder.height = shoulder_size; r_shoulder.thickness = shoulder_size;
    r_shoulder.r = skin_r; r_shoulder.g = skin_g; r_shoulder.b = skin_b; r_shoulder.a = 1.0f;
    r_shoulder.material_strength = 50e6f;

    auto r_shoulder_gluon = std::make_unique<NailGluon>();
    r_shoulder_gluon->offset_a = Vec3(ARM_SPREAD, 0.0f, chest_thickness / 2.0f);   // Chest top right
    r_shoulder_gluon->offset_b = Vec3(0.0f, 0.0f, -shoulder_size / 2.0f);          // Shoulder bottom
    r_shoulder_gluon->target_distance = 0.0f;
    r_shoulder_gluon->breaking_force = 50000.0f;
    // rotate_offsets=true: Same logic as left shoulder - attachment orbits with chest
    r_shoulder_gluon->rotate_offsets = true;
    r_shoulder_gluon->angular_stiffness = 50.0f;  // Loose (reduced 2x)
    r_shoulder_gluon->angular_damping = 2.0f;
    r_shoulder_gluon->max_relative_rotation = 1.05f;  // ±60° - arms swing freely
    int r_shoulder_id = physics.add_particle_with_gluon_to(chest_id, r_shoulder, std::move(r_shoulder_gluon));
    result.body_ids.push_back(r_shoulder_id);
    std::cout << "  R_Shoulder: id=" << r_shoulder_id << std::endl;

    // ========================================================================
    // LEFT ARM: shoulder → upper_arm → forearm → hand
    // ========================================================================
    Particle l_upper_arm = {};
    l_upper_arm.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_upper_arm.shape = ParticleShape::BOX;
    l_upper_arm.width = upper_arm_width; l_upper_arm.height = upper_arm_width; l_upper_arm.thickness = upper_arm_length;
    l_upper_arm.r = clothing_r; l_upper_arm.g = clothing_g + 0.1f; l_upper_arm.b = clothing_b + 0.2f; l_upper_arm.a = 1.0f;
    l_upper_arm.material_strength = 50e6f;

    auto l_upper_gluon = std::make_unique<NailGluon>();
    // THE ARM HANGS FROM THE SHOULDER'S UNDERSIDE, NOT THROUGH ITS SIDE.
    // The old pivot was the shoulder's lateral face at mid-height, which put
    // the arm's top half INSIDE the shoulder cube: 22.5 mm for Eva, 30 for
    // the default spec, 37.5 for the hunter, on every humanoid the engine has
    // ever made. Under INV-37 that is a refused birth, not a modelling
    // choice. The arm's top face now sits on the shoulder's bottom face and
    // the joint pivots there, which is also where a shoulder joint is.
    l_upper_gluon->offset_a = Vec3(-arm_lateral_clearance, 0.0f,
                                   -shoulder_size / 2.0f);                  // Shoulder underside, outboard of the chest
    l_upper_gluon->offset_b = Vec3(0.0f, 0.0f, upper_arm_length / 2.0f);    // Upper arm top
    l_upper_gluon->target_distance = 0.0f;
    l_upper_gluon->breaking_force = 30000.0f;
    l_upper_gluon->angular_stiffness = 50.0f;  // Shoulder (reduced 2x)
    l_upper_gluon->angular_damping = 2.0f;
    l_upper_gluon->max_relative_rotation = 0.785f;  // ±45° - upper arm swings
    int l_upper_arm_id = physics.add_particle_with_gluon_to(l_shoulder_id, l_upper_arm, std::move(l_upper_gluon));
    result.body_ids.push_back(l_upper_arm_id);
    std::cout << "  L_UpperArm: id=" << l_upper_arm_id << std::endl;

    Particle l_forearm = {};
    l_forearm.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_forearm.shape = ParticleShape::BOX;
    l_forearm.width = forearm_width; l_forearm.height = forearm_width; l_forearm.thickness = forearm_length;
    l_forearm.r = skin_r; l_forearm.g = skin_g; l_forearm.b = skin_b; l_forearm.a = 1.0f;
    l_forearm.material_strength = 50e6f;

    auto l_forearm_gluon = std::make_unique<NailGluon>();
    l_forearm_gluon->offset_a = Vec3(0.0f, 0.0f, -upper_arm_length / 2.0f);  // Upper arm bottom
    l_forearm_gluon->offset_b = Vec3(0.0f, 0.0f, forearm_length / 2.0f);     // Forearm top
    l_forearm_gluon->target_distance = 0.0f;
    l_forearm_gluon->breaking_force = 20000.0f;
    l_forearm_gluon->angular_stiffness = 40.0f;  // Elbow (reduced 2x)
    l_forearm_gluon->angular_damping = 10.0f;
    l_forearm_gluon->max_relative_rotation = 0.52f;  // ±30° - elbow fairly rigid
    int l_forearm_id = physics.add_particle_with_gluon_to(l_upper_arm_id, l_forearm, std::move(l_forearm_gluon));
    result.body_ids.push_back(l_forearm_id);
    std::cout << "  L_Forearm: id=" << l_forearm_id << std::endl;

    // LEFT HAND
    Particle l_hand = {};
    l_hand.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    l_hand.shape = ParticleShape::BOX;
    l_hand.width = hand_width; l_hand.height = hand_depth; l_hand.thickness = hand_thickness;
    l_hand.r = skin_r; l_hand.g = skin_g; l_hand.b = skin_b; l_hand.a = 1.0f;
    l_hand.material_strength = 50e6f;

    auto l_hand_gluon = std::make_unique<NailGluon>();
    l_hand_gluon->offset_a = Vec3(0.0f, 0.0f, -forearm_length / 2.0f);  // Forearm bottom
    l_hand_gluon->offset_b = Vec3(0.0f, 0.0f, hand_thickness / 2.0f);   // Hand top
    l_hand_gluon->target_distance = 0.0f;
    l_hand_gluon->breaking_force = 10000.0f;
    l_hand_gluon->angular_stiffness = 150.0f;  // Wrist (reduced 2x)
    l_hand_gluon->angular_damping = 8.0f;
    int l_hand_id = physics.add_particle_with_gluon_to(l_forearm_id, l_hand, std::move(l_hand_gluon));
    result.body_ids.push_back(l_hand_id);
    std::cout << "  L_Hand: id=" << l_hand_id << std::endl;

    // Populate left arm IDs for animation
    result.left_arm_ids = {l_shoulder_id, l_upper_arm_id, l_forearm_id, l_hand_id};

    // ========================================================================
    // RIGHT ARM: shoulder → upper_arm → forearm → hand
    // ========================================================================
    Particle r_upper_arm = {};
    r_upper_arm.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_upper_arm.shape = ParticleShape::BOX;
    r_upper_arm.width = upper_arm_width; r_upper_arm.height = upper_arm_width; r_upper_arm.thickness = upper_arm_length;
    r_upper_arm.r = clothing_r; r_upper_arm.g = clothing_g + 0.1f; r_upper_arm.b = clothing_b + 0.2f; r_upper_arm.a = 1.0f;
    r_upper_arm.material_strength = 50e6f;

    auto r_upper_gluon = std::make_unique<NailGluon>();
    // Mirror of the left: the arm's top face on the shoulder's underside.
    // See the note on l_upper_gluon for what the lateral pivot cost (INV-37).
    r_upper_gluon->offset_a = Vec3(arm_lateral_clearance, 0.0f,
                                   -shoulder_size / 2.0f);                  // Shoulder underside, outboard of the chest
    r_upper_gluon->offset_b = Vec3(0.0f, 0.0f, upper_arm_length / 2.0f);    // Upper arm top
    std::cout << "[GLUON_CREATE] r_upper_gluon: shoulder_size=" << shoulder_size
              << " upper_arm_length=" << upper_arm_length
              << " offset_a=(" << r_upper_gluon->offset_a.x << "," << r_upper_gluon->offset_a.y << "," << r_upper_gluon->offset_a.z << ")"
              << " offset_b.z=" << r_upper_gluon->offset_b.z << std::endl;
    r_upper_gluon->target_distance = 0.0f;
    r_upper_gluon->breaking_force = 30000.0f;
    r_upper_gluon->angular_stiffness = 50.0f;  // Shoulder (reduced 2x)
    r_upper_gluon->angular_damping = 2.0f;
    r_upper_gluon->max_relative_rotation = 0.785f;  // ±45° - upper arm swings
    int r_upper_arm_id = physics.add_particle_with_gluon_to(r_shoulder_id, r_upper_arm, std::move(r_upper_gluon));
    result.body_ids.push_back(r_upper_arm_id);
    std::cout << "  R_UpperArm: id=" << r_upper_arm_id << std::endl;

    Particle r_forearm = {};
    r_forearm.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_forearm.shape = ParticleShape::BOX;
    r_forearm.width = forearm_width; r_forearm.height = forearm_width; r_forearm.thickness = forearm_length;
    r_forearm.r = skin_r; r_forearm.g = skin_g; r_forearm.b = skin_b; r_forearm.a = 1.0f;
    r_forearm.material_strength = 50e6f;

    auto r_forearm_gluon = std::make_unique<NailGluon>();
    r_forearm_gluon->offset_a = Vec3(0.0f, 0.0f, -upper_arm_length / 2.0f);  // Upper arm bottom
    r_forearm_gluon->offset_b = Vec3(0.0f, 0.0f, forearm_length / 2.0f);     // Forearm top
    r_forearm_gluon->target_distance = 0.0f;
    r_forearm_gluon->breaking_force = 20000.0f;
    r_forearm_gluon->angular_stiffness = 40.0f;  // Elbow (reduced 2x)
    r_forearm_gluon->angular_damping = 10.0f;
    r_forearm_gluon->max_relative_rotation = 0.52f;  // ±30° - elbow fairly rigid
    int r_forearm_id = physics.add_particle_with_gluon_to(r_upper_arm_id, r_forearm, std::move(r_forearm_gluon));
    result.body_ids.push_back(r_forearm_id);
    std::cout << "  R_Forearm: id=" << r_forearm_id << std::endl;

    // RIGHT HAND
    Particle r_hand = {};
    r_hand.SetMaterial(Materials::Type::FLESH);   // INV-38: a humanoid is flesh, SET, not inherited
    r_hand.shape = ParticleShape::BOX;
    r_hand.width = hand_width; r_hand.height = hand_depth; r_hand.thickness = hand_thickness;
    r_hand.r = skin_r; r_hand.g = skin_g; r_hand.b = skin_b; r_hand.a = 1.0f;
    r_hand.material_strength = 50e6f;

    auto r_hand_gluon = std::make_unique<NailGluon>();
    r_hand_gluon->offset_a = Vec3(0.0f, 0.0f, -forearm_length / 2.0f);  // Forearm bottom
    r_hand_gluon->offset_b = Vec3(0.0f, 0.0f, hand_thickness / 2.0f);   // Hand top
    r_hand_gluon->target_distance = 0.0f;
    r_hand_gluon->breaking_force = 10000.0f;
    r_hand_gluon->angular_stiffness = 150.0f;  // Wrist (reduced 2x)
    r_hand_gluon->angular_damping = 8.0f;
    int r_hand_id = physics.add_particle_with_gluon_to(r_forearm_id, r_hand, std::move(r_hand_gluon));
    result.body_ids.push_back(r_hand_id);
    std::cout << "  R_Hand: id=" << r_hand_id << std::endl;

    // Populate right arm IDs for animation
    result.right_arm_ids = {r_shoulder_id, r_upper_arm_id, r_forearm_id, r_hand_id};

    std::cout << "[PHYSICS_HUMANOID] Created " << result.body_ids.size() << " body particles" << std::endl;

    // Bind all particles to KG entity if kg_support enabled
    if (kg_support && entity_id != kg::INVALID_ENTITY) {
        auto& kg = engine_->get_kg();
        for (int pid : result.body_ids) {
            kg.createKGParticle(entity_id, static_cast<kg::RenderIndex>(pid));
        }
        std::cout << "[PHYSICS_HUMANOID] Bound " << result.body_ids.size() << " particles to KG entity " << entity_id << std::endl;
    }

    // Apply initial facing angle if specified
    if (spec.facing_angle != 0.0f) {
        auto particles = ps.lock_particles_for_write();
        float cos_a = std::cos(spec.facing_angle);
        float sin_a = std::sin(spec.facing_angle);

        for (int body_id : result.body_ids) {
            Particle& p = particles[body_id];

            // Rotate position around spawn point (world_x, world_y)
            float rel_x = p.x - world_x;
            float rel_y = p.y - world_y;
            p.x = world_x + rel_x * cos_a - rel_y * sin_a;
            p.y = world_y + rel_x * sin_a + rel_y * cos_a;

            // Set visual rotation
            p.rotation_z = spec.facing_angle;
        }
    }

    return result;
}

// ============================================================================
// JOINT DERIVATION (FK Animation Support)
// ============================================================================
// Derives joints from the actual created structure. Missing limbs = no joints.

void PhysicsHumanoidResult::register_joints(
        logosphere::animation::HumanoidLocomotion* locomotion) const {
    if (!locomotion || entity_id == kg::INVALID_ENTITY) return;

    using Joint = logosphere::animation::Joint;

    // ========================================================================
    // SPINE JOINTS - registered BEFORE arms (parents before children for FK)
    // ========================================================================
    // torso_ids: [0]=hips, [1]=abdomen, [2]=chest, [3]=neck, [4]=head
    // Spine FK cascades: hips → abdomen → chest → neck → head
    // Then bridge joints propagate chest rotation into shoulder FK chains

    if (torso_ids.size() >= 5) {
        // lower_spine: hips → abdomen
        {
            Joint j;
            j.name = "lower_spine";
            j.parent_particle = static_cast<unsigned int>(torso_ids[0]);  // hips
            j.child_particle = static_cast<unsigned int>(torso_ids[1]);   // abdomen
            j.definition = &logosphere::LOWER_SPINE;
            locomotion->register_joint(entity_id, j);
        }
        // upper_spine: abdomen → chest
        {
            Joint j;
            j.name = "upper_spine";
            j.parent_particle = static_cast<unsigned int>(torso_ids[1]);  // abdomen
            j.child_particle = static_cast<unsigned int>(torso_ids[2]);   // chest
            j.definition = &logosphere::UPPER_SPINE;
            locomotion->register_joint(entity_id, j);
        }
        // neck: chest → neck
        {
            Joint j;
            j.name = "neck";
            j.parent_particle = static_cast<unsigned int>(torso_ids[2]);  // chest
            j.child_particle = static_cast<unsigned int>(torso_ids[3]);   // neck
            j.definition = &logosphere::NECK_JOINT;
            locomotion->register_joint(entity_id, j);
        }
        // head: neck → head
        {
            Joint j;
            j.name = "Head";
            j.parent_particle = static_cast<unsigned int>(torso_ids[3]);  // neck
            j.child_particle = static_cast<unsigned int>(torso_ids[4]);   // head
            j.definition = &logosphere::HEAD_JOINT;
            locomotion->register_joint(entity_id, j);
        }

        // Bridge joints: chest → shoulder particles (FIXED, 0-DOF)
        // Lets torso torsion propagate into arm FK chain
        if (right_arm_ids.size() >= 1) {
            Joint j;
            j.name = "right_chest_shoulder";
            j.parent_particle = static_cast<unsigned int>(torso_ids[2]);     // chest
            j.child_particle = static_cast<unsigned int>(right_arm_ids[0]);  // right shoulder
            j.definition = &logosphere::FIXED_JOINT;
            locomotion->register_joint(entity_id, j);
        }
        if (left_arm_ids.size() >= 1) {
            Joint j;
            j.name = "left_chest_shoulder";
            j.parent_particle = static_cast<unsigned int>(torso_ids[2]);    // chest
            j.child_particle = static_cast<unsigned int>(left_arm_ids[0]); // left shoulder
            j.definition = &logosphere::FIXED_JOINT;
            locomotion->register_joint(entity_id, j);
        }

        std::cout << "[PhysicsHumanoidResult] Registered 4 spine joints + "
                  << (right_arm_ids.size() >= 1 ? 1 : 0) + (left_arm_ids.size() >= 1 ? 1 : 0)
                  << " bridge joints" << std::endl;
    }

    // ========================================================================
    // ARM JOINTS - with anatomical joint definitions
    // ========================================================================
    // Order: [shoulder, upper_arm, forearm, hand]
    // Each joint gets a JointDefinition that defines semantic axes for flex/abduct/twist

    // Right arm joints
    if (right_arm_ids.size() >= 2) {
        Joint j;
        j.name = "right_shoulder";
        j.parent_particle = static_cast<unsigned int>(right_arm_ids[0]);
        j.child_particle = static_cast<unsigned int>(right_arm_ids[1]);
        j.definition = &logosphere::SHOULDER_RIGHT;  // Ball-socket: flex + abduct + twist
        locomotion->register_joint(entity_id, j);
    }
    if (right_arm_ids.size() >= 3) {
        Joint j;
        j.name = "right_elbow";
        j.parent_particle = static_cast<unsigned int>(right_arm_ids[1]);
        j.child_particle = static_cast<unsigned int>(right_arm_ids[2]);
        j.definition = &logosphere::ELBOW_RIGHT;  // Hinge: flex only
        locomotion->register_joint(entity_id, j);
    }
    if (right_arm_ids.size() >= 4) {
        Joint j;
        j.name = "right_wrist";
        j.parent_particle = static_cast<unsigned int>(right_arm_ids[2]);
        j.child_particle = static_cast<unsigned int>(right_arm_ids[3]);
        j.definition = &logosphere::WRIST_RIGHT;  // Hinge: flex only
        locomotion->register_joint(entity_id, j);
    }

    // Left arm joints
    if (left_arm_ids.size() >= 2) {
        Joint j;
        j.name = "left_shoulder";
        j.parent_particle = static_cast<unsigned int>(left_arm_ids[0]);
        j.child_particle = static_cast<unsigned int>(left_arm_ids[1]);
        j.definition = &logosphere::SHOULDER_LEFT;
        locomotion->register_joint(entity_id, j);
    }
    if (left_arm_ids.size() >= 3) {
        Joint j;
        j.name = "left_elbow";
        j.parent_particle = static_cast<unsigned int>(left_arm_ids[1]);
        j.child_particle = static_cast<unsigned int>(left_arm_ids[2]);
        j.definition = &logosphere::ELBOW_LEFT;
        locomotion->register_joint(entity_id, j);
    }
    if (left_arm_ids.size() >= 4) {
        Joint j;
        j.name = "left_wrist";
        j.parent_particle = static_cast<unsigned int>(left_arm_ids[2]);
        j.child_particle = static_cast<unsigned int>(left_arm_ids[3]);
        j.definition = &logosphere::WRIST_LEFT;
        locomotion->register_joint(entity_id, j);
    }

    // ========================================================================
    // LEG JOINTS - with anatomical joint definitions
    // ========================================================================
    // Leg IDs order: [foot(0), shin(1), thigh(2)] - bottom-up

    // Right leg joints
    if (right_leg_ids.size() >= 3 && hips_id >= 0) {
        Joint j;
        j.name = "right_hip";
        j.parent_particle = static_cast<unsigned int>(hips_id);
        j.child_particle = static_cast<unsigned int>(right_leg_ids[2]);  // thigh
        j.definition = &logosphere::HIP_RIGHT;  // Ball-socket: flex + abduct + twist
        locomotion->register_joint(entity_id, j);
    }
    if (right_leg_ids.size() >= 2) {
        Joint j;
        j.name = "right_knee";
        j.parent_particle = static_cast<unsigned int>(right_leg_ids[2]);  // thigh
        j.child_particle = static_cast<unsigned int>(right_leg_ids[1]);   // shin
        j.definition = &logosphere::KNEE_RIGHT;  // Hinge: flex only
        locomotion->register_joint(entity_id, j);
    }
    if (right_leg_ids.size() >= 1) {
        Joint j;
        j.name = "right_ankle";
        j.parent_particle = static_cast<unsigned int>(right_leg_ids[1]);  // shin
        j.child_particle = static_cast<unsigned int>(right_leg_ids[0]);   // foot
        j.definition = &logosphere::ANKLE_RIGHT;  // Hinge: flex only
        locomotion->register_joint(entity_id, j);
    }
    if (right_leg_ids.size() >= 4) {
        Joint j;
        j.name = "right_toe";
        j.parent_particle = static_cast<unsigned int>(right_leg_ids[0]);  // foot
        j.child_particle = static_cast<unsigned int>(right_leg_ids[3]);   // toe
        j.definition = &logosphere::TOE_RIGHT;
        locomotion->register_joint(entity_id, j);
    }

    // Left leg joints
    if (left_leg_ids.size() >= 3 && hips_id >= 0) {
        Joint j;
        j.name = "left_hip";
        j.parent_particle = static_cast<unsigned int>(hips_id);
        j.child_particle = static_cast<unsigned int>(left_leg_ids[2]);  // thigh
        j.definition = &logosphere::HIP_LEFT;
        locomotion->register_joint(entity_id, j);
    }
    if (left_leg_ids.size() >= 2) {
        Joint j;
        j.name = "left_knee";
        j.parent_particle = static_cast<unsigned int>(left_leg_ids[2]);
        j.child_particle = static_cast<unsigned int>(left_leg_ids[1]);
        j.definition = &logosphere::KNEE_LEFT;
        locomotion->register_joint(entity_id, j);
    }
    if (left_leg_ids.size() >= 1) {
        Joint j;
        j.name = "left_ankle";
        j.parent_particle = static_cast<unsigned int>(left_leg_ids[1]);
        j.child_particle = static_cast<unsigned int>(left_leg_ids[0]);
        j.definition = &logosphere::ANKLE_LEFT;
        locomotion->register_joint(entity_id, j);
    }
    if (left_leg_ids.size() >= 4) {
        Joint j;
        j.name = "left_toe";
        j.parent_particle = static_cast<unsigned int>(left_leg_ids[0]);  // foot
        j.child_particle = static_cast<unsigned int>(left_leg_ids[3]);   // toe
        j.definition = &logosphere::TOE_LEFT;
        locomotion->register_joint(entity_id, j);
    }

    int arm_joints = (right_arm_ids.size() >= 2 ? 1 : 0) + (right_arm_ids.size() >= 3 ? 1 : 0) + (right_arm_ids.size() >= 4 ? 1 : 0)
                   + (left_arm_ids.size() >= 2 ? 1 : 0) + (left_arm_ids.size() >= 3 ? 1 : 0) + (left_arm_ids.size() >= 4 ? 1 : 0);
    int leg_joints = (right_leg_ids.size() >= 3 && hips_id >= 0 ? 1 : 0) + (right_leg_ids.size() >= 2 ? 1 : 0) + (right_leg_ids.size() >= 1 ? 1 : 0) + (right_leg_ids.size() >= 4 ? 1 : 0)
                   + (left_leg_ids.size() >= 3 && hips_id >= 0 ? 1 : 0) + (left_leg_ids.size() >= 2 ? 1 : 0) + (left_leg_ids.size() >= 1 ? 1 : 0) + (left_leg_ids.size() >= 4 ? 1 : 0);

    std::cout << "[PhysicsHumanoidResult] Registered " << arm_joints << " arm joints (with joint definitions), "
              << leg_joints << " leg joints (with joint definitions)" << std::endl;
}

// ============================================================================
// Create typed body part entities in KG with health, strength, flexibility.
// ============================================================================

void PhysicsHumanoidResult::create_kg_entities(
    kg::KGModule& kg, const std::string& entity_type,
    float reflexes_ms, float grit_W)
{
    // --- Root humanoid entity ---
    // generate_humanoid_physics already minted the root ("Humanoid",
    // owner of the locomotion joints). Build the body graph ON it —
    // minting a second root split ownership (joints on one entity,
    // parts on the other) and, for callers passing a type outside
    // the ontology ("Human"), silently replaced a valid root with
    // INVALID_ENTITY. entity_type applies only when no root exists.
    if (entity_id == kg::INVALID_ENTITY)
        entity_id = kg.createEntity(entity_type);

    // Store physical inputs as KG properties (read by CapabilityProfile::compute_from_kg)
    kg.setProperty(entity_id, "reflexes_ms", std::to_string(reflexes_ms));
    kg.setProperty(entity_id, "grit_W", std::to_string(grit_W));

    // Link all particles to the root entity
    for (int pid : body_ids) {
        kg.createKGParticle(entity_id, static_cast<unsigned>(pid));
    }

    // --- Helper: create a body part entity with health, strength, and capability bindings ---
    struct CapBinding { std::string cap_name; float weight; std::string side; };

    auto make_part = [&](const std::string& type, const std::string& name,
                         float strength_ratio, float max_hp,
                         const std::vector<CapBinding>& caps) -> kg::EntityID {
        kg::EntityID part = kg.createEntity(type);
        kg.setProperty(part, "body_part_name", name);
        kg.setProperty(part, "health", std::to_string(max_hp));
        kg.setProperty(part, "max_health", std::to_string(max_hp));
        // Physical capability
        float strength = grit_W * strength_ratio;
        kg.setProperty(part, "part_strength", std::to_string(strength));
        kg.setProperty(part, "part_max_strength", std::to_string(strength));
        kg.setProperty(part, "part_flexibility", "1.0");
        kg.setProperty(part, "part_responsiveness", std::to_string(reflexes_ms));
        kg.setProperty(part, "part_endurance", "100.0");
        // Tissue health (all start at 100%)
        kg.setProperty(part, "muscle_health", "100.0");
        kg.setProperty(part, "nerve_health", "100.0");
        kg.setProperty(part, "tendon_health", "100.0");
        kg.setProperty(part, "bone_health", "100.0");
        // Capability bindings (KG-driven)
        if (!caps.empty()) {
            std::string cap_list;
            for (const auto& c : caps) {
                kg.setProperty(part, "cap." + c.cap_name + ".weight", std::to_string(c.weight));
                if (!c.side.empty())
                    kg.setProperty(part, "cap." + c.cap_name + ".side", c.side);
                if (!cap_list.empty()) cap_list += ",";
                cap_list += c.cap_name;
            }
            kg.setProperty(part, "cap_list", cap_list);
        }
        kg.createRelation(entity_id, "HAS_PART", part);
        return part;
    };

    // --- Biped body plan declarations on root entity ---
    kg.setProperty(entity_id, "cap.locomotion.expected_count", "2");
    kg.setProperty(entity_id, "cap.locomotion.default_mode", "average");
    kg.setProperty(entity_id, "cap.manipulation.expected_count", "2");
    kg.setProperty(entity_id, "cap.manipulation.default_mode", "average");
    kg.setProperty(entity_id, "cap.rotation.expected_count", "1");
    kg.setProperty(entity_id, "cap.rotation.default_mode", "minimum");
    kg.setProperty(entity_id, "cap.perception.expected_count", "1");
    kg.setProperty(entity_id, "cap.perception.default_mode", "minimum");

    // --- Legs (0.4 grit each, 100 HP) ---
    if (!left_leg_ids.empty()) {
        make_part("Leg", "left_leg", 0.4f, 100.0f,
                  {{"locomotion", 1.0f, "left"}});
    }
    if (!right_leg_ids.empty()) {
        make_part("Leg", "right_leg", 0.4f, 100.0f,
                  {{"locomotion", 1.0f, "right"}});
    }

    // --- Arms (0.15 grit each, 80 HP) ---
    if (!left_arm_ids.empty()) {
        make_part("Arm", "left_arm", 0.15f, 80.0f,
                  {{"manipulation", 1.0f, "left"}});
    }
    if (!right_arm_ids.empty()) {
        make_part("Arm", "right_arm", 0.15f, 80.0f,
                  {{"manipulation", 1.0f, "right"}});
    }

    // --- Torso (0.3 grit, 150 HP) ---
    if (hips_id >= 0) {
        make_part("Torso", "torso", 0.3f, 150.0f,
                  {{"rotation", 1.0f, ""}});
    }

    // --- Head (0 locomotive grit, 80 HP) ---
    if (head_id >= 0) {
        make_part("Head", "head", 0.0f, 80.0f,
                  {{"perception", 1.0f, ""}});
    }

    std::cout << "[PhysicsHumanoidResult] Created KG body graph: entity=" << entity_id
              << " type=" << entity_type
              << " reflexes=" << reflexes_ms << "ms grit=" << grit_W << "W"
              << std::endl;
}
