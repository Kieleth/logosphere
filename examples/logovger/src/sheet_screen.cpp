#include "sheet_screen.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace logovger {
namespace {

// A dark table with a lamp on it.
constexpr int kPad = 12;
constexpr int kLine = 18;
constexpr int kStatRows = 12;      // six characteristics, career, age, terms
constexpr int kNarrationLines = 6;   // the list needs the room:
                                     // containers do not clip children,
                                     // so an overflowing menu draws
                                     // outside its panel
constexpr int kProvLines = 6;

ui::Label* make_label(ui::Container* parent, int x, int y, int w,
                      uint8_t r, uint8_t g, uint8_t b) {
    auto* l = new ui::Label("", "");
    l->set_position(x, y);
    l->set_size(w, kLine);
    l->set_color(r, g, b);
    parent->add_child(l);
    return l;
}

// The bitmap font (5x7, fixed charset) draws unknown bytes as blanks,
// and the book is full of en dashes and multiplication signs. Show
// them as their ASCII cousins rather than as holes; the DATA is
// untouched, this is only what the panel paints.
std::string renderable(const std::string& s) {
    static const struct { const char* from; const char* to; } kMap[] = {
        {"\xE2\x80\x93", "-"},      // en dash
        {"\xE2\x80\x94", "--"},     // em dash
        {"\xC3\x97",      "x"},      // multiplication sign
        {"\xE2\x80\x99", "'"},      // right single quote
        {"\xE2\x80\x9C", "\""},     // left double quote
        {"\xE2\x80\x9D", "\""},     // right double quote
    };
    std::string out = s;
    for (const auto& m : kMap) {
        size_t at = 0;
        while ((at = out.find(m.from, at)) != std::string::npos)
            out.replace(at, std::strlen(m.from), m.to);
    }
    return out;
}

std::string ellipsize(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max - 3) + "...";
}

}  // namespace

