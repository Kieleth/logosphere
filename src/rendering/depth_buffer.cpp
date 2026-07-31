#include "logosphere/rendering/depth_buffer.h"
#include <algorithm>

// Default constructor
DepthBuffer::DepthBuffer()
    : width_(0)
    , height_(0) {
}

// Constructor with dimensions
DepthBuffer::DepthBuffer(int width, int height)
    : width_(width)
    , height_(height)
    , buffer_(width * height, std::numeric_limits<float>::max()) {
}

// Resize the buffer
void DepthBuffer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    
    // Resize and initialize to max depth (nothing drawn)
    buffer_.resize(width * height, std::numeric_limits<float>::max());
}

// Clear buffer to specified depth (default: max)
void DepthBuffer::clear(float max_depth) {
    // Fast fill with specified depth value
    std::fill(buffer_.begin(), buffer_.end(), max_depth);
}

// Test if pixel at given depth should be drawn
bool DepthBuffer::test(int x, int y, float depth) const {
    if (!is_valid_coord(x, y)) {
        return false;
    }
    
    // Pixel is visible if its depth is less than what's stored
    // (closer to camera = smaller depth value)
    return depth < buffer_[index(x, y)];
}

// Test and update depth in one operation
bool DepthBuffer::test_and_set(int x, int y, float depth) {
    return test_and_set(x, y, depth, false);
}

// Test and set with squared depth optimization
bool DepthBuffer::test_and_set(int x, int y, float depth, bool using_squared_depth) {
    if (!is_valid_coord(x, y)) {
        return false;
    }
    
    int idx = index(x, y);
    
    // Add epsilon tolerance for floating point comparison
    // This helps with surfaces that should be at the same depth but have
    // tiny variations due to floating point precision
    // When using squared distances, we need a larger epsilon
    const float epsilon = using_squared_depth ? 0.1f : 0.001f;
    
    // Check if this pixel is closer (or within epsilon tolerance)
    if (depth <= buffer_[idx] + epsilon) {
        // Update depth and return true (pixel should be drawn)
        buffer_[idx] = depth;
        return true;
    }
    
    // Pixel is behind existing content
    return false;
}

// Set depth directly (no testing)
void DepthBuffer::set_depth(int x, int y, float depth) {
    if (is_valid_coord(x, y)) {
        buffer_[index(x, y)] = depth;
    }
}

// Get depth at position
float DepthBuffer::get_depth(int x, int y) const {
    if (!is_valid_coord(x, y)) {
        return std::numeric_limits<float>::max();
    }
    return buffer_[index(x, y)];
}

// Check if coordinates are valid
bool DepthBuffer::is_valid_coord(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

// Get minimum depth in buffer (closest pixel)
float DepthBuffer::get_min_depth() const {
    if (buffer_.empty()) {
        return std::numeric_limits<float>::max();
    }
    return *std::min_element(buffer_.begin(), buffer_.end());
}

// Get maximum depth in buffer (furthest pixel)
float DepthBuffer::get_max_depth() const {
    if (buffer_.empty()) {
        return std::numeric_limits<float>::min();
    }
    
    // Find max that isn't the initialization value
    float max_depth = std::numeric_limits<float>::min();
    float init_depth = std::numeric_limits<float>::max();
    
    for (float depth : buffer_) {
        if (depth < init_depth && depth > max_depth) {
            max_depth = depth;
        }
    }
    
    return max_depth;
}

// Count pixels that have been drawn to (depth < max)
int DepthBuffer::count_pixels_with_depth() const {
    float init_depth = std::numeric_limits<float>::max();
    int count = 0;
    
    for (float depth : buffer_) {
        if (depth < init_depth) {
            count++;
        }
    }
    
    return count;
}