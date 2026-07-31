#include "logosphere/rendering/pixel_buffer.h"
#include <cstring>  // For memcpy
#include <iostream> // For debug output

PixelBuffer::PixelBuffer() : width_(0), height_(0) {
    // Default constructor - buffer must be resized before use
}

PixelBuffer::PixelBuffer(int width, int height) : width_(width), height_(height) {
    // Initialize buffer with given dimensions
#if USE_NATIVE_PIXEL_FORMAT
    native_buffer_.resize(width * height);  // One uint32_t per pixel
    // Clear to black BGRA (0xFF000000 = black with full alpha)
    uint32_t black = 0xFF000000;  // BGRA: A=FF, R=00, G=00, B=00
    std::fill(native_buffer_.begin(), native_buffer_.end(), black);
    #ifdef DEBUG_BUILD
    // Also initialize debug buffer for tests
    debug_buffer_.resize(width * height);
    for (auto& pixel : debug_buffer_) {
        pixel.r = 0;
        pixel.g = 0;
        pixel.b = 0;
        pixel.a = 255;
        pixel.object_id = 0;
    }
    #endif
#else
    buffer_.resize(width * height);
    clear(0, 0, 0, 255, 0);  // Clear to black with full alpha
#endif
}

void PixelBuffer::resize(int width, int height) {
    // Resize the buffer for new dimensions
    width_ = width;
    height_ = height;
#if USE_NATIVE_PIXEL_FORMAT
    native_buffer_.resize(width * height);  // One uint32_t per pixel
    // Clear after resize to black with full alpha
    uint32_t black = 0xFF000000;  // BGRA: A=FF, R=00, G=00, B=00
    std::fill(native_buffer_.begin(), native_buffer_.end(), black);
    #ifdef DEBUG_BUILD
    // Also resize debug buffer
    debug_buffer_.resize(width * height);
    for (auto& pixel : debug_buffer_) {
        pixel.r = 0;
        pixel.g = 0;
        pixel.b = 0;
        pixel.a = 255;
        pixel.object_id = 0;
    }
    #endif
#else
    buffer_.resize(width * height);
    clear(0, 0, 0, 255, 0);  // Clear after resize
#endif
}

void PixelBuffer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id) {
    // Clear entire buffer to a single color and object ID
#if USE_NATIVE_PIXEL_FORMAT
    // Clear native buffer
    uint32_t bgra = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);

    // DEBUG: Track buffer clears for first 5 frames
    static int clear_count = 0;
    if (clear_count < 5) {
        std::cout << "[BUFFER CLEAR " << clear_count << "] RGBA(" << (int)r << "," << (int)g
                  << "," << (int)b << "," << (int)a << ") → BGRA=0x" << std::hex << bgra << std::dec
                  << " pixels=" << native_buffer_.size() << std::endl;
        clear_count++;
    }

    std::fill(native_buffer_.begin(), native_buffer_.end(), bgra);
    #ifdef DEBUG_BUILD
    // Only clear debug buffer when needed for tests
    if (debug_mode_) {
        EnhancedPixel clear_pixel(r, g, b, a, object_id);
        std::fill(debug_buffer_.begin(), debug_buffer_.end(), clear_pixel);
    }
    #endif
#else
    EnhancedPixel clear_pixel(r, g, b, a, object_id);
    std::fill(buffer_.begin(), buffer_.end(), clear_pixel);
#endif
}

void PixelBuffer::clear(const EnhancedPixel& clear_pixel) {
    // Clear entire buffer with a specific pixel value
#if USE_NATIVE_PIXEL_FORMAT
    // Clear native buffer with color from EnhancedPixel
    uint32_t bgra = (uint32_t(clear_pixel.a) << 24) | (uint32_t(clear_pixel.r) << 16) | 
                    (uint32_t(clear_pixel.g) << 8) | uint32_t(clear_pixel.b);
    std::fill(native_buffer_.begin(), native_buffer_.end(), bgra);
    #ifdef DEBUG_BUILD
    // Only clear debug buffer when needed for tests
    if (debug_mode_) {
        std::fill(debug_buffer_.begin(), debug_buffer_.end(), clear_pixel);
    }
    #endif
#else
    std::fill(buffer_.begin(), buffer_.end(), clear_pixel);
#endif
}

