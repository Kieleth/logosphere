#include "platform_macos.h"
#include "../debug_control.h"  // Use engine's debug control
#include "../optimization_flags.h"  // For USE_ASYNC_PRESENTATION
#include <iostream>
#include <iomanip>  // For std::fixed and std::setprecision
#include <cstring>
#include <chrono>  // For high_resolution_clock timing measurements
#include <thread>  // For std::this_thread::sleep_for
#include <atomic>  // For std::atomic thread-safe flag

// Define before including GLFW to get native access functions
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

namespace Platform {

// Static instance for GLFW callbacks
PlatformMacOS* PlatformMacOS::instance_ = nullptr;

// Global platform system instance
// Global platform system removed - now accessed via Engine::get_platform()

// Factory function implementation
std::unique_ptr<IPlatformSystem> create_platform_system() {
    return std::make_unique<PlatformMacOS>();
}

PlatformMacOS::PlatformMacOS() {
    instance_ = this;
    
    // Set GLFW error callback
    glfwSetErrorCallback(error_callback);
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "PlatformMacOS: Failed to initialize GLFW" << std::endl;
        return;
    }
    
    // CRITICAL: NO OpenGL - we use Metal directly
    // GLFW is only for window management and input
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // Start hidden

    std::cout << "[PLATFORM] GLFW initialized with NO_API (Metal-only)" << std::endl;
}

PlatformMacOS::~PlatformMacOS() {
    destroy_window();
    glfwTerminate();
    
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool PlatformMacOS::create_window(const WindowConfig& config) {
    std::cout << "[PLATFORM] create_window called with size " << config.width << "x" << config.height 
              << " title: " << config.title << std::endl;
    
    if (window_) {
        std::cerr << "[PLATFORM] ERROR: Window already exists" << std::endl;
        return false;
    }
    
    // Set window hints
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    
    std::cout << "[PLATFORM] Creating GLFW window..." << std::endl;

    // Create window (NO_API already set in constructor)
    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    window_ = glfwCreateWindow(config.width, config.height, config.title, monitor, nullptr);

    if (!window_) {
        std::cerr << "[PLATFORM] ERROR: Failed to create GLFW window!" << std::endl;
        return false;
    }

    std::cout << "[PLATFORM] GLFW window created successfully (no OpenGL context)" << std::endl;
    
    // Update DPI scale
    update_dpi_scale();
    
    // Set callbacks
    glfwSetKeyCallback(window_, key_callback);
    glfwSetCharCallback(window_, char_callback);  // For proper text input
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
    glfwSetScrollCallback(window_, scroll_callback);
    glfwSetWindowSizeCallback(window_, window_size_callback);
    glfwSetWindowCloseCallback(window_, window_close_callback);
    
    // Store this instance in the window user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

    // Show window
    glfwShowWindow(window_);

    // DEBUG: Check window state after show
    {
        int visible = glfwGetWindowAttrib(window_, GLFW_VISIBLE);
        int focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED);
        int iconified = glfwGetWindowAttrib(window_, GLFW_ICONIFIED);
        std::cout << "[PLATFORM] Window state after glfwShowWindow: visible=" << visible
                  << " focused=" << focused << " iconified=" << iconified << std::endl;

        int x, y;
        glfwGetWindowPos(window_, &x, &y);
        std::cout << "[PLATFORM] Window position: (" << x << ", " << y << ")" << std::endl;
    }

    // CRITICAL: Create CAMetalLayer on main thread (required by Apple)
    // This must happen during window creation, NOT during first presentation
    @autoreleasepool {
        NSWindow* nsWindow = glfwGetCocoaWindow(window_);
        NSView* contentView = [nsWindow contentView];

        // Create Metal device
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "[PLATFORM] ERROR: Failed to create Metal device!" << std::endl;
            return false;
        }
        [device retain];  // Manual retain for non-ARC
        metal_device_ = (__bridge void*)device;

        // Create Metal layer
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.device = device;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

        // CRITICAL: Must be NO to allow MPS compute operations on drawable textures
        metalLayer.framebufferOnly = NO;

        // CRITICAL: Disable automatic content updates - Metal manages this
        metalLayer.needsDisplayOnBoundsChange = NO;
        metalLayer.autoresizingMask = kCALayerNotSizable;

        // CRITICAL: Control presentation mode based on optimization flag
        // When USE_ASYNC_PRESENTATION = true: Use asynchronous presentation (NO)
        //   - Non-blocking, allows CPU/GPU to start next frame immediately
        //   - Fixes 170ms present stall issue
        // When USE_ASYNC_PRESENTATION = false: Use synchronous presentation (YES)
        //   - Blocking, ensures frame is presented before continuing
        metalLayer.presentsWithTransaction = Optimizations::USE_ASYNC_PRESENTATION ? NO : YES;

        // VSYNC TEST: Force disable display synchronization at Metal layer level
        if (@available(macOS 10.15.4, *)) {
            metalLayer.displaySyncEnabled = NO;
            std::cout << "[VSYNC_TEST] displaySyncEnabled set to NO (force disable vsync)" << std::endl;
        }

        // CRITICAL: Set contentsScale for Retina displays
        CGFloat scaleFactor = [nsWindow backingScaleFactor];
        metalLayer.contentsScale = scaleFactor;

        // Set layer frame to match view bounds
        metalLayer.frame = contentView.bounds;

        // Attach layer to view
        [contentView setLayer:metalLayer];
        [contentView setWantsLayer:YES];

        // Force layout
        [contentView setNeedsLayout:YES];
        [contentView layoutSubtreeIfNeeded];

        [metalLayer retain];  // Manual retain for non-ARC
        metal_layer_ = (__bridge void*)metalLayer;

        std::cout << "[PLATFORM] Metal layer created on main thread, frame: "
                  << metalLayer.frame.size.width << "x" << metalLayer.frame.size.height
                  << ", contentsScale: " << metalLayer.contentsScale
                  << ", presentsWithTransaction: " << (metalLayer.presentsWithTransaction ? "YES (sync)" : "NO (async)")
                  << std::endl;

        // CRITICAL: Set initial drawable size
        CGSize drawableSize = CGSizeMake(contentView.bounds.size.width * scaleFactor,
                                         contentView.bounds.size.height * scaleFactor);
        metalLayer.drawableSize = drawableSize;

        std::cout << "[PLATFORM] Metal layer drawable size set to: "
                  << drawableSize.width << "×" << drawableSize.height << std::endl;
    }

    // CRITICAL: Focus window AFTER Metal layer setup to ensure it receives keyboard input
    glfwFocusWindow(window_);
    std::cout << "[PLATFORM] Called glfwFocusWindow() after Metal setup" << std::endl;

    // DEBUG: Check final window state
    {
        int visible = glfwGetWindowAttrib(window_, GLFW_VISIBLE);
        int focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED);
        std::cout << "[PLATFORM] Final window state: visible=" << visible << " focused=" << focused << std::endl;
    }

    DEBUG_LOG("PlatformMacOS: Created window " << config.width << "x" << config.height
              << " (DPI scale: " << dpi_scale_x_ << "x" << dpi_scale_y_ << ")");

    return true;
}

