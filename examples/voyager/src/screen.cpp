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
    prompt_ = make_label(left_, 0, layout_.prompt_y - layout_.left.y,
                         layout_.left.w, kAskR, kAskG, kAskB);

    // A root widget, not a child: a list must receive its own clicks
    // and scrolls without a panel in front of it swallowing them.
    doors_ = new ui::ListMenu("voyager_doors");
    doors_->set_position(layout_.doors.x, layout_.doors.y);
    doors_->set_size(layout_.doors.w, layout_.doors.h);
    doors_->set_ui_system(&ui);
    doors_->on_item_activated = [this](const std::string& data) {
        if (on_choice) on_choice(data);
    };
    ui.add_widget(doors_);
}

void Screen::set_prose(const std::string& text) {
    const auto lines = wrap(renderable(text),
                            static_cast<size_t>(layout_.prose_columns));
    for (size_t i = 0; i < prose_.size(); ++i) {
        prose_[i]->set_text(i < lines.size() ? lines[i] : std::string());
    }
}

void Screen::say(const std::string& text) {
    set_prose(text);
    if (prompt_) prompt_->set_text("");
    if (doors_) doors_->clear_items();
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
    const size_t rows = sheet.lines.size() + 2 +   // age and career
                        (sheet.record.empty() ? 0
                                              : sheet.record.size() + 1);
    while (sheet_.size() < rows) {
        sheet_.push_back(make_label(
            right_, 0,
            layout_.sheet_top - layout_.right.y +
                static_cast<int>(sheet_.size()) * kLine,
            layout_.right.w, kInkR, kInkG, kInkB));
    }
    size_t at = 0;
    for (const auto& line : sheet.lines) {
        std::string text = line.label;
        while (text.size() < 5) text += ' ';
        text += line.value.empty() ? std::string("--") : line.value;
        if (!line.modifier.empty()) text += "  " + line.modifier;
        sheet_[at++]->set_text(text);
    }
    sheet_[at]->set_text("");
    ++at;
    std::string tail = "AGE  " + sheet.age;
    if (!sheet.career.empty()) tail += "    " + renderable(sheet.career);
    sheet_[at++]->set_text(tail);
    if (!sheet.record.empty()) {
        sheet_[at++]->set_text("");
        for (const auto& line : sheet.record) {
            sheet_[at++]->set_text(renderable(line.label) + "  " +
                                   line.count);
        }
    }
    for (size_t i = at; i < sheet_.size(); ++i) sheet_[i]->set_text("");

    set_prose(sheet.background);

    doors_->clear_items();
    if (session.finished()) {
        close_out(sheet.career);
        return;
    }
    prompt_->set_text(renderable(session.prompt()));
    for (const auto& choice : session.choices()) {
        std::string row = choice.label;
        if (!choice.detail.empty()) row += "   " + choice.detail;
        doors_->add_item(renderable(row), choice.key);
    }
}

void Screen::close_out(const std::string& career) {
    if (!prompt_) return;
    prompt_->set_text(
        career.empty()
            ? "That is as far as the book is written. Start over."
            : renderable(career) +
                  ". That is as far as the book is written. Start over.");
    if (doors_) doors_->clear_items();
}

}  // namespace voyager
