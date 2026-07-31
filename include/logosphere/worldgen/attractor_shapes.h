#ifndef ATTRACTOR_SHAPES_H
#define ATTRACTOR_SHAPES_H

#include "logosphere/worldgen/tree_skeleton.h"
#include "logosphere/worldgen/organic_spec.h"
#include <vector>

// AttractorShapes - Generates attractor point distributions for Space Colonization
//
// Design: Different shapes produce different organic forms
// - Sphere: Tree canopy (radial spreading from center)
// - Cylinder: Grass blade (vertical growth, minimal spreading)
// - Cone: Pine tree (conical canopy, narrow at top)
// - Inverted Cone: Vine (hanging downward)
// - Hemisphere: Bush (ground-level spreading, no underground growth)
// - Torus: Wreath/ring shape
class AttractorShapes {
public:
    // Generate attractors based on spec
    static std::vector<Vec3> generate(const OrganicSpec& spec, Vec3 center, unsigned int seed);

private:
    // Shape-specific generators
    static std::vector<Vec3> generate_sphere(Vec3 center, float radius, int count, unsigned int seed);
    static std::vector<Vec3> generate_cylinder(Vec3 center, float radius, float height, int count, unsigned int seed);
    static std::vector<Vec3> generate_cone(Vec3 center, float base_radius, float height, int count, unsigned int seed);
    static std::vector<Vec3> generate_inverted_cone(Vec3 center, float base_radius, float height, int count, unsigned int seed);
    static std::vector<Vec3> generate_hemisphere(Vec3 center, float radius, int count, unsigned int seed);
    static std::vector<Vec3> generate_torus(Vec3 center, float major_radius, float minor_radius, int count, unsigned int seed);

    // Helper: Random point in unit sphere
    static Vec3 random_in_unit_sphere(unsigned int& rng_state);

    // Helper: Random float [0, 1]
    static float random_01(unsigned int& rng_state);
};

#endif // ATTRACTOR_SHAPES_H
