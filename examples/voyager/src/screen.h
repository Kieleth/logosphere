// One screen. No frames, no chrome, no scrollback.
//
// Left: what this person is and where they came from, then the doors
// that are open to them. Right: the sheet, one line per characteristic,
// the way it is written on paper.
//
// The sheet is a VIEW. show() takes the graph and reads it; this class
// keeps no score, no age and no career of its own. That is what makes
// "add a characteristic to the graph and the screen grows a line" true
// rather than aspirational, and it is measured in
// test_characteristics_from_graph.
//
// Nothing here draws pixels: engine widgets throughout
// (src/ui/widgets.h). Every panel has its border turned off, which is
// what the owner asked for and also what stops the screen looking like
// a form.

#ifndef VOYAGER_SCREEN_H
#define VOYAGER_SCREEN_H

#include "ui/ui_system.h"
#include "ui/widgets.h"

#include "logosphere/kg/kg_module.h"

#include "screen_layout.h"
#include "session.h"

#include <functional>
#include <string>
#include <vector>

namespace voyager {

class Screen {
public:
    // A door was taken.
    std::function<void(const std::string& key)> on_choice;

    void build(UISystem& ui, int screen_w = kScreenW,
               int screen_h = kScreenH);

    // Everything on screen, read out of the graph and the session.
    void show(const kg::KGModule& world, const Session& session);

    // What the screen says while the referee is being waited on, and
    // what it says when something went wrong. Both replace the prose,
    // because both are the answer to "what is happening".
    void say(const std::string& text);

    // The last thing this slice does.
    void close_out(const std::string& career);

private:
    void set_prose(const std::string& text);

    UISystem*    ui_ = nullptr;
    ScreenLayout layout_;
    ui::Panel*   left_ = nullptr;
    ui::Panel*   right_ = nullptr;
    ui::Label*   title_ = nullptr;
    ui::Label*   prompt_ = nullptr;
    ui::ListMenu* doors_ = nullptr;
    std::vector<ui::Label*> prose_;
    // One label per line the sheet can hold. Grown on demand, so a
    // seventh characteristic in the graph gets a seventh label rather
    // than being dropped off the end of a fixed array.
    std::vector<ui::Label*> sheet_;
};

}  // namespace voyager

#endif  // VOYAGER_SCREEN_H
