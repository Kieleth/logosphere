// Well rules, headless, no engine, no window.
//
// Every case here is something that was WRONG at some point today and
// was found by measuring rather than by watching the screen. The
// vertical-I case in particular passed visual inspection for an hour:
// the piece looked fine, it just silently refused to stand up.
//
// Each check prints the value it measured before it judges it, and
// each one has a control: a case that must come out the other way, so
// a check that can no longer fail says so.

#include <cstdio>
#include <string>
#include <vector>

#include "tetromino.h"
#include "well.h"

using namespace tetris;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok) failures++;
}

// A stand-in for the entity ids the live game stores. The well only
// ever asks "is this slot empty", so any non-zero id will do.
constexpr kg::EntityID kFake = 42;

void fill_row_except(Well& w, int row, int hole) {
    for (int c = 0; c < kCols; c++)
        if (c != hole) w.put(c, row, kFake + static_cast<kg::EntityID>(c));
}

// ---------------------------------------------------------------------
// 1. Every orientation of every piece must be placeable somewhere, and
//    must be able to reach column 0 and column kCols-1. A piece that
//    cannot reach a wall makes that column unfillable and the game
//    unwinnable in a way no crash reports.
// ---------------------------------------------------------------------
void test_reach() {
    const Well empty;
    int unplaceable = 0, no_left = 0, no_right = 0;

    for (int k = 0; k < kKindCount; k++) {
        for (int r = 0; r < 4; r++) {
            bool any = false, left = false, right = false;
            for (int x = -3; x < kCols; x++) {
                for (int y = 0; y < kRows; y++) {
                    if (!empty.fits(static_cast<Kind>(k), r, x, y)) continue;
                    any = true;
                    int lo = 99, hi = -99;
                    for (const Cell& c : kShapes[k][r]) {
                        if (x + c.dx < lo) lo = x + c.dx;
                        if (x + c.dx > hi) hi = x + c.dx;
                    }
                    if (lo == 0) left = true;
                    if (hi == kCols - 1) right = true;
                }
            }
            if (!any)   { unplaceable++; std::printf("    [measure] %s r%d placeable NOWHERE\n", kind_name(static_cast<Kind>(k)), r); }
            if (!left)  { no_left++;     std::printf("    [measure] %s r%d cannot reach column 0\n", kind_name(static_cast<Kind>(k)), r); }
            if (!right) { no_right++;    std::printf("    [measure] %s r%d cannot reach column %d\n", kind_name(static_cast<Kind>(k)), r, kCols - 1); }
        }
    }
    std::printf("    [measure] unplaceable=%d no_left=%d no_right=%d (of %d orientations)\n",
                unplaceable, no_left, no_right, kKindCount * 4);
    check(unplaceable == 0, "every orientation is placeable in an empty well");
    check(no_left == 0,     "every orientation can reach the left wall");
    check(no_right == 0,    "every orientation can reach the right wall");
}

// ---------------------------------------------------------------------
// 2. THE BUG. The I piece rotated vertical occupies four rows; its box
//    reaches three cells above its floor. At the spawn row that top
//    cell is outside the well, so fits() said no and the piece could
//    not be stood up until gravity had already moved it. The fix is
//    the downward entries in kKicks, so the assertion is on the KICK
//    LIST doing its job, not on fits() having been loosened.
// ---------------------------------------------------------------------
bool rotate_with_kicks(const Well& w, Kind k, int from_rot, int dir,
                       int px, int py, int& out_rot, int& out_px, int& out_py) {
    const int next = (from_rot + dir + 4) & 3;
    for (const Cell& kick : kKicks) {
        if (!w.fits(k, next, px + kick.dx, py + kick.dy)) continue;
        out_rot = next; out_px = px + kick.dx; out_py = py + kick.dy;
        return true;
    }
    return false;
}