void SheetScreen::build(UISystem& ui, int screen_w, int screen_h) {
    ui_ = &ui;

    const int right_w = 340;
    const int bottom_h = kProvLines * kLine + 3 * kPad;
    const int left_w = screen_w - right_w - 3 * kPad;
    const int top_h = screen_h - bottom_h - 3 * kPad;

    // ---- LEFT: what is happening, and what you are asked --------------
    left_ = new ui::Panel("logovger_narration");
    left_->set_position(kPad, kPad);
    left_->set_size(left_w, top_h);
    left_->set_background_color(16, 16, 22, 235);
    left_->set_border(true, 70, 80, 110);
    left_->set_ui_system(&ui);
    ui.add_widget(left_);

    int y = kPad;
    auto* title = make_label(left_, kPad, y, left_w - 2 * kPad, 235, 225, 190);
    title->set_text("CHARACTER CREATION -- Cepheus Engine, read from the graph");
    y += kLine + kPad / 2;

    for (int i = 0; i < kNarrationLines; ++i) {
        Line ln;
        ln.label = make_label(left_, kPad, y, left_w - 2 * kPad,
                              205, 205, 215);
        narration_.push_back(ln);
        y += kLine;
    }
    y += kPad / 2;

    prompt_ = make_label(left_, kPad, y, left_w - 2 * kPad, 150, 200, 255);
    y += kLine + 4;

    choices_ = new CareerList("logovger_choices");
    choices_->set_position(kPad * 2, y + kPad);
    choices_->set_size(left_w - 4 * kPad, top_h - y - 3 * kPad);
    choices_->set_ui_system(&ui);
    choices_->on_choose = [this](const std::string& key) {
        if (on_answer) on_answer(key);
    };
    choices_->on_inspect = [this](const std::string& key) {
        if (kg_ && on_inspect_key) on_inspect_key(key);
    };
    ui.add_widget(choices_);   // a root widget: it must receive scroll
                               // and clicks without a parent swallowing
                               // them

    // ---- RIGHT: the sheet, every value clickable ----------------------
    right_ = new ui::Panel("logovger_sheet");
    right_->set_position(kPad * 2 + left_w, kPad);
    right_->set_size(right_w, top_h);
    right_->set_background_color(22, 20, 16, 240);
    right_->set_border(true, 120, 100, 60);
    right_->set_ui_system(&ui);
    ui.add_widget(right_);

    int sy = kPad;
    auto* sheet_title = make_label(right_, kPad, sy, right_w - 2 * kPad,
                                   235, 210, 150);
    sheet_title->set_text("THE CHARACTER   (click any value)");
    sy += kLine + kPad / 2;

    stats_.reserve(kStatRows);
    stat_cites_.assign(kStatRows, kg::INVALID_ENTITY);
    stat_claims_.assign(kStatRows, "");
    for (int i = 0; i < kStatRows; ++i) {
        auto* b = new ui::Button("", "stat" + std::to_string(i));
        b->set_position(kPad, sy);
        b->set_size(right_w - 2 * kPad, kLine + 2);
        const size_t idx = static_cast<size_t>(i);
        b->on_click = [this, idx]() {
            if (idx >= stat_cites_.size() || !kg_) return;
            if (stat_cites_[idx] == kg::INVALID_ENTITY) {
                Provenance p;
                p.claim = stat_claims_[idx];
                p.trouble = "rolled by the engine, not read from the book: "
                            "a die result has a roll id, not a citation";
                show_provenance(p);
                return;
            }
            show_provenance(provenance_of(*kg_, stat_cites_[idx],
                                          stat_claims_[idx]));
        };
        right_->add_child(b);
        stats_.push_back(b);
        sy += kLine + 4;
    }

    // What you have learned, one row per skill, each one clickable:
    // a skill is a book entry, not a word.
    sy += kPad;
    auto* skills_title = make_label(right_, kPad, sy, right_w - 2 * kPad,
                                    200, 190, 150);
    skills_title->set_text("SKILLS   (click one to read it)");
    sy += kLine + 2;
    for (int i = 0; i < 8; ++i) {
        auto* b = new ui::Button("", "skill" + std::to_string(i));
        b->set_position(kPad, sy);
        b->set_size(right_w - 2 * kPad, kLine);
        const size_t idx = static_cast<size_t>(i);
        b->on_click = [this, idx]() {
            if (!kg_ || idx >= skill_names_.size()) return;
            if (skill_names_[idx].empty()) return;
            show_skill(*kg_, skill_names_[idx]);
        };
        right_->add_child(b);
        skill_buttons_.push_back(b);
        sy += kLine + 2;
    }
    skill_names_.assign(8, "");

    auto* again = new ui::Button("   [ another life ]", "logovger_again");
    again->set_position(kPad, sy + kPad);
    again->set_size(right_w - 2 * kPad, kLine + 6);
    again->on_click = [this]() { if (on_new_life) on_new_life(); };
    right_->add_child(again);

    // ---- BOTTOM: why. the book answers -------------------------------
    bottom_ = new ui::Panel("logovger_provenance");
    bottom_->set_position(kPad, kPad * 2 + top_h);
    bottom_->set_size(screen_w - 2 * kPad, bottom_h);
    bottom_->set_background_color(12, 18, 16, 240);
    bottom_->set_border(true, 70, 120, 90);
    bottom_->set_ui_system(&ui);
    ui.add_widget(bottom_);

    int by = kPad;
    auto* prov_title = make_label(bottom_, kPad, by,
                                  screen_w - 4 * kPad, 150, 220, 170);
    prov_title->set_text("WHY -- click a value on the sheet and the book "
                         "answers");
    by += kLine + 2;
    // A career's "it teaches" list, as buttons rather than a sentence,
    // so any skill in it can be read before you sign.
    for (int i = 0; i < 8; ++i) {
        auto* b = new ui::Button("", "teaches" + std::to_string(i));
        b->set_position(kPad + 110 + i * 130, by + kProvLines * kLine - kLine);
        b->set_size(126, kLine);
        b->set_visible(false);
        const size_t idx = static_cast<size_t>(i);
        b->on_click = [this, idx]() {
            if (!kg_ || idx >= teaches_names_.size()) return;
            if (teaches_names_[idx].empty()) return;
            show_skill(*kg_, teaches_names_[idx]);
        };
        bottom_->add_child(b);
        teaches_buttons_.push_back(b);
    }
    teaches_names_.assign(8, "");

    for (int i = 0; i < kProvLines; ++i) {
        provenance_lines_.push_back(
            make_label(bottom_, kPad, by, screen_w - 4 * kPad,
                       190, 215, 200));
        by += kLine;
    }
}

