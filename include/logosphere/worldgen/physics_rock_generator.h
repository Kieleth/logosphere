#ifndef PHYSICS_ROCK_GENERATOR_H
#define PHYSICS_ROCK_GENERATOR_H

/**
 * ============================================================================
 * PHYSICS ROCK GENERATOR - Gluon-Based Structural Rocks
 * ============================================================================
 *
 * ONE SENTENCE: Creates rocks where boxes are physically connected via
 * OrganicGluon constraints and will fall/break under sufficient force.
 *
 * DIFFERENCE FROM RockGenerator:
 *   RockGenerator: Uses KG/chunk system, delayed activation
 *   PhysicsRockGenerator: Direct ParticleSystem, immediate physics
 *
 * ALGORITHM:
 *   1. Create central core box with mass
 *   2. Create surrounding boxes attached via OrganicGluon
 *   3. Gluons can break if force exceeds material strength
 *
 * EXAMPLE (Pebble):
 *
 *     [Left box] ←── OrganicGluon ──→ [Center box] ←── OrganicGluon ──→ [Right box]
 *         2kg                              3kg                              2kg
 *
 * Rock density: ~2500 kg/m³ (granite/basalt)
 * ============================================================================
 */

#include <vector>
#include "particle.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
class PhysicsSystem;
class ParticleSystem;
namespace kg { class KGModule; }

// Rock specification for physics generator
struct PhysicsRockSpec {
    // Rock type presets
    enum class Type {
        PEBBLE,         // Small 3-box egg shape (~0.2m)
        SMALL_ROCK,     // Fist-sized (~0.4m)
        MEDIUM_ROCK,    // Head-sized (~0.8m)
        LARGE_BOULDER   // Large boulder (~1.5m)
    };
    Type type = Type::PEBBLE;

    // Overall size (meters)
    float size = 0.2f;

    // Material properties
    float density = 2500.0f;           // kg/m³ (granite)
    float material_strength = 50e6f;   // Pa (rock is weaker than oak)

    // Appearance
    float r = 0.5f, g = 0.5f, b = 0.5f;  // Gray
    float color_variance = 0.1f;

    // Presets
    static PhysicsRockSpec pebble();
    static PhysicsRockSpec small_rock();
    static PhysicsRockSpec medium_rock();
    static PhysicsRockSpec large_boulder();
};

// Result of physics rock generation
struct PhysicsRockResult {
    int core_id = -1;                // Central box particle
    std::vector<int> box_ids;        // All box particle IDs
    int total_boxes = 0;
    // Note: total_mass is now calculated dynamically via kg_->getEntityMass(entity_id)
};

// Physics rock generator - creates structurally sound rocks with gluon constraints
class PhysicsRockGenerator {
public:
    PhysicsRockGenerator();
    ~PhysicsRockGenerator();

    // Initialize with engine reference
    void initialize(Engine* engine);

    // ========================================================================
    // IMMEDIATE MODE - Creates particles now (legacy, for testing)
    // ========================================================================

    // Generate a physics-enabled rock at world position
    // Returns structure with all particle IDs for testing/tracking
    PhysicsRockResult generate_rock(float world_x, float world_y, float world_z,
                                    const PhysicsRockSpec& spec);

    // Generate rock on existing floor tile (for testing)
    PhysicsRockResult generate_rock_on_floor(float world_x, float world_y, float world_z,
                                              int floor_particle_id,
                                              const PhysicsRockSpec& spec);

    // ========================================================================
    // PERMAWORLD MODE - Stores in KG only, chunk loading creates particles
    // ========================================================================

    // Store rock entity in KG with all particle and gluon data
    // Does NOT create particles - chunk loading will activate via EntityManager
    // Returns KG entity ID (INVALID_ENTITY if KG disabled)
    kg::EntityID store_rock_entity(float world_x, float world_y, float world_z,
                                   const PhysicsRockSpec& spec,
                                   float chunk_size = 50.0f);

private:
    // Create a single rock box particle (with mass)
    int create_rock_box(
        float x, float y, float z,
        float width, float height, float depth,
        float rotation_x, float rotation_y, float rotation_z,
        float r, float g, float b,
        float density
    );

    // Create OrganicGluon between two rock boxes
    void create_rock_gluon(int box_a, int box_b, float material_strength);

    // Build particle struct without adding to ParticleSystem (for KG storage)
    Particle build_rock_box_particle(
        float x, float y, float z,
        float width, float height, float depth,
        float rotation_x, float rotation_y, float rotation_z,
        float r, float g, float b,
        float density
    );

    // Build gluon data struct (for KG storage)
    kg::KGGluonData build_gluon_data(
        const Particle& pa, const Particle& pb,
        float material_strength
    );

    // Random number generation
    float random_variance(float base, float variance);
    float random_range(float min, float max);
    void seed_rng(unsigned int seed);

    Engine* engine_;
    PhysicsSystem* physics_;
    ParticleSystem* particles_;
    kg::KGModule* kg_;
    unsigned int rng_state_;
};

#endif // PHYSICS_ROCK_GENERATOR_H
