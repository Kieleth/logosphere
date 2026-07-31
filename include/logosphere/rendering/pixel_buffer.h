#ifndef PIXEL_BUFFER_H
#define PIXEL_BUFFER_H

#include <vector>
#include <limits>
#include <cstdint>
#include <algorithm>
#include "optimization_flags.h"
#include "platform/platform_system.h"

/**
 * EnhancedPixel - Pixel structure with object tracking
 * 
 * This innovative structure combines visual data (RGBA) with object identification,
 * enabling pixel-perfect mouse interaction without separate buffers or ray casting.
 * Each pixel knows what object it belongs to, making selection trivial.
 */
struct EnhancedPixel {
    uint8_t r, g, b, a;  // Visual color data
    int object_id;       // Object that owns this pixel (particle, UI, etc.)
    
    EnhancedPixel() : r(0), g(0), b(0), a(255), object_id(0) {}
    EnhancedPixel(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, int id = 0) 
        : r(red), g(green), b(blue), a(alpha), object_id(id) {}
};

/**
 * PixelBuffer - Low-level framebuffer operations and pixel manipulation
 * 
 * Extracted from the monolithic RenderSystem to follow Single Responsibility Principle.
 * This class manages the framebuffer memory and provides primitive pixel operations.
 * 
 * Historical note: Direct framebuffer manipulation was how all early graphics worked
 * before hardware acceleration. Games like Doom and Quake started with software
 * renderers that directly wrote to pixel buffers like this.
 */
class PixelBuffer {
public:
    PixelBuffer();
    PixelBuffer(int width, int height);
    
    // Buffer management
    void resize(int width, int height);
    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, int object_id = 0);
    void clear(const EnhancedPixel& clear_pixel);
    
    // Core pixel operations
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void set_pixel_with_object(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id);
    void set_enhanced_pixel(int x, int y, const EnhancedPixel& pixel);
    
    // Pixel queries
    EnhancedPixel get_pixel(int x, int y) const;
    int get_object_at(int x, int y) const;
    bool is_valid_coord(int x, int y) const;
    
    // Buffer access
#if USE_NATIVE_PIXEL_FORMAT
    // Provide access to native buffer for rasterizer
    const std::vector<uint32_t>& get_native_buffer() const { return native_buffer_; }
    std::vector<uint32_t>& get_native_buffer() { return native_buffer_; }
    
    // Debug buffer access for tests. Empty unless set_debug_mode(true)
    // has run: allocation is lazy, so no build pays for it at rest.
    const std::vector<EnhancedPixel>& get_debug_buffer() const { return debug_buffer_; }
    std::vector<EnhancedPixel>& get_debug_buffer() { return debug_buffer_; }
    const std::vector<EnhancedPixel>& get_buffer() const { return debug_buffer_; }
    std::vector<EnhancedPixel>& get_buffer() { return debug_buffer_; }
#else
    const std::vector<EnhancedPixel>& get_buffer() const { return buffer_; }
    std::vector<EnhancedPixel>& get_buffer() { return buffer_; }