void PlatformMacOS::destroy_window() {
    // Clean up Metal resources (manual release for non-ARC)
    if (metal_layer_) {
        [(__bridge id)metal_layer_ release];
        metal_layer_ = nullptr;
    }
    if (metal_device_) {
        [(__bridge id)metal_device_ release];
        metal_device_ = nullptr;
    }

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

bool PlatformMacOS::should_close() const {
    return window_ ? glfwWindowShouldClose(window_) : true;
}

void PlatformMacOS::set_should_close(bool should_close) {
    if (window_) {
        glfwSetWindowShouldClose(window_, should_close ? GLFW_TRUE : GLFW_FALSE);
    }
}

void PlatformMacOS::poll_events() {
    glfwPollEvents();
}

void PlatformMacOS::wait_events() {
    glfwWaitEvents();
}

void PlatformMacOS::get_window_size(int& width, int& height) const {
    if (window_) {
        glfwGetWindowSize(window_, &width, &height);
    } else {
        width = height = 0;
    }
}

void PlatformMacOS::set_window_size(int width, int height) {
    if (window_) {
        glfwSetWindowSize(window_, width, height);
        
        // Update DPI scale factors for the new size
        int fb_width, fb_height;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        dpi_scale_x_ = static_cast<float>(fb_width) / width;
        dpi_scale_y_ = static_cast<float>(fb_height) / height;
        
        std::cout << "PlatformMacOS: Window resized to " << width << "x" << height 
                  << " (framebuffer: " << fb_width << "x" << fb_height << ")" << std::endl;
    }
}

void PlatformMacOS::get_framebuffer_size(int& width, int& height) const {
    if (window_) {
        glfwGetFramebufferSize(window_, &width, &height);
    } else {
        width = height = 0;
    }
}

DisplayInfo PlatformMacOS::get_display_info() const {
    DisplayInfo info;
    get_window_size(info.width, info.height);
    info.dpi_scale_x = dpi_scale_x_;
    info.dpi_scale_y = dpi_scale_y_;
    
    // Get refresh rate from primary monitor
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
            info.refresh_rate = static_cast<float>(mode->refreshRate);
        }
    }
    
    return info;
}

void PlatformMacOS::present_framebuffer(const uint8_t* buffer,
                                       int render_width, int render_height,
                                       int window_width, int window_height) {
    present_framebuffer_with_overlay(buffer, render_width, render_height,
                                     window_width, window_height,
                                     nullptr, 0, 0, 0, 0);
}

