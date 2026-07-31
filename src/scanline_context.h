#ifndef SCANLINE_CONTEXT_H
#define SCANLINE_CONTEXT_H

// =========================================================================
// SCANLINE CONTEXT - Thread-local state for scanline rendering
// 
// PROBLEM: UV calculations happen deep in Surface::get_world_position_at_uv
//          but scanline coherence needs to know pixel X,Y coordinates
//
// SOLUTION: Thread-local context that tracks current rendering position
//           Render system sets this, Surface reads it
//
// This avoids passing X,Y through multiple layers of function calls
// =========================================================================

struct ScanlineContext {
    // Current pixel being rendered
    int pixel_x = -1;
    int pixel_y = -1;
    
    // Current surface being rendered
    int particle_id = -1;
    int surface_index = -1;
    const void* surface_ptr = nullptr;
    
    // UV bounds for current scanline
    float u_start = 0.0f;
    float u_end = 1.0f;
    float v_current = 0.0f;
    
    // Pixel bounds for current scanline  
    int x_start = -1;
    int x_end = -1;
    
    // Reset for new frame
    void reset() {
        pixel_x = pixel_y = -1;
        particle_id = surface_index = -1;
        surface_ptr = nullptr;
    }
    
    // Get thread-local instance
    static ScanlineContext& get() {
        static thread_local ScanlineContext instance;
        return instance;
    }
};

#endif // SCANLINE_CONTEXT_H