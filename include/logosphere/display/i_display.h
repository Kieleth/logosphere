#ifndef LOGOSPHERE_I_DISPLAY_H
#define LOGOSPHERE_I_DISPLAY_H

#include <cstdint>

// IDisplay: the platform-specific half. A Display takes BGRA
// pixels from the Renderer and shows them to the user. It owns
// whatever the target platform's presentation surface is
// (CAMetalLayer on macOS, swapchain on Vulkan, HWND on Win32, a
// framebuffer device on Linux, etc.).
//
// Intentionally narrow. Window lifecycle, input events, cursor
// control, clipboard, and timing stay on Platform::IPlatformSystem
// (src/platform/platform_system.h). The Display is ONLY the
// pixels-to-screen pathway, so an Engine can composite a Display
// over any IPlatformSystem or run with no Display at all (headless).

namespace Logosphere {
namespace Display {

struct DisplayConfig {
    int window_width  = 1600;
    int window_height = 1200;
    const char* window_title = "Logosphere";
};

class IDisplay {
public:
    virtual ~IDisplay() = default;

    // Lifecycle. initialize() opens the presentation surface and
    // returns false on failure. shutdown() releases it.
    virtual bool initialize(const DisplayConfig& config) = 0;
    virtual void shutdown() = 0;

    // Show one frame. pixels points to (render_width * render_height)
    // BGRA uint32_t values (alpha top byte). The Display scales /
    // blits to its output surface dimensions as needed.
    //
    // The render dims may differ from the window dims: the Renderer
    // targets a fixed internal resolution and the Display handles
    // scaling. Window dims are passed so the Display can size its
    // blit without a separate round-trip.
    virtual void present(const uint32_t* pixels,
                         int render_width, int render_height,
                         int window_width, int window_height) = 0;

    // Present the scene with a pre-composited UI overlay rect on top.
    // The caller blends (scene OVER ui) for the rect; this is a second
    // sub-region upload, not a blending operation. Lets the UI refresh
    // without re-rendering the scene — see
    // the UI overlay design notes. Default forwards to
    // present(), so displays that do not implement it still work.
    virtual void present_with_overlay(const uint32_t* pixels,
                                      int render_width, int render_height,
                                      int window_width, int window_height,
                                      const uint32_t* overlay,
                                      int overlay_x, int overlay_y,
                                      int overlay_w, int overlay_h) {
        (void)overlay; (void)overlay_x; (void)overlay_y;
        (void)overlay_w; (void)overlay_h;
        present(pixels, render_width, render_height, window_width, window_height);
    }

    // Current window / surface dimensions, as reported by the
    // platform. May change if the user resizes the window.
    virtual void get_framebuffer_size(int& width, int& height) const = 0;
    virtual void get_window_size(int& width, int& height) const = 0;

    // Native window handle for input system wiring (GLFWwindow* on
    // macOS today). Opaque; callers cast based on platform.
    virtual void* get_native_window_handle() = 0;
};

} // namespace Display
} // namespace Logosphere

#endif // LOGOSPHERE_I_DISPLAY_H