// Scene upload + optional pre-composited overlay rect. The overlay is a
// SECOND sub-region upload onto the same target texture (drawable in the
// direct path, render texture in the scaling path) — no blending state,
// the caller already blended (scene OVER ui) for that rect.
// the UI overlay design notes
void PlatformMacOS::present_framebuffer_with_overlay(const uint8_t* buffer,
                                       int render_width, int render_height,
                                       int window_width, int window_height,
                                       const uint8_t* overlay,
                                       int overlay_x, int overlay_y,
                                       int overlay_w, int overlay_h) {
    // Hardware scaling implementation using Metal
    // Renders at render_width x render_height, scales to window_width x window_height
    
    if (!window_) return;
    
    @autoreleasepool {
        NSWindow* nsWindow = glfwGetCocoaWindow(window_);
        if (!nsWindow) return;
        
        NSView* contentView = [nsWindow contentView];
        if (!contentView) return;

        // Use pre-created Metal layer and device (created on main thread in create_window)
        if (!metal_layer_ || !metal_device_) {
            std::cerr << "[METAL ERROR] Metal layer or device not initialized!" << std::endl;
            return;
        }

        CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)metal_layer_;
        id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;

        // CRITICAL: ALWAYS use actual contentView bounds, IGNORE window_width/window_height params
        // The params are RENDER size, not window size. We need ACTUAL window bounds.
        // This is the game engine pattern: render resolution is independent of window size
        CGRect viewBounds = contentView.bounds;
        metalLayer.frame = viewBounds;

        // Calculate proper drawable size from ACTUAL view bounds (handles retina scaling)
        CGFloat scaleFactor = [nsWindow backingScaleFactor];
        metalLayer.contentsScale = scaleFactor;  // Update in case window moved to different display

        CGSize newDrawableSize = CGSizeMake(viewBounds.size.width * scaleFactor,
                                            viewBounds.size.height * scaleFactor);

        // NOTE: Drawable size is now changed ONLY via force_drawable_resize()
        // which is called while holding all GPU semaphores (GPU is idle).
        // DO NOT auto-change drawable size here - causes kernel panic!
        // See GPU_RENDERING_CRASH_BUG.md for full analysis.
        //
        // Just track expected size for logging purposes:
        last_drawable_width_ = newDrawableSize.width;
        last_drawable_height_ = newDrawableSize.height;

        // Log configuration on first frame only
        static bool logged_config = false;
        if (!logged_config) {
            std::cout << "[METAL CONFIG] Render: " << render_width << "×" << render_height
                      << " | View: " << viewBounds.size.width << "×" << viewBounds.size.height << " points"
                      << " | Drawable: " << newDrawableSize.width << "×" << newDrawableSize.height << " pixels"
                      << " (scale: " << scaleFactor << "×)" << std::endl;
            logged_config = true;
        }

        // DEBUG: Trace Metal layer state before getting drawable - DISABLED (causes per-frame overhead)
        // static int layer_trace_count = 0;
        // if (layer_trace_count < 3) {
        //     std::cout << "[LAYER " << layer_trace_count << "] before nextDrawable:"
        //               << " drawableSize=" << metalLayer.drawableSize.width << "x" << metalLayer.drawableSize.height
        //               << " frame=" << metalLayer.frame.size.width << "x" << metalLayer.frame.size.height
        //               << " contentsScale=" << metalLayer.contentsScale
        //               << " device=" << metalLayer.device
        //               << " pixelFormat=" << metalLayer.pixelFormat
        //               << " framebufferOnly=" << (metalLayer.framebufferOnly ? "YES" : "NO")
        //               << " presentsWithTransaction=" << (metalLayer.presentsWithTransaction ? "YES" : "NO") << std::endl;
        //     layer_trace_count++;
        // }

        // Get drawable
        // VSYNC TEST: Time drawable acquisition to detect vsync blocking
        static int drawable_timing_counter = 0;
        drawable_timing_counter++;
        auto drawable_start = std::chrono::high_resolution_clock::now();

        id<CAMetalDrawable> drawable = [metalLayer nextDrawable];

        auto drawable_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - drawable_start).count();

        // Log every 60 frames to capture actual timings
        if (drawable_timing_counter % 60 == 0) {
            std::cout << "[VSYNC_TEST] Frame " << drawable_timing_counter
                      << " | nextDrawable took: " << std::fixed << std::setprecision(2)
                      << drawable_ms << "ms" << (drawable_ms > 1.0 ? " (SIGNIFICANT)" : "") << std::endl;
        }

        if (!drawable) {
            std::cerr << "[METAL ERROR] Failed to get drawable!" << std::endl;
            return;
        }

        // DEBUG: Trace drawable state - DISABLED (causes per-frame overhead)
        // static int drawable_trace_count = 0;
        // if (drawable_trace_count < 3) {
        //     std::cout << "[DRAWABLE " << drawable_trace_count << "] acquired:"
        //               << " ptr=" << drawable
        //               << " presentedTime=" << drawable.presentedTime
        //               << " drawableID=" << drawable.drawableID << std::endl;
        // }

        id<MTLTexture> drawableTexture = drawable.texture;
        if (!drawableTexture) {
            std::cerr << "[METAL ERROR] Failed to get drawable texture!" << std::endl;
            return;
        }

        // DEBUG: Trace drawable texture state - DISABLED (causes per-frame overhead)
        // if (drawable_trace_count < 3) {
        //     std::cout << "[DRAWABLE " << drawable_trace_count << "] texture:"
        //               << " size=" << drawableTexture.width << "x" << drawableTexture.height
        //               << " pixelFormat=" << drawableTexture.pixelFormat
        //               << " usage=" << drawableTexture.usage
        //               << " storageMode=" << drawableTexture.storageMode << std::endl;
        //     drawable_trace_count++;
        // }

        // Determine render path: direct copy vs GPU scaling
        // Note: window_width/window_height params are actually RENDER dimensions (misleading name)
        // We always compare render size to drawable size for path selection
        if (render_width == static_cast<int>(drawableTexture.width) &&
            render_height == static_cast<int>(drawableTexture.height)) {
            // Fast path: direct copy, no scaling needed
            MTLRegion region = MTLRegionMake2D(0, 0, render_width, render_height);
            [drawableTexture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:buffer
                               bytesPerRow:render_width * 4];

            // UI overlay rect on top (already composited by the caller)
            if (overlay && overlay_w > 0 && overlay_h > 0 &&
                overlay_x >= 0 && overlay_y >= 0 &&
                overlay_x + overlay_w <= render_width &&
                overlay_y + overlay_h <= render_height) {
                MTLRegion ov = MTLRegionMake2D(overlay_x, overlay_y, overlay_w, overlay_h);
                [drawableTexture replaceRegion:ov
                                   mipmapLevel:0
                                     withBytes:overlay
                                   bytesPerRow:overlay_w * 4];
            }

            // Fast path doesn't use command buffers - direct CPU→GPU upload
            // No need to store command buffer reference here
        } else {
            // Hardware scaling path: create texture at render resolution, let Metal scale it

            // DEBUG: Log scaling on first frame
            static bool logged_scaling = false;
            if (!logged_scaling) {
                std::cout << "[METAL SCALE] Scaling " << render_width << "x" << render_height
                          << " → " << drawableTexture.width << "x" << drawableTexture.height << std::endl;

                // Check buffer content
                if (buffer) {
                    uint32_t first_pixel = *reinterpret_cast<const uint32_t*>(buffer);
                    std::cout << "[METAL SCALE] Source buffer first pixel: 0x" << std::hex << first_pixel << std::dec << std::endl;
                }
                logged_scaling = true;
            }

            // Cache texture at RENDER resolution with ShaderWrite for MPS
            static id<MTLTexture> renderTexture = nil;
            static int last_render_width = 0;
            static int last_render_height = 0;

            // Recreate texture only if size changed
            if (!renderTexture || last_render_width != render_width || last_render_height != render_height) {
                MTLTextureDescriptor* renderTextureDesc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                    width:render_width
                    height:render_height
                    mipmapped:NO];
                renderTextureDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

                renderTexture = [device newTextureWithDescriptor:renderTextureDesc];
                if (!renderTexture) {
                    std::cout << "[METAL ERROR] Failed to create render texture!" << std::endl;
                    return;
                }
                last_render_width = render_width;
                last_render_height = render_height;
                std::cout << "[METAL] Created/resized source texture: " << render_width << "x" << render_height << std::endl;
            }

            // Upload our render buffer to the render-sized texture
            MTLRegion region = MTLRegionMake2D(0, 0, render_width, render_height);

            // DEBUG: Verify buffer content - DISABLED (causes per-frame overhead)
            // static int upload_count = 0;
            // if (upload_count < 5 && buffer) {
            //     const uint32_t* pixels = reinterpret_cast<const uint32_t*>(buffer);
            //     int total = render_width * render_height;
            //     std::cout << "[PRE-UPLOAD " << upload_count << "] Buffer state:" << std::endl;
            //     std::cout << "  [0]=0x" << std::hex << pixels[0];
            //     if (total > 1000) std::cout << " [1000]=0x" << pixels[1000];
            //     if (total > 100000) std::cout << " [100000]=0x" << pixels[100000];
            //     std::cout << " [center]=0x" << pixels[total/2];
            //     std::cout << " [last]=0x" << pixels[total-1] << std::dec << std::endl;
            //     upload_count++;
            // }

            auto upload_start = std::chrono::high_resolution_clock::now();
            [renderTexture replaceRegion:region
                             mipmapLevel:0
                               withBytes:buffer
                             bytesPerRow:render_width * 4];

            // UI overlay rect on top, at render resolution — MPS scales
            // the composited result to the drawable below.
            if (overlay && overlay_w > 0 && overlay_h > 0 &&
                overlay_x >= 0 && overlay_y >= 0 &&
                overlay_x + overlay_w <= render_width &&
                overlay_y + overlay_h <= render_height) {
                MTLRegion ov = MTLRegionMake2D(overlay_x, overlay_y, overlay_w, overlay_h);
                [renderTexture replaceRegion:ov
                                 mipmapLevel:0
                                   withBytes:overlay
                                 bytesPerRow:overlay_w * 4];
            }
            auto upload_ms = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - upload_start).count();

            static double total_upload_ms = 0;
            static int upload_count = 0;
            total_upload_ms += upload_ms;
            upload_count++;

            if (upload_count % 60 == 0) {
                std::cout << "[UPLOAD_TIMING] Avg texture upload: " << std::fixed << std::setprecision(2)
                          << (total_upload_ms / 60.0) << "ms over 60 frames" << std::endl;
                total_upload_ms = 0;
            }

            // DEBUG: Verify texture upload - DISABLED (causes per-frame overhead + GPU stall!)
            // static int readback_count = 0;
            // if (readback_count < 3) {
            //     std::vector<uint32_t> readback(render_width * render_height);
            //     [renderTexture getBytes:readback.data()
            //                 bytesPerRow:render_width * 4
            //                  fromRegion:region
            //                 mipmapLevel:0];
            //     std::cout << "[POST-UPLOAD " << readback_count << "] Texture readback: [0]=0x" << std::hex
            //               << readback[0] << " [1000]=0x" << readback[1000]
            //               << " [center]=0x" << readback[render_width*render_height/2] << std::dec << std::endl;
            //     readback_count++;
            // }

            // Hardware scaling with MPS (now works because framebufferOnly = NO)
            static id<MTLCommandQueue> commandQueue = nil;
            if (!commandQueue) {
                commandQueue = [device newCommandQueue];
                std::cout << "[METAL] Created command queue" << std::endl;
            }

            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

            // DEBUG: Trace command buffer state on first 3 frames
            static int cmdbuf_trace_count = 0;
            // if (cmdbuf_trace_count < 3) {
            //     std::cout << "[CMDBUF " << cmdbuf_trace_count << "] created:"
            //               << " ptr=" << commandBuffer
            //               << " device=" << commandBuffer.device
            //               << " status=" << commandBuffer.status << std::endl;
            // }

            // MPS scaler - configure once per destination size
            static MPSImageBilinearScale* scaler = nil;
            static int last_dest_width = 0;
            static int last_dest_height = 0;

            // Recreate scaler if destination size changed
            if (!scaler || last_dest_width != drawableTexture.width || last_dest_height != drawableTexture.height) {
                std::cout << "[MPS DEBUG] Creating scaler: scaler=" << (scaler ? "exists" : "nil")
                          << " last_dest=" << last_dest_width << "x" << last_dest_height
                          << " drawable=" << drawableTexture.width << "x" << drawableTexture.height << std::endl;

                scaler = [[MPSImageBilinearScale alloc] initWithDevice:device];

                // CRITICAL: Explicitly set clipRect to destination size
                // Default clipRect may not work correctly - must match destination exactly
                MTLRegion clipRegion;
                clipRegion.origin = MTLOriginMake(0, 0, 0);
                clipRegion.size = MTLSizeMake(drawableTexture.width, drawableTexture.height, 1);
                scaler.clipRect = clipRegion;

                last_dest_width = drawableTexture.width;
                last_dest_height = drawableTexture.height;

                std::cout << "[METAL] Created MPS scaler for dest " << drawableTexture.width << "x" << drawableTexture.height << std::endl;
                std::cout << "[MPS CONFIG] clipRect explicitly set to: " << clipRegion.size.width << "x" << clipRegion.size.height << std::endl;

                // DEBUG: Trace scaler configuration
                if (cmdbuf_trace_count < 3) {
                    std::cout << "[SCALER " << cmdbuf_trace_count << "] config:"
                              << " device=" << scaler.device
                              << " clipRect=" << scaler.clipRect.size.width << "x" << scaler.clipRect.size.height << std::endl;
                }
            } // else if (cmdbuf_trace_count < 3) {
            //     std::cout << "[SCALER " << cmdbuf_trace_count << "] reusing existing scaler" << std::endl;
            // }

            // Calculate aspect ratios for letterboxing
            float render_aspect = (float)render_width / render_height;
            float drawable_aspect = (float)drawableTexture.width / drawableTexture.height;
            bool needs_letterbox = std::abs(render_aspect - drawable_aspect) > 0.01f;

            static bool logged_mps_dims = false;
            static int present_count = 0;

            if (!logged_mps_dims) {
                std::cout << "[MPS SCALE] Source texture: " << renderTexture.width << "x" << renderTexture.height << std::endl;
                std::cout << "[MPS SCALE] Dest texture: " << drawableTexture.width << "x" << drawableTexture.height << std::endl;
                std::cout << "[MPS SCALE] Render aspect: " << render_aspect << " Drawable aspect: " << drawable_aspect << std::endl;
                std::cout << "[MPS SCALE] Letterboxing: " << (needs_letterbox ? "YES" : "NO") << std::endl;
                logged_mps_dims = true;
            }

            // Handle letterboxing if aspect ratios don't match
            if (needs_letterbox) {
                // Calculate viewport (fit render aspect into drawable, centered)
                int viewport_w, viewport_h;
                if (render_aspect > drawable_aspect) {
                    // Render is wider - letterbox top/bottom
                    viewport_w = drawableTexture.width;
                    viewport_h = (int)(drawableTexture.width / render_aspect);
                } else {
                    // Render is taller - letterbox left/right
                    viewport_h = drawableTexture.height;
                    viewport_w = (int)(drawableTexture.height * render_aspect);
                }

                int offset_x = (drawableTexture.width - viewport_w) / 2;
                int offset_y = (drawableTexture.height - viewport_h) / 2;

                static bool logged_letterbox = false;
                if (!logged_letterbox) {
                    std::cout << "[LETTERBOX] Viewport: " << viewport_w << "x" << viewport_h
                              << " at offset (" << offset_x << "," << offset_y << ")" << std::endl;
                    logged_letterbox = true;
                }

                // Clear drawable to black first for letterbox bars
                id<MTLRenderCommandEncoder> clearEncoder = [commandBuffer renderCommandEncoderWithDescriptor:
                    ({
                        MTLRenderPassDescriptor *desc = [MTLRenderPassDescriptor renderPassDescriptor];
                        desc.colorAttachments[0].texture = drawableTexture;
                        desc.colorAttachments[0].loadAction = MTLLoadActionClear;
                        desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                        desc.colorAttachments[0].storeAction = MTLStoreActionStore;
                        desc;
                    })];
                [clearEncoder endEncoding];

                // Create intermediate texture at viewport size (for aspect-correct scaling)
                static id<MTLTexture> intermediateTexture = nil;
                static int last_viewport_w = 0;
                static int last_viewport_h = 0;

                if (!intermediateTexture || last_viewport_w != viewport_w || last_viewport_h != viewport_h) {
                    MTLTextureDescriptor* intermediateDesc = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                        width:viewport_w
                        height:viewport_h
                        mipmapped:NO];
                    intermediateDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                    intermediateTexture = [device newTextureWithDescriptor:intermediateDesc];
                    last_viewport_w = viewport_w;
                    last_viewport_h = viewport_h;
                    std::cout << "[LETTERBOX] Created intermediate texture: " << viewport_w << "x" << viewport_h << std::endl;
                }

                // Scale render → intermediate (perfect aspect match)
                [scaler encodeToCommandBuffer:commandBuffer
                                 sourceTexture:renderTexture
                            destinationTexture:intermediateTexture];

                // Blit intermediate → drawable at centered offset
                id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
                [blitEncoder copyFromTexture:intermediateTexture
                                 sourceSlice:0
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:MTLSizeMake(viewport_w, viewport_h, 1)
                                   toTexture:drawableTexture
                            destinationSlice:0
                            destinationLevel:0
                           destinationOrigin:MTLOriginMake(offset_x, offset_y, 0)];
                [blitEncoder endEncoding];
            } else {
                // No letterboxing needed - scale directly
                // DEBUG: Present logging - DISABLED (causes per-frame overhead)
                // if (present_count < 3) {
                //     std::cout << "[PRESENT " << present_count << "] About to encode MPS" << std::endl;
                //     std::cout << "[PRESENT " << present_count << "] Source texture: " << renderTexture
                //               << " size=" << renderTexture.width << "x" << renderTexture.height << std::endl;
                //     std::cout << "[PRESENT " << present_count << "] Dest texture: " << drawableTexture
                //               << " size=" << drawableTexture.width << "x" << drawableTexture.height << std::endl;
                // }

                [scaler encodeToCommandBuffer:commandBuffer
                                 sourceTexture:renderTexture
                            destinationTexture:drawableTexture];
            }

            // DEBUG: Present logging - DISABLED (causes per-frame overhead)
            // if (present_count < 3) {
            //     std::cout << "[PRESENT " << present_count << "] MPS encoded, status=" << commandBuffer.status << std::endl;
            // }
            // present_count++;

            // BOTTLENECK DIAGNOSIS: Time commit and present separately
            static int present_timing_counter = 0;
            present_timing_counter++;

            auto commit_start = std::chrono::high_resolution_clock::now();
            [commandBuffer commit];
            // CRITICAL: Wait for presentation GPU work to complete before returning.
            // Without this, the presentation command queue runs concurrently with
            // GPURasterizer's compute queue on the same Metal device. Combined with
            // ~750MB of compute buffers, this concurrent contention causes kernel panics.
            // Cost: ~1-2ms CPU block per frame. Worth it to prevent system crashes.
            [commandBuffer waitUntilCompleted];
            auto commit_ms = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - commit_start).count();

            auto present_start = std::chrono::high_resolution_clock::now();
            [drawable present];
            auto present_ms = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - present_start).count();

            // Log every 60 frames to capture timing breakdown
            if (present_timing_counter % 60 == 0) {
                std::cout << "[BOTTLENECK] Frame " << present_timing_counter
                          << " | commit: " << std::fixed << std::setprecision(2) << commit_ms << "ms"
                          << " | present: " << present_ms << "ms"
                          << " | TOTAL: " << (commit_ms + present_ms) << "ms" << std::endl;
            }

            // DEBUG: Present logging - DISABLED (causes per-frame overhead)
            // if (present_count < 3) {
            //     std::cout << "[PRESENT " << present_count << "] Presented, status=" << commandBuffer.status << std::endl;
            //     std::cout << "[PRESENT " << present_count << "] Drawable presentedTime=" << drawable.presentedTime << std::endl;
            //     present_count++;
            //     cmdbuf_trace_count++;
            // }

            return;
        }
        
        // Present immediately (for fast path only)
        [drawable present];
    }
}

