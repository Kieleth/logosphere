/**
 * macOS Platform Layer Implementation
 * 
 * This file contains the macOS-specific code for displaying framebuffers.
 * Extracted from main.mm as part of the engine-application separation.
 */

#include "platform/platform_layer.h"
#include <iostream>
#include <vector>

// macOS-specific includes
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <GLFW/glfw3.h>  // Must include glfw3.h before glfw3native.h
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#endif

namespace Logosphere {

/**
 * MacOSPlatform - macOS-specific implementation of PlatformLayer
 */
class MacOSPlatform : public PlatformLayer {
public:
    MacOSPlatform() = default;
    ~MacOSPlatform() = default;
    
    // display_framebuffer removed in Phase 6 of the Renderer/Display
    // split. Presentation now flows through IDisplay (MacOSDisplay →
    // Platform::IPlatformSystem::present_framebuffer) with Metal
    // blit, not through this Core Graphics path.
};

/**
 * Factory function to create platform-specific implementation
 */
std::unique_ptr<PlatformLayer> create_platform_layer() {
    return std::make_unique<MacOSPlatform>();
}

} // namespace Logosphere