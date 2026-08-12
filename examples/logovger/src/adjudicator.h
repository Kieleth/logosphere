// The adjudicator: the judgments the book hands to a person.
//
// Some rules do not decide everything. Cepheus aging reduces "two
// physical characteristics by 2" without saying which two; the mishap
// table turns on the referee's approval. The dice do not answer those
// and neither does the engine, which supplies the eligible options and
// then refuses to pick among them.
//
// This is the referee's seat. It reads who the character has become
// and answers, and the engine validates the answer against the rule
// before anything moves.
//
// It is NOT the narrator, and the difference is the point:
//
//   The narrator produces words. It never changes the world, never
//   blocks, and a dead API costs you flavour rather than play.
//
//   The adjudicator produces DECISIONS. A rule cannot proceed without
//   one, so it BLOCKS, and there is no fallback. No key and no local
//   server means the game refuses to run.
//
// The absent fallback is deliberate. A stand-in such as "take the
// highest first" would be policy the book never printed, invented by
// us, applied silently, and indistinguishable in the finished
// character from a judgment someone actually made. A missing model is
// a missing referee, and the honest response is to stop. Tests inject
// their own stub selector; the production path never carries one.

#ifndef LOGOVGER_ADJUDICATOR_H
#define LOGOVGER_ADJUDICATOR_H

#include "logosphere/llm/llm_system_http.h"

#include <memory>
#include <string>
#include <vector>

namespace logovger {

// One judgment the rule left open.
struct Judgment {
    std::string              question;  // "which two decline, and why?"
    std::string              rule;      // the book's own sentence
    std::vector<std::string> options;   // what may be chosen
    int                      count = 0; // exactly how many
    std::string              history;   // who this person has become
    std::string              hint;      // a lean, never a constraint
};

class Adjudicator {
public:
    // Fails loudly when there is no model to ask. There is no mode in
    // which this class works without one.
    bool initialize(const std::string& lore_path, std::string& error);

    bool ready() const { return llm_ != nullptr; }

    // Blocks until the model answers, then holds it to the rule: the
    // reply must name exactly `count` distinct options drawn from
    // `options`. A timeout, a transport failure, or an answer that
    // does not fit returns false with a filled error. It never
    // substitutes a choice of its own.
    //
    // `reason` receives the one clause the model gave for the choice,
    // for the life log. An empty reason is not an error; a wrong
    // choice is.
    bool decide(const Judgment& judgment, uint64_t seed,
                std::vector<std::string>& chosen, std::string& reason,
                std::string& error);

    // Reading the answer lives in judgment_answer.h, free of any LLM
    // dependency, so the check that stops a loose reply widening a
    // rule is testable in every build profile.

private:
    std::string build_prompt(const Judgment& judgment) const;
    std::string cache_key(const Judgment& judgment, uint64_t seed) const;

    std::unique_ptr<Logosphere::LLMSystemHTTP> llm_;
    std::string lore_;
    std::string backend_;
    std::string model_;
    std::string voice_version_;
    std::string cache_dir_;
    int         timeout_ms_ = 30000;
};

}  // namespace logovger

#endif  // LOGOVGER_ADJUDICATOR_H
