#include "macos_display.h"

#include "platform/platform_system.h"

#include <cstdint>

namespace Logosphere {
namespace Display {

MacOSDisplay::MacOSDisplay(Platform::IPlatformSystem& platform)
    : platform_(&platform) {}

bool MacOSDisplay::initialize(const DisplayConfig& /*config*/) {
    // Phase 3: the Engine still creates and initializes
    // PlatformMacOS directly. This Display wraps an already-live
    // platform; initialize() is a stub. Phase 4 will move platform
    // creation here.
    return platform_ != nullptr;
}

void MacOSDisplay::shutdown() {
    // Platform is owned by the Engine today; nothing to do.
}

void MacOSDisplay::present(const uint32_t* pixels,
                           int render_width, int render_height,
                           int window_width, int window_height) {
    if (!platform_ || !pixels) return;
    // IPlatformSystem::present_framebuffer takes a uint8_t*; the
    // underlying buffer is BGRA uint32_t either way. Cast is safe.
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pixels);
    platform_->present_framebuffer(bytes,
                                   render_width, render_height,
                                   window_width, window_height);
}

void MacOSDisplay::present_with_overlay(const uint32_t* pixels,
                                        int render_width, int render_height,
                                        int window_width, int window_height,
                                        const uint32_t* overlay,
                                        int overlay_x, int overlay_y,
                                        int overlay_w, int overlay_h) {
    if (!platform_ || !pixels) return;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pixels);
    const uint8_t* overlay_bytes = reinterpret_cast<const uint8_t*>(overlay);
    platform_->present_framebuffer_with_overlay(bytes,
                                                render_width, render_height,
                                                window_width, window_height,
                                                overlay_bytes,
                                                overlay_x, overlay_y,
                                                overlay_w, overlay_h);
}

void MacOSDisplay::get_framebuffer_size(int& width, int& height) const {
    if (platform_) {
        platform_->get_framebuffer_size(width, height);
    } else {
        width = height = 0;
    }
}

void MacOSDisplay::get_window_size(int& width, int& height) const {
    if (platform_) {
        platform_->get_window_size(width, height);
    } else {
        width = height = 0;
    }
}

void* MacOSDisplay::get_native_window_handle() {
    return platform_ ? platform_->get_native_window_handle() : nullptr;
}

} // namespace Display
} // namespace Logosphere
