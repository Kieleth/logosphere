// One character, made once, by walking a procedure that lives in the
// graph.
//
// THE FLOW IS DATA. This class binds three primitives by name and then
// gets out of the way: the Procedure entity owns which steps run and in
// what order, and a fourth step is a row in a seed rather than a call
// added here. There is no chain of function calls describing character
// creation anywhere in this game, and that is the point rather than a
// style preference — a chain cannot be cited, verified, or changed by
// anyone who does not compile C++.
//
// THE CHARACTER IS THE GRAPH. This class holds no score, no age and no
// career. It writes them through the validated op path and reads them
// back through sheet.h. A member holding a copy would be a second
// answer to "what is this character", and there is no mechanism that
// can say which of two answers is right.
//
// THE REFEREE IS A SEAM, NOT A DEPENDENCY. Two questions this slice
// cannot derive go to whoever is installed: where the person comes
// from, and which careers are worth offering THIS person. The session
// does not know whether that is a model, a tape, or a seeded
// generator, and it refuses to run without one rather than answering
// for itself.

#ifndef VOYAGER_SESSION_H
#define VOYAGER_SESSION_H

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/rules/procedure_runner.h"

#include <functional>
#include <string>
#include <vector>

namespace voyager {

// A question the rules leave open, put to whoever is holding the
// referee's chair.
struct RefereeQuestion {
    // Stable, short, owned here: "background", "careers". A driver
    // turns it into whatever its answer source keys on.
    std::string site;
    std::string prompt;
    // The keys an answer may draw from. EMPTY means prose, and the two
    // are genuinely different questions: a driver answering "careers"
    // has a closed set to draw from and a driver answering "background"
    // has none. Handing over the set is what lets a driver with no
    // model answer at all without this class inventing a fallback.
    std::vector<std::string> allowed;
    // When the answer must carry a number, the bounds the rules put on
    // it, as the graph states them. Empty when no number is asked for.
    // Handed over for the same reason as `allowed`: a driver with no
    // model must be able to answer inside the rules without this class
    // spelling a rule value into code.
    std::string low;
    std::string high;
};

using Referee = std::function<bool(const RefereeQuestion& question,
                                   std::string& answer,
                                   std::string& error)>;

using Choice = logosphere::rules::ProcedureChoice;

class Session {
public:
    Session(kg::KGModule& world, logosphere::dice::DiceService& dice);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Who answers what the rules leave open. There is no default and
    // there will not be one: a stand-in would be policy nobody wrote,
    // applied silently, and indistinguishable afterwards from a
    // judgement somebody made.
    void set_referee(Referee referee) { referee_ = std::move(referee); }

    // Who that was, in words, for the record every decision leaves.
    // Exactly one arbiter: two authorities over one record leaves no
    // way to say which of them wrote it.
    void set_arbiter(std::string arbiter) { arbiter_ = std::move(arbiter); }

    // Create the character and run to the first question. False with
    // `error` set when the procedure, its primitives, or the rule data
    // they need is missing — never a half-made character.
    bool begin(std::string& error);

    bool finished() const { return finished_; }
    const std::string& prompt() const { return prompt_; }
    const std::vector<Choice>& choices() const { return choices_; }

    // Answer the question on the table. Accepts a choice key; an answer
    // that was not offered is refused rather than guessed at.
    bool choose(const std::string& answer, std::string& error);

    kg::EntityID character() const { return character_; }

    // What happened, in the order it happened, one line each. The
    // screen reads it; nothing derives state from it.
    const std::vector<std::string>& log() const { return log_; }

private:
    using Context = logosphere::rules::ProcedurePrimitiveContext;
    using Result = logosphere::rules::ProcedurePrimitiveResult;

    void bind_primitives();
    bool accept(logosphere::rules::ProcedureResult result,
                std::string& error);

    Result roll_characteristics(const Context& context);
    Result narrate_background(const Context& context);
    Result choose_career(const Context& context);
    Result spend_season(const Context& context);
    Result face_moment(const Context& context);

    // A number the book fixes, read where it is used. The NAME is the
    // key; the VALUE is the graph's. A primitive that spelled the value
    // out would be exactly the leak this game exists to not have.
    bool constant(const std::string& name, long long& value,
                  std::string& error) const;

    // The same, for a number the book writes as the physicist does.
    bool constant_real(const std::string& name, double& value,
                       std::string& error) const;

    // Stage, counted and never stored: how many moments of this kind
    // the character has faced, read off the MomentFaced records.
    size_t stage_count(kg::EntityID kind) const;

    // A throw as the book prints it, assembled from the throw's own
    // parts: the short name comes off the characteristic the throw
    // names and the target off the throw. Nothing here knows which
    // characteristics exist or what they are called.
    bool throw_text(kg::EntityID check, std::string& text,
                    std::string& error) const;

    kg::KGModule&                    world_;
    logosphere::dice::DiceService&   dice_;
    logosphere::rules::ProcedurePrimitiveRegistry primitives_;
    logosphere::rules::ProcedureRunner runner_;
    Referee                          referee_;
    std::string                      arbiter_;
    kg::EntityID                     character_ = kg::INVALID_ENTITY;
    kg::EntityID                     procedure_ = kg::INVALID_ENTITY;
    logosphere::rules::ProcedureCursor cursor_;
    std::vector<Choice>              choices_;
    std::string                      prompt_;
    std::vector<std::string>         log_;
    bool                             finished_ = false;
    // The careers the referee offered this character, by key, kept
    // between the question and the answer so the arbiter's record can
    // say what the field was. Cleared when the question is answered.
    std::vector<std::string>         offered_;
    std::string                      question_asked_;
    // Which procedure is running. Character creation comes from the
    // published book's checklist; the season and its moment come from
    // the game's own book; the seam between them is here, because a
    // seed cites one file and the two flows cite different books.
    bool                             in_life_ = false;
};

// The two Procedures this game walks, in order: creation from the
// published book's checklist, then a season of the game's own book.
// Named here rather than in the seed loader because they are the
// game's entry points into its own rules.
inline constexpr const char* kProcedureName = "voyager_chargen";
inline constexpr const char* kLifeProcedureName = "voyager_life";

}  // namespace voyager

#endif  // VOYAGER_SESSION_H
