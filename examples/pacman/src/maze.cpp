#include "maze.h"

#include <algorithm>
#include <deque>
#include <sstream>

namespace pacman {

// ---------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------

Board::Board() {
    const auto& src = maze_source();
    rows_ = static_cast<int>(src.size());
    cols_ = rows_ > 0 ? static_cast<int>(src[0].size()) : 0;
    grid_.resize(rows_);
    for (int r = 0; r < rows_; ++r) {
        grid_[r].resize(cols_, Tile::WALL);
        for (int c = 0; c < cols_ && c < static_cast<int>(src[r].size()); ++c) {
            switch (src[r][c]) {
                case '#': grid_[r][c] = Tile::WALL;      break;
                case '-': grid_[r][c] = Tile::DOOR;      break;
                case '.': grid_[r][c] = Tile::PELLET;    ++pellet_count_; break;
                case 'o': grid_[r][c] = Tile::ENERGIZER; ++pellet_count_; break;
                default:  grid_[r][c] = Tile::OPEN;      break;
            }
        }
    }
}

Tile Board::at(int col, int row) const {
    if (row < 0 || row >= rows_) return Tile::WALL;
    col = wrap_col(col);
    return grid_[row][col];
}

bool Board::open_for_avatar(int col, int row) const {
    const Tile t = at(col, row);
    return t != Tile::WALL && t != Tile::DOOR;
}

bool Board::open_for_ghost(int col, int row) const {
    return at(col, row) != Tile::WALL;
}

int Board::wrap_col(int col) const {
    if (cols_ <= 0) return 0;
    col %= cols_;
    if (col < 0) col += cols_;
    return col;
}

void Board::cell_to_world(float col, float row, float& out_x, float& out_y) const {
    out_x = col - (cols_ - 1) * 0.5f;
    out_y = (rows_ - 1) * 0.5f - row;
}

std::vector<std::string> Board::validate() const {
    std::vector<std::string> problems;
    const auto& src = maze_source();

    // 1. Every row the same width. A hand-typed maze gets one character
    //    wrong exactly once, and it is always this.
    for (int r = 0; r < rows_; ++r) {
        if (static_cast<int>(src[r].size()) != cols_) {
            std::ostringstream os;
            os << "row " << r << " is " << src[r].size()
               << " chars, expected " << cols_;
            problems.push_back(os.str());
        }
    }

    // 2. The avatar's start must be standable.
    if (!open_for_avatar(kAvatarStartCol, kAvatarStartRow)) {
        problems.push_back("avatar start cell is not open");
    }

    // 3. Every pellet must be reachable from the start. Flood fill using
    //    the avatar's own passability, including the tunnel wrap, so
    //    this checks the graph the player actually walks.
    std::vector<std::vector<char>> seen(rows_, std::vector<char>(cols_, 0));
    std::deque<std::pair<int, int>> q;
    q.push_back({kAvatarStartCol, kAvatarStartRow});
    seen[kAvatarStartRow][kAvatarStartCol] = 1;
    while (!q.empty()) {
        auto [c, r] = q.front();
        q.pop_front();
        const int dc[4] = {0, 0, -1, 1};
        const int dr[4] = {-1, 1, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const int nr = r + dr[i];
            if (nr < 0 || nr >= rows_) continue;
            const int nc = wrap_col(c + dc[i]);
            if (seen[nr][nc]) continue;
            if (!open_for_avatar(nc, nr)) continue;
            seen[nr][nc] = 1;
            q.push_back({nc, nr});
        }
    }
    int unreachable = 0;
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            const Tile t = grid_[r][c];
            if ((t == Tile::PELLET || t == Tile::ENERGIZER) && !seen[r][c])
                ++unreachable;
        }
    if (unreachable > 0) {
        std::ostringstream os;
        os << unreachable << " pellet(s) unreachable from the avatar start";
        problems.push_back(os.str());
    }

    // 4. The ghosts must be able to get out of the house, and back in.
    if (at(kHouseDoorCol, kHouseDoorRow) != Tile::DOOR) {
        problems.push_back("house door cell is not a DOOR tile");
    }
    if (!open_for_ghost(kHouseExitCol, kHouseExitRow)) {
        problems.push_back("cell above the house door is not ghost-passable");
    }
    if (!open_for_ghost(kHouseCentreCol, kHouseCentreRow)) {
        problems.push_back("house centre is not ghost-passable");
    }

    // 5. The tunnel must actually wrap: both ends of the tunnel row open.
    if (!open_for_avatar(0, kTunnelRow) || !open_for_avatar(cols_ - 1, kTunnelRow)) {
        problems.push_back("tunnel row is walled at one or both ends");
    }

    return problems;
}

// ---------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------

const char* dir_name(Dir d) {
    switch (d) {
        case Dir::UP:    return "UP";
        case Dir::DOWN:  return "DOWN";
        case Dir::LEFT:  return "LEFT";
        case Dir::RIGHT: return "RIGHT";
        default:         return "NONE";
    }
}

void dir_delta(Dir d, int& dc, int& dr) {
    dc = 0; dr = 0;
    switch (d) {
        case Dir::UP:    dr = -1; break;
        case Dir::DOWN:  dr =  1; break;
        case Dir::LEFT:  dc = -1; break;
        case Dir::RIGHT: dc =  1; break;
        default: break;
    }
}

