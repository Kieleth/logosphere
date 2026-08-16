// The well: pure board logic, no engine, no GLFW.
//
// Extracted so the rules (collision, locking, line clears, scoring)
// can be exercised headless without dragging the renderer into the
// test, the same reason examples/logotron/src/walls.h exists.
//
// The grid stores knowledge-graph entity ids, not colours. A cell is
// occupied iff a TetrisMino entity is standing in it, so the graph is
// the record of the locked field and this array is its index, not a
// second copy of the truth.

#pragma once

#include <array>
#include <vector>

#include "logosphere/kg/kg_types.h"
#include "tetromino.h"

namespace tetris {

inline constexpr int kCols = 10;
inline constexpr int kRows = 20;

// Row 0 is the floor; row kRows-1 is the ceiling.
class Well {
public:
    Well() { clear(); }

    void clear() {
        for (auto& row : cells_) row.fill(kg::INVALID_ENTITY);
    }

    kg::EntityID at(int col, int row) const {
        if (!inside(col, row)) return kg::INVALID_ENTITY;
        return cells_[row][col];
    }

    void put(int col, int row, kg::EntityID e) { cells_[row][col] = e; }

    static bool inside(int col, int row) {
        return col >= 0 && col < kCols && row >= 0 && row < kRows;
    }

    bool occupied(int col, int row) const {
        return at(col, row) != kg::INVALID_ENTITY;
    }

    // A placement is legal when all four cells are inside the walls,
    // above the floor, and empty. Above the ceiling is NOT legal: the
    // spawn rows are inside the well, which is what makes topping out
    // detectable.
    bool fits(Kind kind, int rot, int px, int py) const {
        for (const Cell& c : kShapes[static_cast<int>(kind)][rot & 3]) {
            const int col = px + c.dx, row = py + c.dy;
            if (!inside(col, row)) return false;
            if (occupied(col, row)) return false;
        }
        return true;
    }

    // Rows that are full, listed from the floor up.
    std::vector<int> full_rows() const {
        std::vector<int> full;
        for (int r = 0; r < kRows; r++) {
            bool all = true;
            for (int c = 0; c < kCols; c++) {
                if (!occupied(c, r)) { all = false; break; }
            }
            if (all) full.push_back(r);
        }
        return full;
    }

    // Drop everything above the cleared rows by exactly the number of
    // cleared rows beneath it. The caller has already taken the
    // entities in `rows` out of the graph; this only closes the gaps
    // they left.
    //
    // The first version of this compacted every EMPTY row instead of
    // every CLEARED row, which is a different thing and quietly wrong:
    // an all-empty row can sit under an overhang without having been
    // cleared, and squeezing it out teleports the stack down a row
    // nobody earned. tests/test_tetris_well.cpp caught it on the first
    // run; nothing on screen would have.
    void collapse(const std::vector<int>& rows) {
        bool cleared[kRows] = {false};
        for (int r : rows) if (r >= 0 && r < kRows) cleared[r] = true;

        int write = 0;
        for (int read = 0; read < kRows; read++) {
            if (cleared[read]) continue;
            if (write != read) cells_[write] = cells_[read];
            write++;
        }
        for (int r = write; r < kRows; r++) cells_[r].fill(kg::INVALID_ENTITY);
    }

private:
    std::array<std::array<kg::EntityID, kCols>, kRows> cells_;
};

// Guideline scoring. Level multiplies, so the same tetris is worth
// more the deeper you are.
inline int line_score(int lines, int level) {
    static const int base[5] = {0, 100, 300, 500, 800};
    return base[lines < 0 ? 0 : (lines > 4 ? 4 : lines)] * level;
}

inline int level_for(int total_lines) { return 1 + total_lines / 10; }

inline float fall_interval_for(int level) {
    const float s = 0.80f - 0.065f * static_cast<float>(level - 1);
    return s < 0.06f ? 0.06f : s;
}

}  // namespace tetris
