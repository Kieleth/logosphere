#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include "logosphere/worldgen/entity_generator.h"

// A small spherical world: a kinematic core with a crust of stones
// laid on it, floating clear of the turtle. Everything is a particle;
// there is no sky-ball prop and no drawn sphere.
//
// The crust is KINEMATIC, which is the sanctioned mechanism for
// immobility (turtle boundary / anchored gluons / kinematic mode) and
// what terrain is: it owns its position, never integrates gravity,
// and still collides. A DYNAMIC crust held up by is_at_rest was tried
// and is wrong - is_at_rest is a solver optimisation, not immobility,
// and a walker was enough to wake the shell and drop it.
//
// Consequence, stated plainly: this body is TERRAIN, not a dynamic
// object. It cannot be cratered, knocked, or moved. Making it
// destructible means flipping a region back to DYNAMIC and bonding it
// to the core with the constraint API, and needs a solver pass that
// can hold thousands of stones against gravity every frame.
//
// The generator makes no gravity assumption of its own: a kinematic
// shell is equally correct under a future central-gravity field.
struct PlanetSpec {
    float radius = 3.0f;          // Crust radius (m)
    float stone_size = 0.45f;     // Crust stone edge (m), jittered
    float stone_jitter = 0.20f;   // Size variation fraction

    // Regolith: the dust the world is actually seen through. Laid
    // over the structural plates as grains that are small, very flat,
    // and nearly tangential, because that is what separates a lunar
    // surface from a gravel heap - not colour. Thick chunks tilted
    // off-radial read as rocks however finely you dice them.
    // Size is a fraction of stone_size; 0 disables the layer and its
    // particle cost.
    float surface_ratio = 0.32f;    // grain size / stone_size
    // Coverage multiplier, and the single most important number here.
    // The grain count works out to density/1.7 of the sphere's area,
    // INDEPENDENT of grain size, so density is what decides whether
    // the dust closes over the rock or leaves it showing through. At
    // 1.0 only 59% of the surface was covered and the bare 41% read
    // as dark cracks between scales. 1.8 gives 106%: grains overlap,
    // and the surface becomes continuous.
    float surface_density = 1.8f;   // coverage multiplier (/1.7 = fraction)
    float surface_tint = 1.10f;     // dust catches more light than rock
    float surface_flatten = 0.18f;  // grain thickness / width: flat tiles
    float surface_scatter = 0.06f;  // radians of lie; near-tangential

    // Rust-warm crust. The core is a SHADE of it rather than its own
    // colour: gaps between stones should read as the same rock in
    // shadow, not as a different, darker object showing through. It
    // also keeps a recoloured planet coherent - setting crust to blue
    // used to leave a brown core behind it.
    float crust_r = 0.62f, crust_g = 0.42f, crust_b = 0.28f;
    float core_shade = 0.85f;     // core colour = crust * this
    float color_variance = 0.06f;

    float stiffness = 90000.0f;   // Crust-to-core bond strength

    static PlanetSpec small_rocky_world();  // rust-warm default
};

class PlanetGenerator : public EntityGenerator {
public:
    PlanetGenerator() = default;
    ~PlanetGenerator() override = default;

    // Generate the planet centered at (cx, cy, cz). cz must exceed
    // radius so the body floats clear of the turtle.
    // Returns the KG entity ID.
    kg::EntityID generate_planet(float cx, float cy, float cz,
                                 const PlanetSpec& spec);

private:
    unsigned int rng_state_ = 1u;
    float random_range(float min, float max);
};

#endif  // PLANET_GENERATOR_H
