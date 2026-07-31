#ifndef LOGOSPHERE_MACOS_DISPLAY_H
#define LOGOSPHERE_MACOS_DISPLAY_H

#include "logosphere/display/i_display.h"

// Forward decl to avoid pulling the Platform header into every
// consumer.
namespace Platform { class IPlatformSystem; }

namespace Logosphere {
namespace Display {

// MacOSDisplay: macOS implementation of IDisplay.
//
// Thin adapter over Platform::IPlatformSystem. The CAMetalLayer +
// drawable + blit machinery already lives in PlatformMacOS; this
// class exposes ONLY the present / get-size surface that the Engine
// needs, keeping the window/event/cursor concerns encapsulated
// inside the platform layer.
//
// Phase 3 (this class's initial shape): non-owning wrapper. The
// Engine continues to create / destroy the PlatformMacOS; this
// Display just holds a pointer and forwards. Phase 4 will move
// platform creation into MacOSDisplay::initialize.
class MacOSDisplay : public IDisplay {
public:
    // Construct wrapping a live Platform::IPlatformSystem. The
    // platform must already be initialized; Display does not own it
    // (in the Phase 3 shape).
    explicit MacOSDisplay(Platform::IPlatformSystem& platform);
    ~MacOSDisplay() override = default;

    bool initialize(const DisplayConfig& config) override;
    void shutdown() override;

    void present(const uint32_t* pixels,
                 int render_width, int render_height,
                 int window_width, int window_height) override;
    void present_with_overlay(const uint32_t* pixels,
                              int render_width, int render_height,
                              int window_width, int window_height,
                              const uint32_t* overlay,
                              int overlay_x, int overlay_y,
                              int overlay_w, int overlay_h) override;

    void get_framebuffer_size(int& width, int& height) const override;
    void get_window_size(int& width, int& height) const override;
    void* get_native_window_handle() override;

private:
    Platform::IPlatformSystem* platform_;  // non-owning
};

} // namespace Display
} // namespace Logosphere

#endif // LOGOSPHERE_MACOS_DISPLAY_H
