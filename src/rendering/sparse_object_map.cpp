#include "logosphere/rendering/sparse_object_map.h"
#include <algorithm>

SparseObjectMap::SparseObjectMap(int width, int height)
    : buffer_(width * height, 0)  // Pre-allocate and initialize to 0
    , width_(width)
    , height_(height) {
}

void SparseObjectMap::set_object(int x, int y, int object_id) {
    if (!is_valid(x, y)) return;
    buffer_[index(x, y)] = object_id;
}

int SparseObjectMap::get_object(int x, int y) const {
    if (!is_valid(x, y)) return 0;
    return buffer_[index(x, y)];
}

void SparseObjectMap::clear() {
    // Reset all entries to 0 (background)
    std::fill(buffer_.begin(), buffer_.end(), 0);
}

size_t SparseObjectMap::size() const {
    // Count non-zero entries
    return std::count_if(buffer_.begin(), buffer_.end(),
                        [](int id) { return id != 0; });
}