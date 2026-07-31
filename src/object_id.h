#ifndef OBJECT_ID_H
#define OBJECT_ID_H

/**
 * ObjectID - Encoding system for pixel object identification
 * 
 * THE PROBLEM:
 * -----------
 * Each pixel in EnhancedPixel has ONE integer field: object_id
 * But when you click a pixel, we need to know:
 *   - What TYPE of object? (particle, UI element, or background)
 *   - WHICH object? (particle #3, UI button #5)  
 *   - For particles: WHICH surface? (cube has 6 faces, which one?)
 *
 * THE SOLUTION: 
 * ------------
 * Pack all info into that single integer using bit ranges:
 *   Particles: (particle_id << 16) | surface_id
 *   UI: 0x80000000 | ui_id (high bit set)
 *   Background: 0
 *
 * This way we can quickly check ranges to identify type,
 * then extract the specific IDs we need.
 *
 * MEMORY CONSIDERATION:
 * --------------------
 * Alternative would be 3 separate integers per pixel (12 bytes)
 * Current approach: 1 integer per pixel (4 bytes)
 * For 1600x1200 screen: saves 15.4 MB of memory!
 */

namespace ObjectID {
    const int BACKGROUND = 0;
    
    // === PARTICLES ===
    // Format: (particle_id << 16) | surface_id
    // Range: 1 to 0x7FFFFFFF (positive numbers without high bit)
    
    inline int encode_particle_surface(int particle_id, int surface_id) {
        return ((particle_id + 1) << 16) | surface_id;  // +1 to avoid 0
    }
    
    inline int get_particle_id(int object_id) {
        if (object_id > 0 && object_id < 0x80000000) {
            return ((object_id >> 16) & 0xFFFF) - 1;  // -1 to get original ID
        }
        return -1;
    }
    
    inline int get_surface_id(int object_id) {
        if (object_id > 0 && object_id < 0x80000000) {
            return object_id & 0xFFFF;
        }
        return -1;
    }
    
    // === UI ELEMENTS ===
    // Format: 0x80000000 | ui_id (high bit set)
    // Range: 0x80000000 to 0xFFFFFFFF
    
    inline int encode_ui(int ui_id) {
        return 0x80000000 | ui_id;
    }
    
    inline int get_ui_id(int object_id) {
        if (object_id & 0x80000000) {
            return object_id & 0x7FFFFFFF;
        }
        return -1;
    }
    
    // === TYPE CHECKING ===
    // Fast type identification using ranges
    
    inline bool is_particle(int object_id) {
        return object_id > 0 && object_id < 0x80000000;
    }
    
    inline bool is_ui(int object_id) {
        return (object_id & 0x80000000) != 0;
    }
    
    inline bool is_background(int object_id) {
        return object_id == BACKGROUND;
    }
    
    // === COMPATIBILITY ===
    // For existing code that uses these names
    
    inline int get_surface_index(int object_id) {
        return get_surface_id(object_id);
    }
    
    inline int ui_to_object_id(int ui_id) {
        return encode_ui(ui_id);
    }
}

#endif // OBJECT_ID_H