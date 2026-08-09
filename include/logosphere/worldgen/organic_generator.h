#ifndef ORGANIC_GENERATOR_H
#define ORGANIC_GENERATOR_H

#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/worldgen/organic_spec.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/tree_skeleton.h"
#include "logosphere/kg/kg_types.h"
#include "particle.h"
#include <vector>

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// OrganicGenerator - Unified generator for all organic entity types
//
// Design: Replaces TreeGenerator with generalized organic entity generation
// Uses OrganicSpec for unified parameterization across entity types
//
// Supported types:
// - Trees: oak, pine, willow, etc. (AttractorShape::SPHERE, CONE)
// - Grass: individual blades or patches (AttractorShape::CYLINDER)
// - Bushes: ground-level spreading (AttractorShape::HEMISPHERE)
// - Vines: hanging growth (AttractorShape::INVERTED_CONE)
// - Any custom organic form via OrganicSpec
//
// Process:
// 1. Generate attractor distribution (AttractorShapes)
// 2. Create trunk/stem with organic tapering
// 3. Run Space Colonization algorithm
// 4. Convert skeleton to particles (branches + foliage)
// 5. Store in KG with GrowthState for temporal system
class OrganicGenerator : public EntityGenerator {
public:
    OrganicGenerator();
    ~OrganicGenerator();

    // Generate organic entity at world position
    // Returns KG entity ID for the entity root
    // Destruction handled by KG - just call kg->destroyEntity(entity_id)
    kg::EntityID generate(float world_x, float world_y, float world_z,
                          const OrganicSpec& spec);

    // Convenience methods for specific types
    kg::EntityID generate_tree(float x, float y, float z, const OrganicSpec& spec) {
        return generate(x, y, z, spec);
    }

    kg::EntityID generate_grass(float x, float y, float z, const OrganicSpec& spec) {
        return generate(x, y, z, spec);
    }

    kg::EntityID generate_bush(float x, float y, float z, const OrganicSpec& spec) {
        return generate(x, y, z, spec);
    }

    kg::EntityID generate_vine(float x, float y, float z, const OrganicSpec& spec) {
        return generate(x, y, z, spec);
    }

    // Generate grass patch (parent entity with child blade entities)
    // Returns KG entity ID for the patch root
    kg::EntityID generate_grass_patch(float world_x, float world_y, float world_z,
                                      const GrassPatchSpec& spec);

private:
    // Generate trunk/stem with organic tapering
    // Returns particles for trunk segments
    std::vector<Particle> generate_trunk(float world_x, float world_y, float world_z,
                                         const OrganicSpec& spec,
                                         Vec3& out_trunk_top);

    // Convert skeleton to particles (branches + foliage).
    //
    // `parents_out` gets one entry per returned particle: the index (into the
    // SAME returned vector) of the particle it should be BONDED to, or -1 for
    // segments rooted at the trunk top. Determined GEOMETRICALLY (the segment
    // whose end meets this segment's start), not from
    // BranchSegment::parent_index: that field stores node indices against a
    // comment claiming segment indices, and nothing had ever read it, so its
    // semantics are unverified. Issue #47: these edges become the gluons that
    // stop blades being loose towers of plates.
    std::vector<Particle> skeleton_to_particles(const TreeSkeleton& skeleton,
                                                const OrganicSpec& spec,
                                                std::vector<int>& parents_out);

    // Create foliage elements at branch endpoint
    std::vector<Particle> create_foliage(const Vec3& position,
                                        const BranchSegment& segment,
                                        const OrganicSpec& spec,
                                        int max_iteration);

    // Random number generation (seeded for reproducibility)
    float random_variance(float base, float variance);

    // Per-patch lobe cache for CLUSTERED (multi-lobe dab) sampling.
    const GrassPatchSpec* lobes_cached_for_ = nullptr;
    int lobe_count_ = 1;
    float lobe_x_[3] = {0, 0, 0};
    float lobe_y_[3] = {0, 0, 0};
    float random_gaussian(float mean, float stddev);  // Gaussian distribution
    void seed_rng(unsigned int seed);

    // Spatial distribution helpers
    struct Vec2 { float x, y; };
    std::vector<Vec2> generate_blade_positions(const GrassPatchSpec& spec);

    unsigned int rng_state_;  // Simple RNG state
};

#endif // ORGANIC_GENERATOR_H
