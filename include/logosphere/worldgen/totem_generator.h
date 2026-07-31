#ifndef TOTEM_GENERATOR_H
#define TOTEM_GENERATOR_H

#include <vector>
#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/kg/kg_types.h"
#include "particle.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Totem specification - simple test entity for physics testing
struct TotemSpec {
    // Lower trunk (Phase 2.1)
    float trunk_size = 1.0f;        // Cube size (m)
    float trunk_mass = 10.0f;       // Mass (kg)

    // Upper trunk (Phase 2.2 - MVP stacking)
    float upper_trunk_size = 0.8f;  // Upper trunk smaller (m)
    float upper_trunk_mass = 8.0f;  // Upper trunk lighter (kg)

    // Future: Constraints (Phase 2.3+)
    float constraint_rest_distance = 0.9f;  // Distance between trunks (m)
    float constraint_stiffness = 500.0f;    // Spring stiffness (N/m) - Reduced for Verlet stability

    // Appearance (wood)
    float r = 0.4f, g = 0.25f, b = 0.15f;  // Brown wood

    // Default: single trunk totem
    static TotemSpec single_trunk();

    // Future: two trunk totem with constraint
    static TotemSpec two_trunk();
};

// Simple totem generator for physics testing
// Phase 2.1: Single trunk particle (test floor collision)
// Phase 2.2: Two trunk particles with constraint (test particle-particle collision)
class TotemGenerator : public EntityGenerator {
public:
    TotemGenerator();
    ~TotemGenerator();

    // Generate single trunk totem at world position
    // Returns KG entity ID for the totem
    kg::EntityID generate_totem(float world_x, float world_y, float world_z,
                                const TotemSpec& spec);

private:
    // Create particle data for trunk
    Particle create_trunk_particle(float x, float y, float z,
                                   float size, float mass,
                                   float r, float g, float b);

    // Note: create_constraint() inherited from EntityGenerator base class
};

#endif // TOTEM_GENERATOR_H
