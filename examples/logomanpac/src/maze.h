// The board and the rules of moving on it. No engine, no KG, no GLFW:
// everything here is pure so the headless acceptance test can drive a
// whole game of Logomanpac without opening a window.
//
// Coordinate convention, and it is the one that bites:
//   - CELL space is (col, row) with row 0 at the TOP, the way the maze
//     is written as text. Column grows right, row grows DOWN.
//   - WORLD space is the engine's: +x right, +y UP (BirdsEyeProjection
//     in src/projection_system.cpp:190 negates y to get screen y).
//   - So cell_to_world flips the row: world_y = (rows-1)/2 - row.
// Every position in this header is CELL space. The flip happens once,
// in cell_to_world(), and nowhere else.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace logomanpac {

// ---------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------

enum class Tile : uint8_t {
    WALL,      // '#'  impassable to everything
    DOOR,      // '-'  ghost house door: ghosts pass, the avatar does not
    OPEN,      // ' '  passable, no food
    PELLET,    // '.'  passable, 10 points
    ENERGIZER, // 'o'  passable, 50 points, flips the ghosts
};

// The classic 28x31 board, written the way it reads on screen.
// Row 14 is the tunnel row: it runs off both edges and wraps.
inline const std::vector<std::string>& maze_source() {
    static const std::vector<std::string> rows = {
        "############################",  //  0
        "#............##............#",  //  1
        "#.####.#####.##.#####.####.#",  //  2
        "#o####.#####.##.#####.####o#",  //  3
        "#.####.#####.##.#####.####.#",  //  4
        "#..........................#",  //  5
        "#.####.##.########.##.####.#",  //  6
        "#.####.##.########.##.####.#",  //  7
        "#......##....##....##......#",  //  8
        "######.##### ## #####.######",  //  9
        "     #.##### ## #####.#     ",  // 10
        "     #.##          ##.#     ",  // 11
        "     #.## ###--### ##.#     ",  // 12
        "######.## #      # ##.######",  // 13
        "      .   #      #   .      ",  // 14  <- tunnel row
        "######.## #      # ##.######",  // 15
        "     #.## ######## ##.#     ",  // 16
        "     #.##          ##.#     ",  // 17
        "     #.## ######## ##.#     ",  // 18
        "######.## ######## ##.######",  // 19
        "#............##............#",  // 20
        "#.####.#####.##.#####.####.#",  // 21
        "#.####.#####.##.#####.####.#",  // 22
        "#o..##................##..o#",  // 23  <- avatar starts here
        "###.##.##.########.##.##.###",  // 24
        "###.##.##.########.##.##.###",  // 25
        "#......##....##....##......#",  // 26
        "#.##########.##.##########.#",  // 27
        "#.##########.##.##########.#",  // 28
        "#..........................#",  // 29
        "############################",  // 30
    };
    return rows;
}

// Where things start. Cell centres, integer.
inline constexpr int kAvatarStartCol = 13;
inline constexpr int kAvatarStartRow = 23;
inline constexpr int kHouseDoorCol   = 13;   // the left of the two door cells
inline constexpr int kHouseDoorRow   = 12;
inline constexpr int kHouseExitCol   = 13;   // the cell above the door
inline constexpr int kHouseExitRow   = 11;
inline constexpr int kHouseCentreCol = 13;
inline constexpr int kHouseCentreRow = 14;
inline constexpr int kTunnelRow      = 14;

class Board {
public:
    Board();

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    Tile at(int col, int row) const;

    // Passability. The door is the only tile the two disagree about,
    // which is the whole reason there are two of these.
    bool open_for_avatar(int col, int row) const;
    bool open_for_ghost(int col, int row) const;

    // Column wrap for the tunnel row. Rows other than the tunnel are
    // walled at both ends, so wrapping them is harmless and never
    // reached.
    int wrap_col(int col) const;

    int pellet_count() const { return pellet_count_; }

    // Self-check, run at construction and printed by the AT. Returns an
    // empty vector when the board is sound; otherwise one line per
    // defect. A hand-typed maze is a data file with no compiler, so it
    // gets a verifier instead.
    std::vector<std::string> validate() const;

    // Cell centre -> engine world position. The single place the row
    // flip lives.
    void cell_to_world(float col, float row, float& out_x, float& out_y) const;

private:
    int cols_ = 0;
    int rows_ = 0;
    int pellet_count_ = 0;
    std::vector<std::vector<Tile>> grid_;
};

// ---------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------

enum class Dir : uint8_t { NONE, UP, DOWN, LEFT, RIGHT };

const char* dir_name(Dir d);
void        dir_delta(Dir d, int& dc, int& dr);
Dir         opposite(Dir d);

// A thing that walks the lanes. Position is continuous in cell units;
// `col`/`row` of 13.0 means dead centre of cell (13, ...).
struct Walker {
    float col = 0.0f;
    float row = 0.0f;
    Dir   dir = Dir::NONE;
    Dir   desired = Dir::NONE;
    float speed = 6.0f;      // cells per second
};

// True when the walker is within `eps` of a cell centre on BOTH axes.
bool at_cell_centre(const Walker& w, float eps = 0.06f);

int cell_of(float v);

// Advance one frame.
//
// The turn rule is the classic one and it is the only subtle thing in
// this file: a queued direction commits the moment the walker is
// centred on the axis it is about to leave AND the next cell in that
// direction is open. Until then the walker keeps its current heading.
// Walking into a wall stops the walker exactly on the last centre
// rather than letting it drift into the wall.
//
// `ghost` selects which passability table is used.
void step_walker(Walker& w, const Board& b, float dt, bool ghost);

// The distance a ghost uses to compare candidate turns. Squared
// Euclidean in cell units, which is what the arcade does.
float target_score(int col, int row, int target_col, int target_row);

// Pick a ghost's next heading at a junction: the open direction that
// gets closest to the target, never reversing unless there is no other
// option. Returns Dir::NONE only if the ghost is walled in on all four
// sides, which the board verifier rules out.
Dir choose_ghost_dir(const Board& b, int col, int row, Dir current,
                     int target_col, int target_row);

// Frightened ghosts turn at random. Deterministic given the seed, so a
// run replays.
Dir choose_random_dir(const Board& b, int col, int row, Dir current,
                      uint32_t& rng_state);

uint32_t next_random(uint32_t& state);

}  // namespace logomanpac
