#ifndef ASYNC_CHUNK_LOADER_H
#define ASYNC_CHUNK_LOADER_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unordered_map>
#include "logosphere/worldgen/chunk_system.h"

/**
 * AsyncChunkLoader - Background thread chunk loading for Two-Tier Streaming
 *
 * Problem: Chunk creation on main thread causes frame hitches (100-500ms stalls)
 * Solution: Run heavy chunk generation on background thread
 *
 * Architecture:
 * - Main thread: Requests chunks, polls for completion, flushes particles
 * - Background thread: Runs create_callback, queues particles via queue_particle_addition()
 *
 * Thread Safety:
 * - KG has recursive_mutex (safe for queries from any thread)
 * - ParticleSystem::queue_particle_addition() is thread-safe
 * - flush_pending_particles() must be called from main thread
 *
 * Usage:
 *   loader.start(engine, kg, create_callback);
 *   loader.request_load(coord);           // Non-blocking
 *   if (loader.has_ready()) {
 *       auto result = loader.pop_ready(); // Get completed chunk
 *       // Result already queued particles via queue_particle_addition
 *   }
 *   particle_system.flush_pending_particles();  // Main thread applies
 */
class AsyncChunkLoader {
public:
    // Result of async chunk loading
    struct LoadResult {
        ChunkCoord coord;
        ChunkData data;
        bool success;
    };

    AsyncChunkLoader();
    ~AsyncChunkLoader();

    // Start the background worker thread
    void start(Engine* engine, kg::KGModule* kg, ChunkCreateCallback callback);

    // Stop the background thread (blocks until complete)
    void stop();

    // Request a chunk to be loaded (non-blocking)
    // Returns true if request accepted, false if queue full
    bool request_load(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity = 0);

    // Check if any chunks are ready
    bool has_ready() const;

    // Get a completed chunk (blocks if none ready)
    // Call has_ready() first to avoid blocking
    LoadResult pop_ready();

    // Check if a coord is currently being loaded or queued
    bool is_pending(const ChunkCoord& coord) const;

    // Configuration
    void set_max_queue_size(size_t max_size) { max_queue_size_ = max_size; }

private:
    // Worker thread function
    void worker_loop();

    // Request structure
    struct LoadRequest {
        ChunkCoord coord;
        float chunk_size;
        kg::EntityID existing_entity;
    };

    // Thread management
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // Request queue (main thread → worker)
    std::queue<LoadRequest> request_queue_;
    mutable std::mutex request_mutex_;
    std::condition_variable request_cv_;

    // Result queue (worker → main thread)
    std::queue<LoadResult> result_queue_;
    mutable std::mutex result_mutex_;

    // Pending set (for is_pending queries)
    std::unordered_map<ChunkCoord, bool, ChunkCoordHash> pending_coords_;
    mutable std::mutex pending_mutex_;

    // Dependencies
    Engine* engine_ = nullptr;
    kg::KGModule* kg_ = nullptr;
    ChunkCreateCallback create_callback_;

    // Configuration
    size_t max_queue_size_ = 64;  // Max pending requests (supports 8x8 chunk grid)
};

#endif // ASYNC_CHUNK_LOADER_H
