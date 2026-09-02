// One screen. No frames, no chrome, no scrollback.
//
// Left: what this person is and where they came from, then the
// question on the table, then the doors, then the field their own
// words go in. Right: the sheet, one row per characteristic the way
// it is written on paper, and below it the notes: whatever the
// pointer rests on, explained in the book's own words.
//
// The sheet is a VIEW. show() takes the graph and reads it; this
// class keeps no score, no age and no career of its own. That is what
// makes "add a characteristic to the graph and the screen grows a
// row" true rather than aspirational, and it is measured in
// test_characteristics_from_graph.
//
// WHAT THE SCREEN KEEPS. The words, never the pixels. Every position
// comes from compute_layout, which is a function of the window, the
// type size and how many rows the sheet has; every box of text is
// re-wrapped from the words the screen was last handed. So changing
// the type size is one call: keep the words, ask the layout again,
// put everything back. Nothing is computed twice, and nothing can be
// computed differently in two places.
//
// Nothing here draws pixels: widgets throughout. Every panel has its
// border turned off, which is what the owner asked for and also what
// stops the screen looking like a form.

#ifndef VOYAGER_SCREEN_H
#define VOYAGER_SCREEN_H

#include "ui/ui_system.h"
#include "ui/widgets.h"

#include "logosphere/kg/kg_module.h"

#include "door_list.h"
#include "screen_layout.h"
#include "session.h"
#include "text_box.h"

#include <functional>
#include <string>
#include <vector>

namespace voyager {

class Screen {
public:
    // A door was taken.
    std::function<void(const std::string& key)> on_choice;

    void build(UISystem& ui, int screen_w = kRenderW,
               int screen_h = kRenderH);

    // Everything on screen, read out of the graph and the session.
    void show(const kg::KGModule& world, const Session& session);

    // What the screen says while the referee is being waited on, and
    // what it says when something went wrong. Both replace the prose,
    // because both are the answer to "what is happening".
    void say(const std::string& text);

    // The last thing this slice does.
    void close_out(const std::string& career);

    // Bigger or smaller type, applied to everything at once. Returns
    // false when the size asked for is the one already on screen, or
    // is off either end of the range.
    bool set_text_scale(int scale);
    int text_scale() const { return scale_; }

private:
    // The whole screen, rebuilt from the words it kept, at the size
    // it is now.
    void relayout();
    void set_note(const std::string& text);

    UISystem*    ui_ = nullptr;
    ScreenLayout layout_;
    int          screen_w_ = kRenderW;
    int          screen_h_ = kRenderH;
    int          scale_ = kTextScale;

    ui::Panel*   left_ = nullptr;
    ui::Panel*   right_ = nullptr;
    TextBox*     title_ = nullptr;
    TextBox*     prose_ = nullptr;
    TextBox*     prompt_ = nullptr;
    TextBox*     note_ = nullptr;
    TextBox*     sheet_ = nullptr;
    DoorList*    doors_ = nullptr;

    // The words, kept unwrapped. The screen is rebuilt from these
    // whenever the type size changes.
    struct SheetRow {
        std::string text;
        std::string note;
        bool folds = false;   // a row long enough to need two of them
    };
    struct DoorRow {
        std::string key;
        std::string text;
        std::string note;
    };
    std::string           prose_text_;
    std::string           prompt_text_;
    std::string           note_text_;
    std::vector<SheetRow> sheet_rows_;
    std::vector<DoorRow>  door_rows_;
    bool                  field_wanted_ = false;
};

}  // namespace voyager

#endif  // VOYAGER_SCREEN_H
