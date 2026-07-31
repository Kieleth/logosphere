#ifndef PHYSICS_TREE_GENERATOR_H
#define PHYSICS_TREE_GENERATOR_H

/**
 * ============================================================================
 * PHYSICS TREE GENERATOR - Gluon-Based Structural Trees
 * ============================================================================
 *
 * ONE SENTENCE: Creates trees where branches are physically connected via
 * OrganicGluon constraints and will fall/break under sufficient force.
 *
 * DIFFERENCE FROM TreeGenerator:
 *   TreeGenerator: Visual particles only, no physics, stored in KG
 *   PhysicsTreeGenerator: Mass + gluons, physically simulated, can break
 *
 * ALGORITHM:
 *   1. Create kinematic floor anchor (optional)
 *   2. Create trunk segment with mass, attached to floor
 *   3. Recursively create branches:
 *      - Each branch attached to parent via OrganicGluon
 *      - Mass = volume × material_density
 *      - Breaking force = contact_area × material_strength
 *   4. Leaves are visual-only (no physics)
 *
 * EXAMPLE (Simple tree):
 *
 *        [Leaf cluster] (no physics)
 *              |
 *         [Branch] 2kg ←── OrganicGluon (can break!)
 *              |
 *         [Trunk] 10kg ←── NailGluon (rigid to floor)
 *              |
 *         [Floor] kinematic
 *
 * USES: Same TreeSpec as TreeGenerator for compatibility
 * ============================================================================
 */

#include <vector>
#include <memory>
#include "logosphere/worldgen/tree_generator.h"  // Reuse TreeSpec
#include "particle.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
class PhysicsSystem;
class ParticleSystem;
namespace kg { class KGModule; }

// Result of physics tree generation
struct PhysicsTreeResult {
    kg::EntityID entity_id = kg::INVALID_ENTITY;  // KG entity (for removal)
    int floor_id = -1;              // Kinematic floor (if created)
    int trunk_id = -1;              // Root trunk segment
    std::vector<int> branch_ids;    // All branch segment IDs
    std::vector<int> leaf_ids;      // Leaf particles (visual only)
    int total_segments = 0;         // Total physics segments
    // Root system (when using generate_tree_with_roots)
    int root_plate_id = -1;         // Central root plate particle
    std::vector<int> root_ids;      // Primary root particles spreading from plate
};

// Physics tree generator - creates structurally sound trees with gluon constraints
class PhysicsTreeGenerator {
public:
    PhysicsTreeGenerator();
    ~PhysicsTreeGenerator();

    // Initialize with engine reference
    void initialize(Engine* engine);

    // Generate a physics-enabled tree at world position
    // Returns structure with all particle IDs for testing/tracking
    PhysicsTreeResult generate_tree(float world_x, float world_y, float world_z,
                                    const TreeSpec& spec);

    // Generate with explicit floor (for testing without creating new floor)
    PhysicsTreeResult generate_tree_on_floor(float world_x, float world_y, float world_z,
                                             int floor_particle_id,
                                             const TreeSpec& spec);

    // Generate tree with root system (no floor tile required)
    // Root plate + primary roots rest on turtle via physics collision
    PhysicsTreeResult generate_tree_with_roots(float world_x, float world_y, float ground_z,
                                                const TreeSpec& spec);

    // ========================================================================
    // PERMAWORLD MODE - Store in KG only, chunk loading creates particles
    // ========================================================================

    // Store tree entity in KG (no particles created)
    // Returns entity_id for tracking; particles created on chunk activation
    kg::EntityID store_tree_entity(float world_x, float world_y, float ground_z,
                                   const TreeSpec& spec,
                                   float chunk_size = 50.0f);

private:
    // Internal struct for tracking gluon specs during tree building
    struct GluonSpec {
        size_t parent_index;      // Index into particles vector
        size_t child_index;       // Index into particles vector
        kg::KGGluonData data;     // Gluon parameters
    };

    // Build tree particles and gluons into vectors (no ParticleSystem)
    void collect_tree_specs(
        float world_x, float world_y, float ground_z,
        const TreeSpec& spec,
        std::vector<Particle>& out_particles,
        std::vector<GluonSpec>& out_gluons
    );

    // Recursive branch collection (store mode - no particle creation)
    void collect_branch(
        size_t parent_index,          // Index of parent in particles vector
        float parent_top_z,           // Z of parent's top
        float direction_angle,
        float elevation_angle,
        float length,
        float thickness,
        int depth,
        const TreeSpec& spec,
        std::vector<Particle>& particles,
        std::vector<GluonSpec>& gluons
    );
    // Recursive branch generation with gluon attachment
    // Returns particle ID of created branch segment
    int generate_branch(
        int parent_id,              // Parent particle to attach to
        float parent_top_z,         // Z position of parent's top surface
        float direction_angle,      // Horizontal angle (degrees)
        float elevation_angle,      // Vertical angle (degrees)
        float length,               // Branch length
        float thickness,            // Branch thickness (diameter)
        int depth,                  // Remaining recursion depth
        const TreeSpec& spec,
        PhysicsTreeResult& result   // Accumulator for IDs
    );

    // Create a single branch segment particle (with mass)
    Particle create_branch_particle(
        float x, float y, float z,
        float length, float thickness,
        float angle_h, float angle_v,
        const TreeSpec& spec,
        int depth
    );

    // Calculate mass from dimensions and material density
    float calculate_mass(float length, float thickness, float density);

    // Calculate contact area for OrganicGluon (cross-sectional area)
    float calculate_contact_area(float thickness);

    // Find existing floor tile at position (returns -1 if none found)
    int find_floor_tile_at(float world_x, float world_y);

    // Generate root system: root plate + radial primary roots
    // Returns root plate particle ID
    int generate_root_system(
        float world_x, float world_y, float ground_z,
        float tree_height, float trunk_thickness,
        PhysicsTreeResult& result
    );

    // Random number generation (seeded for reproducibility)
    float random_variance(float base, float variance);
    void seed_rng(unsigned int seed);

    Engine* engine_;
    PhysicsSystem* physics_;
    ParticleSystem* particles_;
    kg::KGModule* kg_;
    unsigned int rng_state_;
};

#endif // PHYSICS_TREE_GENERATOR_H
