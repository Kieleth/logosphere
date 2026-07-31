# Platform System Reference

## Quick Reference for Platform abstraction layer

### Files

#### Core Platform Files
- `src/platform/platform_types.h` - Platform-independent types
- `src/platform/platform_system.h` - IPlatformSystem interface
- `src/platform/platform_macos.h` - macOS implementation header
- `src/platform/platform_macos.mm` - macOS implementation

#### Test Files
- `test_platform_demo.cpp` - Standalone platform demo
- `tests/test_platform_system.cpp` - Platform unit tests
- `tests/register_platform_tests.cpp` - Test registration

### Key Classes

#### IPlatformSystem (Interface)
```cpp
// Window management
create_window(config)    // Create OS window
destroy_window()         // Destroy window
is_window_open()        // Check if window exists
should_close()          // Check close request

// Display
present_framebuffer(buffer, width, height)  // Display pixels
get_framebuffer_size(width, height)        // Get actual FB size
swap_buffers()                              // Swap front/back buffer

// Input
poll_events()           // Process OS events
set_input_callbacks()   // Register input handlers

// Info
get_dpi_scale_x/y()    // Get DPI scaling factors
get_platform_name()    // "macOS", "Linux", etc.
supports_retina()      // High-DPI support check
```

#### PlatformMacOS (Implementation)
- Implements IPlatformSystem for macOS
- Uses Metal/MetalKit for rendering
- GLFW for window management
- Handles Retina scaling automatically

### Usage Examples

#### Creating Platform
```cpp
// In Engine::initialize()
platform_ = new Platform::PlatformMacOS();
Platform::WindowConfig config;
config.width = 1600;
config.height = 1200;
config.title = "Logosphere";
platform_->create_window(config);
```

#### Using in RenderSystem
```cpp
// Set platform
render_system.set_platform_system(platform);

// Present framebuffer
void RenderSystem::present_framebuffer() {
    auto* platform = static_cast<Platform::IPlatformSystem*>(platform_system_);
    platform->present_framebuffer(framebuffer_.data(), width, height);
}
```

#### Getting DPI Info
```cpp
float dpi_x = platform->get_dpi_scale_x();  // 2.0 on Retina
float dpi_y = platform->get_dpi_scale_y();  // 2.0 on Retina

int fb_width, fb_height;
platform->get_framebuffer_size(fb_width, fb_height);
// fb_width = window_width * dpi_x
```

### Build Commands

```bash
make platform-demo   # Run platform system demo
make eden           # Run Eden (uses platform system)
make run            # Run engine (uses platform system)
```

### Platform Types

#### WindowConfig
```cpp
struct WindowConfig {
    int width = 800;
    int height = 600;
    const char* title = "Logosphere";
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
};
```

#### DisplayInfo
```cpp
struct DisplayInfo {
    int width;           // Display width in pixels
    int height;          // Display height in pixels
    float dpi_scale_x;   // DPI scale factor X
    float dpi_scale_y;   // DPI scale factor Y
    float refresh_rate;  // Hz
};
```

### Global Access
```cpp
extern Platform::IPlatformSystem* g_platform_system;  // Global instance
```

### Adding New Platform

1. Create implementation class:
```cpp
class PlatformLinux : public IPlatformSystem {
    // Implement all virtual methods
};
```

2. Update Engine to use it:
```cpp
#ifdef __linux__
    platform_ = new Platform::PlatformLinux();
#elif __APPLE__
    platform_ = new Platform::PlatformMacOS();
#endif
```

### Common Operations

#### Check if should exit
```cpp
while (!platform->should_close()) {
    platform->poll_events();
    // Game loop
}
```

#### Handle window close
```cpp
// In key handler
if (key == ESC) {
    platform->set_should_close(true);
}
```

---
