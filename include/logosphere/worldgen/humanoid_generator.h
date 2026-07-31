#ifndef HUMANOID_GENERATOR_H
#define HUMANOID_GENERATOR_H

#include <vector>
#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
class ParticleDynamicsSystem;
namespace kg { class KGModule; }
namespace logosphere::animation { class HumanoidLocomotion; }

// Humanoid specification - defines all parameters for procedural generation
// LLM can control these parameters to create different body types
struct HumanoidSpec {
    // === Overall Dimensions ===
    float total_height = 1.8f;         // Total height (m) - typical human ~1.8m

    // === Torso ===
    float torso_height = 0.6f;         // Torso height (m)
    float torso_width = 0.4f;          // Shoulder width (m)
    float torso_depth = 0.25f;         // Front-to-back (m)

    // === Head ===
    float head_size = 0.15f;           // Head radius (m)

    // === Arms ===
    float arm_length = 0.6f;           // Total arm length (m)
    float upper_arm_ratio = 0.55f;     // Upper arm = 55% of total arm
    float arm_thickness = 0.08f;       // Arm diameter (m)

    // === Hips ===
    float hip_width = 0.35f;           // Hip width (m)
    float hip_height = 0.15f;          // Hip section height (m)

    // === Legs ===
    float leg_length = 0.9f;           // Total leg length (m)
    float thigh_ratio = 0.55f;         // Thigh = 55% of total leg
    float leg_thickness = 0.12f;       // Leg diameter (m)

    // === Appearance ===
    float skin_r = 0.8f, skin_g = 0.6f, skin_b = 0.5f;  // Skin tone
    float clothing_r = 0.3f, clothing_g = 0.3f, clothing_b = 0.7f;  // Clothing color

    // === Patterns (procedural textures) ===
    // Pattern IDs match shader constants in procedural_patterns.metal:
    // 0=none, 1=wood, 2=stone, 3=leather, 4=linen, 5=cotton, 6=silk, 7=embroidery
    // 8=skin, 9=skin_reptile, 10=hair
    uint8_t torso_pattern = 4;   // PATTERN_LINEN (clothing)
    uint8_t legs_pattern = 4;    // PATTERN_LINEN (pants)
    uint8_t arms_pattern = 8;    // PATTERN_SKIN (bare arms)
    uint8_t hands_pattern = 8;   // PATTERN_SKIN
    uint8_t feet_pattern = 3;    // PATTERN_LEATHER (shoes)
    uint8_t head_pattern = 8;    // PATTERN_SKIN (face)
    uint8_t hair_pattern = 10;   // PATTERN_HAIR

    // === Eyes (optional - disabled by default) ===
    bool has_eyes = false;                              // Set true to enable eyes
    float eye_size = 0.02f;                             // Eye socket size (m)
    float eye_spacing = 0.10f;                          // Distance between eyes (m)
    float eye_forward_offset = -0.005f;                 // Slightly embedded in face (flush)
    float eye_height_offset = 0.02f;                    // Height above head center
    // Outer eye (socket/sclera)
    float eye_outer_r = 0.0f, eye_outer_g = 0.0f, eye_outer_b = 0.0f;  // Black socket
    // Inner eye (pupil/iris)
    float eye_inner_r = 0.0f, eye_inner_g = 1.0f, eye_inner_b = 0.0f;  // Green glow
    float eye_pupil_scale = 0.4f;                       // Inner eye size relative to outer

    // === Orientation ===
    float facing_angle = 0.0f;                          // Initial facing direction in radians (0 = +Y/north)

    // === Presets ===
    static HumanoidSpec default_human();
    static HumanoidSpec eva();          // Original Eva character
    static HumanoidSpec hunter();       // Male hunter/villager for Logomancers
    static HumanoidSpec tall_human();
    static HumanoidSpec short_human();

    // Apply natural random variation to this spec (modifies in place)
    void apply_natural_variation(float variance_amount = 0.1f, unsigned int seed = 0);
};

// Humanoid body part references for look-at system
struct HumanoidBodyParts {
    unsigned int head = 0;
    unsigned int neck = 0;
    unsigned int torso = 0;
    unsigned int abdomen = 0;
    unsigned int hips = 0;
    std::vector<unsigned int> legs;  // All leg particles
    float world_x = 0, world_y = 0, world_z = 0;  // Base position
    float base_rotation = 0.0f;  // Current body base rotation
};

// Result of physics humanoid generation (gluon-based)
struct PhysicsHumanoidResult {
    kg::EntityID entity_id = kg::INVALID_ENTITY;  // KG entity (for entity-level operations)
    int floor_id = -1;               // Floor particle (kinematic)
    std::vector<int> body_ids;       // All body particle IDs (for tracking)
    int head_id = -1;                // Head particle (for drift tracking)
    int hips_id = -1;                // Hips particle (center of mass)
    // Note: total_mass is now calculated dynamically via kg_->getEntityMass(entity_id)

    // For animation registration (foot → shin → thigh order)
    std::vector<int> left_leg_ids;
    std::vector<int> right_leg_ids;
    std::vector<int> left_arm_ids;
    std::vector<int> right_arm_ids;

