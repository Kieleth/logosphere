#include "screen.h"

#include "sheet.h"
#include "text_flow.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace voyager {
namespace {

// A dark table with a lamp on it. One warm ink for the person, one cold
// one for what is being asked, and nothing else.
constexpr uint8_t kInkR = 214, kInkG = 210, kInkB = 198;
constexpr uint8_t kDimR = 120, kDimG = 118, kDimB = 112;
constexpr uint8_t kAskR = 150, kAskG = 190, kAskB = 255;

TextBox* make_box(UISystem& ui, const std::string& id, int scale,
                  uint8_t r, uint8_t g, uint8_t b) {
    // A root widget, not a child of a panel: every box takes its own
    // pointer and its own wheel, and a panel in front of it would
    // swallow both.
    auto* box = new TextBox(id, 1, scale);
    box->set_color(r, g, b);
    box->set_ui_system(&ui);
    ui.add_widget(box);
    return box;
}

void place(ui::Widget* widget, const Rect& rect) {
    widget->set_position(rect.x, rect.y);
    widget->set_size(rect.w, rect.h);
}

std::vector<TextBox::Row> rows_of(const std::string& text, int columns) {
    std::vector<TextBox::Row> rows;
    for (auto& line : wrap(renderable(text), static_cast<size_t>(columns))) {
        rows.push_back({std::move(line), std::string()});
    }
    return rows;
}

}  // namespace

void Screen::build(UISystem& ui, int screen_w, int screen_h) {
    ui_ = &ui;
    screen_w_ = screen_w;
    screen_h_ = screen_h;

    left_ = new ui::Panel("voyager_left");
    left_->set_background_color(14, 14, 17, 255);
    left_->set_border(false);
    left_->set_ui_system(&ui);
    ui.add_widget(left_);

    right_ = new ui::Panel("voyager_right");
    right_->set_background_color(14, 14, 17, 255);
    right_->set_border(false);
    right_->set_ui_system(&ui);
    ui.add_widget(right_);

    title_ = make_box(ui, "voyager_title", scale_, kDimR, kDimG, kDimB);
    prose_ = make_box(ui, "voyager_prose", scale_, kInkR, kInkG, kInkB);
    prompt_ = make_box(ui, "voyager_prompt", scale_, kAskR, kAskG, kAskB);
    note_ = make_box(ui, "voyager_note", scale_, kDimR, kDimG, kDimB);
    sheet_ = make_box(ui, "voyager_sheet", scale_, kInkR, kInkG, kInkB);

    // The story arrives a season at a time and its newest part is what
    // the player is reading; the wheel goes back through the rest.
    prose_->rest_at_end(true);
    // A row of the sheet explains itself in the book's own words.
    sheet_->on_note = [this](const std::string& note) { set_note(note); };

    doors_ = new DoorList("voyager_doors", 1, 1, scale_);
    doors_->set_ui_system(&ui);
    doors_->set_colors(kInkR, kInkG, kInkB, 40, 40, 52);
    doors_->on_taken = [this](const std::string& key) {
        if (on_choice) on_choice(key);
    };
    // A selected door explains itself in the notes: the whole door,
    // its chance and its two lists, or the plan behind a way to spend
    // a season. Arrow keys or one click select; Enter or a second
    // click take it.
    doors_->on_selected = [this](int index) {
        if (index >= 0 && index < static_cast<int>(door_rows_.size())) {
            set_note(door_rows_[static_cast<size_t>(index)].note);
        }
    };
    ui.add_widget(doors_);

    relayout();
}

bool Screen::set_text_scale(int scale) {
    if (scale < kMinTextScale || scale > kMaxTextScale) return false;
    if (scale == scale_) return false;
    scale_ = scale;
    relayout();
    return true;
}

