#include "../entity_manager.h"
#include <iostream>

/**
 * Simple Entity Activator
 *
 * Handles simple entities that store their particle data as KG properties:
 * - cube: position, size, color
 * - light: position, size, color, emission properties
 *
 * Builds a single particle from KG properties.
 */
ActivationResult simple_entity_activator(
    kg::KGModule* kg,
    kg::EntityID entity_id,
    ParticleSystem& particle_system
) {
    ActivationResult result;
    std::string type = kg->getType(entity_id);

    // Build particle from KG properties
    Particle p;
    p.x = std::stof(kg->getProperty(entity_id, "x"));
    p.y = std::stof(kg->getProperty(entity_id, "y"));
    p.z = std::stof(kg->getProperty(entity_id, "z"));
    p.size = std::stof(kg->getProperty(entity_id, "size"));
    // CRITICAL: Physics uses width/height/thickness for AABB collision, NOT size!
    // For simple entities (cubes), all dimensions equal size
    p.width = p.size;
    p.height = p.size;
    p.thickness = p.size;
    p.r = std::stof(kg->getProperty(entity_id, "r"));
    p.g = std::stof(kg->getProperty(entity_id, "g"));
    p.b = std::stof(kg->getProperty(entity_id, "b"));
    p.a = 1.0f;  // Default opaque

    // Type-specific properties
    if (type == "light") {
        p.is_light_source = true;
        p.emission_strength = std::stof(kg->getProperty(entity_id, "emission_strength"));
        p.emission_radius = std::stof(kg->getProperty(entity_id, "emission_radius"));
        std::cout << "[SimpleEntityActivator] Light " << entity_id
                  << ": strength=" << p.emission_strength
                  << " radius=" << p.emission_radius << std::endl;
    } else {
        p.is_light_source = false;
        p.emission_strength = 0.0f;
    }

    // ⚠️ WARNING: DO NOT SET entity_id! See particle.h MONSTER WARNING!
    // Setting entity_id breaks GPU shadow pipeline (Dec 2024 bug).
    // Object picking is disabled until this is fixed.
    // p.entity_id = entity_id;  // DISABLED - BREAKS SHADOWS!

    // Check for mass property - if set, entity is physics-based
    // Mass auto-calculates from volume and material_density in add_particle()
    std::string mass_str = kg->getProperty(entity_id, "mass");
    if (!mass_str.empty() && std::stof(mass_str) > 0.0f) {
        // Physics-based entity: affected by gravity and forces
        p.SetMaterial(Materials::Type::WOOD_HARD);  // Default physics material
        p.is_at_rest = false;  // Physics entities can move
        std::cout << "[SimpleEntityActivator] Physics " << type << " " << entity_id
                  << ": mass=" << p.GetMass() << "kg" << std::endl;
    } else {
        // Static prop: very heavy (high density, settles fast, still movable)
        p.SetMaterial(Materials::Type::HEAVY_STATIC);
        p.is_at_rest = true;  // Static props start at rest
    }

    result.particles.push_back(p);
    // Note: Simple entities don't have pre-existing KG particles
    // They'll be created when particle is added to system
    return result;
}