void PlatformMacOS::swap_buffers() {
    // NO-OP: Metal handles presentation via drawable.present() in present_framebuffer()
    // This method exists for interface compatibility but does nothing
}

void PlatformMacOS::force_drawable_resize(int width, int height) {
    // CRITICAL: This must only be called while holding all GPU semaphores!
    // The caller is responsible for ensuring GPU is completely idle.
    //
    // This is the ONLY safe place to change CAMetalLayer.drawableSize.
    // Changing it during active GPU rendering causes kernel panic.

    if (!metal_layer_) {
        std::cerr << "[PLATFORM] force_drawable_resize: no Metal layer!" << std::endl;
        return;
    }

    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)metal_layer_;
    CGSize newSize = CGSizeMake(width, height);
    CGSize currentSize = metalLayer.drawableSize;

    // Only change if different
    if (currentSize.width == newSize.width && currentSize.height == newSize.height) {
        std::cout << "[PLATFORM] force_drawable_resize: size unchanged (" << width << "x" << height << ")" << std::endl;
        return;
    }

    std::cout << "[PLATFORM] force_drawable_resize: changing drawable "
              << currentSize.width << "x" << currentSize.height
              << " → " << width << "x" << height << std::endl;

    // Change drawable size while GPU is idle (caller holds semaphores)
    metalLayer.drawableSize = newSize;

    // Wait for Metal to process the change
    // This gives the system time to invalidate old drawables and create new ones
    usleep(100000);  // 100ms - conservative wait for drawable pool to stabilize

    // Update tracking
    last_drawable_width_ = width;
    last_drawable_height_ = height;

    std::cout << "[PLATFORM] force_drawable_resize: complete" << std::endl;
}

