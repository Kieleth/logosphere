#include "scanline_uv_cache.h"
#include <cmath>

void ScanlineUVCache::begin_scanline(int y, int surface_id, const void* surface_ptr,
                                     float u_start, float u_end, float v, 
                                     int x_start, int x_end) {
    // Check if this is actually a new scanline
    if (state_.y == y && state_.surface_id == surface_id && 
        state_.surface_ptr == surface_ptr && state_.valid) {
        return;  // Already initialized for this scanline
    }
    
    // Reset for new scanline
    state_.reset();
    state_.y = y;
    state_.surface_id = surface_id;
    state_.surface_ptr = surface_ptr;
    state_.u_start = u_start;
    state_.v = v;
    
    // Calculate delta U per pixel
    if (x_end > x_start) {
        state_.du = (u_end - u_start) / (x_end - x_start);
    } else {
        state_.du = 0.0f;
    }
    
    // Mark as valid but not yet computed
    // World positions will be computed on first pixel request
    state_.valid = true;
    state_.last_x = -1;
    
    stats_.scanline_starts++;
}

bool ScanlineUVCache::get_world_position(int x, int y, int surface_id, 
                                         const void* surface_ptr,
                                         float /*u*/, float /*v*/,
                                         float& out_x, float& out_y, float& out_z) {
    stats_.total_pixels++;
    
    // Check if we're on the cached scanline
    if (!state_.valid || state_.y != y || state_.surface_id != surface_id || 
        state_.surface_ptr != surface_ptr) {
        stats_.cache_misses++;
        return false;  // Need full calculation
    }
    
    // Check if this is the expected next pixel
    if (state_.last_x == -1) {
        // First pixel of scanline - need full calculation to establish baseline
        // Caller will compute it, then we'll store the result
        state_.last_x = x;
        stats_.cache_misses++;
        return false;
    }
    
    // Check if we're stepping sequentially
    if (x == state_.last_x + 1) {
        // Perfect! Use incremental calculation
        // Just add the deltas we computed for this scanline
        state_.x_current += state_.dx;
        state_.y_current += state_.dy;
        state_.z_current += state_.dz;
        
        out_x = state_.x_current;
        out_y = state_.y_current;
        out_z = state_.z_current;
        
        state_.last_x = x;
        stats_.cache_hits++;
        return true;
    }
    
    // Non-sequential access - need recalculation
    stats_.cache_misses++;
    return false;
}

// Note: store_computed_position removed - not used in current implementation
// The scanline cache approach was replaced with the simpler UV coherence cache