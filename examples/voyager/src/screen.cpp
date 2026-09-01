#include "screen.h"

#include "sheet.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace voyager {
namespace {

// A dark table with a lamp on it. One warm ink for the person, one cold
// one for what is being asked, and nothing else.
constexpr uint8_t kInkR = 214, kInkG = 210, kInkB = 198;
constexpr uint8_t kDimR = 120, kDimG = 118, kDimB = 112;
constexpr uint8_t kAskR = 150, kAskG = 190, kAskB = 255;

ui::Label* make_label(ui::Container* parent, int x, int y, int w,
                      uint8_t r, uint8_t g, uint8_t b) {
    auto* label = new ui::Label("", "");
    label->set_position(x, y);
    label->set_size(w, kLine);
    label->set_color(r, g, b);
    parent->add_child(label);
    return label;
}

// A label that reports the pointer arriving, so a line of the sheet
// can explain itself: the note it shows is the graph's, not its own.
class HoverLabel : public ui::Label {
public:
    using ui::Label::Label;
    std::function<void()> on_hover;
    bool on_mouse_enter(ui::MouseEvent&) override {
        if (on_hover) on_hover();
        return false;
    }
    bool on_mouse_move(ui::MouseEvent&) override {
        if (on_hover) on_hover();
        return false;
    }
};

HoverLabel* make_hover_label(ui::Container* parent, int x, int y, int w,
                             uint8_t r, uint8_t g, uint8_t b) {
    auto* label = new HoverLabel("", "");
    label->set_position(x, y);
    label->set_size(w, kLine);
    label->set_color(r, g, b);
    parent->add_child(label);
    return label;
}


// The engine's bitmap font draws bytes it does not know as blanks, and
// the book is full of en dashes and typographic quotes. Show them as
// their ASCII cousins. The DATA is untouched; this is only paint.
std::string renderable(const std::string& text) {
    static const struct { const char* from; const char* to; } kMap[] = {
        {"\xE2\x80\x93", "-"},
        {"\xE2\x80\x94", "--"},
        {"\xC3\x97", "x"},
        {"\xE2\x80\x99", "'"},
        {"\xE2\x80\x98", "'"},
        {"\xE2\x80\x9C", "\""},
        {"\xE2\x80\x9D", "\""},
    };
    std::string out = text;
    for (const auto& entry : kMap) {
        size_t at = 0;
        while ((at = out.find(entry.from, at)) != std::string::npos) {
            out.replace(at, std::strlen(entry.from), entry.to);
        }
    }
    return out;
}

std::vector<std::string> wrap(const std::string& text, size_t columns) {
    std::vector<std::string> out;
    if (columns == 0) return out;
    size_t at = 0;
    while (at < text.size()) {
        const size_t hard = text.find('\n', at);
        const size_t end = hard == std::string::npos ? text.size() : hard;
        size_t take = std::min(columns, end - at);
        if (at + take < end) {
            const size_t brk = text.rfind(' ', at + take);
            if (brk != std::string::npos && brk > at) take = brk - at;
        }
        out.push_back(text.substr(at, take));
        at += take;
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n')) {
            ++at;
        }
    }
    return out;
}

}  // namespace

void Screen::build(UISystem& ui, int screen_w, int screen_h) {
    ui_ = &ui;
    layout_ = compute_layout(screen_w, screen_h);

    ui_ = &ui;
    left_ = new ui::Panel("voyager_left");
    left_->set_position(layout_.left.x, layout_.left.y);
    left_->set_size(layout_.left.w, layout_.left.h);
    left_->set_background_color(14, 14, 17, 255);
    left_->set_border(false);
    left_->set_ui_system(&ui);
    ui.add_widget(left_);

    right_ = new ui::Panel("voyager_right");
    right_->set_position(layout_.right.x, layout_.right.y);
    right_->set_size(layout_.right.w, layout_.right.h);
    right_->set_background_color(14, 14, 17, 255);
    right_->set_border(false);
    right_->set_ui_system(&ui);
    ui.add_widget(right_);

    title_ = make_label(left_, 0, layout_.title_y - layout_.left.y,
                        layout_.left.w, kDimR, kDimG, kDimB);
    title_->set_text("VOYAGER");

    for (int i = 0; i < layout_.prose_lines; ++i) {
        prose_.push_back(make_label(
            left_, 0, layout_.prose_top - layout_.left.y + i * kLine,
            layout_.left.w, kInkR, kInkG, kInkB));
    }
    for (int i = 0; i < layout_.prompt_lines; ++i) {
        prompt_.push_back(make_label(
            left_, 0, layout_.prompt_y - layout_.left.y + i * kLine,
            layout_.left.w, kAskR, kAskG, kAskB));
    }
    for (int i = 0; i < layout_.note_lines; ++i) {
        note_.push_back(make_label(
            right_, 0, layout_.note_top - layout_.right.y + i * kLine,
            layout_.right.w, kDimR, kDimG, kDimB));
    }

    // A root widget, not a child: the doors must receive their own
    // clicks and keys without a panel in front of them swallowing them.
    doors_ = new DoorList("voyager_doors", kLine, kLine / 2);
    doors_->set_position(layout_.doors.x, layout_.doors.y);
    doors_->set_size(layout_.doors.w, layout_.doors.h);
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
        if (index >= 0 && index < static_cast<int>(door_notes_.size())) {
            set_note(door_notes_[static_cast<size_t>(index)]);
        }
    };
    ui.add_widget(doors_);
}