double PlatformMacOS::get_time() const {
    return glfwGetTime();
}

void PlatformMacOS::set_swap_interval(int interval) {
    // NO-OP: Metal controls vsync via presentsWithTransaction
}

void PlatformMacOS::set_cursor_visible(bool visible) {
    if (window_) {
        glfwSetInputMode(window_, GLFW_CURSOR, 
                        visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
}

void PlatformMacOS::set_cursor_position(double x, double y) {
    if (window_) {
        glfwSetCursorPos(window_, x, y);
    }
}

void PlatformMacOS::get_cursor_position(double& x, double& y) const {
    if (window_) {
        glfwGetCursorPos(window_, &x, &y);
    } else {
        x = y = 0;
    }
}

void PlatformMacOS::set_clipboard_text(const char* text) {
    if (window_) {
        glfwSetClipboardString(window_, text);
    }
}

const char* PlatformMacOS::get_clipboard_text() {
    if (window_) {
        return glfwGetClipboardString(window_);
    }
    return "";
}

void PlatformMacOS::update_dpi_scale() {
    if (!window_) return;
    
    int window_width, window_height;
    glfwGetWindowSize(window_, &window_width, &window_height);
    
    int fb_width, fb_height;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    
    if (window_width > 0 && window_height > 0) {
        dpi_scale_x_ = static_cast<float>(fb_width) / window_width;
        dpi_scale_y_ = static_cast<float>(fb_height) / window_height;
    }
}

// GLFW Callbacks
void PlatformMacOS::error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void PlatformMacOS::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;

    bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
    platform->callbacks_->on_key_event(
        platform->glfw_to_keycode(key, mods),
        scancode,
        pressed,
        platform->glfw_to_modifiers(mods)
    );
}

void PlatformMacOS::char_callback(GLFWwindow* window, unsigned int codepoint) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;

    platform->callbacks_->on_char_input(codepoint);
}

