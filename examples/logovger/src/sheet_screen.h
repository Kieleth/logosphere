// The character-creation screen: a table you sit at, not a log you read.
//
// Three panels, and the third is the point of the whole module:
//
//   LEFT    what is happening, and what you are being asked. Careers
//           are a clickable list, not a paragraph of 24 options.
//   MIDDLE  the file. Who this person is, written by the narrator as
//           the life happens: name, origin, build, then a service
//           record that grows a line at a time, then the assessment
//           that closes it. A dossier you watch being typed.
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
#include "screen_layout.h"
#include "chargen/chargen.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace logovger {

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

    // The file, written live. The header lands once, near the start;
    // the record grows with the life; the assessment closes it.
    void set_dossier(const std::string& name, const std::string& born,
                     const std::string& build, const std::string& note);

    // One clause, appended to the file. This is the biopic: it is
    // written a line at a time as the life happens, not composed at
    // the end out of a summary.
    void add_file_line(const std::string& clause);
    void set_assessment(const std::string& text);
    void clear_dossier();

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

    // Hold the choices back until the beat has been narrated: prose
    // that arrives after you have already clicked is prose about
    // something you have stopped caring about.
    void set_waiting(bool waiting) { waiting_for_prose_ = waiting; }
    bool waiting() const { return waiting_for_prose_; }

    // Fade the flashes. Call every frame.
    void tick(float dt);
    void show_provenance(const Provenance& p);  // bottom panel

    // Reading is a trail, not a single lookup: a career leads to its
    // skills, a skill to its entry. Every view is remembered so you can
    // walk back out the way you came.
    void go_back();

    // Read whatever an option refers to before taking it: a table's
    // six results, a career's terms, a skill's entry. Dispatches on
    // what the thing actually is.
    void inspect_entity(kg::KGModule& kg, kg::EntityID id);

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
    void say_line(const std::string& line, Tone tone);
    void set_stat(size_t index, const std::string& label,
                  const std::string& value, kg::EntityID cite);

    UISystem* ui_ = nullptr;
    const logosphere::text::SourceDocument* chapter_ = nullptr;
    const logosphere::text::SourceDocument* skills_ = nullptr;

    ui::Panel*    left_ = nullptr;
    ui::Panel*    right_ = nullptr;
    ui::Panel*    bottom_ = nullptr;
    ui::Panel*    dossier_ = nullptr;
    CareerList*   record_ = nullptr;      // service record, scrolls
    CareerList*   biopic_ = nullptr;      // the file, a clause at a time
    std::vector<std::string> file_lines_;
    CareerList*   skills_list_ = nullptr; // grows past any fixed count
    ui::Label*    dossier_name_ = nullptr;
    std::vector<ui::Label*> dossier_lines_;
    std::vector<ui::Label*> assessment_lines_;
    size_t        dossier_columns_ = 60;
    // Wrap a paragraph into a fixed run of labels, blanking the rest.
    void fill_wrapped(std::vector<ui::Label*>& into,
                      const std::string& text);
    // Write into the provenance panel, wrapping. `at` is advanced.
    void write_prov(size_t& at, const std::string& text,
                    uint8_t r, uint8_t g, uint8_t b);
    size_t prov_columns_ = 250;
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
    std::vector<ui::Button*> teaches_buttons_;  // the career entry
    std::vector<std::string> teaches_names_;

    // What was on the panel, so BACK can put it there again.
    struct View {
        enum class Kind { Prov, Career, Skill } kind = Kind::Prov;
        Provenance  prov;      // Kind::Prov
        std::string name;      // Kind::Career / Kind::Skill
    };
    std::vector<View> history_;
    bool              replaying_ = false;   // do not record a replay
    ui::Button*       back_ = nullptr;
    void record(View v);
    void refresh_back();
    size_t narration_next_ = 0;
    bool   waiting_for_prose_ = false;
    size_t wrap_columns_ = 150;

    // What each stat button cites, parallel to stats_.
    std::vector<kg::EntityID>  stat_cites_;
    std::vector<std::string>   stat_claims_;
    kg::KGModule*              kg_ = nullptr;
};

}  // namespace logovger

#endif  // LOGOVGER_SHEET_SCREEN_H