void PixelBuffer::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Set pixel without object ID (defaults to 0)
    if (!is_valid_coord(x, y)) return;

    if (track_dirty_) mark_dirty(x, y);

    int idx = index(x, y);
#if USE_NATIVE_PIXEL_FORMAT
    // Direct write to native buffer - now properly aligned!
    // Pack BGRA into a 32-bit value
    uint32_t bgra = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    native_buffer_[idx] = bgra;  // Direct array access, guaranteed aligned!
#else
    buffer_[idx].r = r;
    buffer_[idx].g = g;
    buffer_[idx].b = b;
    buffer_[idx].a = a;
    // Keep existing object_id unchanged
#endif
}

void PixelBuffer::set_pixel_with_object(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id) {
    // Set pixel with specific object ID
    if (!is_valid_coord(x, y)) return;

    if (track_dirty_) mark_dirty(x, y);

    int idx = index(x, y);
#if USE_NATIVE_PIXEL_FORMAT
    // Write to native buffer - properly aligned!
    uint32_t bgra = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    native_buffer_[idx] = bgra;
    
    // PERFORMANCE TEST: Remove debug buffer check entirely
    // #ifdef DEBUG_BUILD
    // // Only update debug buffer when explicitly needed for tests
    // if (debug_mode_) {
    //     debug_buffer_[idx].r = r;
    //     debug_buffer_[idx].g = g;
    //     debug_buffer_[idx].b = b;
    //     debug_buffer_[idx].a = a;
    //     debug_buffer_[idx].object_id = object_id;
    // }
    // #endif
#else
    buffer_[idx].r = r;
    buffer_[idx].g = g;
    buffer_[idx].b = b;
    buffer_[idx].a = a;
    buffer_[idx].object_id = object_id;
#endif
}

void PixelBuffer::set_enhanced_pixel(int x, int y, const EnhancedPixel& pixel) {
    // Set complete pixel data
    if (!is_valid_coord(x, y)) return;
#if USE_NATIVE_PIXEL_FORMAT
    // In native mode, extract color only
    int idx = index(x, y);
    uint32_t bgra = (uint32_t(pixel.a) << 24) | (uint32_t(pixel.r) << 16) | 
                    (uint32_t(pixel.g) << 8) | uint32_t(pixel.b);
    native_buffer_[idx] = bgra;
#else
    buffer_[index(x, y)] = pixel;
#endif
}

EnhancedPixel PixelBuffer::get_pixel(int x, int y) const {
    // Get complete pixel data
    if (!is_valid_coord(x, y)) {
        return EnhancedPixel();  // Return black pixel if out of bounds
    }
#if USE_NATIVE_PIXEL_FORMAT
    // In native mode, reconstruct EnhancedPixel from native buffer (no object_id)
    int idx = index(x, y);
    uint32_t bgra = native_buffer_[idx];
    return EnhancedPixel(
        (bgra >> 16) & 0xFF,  // R
        (bgra >> 8) & 0xFF,   // G
        bgra & 0xFF,          // B
        (bgra >> 24) & 0xFF,  // A
        0                     // No object_id in native mode
    );
#else
    return buffer_[index(x, y)];
#endif
}

int PixelBuffer::get_object_at(int x, int y) const {
    // Get object ID at specific coordinates
    if (!is_valid_coord(x, y)) {
        return 0;  // Background/no object
    }
#if USE_NATIVE_PIXEL_FORMAT
    // In native mode, object IDs are stored in sparse map in SurfaceRasterizer
    return 0;  // Always return 0 - rasterizer handles object tracking
#else
    return buffer_[index(x, y)].object_id;
#endif
}

