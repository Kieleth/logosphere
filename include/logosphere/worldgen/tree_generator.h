#ifndef TREE_GENERATOR_H
#define TREE_GENERATOR_H

#include <vector>
#include <string>
#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/kg/kg_types.h"
#include "particle.h"
#include "logosphere/worldgen/tree_skeleton.h"
#include "logosphere/worldgen/space_colonization.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Tree specification - defines all parameters for procedural generation
// LLM can control these parameters to create different tree species
struct TreeSpec {
    // === Shape Parameters ===
    float height = 10.0f;              // Total tree height (m)
    float crown_radius = 5.0f;         // Canopy width (m)
    // Space-colonization anatomy (LLM-facing levers; see Logogenesis):
    // canopy_start: fraction of height where the crown begins.
    // < 0 keeps the legacy random 30-60%. Low = bushy, high = palm
    // or redwood silhouette.
    float canopy_start = -1.0f;
    // canopy_density: multiplier on attractor count (crown fullness).
    float canopy_density = 1.0f;
    // Branches sprouting from the bare trunk below the crown (old
    // oaks have them, redwoods don't). 0 = legacy clean trunk.
    int lower_branch_count = 0;
    float trunk_diameter = 1.0f;       // Base trunk thickness (m)

    // === Fractal Parameters ===
    int branch_depth = 5;              // Recursion levels (3-7 typical)
    float branch_angle = 30.0f;        // Branch divergence angle (degrees)
    float length_ratio = 0.7f;         // Child length = parent * ratio (0.6-0.8)
    float thickness_ratio = 0.7f;      // Child thickness = parent * ratio
    int branches_per_split = 2;        // Binary tree (2) or ternary (3)

    // === Appearance ===
    float trunk_r = 0.4f, trunk_g = 0.25f, trunk_b = 0.15f;  // Brown bark
    float leaf_r = 0.2f, leaf_g = 0.6f, leaf_b = 0.2f;       // Green leaves

    // === Description (for observation) ===
    std::string description = "A tree.";  // Default, species presets override

    // === Leaf Parameters (Space Colonization) ===
    float leaf_thickness_threshold = 0.4f;  // Branches thinner than this get leaves (ratio of initial thickness)
    int leaf_count_min = 2;                 // Min leaves per branch endpoint
    int leaf_count_max = 2;                 // Max leaves per branch endpoint
    float leaf_offset_radius = 0.6f;        // Center of leaf spread radius (m)
    float leaf_offset_variance = 0.3f;      // Variance in spread radius (m)
    float leaf_base_width = 0.35f;          // Base leaf width before maturity scaling (m)
    float leaf_base_height = 0.25f;         // Base leaf height before maturity scaling (m)
    float leaf_tilt_probability = 0.2f;     // Probability of leaf tilting downward (0.0-1.0)
    float leaf_tilt_angle_min = 10.0f;      // Min downward tilt angle (degrees)
    float leaf_tilt_angle_max = 30.0f;      // Max downward tilt angle (degrees)

    // === Variation (Stochastic) ===
    float angle_variance = 10.0f;      // ±degrees random per branch
    float length_variance = 0.1f;      // ±ratio random per branch
    float side_branch_probability = 0.3f; // Probability of spawning side branch per trunk segment (0-1)
    unsigned int random_seed = 0;      // Seed for reproducible randomness

    // === Species Presets ===
    static TreeSpec sapling();      // Tiny young tree (3m)
    static TreeSpec young_oak();    // Young oak (6m)
    static TreeSpec oak();          // Mature oak (12m)
    static TreeSpec ancient_oak();  // Huge ancient tree (25m)
    static TreeSpec baobab();
    static TreeSpec pine();
    static TreeSpec palm();
    static TreeSpec willow();

    // Apply natural random variation to this spec (modifies in place)
    // variance_amount: 0.0 = no change, 1.0 = maximum natural variation
    void apply_natural_variation(float variance_amount = 0.3f, unsigned int seed = 0);
};

// Procedural tree generator using recursive fractal algorithm
// Inherits from EntityGenerator for common initialization and tracking
// Phase 1: Generate single static trees
// Phase 2: Chunk-based distribution (future)
// Phase 3: LLM-controllable parameters (future)
class TreeGenerator : public EntityGenerator {
public:
    TreeGenerator();
    ~TreeGenerator();

    // Generate a single tree at world position (recursive fractal method)
    // Returns KG entity ID for the tree root
    // Note: Destruction handled by KG - just call kg->destroyEntity(tree_id)
    kg::EntityID generate_tree(float world_x, float world_y, float world_z,
                                const TreeSpec& spec);

    // Generate tree using Space Colonization Algorithm
    // Returns KG entity ID for the tree root
    kg::EntityID generate_tree_space_colonization(float world_x, float world_y, float world_z,
                                                  const TreeSpec& spec);

private:
    // Recursive branch generation (core algorithm)
    // Returns KG entity for this branch (for hierarchy)
    kg::EntityID generate_branch(
        float start_x, float start_y, float start_z,
        float direction_angle,  // Horizontal angle (degrees)
        float elevation_angle,  // Vertical angle (degrees)
        float length,
        float thickness,
        int depth,
        const TreeSpec& spec,
        kg::EntityID parent_entity  // For KG hierarchy
    );

    // Create particle data for branch segment (returns particle data for KG storage)
    std::vector<Particle> create_branch_particles(
        float x, float y, float z,
        float length, float thickness,
        float angle_h, float angle_v,
        float r, float g, float b,
        int depth  // Tree depth for particle allocation strategy
    );

    // Create leaf cluster at branch end
    kg::EntityID create_leaf_cluster(
        float x, float y, float z,
        const TreeSpec& spec
    );

    // Random number generation (seeded for reproducibility)
    float random_variance(float base, float variance);
    void seed_rng(unsigned int seed);

    // Skeleton to particles conversion
    std::vector<Particle> skeleton_to_particles(const TreeSkeleton& skeleton,
                                                const TreeSpec& spec);

    unsigned int rng_state_;  // Simple RNG state
};

#endif // TREE_GENERATOR_H
