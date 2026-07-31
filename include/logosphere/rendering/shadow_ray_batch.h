#ifndef SHADOW_RAY_BATCH_H
#define SHADOW_RAY_BATCH_H

#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include "particle.h"

// Shadow Ray Batch Processing - Parallelize shadow ray computation
//
// This addresses the core bottleneck: shadow rays take 89% of frame time
// and are currently computed serially. By batching all shadow rays for
// a tile and processing them in parallel, we expect 3-4x speedup.

// Forward declaration of BVH (from global namespace)
class BVH;

namespace ShadowRayBatch {
    
    // Work unit for a single shadow ray test
    struct ShadowRayWork {
        // Ray data
        float from_x, from_y, from_z;  // Pixel world position
        float to_x, to_y, to_z;        // Light position
        int skip_particle_id;          // Surface particle to skip
        
        // Result location
        int pixel_index;               // Which pixel in tile (for result indexing)
        int light_index;               // Which light (for multi-light support)
    };
    
    // Process shadow rays in parallel using std::thread
    // Returns vector of bools: true = blocked/shadowed, false = lit
    void process_parallel(
        const std::vector<ShadowRayWork>& work,
        std::vector<bool>& results,
        const std::vector<Particle>& particles,
        const ::BVH* bvh,
        int num_threads = 4
    );
    
    // Helper to trace a single shadow ray (thread-safe)
    // Uses thread-local BVH copy to avoid cache contention
    bool trace_shadow_ray_thread_safe(
        const ShadowRayWork& work,
        const std::vector<Particle>& particles,
        const ::BVH* bvh
    );
}

#endif // SHADOW_RAY_BATCH_H