#ifndef DEPTH_BUFFER_H
#define DEPTH_BUFFER_H

#include <vector>
#include <limits>
#include <cstdint>

/**
 * DepthBuffer - Dedicated Z-buffer management for depth testing
 * 
 * Following KISS & UNIX philosophy: This class does ONE thing well - depth testing.
 * It manages the Z-buffer that tracks the depth of each pixel to handle occlusion.
 * 
 * Part of the rendering system refactoring to break down the monolithic RenderSystem.
 * Small, focused, composable primitive that can be tested independently.
 * 
 * Historical note: The Z-buffer algorithm was invented by Edwin Catmull in 1974.
 * It revolutionized 3D graphics by providing a simple solution to the visibility problem.
 * Before Z-buffers, games like Elite (1984) used painter's algorithm (back-to-front sorting).
 */
class DepthBuffer {
public:
    DepthBuffer();
    DepthBuffer(int width, int height);
    ~DepthBuffer() = default;
    
    // Buffer management
    void resize(int width, int height);
    void clear(float max_depth = std::numeric_limits<float>::max());
    
    // Core depth operations
    // Test if a pixel at (x,y) with given depth should be drawn
    // Returns true if pixel is closer (should draw), false if behind existing
    bool test(int x, int y, float depth) const;
    
    // Test and update in one operation (most common use case)
    // Returns true if pixel was closer and buffer was updated
    bool test_and_set(int x, int y, float depth);
    
    // Test and set with squared depth optimization
    // When using_squared_depth is true, uses larger epsilon for comparison
    bool test_and_set(int x, int y, float depth, bool using_squared_depth);
    
    // Direct depth access
    void set_depth(int x, int y, float depth);
    float get_depth(int x, int y) const;
    
    // Utility methods
    bool is_valid_coord(int x, int y) const;
    int width() const { return width_; }
    int height() const { return height_; }
    size_t size() const { return buffer_.size(); }
    
    // Statistics for debugging
    float get_min_depth() const;
    float get_max_depth() const;
    int count_pixels_with_depth() const;
    
private:
    int width_;
    int height_;
    std::vector<float> buffer_;
    
    // Helper to calculate buffer index from coordinates
    inline int index(int x, int y) const {
        return y * width_ + x;
    }
};

#endif // DEPTH_BUFFER_H