void Screen::set_prompt(const std::string& text) {
    const auto lines = wrap(renderable(text),
                            static_cast<size_t>(layout_.prose_columns));
    for (size_t i = 0; i < prompt_.size(); ++i) {
        prompt_[i]->set_text(i < lines.size() ? lines[i] : std::string());
    }
}

void Screen::set_note(const std::string& text) {
    const auto lines = wrap(renderable(text),
                            static_cast<size_t>(layout_.note_columns));
    for (size_t i = 0; i < note_.size(); ++i) {
        note_[i]->set_text(i < lines.size() ? lines[i] : std::string());
    }
}

// The sheet's labels, grown on demand, each one reporting the pointer
// so its note can show. A seventh characteristic in the graph gets a
// seventh label instead of being dropped off the end of a fixed array.
ui::Label* Screen::sheet_label(size_t index) {
    while (sheet_.size() <= index) {
        const size_t at = sheet_.size();
        auto* label = make_hover_label(
            right_, 0,
            layout_.sheet_top - layout_.right.y + static_cast<int>(at) * kLine,
            layout_.right.w, kInkR, kInkG, kInkB);
        label->on_hover = [this, at] {
            if (at < sheet_notes_.size() && !sheet_notes_[at].empty()) {
                set_note(sheet_notes_[at]);
            }
        };
        sheet_.push_back(label);
    }
    return sheet_[index];
}

void Screen::set_prose(const std::string& text) {
    const auto lines = wrap(renderable(text),
                            static_cast<size_t>(layout_.prose_columns));
    // The newest prose is what matters; when a life outgrows the
    // panel, the tail shows and the beginning scrolls off the top.
    const size_t skip =
        lines.size() > prose_.size() ? lines.size() - prose_.size() : 0;
    for (size_t i = 0; i < prose_.size(); ++i) {
        const size_t at = i + skip;
        prose_[i]->set_text(at < lines.size() ? lines[at] : std::string());
    }
}

void Screen::say(const std::string& text) {
    set_prose(text);
    set_prompt("");
    if (doors_) doors_->clear();
}

void Screen::show(const kg::KGModule& world, const Session& session) {
    Sheet sheet;
    std::string error;
    if (!read_sheet(world, session.character(), sheet, error)) {
        say("The sheet cannot be read: " + error);
        return;
    }

    // The sheet, one line per characteristic the GRAPH holds, and one
    // per kind of moment this life has faced. Labels are grown to fit
    // rather than fixed, so a seventh characteristic or a first-lived
    // kind gets its line instead of falling off the end.
    // Each row and, beside it, the note the row shows when the pointer
    // rests on it. Both come out of the graph; this code labels nothing.
    sheet_notes_.clear();
    size_t at = 0;
    const auto row = [&](const std::string& text, const std::string& note) {
        sheet_label(at)->set_text(text);
        sheet_notes_.push_back(note);
        ++at;
    };
    for (const auto& line : sheet.lines) {
        std::string text = line.label;
        while (text.size() < 5) text += ' ';
        text += line.value.empty() ? std::string("--") : line.value;
        if (!line.modifier.empty()) text += "  " + line.modifier;
        row(text, line.note);
    }
    row("", "");
    row("AGE  " + sheet.age, sheet.age_note);
    if (!sheet.career.empty()) {
        row(renderable(sheet.career), sheet.career_note);
    }
    if (!sheet.record.empty()) {
        row("", "");
        for (const auto& line : sheet.record) {
            std::string text = renderable(line.label);
            if (!line.count.empty()) text += "  " + renderable(line.count);
            // Wrapped over as many rows as it needs, never cut; every
            // row of it carries the same note.
            for (const auto& part :
                 wrap(text, static_cast<size_t>(layout_.note_columns))) {
                row(part, line.note);
            }
        }
    }
    for (size_t i = at; i < sheet_.size(); ++i) sheet_[i]->set_text("");

    set_prose(sheet.background);

    doors_->clear();
    if (session.finished()) {
        close_out(sheet.career);
        return;
    }
    set_prompt(session.prompt());
    door_notes_.clear();
    // Every door whole, wrapped to the list's width, never cut.
    const auto columns =
        static_cast<size_t>((layout_.doors.w - kLine) / kGlyphW);
    std::vector<DoorList::Door> doors;
    for (const auto& choice : session.choices()) {
        std::string text = choice.label;
        if (!choice.detail.empty()) text += "   " + choice.detail;
        doors.push_back({choice.key, wrap(renderable(text), columns)});
        door_notes_.push_back(choice.label +
                              (choice.detail.empty() ? "" : "\n" + choice.detail));
    }
    doors_->set_doors(std::move(doors));
    // The player's own door takes the player's own words, through the
    // engine's text field. It shows only while those words are wanted.
    if (ui_) ui_->set_chat_visible(session.awaiting_plan());
}

void Screen::close_out(const std::string& career) {
    set_prompt(career.empty()
                   ? "That is as far as the book is written. Start over."
                   : renderable(career) +
                         ". That is as far as the book is written. Start "
                         "over.");
    if (doors_) doors_->clear();
    if (ui_) ui_->set_chat_visible(false);
}

}  // namespace voyager
