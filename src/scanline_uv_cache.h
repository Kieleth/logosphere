#ifndef SCANLINE_UV_CACHE_H
#define SCANLINE_UV_CACHE_H

#include <array>
#include <cstdint>

// =========================================================================
// SCANLINE UV COHERENCE OPTIMIZATION
// 
// PROBLEM: UV→World conversion takes 12ms out of 20ms frame time (60%)
//          Each pixel does full bilinear interpolation (16 muls + 9 adds)
//          With 38,000 lit pixels = 608,000 multiplies per frame!
//
// INSIGHT: On a scanline, adjacent pixels have predictable UV values!
//          If pixel[x] has UV (u,v), then pixel[x+1] has UV (u+du, v)
//          Where du is constant for the entire scanline.
//
// SOLUTION: For each scanline:
//           1. Compute UV and world position for first pixel (full calculation)
//           2. Compute delta_u and delta_world per pixel step
//           3. For remaining pixels: just add deltas! (3 adds vs 16 muls + 9 adds)
//
// MATH: Bilinear interpolation is LINEAR in u when v is constant:
//       P(u,v) = (1-u)(1-v)P0 + u(1-v)P1 + uv*P2 + (1-u)v*P3
//       P(u+du,v) = P(u,v) + du * [(1-v)(P1-P0) + v(P2-P3)]
//                 = P(u,v) + du * delta_per_u   (precomputed for scanline!)
//
// IMPACT: 38,000 pixels → 100 scanlines * 380 pixels/scanline
//         Old: 38,000 * (16 muls + 9 adds) = 950,000 operations
//         New: 100 * (16 muls + 9 adds) + 37,900 * 3 adds = 116,200 ops
//         Expected speedup: 8.2x for UV→World conversion (12ms → 1.5ms)
// =========================================================================

class ScanlineUVCache {
public:
    struct ScanlineState {
        // Current scanline info
        int y = -1;                    // Current scanline Y coordinate
        int surface_id = -1;           // Current surface being rendered
        const void* surface_ptr = nullptr;  // Pointer to validate surface
        
        // UV stepping
        float u_start = 0.0f;          // U value at scanline start
        float v = 0.0f;                // V value (constant for scanline)
        float du = 0.0f;               // Delta U per pixel
        
        // World position stepping
        float x_current = 0.0f;        // Current world X
        float y_current = 0.0f;        // Current world Y  
        float z_current = 0.0f;        // Current world Z
        float dx = 0.0f;               // Delta world X per pixel
        float dy = 0.0f;               // Delta world Y per pixel
        float dz = 0.0f;               // Delta world Z per pixel
        
        // Tracking
        int last_x = -1;               // Last X coordinate computed
        bool valid = false;            // Is this scanline data valid?
        
        void reset() {
            y = -1;
            surface_id = -1;
            surface_ptr = nullptr;
            valid = false;
            last_x = -1;
        }
    };
    
    // Get singleton instance
    static ScanlineUVCache& get() {
        static ScanlineUVCache instance;
        return instance;
    }
    
    // Start a new scanline
    // Call this when beginning to render a new scanline of a surface
    void begin_scanline(int y, int surface_id, const void* surface_ptr,
                        float u_start, float u_end, float v, int x_start, int x_end);
    
    // Get world position for pixel (x,y)
    // Returns true if using cached/interpolated value, false if full calculation needed
    bool get_world_position(int x, int y, int surface_id, const void* surface_ptr,
                           float u, float v,
                           float& out_x, float& out_y, float& out_z);
    
    // Clear cache for new frame
    void clear() {
        state_.reset();
    }
    
    // Statistics
    struct Stats {
        uint64_t scanline_starts = 0;   // Number of scanlines processed
        uint64_t cache_hits = 0;         // Pixels using incremental calculation
        uint64_t cache_misses = 0;       // Pixels needing full calculation
        uint64_t total_pixels = 0;       // Total pixels processed
        
        float get_hit_rate() const {
            return total_pixels > 0 ? 
                   100.0f * cache_hits / total_pixels : 0.0f;
        }
        
        void reset() {
            scanline_starts = 0;
            cache_hits = 0;
            cache_misses = 0;
            total_pixels = 0;
        }
    };
    
    const Stats& get_stats() const { return stats_; }
    void reset_stats() { stats_.reset(); }
    
private:
    ScanlineUVCache() = default;
    
    ScanlineState state_;
    Stats stats_;
};

#endif // SCANLINE_UV_CACHE_H