void test_vertical_i_at_spawn() {
    const Well empty;
    const int spawn_py = kRows - 3;
    const int spawn_px = (kCols - 4) / 2;

    const bool naive = empty.fits(Kind::I, 1, spawn_px, spawn_py);
    std::printf("    [measure] I r1 fits at spawn WITHOUT a kick: %s\n",
                naive ? "yes" : "no");
    // Control: if this ever becomes true the spawn row moved, and the
    // rest of this test stops proving what it says it proves.
    check(!naive, "CONTROL: the naive rotation really is blocked at spawn "
                  "(otherwise the kick below is untested)");

    int r = 0, x = spawn_px, y = spawn_py;
    const bool kicked = rotate_with_kicks(empty, Kind::I, 0, 1, x, y, r, x, y);
    std::printf("    [measure] with kicks: %s -> rot=%d px=%d py=%d\n",
                kicked ? "rotated" : "REFUSED", r, x, y);
    check(kicked, "an I piece can be stood up on the frame it spawns");
    check(kicked && r == 1, "and it actually ends in the vertical state");

    // All four rotations of every piece must be reachable from spawn.
    int refused = 0;
    for (int k = 0; k < kKindCount; k++) {
        int rr = 0, xx = spawn_px, yy = spawn_py;
        for (int step = 0; step < 4; step++) {
            int nr, nx, ny;
            if (!rotate_with_kicks(empty, static_cast<Kind>(k), rr, 1,
                                   xx, yy, nr, nx, ny)) {
                refused++;
                std::printf("    [measure] %s stuck at rot %d\n",
                            kind_name(static_cast<Kind>(k)), rr);
                break;
            }
            rr = nr; xx = nx; yy = ny;
        }
    }
    std::printf("    [measure] pieces that cannot complete a full turn from spawn: %d\n",
                refused);
    check(refused == 0, "every piece can turn full circle from its spawn");
}

// ---------------------------------------------------------------------
// 3. Line detection and collapse.
// ---------------------------------------------------------------------
void test_clear_and_collapse() {
    Well w;
    fill_row_except(w, 0, 3);
    std::printf("    [measure] row 0 with one hole -> full_rows=%zu\n",
                w.full_rows().size());
    check(w.full_rows().empty(), "a row with one gap is NOT a line");

    w.put(3, 0, kFake);
    std::printf("    [measure] row 0 with the hole plugged -> full_rows=%zu\n",
                w.full_rows().size());
    check(w.full_rows().size() == 1, "a complete row IS a line");

    // A marker two rows up must land one row lower after the clear.
    w.put(5, 2, 777);
    w.collapse(w.full_rows());
    std::printf("    [measure] after collapse: row0[5]=%u row1[5]=%u row2[5]=%u\n",
                w.at(5, 0), w.at(5, 1), w.at(5, 2));
    check(w.at(5, 1) == 777, "everything above a cleared line drops exactly one row");
    check(w.at(5, 2) == kg::INVALID_ENTITY, "and does not stay where it was");
    check(w.full_rows().empty(), "the cleared row is empty afterwards");

    // Four at once, the deepest and shallowest both handled.
    Well t;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < kCols; c++) t.put(c, r, kFake);
    t.put(2, 7, 888);
    const auto full = t.full_rows();
    std::printf("    [measure] tetris -> full_rows=%zu\n", full.size());
    check(full.size() == 4, "four complete rows are four lines");
    t.collapse(full);
    std::printf("    [measure] marker was at row 7, now at row %d\n",
                t.at(2, 3) == 888 ? 3 : -1);
    check(t.at(2, 3) == 888, "a tetris drops the stack by four");
}