void SheetScreen::say(const std::string& raw, Tone tone) {
    const std::string line = renderable(raw);
    // A rolling window: newest at the bottom, older ones scroll up,
    // and the flash travels with its line rather than staying put.
    if (narration_next_ < narration_.size()) {
        auto& ln = narration_[narration_next_++];
        ln.label->set_text(line);
        ln.tone = tone;
        ln.flash = (tone == Tone::Plain) ? 0.0f : 1.4f;
        return;
    }
    for (size_t i = 1; i < narration_.size(); ++i) {
        narration_[i - 1].label->set_text(narration_[i].label->get_text());
        narration_[i - 1].tone  = narration_[i].tone;
        narration_[i - 1].flash = narration_[i].flash;
    }
    auto& last = narration_.back();
    last.label->set_text(line);
    last.tone = tone;
    last.flash = (tone == Tone::Plain) ? 0.0f : 1.4f;
}

// The flash fades from the tone's colour back to plain text. Watching
// a throw land is most of the pleasure of a dice game, and a log that
// simply appears takes that away.
void SheetScreen::tick(float dt) {
    for (auto& ln : narration_) {
        if (ln.flash <= 0.0f) continue;
        ln.flash = std::max(0.0f, ln.flash - dt);
        const float t = ln.flash / 1.4f;          // 1 fresh -> 0 faded
        auto mix = [t](int hot, int cold) {
            return static_cast<uint8_t>(cold + (hot - cold) * t);
        };
        switch (ln.tone) {
            case Tone::Good:
                ln.label->set_color(mix(110, 205), mix(255, 205),
                                    mix(140, 215));
                break;
            case Tone::Bad:
                ln.label->set_color(mix(255, 205), mix(110, 205),
                                    mix(110, 215));
                break;
            case Tone::Roll:
                ln.label->set_color(mix(255, 205), mix(220, 205),
                                    mix(140, 215));
                break;
            default: break;
        }
        if (ln.flash <= 0.0f) ln.label->set_color(205, 205, 215);
    }
}

void SheetScreen::clear_choices() {
    if (choices_) choices_->clear();
}

void SheetScreen::set_stat(size_t index, const std::string& label,
                           const std::string& value, kg::EntityID cite) {
    if (index >= stats_.size()) return;
    std::ostringstream o;
    o << "  " << label;
    const int pad = 18 - static_cast<int>(label.size());
    for (int i = 0; i < pad; ++i) o << " ";
    o << value;
    stats_[index]->set_text(o.str());
    stat_cites_[index] = cite;
    stat_claims_[index] = label + " " + value;
}

