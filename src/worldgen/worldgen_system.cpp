#include "logosphere/worldgen/worldgen_system.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"
#include "optimization_flags.h"
#include <iostream>

WorldGenSystem::WorldGenSystem()
    : engine_(nullptr)
    , kg_(nullptr)
{
}

WorldGenSystem::~WorldGenSystem() {
}

void WorldGenSystem::initialize(Engine* engine, kg::KGModule* kg,
                                logosphere::EventBus* event_bus) {
    engine_ = engine;
    kg_ = kg;

    // Initialize all generators
    floor_generator_.initialize(engine, kg);
    strata_floor_generator_.initialize(engine, kg);
    tree_generator_.initialize(engine, kg);
    physics_tree_generator_.initialize(engine);  // Physics tree doesn't use KG (gluon-based)
    physics_rock_generator_.initialize(engine);  // Physics rock doesn't use KG (gluon-based)
    humanoid_generator_.initialize(engine, kg);
    organic_generator_.initialize(engine, kg);
    snake_generator_.initialize(engine, kg);
    butterfly_generator_.initialize(engine, kg);
    totem_generator_.initialize(engine, kg);
    scene_generator_.initialize(engine, kg);
    rock_generator_.initialize(engine, kg);
    fallen_tree_generator_.initialize(engine, kg);

    // Wire event bus to EntityGenerator subclasses (SpawnEvent on entity creation)
    if (event_bus) {
        tree_generator_.set_event_bus(event_bus);
        humanoid_generator_.set_event_bus(event_bus);
        organic_generator_.set_event_bus(event_bus);
        totem_generator_.set_event_bus(event_bus);
        rock_generator_.set_event_bus(event_bus);
        fallen_tree_generator_.set_event_bus(event_bus);
    }

    std::cout << "[WorldGenSystem] Initialized" << std::endl;
}

void WorldGenSystem::update(float observer_x, float observer_y) {
    // Override observer position to follow input target entity
    // Uses generic "center_particle" property - works for any entity type
    kg::EntityID input_target = engine_->get_input_target_entity();
    if (input_target != kg::INVALID_ENTITY) {
        auto center_str = kg_->getProperty(input_target, "center_particle");
        if (!center_str.empty()) {
            unsigned int center_id = static_cast<unsigned int>(std::stoul(center_str));
            auto& particle_system = engine_->get_particle_system();

            // THREAD SAFETY: Use ReadView for safe particle access
            auto particles_view = particle_system.lock_particles_for_read();
            if (center_id < particles_view.size()) {
                const Particle& center = particles_view[center_id];
                observer_x = center.x;
                observer_y = center.y;
            }
        }
    }
    // else: use provided observer position (camera fallback)

    // Update all active generators (no prediction)
    floor_generator_.update(observer_x, observer_y);
    strata_floor_generator_.update(observer_x, observer_y);
    scene_generator_.update(observer_x, observer_y);
}

void WorldGenSystem::update(float observer_x, float observer_y,
                            float velocity_x, float velocity_y) {
    // Override observer position to follow input target entity
    kg::EntityID input_target = engine_->get_input_target_entity();
    if (input_target != kg::INVALID_ENTITY) {
        auto center_str = kg_->getProperty(input_target, "center_particle");
        if (!center_str.empty()) {
            unsigned int center_id = static_cast<unsigned int>(std::stoul(center_str));
            auto& particle_system = engine_->get_particle_system();

            auto particles_view = particle_system.lock_particles_for_read();
            if (center_id < particles_view.size()) {
                const Particle& center = particles_view[center_id];
                observer_x = center.x;
                observer_y = center.y;
            }
        }
    }

    // Update floor generator (no prediction - floor has different load semantics)
    floor_generator_.update(observer_x, observer_y);
    strata_floor_generator_.update(observer_x, observer_y);

    // Update scene generator with prediction if enabled
    if (Optimizations::USE_CAMERA_PREDICTION && Optimizations::USE_ASYNC_CHUNK_LOADING) {
        scene_generator_.update_with_prediction(observer_x, observer_y,
                                                 velocity_x, velocity_y);
    } else {
        scene_generator_.update(observer_x, observer_y);
    }
}

void WorldGenSystem::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    // Pre-load chunks for all active generators
    std::cout << "[WorldGenSystem] Pre-loading chunks around (" << world_x << "," << world_y << ")" << std::endl;
    floor_generator_.preload_chunks_around(world_x, world_y, radius_chunks);
    strata_floor_generator_.preload_chunks_around(world_x, world_y, radius_chunks);
    scene_generator_.preload_chunks_around(world_x, world_y, radius_chunks);
    // tree_generator_.preload_chunks_around(world_x, world_y, radius_chunks);
}
