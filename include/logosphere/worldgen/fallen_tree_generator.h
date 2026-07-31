#ifndef FALLEN_TREE_GENERATOR_H
#define FALLEN_TREE_GENERATOR_H

#include <vector>
#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Fallen tree/branch specification - defines parameters for procedural debris
// Creates horizontal logs and branches lying on the forest floor
struct FallenTreeSpec {
    // === Fallen Tree Type ===
    enum class FallenType {
        TRUNK,      // Large fallen tree trunk (3-8m long)
        LOG,        // Medium log section (1-3m long)
        BRANCH,     // Fallen branch (0.5-2m long)
        TWIG        // Small twig debris (0.1-0.5m long)
    };
    FallenType type = FallenType::BRANCH;

    // === Dimensions ===
    float length = 2.0f;         // Length along ground (m)
    float diameter = 0.3f;       // Trunk/branch diameter (m)

    // === Appearance ===
    float bark_r = 0.35f, bark_g = 0.22f, bark_b = 0.12f;  // Brown bark
    float color_variance = 0.08f;  // Random color variation

    // === Physics Parameters ===
    float stiffness = 80000.0f;  // How rigidly segments are held together

    // === Segmentation ===
    int num_segments = 3;        // Number of segments along length

    // === Orientation ===
    float rotation_angle = 0.0f; // Horizontal rotation (degrees, 0 = pointing East)

    // === Description (for narrative) ===
    std::string description = "A fallen branch lies on the ground.";

    // === Presets ===
    static FallenTreeSpec fallen_trunk();   // Large fallen tree trunk
    static FallenTreeSpec fallen_log();     // Medium log section
    static FallenTreeSpec fallen_branch();  // Fallen branch
    static FallenTreeSpec twig();           // Small twig

    // Apply natural random variation (modifies in place)
    void apply_natural_variation(float variance_amount = 0.3f, unsigned int seed = 0);
};

// Procedural fallen tree/debris generator
// Creates horizontal logs and branches that lie on the forest floor
// Can be interacted with (physics-enabled)
class FallenTreeGenerator : public EntityGenerator {
public:
    FallenTreeGenerator();
    ~FallenTreeGenerator();

    // Generate a fallen tree/branch at world position
    // Returns KG entity ID
    kg::EntityID generate_fallen_tree(float world_x, float world_y, float world_z,
                                       const FallenTreeSpec& spec);

private:
    // Create a single segment particle in KG storage
    kg::KGParticleID create_segment(float x, float y, float z,
                                    float length, float diameter,
                                    float rotation_h, float rotation_v,
                                    float r, float g, float b,
                                    kg::EntityID entity_id);

    // Random number generation
    float random_range(float min, float max);
    float random_variance(float base, float variance);
    void seed_rng(unsigned int seed);

    unsigned int rng_state_;
};

#endif // FALLEN_TREE_GENERATOR_H
