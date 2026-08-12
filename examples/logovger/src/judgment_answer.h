// Reading a referee's answer, held to the rule that asked for it.
//
// Kept apart from the adjudicator, and free of any LLM dependency, for
// one reason: this is where an unreliable answer is caught. It is the
// seam between something that replies in prose and a rule that must
// not widen, so it belongs where every build profile can test it
// rather than only the one that can talk to a model.
//
// Nothing here guesses. Every way a reply can fail to fit the rule
// returns false with a reason, and leaves nothing chosen.

#ifndef LOGOVGER_JUDGMENT_ANSWER_H
#define LOGOVGER_JUDGMENT_ANSWER_H

#include <string>
#include <vector>

namespace logovger {

// Expects a reply shaped:
//
//   CHOICE: <option>, <option>
//   WHY: <one clause>
//
// Fills `chosen` with exactly `count` distinct entries, each spelled
// as `options` spells it, so a later check compares like with like.
// Matching is case-insensitive and tolerates surrounding space; it is
// not otherwise forgiving. A missing CHOICE line, an option the rule
// does not allow, a repeat, or the wrong number of them each return
// false. `reason` is best-effort: a missing WHY is not an error,
// because the choice is what the rule needs.
bool parse_judgment_answer(const std::string& reply,
                           const std::vector<std::string>& options,
                           int count, std::vector<std::string>& chosen,
                           std::string& reason, std::string& error);

}  // namespace logovger

#endif  // LOGOVGER_JUDGMENT_ANSWER_H
