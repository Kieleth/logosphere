#ifndef CHUNK_SYSTEM_H
#define CHUNK_SYSTEM_H

#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include "logosphere/kg/kg_types.h"
#include "particle.h"

// Forward declaration
class AsyncChunkLoader;

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// === CHUNK CONSTANTS ===
// Default chunk size in meters (50m grid cells for spatial partitioning)
// This is a compile-time constant - won't change within a game instance
constexpr float CHUNK_SIZE = 50.0f;

// Chunk coordinate in discrete chunk space
struct ChunkCoord {
    int x;
    int y;

    ChunkCoord(int x_ = 0, int y_ = 0) : x(x_), y(y_) {}

    // World position of chunk center
    float world_x(float chunk_size) const { return x * chunk_size + chunk_size / 2.0f; }
    float world_y(float chunk_size) const { return y * chunk_size + chunk_size / 2.0f; }

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y;
    }
};

// Hash function for ChunkCoord (for unordered_map)
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.y) << 1);
    }
};

// Particle data prepared by background thread, to be added on main thread
// This separates data preparation (background) from particle creation (main thread)
struct PreparedParticle {
    Particle particle;                       // Particle data ready for add_particle()
    kg::EntityID entity_id;                  // Entity this particle belongs to
    kg::KGParticleID kg_particle_id;         // For reload: existing KG particle ID (0 if first load)
};

// Gluon data prepared by background thread, to be created after particles on main thread
// Uses KGParticleIDs which are resolved to render indices after particles are created
struct PreparedGluon {
    kg::KGGluonID kg_gluon_id;              // KG gluon ID to get gluon data
    kg::KGParticleID kg_particle_a;          // KGParticle A (resolve to render index from KG)
    kg::KGParticleID kg_particle_b;          // KGParticle B (resolve to render index from KG)
};

// Generic chunk data - specialized systems store their own data here
struct ChunkData {
    kg::EntityID kg_entity;                  // KG entity representing this chunk
    std::vector<kg::EntityID> entity_ids;    // Content entities in this chunk (trees, cubes, lights, etc.)
    std::vector<PreparedParticle> prepared_particles;  // Particles to add on main thread
    std::vector<PreparedGluon> prepared_gluons;        // Gluons to create after particles
    void* custom_data = nullptr;             // System-specific data (floor config, biome info, etc.)
};

// Callback types for chunk lifecycle
// create_callback receives existing_entity_id (INVALID_ENTITY if creating new, valid ID if reloading)
using ChunkCreateCallback = std::function<ChunkData(const ChunkCoord&, float chunk_size, kg::EntityID existing_entity_id)>;
using ChunkDestroyCallback = std::function<void(const ChunkData&)>;

// Generic chunk management system
// Handles spatial grid, loading/unloading based on view position, KG integration
// Content generation delegated to callbacks (strategy pattern)
class ChunkSystem {
public:
    ChunkSystem();
    ~ChunkSystem();

    // Initialize with engine and KG
    void initialize(Engine* engine, kg::KGModule* kg, const char* system_name = "chunk_system");

    // Set content generation callbacks
    void set_create_callback(ChunkCreateCallback callback) { create_callback_ = callback; }
    void set_destroy_callback(ChunkDestroyCallback callback) { destroy_callback_ = callback; }

    // Update chunks based on observer position (call each frame or periodically)
    void update(float observer_x, float observer_y);

    // Update with camera velocity for predictive pre-loading (Phase 3: Camera Prediction)
    // velocity_x, velocity_y: camera movement speed in world units/second
    // predict_time: how far ahead to predict (seconds, default 2.0)
    void update_with_prediction(float observer_x, float observer_y,
                                float velocity_x, float velocity_y,
                                float predict_time = 2.0f);

    // Pre-load chunks around a position (useful for startup initialization)
    // radius_chunks: how many chunks in each direction (1 = 3x3 grid, 2 = 5x5 grid, etc.)
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // Configuration
    void set_load_radius(float meters) { load_radius_ = meters; }
    void set_unload_radius(float meters) { unload_radius_ = meters; }
    void set_chunk_size(float meters) { chunk_size_ = meters; }
    void set_update_frequency(int frames) { update_frequency_ = frames; }

    // Enable/disable system
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    // Async loading control (Two-Tier Chunk Streaming Phase 2)
    // Note: set_async_loading starts/stops the background loader thread
    void set_async_loading(bool enabled);
    bool is_async_loading() const { return async_loading_enabled_; }

    // Process ready chunks from async loader (call from main thread after update)
    void apply_ready_chunks();

    // Query
    bool chunk_exists(const ChunkCoord& coord) const;
    const ChunkData* get_chunk(const ChunkCoord& coord) const;
    int get_active_chunk_count() const { return active_chunks_.size(); }

    // Manual control (for testing)
    void create_chunk(const ChunkCoord& coord);
    void destroy_chunk(const ChunkCoord& coord);
    void clear_all_chunks();

    // Add entity to loaded chunk's tracking list
    void add_entity_to_chunk(const ChunkCoord& coord, kg::EntityID entity_id);

    // Utility: convert world to chunk
    ChunkCoord world_to_chunk(float world_x, float world_y) const;

protected:
    // Get all chunks within radius
    std::vector<ChunkCoord> get_chunks_in_radius(float center_x, float center_y, float radius) const;

    // Check if chunk is within radius
    bool is_in_radius(const ChunkCoord& chunk, float center_x, float center_y, float radius) const;

    // Find existing chunk entity in KG for permaworld reload
    kg::EntityID find_existing_chunk_entity(const ChunkCoord& coord) const;

    Engine* engine_;
    kg::KGModule* kg_;

    // Content generation callbacks (set by specialized systems)
    ChunkCreateCallback create_callback_;
    ChunkDestroyCallback destroy_callback_;

    // Configuration
    float load_radius_ = 30.0f;      // Load chunks within this radius
    float unload_radius_ = 40.0f;    // Unload chunks beyond this (hysteresis prevents thrashing)
    float chunk_size_ = CHUNK_SIZE;  // Chunk dimensions in meters (can be changed via set_chunk_size)
    int update_frequency_ = 10;      // Update every N frames (performance)
    bool enabled_ = false;           // System starts disabled (toggle with key 7)

    // Active chunks (ChunkCoord → ChunkData)
    std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> active_chunks_;

    // Root KG entity for this chunk system
    kg::EntityID system_entity_;

    // Frame counter for update frequency
    int frame_counter_ = 0;

    // Async loading (Two-Tier Chunk Streaming Phase 2)
    bool async_loading_enabled_ = false;
    std::unique_ptr<AsyncChunkLoader> async_loader_;
};

#endif // CHUNK_SYSTEM_H
