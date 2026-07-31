#ifndef TILE_THREAD_POOL_H
#define TILE_THREAD_POOL_H

#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include "logosphere/rendering/parallel_tile_processor.h"

/**
 * TileThreadPool - Simple worker thread pool for parallel tile rendering
 * 
 * DESIGN PRINCIPLES (2025-01-22):
 * ================================
 * 1. KISS: Simplest possible thread pool that works
 * 2. FIXED SIZE: Always 4 threads (we assume 4+ cores)
 * 3. WORK STEALING: Atomic counter for dynamic load balancing
 * 4. NO MUTEXES: Lock-free design using atomics only
 * 5. PERSISTENT: Threads created once, reused for all frames
 * 
 * THREADING MODEL:
 * ================
 * - Main thread: Submits work, continues immediately
 * - Worker threads: Process tiles using atomic work stealing
 * - No blocking: Main thread never waits for workers
 * 
 * MEMORY MODEL:
 * =============
 * - Each thread has its own TileBuffer (stack allocated)
 * - No shared mutable state between threads
 * - Read-only access to surfaces, lights, BVH
 */

class TileThreadPool {
public:
    // Fixed thread count (KISS principle)
    static constexpr int NUM_THREADS = 1;  // KISS: Just 1 worker thread + main thread
    
    // Constructor/Destructor
    TileThreadPool();
    ~TileThreadPool();
    
    // Submit a batch of tiles for processing
    // Returns immediately, doesn't wait for completion
    void submit_tiles(
        const std::vector<ParallelTile::TileInput>& tiles,
        uint32_t* framebuffer,
        int fb_width,
        int fb_height
    );
    
    // Check if current batch is complete
    bool is_batch_complete() const {
        return tiles_completed_.load() >= total_tiles_.load();
    }
    
    // Get progress for debugging
    int get_tiles_completed() const {
        return tiles_completed_.load();
    }
    
    int get_total_tiles() const {
        return total_tiles_.load();
    }
    
    // Wait for current batch to complete (blocking)
    // Only use for synchronization points, not every frame
    void wait_for_completion();
    
private:
    // Worker thread function
    void worker_thread_func(int thread_id);
    
    // Thread pool
    std::vector<std::thread> workers_;
    
    // Work queue (lock-free)
    std::atomic<int> next_tile_index_{0};
    std::atomic<int> tiles_completed_{0};
    std::atomic<int> total_tiles_{0};
    
    // Shutdown flag
    std::atomic<bool> should_exit_{false};
    
    // Current batch data (read-only for workers)
    const std::vector<ParallelTile::TileInput>* current_tiles_ = nullptr;
    uint32_t* current_framebuffer_ = nullptr;
    int fb_width_ = 0;
    int fb_height_ = 0;
    
    // Simple signaling for work availability
    std::atomic<bool> work_available_{false};
    
    // Prevent copy/assignment
    TileThreadPool(const TileThreadPool&) = delete;
    TileThreadPool& operator=(const TileThreadPool&) = delete;
};

#endif // TILE_THREAD_POOL_H