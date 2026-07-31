#include "logosphere/rendering/tile_thread_pool.h"
#include <chrono>
#include <iostream>

/**
 * TileThreadPool Implementation
 * 
 * THREADING NOTES (2025-01-22):
 * =============================
 * Simple, lock-free thread pool using atomic work stealing.
 * Workers spin-wait on work_available_ flag (crude but simple).
 * No mutexes, no condition variables - just atomics.
 * 
 * PERFORMANCE NOTES:
 * - Spin-wait burns CPU but has lowest latency
 * - Can optimize later with condition variables if needed
 * - For now, KISS principle wins
 */

TileThreadPool::TileThreadPool() {
    // Create worker threads
    workers_.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers_.emplace_back(&TileThreadPool::worker_thread_func, this, i);
    }
    
    std::cout << "[TileThreadPool] Created " << NUM_THREADS << " worker threads\n";
}

TileThreadPool::~TileThreadPool() {
    // Signal shutdown
    should_exit_ = true;
    work_available_ = true;  // Wake up any sleeping threads
    
    // Join all threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    std::cout << "[TileThreadPool] Shut down successfully\n";
}

void TileThreadPool::submit_tiles(
    const std::vector<ParallelTile::TileInput>& tiles,
    uint32_t* framebuffer,
    int fb_width,
    int fb_height) {
    
    // Wait for any previous batch to complete
    // TODO: This could be optimized with double buffering
    while (!is_batch_complete() && !should_exit_) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    // Setup new batch
    current_tiles_ = &tiles;
    current_framebuffer_ = framebuffer;
    fb_width_ = fb_width;
    fb_height_ = fb_height;
    
    // Reset counters
    next_tile_index_ = 0;
    tiles_completed_ = 0;
    total_tiles_ = static_cast<int>(tiles.size());
    
    // Signal workers to start
    work_available_ = true;
}

void TileThreadPool::wait_for_completion() {
    while (!is_batch_complete() && !should_exit_) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void TileThreadPool::worker_thread_func(int thread_id) {
    // Thread-local tile buffer (stack allocated, no heap)
    ParallelTile::TileBuffer local_buffer;
    
    std::cout << "[Worker " << thread_id << "] Started\n";
    
    while (!should_exit_) {
        // Wait for work
        if (!work_available_) {
            // Crude spin-wait with small sleep to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Work stealing loop
        while (true) {
            // Atomically grab next tile
            int tile_idx = next_tile_index_.fetch_add(1);
            
            // Check if we're done
            if (tile_idx >= total_tiles_) {
                // No more tiles, mark work as unavailable if we're the last
                if (tiles_completed_.load() >= total_tiles_) {
                    work_available_ = false;
                }
                break;
            }
            
            // Process the tile
            if (current_tiles_ && tile_idx < current_tiles_->size()) {
                const auto& tile_input = (*current_tiles_)[tile_idx];
                
                // Clear the buffer for this tile
                local_buffer = ParallelTile::TileBuffer();
                
                // Process tile into local buffer
                int pixels_written = ParallelTile::process_tile_pure(
                    tile_input, 
                    local_buffer
                );
                
                // Copy to framebuffer (each tile writes to different region, no contention)
                if (pixels_written > 0 && current_framebuffer_) {
                    ParallelTile::copy_tile_to_framebuffer(
                        local_buffer,
                        current_framebuffer_,
                        fb_width_,
                        fb_height_
                    );
                }
                
                // Increment completed count
                tiles_completed_.fetch_add(1);
            }
        }
    }
    
    std::cout << "[Worker " << thread_id << "] Exiting\n";
}