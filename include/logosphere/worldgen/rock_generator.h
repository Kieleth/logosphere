#ifndef ROCK_GENERATOR_H
#define ROCK_GENERATOR_H

#include <vector>
#include <string>
#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Rock specification - defines all parameters for procedural generation
// LLM can control these parameters to create different rock types
struct RockSpec {
    // === Rock Type ===
    enum class RockType {
        PEBBLE,        // Small egg-shaped rock (3 boxes)
        SMALL_ROCK,    // Fist-sized rock (4-6 boxes)
        MEDIUM_ROCK,   // Head-sized rock (6-9 boxes)
        LARGE_BOULDER  // Large boulder (9-15 boxes)
    };
    RockType type = RockType::PEBBLE;

    // === Overall Dimensions ===
    float size = 0.2f;  // Overall rock size (m) - diameter/length

    // === Physics Parameters ===
    // Stiffness determines how rigidly boxes are held together
    // High stiffness (90,000-100,000 N/m) = rigid rock
    // Medium stiffness (40,000-60,000 N/m) = breakable rock
    float core_stiffness = 98000.0f;    // Core boxes (very rigid)
    float detail_stiffness = 50000.0f;  // Detail boxes (medium rigid)

    // === Appearance ===
    float base_r = 0.5f, base_g = 0.5f, base_b = 0.5f;  // Gray rock
    float color_variance = 0.1f;  // Random color variation per box

    // === Description (for LLM/narrative) ===
    std::string description;

    // === Composition (auto-calculated from type) ===
    int core_boxes = 3;      // Core structural boxes
    int detail_boxes = 0;    // Surface detail boxes

    // === Presets ===
    static RockSpec pebble();           // Small egg-shaped pebble
    static RockSpec small_rock();       // Fist-sized rock
    static RockSpec medium_rock();      // Head-sized rock
    static RockSpec large_boulder();    // Large boulder

    // Apply natural random variation to this spec (modifies in place)
    void apply_natural_variation(float variance_amount = 0.2f, unsigned int seed = 0);

    // Update composition based on type (auto-called by presets)
    void update_composition_from_type();
};

// Procedural rock generator using physics-based assembly
// Inherits from EntityGenerator for common initialization and tracking
// Rocks are assemblies of BOX particles held together by stiffness constraints
// Phase 1: Simple pebbles (egg-shaped, 3 boxes)
// Phase 2: Complex rocks (core + detail boxes)
// Phase 3: LLM-controllable parameters (future)
class RockGenerator : public EntityGenerator {
public:
    RockGenerator();
    ~RockGenerator();

    // Generate a rock at world position
    // Returns KG entity ID for the rock root
    // Note: Destruction handled by KG - just call kg->destroyEntity(rock_id)
    kg::EntityID generate_rock(float world_x, float world_y, float world_z,
                                const RockSpec& spec);

private:
    // Phase 1: Generate simple pebble (egg-shape)
    // 1 central box + 2 side boxes, high stiffness
    kg::EntityID generate_pebble(float x, float y, float z, const RockSpec& spec);

    // Phase 2: Generate complex rock (core + detail boxes)
    kg::EntityID generate_complex_rock(float x, float y, float z, const RockSpec& spec);

    // Helper: Create single rock box particle in KG storage
    // Returns KGParticleID for constraint creation
    kg::KGParticleID create_rock_box(float x, float y, float z,
                                     float width, float height, float depth,
                                     float rotation_x, float rotation_y, float rotation_z,
                                     float r, float g, float b,
                                     kg::EntityID entity_id);

    // NOTE: Constraint creation uses base class EntityGenerator::create_constraint()
    // Do NOT implement custom constraint methods - use entity-level API

    // Random number generation (seeded for reproducibility)
    float random_range(float min, float max);
    float random_variance(float base, float variance);
    void seed_rng(unsigned int seed);

    unsigned int rng_state_;  // Simple RNG state
};

#endif // ROCK_GENERATOR_H
