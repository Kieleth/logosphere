#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include "logosphere/worldgen/entity_generator.h"

// A small planet: one kinematic core sphere with a crust of stones
// gluon-bonded onto it, floating above the turtle. Immobility comes
// from the sanctioned mechanisms only: the core is KINEMATIC (no
// writer, so it holds its place) and the crust hangs off it through
// constraints. Everything is a particle; there is no sky-ball prop.
//
// This is the Road-B "prince planet": the world stays -Z-gravity and
// walkers stand on the apex where down is still down. A future
// central-gravity field (Road A) can reuse the same body.
struct PlanetSpec {
    float radius = 3.0f;          // Crust radius (m)
    float stone_size = 0.45f;     // Crust stone edge (m), jittered
    float stone_jitter = 0.20f;   // Size variation fraction

    // Rust-warm crust over a darker core
    float crust_r = 0.62f, crust_g = 0.42f, crust_b = 0.28f;
    float core_r = 0.35f, core_g = 0.22f, core_b = 0.16f;
    float color_variance = 0.06f;

    float stiffness = 90000.0f;   // Crust-to-core bond strength

    static PlanetSpec asteroid_b612();  // The Little Prince preset
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