void SheetScreen::show(kg::KGModule& kg, const ChargenSession& session) {
    kg_ = &kg;
    const auto& s = session.sheet();

    // The sheet. A characteristic was ROLLED, so it cites no book: the
    // panel says so rather than pretending. A career's throws DO cite
    // the book, and those are the interesting clicks.
    size_t i = 0;
    set_stat(i++, "Str", std::to_string(s.strength), kg::INVALID_ENTITY);
    set_stat(i++, "Dex", std::to_string(s.dexterity), kg::INVALID_ENTITY);
    set_stat(i++, "End", std::to_string(s.endurance), kg::INVALID_ENTITY);
    set_stat(i++, "Int", std::to_string(s.intelligence), kg::INVALID_ENTITY);
    set_stat(i++, "Edu", std::to_string(s.education), kg::INVALID_ENTITY);
    set_stat(i++, "Soc", std::to_string(s.social_standing), kg::INVALID_ENTITY);
    set_stat(i++, "UPP", s.upp, kg::INVALID_ENTITY);

    kg::EntityID career = kg::INVALID_ENTITY, qual = kg::INVALID_ENTITY,
                 surv = kg::INVALID_ENTITY;
    if (!s.career.empty()) {
        for (auto id : kg.findByType("Career"))
            if (kg.getProperty(id, "name") == s.career) career = id;
        if (career != kg::INVALID_ENTITY) {
            const auto q = kg.getProperty(career, "qualification_check");
            const auto v = kg.getProperty(career, "survival_check");
            if (!q.empty()) qual = static_cast<kg::EntityID>(std::stoul(q));
            if (!v.empty()) surv = static_cast<kg::EntityID>(std::stoul(v));
        }
    }
    set_stat(i++, "Career", s.career.empty() ? "-" : s.career, career);
    if (qual != kg::INVALID_ENTITY) {
        set_stat(i++, "qualifies on",
                 kg.getProperty(qual, "attribute_ref") + " " +
                     kg.getProperty(qual, "target_number") + "+", qual);
    } else {
        set_stat(i++, "qualifies on", "-", kg::INVALID_ENTITY);
    }
    if (surv != kg::INVALID_ENTITY) {
        set_stat(i++, "survives on",
                 kg.getProperty(surv, "attribute_ref") + " " +
                     kg.getProperty(surv, "target_number") + "+", surv);
    } else {
        set_stat(i++, "survives on", "-", kg::INVALID_ENTITY);
    }
    set_stat(i++, "Age", std::to_string(s.age_years), kg::INVALID_ENTITY);
    set_stat(i++, "Terms", std::to_string(s.terms_served), kg::INVALID_ENTITY);

    // What has been learned so far, from the ratings hanging off the
    // character: name and level, one row each, updated every term.
    size_t slot = 0;
    for (auto part : kg.getRelated(s.id, "HAS_PART")) {
        const auto ref = kg.getProperty(part, "skill");
        if (ref.empty() || slot >= skill_buttons_.size()) continue;
        const auto skill = static_cast<kg::EntityID>(std::stoul(ref));
        const auto name = kg.getProperty(skill, "name");
        const auto level = kg.getProperty(part, "skill_level");
        if (name.empty()) continue;
        skill_buttons_[slot]->set_text("  " + name + "-" +
                                       (level.empty() ? "1" : level));
        skill_names_[slot] = name;
        ++slot;
    }
    for (size_t k = slot; k < skill_buttons_.size(); ++k) {
        skill_buttons_[k]->set_text(k == 0 && slot == 0 ? "  (none yet)" : "");
        skill_names_[k].clear();
    }

    // The question, and the answers as a clickable list.
    clear_choices();
    prompt_->set_text(session.prompt());
    std::vector<CareerList::Row> rows;
    for (const auto& c : session.choices())
        rows.push_back({c.key, renderable(c.label), renderable(c.detail)});
    choices_->set_rows(std::move(rows));
}

