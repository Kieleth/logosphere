// The screen, measured. Nobody looks at this game to find out whether
// its notes are sitting on top of its sheet.
//
// Over every text size the player can ask for, three window shapes,
// and a sheet from eight rows to thirty:
//   1. every rectangle is inside the window
//   2. no two of them overlap
//   3. the notes start below the LAST row of the sheet, with a gap
//   4. the columns are wide enough to read, and the doors deep
//      enough to choose from
//   5. a short question does not leave a hole above the doors
//
// Property 3 is the defect this file was written for: the notes were
// pinned to the middle of the right column, so a sheet that grew past
// the middle had its rows written over.

#undef NDEBUG

#include "screen_layout.h"

#include <iostream>
#include <string>
#include <utility>
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

std::string say(const voyager::Rect& rect) {
    return "(" + std::to_string(rect.x) + "," + std::to_string(rect.y) +
           " " + std::to_string(rect.w) + "x" + std::to_string(rect.h) +
           ")";
}

}  // namespace

int main() {
    // 1280x800 is the window the game opens; the others are a larger
    // display and the narrowest laptop that still meets the width the
    // header states for the largest type (276 * 4 = 1104).
    const std::vector<std::pair<int, int>> windows = {
        {1280, 800}, {1920, 1200}, {1152, 800},
    };

    for (const auto& window : windows) {
        const int screen_w = window.first;
        const int screen_h = window.second;
        for (int scale = voyager::kMinTextScale;
             scale <= voyager::kMaxTextScale; ++scale) {
            for (int rows = 8; rows <= 30; ++rows) {
                const auto layout =
                    voyager::compute_layout(screen_w, screen_h, scale, rows);
                const std::string where =
                    std::to_string(screen_w) + "x" +
                    std::to_string(screen_h) + " at scale " +
                    std::to_string(scale) + " with " +
                    std::to_string(rows) + " sheet rows";

                CHECK(layout.text_scale == scale,
                      where << ": the layout came back at scale "
                            << layout.text_scale);

                const std::vector<std::pair<std::string, voyager::Rect>>
                    pieces = {
                        {"title", layout.title},
                        {"prose", layout.prose},
                        {"prompt", layout.prompt},
                        {"doors", layout.doors},
                        {"field", layout.field},
                        {"sheet", layout.sheet},
                        {"note", layout.note},
                    };

                // 1. inside the window
                for (const auto& piece : pieces) {
                    CHECK(piece.second.within(screen_w, screen_h),
                          where << ": " << piece.first << " at "
                                << say(piece.second)
                                << " hangs outside the window");
                }

                // 2. no two overlap
                for (size_t i = 0; i < pieces.size(); ++i) {
                    for (size_t k = i + 1; k < pieces.size(); ++k) {
                        CHECK(!pieces[i].second.overlaps(pieces[k].second),
                              where << ": " << pieces[i].first << " at "
                                    << say(pieces[i].second) << " covers "
                                    << pieces[k].first << " at "
                                    << say(pieces[k].second));
                    }
                }

                // 2b. the columns' own backgrounds. They sit BEHIND
                // this game's boxes, so they may cover those; what
                // they may not cover is the engine's text field,
                // which the engine renders before them and hit-tests
                // after them. A panel over it is a field that cannot
                // be seen or typed in.
                CHECK(!layout.left.overlaps(layout.field) &&
                          !layout.right.overlaps(layout.field),
                      where << ": a column background at "
                            << say(layout.left) << " / "
                            << say(layout.right) << " covers the text "
                               "field at " << say(layout.field));
                for (const auto& piece :
                     {std::make_pair("title", layout.title),
                      std::make_pair("prose", layout.prose),
                      std::make_pair("prompt", layout.prompt),
                      std::make_pair("doors", layout.doors)}) {
                    CHECK(!piece.second.empty() &&
                              piece.second.within(
                                  layout.left.x + layout.left.w,
                                  layout.left.y + layout.left.h) &&
                              piece.second.x >= layout.left.x &&
                              piece.second.y >= layout.left.y,
                          where << ": " << piece.first << " at "
                                << say(piece.second)
                                << " is outside its column "
                                << say(layout.left));
                }
                for (const auto& piece :
                     {std::make_pair("sheet", layout.sheet),
                      std::make_pair("note", layout.note)}) {
                    CHECK(!piece.second.empty() &&
                              piece.second.within(
                                  layout.right.x + layout.right.w,
                                  layout.right.y + layout.right.h) &&
                              piece.second.x >= layout.right.x &&
                              piece.second.y >= layout.right.y,
                          where << ": " << piece.first << " at "
                                << say(piece.second)
                                << " is outside its column "
                                << say(layout.right));
                }

                // 3. the notes start below the last row of the sheet
                const int last_row = layout.sheet.y + layout.sheet.h;
                CHECK(layout.note.y >= last_row + layout.line / 2,
                      where << ": the notes start at " << layout.note.y
                            << " and the sheet's last row ends at "
                            << last_row << "; that is less than half a "
                               "line of air");
                CHECK(layout.sheet.h ==
                          layout.sheet_rows_shown * layout.line,
                      where << ": the sheet box is " << layout.sheet.h
                            << " tall for " << layout.sheet_rows_shown
                            << " rows of " << layout.line);
                CHECK(layout.sheet_rows_shown >= 1 &&
                          layout.sheet_rows_shown <= rows,
                      where << ": the sheet shows "
                            << layout.sheet_rows_shown << " of " << rows
                            << " rows");

                // 4. wide enough to read, deep enough to choose from
                CHECK(layout.prose_columns >= 20,
                      where << ": the story column holds "
                            << layout.prose_columns << " characters");
                CHECK(layout.note_columns >= 20,
                      where << ": the note column holds "
                            << layout.note_columns << " characters");
                CHECK(layout.doors.h >= 3 * layout.line,
                      where << ": the doors get " << layout.doors.h
                            << " pixels, under three lines of "
                            << layout.line);
                CHECK(layout.note_lines >= 3,
                      where << ": the notes get " << layout.note_lines
                            << " lines");
                CHECK(layout.prose_lines >= 3 && layout.prompt_lines >= 2,
                      where << ": the story gets " << layout.prose_lines
                            << " lines and the question "
                            << layout.prompt_lines);

                // 5. a short question pulls the doors up behind it and
                // never over anything else
                for (int shown = 0; shown <= layout.prompt_lines; ++shown) {
                    const auto doors = layout.doors_after(shown);
                    const auto asked = layout.prompt_shown_rect(shown);
                    CHECK(doors.within(screen_w, screen_h),
                          where << ": the doors under a " << shown
                                << "-line question, " << say(doors)
                                << ", hang outside the window");
                    CHECK(!doors.overlaps(asked),
                          where << ": the doors under a " << shown
                                << "-line question cover the question");
                    CHECK(!doors.overlaps(layout.field) &&
                              !doors.overlaps(layout.prose) &&
                              !doors.overlaps(layout.title) &&
                              !doors.overlaps(layout.sheet) &&
                              !doors.overlaps(layout.note),
                          where << ": the doors under a " << shown
                                << "-line question, " << say(doors)
                                << ", cover something else");
                    CHECK(doors.h >= layout.doors.h,
                          where << ": a " << shown << "-line question "
                                << "left the doors " << doors.h
                                << " pixels, less than the "
                                << layout.doors.h << " reserved for a "
                                   "full-length one");
                    // The hole this closes: with the reserve unspent,
                    // the doors used to start a whole reserve down.
                    if (shown < layout.prompt_lines) {
                        CHECK(doors.y < layout.doors.y,
                              where << ": a " << shown
                                    << "-line question left the doors "
                                       "where a full one would put them");
                    }
                }
            }
        }
    }

    // A window nobody should open, which must still not produce a
    // rectangle outside it or a negative size.
    for (const auto& window : std::vector<std::pair<int, int>>{
             {320, 240}, {640, 200}, {200, 900}, {1, 1}}) {
        for (int scale = voyager::kMinTextScale;
             scale <= voyager::kMaxTextScale; ++scale) {
            const auto layout = voyager::compute_layout(
                window.first, window.second, scale, 12);
            for (const auto& rect :
                 {layout.title, layout.prose, layout.prompt, layout.doors,
                  layout.field, layout.sheet, layout.note, layout.left,
                  layout.right}) {
                CHECK(rect.w >= 0 && rect.h >= 0,
                      "a " << window.first << "x" << window.second
                           << " window at scale " << scale
                           << " produced " << say(rect));
                CHECK(rect.within(window.first, window.second),
                      "a " << window.first << "x" << window.second
                           << " window at scale " << scale
                           << " put a rectangle at " << say(rect)
                           << " outside it");
            }
            // The one rule that has to hold even here: the column
            // backgrounds are drawn after the engine's text field and
            // hit-tested before it, so either of them over it is a
            // field nobody can see or click.
            CHECK(!layout.left.overlaps(layout.field) &&
                      !layout.right.overlaps(layout.field),
                  "a " << window.first << "x" << window.second
                       << " window at scale " << scale
                       << " put a column background over the text "
                          "field at " << say(layout.field));
        }
    }

    // The scale is clamped, not trusted: a key held down must not walk
    // the type off either end.
    for (int asked : {-5, 0, 5, 99}) {
        const auto layout = voyager::compute_layout(1280, 800, asked, 12);
        CHECK(layout.text_scale >= voyager::kMinTextScale &&
                  layout.text_scale <= voyager::kMaxTextScale,
              "a request for scale " << asked << " gave scale "
                                     << layout.text_scale);
    }

    // Bigger type, bigger everything: the one property a reader would
    // check by squinting.
    {
        int previous_line = 0;
        for (int scale = voyager::kMinTextScale;
             scale <= voyager::kMaxTextScale; ++scale) {
            const auto layout = voyager::compute_layout(1280, 800, scale, 12);
            CHECK(layout.line > previous_line,
                  "scale " << scale << " draws its lines "
                           << layout.line << " apart, no more than scale "
                           << (scale - 1) << " did");
            previous_line = layout.line;
        }
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