void PlatformMacOS::cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;
    
    platform->callbacks_->on_mouse_move(xpos, ypos);
}

void PlatformMacOS::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;
    
    bool pressed = (action == GLFW_PRESS);
    platform->callbacks_->on_mouse_button(
        platform->glfw_to_mouse_button(button),
        pressed,
        platform->glfw_to_modifiers(mods)
    );
}

void PlatformMacOS::scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;
    
    platform->callbacks_->on_mouse_scroll(xoffset, yoffset);
}

void PlatformMacOS::window_size_callback(GLFWwindow* window, int width, int height) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform) return;
    
    // Update DPI scale when window resizes
    platform->update_dpi_scale();
    
    if (platform->callbacks_) {
        platform->callbacks_->on_window_resize(width, height);
    }
}

void PlatformMacOS::window_close_callback(GLFWwindow* window) {
    auto* platform = static_cast<PlatformMacOS*>(glfwGetWindowUserPointer(window));
    if (!platform || !platform->callbacks_) return;
    
    platform->callbacks_->on_window_close();
}

// Helper conversions
KeyCode PlatformMacOS::glfw_to_keycode(int glfw_key, int mods) const {
    // Map GLFW keys to our platform-independent key codes
    // For letter keys, respect shift modifier for case sensitivity

    // CRITICAL: Handle both uppercase GLFW_KEY_* (65-90) AND lowercase codes macOS sends (97-122)
    // macOS GLFW sends lowercase character codes, other platforms may send GLFW_KEY_* constants
    // Normalize to GLFW_KEY_* constants for platform-independent key binding
    if ((glfw_key >= GLFW_KEY_A && glfw_key <= GLFW_KEY_Z) ||
        (glfw_key >= 'a' && glfw_key <= 'z')) {

        // Normalize to uppercase GLFW_KEY_* range (65-90)
        int normalized_key = glfw_key;
        if (glfw_key >= 'a' && glfw_key <= 'z') {
            // Convert lowercase char to uppercase GLFW_KEY_* (a->A: 97->65)
            normalized_key = GLFW_KEY_A + (glfw_key - 'a');
        }

        // Return GLFW_KEY_* constant directly for consistent key binding
        // Applications check GLFW_KEY_A/B/C/etc, not lowercase 'a'/'b'/'c'
        // Shift state is passed separately via mods parameter
        return static_cast<KeyCode>(normalized_key);
    }
    if (glfw_key >= GLFW_KEY_0 && glfw_key <= GLFW_KEY_9) {
        return static_cast<KeyCode>('0' + (glfw_key - GLFW_KEY_0));
    }
    
    switch (glfw_key) {
        case GLFW_KEY_SPACE: return KeyCode::Space;
        case GLFW_KEY_ENTER: return KeyCode::Enter;
        case GLFW_KEY_TAB: return KeyCode::Tab;
        case GLFW_KEY_ESCAPE: return KeyCode::Escape;
        case GLFW_KEY_BACKSPACE: return KeyCode::Backspace;
        case GLFW_KEY_DELETE: return KeyCode::Delete;
        case GLFW_KEY_GRAVE_ACCENT: return KeyCode::GraveAccent;
        case GLFW_KEY_COMMA: return KeyCode::Comma;
        case GLFW_KEY_PERIOD: return KeyCode::Period;
        case GLFW_KEY_SEMICOLON: return KeyCode::Semicolon;
        case GLFW_KEY_LEFT_BRACKET: return KeyCode::LeftBracket;
        case GLFW_KEY_RIGHT_BRACKET: return KeyCode::RightBracket;
        case GLFW_KEY_BACKSLASH: return KeyCode::Backslash;
        case GLFW_KEY_MINUS: return KeyCode::Minus;
        case GLFW_KEY_EQUAL: return KeyCode::Equal;
        case GLFW_KEY_UP: return KeyCode::Up;
        case GLFW_KEY_DOWN: return KeyCode::Down;
        case GLFW_KEY_LEFT: return KeyCode::Left;
        case GLFW_KEY_RIGHT: return KeyCode::Right;
        case GLFW_KEY_F1: return KeyCode::F1;
        case GLFW_KEY_F2: return KeyCode::F2;
        case GLFW_KEY_F3: return KeyCode::F3;
        case GLFW_KEY_F4: return KeyCode::F4;
        case GLFW_KEY_F5: return KeyCode::F5;
        case GLFW_KEY_F6: return KeyCode::F6;
        case GLFW_KEY_F7: return KeyCode::F7;
        case GLFW_KEY_F8: return KeyCode::F8;
        case GLFW_KEY_F9: return KeyCode::F9;
        case GLFW_KEY_F10: return KeyCode::F10;
        case GLFW_KEY_F11: return KeyCode::F11;
        case GLFW_KEY_F12: return KeyCode::F12;
        case GLFW_KEY_LEFT_SHIFT: return KeyCode::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return KeyCode::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return KeyCode::LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return KeyCode::RightControl;
        case GLFW_KEY_LEFT_ALT: return KeyCode::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return KeyCode::RightAlt;
        case GLFW_KEY_LEFT_SUPER: return KeyCode::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER: return KeyCode::RightSuper;
        default: return KeyCode::Unknown;
    }
}

MouseButton PlatformMacOS::glfw_to_mouse_button(int glfw_button) const {
    switch (glfw_button) {
        case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
        case GLFW_MOUSE_BUTTON_4: return MouseButton::Button4;
        case GLFW_MOUSE_BUTTON_5: return MouseButton::Button5;
        default: return MouseButton::Left;
    }
}

ModifierKeys PlatformMacOS::glfw_to_modifiers(int mods) const {
    ModifierKeys keys;
    keys.shift = (mods & GLFW_MOD_SHIFT) != 0;
    keys.control = (mods & GLFW_MOD_CONTROL) != 0;
    keys.alt = (mods & GLFW_MOD_ALT) != 0;
    keys.super = (mods & GLFW_MOD_SUPER) != 0;
    return keys;
}

} // namespace Platform