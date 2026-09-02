// The scrolling, measured. Both lists that scroll in this game use
// the same arithmetic: the doors, whose blocks are as many lines as
// the door needs, and the prose, whose blocks are single lines.
//
// The properties, over a table of panel sizes and block runs rather
// than one hand-picked case:
//   1. block_at agrees with a plain walk down the list, for EVERY
//      pixel row of the panel and at every legal scroll
//   2. max_scroll is the content minus the panel, never negative
//   3. keep_in_view leaves the block whole in the panel, from any
//      starting scroll, whenever the block is short enough to be
//      whole at all; a taller one starts at its first line
//   4. a wheel notch never leaves the range
//
// Property 1 is the one that catches a click landing on the door
// above the one under the pointer, which is the bug that makes a
// player take a door they did not choose.

#undef NDEBUG

#include "scroll_geometry.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

// The same question answered the slow, obvious way.
int walk_to(const voyager::ScrollGeometry& geometry, int local_y,
            int scroll) {
    int y = geometry.lead - scroll;
    for (std::size_t i = 0; i < geometry.heights.size(); ++i) {
        const int bottom = y + geometry.heights[i];
        if (local_y >= y && local_y < bottom) return static_cast<int>(i);
        y = bottom;
    }
    return -1;
}

std::string shape_of(const voyager::ScrollGeometry& geometry) {
    std::string out = "panel=" + std::to_string(geometry.panel) +
                      " lead=" + std::to_string(geometry.lead) + " [";
    for (int height : geometry.heights) out += std::to_string(height) + " ";
    return out + "]";
}

void check_geometry(voyager::ScrollGeometry geometry) {
    // 2. the range
    int content = geometry.lead;
    for (int height : geometry.heights) content += height;
    CHECK(geometry.content() == content,
          shape_of(geometry) << ": content is " << geometry.content()
          << ", the blocks add up to " << content);
    CHECK(geometry.max_scroll() >= 0,
          shape_of(geometry) << ": max_scroll is negative");
    CHECK(geometry.max_scroll() ==
              (content > geometry.panel ? content - geometry.panel : 0),
          shape_of(geometry) << ": max_scroll is "
          << geometry.max_scroll() << " for content " << content);

    const int top_scroll = geometry.max_scroll();
    const std::vector<int> scrolls = {0, top_scroll / 3, top_scroll / 2,
                                      top_scroll};
    for (int scroll : scrolls) {
        // 1. every pixel row of the panel
        for (int y = 0; y < geometry.panel; ++y) {
            const int block = geometry.block_at(y, scroll);
            CHECK(block == walk_to(geometry, y, scroll),
                  shape_of(geometry) << " scroll=" << scroll << " row="
                  << y << ": block_at says " << block << ", walking the "
                  "list says " << walk_to(geometry, y, scroll));
        }
        // 4. a wheel notch, either way, stays in range
        for (int notch : {-4, -1, 1, 4}) {
            const int after = geometry.clamp_scroll(scroll - notch * 13);
            CHECK(after >= 0 && after <= geometry.max_scroll(),
                  shape_of(geometry) << ": a wheel notch left the scroll "
                  "at " << after << ", outside [0, "
                  << geometry.max_scroll() << "]");
        }
        // 3. keep_in_view, from this scroll and from two absurd ones
        for (int from : {scroll, -100000, 100000}) {
            for (std::size_t i = 0; i < geometry.heights.size(); ++i) {
                const int after = geometry.keep_in_view(i, from);
                CHECK(after >= 0 && after <= geometry.max_scroll(),
                      shape_of(geometry) << ": keep_in_view(" << i
                      << ") left the scroll at " << after);
                const int top = geometry.top_of(i) - after;
                const int bottom = top + geometry.heights[i];
                if (geometry.heights[i] <= geometry.panel) {
                    CHECK(top >= 0 && bottom <= geometry.panel,
                          shape_of(geometry) << " from " << from
                          << ": keep_in_view(" << i << ") left the block "
                          "at [" << top << ", " << bottom << ") in a "
                          << geometry.panel << "-tall panel");
                } else {
                    CHECK(top <= 0 && bottom > 0,
                          shape_of(geometry) << ": a block taller than "
                          "the panel was scrolled out of sight, to ["
                          << top << ", " << bottom << ")");
                }
            }
        }
    }
}

}  // namespace

int main() {
    // The doors: blocks of one to four lines, a lead above the first.
    const std::vector<std::vector<int>> door_runs = {
        {},
        {39},
        {39, 65, 39},
        {39, 39, 39, 39, 39, 39, 39, 39, 39, 39},
        {117, 39, 91, 143, 39, 65},
        {520},               // one door taller than any panel here
    };
    for (const auto& heights : door_runs) {
        for (int panel : {0, 40, 130, 316, 900}) {
            for (int lead : {0, 13}) {
                voyager::ScrollGeometry geometry;
                geometry.heights = heights;
                geometry.panel = panel;
                geometry.lead = lead;
                check_geometry(geometry);
            }
        }
    }

    // The prose: every block one line tall, no lead. Same arithmetic.
    for (int line : {13, 26, 52}) {
        for (int rows : {0, 1, 3, 8, 40}) {
            for (int panel : {0, 130, 316}) {
                voyager::ScrollGeometry geometry;
                geometry.heights.assign(static_cast<std::size_t>(rows), line);
                geometry.panel = panel;
                check_geometry(geometry);
            }
        }
    }

    // The two answers the screen leans on, spelled out.
    {
        voyager::ScrollGeometry geometry;
        geometry.heights.assign(20, 26);   // 20 rows of prose
        geometry.panel = 8 * 26;           // a box that holds 8
        CHECK(geometry.max_scroll() == 12 * 26,
              "20 rows in a box of 8 should scroll by 12 rows, not "
              << (geometry.max_scroll() / 26.0));
        // Newest visible: the box resting at the end shows the last 8.
        CHECK(geometry.block_at(0, geometry.max_scroll()) == 12,
              "at the end of the scroll the top row should be row 12, "
              "not row " << geometry.block_at(0, geometry.max_scroll()));
        CHECK(geometry.block_at(geometry.panel - 1,
                                geometry.max_scroll()) == 19,
              "at the end of the scroll the bottom row should be the "
              "last one");
        CHECK(geometry.block_at(0, 0) == 0,
              "at the start of the scroll the top row should be row 0");
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
