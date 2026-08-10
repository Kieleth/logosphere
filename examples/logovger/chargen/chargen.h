// Character generation: the book's own procedure, run against the KG.
//
// Design record: docs/RPG_MODULE.md. This is the first executable
// slice, and it is deliberately GAME-side code (examples/logovger),
// not engine code. Game procedure selects the table row; the engine's
// typed outcome executor applies the row's structured consequence.
//
// What it proves, or fails to:
//   - The rules come out of the KG. Careers, targets, characteristics,
//     skill tables and outcomes are read from seeded entities, never
//     from constants in this file. Change the seed, change the life.
//   - The dice come from the engine. Every roll is seeded, journaled
//     and citable by id; this code cannot assert a result.
//   - The result goes back into the KG, so a referee reading the graph
//     sees the same life the player did.
//
// A session STOPS at the book's decision points and asks. That is the
// a-b-c half of the a-b-c+L design: baked choices now, the free-form
// L option (the referee) later, in the same place.
//
// Basic on purpose: choose a career, qualify, then per term survive,
// train, age, and decide whether to stay. No commission, mishaps,
// benefits or ageing crisis yet.

#ifndef LOGOVGER_CHARGEN_H
#define LOGOVGER_CHARGEN_H

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"

#include <string>
#include <vector>

namespace logovger {

// One thing that happened, in the order it happened. The timeline is
// the watchable artifact: a life you can read.
struct LifeEvent {
    int         term = 0;        // 0 = before service
    std::string what;            // "qualified", "gained Admin-2"
    std::string detail;          // the book's terms: "2D6 = 8 +1 DM -> 9 vs 6+"
    uint64_t    roll_id = 0;     // 0 when nothing was rolled
};

struct CharacterSheet {
    kg::EntityID id = kg::INVALID_ENTITY;
    std::string  upp;
    int          strength = 0, dexterity = 0, endurance = 0;
    int          intelligence = 0, education = 0, social_standing = 0;
    int          age_years = 18;
    int          terms_served = 0;
    std::string  career;
    bool         qualified = false;
    std::vector<std::string> skills;   // as gained, "Admin-1", "Admin-2"
    std::vector<LifeEvent>   life;
};

// One option at a decision point. `key` is what the player types.
struct Choice {
    std::string key;      // "a", "b", ...
    std::string label;    // "Agent"
    std::string detail;   // "qualify on Soc 6+, survive on Int 6+"
};

// A life being lived, one decision at a time.
//
// begin() rolls the characteristics and stops at the first question.
// choose() applies the answer and runs until the next question, or
// until the life is done. Nothing happens without an answer, which is
// what makes a player (or later, a referee) part of the procedure.
class ChargenSession {
public:
    ChargenSession(kg::KGModule& kg, logosphere::dice::DiceService& dice)
        : kg_(kg), dice_(dice) {}

    // Rolls the six characteristics and offers the careers the KG
    // knows. False with `error` set when the world has no careers.
    bool begin(uint64_t seed, std::string& error);

    bool finished() const { return finished_; }
    // What the player is being asked right now.
    const std::string& prompt() const { return prompt_; }
    const std::vector<Choice>& choices() const { return choices_; }

    // Answer the current question. Accepts the choice key ("a") or the
    // label ("Agent"), case-insensitively. False with `error` set on an
    // answer that is not on offer - a wrong answer is never guessed at.
    bool choose(const std::string& answer, std::string& error);

    const CharacterSheet& sheet() const { return sheet_; }
    // Events since the last drain, for incremental display.
    std::vector<LifeEvent> drain();

private:
    void offer_careers();
    bool run_term(std::string& error);  // survive, train, age, then ask
    void finish(const std::string& why);

    kg::KGModule&                    kg_;
    logosphere::dice::DiceService&   dice_;
    CharacterSheet                   sheet_;
    std::vector<Choice>              choices_;
    std::string                      prompt_;
    bool                             finished_ = false;
    kg::EntityID                     career_ = kg::INVALID_ENTITY;
    size_t                           drained_ = 0;
};

// Auto-played convenience: the whole life with no questions asked,
// taking the named career and always reenlisting. Used by tests and
// by anything that wants a character without a conversation.
struct ChargenRequest {
    std::string career_name;
    uint64_t    seed = 0;
    int         max_terms = 4;
};
bool run_chargen(const ChargenRequest& request,
                 kg::KGModule& kg,
                 logosphere::dice::DiceService& dice,
                 CharacterSheet& out,
                 std::string& error);

// The life as a timeline, one line per event, with roll citations.
std::string format_life(const CharacterSheet& sheet);

}  // namespace logovger

#endif  // LOGOVGER_CHARGEN_H
