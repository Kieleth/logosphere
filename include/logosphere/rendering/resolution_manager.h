#ifndef RESOLUTION_MANAGER_H
#define RESOLUTION_MANAGER_H

#include <vector>

// Forward declarations
class PixelBuffer;

/**
 * ResolutionManager - Decouples render resolution from window size
 * 
 * This allows users to control rendering quality independently of window size:
 * - Render at lower resolution for better performance
 * - Render at higher resolution for supersampling anti-aliasing
 * - UI always renders at window resolution for sharpness
 * 
 * Three coordinate spaces:
 * 1. Render space - User-chosen resolution for 3D rendering
 * 2. Window space - Logical window size for UI
 * 3. Framebuffer space - Physical pixels from platform (Retina aware)
 */
class ResolutionManager {
public:
    // Resolution presets for quick switching
    enum class ResolutionPreset {
        ULTRA_LOW,   // 400x300 (for weak hardware)
        LOW,         // 800x600 (performance mode)
        MEDIUM,      // 1280x960 (balanced)
        HIGH,        // 1600x1200 (quality mode)
        NATIVE,      // Match window size exactly
        RETINA,      // Match framebuffer size (2x on Retina)
        CUSTOM       // User-defined resolution
    };
    
    ResolutionManager();
    ~ResolutionManager() = default;
    
    // Configuration
    void set_window_size(int width, int height);
    void set_framebuffer_size(int width, int height);  // From platform
    void set_render_resolution(int width, int height);
    void set_resolution_preset(ResolutionPreset preset);
    
    // Current settings
    int get_render_width() const { return render_width_; }
    int get_render_height() const { return render_height_; }
    int get_window_width() const { return window_width_; }
    int get_window_height() const { return window_height_; }
    int get_framebuffer_width() const { return framebuffer_width_; }
    int get_framebuffer_height() const { return framebuffer_height_; }
    
    // Scaling factors
    float get_render_to_window_scale() const;
    float get_window_to_framebuffer_scale() const;
    
    // Coordinate transformations
    // Convert UI coordinates (window space) to render buffer coordinates
    void window_to_render(int win_x, int win_y, int& render_x, int& render_y) const;
    void render_to_window(int render_x, int render_y, int& win_x, int& win_y) const;
    
    // Convert window coordinates to framebuffer coordinates
    void window_to_framebuffer(int win_x, int win_y, int& fb_x, int& fb_y) const;
    void framebuffer_to_window(int fb_x, int fb_y, int& win_x, int& win_y) const;
    
    // Buffer scaling operations
    void scale_render_to_window(const PixelBuffer& render_buffer, 
                                PixelBuffer& window_buffer) const;
    void scale_window_to_framebuffer(const PixelBuffer& window_buffer,
                                     PixelBuffer& framebuffer) const;
    
    // Composite render buffer and UI buffer
    void composite_buffers(const PixelBuffer& render_buffer,
                          const PixelBuffer& ui_buffer,
                          PixelBuffer& output_buffer) const;
    
    // Query capabilities
    bool can_increase_resolution() const;
    bool can_decrease_resolution() const;
    void cycle_resolution_up();
    void cycle_resolution_down();
    
    // Get available presets based on current window/framebuffer
    void get_available_presets(std::vector<ResolutionPreset>& presets) const;
    const char* get_preset_name(ResolutionPreset preset) const;
    ResolutionPreset get_current_preset() const { return current_preset_; }
    
private:
    // Dimensions
    int window_width_ = 1600;        // Logical window size
    int window_height_ = 1200;
    int render_width_ = 1600;        // Chosen render resolution
    int render_height_ = 1200;
    int framebuffer_width_ = 1600;   // Physical pixels from platform
    int framebuffer_height_ = 1200;
    
    // Current preset
    ResolutionPreset current_preset_;
    
    // Preset definitions (aspect ratio 4:3 to match default window)
    struct PresetDefinition {
        ResolutionPreset preset;
        int width;
        int height;
        const char* name;
    };
    static const PresetDefinition preset_definitions_[];
    
    // Helper to find best preset for current settings
    void update_current_preset();
};

#endif // RESOLUTION_MANAGER_H