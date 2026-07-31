#ifndef WORLDGEN_SYSTEM_H
#define WORLDGEN_SYSTEM_H

#include "logosphere/worldgen/floor_generator.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/snake_generator.h"
#include "logosphere/worldgen/butterfly_generator.h"
#include "logosphere/worldgen/totem_generator.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/rock_generator.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include "logosphere/worldgen/fallen_tree_generator.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }
namespace logosphere { class EventBus; }

// World generation system - manages all procedural generators
class WorldGenSystem {
public:
    WorldGenSystem();
    ~WorldGenSystem();

    // Initialize with engine and KG
    void initialize(Engine* engine, kg::KGModule* kg,
                    logosphere::EventBus* event_bus = nullptr);

    // Update all generators based on observer position
    void update(float observer_x, float observer_y);

    // Update with camera velocity for predictive pre-loading (Phase 3)
    void update(float observer_x, float observer_y,
                float velocity_x, float velocity_y);

    // Pre-load chunks around a position (for startup initialization)
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // Access to individual generators
    FloorGenerator& get_floor_generator() { return floor_generator_; }
    StrataFloorGenerator& get_strata_floor_generator() { return strata_floor_generator_; }
    TreeGenerator& get_tree_generator() { return tree_generator_; }
    PhysicsTreeGenerator& get_physics_tree_generator() { return physics_tree_generator_; }
    HumanoidGenerator& get_humanoid_generator() { return humanoid_generator_; }
    OrganicGenerator& get_organic_generator() { return organic_generator_; }
    SnakeGenerator& get_snake_generator() { return snake_generator_; }
    ButterflyGenerator& get_butterfly_generator() { return butterfly_generator_; }
    TotemGenerator& get_totem_generator() { return totem_generator_; }
    SceneChunkGenerator& get_scene_generator() { return scene_generator_; }
    RockGenerator& get_rock_generator() { return rock_generator_; }
    PhysicsRockGenerator& get_physics_rock_generator() { return physics_rock_generator_; }
    FallenTreeGenerator& get_fallen_tree_generator() { return fallen_tree_generator_; }

    // Future generators will be added here:
    // BiomeSystem& get_biome_system() { return biome_system_; }

private:
    Engine* engine_;
    kg::KGModule* kg_;

    // Procedural generators
    FloorGenerator floor_generator_;
    StrataFloorGenerator strata_floor_generator_;
    TreeGenerator tree_generator_;
    PhysicsTreeGenerator physics_tree_generator_;
    HumanoidGenerator humanoid_generator_;
    OrganicGenerator organic_generator_;
    SnakeGenerator snake_generator_;
    ButterflyGenerator butterfly_generator_;
    TotemGenerator totem_generator_;
    SceneChunkGenerator scene_generator_;  // Generic KG-based entity loader
    RockGenerator rock_generator_;
    PhysicsRockGenerator physics_rock_generator_;
    FallenTreeGenerator fallen_tree_generator_;
    // etc.
};

#endif // WORLDGEN_SYSTEM_H
