#ifndef TREE_SKELETON_H
#define TREE_SKELETON_H

#include <vector>
#include "math_types.h"

// Single branch segment in skeleton
struct BranchSegment {
    Vec3 start;
    Vec3 end;
    float thickness;
    int parent_index;      // Index of parent segment in skeleton (-1 for root)
    int creation_iteration; // Growth iteration when this branch was created (for leaf maturity)

    BranchSegment()
        : start(), end(), thickness(1.0f), parent_index(-1), creation_iteration(0) {}

    BranchSegment(Vec3 s, Vec3 e, float t, int p = -1, int iter = 0)
        : start(s), end(e), thickness(t), parent_index(p), creation_iteration(iter) {}

    float length() const {
        return (end - start).length();
    }

    Vec3 direction() const {
        return (end - start).normalized();
    }
};

// Tree skeleton: list of connected branch segments
// Output of organic growth algorithms (Space Colonization, L-System, etc.)
class TreeSkeleton {
public:
    TreeSkeleton() {}

    void add_segment(const BranchSegment& seg) {
        segments.push_back(seg);
    }

    void add_segment(Vec3 start, Vec3 end, float thickness, int parent_idx = -1) {
        segments.push_back(BranchSegment(start, end, thickness, parent_idx));
    }

    int segment_count() const {
        return segments.size();
    }

    const BranchSegment& get_segment(int idx) const {
        return segments[idx];
    }

    BranchSegment& get_segment(int idx) {
        return segments[idx];
    }

    const std::vector<BranchSegment>& get_segments() const {
        return segments;
    }

    void clear() {
        segments.clear();
    }

private:
    std::vector<BranchSegment> segments;
};

#endif // TREE_SKELETON_H
