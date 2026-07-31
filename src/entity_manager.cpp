#include "entity_manager.h"
#include "entities/entity_activators.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/events/event_bus.h"
#include <iostream>

EntityManager::EntityManager(kg::KGModule* kg, ParticleSystem& particle_system,
                             PhysicsSystem* physics, logosphere::EventBus* event_bus)
    : kg_(kg), particle_system_(particle_system), physics_(physics), event_bus_(event_bus) {
    // Register built-in entity types (CamelCase matches ontology)
    register_type("Tree", vegetation_activator);
    register_type("Cube", simple_entity_activator);
    register_type("LightSource", simple_entity_activator);
    register_type("Totem", vegetation_activator);
    register_type("Humanoid", vegetation_activator);
    register_type("GrassCluster", vegetation_activator);
    register_type("TallGrass", vegetation_activator);
    register_type("GrassPatch", vegetation_activator);
    register_type("Grass", vegetation_activator);
    register_type("Rock", rock_activator);
    register_type("PhysicsRock", physics_rock_activator);
    register_type("PhysicsTree", physics_tree_activator);
    register_type("FallenTree", vegetation_activator);
    register_type("Weapon", rock_activator);
    register_type("Campfire", vegetation_activator);
    register_type("CelestialBody", simple_entity_activator);
}

void EntityManager::register_type(const std::string& type, EntityActivator activator) {
    type_registry_[type] = activator;
    std::cout << "[EntityManager] Registered entity type: " << type << std::endl;
}

ActivationResult EntityManager::activate_entity(kg::EntityID entity_id) {
    std::string type = kg_->getType(entity_id);

    auto it = type_registry_.find(type);
    if (it == type_registry_.end()) {
        std::cerr << "[EntityManager] WARNING: No activator registered for type '" << type
                  << "' (entity " << entity_id << ") - skipping" << std::endl;
        return {};  // Return empty result - graceful degradation
    }

    std::cout << "[EntityManager] Activating entity " << entity_id << " (type=" << type << ")" << std::endl;
    auto result = it->second(kg_, entity_id, particle_system_);

    if (event_bus_ && !result.particles.empty()) {
        logosphere::ontology::SpawnEvent evt;
        evt.target_entity_id = std::to_string(entity_id);
        evt.event_type = "SPAWN";
        event_bus_->spawns().emit(std::move(evt));
    }

    return result;
}

void EntityManager::deactivate_entity(kg::EntityID entity_id) {
    // Get all KG particles for this entity (including hierarchical children)
    auto kg_particles = kg_->getEntityKGParticlesRecursive(entity_id);

    if (kg_particles.empty()) {
        return;  // No particles to deactivate
    }

    std::cout << "[EntityManager] Deactivating entity " << entity_id
              << " (" << kg_particles.size() << " particles)" << std::endl;

    // Collect render indices
    std::vector<kg::RenderIndex> render_indices;
    for (kg::KGParticleID kg_id : kg_particles) {
        kg::RenderIndex idx = kg_->getRenderIndex(kg_id);
        if (idx != kg::INVALID_RENDER_INDEX) {
            render_indices.push_back(idx);
        }
    }

    // Queue particles for deletion
    for (kg::RenderIndex idx : render_indices) {
        particle_system_.remove_particle(idx);
    }
}

void EntityManager::create_gluon_from_kg(kg::KGGluonID kg_gluon_id, int render_a, int render_b) {
    if (!physics_) {
        std::cerr << "[EntityManager] ERROR: PhysicsSystem not set, cannot create gluon" << std::endl;
        return;
    }

    if (!kg_->hasKGGluonData(kg_gluon_id)) {
        std::cerr << "[EntityManager] ERROR: KG gluon " << kg_gluon_id << " has no stored data!" << std::endl;
        return;
    }

    kg::KGGluonData data = kg_->getKGGluonData(kg_gluon_id);

    // Create appropriate gluon type based on stored data
    switch (data.type) {
        case kg::KGGluonType::NAIL: {
            auto gluon = std::make_unique<NailGluon>();
            gluon->offset_a = Vec3(data.offset_a_x, data.offset_a_y, data.offset_a_z);
            gluon->offset_b = Vec3(data.offset_b_x, data.offset_b_y, data.offset_b_z);
            gluon->target_distance = data.target_distance;
            gluon->stiffness = data.stiffness;
            gluon->damping = data.damping;
            gluon->rotate_offsets = data.rotate_offsets;
            gluon->angular_stiffness = data.angular_stiffness;
            gluon->angular_damping = data.angular_damping;
            gluon->target_relative_rotation = data.target_relative_rotation;
            gluon->enable_angular_constraint = data.enable_angular_constraint;
            gluon->max_relative_rotation = data.max_relative_rotation;
            gluon->breaking_force = data.breaking_force;
            physics_->add_gluon_between(render_a, render_b, std::move(gluon));
            break;
        }
        case kg::KGGluonType::ORGANIC: {
            auto gluon = std::make_unique<OrganicGluon>();
            gluon->offset_a = Vec3(data.offset_a_x, data.offset_a_y, data.offset_a_z);
            gluon->offset_b = Vec3(data.offset_b_x, data.offset_b_y, data.offset_b_z);
            gluon->target_distance = data.target_distance;
            gluon->stiffness = data.stiffness;
            gluon->damping = data.damping;
            gluon->rotate_offsets = data.rotate_offsets;
            gluon->angular_stiffness = data.angular_stiffness;
            gluon->angular_damping = data.angular_damping;
            gluon->target_relative_rotation = data.target_relative_rotation;
            gluon->enable_angular_constraint = data.enable_angular_constraint;
            gluon->max_relative_rotation = data.max_relative_rotation;
            gluon->contact_area = data.contact_area;
            physics_->add_gluon_between(render_a, render_b, std::move(gluon));
            break;
        }
        case kg::KGGluonType::ELASTIC: {
            auto gluon = std::make_unique<ElasticGluon>();
            gluon->offset_a = Vec3(data.offset_a_x, data.offset_a_y, data.offset_a_z);
            gluon->offset_b = Vec3(data.offset_b_x, data.offset_b_y, data.offset_b_z);
            gluon->target_distance = data.target_distance;
            gluon->stiffness = data.stiffness;
            gluon->damping = data.damping;
            gluon->rotate_offsets = data.rotate_offsets;
            gluon->angular_stiffness = data.angular_stiffness;
            gluon->angular_damping = data.angular_damping;
            gluon->target_relative_rotation = data.target_relative_rotation;
            gluon->enable_angular_constraint = data.enable_angular_constraint;
            gluon->max_relative_rotation = data.max_relative_rotation;
            gluon->breaking_force = data.breaking_force;
            gluon->max_strain = data.max_strain;
            physics_->add_gluon_between(render_a, render_b, std::move(gluon));
            break;
        }
    }

    std::cout << "[EntityManager] Created gluon " << kg_gluon_id
              << " between render indices " << render_a << " and " << render_b << std::endl;
}