bool PixelBuffer::is_valid_coord(int x, int y) const {
    // Check if coordinates are within buffer bounds
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

std::vector<uint8_t> PixelBuffer::get_raw_rgba_buffer() const {
    // Extract RGBA data for platform display (strips object IDs)
#if USE_NATIVE_PIXEL_FORMAT
    // Native format is BGRA, need to convert to RGBA
    std::vector<uint8_t> rgba_buffer;
    rgba_buffer.reserve(native_buffer_.size() * 4);
    
    for (uint32_t bgra : native_buffer_) {
        rgba_buffer.push_back((bgra >> 16) & 0xFF);  // R
        rgba_buffer.push_back((bgra >> 8) & 0xFF);   // G
        rgba_buffer.push_back(bgra & 0xFF);          // B
        rgba_buffer.push_back((bgra >> 24) & 0xFF);  // A
    }
    
    return rgba_buffer;
#else
    std::vector<uint8_t> rgba_buffer;
    rgba_buffer.reserve(buffer_.size() * 4);
    
    for (const auto& pixel : buffer_) {
        rgba_buffer.push_back(pixel.r);
        rgba_buffer.push_back(pixel.g);
        rgba_buffer.push_back(pixel.b);
        rgba_buffer.push_back(pixel.a);
    }
    
    return rgba_buffer;
#endif
}

std::vector<uint8_t> PixelBuffer::get_raw_bgra_buffer() const {
    // Extract BGRA data for Metal/macOS display (strips object IDs)
#if USE_NATIVE_PIXEL_FORMAT
    // Native format is uint32_t BGRA - need to convert to uint8_t array
    std::vector<uint8_t> bgra_buffer;
    bgra_buffer.reserve(native_buffer_.size() * 4);
    
    for (uint32_t bgra : native_buffer_) {
        bgra_buffer.push_back(bgra & 0xFF);          // B
        bgra_buffer.push_back((bgra >> 8) & 0xFF);   // G
        bgra_buffer.push_back((bgra >> 16) & 0xFF);  // R
        bgra_buffer.push_back((bgra >> 24) & 0xFF);  // A
    }
    
    return bgra_buffer;
#else
    std::vector<uint8_t> bgra_buffer;
    bgra_buffer.reserve(buffer_.size() * 4);
    
    for (const auto& pixel : buffer_) {
        bgra_buffer.push_back(pixel.b);  // B first for Metal
        bgra_buffer.push_back(pixel.g);
        bgra_buffer.push_back(pixel.r);  // R third for Metal
        bgra_buffer.push_back(pixel.a);
    }
    
    return bgra_buffer;
#endif
}

void PixelBuffer::copy_from(const PixelBuffer& source) {
    // Copy entire buffer from another PixelBuffer
    if (source.width_ != width_ || source.height_ != height_) {
        // Resize if dimensions don't match
        resize(source.width_, source.height_);
    }
#if USE_NATIVE_PIXEL_FORMAT
    native_buffer_ = source.native_buffer_;
#else
    buffer_ = source.buffer_;
#endif
}

void PixelBuffer::copy_region(const PixelBuffer& source,
                              int src_x, int src_y, int src_width, int src_height,
                              int dst_x, int dst_y) {
    // Copy a rectangular region from source to this buffer
    // Clip source region to source bounds
    int actual_src_x = std::max(0, src_x);
    int actual_src_y = std::max(0, src_y);
    int actual_src_right = std::min(source.width_, src_x + src_width);
    int actual_src_bottom = std::min(source.height_, src_y + src_height);
    
    // Clip destination region to destination bounds
    int actual_dst_x = std::max(0, dst_x);
    int actual_dst_y = std::max(0, dst_y);
    
    // Calculate actual copy dimensions
    int copy_width = std::min(actual_src_right - actual_src_x, width_ - actual_dst_x);
    int copy_height = std::min(actual_src_bottom - actual_src_y, height_ - actual_dst_y);
    
    // Copy row by row
    for (int row = 0; row < copy_height; row++) {
        int src_row = actual_src_y + row;
        int dst_row = actual_dst_y + row;
        
        for (int col = 0; col < copy_width; col++) {
            int src_col = actual_src_x + col;
            int dst_col = actual_dst_x + col;
            
#if USE_NATIVE_PIXEL_FORMAT
            // Copy pixel (uint32_t) from source to destination
            int src_idx = source.index(src_col, src_row);
            int dst_idx = index(dst_col, dst_row);
            native_buffer_[dst_idx] = source.native_buffer_[src_idx];
#else
            buffer_[index(dst_col, dst_row)] = source.buffer_[source.index(src_col, src_row)];
#endif
        }
    }
}

void PixelBuffer::blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id) {
    // Alpha blend new color with existing pixel
    if (!is_valid_coord(x, y)) return;
    
    if (a == 0) return;  // Fully transparent, no change

    if (track_dirty_) mark_dirty(x, y);

    int idx = index(x, y);
    
#if USE_NATIVE_PIXEL_FORMAT
    if (a == 255) {
        // Fully opaque, just set the pixel
        uint32_t bgra = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
        native_buffer_[idx] = bgra;
        // Note: object_id ignored in native mode (handled by sparse map)
    } else {
        // Alpha blending
        uint32_t dst_bgra = native_buffer_[idx];
        uint8_t dst_b = dst_bgra & 0xFF;
        uint8_t dst_g = (dst_bgra >> 8) & 0xFF;
        uint8_t dst_r = (dst_bgra >> 16) & 0xFF;
        uint8_t dst_a = (dst_bgra >> 24) & 0xFF;
        
        uint8_t new_b = blend_channel(b, dst_b, a);
        uint8_t new_g = blend_channel(g, dst_g, a);
        uint8_t new_r = blend_channel(r, dst_r, a);
        uint8_t new_a = std::max(dst_a, a);
        
        native_buffer_[idx] = (uint32_t(new_a) << 24) | (uint32_t(new_r) << 16) | 
                              (uint32_t(new_g) << 8) | uint32_t(new_b);
    }
#else
    if (a == 255) {
        // Fully opaque, just set the pixel
        buffer_[idx].r = r;
        buffer_[idx].g = g;
        buffer_[idx].b = b;
        buffer_[idx].a = a;
        if (object_id != 0) {
            buffer_[idx].object_id = object_id;
        }
    } else {
        // Alpha blending
        EnhancedPixel& dst = buffer_[idx];
        dst.r = blend_channel(r, dst.r, a);
        dst.g = blend_channel(g, dst.g, a);
        dst.b = blend_channel(b, dst.b, a);
        // Alpha channel uses max (not blended)
        dst.a = std::max(dst.a, a);
        // Object ID from topmost non-transparent pixel
        if (object_id != 0 && a > 127) {  // More than 50% opaque
            dst.object_id = object_id;
        }
    }
#endif
}

