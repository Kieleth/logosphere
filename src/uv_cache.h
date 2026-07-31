#ifndef UV_CACHE_H
#define UV_CACHE_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "optimization_flags.h"

// =========================================================================
// UV→WORLD CACHE - Dynamic programming for expensive UV interpolation
// =========================================================================
// The UV→World conversion (bilinear interpolation) takes 96% of lighting time!
// This cache stores recent conversions to avoid redundant calculations.
//
// Key insight: Nearby pixels often have similar UV coordinates (especially
// in tile-based rendering), so caching provides high hit rates.
// =========================================================================

class UVCache {
public:
    struct CacheEntry {
        // Key: surface pointer + UV coordinates
        const void* surface_ptr;  // Which surface (unique identifier)
        float u, v;                // UV coordinates
        
        // Value: computed world position
        float world_x, world_y, world_z;
        
        // Validity flag
        bool valid;
    };
    
private:
    static constexpr size_t CACHE_SIZE = Optimizations::UV_CACHE_SIZE;
    static constexpr size_t CACHE_MASK = CACHE_SIZE - 1;  // For fast modulo
    
    CacheEntry cache_[CACHE_SIZE];
    
    // Statistics for monitoring
    mutable uint32_t hits_ = 0;
    mutable uint32_t misses_ = 0;
    mutable uint32_t lookups_ = 0;
    
    // Quantize UV to grid for better cache hits
    inline void quantize_uv(float& u, float& v) const {
        // Quantize to 64x64 grid (adjustable)
        constexpr float GRID_SIZE = 64.0f;
        u = std::floor(u * GRID_SIZE) / GRID_SIZE;
        v = std::floor(v * GRID_SIZE) / GRID_SIZE;
    }
    
    // Hash function for cache indexing
    inline size_t hash(const void* surface, float u, float v) const {
        // Quantize first for consistent hashing
        float qu = u, qv = v;
        quantize_uv(qu, qv);
        
        // Simple hash combining surface pointer and quantized UV
        uint32_t u_bits = static_cast<uint32_t>(qu * 1000.0f);
        uint32_t v_bits = static_cast<uint32_t>(qv * 1000.0f);
        
        // Mix surface pointer with UV bits
        size_t h = reinterpret_cast<uintptr_t>(surface);
        h ^= (h >> 16);
        h = h * 0x45d9f3b;  // Magic number from MurmurHash
        h ^= u_bits;
        h = h * 0x45d9f3b;
        h ^= v_bits;
        h ^= (h >> 16);
        
        return h & CACHE_MASK;
    }
    
    // Check if cache entry matches (using quantized UV)
    inline bool matches(const CacheEntry& entry, const void* surface, float u, float v) const {
        // Quantize the lookup UV to match stored quantized values
        float qu = u, qv = v;
        quantize_uv(qu, qv);
        
        return entry.valid &&
               entry.surface_ptr == surface &&
               entry.u == qu &&  // Exact match after quantization
               entry.v == qv;
    }
    
public:
    UVCache() {
        clear();
    }
    
    // Clear all cache entries
    void clear() {
        std::memset(cache_, 0, sizeof(cache_));
        hits_ = misses_ = lookups_ = 0;
    }
    
    // Look up a UV→World conversion in the cache
    bool lookup(const void* surface, float u, float v,
                float& out_x, float& out_y, float& out_z) const {
        if (!Optimizations::USE_UV_CACHE) {
            return false;  // Cache disabled
        }
        
        lookups_++;
        
        size_t index = hash(surface, u, v);
        const CacheEntry& entry = cache_[index];
        
        if (matches(entry, surface, u, v)) {
            // Cache hit!
            out_x = entry.world_x;
            out_y = entry.world_y;
            out_z = entry.world_z;
            hits_++;
            return true;
        }
        
        misses_++;
        return false;  // Cache miss
    }
    
    // Store a UV→World conversion in the cache
    void store(const void* surface, float u, float v,
               float world_x, float world_y, float world_z) {
        if (!Optimizations::USE_UV_CACHE) {
            return;  // Cache disabled
        }
        
        // CRITICAL: Quantize UV before storing to match lookup!
        float qu = u, qv = v;
        quantize_uv(qu, qv);
        
        size_t index = hash(surface, u, v);  // Hash uses quantized internally
        CacheEntry& entry = cache_[index];
        
        // Simple replacement policy (no LRU for speed)
        entry.surface_ptr = surface;
        entry.u = qu;  // Store QUANTIZED UV values!
        entry.v = qv;  // Store QUANTIZED UV values!
        entry.world_x = world_x;
        entry.world_y = world_y;
        entry.world_z = world_z;
        entry.valid = true;
    }
    
    // Get cache statistics
    float get_hit_rate() const {
        return lookups_ > 0 ? (float)hits_ / lookups_ : 0.0f;
    }
    
    uint32_t get_hits() const { return hits_; }
    uint32_t get_misses() const { return misses_; }
    uint32_t get_lookups() const { return lookups_; }
    
    // Reset statistics (but keep cache contents)
    void reset_stats() {
        hits_ = misses_ = lookups_ = 0;
    }
    
    // Singleton access
    static UVCache& get() {
        static UVCache instance;
        return instance;
    }
};

#endif // UV_CACHE_H