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
#include "logosphere/rules/outcome_executor.h"
#include "logosphere/rules/procedure_runner.h"

#include <functional>
#include <set>
#include <string>
#include <utility>
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
    // Terms in the CURRENT career only. Benefits are paid per term
    // served in that career, while the seven-term cap counts a whole
    // life, so the two cannot share a counter.
    int          terms_in_career = 0;
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
    long long    credits = 0;
    std::vector<std::string> possessions;
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
    // Which absorbed rules this life actually reached. A sweep unions
    // these to find rules the book prints that no life ever receives.
    const std::set<kg::EntityID>& rules_reached() const {
        return executor_.rules_reached();
    }
    // Events since the last drain, for incremental display.
    std::vector<LifeEvent> drain();

    // Is a step still part-way through itself? Training and mustering
    // out ask several times before they are done, and each answer
    // produces events. A caller that reacts to every drain reacts four
    // times to one payout; this says "not yet".
    bool mid_sequence() const {
        return training_rolls_owed_ > 0 || benefit_rolls_owed_ > 0;
    }

    // Who answers the questions the book leaves to a referee: which
    // characteristics aging takes, when the rule fixes how many but
    // not which. The game installs this; the engine refuses to choose
    // on its own, so without one those outcomes fail loudly rather
    // than picking quietly. The app installs an adjudicator-backed
    // selector; a test installs its own stub. There is deliberately no
    // default.
    void set_attribute_selector(
        logosphere::rules::AttributeSelector selector) {
        attribute_selector_ = std::move(selector);
        executor_.set_attribute_selector(attribute_selector_);
    }

    // The other referee question: which of the branches a rule prints.
    // Cepheus chapter 1 has two - mishap 1 offers "as a result of 2 on
    // the Injury table" or "roll twice and take the lower", and injury
    // 3 names the characteristics it may take. Without a resolver both
    // rows abort the run, which is the intended behaviour and was the
    // actual behaviour for a while without anyone noticing: 8% of
    // generated lives died on a rule the book prints plainly.
    void set_choice_resolver(logosphere::rules::ChoiceResolver resolver) {
        choice_resolver_ = std::move(resolver);
        executor_.set_choice_resolver(choice_resolver_);
    }

