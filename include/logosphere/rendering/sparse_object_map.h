#ifndef SPARSE_OBJECT_MAP_H
#define SPARSE_OBJECT_MAP_H

#include <vector>
#include <cstdint>

/**
 * SparseObjectMap - Fixed-size array for object IDs at pixel coordinates
 *
 * Uses a pre-allocated array (width × height) initialized to 0 (background).
 * Memory: ~5MB for 1280×720 (4 bytes per pixel).
 *
 * Thread Safety: Multiple threads can write to different pixels safely
 * (no locks needed since tile-based rendering guarantees non-overlapping writes).
 * This matches PixelBuffer's thread-safety model.
 */
class SparseObjectMap {
public:
    SparseObjectMap(int width, int height);
    ~SparseObjectMap() = default;

    // Set object ID at pixel (0 means no object/background)
    void set_object(int x, int y, int object_id);

    // Get object ID at pixel (returns 0 if not found)
    int get_object(int x, int y) const;

    // Clear all entries (reset to 0)
    void clear();

    // Get number of non-zero entries currently stored
    size_t size() const;

    // Get memory usage in bytes
    size_t memory_usage() const {
        return buffer_.size() * sizeof(int) + sizeof(*this);
    }

    // Get dimensions
    int width() const { return width_; }
    int height() const { return height_; }

private:
    // Bounds check
    inline bool is_valid(int x, int y) const {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    // Convert 2D coordinates to index
    inline size_t index(int x, int y) const {
        return static_cast<size_t>(y * width_ + x);
    }

    std::vector<int> buffer_;  // Pre-allocated width*height, 0 = background
    int width_;
    int height_;
};

#endif // SPARSE_OBJECT_MAP_H