#include "logosphere/worldgen/async_chunk_loader.h"
#include "core/engine.h"
#include <iostream>

AsyncChunkLoader::AsyncChunkLoader() = default;

AsyncChunkLoader::~AsyncChunkLoader() {
    stop();
}

void AsyncChunkLoader::start(Engine* engine, kg::KGModule* kg, ChunkCreateCallback callback) {
    if (running_.load()) {
        std::cerr << "[AsyncChunkLoader] Already running!" << std::endl;
        return;
    }

    engine_ = engine;
    kg_ = kg;
    create_callback_ = callback;
    running_.store(true);

    worker_thread_ = std::thread(&AsyncChunkLoader::worker_loop, this);

    std::cout << "[AsyncChunkLoader] Started background worker thread" << std::endl;
}

void AsyncChunkLoader::stop() {
    if (!running_.load()) return;

    // Signal worker to stop
    running_.store(false);

    // Wake up worker if waiting
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        request_cv_.notify_all();
    }

    // Wait for worker to finish
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    std::cout << "[AsyncChunkLoader] Stopped background worker thread" << std::endl;
}

bool AsyncChunkLoader::request_load(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity) {
    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_coords_.count(coord)) {
            return false;  // Already loading
        }
    }

    // Check queue size
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        if (request_queue_.size() >= max_queue_size_) {
            std::cerr << "[AsyncChunkLoader] Request queue full, dropping request for ("
                      << coord.x << "," << coord.y << ")" << std::endl;
            return false;
        }

        // Add to queue
        request_queue_.push({coord, chunk_size, existing_entity});
    }

    // Mark as pending
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_coords_[coord] = true;
    }

    // Wake up worker
    request_cv_.notify_one();

    return true;
}

bool AsyncChunkLoader::has_ready() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return !result_queue_.empty();
}

AsyncChunkLoader::LoadResult AsyncChunkLoader::pop_ready() {
    std::lock_guard<std::mutex> lock(result_mutex_);

    if (result_queue_.empty()) {
        return {{0, 0}, {}, false};  // Return empty result
    }

    LoadResult result = std::move(result_queue_.front());
    result_queue_.pop();

    // Remove from pending
    {
        std::lock_guard<std::mutex> plock(pending_mutex_);
        pending_coords_.erase(result.coord);
    }

    return result;
}

bool AsyncChunkLoader::is_pending(const ChunkCoord& coord) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    return pending_coords_.count(coord) > 0;
}

void AsyncChunkLoader::worker_loop() {
    std::cout << "[AsyncChunkLoader] Worker thread started" << std::endl;

    while (running_.load()) {
        LoadRequest request;

        // Wait for request
        {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return !request_queue_.empty() || !running_.load();
            });

            if (!running_.load() && request_queue_.empty()) {
                break;  // Shutdown
            }

            if (request_queue_.empty()) {
                continue;  // Spurious wakeup
            }

            request = std::move(request_queue_.front());
            request_queue_.pop();
        }

        // Process request (this is the heavy work)
        std::cout << "[AsyncChunkLoader] Loading chunk (" << request.coord.x
                  << "," << request.coord.y << ") on background thread" << std::endl;

        LoadResult result;
        result.coord = request.coord;
        result.success = false;

        try {
            // Call the chunk creation callback
            // This may use queue_particle_addition() which is thread-safe
            result.data = create_callback_(request.coord, request.chunk_size, request.existing_entity);
            result.success = true;

            std::cout << "[AsyncChunkLoader] Chunk (" << request.coord.x
                      << "," << request.coord.y << ") loaded with "
                      << result.data.entity_ids.size() << " entities" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[AsyncChunkLoader] Error loading chunk ("
                      << request.coord.x << "," << request.coord.y
                      << "): " << e.what() << std::endl;
        }

        // Push result
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            result_queue_.push(std::move(result));
        }
    }

    std::cout << "[AsyncChunkLoader] Worker thread exiting" << std::endl;
}
