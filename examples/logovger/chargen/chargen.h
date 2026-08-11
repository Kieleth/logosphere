// Character generation: the book's own procedure, run against the KG.
//
// Design record: docs/RPG_MODULE.md. This is the first executable
// slice, and it is deliberately GAME-side code (examples/logovger),
// not engine code. A seeded Procedure owns ordering and routing. Game
// primitives select table rows; the engine's typed outcome executor
// applies each row's structured consequence.
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
#include "logosphere/rules/procedure_runner.h"

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
    // Every career lived, in order. The book lets you leave one and
    // try another, at a price, and forbids going back.
    std::vector<std::string> careers_served;
    std::vector<LifeEvent>   life;
    // Rank within the CURRENT career. The book starts everyone at 0
    // and only commissioned characters ever leave it.
    int          rank = 0;
    std::string  rank_title;
};

// One option at a decision point. `key` is what the player types.
using Choice = logosphere::rules::ProcedureChoice;

// A life being lived, one decision at a time.
//
// begin() starts the seeded Procedure and runs to its first question.
// choose() resumes the suspended step and runs until the next question,
// or until the life is done. Nothing happens without an answer, which
// is what makes a player (or later, a referee) part of the procedure.
class ChargenSession {
public:
    ChargenSession(kg::KGModule& kg, logosphere::dice::DiceService& dice);
    ChargenSession(const ChargenSession&) = delete;
    ChargenSession& operator=(const ChargenSession&) = delete;
    ChargenSession(ChargenSession&&) = delete;
    ChargenSession& operator=(ChargenSession&&) = delete;

    // Starts basic_chargen from the KG. False with `error` set when the
    // procedure graph, its primitives, or its required game data is missing.
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
    using PrimitiveContext = logosphere::rules::ProcedurePrimitiveContext;
    using PrimitiveResult = logosphere::rules::ProcedurePrimitiveResult;

    void bind_primitives();
    bool accept(logosphere::rules::ProcedureResult result,
                std::string& error);
    bool offer_careers(std::string& error);
    bool constant(const char* name, int& value, std::string& error) const;
    void finish(const std::string& why);

    PrimitiveResult generate_characteristics(const PrimitiveContext& context);
    PrimitiveResult choose_career(const PrimitiveContext& context);
    PrimitiveResult roll_qualification(const PrimitiveContext& context);
    PrimitiveResult draft_or_drifter(const PrimitiveContext& context);
    PrimitiveResult roll_survival(const PrimitiveContext& context);
    PrimitiveResult roll_training(const PrimitiveContext& context);
    PrimitiveResult roll_reenlistment(const PrimitiveContext& context);
    PrimitiveResult advance_term(const PrimitiveContext& context);
    PrimitiveResult choose_term_end(const PrimitiveContext& context);
    PrimitiveResult finish_character(const PrimitiveContext& context);

    kg::KGModule&                    kg_;
    logosphere::dice::DiceService&   dice_;
    logosphere::rules::ProcedurePrimitiveRegistry primitives_;
    logosphere::rules::ProcedureRunner runner_;
    CharacterSheet                   sheet_;
    std::vector<Choice>              choices_;
    std::string                      prompt_;
    bool                             finished_ = false;
    kg::EntityID                     career_ = kg::INVALID_ENTITY;
    kg::EntityID                     procedure_ = kg::INVALID_ENTITY;
    logosphere::rules::ProcedureCursor cursor_;
    std::string                      finish_reason_;
    size_t                           drained_ = 0;
    std::string                      turned_away_from_;
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

// Mechanical facts for narration. Characteristic modifiers are selected
// from the same KG table as TaskCheckRunner, never recomputed by game code.
bool format_character_facts(const CharacterSheet& sheet,
                            const kg::KGModule& kg,
                            std::string& out,
                            std::string& error);

}  // namespace logovger

#endif  // LOGOVGER_CHARGEN_H
