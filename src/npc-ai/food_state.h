// Food State - Tracks consumption of edible particles
//
// Separate from Particle to keep particle.h pure.
// FoodState wraps a particle and tracks how much has been eaten.
//
// See logomancers/docs/npc_consumables.md for design.

#ifndef FOOD_STATE_H
#define FOOD_STATE_H

#include "../particle.h"
#include <cmath>

namespace npc_ai {

// Consumption state for an edible particle
// Created when shambler starts eating, destroyed when food depleted
struct FoodState {
    int particle_id = -1;          // Which particle this tracks
    float total_mass_g = 0.0f;     // Initial mass (volume × density)
    float remaining_g = 0.0f;      // How much left to eat
    float toughness = 1.0f;        // Bite time multiplier: 0.5 (soft) to 2.0 (bony)
    float volume_cm3 = 0.0f;       // Food volume for density calculations

    // Bite tracking (for executor state)
    float current_bite_timer = 0.0f;  // Time until current bite completes
    bool is_chewing = false;           // Currently processing a bite

    // Create FoodState from particle dimensions
    // toughness: 0.5 (soft rot) to 2.0 (dried/bony), default 1.0
    static FoodState from_particle(const Particle& p, float toughness = 1.0f) {
        FoodState fs;
        fs.particle_id = static_cast<int>(p.particle_id);

        // Volume in cm³ (particle dimensions are in meters)
        // 1 m³ = 1,000,000 cm³
        float volume_m3 = p.width * p.height * p.thickness;
        fs.volume_cm3 = volume_m3 * 1e6f;

        // Mass in grams (density is kg/m³, want grams)
        // mass_kg = volume_m3 × density_kg_m3
        // mass_g = mass_kg × 1000
        float mass_kg = volume_m3 * p.material_density;
        fs.total_mass_g = mass_kg * 1000.0f;
        fs.remaining_g = fs.total_mass_g;

        fs.toughness = toughness;
        return fs;
    }

    // Check if fully consumed
    bool is_depleted() const { return remaining_g <= 0.0f; }

    // Get density in g/cm³ for bite calculations
    float density_g_cm3() const {
        if (volume_cm3 <= 0) return 0.9f;  // Default rotting flesh
        return total_mass_g / volume_cm3;
    }

    // Calculate how much mass one bite removes
    // mouth_volume_cm3: shambler mouth capacity
    float bite_mass_g(float mouth_volume_cm3) const {
        return mouth_volume_cm3 * density_g_cm3();
    }

    // Take a bite, return true if food still remains
    bool take_bite(float mouth_volume_cm3) {
        float bite = bite_mass_g(mouth_volume_cm3);
        remaining_g -= bite;
        return remaining_g > 0.0f;
    }
};

} // namespace npc_ai

#endif // FOOD_STATE_H