private:
    using PrimitiveContext = logosphere::rules::ProcedurePrimitiveContext;
    using PrimitiveResult = logosphere::rules::ProcedurePrimitiveResult;

    void bind_primitives();
    // EndCareer and ForfeitBenefits are GAME policy: the engine
    // dispatches the typed Outcome and this decides what it means.
    // Registered once, on the session's single executor, because
    // register_handler refuses a duplicate rather than overwriting.
    void register_outcome_handlers();
    // Everything that belongs to a career rather than to a life:
    // rank, its title, and the terms served in it. "You begin as a
    // Rank 0 character", each time.
    void enter_career();
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
    // The training tables this character may roll on right now, gates
    // applied. Recomputed per offer: a roll can change the answer.
    std::vector<kg::EntityID> eligible_training_tables(
        kg::EntityID step, std::string& error);
    // The characteristics as the book groups them, from the graph.
    // Rules about "any characteristic" ask this instead of naming six.
    std::vector<std::string> characteristic_slots(std::string& error) const;
    // How many skills the character holds, for reporting how many a
    // grant actually added.
    size_t held_skills() const;
    PrimitiveResult roll_reenlistment(const PrimitiveContext& context);
    PrimitiveResult roll_promotion(const PrimitiveContext& context,
                                   bool commission);
    PrimitiveResult basic_training(const PrimitiveContext& context);
    // "You begin as a Rank 0 character." Most careers print a title
    // and a skill in that row, and both are the career's to give.
    PrimitiveResult grant_rank_zero(const PrimitiveContext& context);
    PrimitiveResult muster_out(const PrimitiveContext& context);
    PrimitiveResult advance_term(const PrimitiveContext& context);
    PrimitiveResult roll_aging(const PrimitiveContext& context);
    // "If any characteristic is reduced to 0 ... the character dies
    // unless he can pay 1D6x10,000 Credits for medical care." The same
    // rule is printed twice, once for aging and once for injuries, so
    // it lives apart from either.
    PrimitiveResult begin_crisis(const char* cause);
    PrimitiveResult resolve_crisis(const std::string& answer);
    // "With the Referee's approval, you can keep the character that
    // fails a survival roll and roll on the Survival Mishaps table
    // instead." Optional in the book, so it is offered rather than
    // taken: in a solo generation the player holds the referee's seat.
    PrimitiveResult survival_mishap(const PrimitiveContext& context);
    // Rolls a table an outcome asked for and applies what it selects.
    // Mishaps reach the Injury table this way; the executor hands back
    // the request and deliberately never rolls it itself.
    bool run_granted_rolls(
        const std::vector<logosphere::rules::TableRollRequest>& requests,
        const char* purpose, std::string& error);
    PrimitiveResult choose_term_end(const PrimitiveContext& context);
    PrimitiveResult finish_character(const PrimitiveContext& context);

    kg::KGModule&                    kg_;
    logosphere::dice::DiceService&   dice_;
    logosphere::rules::AttributeSelector attribute_selector_;
    logosphere::rules::ChoiceResolver choice_resolver_;
    logosphere::rules::ProcedurePrimitiveRegistry primitives_;
    logosphere::rules::ProcedureRunner runner_;
    // ONE executor per session. Handlers are registered on it, and a
    // per-call executor would either lose them or fail on the second
    // registration. It is non-copyable and non-movable, so it is a
    // direct member.
    logosphere::rules::OutcomeExecutor executor_;
    CharacterSheet                   sheet_;
    std::vector<Choice>              choices_;
    std::string                      prompt_;
    bool                             finished_ = false;
    kg::EntityID                     career_ = kg::INVALID_ENTITY;
    kg::EntityID                     procedure_ = kg::INVALID_ENTITY;
    logosphere::rules::ProcedureCursor cursor_;
    std::string                      finish_reason_;
    // Training rolls the character has coming. One a term, plus one
    // for each promotion, plus one more when the career offers no
    // promotion at all. The book counts them; this is that count.
    int                              training_rolls_owed_ = 0;
    // Benefit rolls still to take on leaving a career, and how many of
    // them may still be taken as cash.
    int                              benefit_rolls_owed_ = 0;
    int                              cash_rolls_left_ = 0;
    size_t                           drained_ = 0;
    std::string                      turned_away_from_;
    // The price the crisis quoted, rolled once and remembered across
    // the question so the answer is priced at what was asked.
    long long                        crisis_price_ = 0;
    uint64_t                         crisis_roll_ = 0;
    // A crisis is outstanding, so the next answer belongs to it. Two
    // steps can raise one, and survival_mishap asks a question of its
    // own first: without this it read "pay for care" as an answer to
    // "take the mishap", rolled a second mishap and took no money.
    bool                             crisis_open_ = false;
    std::string                      crisis_cause_;
    // "Lose all benefits", set by a mishap row and consumed by the
    // muster-out that follows it.
    bool                             benefits_forfeited_ = false;
};

// Auto-played convenience: the whole life with no questions asked,
// taking the named career and always reenlisting. Used by tests and
// by anything that wants a character without a conversation.
struct ChargenRequest {
    std::string career_name;
    uint64_t    seed = 0;
    int         max_terms = 4;
    // Who decides which characteristics aging takes. Empty means
    // nobody, and a life that rolls a row leaving that choice open
    // then fails loudly rather than picking for itself. A four-term
    // life reaches the aging table, so anything driving one past a
    // damaging row must supply this.
    logosphere::rules::AttributeSelector attribute_selector;
    // Who answers a branch the book prints and does not decide. Empty
    // means the auto-player answers it: it takes the first option, and
    // says so in the timeline so a reader can see a choice was made
    // and by whom. A caller that wants taste supplies its own.
    logosphere::rules::ChoiceResolver choice_resolver;
    // Where the BOOK leaves a choice open and the auto-player has no
    // taste. Returning a key takes that option; returning empty keeps
    // the standing answer, which is what every existing caller gets,
    // so an empty hook changes no measurement anywhere.
    //
    // It exists because fixed taste is invisible in a green suite. The
    // auto-player always trained on Service Skills and always took
    // cash, so Personal Development, Specialist and Advanced Education
    // - three of the four tables the book gives every career, 72
    // tables, 432 absorbed outcomes - were never executed by any test
    // in any release. A sweep measuring coverage supplies a hook that
    // varies, and only then does the number mean anything.
    std::function<std::string(const std::string& prompt,
                              const std::vector<Choice>& choices)> taste;
};
bool run_chargen(const ChargenRequest& request,
                 kg::KGModule& kg,
                 logosphere::dice::DiceService& dice,
                 CharacterSheet& out,
                 std::string& error,
                 // Optional: the absorbed rules this life reached,
                 // merged into whatever is already there so a sweep can
                 // accumulate across lives.
                 std::set<kg::EntityID>* rules_reached = nullptr);

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
