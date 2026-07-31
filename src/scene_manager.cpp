#include "scene_manager.h"
#include "core/particle_system.h"
#include "debug_control.h"
#include <iostream>

SceneManager::SceneManager()
    : particle_system_(nullptr)
    , player_entity_id_(-1)
{
}

SceneManager::~SceneManager() {
    // Nothing to clean up - we don't own the particle system
}

void SceneManager::create_initial_scene() {
    DEBUG_LOG("Setting up initial scene...");
    
    // Don't add any automatic particles - let applications control exactly what's created
    // This ensures test scenarios and games have full control over particle creation
    DEBUG_LOG("Scene initialized empty - application will populate");
}

void SceneManager::clear_scene() {
    if (!particle_system_) return;
    
    // Remove all tracked entities
    for (int id : scene_entities_) {
        if (id >= 0 && id < static_cast<int>(particle_system_->count())) {
            // Note: ParticleSystem doesn't have remove by ID yet
            // For now, we just clear the entire system
        }
    }
    
    // Clear all particles
    while (particle_system_->count() > 0) {
        particle_system_->remove_particle(particle_system_->count() - 1);
    }
    
    scene_entities_.clear();
    player_entity_id_ = -1;
}

void SceneManager::reset_scene() {
    clear_scene();
    create_initial_scene();
}

int SceneManager::create_player_entity(float x, float y, float z) {
    if (!particle_system_) return -1;
    
    Particle player = {};
    player.x = x;
    player.y = y;
    player.z = z;
    player.size = 1.0f;
    player.r = 0.2f;
    player.g = 0.5f;
    player.b = 1.0f;
    
    player_entity_id_ = add_tracked_particle(player);
    return player_entity_id_;
}

int SceneManager::create_light_entity(float x, float y, float z, float intensity, float r, float g, float b) {
    if (!particle_system_) return -1;
    
    Particle light = {};
    light.x = x;
    light.y = y;
    light.z = z;
    light.size = 0.5f;
    light.r = r;
    light.g = g;
    light.b = b;
    light.is_light_source = true;
    light.emission_strength = intensity;
    
    return add_tracked_particle(light);
}

int SceneManager::create_cube_entity(float x, float y, float z, float size, float r, float g, float b) {
    if (!particle_system_) return -1;
    
    Particle cube = {};
    cube.x = x;
    cube.y = y;
    cube.z = z;
    cube.size = size;
    cube.r = r;
    cube.g = g;
    cube.b = b;
    
    return add_tracked_particle(cube);
}

int SceneManager::create_wall_entity(float x, float y, float z, float width, float height) {
    if (!particle_system_) return -1;

    // Create a wall as a stretched cube
    Particle wall = {};
    wall.x = x;
    wall.y = y;
    wall.z = z;
    wall.size = width;  // Use size for width
    wall.r = 0.6f;
    wall.g = 0.6f;
    wall.b = 0.6f;

    return add_tracked_particle(wall);
}

int SceneManager::create_ground_patch(float x, float y, float radius, int density,
                                       const GroundParticleConfig& config) {
    if (!particle_system_) return -1;

    // Create ground particles using ParticleSystem factory
    auto particle_ids = particle_system_->create_ground_particles(x, y, radius, density, config);

    // Track all created particles
    for (int id : particle_ids) {
        scene_entities_.push_back(id);
    }

    // Return the first particle ID as representative
    // Games can use KG to bind all particles to a single entity
    return particle_ids.empty() ? -1 : particle_ids[0];
}

void SceneManager::create_test_cubes(int count, float spacing) {
    for (int i = 0; i < count; ++i) {
        float x = (i % 5) * spacing - 2 * spacing;  // 5 cubes per row
        float y = (i / 5) * spacing - 2 * spacing;  // Multiple rows
        create_cube_entity(x, y, 0, 1.0f, 0.5f + (i * 0.1f), 0.3f, 0.3f);
    }
}

void SceneManager::create_light_sources(int count, float height) {
    for (int i = 0; i < count; ++i) {
        float angle = (i * 2.0f * 3.14159f) / count;
        float x = 5.0f * cos(angle);
        float y = 5.0f * sin(angle);
        create_light_entity(x, y, height, 10000000.0f);  // Physics-based intensity
    }
}

int SceneManager::get_entity_count() const {
    return static_cast<int>(scene_entities_.size());
}

void SceneManager::load_empty_scene() {
    clear_scene();
    // Just empty - useful for tests that want to control everything
}

void SceneManager::load_basic_test_scene() {
    clear_scene();
    
    // Create player at origin
    create_player_entity(0, 0, 1);
    
    // Create a light above
    create_light_entity(0, 0, 5, 10000000.0f);
    
    // Create a few test cubes
    create_cube_entity(3, 0, 0, 1.0f, 0.8f, 0.2f, 0.2f);  // Red
    create_cube_entity(-3, 0, 0, 1.0f, 0.2f, 0.8f, 0.2f);  // Green
    create_cube_entity(0, 3, 0, 1.0f, 0.2f, 0.2f, 0.8f);  // Blue
    create_cube_entity(0, -3, 0, 1.0f, 0.8f, 0.8f, 0.2f);  // Yellow
}

void SceneManager::load_lighting_test_scene() {
    clear_scene();
    
    // Create player
    create_player_entity(0, 0, 1);
    
    // Create multiple colored lights
    create_light_entity(5, 0, 3, 10000000.0f, 1.0f, 0.0f, 0.0f);  // Red light
    create_light_entity(-5, 0, 3, 10000000.0f, 0.0f, 1.0f, 0.0f);  // Green light
    create_light_entity(0, 5, 3, 10000000.0f, 0.0f, 0.0f, 1.0f);  // Blue light
    create_light_entity(0, -5, 3, 10000000.0f, 1.0f, 1.0f, 0.0f);  // Yellow light
    
    // Create grid of cubes
    create_test_cubes(16, 2.0f);
}

void SceneManager::load_physics_test_scene() {
    clear_scene();
    
    // Create player
    create_player_entity(0, 0, 1);
    
    // Create light
    create_light_entity(0, 0, 10, 10000000.0f);
    
    // Create walls forming a room
    create_wall_entity(10, 0, 0, 1, 20);   // East wall
    create_wall_entity(-10, 0, 0, 1, 20);  // West wall
    create_wall_entity(0, 10, 0, 20, 1);   // North wall
    create_wall_entity(0, -10, 0, 20, 1);  // South wall
    
    // Create some obstacles
    create_cube_entity(3, 3, 0, 2.0f, 0.7f, 0.7f, 0.7f);
    create_cube_entity(-3, -3, 0, 2.0f, 0.7f, 0.7f, 0.7f);
    create_cube_entity(-3, 3, 0, 1.5f, 0.5f, 0.5f, 0.5f);
    create_cube_entity(3, -3, 0, 1.5f, 0.5f, 0.5f, 0.5f);
}

int SceneManager::add_tracked_particle(const Particle& p) {
    if (!particle_system_) return -1;
    
    particle_system_->add_particle(p);
    int id = particle_system_->count() - 1;
    scene_entities_.push_back(id);
    return id;
}