#endif
    
    // Raw buffer for platform display (RGBA only, no object IDs)
    std::vector<uint8_t> get_raw_rgba_buffer() const;
    std::vector<uint8_t> get_raw_bgra_buffer() const;  // For Metal/macOS which expects BGRA
    
    // Optimized version that reuses existing buffer to avoid allocation
    void fill_bgra_buffer(std::vector<uint8_t>& output_buffer) const;
    
    // Direct access to native buffer data for zero-copy presentation
    // Returns nullptr if not using native format
    const uint8_t* get_native_data() const {
#if USE_NATIVE_PIXEL_FORMAT
        return reinterpret_cast<const uint8_t*>(native_buffer_.data());
#else
        return nullptr;
#endif
    }
    
    // Buffer operations
    void copy_from(const PixelBuffer& source);
    void copy_region(const PixelBuffer& source, 
                     int src_x, int src_y, int src_width, int src_height,
                     int dst_x, int dst_y);
    
    // Alpha blending
    void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id = 0);
    
    // Properties
    int width() const { return width_; }
    int height() const { return height_; }
    
    // ---------------------------------------------------------------
    // Dirty-bounds tracking (opt-in, OFF by default)
    // ---------------------------------------------------------------
    // For the UI overlay plane: the HUD paints a small fraction of a
    // full-resolution buffer, so clearing and compositing the whole
    // thing wastes most of the work (at retina a full clear alone is
    // ~27 MB of writes). With tracking on, set_pixel/blend_pixel widen
    // a bounding box that the overlay owner uses to clear, composite,
    // and (once the composite moves GPU-side) upload only what the UI
    // actually touched.
    //
    // OFF by default so the scene buffer's write paths keep exactly the
    // instructions they had; when on it is one predictable branch plus
    // four compares per written pixel.
    void set_track_dirty_bounds(bool enabled) { track_dirty_ = enabled; }
    bool has_dirty_bounds() const { return dirty_max_x_ >= dirty_min_x_; }
    // Valid only when has_dirty_bounds(); inclusive on all four edges.
    void get_dirty_bounds(int& min_x, int& min_y, int& max_x, int& max_y) const {
        min_x = dirty_min_x_; min_y = dirty_min_y_;
        max_x = dirty_max_x_; max_y = dirty_max_y_;
    }
    void reset_dirty_bounds() {
        dirty_min_x_ = dirty_min_y_ = std::numeric_limits<int>::max();
        dirty_max_x_ = dirty_max_y_ = std::numeric_limits<int>::min();
    }

    // Pull the current native (BGRA) contents into the debug buffer.
    // The GPU pipeline writes the native buffer directly, bypassing
    // set_pixel_with_object, so tests reading get_buffer() after a
    // GPU render must call this first. object_id is not recoverable
    // from the native format and comes back 0.
    void sync_debug_from_native() {
#if USE_NATIVE_PIXEL_FORMAT
        if (!debug_mode_) return;
        for (size_t i = 0; i < native_buffer_.size(); ++i) {
            uint32_t bgra = native_buffer_[i];
            EnhancedPixel& px = debug_buffer_[i];
            px.a = uint8_t(bgra >> 24);
            px.r = uint8_t(bgra >> 16);
            px.g = uint8_t(bgra >> 8);
            px.b = uint8_t(bgra);
            px.object_id = 0;
        }
#endif
    }

    // Debug control: enabling allocates the debug buffer lazily; the
    // per-frame cost exists only while a test has this on.
    void set_debug_mode(bool enabled) {
#if USE_NATIVE_PIXEL_FORMAT
        debug_mode_ = enabled;
        if (enabled) {
            debug_buffer_.assign(static_cast<size_t>(width_) * height_,
                                 EnhancedPixel());
        } else {
            debug_buffer_.clear();
            debug_buffer_.shrink_to_fit();
        }
#else
        (void)enabled;
#endif
    }
    size_t size() const {
#if USE_NATIVE_PIXEL_FORMAT
        return native_buffer_.size();  // Already one uint32_t per pixel
#else
        return buffer_.size();
#endif
    }
    
private:
    // Widen the dirty box to include (x, y). Called only from the
    // write paths, only when tracking is on.
    inline void mark_dirty(int x, int y) {
        if (x < dirty_min_x_) dirty_min_x_ = x;
        if (y < dirty_min_y_) dirty_min_y_ = y;
        if (x > dirty_max_x_) dirty_max_x_ = x;
        if (y > dirty_max_y_) dirty_max_y_ = y;
    }

    int width_;
    int height_;

    // Dirty-bounds tracking (see set_track_dirty_bounds)
    bool track_dirty_ = false;
    int dirty_min_x_ = std::numeric_limits<int>::max();
    int dirty_min_y_ = std::numeric_limits<int>::max();
    int dirty_max_x_ = std::numeric_limits<int>::min();
    int dirty_max_y_ = std::numeric_limits<int>::min();
    
    // Hybrid approach: native for speed, debug buffer for testing
#if USE_NATIVE_PIXEL_FORMAT
    // CRITICAL: Use uint32_t for proper alignment! vector<uint8_t> has no alignment guarantees!
    std::vector<uint32_t> native_buffer_;  // BGRA format as packed 32-bit values
    Platform::IPlatformSystem::PixelFormat pixel_format_ = Platform::IPlatformSystem::PixelFormat::BGRA_8888;
    
    // Debug buffer for test inspection; empty unless debug_mode_ is on
    // (lazy allocation in set_debug_mode).
    std::vector<EnhancedPixel> debug_buffer_;
    bool debug_mode_ = false;
#else
    std::vector<EnhancedPixel> buffer_;   // Legacy format with embedded object IDs
#endif
    
    // Helper to calculate buffer index from coordinates
    inline int index(int x, int y) const {
        return y * width_ + x;
    }
    
    // Alpha blending helper
    inline uint8_t blend_channel(uint8_t src, uint8_t dst, uint8_t alpha) const {
        return ((src * alpha) + (dst * (255 - alpha))) / 255;
    }
};

#endif // PIXEL_BUFFER_H