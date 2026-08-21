#ifndef LOGOVGER_DOOR_AUTHOR_H
#define LOGOVGER_DOOR_AUTHOR_H

// Voyager V1: the narrative decides which doors a character sees.
//
// One copy, used by both drivers. The windowed game and the headless
// recorder must offer the same doors for the same life, or a tape
// recorded in one does not describe the other.

#include "adjudicator.h"
#include "chargen/chargen.h"

#include <iostream>
#include <string>
#include <vector>

namespace logovger {

// Four is a menu; twenty-four is a catalogue. The number is a design
// choice, not a rule, so it lives here rather than in the graph.
constexpr int kDoorsOffered = 4;

// The rules build the legal set; this narrows it. The model is handed
// every option the rules allow and the life so far, and picks the few
// that make sense for THIS person now. It cannot add one, cannot change
// a throw, and cannot make anything easier: chargen refuses a key the
// rules did not issue.
//
// Equal opportunity, tilted luck. The point is not kindness to a
// character with poor characteristics; it is that a different KIND of
// door opens for them. The hint says so, because a model told only
// "pick some" picks the strongest.
inline bool author_the_doors(Adjudicator& referee, uint64_t seed,
                             const std::vector<Choice>& legal,
                             const CharacterSheet& life,
                             std::vector<Choice>& offered,
                             std::string& error,
                             std::string* said = nullptr) {
    // Fewer legal options than doors: nothing to narrow, and asking a
    // model to choose four of three is a question with no answer.
    if (legal.size() <= static_cast<size_t>(kDoorsOffered)) {
        offered = legal;
        return true;
    }

    Judgment judgment;
    judgment.rule = "Not every door is open to everyone, and the ones "
                    "that are open are not the same doors.";
    judgment.question = "Which of these does this person actually get a "
                        "shot at, right now?";
    judgment.count = kDoorsOffered;
    judgment.hint = "opportunity is equal, luck is tilted: someone out of "
                    "options meets stranger doors, not easier ones, and "
                    "nobody is handed a walk in the park";
    judgment.history = format_life(life);
    for (const auto& choice : legal) judgment.options.push_back(choice.label);

    std::vector<std::string> chosen;
    std::string reason;
    if (!referee.decide(judgment, seed, chosen, reason, error)) return false;

    // Map the names it chose back to the keys the rules issued. A name
    // matching nothing is dropped here rather than passed on, so
    // chargen's refusal is the second line of defence and not the first.
    for (const auto& name : chosen) {
        for (const auto& choice : legal) {
            if (choice.label != name) continue;
            offered.push_back(choice);
            break;
        }
    }
    if (offered.empty()) {
        error = "the narrative named no option the rules offer";
        return false;
    }
    if (said) *said = reason;
    return true;
}

}  // namespace logovger

#endif  // LOGOVGER_DOOR_AUTHOR_H
