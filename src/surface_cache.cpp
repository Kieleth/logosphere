#include "surface_cache.h"
#include "core/particle_system.h"

// Pre-build cache for all particles (call before parallel rendering)
// This eliminates the need for any locking during parallel processing
// ASSUMES CALLER HOLDS LOCK: Caller must have a ReadView/WriteView active
void SurfaceCache::prebuild_cache(const std::vector<Particle>& particles) {
    // THREAD SAFETY: No lock here - caller already holds lock via ReadView
    // This prevents double-locking which creates a race condition window

    // Ensure cache is sized for all particles
    cache_.resize(particles.size());

    // Build cache for each particle - ALWAYS rebuild, no empty() check
    // This ensures cache is fully populated before parallel access
    // Vectors reuse capacity from previous frame (no new allocations)
    for (size_t i = 0; i < particles.size(); ++i) {
        cache_[i] = particles[i].GetSurfaces();
    }
}