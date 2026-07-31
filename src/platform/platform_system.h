#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#include "platform_types.h"
#include <cstdint>
#include <memory>

// Platform abstraction layer - handles all OS-specific operations

namespace Platform {

class IPlatformSystem {
public:
    virtual ~IPlatformSystem() = default;
    
    // Window management
    virtual bool create_window(const WindowConfig& config) = 0;
    virtual void destroy_window() = 0;
    virtual bool is_window_open() const = 0;
    virtual bool should_close() const = 0;
    virtual void set_should_close(bool should_close) = 0;
    
    // Event handling
    virtual void poll_events() = 0;
    virtual void wait_events() = 0;  // Block until event occurs
    virtual void set_input_callbacks(IInputCallbacks* callbacks) = 0;
    
    // Display information
    virtual void get_window_size(int& width, int& height) const = 0;
    virtual void set_window_size(int width, int height) = 0;
    virtual void get_framebuffer_size(int& width, int& height) const = 0;
    virtual float get_dpi_scale_x() const = 0;
    virtual float get_dpi_scale_y() const = 0;
    virtual DisplayInfo get_display_info() const = 0;
    
    // Framebuffer presentation
    // Hardware scaling: render dimensions can differ from window dimensions
    virtual void present_framebuffer(const uint8_t* buffer,
                                    int render_width, int render_height,
                                    int window_width, int window_height) = 0;

    // Present the scene plus a pre-composited UI overlay rect.
    // `overlay` holds (scene OVER ui) pixels for the rect only — the
    // caller blends, so this is a second sub-region upload onto the same
    // target texture, no blending state here. Overlay rects are small
    // (the HUD's dirty box), so this stays far cheaper than compositing
    // full frames. Default: ignore the overlay and present the scene, so
    // platforms that do not implement it still show a picture.
    // the UI overlay design notes
    virtual void present_framebuffer_with_overlay(const uint8_t* buffer,
                                                  int render_width, int render_height,
                                                  int window_width, int window_height,
                                                  const uint8_t* overlay,
                                                  int overlay_x, int overlay_y,
                                                  int overlay_w, int overlay_h) {
        (void)overlay; (void)overlay_x; (void)overlay_y;
        (void)overlay_w; (void)overlay_h;
        present_framebuffer(buffer, render_width, render_height,
                            window_width, window_height);
    }
    virtual void swap_buffers() = 0;

    // Force drawable resize (called while GPU is idle during resolution change)
    // IMPORTANT: Caller must hold all GPU semaphores before calling this
    virtual void force_drawable_resize(int width, int height) = 0;
    
    // Timing
    virtual double get_time() const = 0;  // Seconds since initialization
    virtual void set_swap_interval(int interval) = 0;  // 0=no vsync, 1=vsync
    
    // Cursor control
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void set_cursor_position(double x, double y) = 0;
    virtual void get_cursor_position(double& x, double& y) const = 0;
    
    // Clipboard
    virtual void set_clipboard_text(const char* text) = 0;
    virtual const char* get_clipboard_text() = 0;
    
    // Platform-specific features
    virtual const char* get_platform_name() const = 0;
    virtual bool supports_retina() const = 0;
    virtual void* get_native_window_handle() = 0;  // For platform-specific needs
    
    // Native pixel format support for zero-copy framebuffer presentation
    enum class PixelFormat {
        RGBA_8888,  // R,G,B,A order (typical for Linux/X11)
        BGRA_8888,  // B,G,R,A order (typical for Metal/DirectX)
        ARGB_8888,  // A,R,G,B order (alternative format)
        RGB_888     // No alpha channel
    };
    
    // Platform declares its preferred native pixel format
    // This allows rendering directly in the platform's format to avoid conversion
    virtual PixelFormat get_native_pixel_format() const = 0;
};

// Factory function to create platform-specific implementation
std::unique_ptr<IPlatformSystem> create_platform_system();

// Global platform system removed - now accessed via Engine::get_platform()

} // namespace Platform

#endif // PLATFORM_SYSTEM_H