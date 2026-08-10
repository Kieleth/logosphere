// A list you can actually use: scrollable, and it tells you what you
// are signing up for before you sign.
//
// The engine's ListMenu has neither scrolling nor clipping (containers
// render children without a clip rect), so 24 careers spill out of any
// panel that holds them. This is a game-layer widget instead, which is
// the established pattern here (see logotron's SpeedDashboard).
//
// Two clicks, two meanings, which is the whole point:
//   hover / single click  ->  LOOK. What is this career, what does it
//                             ask of you, what will it teach you.
//   double click          ->  COMMIT. Now you are in it.
//
// A rulebook you can interrogate should let you read the entry before
// you live it.

#ifndef LOGOVGER_CAREER_LIST_H
#define LOGOVGER_CAREER_LIST_H

#include "ui/widgets.h"

#include <functional>
#include <string>
#include <vector>

namespace logovger {

class CareerList : public ui::Panel {
public:
    struct Row {
        std::string key;      // what the session expects as an answer
        std::string label;    // "Scout"
        std::string detail;   // "qualify on intelligence 6+, ..."
    };

    explicit CareerList(const std::string& id) : ui::Panel(id) {}

    void set_rows(std::vector<Row> rows);

    // Where the dimmer second column starts. Careers need room for a
    // name; a skill level or a record entry does not.
    void set_detail_column(int x) { detail_x_ = x; }

    // What a selected row offers, if anything. A list you only read
    // from should not promise a commit that will not happen.
    void set_commit_hint(const std::string& hint) { commit_hint_ = hint; }

    // Keep the newest rows in view: a record is read at its end.
    void scroll_to_end();
    void clear() { rows_.clear(); scroll_ = 0; selected_ = -1; hovered_ = -1; }

    // Looking is free; choosing is deliberate.
    std::function<void(const std::string& key)> on_inspect;
    std::function<void(const std::string& key)> on_choose;

    void render(IDrawSurface* renderer) override;
    bool on_mouse_move(ui::MouseEvent& e) override;
    bool on_mouse_down(ui::MouseEvent& e) override;
    bool on_mouse_scroll(int delta) override;
    bool on_mouse_leave(ui::MouseEvent& e) override;

private:
    int row_at(int local_y) const;
    int visible_rows() const;
    void clamp_scroll();

    std::vector<Row> rows_;
    int scroll_ = 0;        // first visible row
    int selected_ = -1;
    int hovered_ = -1;

    int         detail_x_ = 150;
    std::string commit_hint_ = "click again to join";

    static constexpr int kRowH = 22;
    static constexpr int kPad = 6;
};

}  // namespace logovger

#endif  // LOGOVGER_CAREER_LIST_H