// ---------------------------------------------------------------------
// 4. Bounds. The negative, the boundary and one step past it.
// ---------------------------------------------------------------------
// The first draft of this function asserted `!fits(T, 0, 3, -1)` and
// `!fits(O, 0, 3, 0)` over a well with (4,0) taken, and both failed.
// The EXPECTATION was wrong, not the code: px/py address the piece's
// bounding BOX, and T r0 and O both have an empty bottom row, so
// py = -1 still puts every occupied cell at row >= 0. Rewritten to
// pin the real invariant — no occupied cell outside the well — with
// the offsets computed rather than assumed.
void test_bounds() {
    const Well w;

    // Lowest and leftmost occupied cell of an orientation, so the
    // boundary case is derived from the shape instead of guessed.
    auto extent = [](Kind k, int rot, int& min_dx, int& max_dx, int& min_dy) {
        min_dx = 99; max_dx = -99; min_dy = 99;
        for (const Cell& c : kShapes[static_cast<int>(k)][rot]) {
            if (c.dx < min_dx) min_dx = c.dx;
            if (c.dx > max_dx) max_dx = c.dx;
            if (c.dy < min_dy) min_dy = c.dy;
        }
    };

    int bad_left = 0, bad_right = 0, bad_floor = 0;
    for (int k = 0; k < kKindCount; k++) {
        for (int rot = 0; rot < 4; rot++) {
            int lo, hi, dn;
            extent(static_cast<Kind>(k), rot, lo, hi, dn);
            const Kind kind = static_cast<Kind>(k);
            // Flush against each wall: legal. One further: not.
            if (!w.fits(kind, rot, -lo, 5))                bad_left++;
            if ( w.fits(kind, rot, -lo - 1, 5))            bad_left++;
            if (!w.fits(kind, rot, kCols - 1 - hi, 5))     bad_right++;
            if ( w.fits(kind, rot, kCols - hi, 5))         bad_right++;
            if (!w.fits(kind, rot, 3, -dn))                bad_floor++;
            if ( w.fits(kind, rot, 3, -dn - 1))            bad_floor++;
        }
    }
    std::printf("    [measure] boundary violations left=%d right=%d floor=%d\n",
                bad_left, bad_right, bad_floor);
    check(bad_left == 0,  "every orientation sits flush against the left wall and no further");
    check(bad_right == 0, "every orientation sits flush against the right wall and no further");
    check(bad_floor == 0, "every orientation rests on the floor and no lower");

    check(!w.fits(Kind::I, 0, 3, kRows - 2), "above the ceiling does not fit");

    // Overlap, aimed at the piece's LOWEST cell so that one row up
    // clears it. Aiming at kShapes[..][0] instead put the obstacle two
    // rows up, where py+1 still overlapped — the assertion was right,
    // the fixture was not.
    Well occupied;
    int olo, ohi, odn;
    extent(Kind::O, 0, olo, ohi, odn);
    occupied.put(3 + olo, odn, kFake);
    std::printf("    [measure] obstacle at (%d,%d); O at py=0 fits=%d, py=1 fits=%d\n",
                3 + olo, odn, occupied.fits(Kind::O, 0, 3, 0) ? 1 : 0,
                occupied.fits(Kind::O, 0, 3, 1) ? 1 : 0);
    check(!occupied.fits(Kind::O, 0, 3, 0), "a piece will not overlap a locked cell");
    check(occupied.fits(Kind::O, 0, 3, 1),  "but sits happily on top of it");
}

// ---------------------------------------------------------------------
// 5. Scoring and speed curve.
// ---------------------------------------------------------------------
void test_scoring() {
    std::printf("    [measure] scores L1: %d %d %d %d\n", line_score(1, 1),
                line_score(2, 1), line_score(3, 1), line_score(4, 1));
    check(line_score(4, 1) > line_score(1, 1) * 4,
          "a tetris beats four singles");
    check(line_score(1, 3) == line_score(1, 1) * 3, "level multiplies the score");
    check(line_score(0, 5) == 0, "clearing nothing scores nothing");

    std::printf("    [measure] level at 0/9/10/25 lines: %d %d %d %d\n",
                level_for(0), level_for(9), level_for(10), level_for(25));
    check(level_for(9) == 1 && level_for(10) == 2, "ten lines is one level");

    std::printf("    [measure] fall interval L1=%.3f L5=%.3f L20=%.3f\n",
                fall_interval_for(1), fall_interval_for(5), fall_interval_for(20));
    check(fall_interval_for(5) < fall_interval_for(1), "higher levels fall faster");
    check(fall_interval_for(100) > 0.0f, "the interval never reaches zero");
}

}  // namespace

int main() {
    std::printf("=== tetris well ===\n");
    test_reach();
    test_vertical_i_at_spawn();
    test_clear_and_collapse();
    test_bounds();
    test_scoring();
    std::printf("=== %s (%d failure%s) ===\n",
                failures == 0 ? "PASS" : "FAIL", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
