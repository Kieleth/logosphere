#ifndef SCENE_CHUNK_GENERATOR_H
#define SCENE_CHUNK_GENERATOR_H

#include "logosphere/worldgen/chunk_system.h"
#include <vector>
#include <mutex>

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Generic scene chunk generator - loads entities from KG
// KG is source of truth: entities stored with chunk_x, chunk_y properties
// When chunk loads, queries KG and creates particles from entity properties
// Works with any game - logosphere knows nothing about Eden specifically
class SceneChunkGenerator {
public:
    SceneChunkGenerator();
    ~SceneChunkGenerator();

    // Initialize with engine and KG
    void initialize(Engine* engine, kg::KGModule* kg);

    // Update chunks based on observer position (delegates to ChunkSystem)
    void update(float observer_x, float observer_y);

    // Update with camera velocity for predictive pre-loading (Phase 3)
    void update_with_prediction(float observer_x, float observer_y,
                                float velocity_x, float velocity_y,
                                float predict_time = 2.0f);

    // Pre-load chunks around a position (for startup)
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // Configuration
    void set_load_radius(float meters);
    void set_unload_radius(float meters);
    void set_chunk_size(float meters);
    void set_update_frequency(int frames);

    // Enable/disable
    void set_enabled(bool enabled);
    bool is_enabled() const;

    // Query
    int get_active_chunk_count() const;

    // Manual control
    void clear_all_chunks();

    // Queue entity for activation on next update cycle
    // Thread-safe: can be called from LLM worker thread
    // Entity will be activated when its chunk is loaded
    void queue_entity_activation(kg::EntityID entity_id);

    // Activate entity immediately if its chunk is already loaded
    // Called after creating entities (trees, etc.) to make them appear instantly
    // NOTE: Prefer queue_entity_activation() for thread safety
    void activate_entity_now(kg::EntityID entity_id);

private:
    // Process pending activations (called from update)
    void process_pending_activations();
    // Callbacks for ChunkSystem
    ChunkData create_scene_chunk(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity);
    void destroy_scene_chunk(const ChunkData& data);

    Engine* engine_;
    kg::KGModule* kg_;

    // Generic chunk system (handles spatial logic)
    ChunkSystem chunk_system_;

    // Pending entity activations (thread-safe queue)
    std::vector<kg::EntityID> pending_activations_;
    std::mutex pending_mutex_;
};

#endif // SCENE_CHUNK_GENERATOR_H