Dir opposite(Dir d) {
    switch (d) {
        case Dir::UP:    return Dir::DOWN;
        case Dir::DOWN:  return Dir::UP;
        case Dir::LEFT:  return Dir::RIGHT;
        case Dir::RIGHT: return Dir::LEFT;
        default:         return Dir::NONE;
    }
}

int cell_of(float v) {
    return static_cast<int>(std::lround(v));
}

bool at_cell_centre(const Walker& w, float eps) {
    return std::fabs(w.col - std::lround(w.col)) < eps &&
           std::fabs(w.row - std::lround(w.row)) < eps;
}

namespace {

bool open_for(const Board& b, int col, int row, bool ghost) {
    return ghost ? b.open_for_ghost(col, row) : b.open_for_avatar(col, row);
}

}  // namespace

void step_walker(Walker& w, const Board& b, float dt, bool ghost) {
    if (dt <= 0.0f) return;

    const int cc = cell_of(w.col);
    const int cr = cell_of(w.row);

    // Commit a queued turn. Two cases, and the second one is what makes
    // the controls feel right: a turn onto the axis you are already
    // travelling (a reversal) commits instantly, anywhere in the lane.
    if (w.desired != Dir::NONE && w.desired != w.dir) {
        if (w.desired == opposite(w.dir)) {
            w.dir = w.desired;
            w.desired = Dir::NONE;
        } else {
            int dc, dr;
            dir_delta(w.desired, dc, dr);
            const bool centred =
                (dc != 0) ? std::fabs(w.row - cr) < 0.18f
                          : std::fabs(w.col - cc) < 0.18f;
            if (centred && open_for(b, cc + dc, cr + dr, ghost)) {
                // Snap onto the lane we are turning into, so the turn
                // does not leave a permanent half-cell offset that
                // slowly walks the actor into a wall.
                if (dc != 0) w.row = static_cast<float>(cr);
                else         w.col = static_cast<float>(cc);
                w.dir = w.desired;
                w.desired = Dir::NONE;
            }
        }
    }

    if (w.dir == Dir::NONE) return;

    int dc, dr;
    dir_delta(w.dir, dc, dr);
    float move = w.speed * dt;

    // Substep so a fast actor cannot skip over a wall in one frame.
    const float kMaxStep = 0.25f;
    while (move > 0.0f) {
        const float step = std::min(move, kMaxStep);
        move -= step;

        const int here_c = cell_of(w.col);
        const int here_r = cell_of(w.row);
        const bool next_open = open_for(b, here_c + dc, here_r + dr, ghost);

        if (!next_open) {
            // How far can we go before the centre of this cell? Past it
            // is the wall.
            const float along = (dc != 0) ? (w.col - here_c) * dc
                                          : (w.row - here_r) * dr;
            const float room = -along;   // 0 when exactly centred
            if (room <= 0.0f) {
                w.col = static_cast<float>(here_c);
                w.row = static_cast<float>(here_r);
                return;   // stopped against the wall
            }
            const float allowed = std::min(step, room);
            w.col += dc * allowed;
            w.row += dr * allowed;
            if (allowed < step) {
                w.col = static_cast<float>(here_c);
                w.row = static_cast<float>(here_r);
                return;
            }
        } else {
            w.col += dc * step;
            w.row += dr * step;
        }
    }

    // Tunnel wrap. Written against the board width so a different board
    // does not need this touched.
    const float span = static_cast<float>(b.cols());
    if (w.col < -0.5f)        w.col += span;
    else if (w.col > span - 0.5f) w.col -= span;
}

float target_score(int col, int row, int target_col, int target_row) {
    const float dx = static_cast<float>(col - target_col);
    const float dy = static_cast<float>(row - target_row);
    return dx * dx + dy * dy;
}

Dir choose_ghost_dir(const Board& b, int col, int row, Dir current,
                     int target_col, int target_row) {
    // Arcade tie-break order: UP, LEFT, DOWN, RIGHT.
    const Dir order[4] = {Dir::UP, Dir::LEFT, Dir::DOWN, Dir::RIGHT};
    const Dir back = opposite(current);

    Dir best = Dir::NONE;
    float best_score = 0.0f;
    for (Dir d : order) {
        if (d == back) continue;
        int dc, dr;
        dir_delta(d, dc, dr);
        if (!b.open_for_ghost(col + dc, row + dr)) continue;
        const float s = target_score(b.wrap_col(col + dc), row + dr,
                                     target_col, target_row);
        if (best == Dir::NONE || s < best_score) { best = d; best_score = s; }
    }
    if (best != Dir::NONE) return best;

    // Dead end: reversing is the only move, and the arcade allows it.
    int dc, dr;
    dir_delta(back, dc, dr);
    if (back != Dir::NONE && b.open_for_ghost(col + dc, row + dr)) return back;
    return current;
}

uint32_t next_random(uint32_t& state) {
    // xorshift32. Small, deterministic, and good enough to pick one of
    // four doors.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

Dir choose_random_dir(const Board& b, int col, int row, Dir current,
                      uint32_t& rng_state) {
    const Dir order[4] = {Dir::UP, Dir::LEFT, Dir::DOWN, Dir::RIGHT};
    const Dir back = opposite(current);
    Dir options[4];
    int n = 0;
    for (Dir d : order) {
        if (d == back) continue;
        int dc, dr;
        dir_delta(d, dc, dr);
        if (b.open_for_ghost(col + dc, row + dr)) options[n++] = d;
    }
    if (n == 0) return back;
    return options[next_random(rng_state) % static_cast<uint32_t>(n)];
}

}  // namespace pacman
