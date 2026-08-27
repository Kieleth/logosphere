// The referee, when the referee is a model.
//
// NO API, NO GAME. This class fails at startup when there is no key,
// and the game stops there rather than starting in a reduced mode. The
// reason is not purity. The two questions it answers cannot be derived
// from the rules, so a stand-in would be policy nobody wrote, applied
// silently, and afterwards indistinguishable on the finished character
// from a judgement somebody made. A missing referee is a missing
// referee, and the honest response is to say so and stop.
//
// It is one of SEVERAL things that can hold the referee's chair. The
// session takes a std::function and does not know which: a tape holds
// it during replay, a seeded generator holds it while fuzzing, and this
// holds it when somebody is actually playing. That is the whole reason
// a recorded life replays without calling a model.
//
// It BLOCKS. A rule cannot move until the call comes back, so there is
// nothing useful to do meanwhile and pretending otherwise would only
// hide the wait.

#ifndef VOYAGER_MODEL_REFEREE_H
#define VOYAGER_MODEL_REFEREE_H

#include "session.h"

#include <memory>
#include <string>

namespace Logosphere {
class LLMSystemHTTP;
}

namespace voyager {

class ModelReferee {
public:
    ModelReferee();
    ~ModelReferee();

    // The brief is the only thing this referee knows about the
    // universe and about its own job. No brief, no referee: prose
    // invented from nowhere is worse than none.
    bool initialize(const std::string& brief_path, std::string& error);

    bool ready() const { return llm_ != nullptr; }

    // Who is deciding, readable, for the record every decision leaves.
    // Never a hash: an identity you cannot read back is a checksum, not
    // provenance.
    std::string who() const;

    // Answer a question the rules left open. False with `error` set on
    // a transport failure, a timeout, or an empty reply. It never
    // substitutes an answer of its own.
    bool answer(const RefereeQuestion& question, std::string& out,
                std::string& error);

private:
    std::unique_ptr<Logosphere::LLMSystemHTTP> llm_;
    std::string brief_;
    std::string backend_;
    std::string model_;
    int timeout_ms_ = 45000;
    int reply_budget_ = 1024;
};

}  // namespace voyager

#endif  // VOYAGER_MODEL_REFEREE_H