void SheetScreen::inspect_career(kg::KGModule& kg, const std::string& name) {
    kg_ = &kg;
    kg::EntityID career = kg::INVALID_ENTITY;
    for (auto id : kg.findByType("Career"))
        if (kg.getProperty(id, "name") == name) career = id;
    if (career == kg::INVALID_ENTITY) return;

    auto ref = [&](const char* slot) {
        const auto v = kg.getProperty(career, slot);
        return v.empty() ? kg::INVALID_ENTITY
                         : static_cast<kg::EntityID>(std::stoul(v));
    };
    const auto qual = ref("qualification_check");
    const auto surv = ref("survival_check");

    // What it will teach you: the service table's six results.
    std::vector<std::string> teaches_list;
    std::string teaches;
    for (auto part : kg.getRelated(career, "HAS_PART")) {
        for (auto row : kg.getRelated(part, "HAS_PART")) {
            const auto out = kg.getProperty(row, "outcome");
            if (out.empty()) continue;
            const auto skill_ref = kg.getProperty(
                static_cast<kg::EntityID>(std::stoul(out)), "skill");
            if (skill_ref.empty()) continue;
            const auto n = kg.getProperty(
                static_cast<kg::EntityID>(std::stoul(skill_ref)), "name");
            if (n.empty()) continue;
            teaches += (teaches.empty() ? "" : ", ") + n;
            if (std::find(teaches_list.begin(), teaches_list.end(), n) ==
                teaches_list.end())
                teaches_list.push_back(n);
        }
    }

    size_t i = 0;
    auto line = [&](const std::string& s, uint8_t r, uint8_t g, uint8_t b) {
        if (i >= provenance_lines_.size()) return;
        provenance_lines_[i]->set_text(renderable(s));
        provenance_lines_[i]->set_color(r, g, b);
        ++i;
    };
    line(name + " -- what you would be signing up for", 235, 235, 210);
    if (qual != kg::INVALID_ENTITY) {
        const auto p = provenance_of(kg, qual, "");
        line("  to get in:   throw " + kg.getProperty(qual, "attribute_ref") +
             " " + kg.getProperty(qual, "target_number") + "+" +
             (p.resolved ? "     the book: \"" + p.says + "\"  (line " +
                           std::to_string(p.line) + ")" : ""),
             190, 240, 200);
    }
    if (surv != kg::INVALID_ENTITY) {
        const auto p = provenance_of(kg, surv, "");
        line("  to survive:  throw " + kg.getProperty(surv, "attribute_ref") +
             " " + kg.getProperty(surv, "target_number") + "+" +
             (p.resolved ? "     the book: \"" + p.says + "\"  (line " +
                           std::to_string(p.line) + ")" : ""),
             240, 210, 170);
    }
    if (!teaches_list.empty()) line("  it teaches:", 180, 200, 240);
    for (size_t k = 0; k < teaches_buttons_.size(); ++k) {
        const bool has = k < teaches_list.size();
        teaches_buttons_[k]->set_visible(has);
        teaches_buttons_[k]->set_text(has ? " " + teaches_list[k] : "");
        teaches_names_[k] = has ? teaches_list[k] : "";
    }
    line("  click it again to join, or click a skill above to read it",
         150, 170, 200);
    while (i < provenance_lines_.size())
        provenance_lines_[i++]->set_text("");
}

// A skill is an entry in the book. Find it by the heading it cites and
// read what the chapter says under it, rather than paraphrasing.
void SheetScreen::show_skill(kg::KGModule& kg, const std::string& name) {
    kg_ = &kg;
    kg::EntityID skill = kg::INVALID_ENTITY;
    for (auto id : kg.findByType("Skill"))
        if (kg.getProperty(id, "name") == name) skill = id;

    size_t i = 0;
    auto line = [&](const std::string& s, uint8_t r, uint8_t g, uint8_t b) {
        if (i >= provenance_lines_.size()) return;
        provenance_lines_[i]->set_text(renderable(s));
        provenance_lines_[i]->set_color(r, g, b);
        ++i;
    };
    for (auto* b : teaches_buttons_) b->set_visible(false);

    line(name + " -- what the book says", 235, 235, 210);
    if (skill == kg::INVALID_ENTITY) {
        line("  no Skill by that name is loaded", 240, 180, 140);
        while (i < provenance_lines_.size()) provenance_lines_[i++]->set_text("");
        return;
    }

    const auto section = kg.getProperty(skill, "source_section");
    const auto file = kg.getProperty(skill, "source_file");
    const auto* doc = (file.find("skills") != std::string::npos) ? skills_
                                                                 : chapter_;
    bool said = false;
    if (doc && !section.empty()) {
        if (const auto* sec = doc->find_section({section})) {
            for (const auto& para : sec->paragraphs) {
                if (para.text.empty()) continue;
                // Two lines of the entry is enough to know what it is.
                const auto text = ellipsize(para.text, 220);
                const size_t half = text.size() / 2;
                size_t brk = text.find(' ', half);
                if (brk == std::string::npos) brk = text.size();
                line("  " + text.substr(0, brk), 200, 230, 210);
                if (brk < text.size())
                    line("  " + text.substr(brk + 1), 200, 230, 210);
                line("  " + file + "  >  " + section + "   (line " +
                     std::to_string(sec->line) + ")", 140, 180, 155);
                said = true;
                break;
            }
        }
    }
    if (!said)
        line("  the chapter has no entry under \"" + section +
             "\" - this skill is named by a career table but never "
             "defined", 240, 180, 140);
    while (i < provenance_lines_.size()) provenance_lines_[i++]->set_text("");
}

