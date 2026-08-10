// The character-creation screen: a table you sit at, not a log you read.
//
// Three panels, and the third is the point of the whole module:
//
//   LEFT    what is happening, and what you are being asked. Careers
//           are a clickable list, not a paragraph of 24 options.
//   RIGHT   the character sheet. Every value is a button.
//   BOTTOM  provenance. Click any value and the book answers: the
//           address it came from, what the source says there, and the
//           line to go and look at.
//
// That last panel is why the citations exist. A rulebook absorbed into
// a graph is only trustworthy if you can ask any number where it came
// from and get the page back, and this is that question made clickable.
//
// Engine widgets throughout (src/ui/widgets.h): Panel, Label, Button,
// ListMenu. Nothing here draws pixels itself.

#ifndef LOGOVGER_SHEET_SCREEN_H
#define LOGOVGER_SHEET_SCREEN_H

#include "ui/ui_system.h"
#include "ui/widgets.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_document.h"
#include "logosphere/text/source_locator.h"

#include "career_list.h"
#include "chargen/chargen.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace logovger {

// One place decides the screen size; main.cpp and the layout agree.
constexpr int kScreenW = 1500;
constexpr int kScreenH = 1000;

// What the provenance panel shows when you click something: the claim,
// where it came from, and what the book actually says there.
struct Provenance {
    std::string claim;      // "Scout survives on End 7+"
    std::string address;    // "book1/character-creation.md > Career Tables"
    std::string detail;     // table / row / column, or the section
    std::string says;       // what the source says at that address
    int         line = 0;
    bool        resolved = false;
    std::string trouble;    // when it does not resolve, why
};

class SheetScreen {
public:
    // Called when the player picks something: a career, or an answer.
    std::function<void(const std::string& answer)> on_answer;

    void build(UISystem& ui, int screen_w = kScreenW,
               int screen_h = kScreenH);

    // "another life", and anything else the screen offers as a button.
    std::function<void()> on_new_life;

    // A career was clicked once: tell me about it.
    std::function<void(const std::string& key)> on_inspect_key;

    // A skill name was clicked, anywhere on screen: what IS it?
    void show_skill(kg::KGModule& kg, const std::string& skill_name);

    // Sources, for resolving citations live.
    void set_sources(const logosphere::text::SourceDocument* chapter,
                     const logosphere::text::SourceDocument* skills) {
        chapter_ = chapter;
        skills_ = skills;
    }

    // Redraw everything from the session's current state.
    void show(kg::KGModule& kg, const ChargenSession& session);
    // How a line reads: neutral, or the colour of what just happened.
    // A throw that passed flashes green, one that failed flashes red,
    // and both fade back, so you can watch the dice land.
    enum class Tone { Plain, Good, Bad, Roll };
    void say(const std::string& line, Tone tone = Tone::Plain);

    // Fade the flashes. Call every frame.
    void tick(float dt);
    void show_provenance(const Provenance& p);  // bottom panel

    // What a career asks of you and what it will teach you, read out
    // of the graph: its two throws with their citations, and the six
    // skills its service table can grant.
    void inspect_career(kg::KGModule& kg, const std::string& name);

    // Turn an entity's citation into something a human can read, by
    // resolving it against the source rather than trusting the stored
    // quote.
    Provenance provenance_of(kg::KGModule& kg, kg::EntityID id,
                             const std::string& claim) const;

private:
    void clear_choices();
    void set_stat(size_t index, const std::string& label,
                  const std::string& value, kg::EntityID cite);

    UISystem* ui_ = nullptr;
    const logosphere::text::SourceDocument* chapter_ = nullptr;
    const logosphere::text::SourceDocument* skills_ = nullptr;

    ui::Panel*    left_ = nullptr;
    ui::Panel*    right_ = nullptr;
    ui::Panel*    bottom_ = nullptr;
    CareerList*   choices_ = nullptr;
    ui::Label*    prompt_ = nullptr;

    struct Line {
        ui::Label* label = nullptr;
        float      flash = 0.0f;      // seconds of colour left
        Tone       tone = Tone::Plain;
    };
    std::vector<Line>        narration_;
    std::vector<ui::Button*> stats_;
    std::vector<ui::Label*>  provenance_lines_;
    // Skill names are clickable wherever they appear: on the sheet as
    // what you have, and in a career's entry as what it would teach.
    std::vector<ui::Button*> skill_buttons_;    // the sheet
    std::vector<ui::Button*> teaches_buttons_;  // the career entry
    std::vector<std::string> skill_names_;
    std::vector<std::string> teaches_names_;
    size_t narration_next_ = 0;

    // What each stat button cites, parallel to stats_.
    std::vector<kg::EntityID>  stat_cites_;
    std::vector<std::string>   stat_claims_;
    kg::KGModule*              kg_ = nullptr;
};

}  // namespace logovger

#endif  // LOGOVGER_SHEET_SCREEN_H
