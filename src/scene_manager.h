#pragma once

#include "core/particle_system.h"
#include <functional>
#include <vector>

// Forward declarations
class ParticleSystem;

// SceneManager: Scene setup and management
//
// Following principles from successful refactoring:
// - Single responsibility: ONLY manages scene creation and entities
// - No game logic, just scene composition
// - Clean interface for creating different entity types
//
// This was extracted from Engine.cpp where scene setup was mixed
// with initialization and other responsibilities.

class SceneManager {
public:
    SceneManager();
    ~SceneManager();
    
    // Set the particle system to work with
    void set_particle_system(ParticleSystem* ps) { particle_system_ = ps; }
    
    // Scene lifecycle
    void create_initial_scene();
    void clear_scene();
    void reset_scene();
    
    // Entity creation helpers
    int create_player_entity(float x, float y, float z);
    int create_light_entity(float x, float y, float z, float intensity, float r = 1.0f, float g = 1.0f, float b = 1.0f);
    int create_cube_entity(float x, float y, float z, float size, float r = 0.5f, float g = 0.5f, float b = 0.5f);
    int create_wall_entity(float x, float y, float z, float width, float height);

    // Ground particle creation (game entity with KG integration)
    int create_ground_patch(float x, float y, float radius, int density,
                            const GroundParticleConfig& config = GroundParticleConfig());
    
    // Batch entity creation
    void create_test_cubes(int count, float spacing);
    void create_light_sources(int count, float height);
    
    // Scene queries
    int get_entity_count() const;
    bool has_player() const { return player_entity_id_ >= 0; }
    int get_player_id() const { return player_entity_id_; }
    
    // Scene presets for testing
    void load_empty_scene();
    void load_basic_test_scene();  // Player + light + a few cubes
    void load_lighting_test_scene();  // Multiple lights and objects
    void load_physics_test_scene();  // Walls and obstacles
    
    // Track created entities
    const std::vector<int>& get_scene_entities() const { return scene_entities_; }
    
private:
    ParticleSystem* particle_system_;
    int player_entity_id_;
    std::vector<int> scene_entities_;  // Track all entities in current scene
    
    // Helper to add particle and track it
    int add_tracked_particle(const Particle& p);
};