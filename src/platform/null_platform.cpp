// NullPlatform — the IPlatformSystem for window-less profiles.
//
// The physics/core profiles have no GLFW, no Metal, no display. Engine
// still requires a platform object unconditionally ("Platform ALWAYS
// exists, even in headless mode"), and a headless run touches exactly
// four members: poll_events (no-op), get_framebuffer_size (discarded),
// get_dpi_scale_x (UI scale), should_close / destroy_window (loop +
// shutdown). Everything else is a neutral stub.
//
// This TU also provides Platform::create_platform_system() for
// GLFW-less builds — the same factory platform_macos.mm defines on
// macOS. Exactly one of the two TUs is compiled per profile.

#include "platform_system.h"

namespace Platform {

class NullPlatform : public IPlatformSystem {
public:
    // Window management
    bool create_window(const WindowConfig& config) override {
        width_ = config.width;
        height_ = config.height;
        return true;
    }
    void destroy_window() override {}
    bool is_window_open() const override { return false; }
    // Mirror PlatformMacOS semantics exactly: with no window,
    // should_close() is true ("window_ ? glfwWindowShouldClose(window_)
    // : true"). Window-loop-style tests rely on this to exit
    // immediately in headless runs; a NullPlatform has no window, ever.
    bool should_close() const override { return true; }
    void set_should_close(bool) override {}

    // Event handling
    void poll_events() override {}
    void wait_events() override {}
    void set_input_callbacks(IInputCallbacks*) override {}

    // Display information
    void get_window_size(int& width, int& height) const override {
        width = width_;
        height = height_;
    }
    void set_window_size(int width, int height) override {
        width_ = width;
        height_ = height;
    }
    void get_framebuffer_size(int& width, int& height) const override {
        width = width_;
        height = height_;
    }
    float get_dpi_scale_x() const override { return 1.0f; }
    float get_dpi_scale_y() const override { return 1.0f; }
    DisplayInfo get_display_info() const override {
        return DisplayInfo{width_, height_, 1.0f, 1.0f, 60.0f};
    }

    // Framebuffer presentation
    void present_framebuffer(const uint8_t*, int, int, int, int) override {}
    void swap_buffers() override {}
    void force_drawable_resize(int, int) override {}

    // Timing
    double get_time() const override { return 0.0; }
    void set_swap_interval(int) override {}

    // Cursor control
    void set_cursor_visible(bool) override {}
    void set_cursor_position(double, double) override {}
    void get_cursor_position(double& x, double& y) const override {
        x = 0.0;
        y = 0.0;
    }

    // Clipboard
    void set_clipboard_text(const char*) override {}
    const char* get_clipboard_text() override { return ""; }

    // Platform-specific features
    const char* get_platform_name() const override { return "Null (headless)"; }
    bool supports_retina() const override { return false; }
    void* get_native_window_handle() override { return nullptr; }
    PixelFormat get_native_pixel_format() const override {
        return PixelFormat::RGBA_8888;
    }

private:
    int width_ = 0;
    int height_ = 0;
};

std::unique_ptr<IPlatformSystem> create_platform_system() {
    return std::make_unique<NullPlatform>();
}

} // namespace Platform