void PixelBuffer::fill_bgra_buffer(std::vector<uint8_t>& output_buffer) const {
    // Optimized version that reuses existing buffer
#if USE_NATIVE_PIXEL_FORMAT
    // Convert uint32_t BGRA to uint8_t array
    output_buffer.resize(native_buffer_.size() * 4);
    size_t idx = 0;
    for (uint32_t bgra : native_buffer_) {
        output_buffer[idx++] = bgra & 0xFF;          // B
        output_buffer[idx++] = (bgra >> 8) & 0xFF;   // G
        output_buffer[idx++] = (bgra >> 16) & 0xFF;  // R
        output_buffer[idx++] = (bgra >> 24) & 0xFF;  // A
    }
#else
    // Resize only if needed (avoids reallocation if size is same)
    const size_t needed_size = buffer_.size() * 4;
    if (output_buffer.size() != needed_size) {
        output_buffer.resize(needed_size);
    }
    
    // Direct memory write without push_back overhead
    size_t idx = 0;
    for (const auto& pixel : buffer_) {
        output_buffer[idx++] = pixel.b;  // B first for Metal
        output_buffer[idx++] = pixel.g;
        output_buffer[idx++] = pixel.r;  // R third for Metal  
        output_buffer[idx++] = pixel.a;
    }
#endif
}