Provenance SheetScreen::provenance_of(kg::KGModule& kg, kg::EntityID id,
                                      const std::string& claim) const {
    using namespace logosphere::text;
    Provenance p;
    p.claim = claim;

    SourceLocator loc;
    loc.file   = kg.getProperty(id, "source_file");
    loc.exact  = kg.getProperty(id, "source_quote");
    loc.table  = kg.getProperty(id, "source_table");
    loc.row    = kg.getProperty(id, "source_row");
    loc.column = kg.getProperty(id, "source_column");
    const auto section = kg.getProperty(id, "source_section");
    if (!section.empty()) loc.path = {section};
    const auto kind = kg.getProperty(id, "source_kind");
    if (kind.empty())
        loc.kind = loc.table.empty() ? LocatorKind::Sentence : LocatorKind::Cell;
    else if (!kind_from_string(kind, loc.kind))
        loc.kind = LocatorKind::Sentence;

    if (loc.exact.empty()) {
        p.trouble = "this entity carries no citation";
        return p;
    }
    p.address = loc.file + "  >  " + section;
    if (!loc.table.empty()) {
        p.detail = std::string("[") + to_string(loc.kind) + "]  table \"" +
                   loc.table + "\"   row \"" + loc.row + "\"   column \"" +
                   loc.column + "\"";
    } else {
        p.detail = std::string("[") + to_string(loc.kind) + "]";
    }

    const auto* doc = (loc.file.find("skills") != std::string::npos)
                          ? skills_ : chapter_;
    if (!doc) { p.trouble = "the source is not loaded"; return p; }

    const auto r = resolve_and_match(*doc, loc);
    p.resolved = r.ok;
    p.line = r.line;
    if (r.ok) p.says = r.text;
    else      p.trouble = r.reason;
    return p;
}

void SheetScreen::show_provenance(const Provenance& p) {
    size_t i = 0;
    auto line = [&](const std::string& s, uint8_t r, uint8_t g, uint8_t b) {
        if (i >= provenance_lines_.size()) return;
        provenance_lines_[i]->set_text(s);
        provenance_lines_[i]->set_color(r, g, b);
        ++i;
    };

    line(renderable(p.claim), 235, 235, 210);
    if (!p.address.empty())
        line("  source:  " + p.address, 150, 200, 170);
    if (!p.detail.empty())
        line("  address: " + p.detail, 150, 200, 170);
    if (p.resolved) {
        line("  the book says:  \"" + renderable(ellipsize(p.says, 110)) +
         "\"",
             190, 240, 200);
        line("  line " + std::to_string(p.line) +
             "   -- resolved against the vendored source, just now",
             120, 170, 140);
    } else if (!p.trouble.empty()) {
        line("  " + renderable(p.trouble), 240, 180, 140);
    }
    while (i < provenance_lines_.size()) {
        provenance_lines_[i]->set_text("");
        ++i;
    }
}

}  // namespace logovger
