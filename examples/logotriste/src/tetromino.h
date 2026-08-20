// The seven tetrominoes, as data.
//
// Each piece is four cells inside a bounding box, given as (dx, dy)
// offsets from the box's bottom-left corner. dy grows UPWARD, which
// matches the board's row numbering (row 0 is the floor) and, once
// mapped to world coordinates, matches +Y in the engine.
//
// Rotation states are listed clockwise from spawn. The tables are
// the classic Super Rotation System shapes; the kick list below is
// the simple five-offset variant, not the full SRS wall-kick tables.

#pragma once

#include <array>
#include <cstddef>

namespace logotriste {

enum class Kind : int { I = 0, O, T, S, Z, J, L, COUNT };

inline constexpr int kKindCount = static_cast<int>(Kind::COUNT);

// The letter the ontology's TetrominoKind enum uses.
inline const char* kind_name(Kind k) {
    static const char* names[] = {"I", "O", "T", "S", "Z", "J", "L"};
    return names[static_cast<int>(k)];
}

struct Cell { int dx, dy; };
using Shape = std::array<Cell, 4>;

// [kind][rotation][cell]
inline constexpr Shape kShapes[kKindCount][4] = {
    // I
    {{{{0,2},{1,2},{2,2},{3,2}}},
     {{{2,3},{2,2},{2,1},{2,0}}},
     {{{0,1},{1,1},{2,1},{3,1}}},
     {{{1,3},{1,2},{1,1},{1,0}}}},
    // O
    {{{{1,2},{2,2},{1,1},{2,1}}},
     {{{1,2},{2,2},{1,1},{2,1}}},
     {{{1,2},{2,2},{1,1},{2,1}}},
     {{{1,2},{2,2},{1,1},{2,1}}}},
    // T
    {{{{1,2},{0,1},{1,1},{2,1}}},
     {{{1,2},{1,1},{2,1},{1,0}}},
     {{{0,1},{1,1},{2,1},{1,0}}},
     {{{1,2},{0,1},{1,1},{1,0}}}},
    // S
    {{{{1,2},{2,2},{0,1},{1,1}}},
     {{{1,2},{1,1},{2,1},{2,0}}},
     {{{1,1},{2,1},{0,0},{1,0}}},
     {{{0,2},{0,1},{1,1},{1,0}}}},
    // Z
    {{{{0,2},{1,2},{1,1},{2,1}}},
     {{{2,2},{1,1},{2,1},{1,0}}},
     {{{0,1},{1,1},{1,0},{2,0}}},
     {{{1,2},{0,1},{1,1},{0,0}}}},
    // J
    {{{{0,2},{0,1},{1,1},{2,1}}},
     {{{1,2},{2,2},{1,1},{1,0}}},
     {{{0,1},{1,1},{2,1},{2,0}}},
     {{{1,2},{1,1},{0,0},{1,0}}}},
    // L
    {{{{2,2},{0,1},{1,1},{2,1}}},
     {{{1,2},{1,1},{1,0},{2,0}}},
     {{{0,1},{1,1},{2,1},{0,0}}},
     {{{0,2},{1,2},{1,1},{1,0}}}},
};

struct Rgb { float r, g, b; };

inline constexpr Rgb kColors[kKindCount] = {
    {0.20f, 0.95f, 1.00f},  // I  cyan
    {1.00f, 0.85f, 0.15f},  // O  yellow
    {0.75f, 0.30f, 1.00f},  // T  purple
    {0.25f, 1.00f, 0.35f},  // S  green
    {1.00f, 0.25f, 0.25f},  // Z  red
    {0.30f, 0.45f, 1.00f},  // J  blue
    {1.00f, 0.55f, 0.10f},  // L  orange
};

// Kick attempts tried in order when a rotation collides.
//
// The last two are downward, and they are not decoration. The I piece
// occupies a 4x4 box: standing it up needs a cell three rows above the
// box floor, which does not exist at the spawn row. Without a downward
// kick the I simply cannot be turned vertical until gravity has moved
// it, and it took a standalone probe over Well::fits to see that —
// watching the game, it just felt like the key was being dropped.
inline constexpr Cell kKicks[7] = {
    {0,0}, {-1,0}, {1,0}, {-2,0}, {2,0}, {0,-1}, {0,-2}};

}  // namespace logotriste
