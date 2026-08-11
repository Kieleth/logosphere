#ifndef PLATFORM_MACOS_H
#define PLATFORM_MACOS_H

#include "platform_system.h"
#include <GLFW/glfw3.h>
#include <atomic>  // For std::atomic thread-safe flag
#include <chrono>

namespace Platform {

// macOS-specific implementation of the platform system
class PlatformMacOS : public IPlatformSystem {
public:
    PlatformMacOS();
    ~PlatformMacOS() override;
    
    // Window management
    bool create_window(const WindowConfig& config) override;
    void destroy_window() override;
    bool is_window_open() const override { return window_ != nullptr; }
    bool should_close() const override;
    void set_should_close(bool should_close) override;
    
    // Event handling
    void poll_events() override;
    void wait_events() override;
    void set_input_callbacks(IInputCallbacks* callbacks) override { callbacks_ = callbacks; }
    
    // Display information
    void get_window_size(int& width, int& height) const override;
    void set_window_size(int width, int height) override;
    void get_framebuffer_size(int& width, int& height) const override;
    float get_dpi_scale_x() const override { return dpi_scale_x_; }
    float get_dpi_scale_y() const override { return dpi_scale_y_; }
    DisplayInfo get_display_info() const override;
    
    // Framebuffer presentation
    void present_framebuffer(const uint8_t* buffer,
                           int render_width, int render_height,
                           int window_width, int window_height) override;
    void present_framebuffer_with_overlay(const uint8_t* buffer,
                           int render_width, int render_height,
                           int window_width, int window_height,
                           const uint8_t* overlay,
                           int overlay_x, int overlay_y,
                           int overlay_w, int overlay_h) override;
    void swap_buffers() override;
    void force_drawable_resize(int width, int height) override;
    
    // Timing
    double get_time() const override;
    void set_swap_interval(int interval) override;
    
    // Cursor control
    void set_cursor_visible(bool visible) override;
    void set_cursor_position(double x, double y) override;
    void get_cursor_position(double& x, double& y) const override;
    
    // Clipboard
    void set_clipboard_text(const char* text) override;
    const char* get_clipboard_text() override;
    
    // Platform-specific features
    const char* get_platform_name() const override { return "macOS"; }
    bool supports_retina() const override { return true; }
    void* get_native_window_handle() override { return window_; }
    
    // Native pixel format - Metal prefers BGRA
    PixelFormat get_native_pixel_format() const override { return PixelFormat::BGRA_8888; }

    // Has GLFW (and therefore NSApplication, and therefore this
    // process's connection to the window server) been brought up?
    // False for the whole life of a headless run. Public so an
    // acceptance test can assert the headless engine never touches
    // the window server; see tests/test_headless_no_window_server.cpp.
    bool is_window_system_live() const { return glfw_ready_; }

private:
    // Bring GLFW up on demand. Called only from create_window(): a
    // headless engine has no window and must never reach this.
    bool ensure_glfw();

    // GLFW callbacks (static methods that forward to instance methods)
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* window, unsigned int codepoint);
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void window_size_callback(GLFWwindow* window, int width, int height);
    static void window_close_callback(GLFWwindow* window);
    static void error_callback(int error, const char* description);
    
    // Helper methods
    void update_dpi_scale();
    KeyCode glfw_to_keycode(int glfw_key, int mods) const;
    MouseButton glfw_to_mouse_button(int glfw_button) const;
    ModifierKeys glfw_to_modifiers(int mods) const;
    
    // Member variables
    GLFWwindow* window_ = nullptr;
    IInputCallbacks* callbacks_ = nullptr;
    bool glfw_ready_ = false;
    // get_time() used to be glfwGetTime(), which forced GLFW up just
    // to read a clock. Same semantics without the dependency:
    // monotonic seconds since this platform was created.
    std::chrono::steady_clock::time_point created_at_ =
        std::chrono::steady_clock::now();
    float dpi_scale_x_ = 1.0f;
    float dpi_scale_y_ = 1.0f;

    // Metal resources (void* to avoid exposing Objective-C types in header)
    void* metal_device_ = nullptr;   // id<MTLDevice>
    void* metal_layer_ = nullptr;    // CAMetalLayer*

    // Drawable size tracking (prevents unnecessary resizes that can cause GPU crashes)
    float last_drawable_width_ = 0.0f;
    float last_drawable_height_ = 0.0f;

    // Static instance for callbacks (GLFW requires static functions)
    static PlatformMacOS* instance_;
};

} // namespace Platform

#endif // PLATFORM_MACOS_H