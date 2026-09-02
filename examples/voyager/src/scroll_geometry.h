// Blocks stacked in a panel, and where they land once the panel
// scrolls.
//
// The doors are blocks of several lines each; the prose, the prompt,
// the notes and the sheet are blocks of exactly one. Both shapes need
// the same four answers: how far the list can move, which block a
// pointer is over, how far to move so a given block is whole in view,
// and where a wheel notch leaves it. Written once, here, with no
// widget and no draw surface in it, so those answers can be measured
// instead of clicked at. Measured in test_voyager_doors.
//
// The panel's visible band is [0, panel) in the panel's own pixels.
// `lead` is the blank kept above the first block.

#ifndef VOYAGER_SCROLL_GEOMETRY_H
#define VOYAGER_SCROLL_GEOMETRY_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace voyager {

struct ScrollGeometry {
    std::vector<int> heights;   // one per block, in order
    int panel = 0;              // the panel's own height
    int lead = 0;               // blank kept above the first block

    int content() const {
        int total = lead;
        for (int height : heights) total += height;
        return total;
    }

    int max_scroll() const { return std::max(0, content() - panel); }

    int clamp_scroll(int scroll) const {
        return std::max(0, std::min(scroll, max_scroll()));
    }

    // The unscrolled top edge of block `index`.
    int top_of(std::size_t index) const {
        int y = lead;
        const std::size_t upto = std::min(index, heights.size());
        for (std::size_t i = 0; i < upto; ++i) y += heights[i];
        return y;
    }

    // Which block covers `local_y`, or -1 for the gaps and the ends.
    int block_at(int local_y, int scroll) const {
        int y = lead - scroll;
        for (std::size_t i = 0; i < heights.size(); ++i) {
            if (local_y >= y && local_y < y + heights[i]) {
                return static_cast<int>(i);
            }
            y += heights[i];
        }
        return -1;
    }

    // The scroll that shows block `index` whole. A block taller than
    // the panel cannot be whole, so it is shown from its top: the
    // reader starts at the first line either way.
    int keep_in_view(std::size_t index, int scroll) const {
        scroll = clamp_scroll(scroll);
        if (index >= heights.size()) return scroll;
        const int top = top_of(index);
        const int height = heights[index];
        if (height > panel) return clamp_scroll(top);
        if (top - scroll < 0) return clamp_scroll(top);
        if (top + height - scroll > panel) {
            return clamp_scroll(top + height - panel);
        }
        return scroll;
    }
};

}  // namespace voyager

#endif  // VOYAGER_SCROLL_GEOMETRY_H
