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
    // Sit the core just under the stone layer. It used to surface a
    // full 6.5% of the radius below the crust, so every gap looked
    // through to a distinct inner sphere - the thing that made the
    // body read as gravel heaped around a ball. Clearance is measured
    // against whichever shell is outermost.
    float inner_gap = spec.stone_size * 0.55f;
    if (spec.surface_ratio > 0.0f && spec.surface_density > 0.0f)
        inner_gap = spec.stone_size * spec.surface_ratio * 0.6f;
    core.size = (spec.radius - inner_gap) * 2.0f;
    core.r = spec.crust_r * spec.core_shade;
    core.g = spec.crust_g * spec.core_shade;
    core.b = spec.crust_b * spec.core_shade;
    core.a = 1.0f;
    core.SetMaterial(Materials::Type::STONE);
    core.solver_mode = ParticleSolverMode::KINEMATIC;
    core.is_at_rest = true;
    kg::KGParticleID core_id =
        kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
    kg_->setKGParticleData(core_id, core);

    // Crust: stones on a Fibonacci sphere, each tilted to its radial
    // frame and bonded hub-and-spoke to the core. Laid down in two
    // shells - the structural plates, then a finer skin filling their
    // gaps - because one shell of big plates reads as rubble, not as
    // a world.
    const float golden = 2.399963f;  // golden angle (rad)
    float area = 4.0f * static_cast<float>(M_PI) * spec.radius * spec.radius;
    int made = 0;

    auto clamp01 = [](float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };

    // One shell. `flatten` scales thickness (plates vs slabs),
    // `scatter` is the azimuthal jitter that reads as unsettled
    // rubble, `tint` brightens the palette for dust over rock.
    auto place_shell = [&](int count, float size, float flatten,
                           float scatter, float tint, float jitter) {
        for (int i = 0; i < count; ++i) {
            float t = (static_cast<float>(i) + 0.5f) / count;
            float polar = std::acos(1.0f - 2.0f * t);
            float azim = golden * i;
            float sx = std::sin(polar) * std::cos(azim);
            float sy = std::sin(polar) * std::sin(azim);
            float sz = std::cos(polar);

            float s = size * random_range(1.0f - jitter, 1.0f + jitter);
            float r_at = spec.radius - s * 0.25f;

            Particle stone = {};
            stone.shape = ParticleShape::BOX;
            stone.x = cx + sx * r_at;
            stone.y = cy + sy * r_at;
            stone.z = cz + sz * r_at;
            stone.width = s;
            stone.height = s;
            stone.thickness = s * flatten * random_range(0.85f, 1.15f);
            stone.size = s;
            stone.r = clamp01(spec.crust_r * tint +
                              random_range(-spec.color_variance,
                                           spec.color_variance));
            stone.g = clamp01(spec.crust_g * tint +
                              random_range(-spec.color_variance,
                                           spec.color_variance));
            stone.b = clamp01(spec.crust_b * tint +
                              random_range(-spec.color_variance,
                                           spec.color_variance));
            stone.a = 1.0f;
            // Tilt the stone's up-axis onto the radial direction, plus
            // scatter so the crust reads as settled, not tiled.
            stone.rotation_y = polar + random_range(-scatter * 0.34f,
                                                     scatter * 0.34f);
            stone.rotation_z = azim + random_range(-scatter, scatter);
            stone.SetMaterial(Materials::Type::STONE);
            stone.is_at_rest = true;
            kg::KGParticleID stone_id =
                kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
            kg_->setKGParticleData(stone_id, stone);
            create_constraint(entity, core_id, stone_id, spec.stiffness);
            ++made;
        }
    };

    // Structural plates (unchanged: this is the body's character).
    int base_count = std::max(24, static_cast<int>(
        area / (spec.stone_size * spec.stone_size * 1.7f)));
    place_shell(base_count, spec.stone_size, 0.625f, 0.35f, 1.0f,
                spec.stone_jitter);

    // Fine skin: smaller, flatter, tighter, dustier.
    int skin_count = 0;
    if (spec.surface_ratio > 0.0f && spec.surface_density > 0.0f) {
        float fine = spec.stone_size * spec.surface_ratio;
        skin_count = static_cast<int>(
            area / (fine * fine * 1.7f) * spec.surface_density);
        // Half the size jitter of the plates: the skin is what draws
        // the limb, and varied protrusion there is what reads as a
        // spiky gravel edge instead of a planet's smooth curve.
        place_shell(skin_count, fine, 0.42f, 0.15f, spec.surface_tint,
                    spec.stone_jitter * 0.5f);
    }

    on_entity_created(entity);
    std::cout << "[PlanetGenerator] Planet at (" << cx << ", " << cy
              << ", " << cz << ") radius=" << spec.radius << "m crust="
              << made << " stones (" << base_count << " plates + "
              << skin_count << " skin)" << std::endl;

    engine_->get_worldgen_system().get_scene_generator()
        .queue_entity_activation(entity);
    return entity;
}