    // Torso particles (not animated, for physics stretch check)
    std::vector<int> torso_ids;      // hips, abdomen, chest, neck, head, hair, ears

    // Eye particles (optional - only if spec.has_eyes)
    int left_eye_outer_id = -1;      // Left eye socket/sclera
    int left_eye_inner_id = -1;      // Left eye pupil/iris
    int right_eye_outer_id = -1;     // Right eye socket/sclera
    int right_eye_inner_id = -1;     // Right eye pupil/iris

    // Hair particles
    int upper_hair_id = -1;          // Hair on top of head
    int back_hair_id = -1;           // Hair behind head

    // Derive and register joints based on created structure
    // Joints are only created for limbs that exist (adapts to missing/extra limbs)
    // Build the FK joint hierarchy on the entity. Pass the engine's
    // HumanoidLocomotion module — the worldgen call used to take the
    // dynamics pointer pre-B8 when the joint API still lived there.
    void register_joints(logosphere::animation::HumanoidLocomotion* locomotion) const;

    // Create body part entities in KG with health, strength, flexibility.
    // Sets physical inputs (reflexes, power) on the humanoid entity, creates typed body part entities
    // (Leg, Arm, Head, Torso, etc.) with HAS_PART relations and links particles.
    // entity_type: ontology type for the root entity (e.g., "Humanoid", "Human")
    void create_kg_entities(kg::KGModule& kg, const std::string& entity_type,
                            float reflexes_ms, float grit_W);
};

// Procedural humanoid generator
// Inherits from EntityGenerator for common initialization and tracking
// Creates hierarchical body structure with KG relationships
class HumanoidGenerator : public EntityGenerator {
public:
    HumanoidGenerator();
    ~HumanoidGenerator();

    // Generate a humanoid at world position
    // Returns KG entity ID for the humanoid root
    kg::EntityID generate_humanoid(float world_x, float world_y, float world_z,
                                    const HumanoidSpec& spec);

    // SHARED HELPER: Create and activate Eva (for Eden + tests)
    // Returns entity ID, particle count, and render indices of activated particles
    struct HumanoidCreationResult {
        kg::EntityID entity_id;
        int particle_count;
        std::vector<kg::RenderIndex> render_indices;
    };
    using EvaCreationResult = HumanoidCreationResult;  // Backwards compatibility

    HumanoidCreationResult create_and_activate_eva(float world_x, float world_y, float world_z,
                                                    class Engine* engine);

    // SHARED HELPER: Create and activate Hunter (for Logomancers)
    HumanoidCreationResult create_and_activate_hunter(float world_x, float world_y, float world_z,
                                                       class Engine* engine);

    // PHYSICS-BASED GENERATION: Create humanoid with gluon constraints
    // Uses NailGluon for rigid body attachment (proven in test_eva_physics)
    // kg_support=false: particles go directly to ParticleSystem (for tests)
    // kg_support=true: particles stored in KG (for chunk loading - NOT YET IMPLEMENTED)
    PhysicsHumanoidResult generate_humanoid_physics(
        float world_x, float world_y, float world_z,
        int floor_particle_id,          // -1 to create floor
        const HumanoidSpec& spec,
        bool kg_support = false         // false for now, KG path implemented later
    );

    // Update humanoid look-at behavior (called each frame)
    // Applies hierarchical rotation: head → torso → body
    void update_look_at(float mouse_world_x, float mouse_world_y);

private:
    // Body part generators
    kg::EntityID create_torso(float x, float y, float z, const HumanoidSpec& spec);
    kg::EntityID create_head(float x, float y, float z, const HumanoidSpec& spec);
    kg::EntityID create_hips(float x, float y, float z, const HumanoidSpec& spec);

    kg::EntityID create_arm(float x, float y, float z, bool is_left,
                           const HumanoidSpec& spec);
    kg::EntityID create_leg(float x, float y, float z, bool is_left,
                           const HumanoidSpec& spec);

    // Helper: create particle in chunk-based KG storage, return KGParticleID
    kg::KGParticleID create_body_particle(float x, float y, float z,
                                          float width, float depth, float height,
                                          float r, float g, float b,
                                          kg::EntityID entity_id,
                                          uint8_t pattern_id = 0);

    // PHASE 2.3: Create particle constraint between two particles
    // Calculates rest distance from current positions, stores in KG
    // joint_name: optional semantic name for FK animation (e.g., "right_shoulder")
    void create_humanoid_constraint(kg::EntityID entity_id,
                                    unsigned int particle_a,
                                    unsigned int particle_b,
                                    float stiffness,
                                    const std::string& joint_name = "");

    // Random number generation (seeded for reproducibility)
    float random_variance(float base, float variance);
    void seed_rng(unsigned int seed);

    unsigned int rng_state_;  // Simple RNG state

    // Active humanoid tracking (for look-at system)
    // Currently supports one humanoid (Eva)
    HumanoidBodyParts eva_parts_;
};

#endif // HUMANOID_GENERATOR_H
