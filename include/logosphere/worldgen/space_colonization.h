#ifndef SPACE_COLONIZATION_H
#define SPACE_COLONIZATION_H

#include "logosphere/worldgen/tree_skeleton.h"
#include "logosphere/worldgen/tree_generator.h"
#include <vector>
#include <cmath>

// Space Colonization parameters
struct SpaceColonizationParams {
    // Attractor distribution
    Vec3 attractor_center;
    float attractor_radius = 5.0f;
    int attractor_count = 1000;

    // External attractors (optional - if provided, use these instead of generating)
    std::vector<Vec3> external_attractors;

    // Growth parameters
    float attraction_range = 5.0f;   // How far branches "sense" attractors
    float kill_distance = 2.0f;      // When attractor is "reached"
    float segment_length = 0.5f;     // Branch growth per iteration
    float initial_thickness = 0.5f;  // Starting trunk thickness
    float thickness_taper = 0.95f;   // Child thickness = parent × taper

    // Root/trunk
    Vec3 root_position;
    Vec3 root_direction = Vec3(0, 0, 1); // Default: grow upward

    // Iteration limits
    int max_iterations = 500;

    // RNG seed
    unsigned int random_seed = 0;
};

// Space Colonization Algorithm
// Generates organic tree skeleton by simulating competition for resources
class SpaceColonization {
public:
    SpaceColonization();

    // Generate tree skeleton using Space Colonization
    TreeSkeleton generate(const SpaceColonizationParams& params);

private:
    // RNG state
    unsigned int rng_state_;

    // Attractor generation
    std::vector<Vec3> generate_attractors(const SpaceColonizationParams& params);
    Vec3 random_in_sphere(Vec3 center, float radius);

    // Growth simulation
    struct GrowthNode {
        Vec3 position;
        Vec3 direction;
        float thickness;
        int parent_index;
        std::vector<int> attractor_indices; // Attractors pulling this node
    };

    int find_closest_node(const std::vector<GrowthNode>& nodes, const Vec3& point);
    float distance(const Vec3& a, const Vec3& b);
    Vec3 average_direction(const std::vector<Vec3>& attractors, const Vec3& from);

    // RNG
    void seed_rng(unsigned int seed);
    float random_01();
};

#endif // SPACE_COLONIZATION_H
