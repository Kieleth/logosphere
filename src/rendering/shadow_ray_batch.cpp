#include "logosphere/rendering/shadow_ray_batch.h"
#include "logosphere/physics/bvh.h"
#include "../lighting_primitives.h"
#include <thread>
#include <atomic>
#include <iostream>

namespace ShadowRayBatch {
    
    // Thread-local BVH storage to avoid cache contention
    // This was already implemented in lighting_primitives.cpp but we need access here
    struct ThreadLocalBVH {
        BVH local_bvh;
        const BVH* source_bvh = nullptr;
        
        const BVH* get_or_clone(const BVH* source) {
            if (!source) return nullptr;
            
            if (source != source_bvh) {
                local_bvh = source->clone();
                source_bvh = source;
            }
            return &local_bvh;
        }
    };
    
    static thread_local ThreadLocalBVH thread_bvh;
    
    bool trace_shadow_ray_thread_safe(
        const ShadowRayWork& work,
        const std::vector<Particle>& particles,
        const BVH* bvh
    ) {
        // Use thread-local BVH copy to avoid cache contention
        const BVH* local_bvh = bvh ? thread_bvh.get_or_clone(bvh) : nullptr;
        
        // Use BVH if available, otherwise fall back to linear search
        if (local_bvh && local_bvh->is_ready()) {
            return local_bvh->trace_shadow_ray(
                work.from_x, work.from_y, work.from_z,
                work.to_x, work.to_y, work.to_z,
                particles,
                work.skip_particle_id
            );
        } else {
            // Fallback to lighting primitives (linear search)
            return LightingPrimitives::is_ray_blocked(
                work.from_x, work.from_y, work.from_z,
                work.to_x, work.to_y, work.to_z,
                particles,
                work.skip_particle_id
            );
        }
    }
    
    void process_parallel(
        const std::vector<ShadowRayWork>& work,
        std::vector<bool>& results,
        const std::vector<Particle>& particles,
        const BVH* bvh,
        int num_threads
    ) {
        const size_t work_size = work.size();
        
        // Handle empty or small work
        if (work_size == 0) {
            results.clear();
            return;
        }
        
        // For very small batches, process serially to avoid thread overhead
        const size_t MIN_BATCH_FOR_PARALLEL = 100;
        if (work_size < MIN_BATCH_FOR_PARALLEL || num_threads <= 1) {
            results.resize(work_size);
            for (size_t i = 0; i < work_size; ++i) {
                results[i] = trace_shadow_ray_thread_safe(work[i], particles, bvh);
            }
            return;
        }
        
        // Resize results vector
        results.resize(work_size);
        
        // Use atomic counter for dynamic work distribution
        std::atomic<size_t> work_index{0};
        
        // Launch worker threads
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&work, &results, &particles, bvh, &work_index, work_size]() {
                // Each thread grabs work dynamically
                size_t idx;
                while ((idx = work_index.fetch_add(1)) < work_size) {
                    results[idx] = trace_shadow_ray_thread_safe(work[idx], particles, bvh);
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
    }
    
} // namespace ShadowRayBatch