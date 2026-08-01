#include "logosphere/worldgen/planet_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include <cmath>
#include <iostream>
#include <string>

PlanetSpec PlanetSpec::asteroid_b612() {
    PlanetSpec spec;
    spec.radius = 3.0f;
    spec.stone_size = 0.45f;
    spec.crust_r = 0.66f; spec.crust_g = 0.44f; spec.crust_b = 0.26f;
    spec.core_r = 0.38f; spec.core_g = 0.24f; spec.core_b = 0.16f;
    return spec;
}

float PlanetGenerator::random_range(float min, float max) {
    rng_state_ = (1103515245u * rng_state_ + 12345u) % 2147483648u;
    float normalized = static_cast<float>(rng_state_) / 2147483648.0f;
    return min + normalized * (max - min);
}

kg::EntityID PlanetGenerator::generate_planet(float cx, float cy, float cz,
                                              const PlanetSpec& spec) {
    if (!engine_ || !kg_) {
        std::cerr << "[PlanetGenerator] Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }
    if (cz <= spec.radius) {
        std::cerr << "[PlanetGenerator] Center z " << cz
                  << " does not clear the turtle for radius "
                  << spec.radius << " - raising to float free."
                  << std::endl;
        cz = spec.radius + 1.0f;
    }

    // createEntityAtPosition (not createEntity): it stamps chunk_x /
    // chunk_y, which activate_entity_now requires to place the body
    // in a chunk. Without them the entity and its constraints exist
    // in the KG but no particle ever reaches the world.
    kg::EntityID entity = kg_->createEntityAtPosition("Planet", cx, cy);
    kg_->setProperty(entity, "z", std::to_string(cz));
    kg_->setProperty(entity, "planet_radius", std::to_string(spec.radius));

    // Core: one kinematic sphere. No writer ever moves it, so it
    // holds its station; the crust hangs off it through constraints.
    Particle core = {};
    core.shape = ParticleShape::SPHERE;
    core.x = cx; core.y = cy; core.z = cz;
    core.size = (spec.radius - spec.stone_size * 0.45f) * 2.0f;
    core.r = spec.core_r; core.g = spec.core_g; core.b = spec.core_b;
    core.a = 1.0f;
    core.SetMaterial(Materials::Type::STONE);
    core.solver_mode = ParticleSolverMode::KINEMATIC;
    core.is_at_rest = true;
    kg::KGParticleID core_id =
        kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(core_id, core);

    // Crust: stones on a Fibonacci sphere, each tilted to its radial
    // frame and bonded hub-and-spoke to the core.
    float area = 4.0f * static_cast<float>(M_PI) * spec.radius * spec.radius;
    int count = std::max(24, static_cast<int>(
        area / (spec.stone_size * spec.stone_size * 1.7f)));
    const float golden = 2.399963f;  // golden angle (rad)
    int made = 0;
    for (int i = 0; i < count; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / count;
        float polar = std::acos(1.0f - 2.0f * t);
        float azim = golden * i;
        float sx = std::sin(polar) * std::cos(azim);
        float sy = std::sin(polar) * std::sin(azim);
        float sz = std::cos(polar);

        float s = spec.stone_size *
                  random_range(1.0f - spec.stone_jitter,
                               1.0f + spec.stone_jitter);
        float r_at = spec.radius - s * 0.25f;

        Particle stone = {};
        stone.shape = ParticleShape::BOX;
        stone.x = cx + sx * r_at;
        stone.y = cy + sy * r_at;
        stone.z = cz + sz * r_at;
        stone.width = s;
        stone.height = s;
        stone.thickness = s * random_range(0.5f, 0.75f);
        stone.size = s;
        stone.r = spec.crust_r + random_range(-spec.color_variance,
                                              spec.color_variance);
        stone.g = spec.crust_g + random_range(-spec.color_variance,
                                              spec.color_variance);
        stone.b = spec.crust_b + random_range(-spec.color_variance,
                                              spec.color_variance);
        stone.a = 1.0f;
        // Tilt the stone's up-axis onto the radial direction, plus a
        // little scatter so the crust reads as settled rubble.
        stone.rotation_y = polar + random_range(-0.12f, 0.12f);
        stone.rotation_z = azim + random_range(-0.35f, 0.35f);
        stone.SetMaterial(Materials::Type::STONE);
        stone.is_at_rest = true;
        kg::KGParticleID stone_id =
            kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(stone_id, stone);
        create_constraint(entity, core_id, stone_id, spec.stiffness);
        ++made;
    }

    on_entity_created(entity);
    std::cout << "[PlanetGenerator] Planet at (" << cx << ", " << cy
              << ", " << cz << ") radius=" << spec.radius << "m crust="
              << made << " stones" << std::endl;

    engine_->get_worldgen_system().get_scene_generator()
        .queue_entity_activation(entity);
    return entity;
}
