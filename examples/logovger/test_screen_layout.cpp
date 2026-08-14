// The layout, asserted.
//
// Two real failures motivated this file, both of which cost a build
// and a play session to find by eye:
//
//   1. The window was widened to 1820 points on a 1728-point display.
//      Nothing appeared. Not a small window, not a clipped window:
//      no window. A screenshot could not catch it because there was
//      nothing to screenshot.
//   2. The scrolling lists must be ROOT widgets to receive their own
//      scroll events, so they are placed in absolute screen
//      coordinates, while the panels behind them lay out their labels
//      in panel-local ones. One list was given a local y and an
//      absolute x, which puts it off its own panel.
//
// Both are arithmetic, so both are testable without a GPU.

#include "src/screen_layout.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace logovger;

static int failures = 0;

static void check(bool ok, const std::string& what,
                  const std::string& detail = "") {
    if (ok) {
        std::printf("  ok    %s\n", what.c_str());
        return;
    }
    ++failures;
    std::printf("  FAIL  %s%s%s\n", what.c_str(),
                detail.empty() ? "" : "  --  ", detail.c_str());
}

static std::string say(const Rect& r) {
    return "x=" + std::to_string(r.x) + " y=" + std::to_string(r.y) +
           " w=" + std::to_string(r.w) + " h=" + std::to_string(r.h);
}

// The window must fit the display it is opened on, measured in the
// points macOS actually lays windows out in, not in retina pixels.
static void test_window_fits_a_laptop_display() {
    std::printf("the window fits the screen\n");
    const int kDisplayW = 1728;   // 16-inch MacBook Pro, points
    const int kDisplayH = 1117;
    const int kMenuBar = 25;

    check(kScreenW <= kDisplayW,
          "window width fits the display",
          std::to_string(kScreenW) + " > " + std::to_string(kDisplayW));
    check(kScreenH <= kDisplayH - kMenuBar,
          "window height fits below the menu bar",
          std::to_string(kScreenH) + " > " +
              std::to_string(kDisplayH - kMenuBar));
}

// Every panel on the screen, nothing off the edge, nothing overlapping
// anything else.
static void test_panels_tile_the_screen() {
    std::printf("the panels tile the screen\n");
    const auto L = compute_layout(kScreenW, kScreenH);
    const Rect screen{0, 0, kScreenW, kScreenH};

    const std::vector<std::pair<std::string, Rect>> panels = {
        {"left", L.left}, {"dossier", L.dossier},
        {"right", L.right}, {"bottom", L.bottom}};

    for (const auto& p : panels) {
        check(p.second.positive(), p.first + " has a positive size",
              say(p.second));
        check(screen.contains(p.second), p.first + " is on screen",
              say(p.second));
    }
    for (size_t i = 0; i < panels.size(); ++i) {
        for (size_t j = i + 1; j < panels.size(); ++j) {
            check(!panels[i].second.overlaps(panels[j].second),
                  panels[i].first + " does not overlap " + panels[j].first,
                  say(panels[i].second) + " vs " + say(panels[j].second));
        }
    }
}

// The root-widget lists are placed in absolute coordinates. Each one
// must land inside the panel it visually belongs to.
static void test_root_lists_sit_inside_their_panels() {
    std::printf("the scrolling lists sit on their panels\n");
    const auto L = compute_layout(kScreenW, kScreenH);

    check(L.choices.positive(), "career list has a positive size",
          say(L.choices));
    check(L.left.contains(L.choices), "career list is inside the left panel",
          say(L.choices) + " not inside " + say(L.left));

    check(L.skills.positive(), "skill list has a positive size",
          say(L.skills));
    check(L.right.contains(L.skills), "skill list is inside the sheet",
          say(L.skills) + " not inside " + say(L.right));

    check(L.biopic.positive(), "the biopic has a positive size",
          say(L.biopic));
    check(L.dossier.contains(L.biopic), "the biopic is inside the file",
          say(L.biopic) + " not inside " + say(L.dossier));
    check(!L.biopic.overlaps(L.record),
          "the biopic does not overlap the service record",
          say(L.biopic) + " vs " + say(L.record));

    check(L.record.positive(), "service record has a positive size",
          say(L.record));
    check(L.dossier.contains(L.record), "service record is inside the file",
          say(L.record) + " not inside " + say(L.dossier));
}

// A list that shows two rows is not a list. These hold a life's worth
// of skills and a life's worth of record without the player scrolling
// for every single one.
static void test_the_lists_are_worth_scrolling() {
    std::printf("the lists hold enough to be useful\n");
    const auto L = compute_layout(kScreenW, kScreenH);
    const int kRowH = 22;   // CareerList::kRowH

    const int skill_rows = (L.skills.h - 2 * kPad) / kRowH;
    const int record_rows = (L.record.h - 2 * kPad) / kRowH;
    // A seven-term life gains roughly one skill a term plus the ones
    // service tables hand out, so ten visible is a working sheet.
    check(skill_rows >= 10, "at least ten skills visible without scrolling",
          std::to_string(skill_rows) + " rows");
    check(record_rows >= 8, "at least eight record lines visible",
          std::to_string(record_rows) + " rows");
}

// The file is written in sentences, and a Label draws one clipped
// line, so the wrap width has to be a real column count.
static void test_prose_has_room_to_wrap() {
    std::printf("prose has columns to wrap into\n");
    const auto L = compute_layout(kScreenW, kScreenH);
    check(L.narration_columns >= 100,
          "narration wraps at a readable width",
          std::to_string(L.narration_columns) + " columns");
    check(L.dossier_columns >= 40, "the file wraps at a readable width",
          std::to_string(L.dossier_columns) + " columns");
}

// The layout is a function, so it should not fall apart on a screen
// that is not the one it was tuned for.
static void test_it_survives_other_screen_sizes() {
    std::printf("other screen sizes do not break it\n");
    const std::vector<std::pair<int, int>> sizes = {
        {1280, 800}, {1440, 900}, {1600, 950}, {1920, 1080}, {2560, 1440}};
    for (const auto& s : sizes) {
        const auto L = compute_layout(s.first, s.second);
        const Rect screen{0, 0, s.first, s.second};
        const std::string at =
            std::to_string(s.first) + "x" + std::to_string(s.second);
        const bool all_ok =
            L.left.positive() && L.dossier.positive() &&
            L.right.positive() && L.bottom.positive() &&
            screen.contains(L.left) && screen.contains(L.dossier) &&
            screen.contains(L.right) && screen.contains(L.bottom) &&
            L.left.contains(L.choices) && L.right.contains(L.skills) &&
            L.dossier.contains(L.record) && L.dossier.contains(L.biopic) &&
            !L.biopic.overlaps(L.record);
        check(all_ok, "layout holds at " + at,
              "left " + say(L.left) + ", record " + say(L.record));
    }
}

int main() {
    std::printf("\n=== logovger screen layout ===\n\n");
    test_window_fits_a_laptop_display();
    test_panels_tile_the_screen();
    test_root_lists_sit_inside_their_panels();
    test_the_lists_are_worth_scrolling();
    test_prose_has_room_to_wrap();
    test_it_survives_other_screen_sizes();
    std::printf("\n%s\n\n", failures == 0
                                ? "all layout checks passed"
                                : (std::to_string(failures) +
                                   " layout checks FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