// Every position on the screen, from the words the screen kept and the
// size it is at. Called on every change of either, and the only place
// a widget is moved or resized.
void Screen::relayout() {
    if (!ui_) return;

    // The columns first: their width does not depend on how many rows
    // the sheet has, and the sheet's rows have to be folded to that
    // width before they can be counted.
    layout_ = compute_layout(screen_w_, screen_h_, scale_, 0);
    std::vector<TextBox::Row> sheet_rows;
    for (const auto& row : sheet_rows_) {
        if (!row.folds) {
            sheet_rows.push_back({renderable(row.text), row.note});
            continue;
        }
        const auto parts = wrap(renderable(row.text),
                                static_cast<size_t>(layout_.note_columns));
        if (parts.empty()) {
            sheet_rows.push_back({std::string(), row.note});
            continue;
        }
        // Every row of a folded line carries the same note.
        for (const auto& part : parts) sheet_rows.push_back({part, row.note});
    }
    layout_ = compute_layout(screen_w_, screen_h_, scale_,
                             static_cast<int>(sheet_rows.size()));

    place(left_, layout_.left);
    place(right_, layout_.right);
    for (TextBox* box : {title_, prose_, prompt_, note_, sheet_}) {
        box->set_metrics(layout_.line, layout_.text_scale);
    }
    doors_->set_metrics(layout_.line, layout_.line / 2, layout_.text_scale);

    place(title_, layout_.title);
    title_->set_rows({{"VOYAGER", std::string()}});

    place(prose_, layout_.prose);
    prose_->set_rows(rows_of(prose_text_, layout_.prose_columns));

    // The question takes the rows it needs and no more, and the doors
    // start under it: a short question used to leave the reserve it
    // did not spend as a hole above the first door.
    auto asked = rows_of(prompt_text_, layout_.prose_columns);
    const int asked_rows = static_cast<int>(asked.size());
    place(prompt_, layout_.prompt_shown_rect(asked_rows));
    prompt_->set_rows(std::move(asked));

    const Rect doors = layout_.doors_after(asked_rows);
    place(doors_, doors);
    const int door_columns =
        std::max(1, (doors.w - layout_.line) / std::max(1, layout_.glyph_w));
    std::vector<DoorList::Door> list;
    for (const auto& row : door_rows_) {
        list.push_back({row.key,
                        wrap(renderable(row.text),
                             static_cast<size_t>(door_columns))});
    }
    doors_->set_doors(std::move(list));

    place(sheet_, layout_.sheet);
    sheet_->set_rows(std::move(sheet_rows));

    place(note_, layout_.note);
    note_->set_rows(rows_of(note_text_, layout_.note_columns));

    // The engine's text field is the player's own door, so this game
    // says where it goes rather than leaving it wherever the engine
    // put it: under the doors, in the same column, in the same ink.
    ui_->set_chat_bounds(layout_.field.x, layout_.field.y,
                         layout_.field.w, layout_.field.h);
    ui_->set_chat_theme(kInkR, kInkG, kInkB, kAskR, kAskG, kAskB);
    ui_->set_chat_visible(field_wanted_);
}

void Screen::set_note(const std::string& text) {
    if (text == note_text_) return;
    note_text_ = text;
    note_->set_rows(rows_of(note_text_, layout_.note_columns));
}

void Screen::say(const std::string& text) {
    prose_text_ = text;
    prompt_text_.clear();
    door_rows_.clear();
    field_wanted_ = false;
    relayout();
}

void Screen::show(const kg::KGModule& world, const Session& session) {
    Sheet sheet;
    std::string error;
    if (!read_sheet(world, session.character(), sheet, error)) {
        say("The sheet cannot be read: " + error);
        return;
    }

    // The sheet, one row per characteristic the GRAPH holds, and one
    // per kind of moment this life has faced. Rows are counted rather
    // than fixed, so a seventh characteristic or a first-lived kind
    // gets its row instead of falling off the end. Each row carries
    // the note it shows when the pointer rests on it. Both come out of
    // the graph; this code labels nothing.
    sheet_rows_.clear();
    for (const auto& line : sheet.lines) {
        std::string text = line.label;
        while (text.size() < 5) text += ' ';
        text += line.value.empty() ? std::string("--") : line.value;
        if (!line.modifier.empty()) text += "  " + line.modifier;
        sheet_rows_.push_back({text, line.note, false});
    }
    sheet_rows_.push_back({std::string(), std::string(), false});
    sheet_rows_.push_back({"AGE  " + sheet.age, sheet.age_note, false});
    if (!sheet.career.empty()) {
        sheet_rows_.push_back({sheet.career, sheet.career_note, true});
    }
    if (!sheet.record.empty()) {
        sheet_rows_.push_back({std::string(), std::string(), false});
        for (const auto& line : sheet.record) {
            std::string text = line.label;
            if (!line.count.empty()) text += "  " + line.count;
            sheet_rows_.push_back({text, line.note, true});
        }
    }

    prose_text_ = sheet.background;

    if (session.finished()) {
        close_out(sheet.career);
        return;
    }

    prompt_text_ = session.prompt();
    door_rows_.clear();
    for (const auto& choice : session.choices()) {
        std::string text = choice.label;
        if (!choice.detail.empty()) text += "   " + choice.detail;
        door_rows_.push_back(
            {choice.key, text,
             choice.label +
                 (choice.detail.empty() ? "" : "\n" + choice.detail)});
    }
    // The player's own door takes the player's own words, through the
    // engine's text field. It shows only while those words are wanted.
    field_wanted_ = session.awaiting_plan();
    relayout();
}

void Screen::close_out(const std::string& career) {
    prompt_text_ = career.empty()
                       ? "That is as far as the book is written. Start over."
                       : career +
                             ". That is as far as the book is written. "
                             "Start over.";
    door_rows_.clear();
    field_wanted_ = false;
    relayout();
}

}  // namespace voyager
