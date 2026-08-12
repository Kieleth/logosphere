#ifndef LOGOSPHERE_REPLAY_RUN_TAPE_H
#define LOGOSPHERE_REPLAY_RUN_TAPE_H

// Where a run's ANSWERS come from, and how they are kept.
//
// RunRecorder captures what the engine did. This captures what was
// decided, which is the other half and the half that makes a run
// repeatable. The engine emits its facts and a recorder can take them
// for free; it never sees a decision, because a decision is a value
// handed to a game and dropped. So a game routes its decisions through
// here and gets recording, replay and fuzzing without writing any of
// the three.
//
// A decision is anything the run cannot derive for itself:
//
//   - what a player chose at a prompt,
//   - what a language model replied,
//   - which seed a stream started from.
//
// THREE SOURCES, ONE INTERFACE. A game asks; where the answer comes
// from is the caller's choice and the game cannot tell the difference:
//
//   LiveInput    asks whoever is really there (a UI, a model, stdin)
//   TapedInput   replays what a previous run answered, in order
//   RandomInput  invents an answer from a seed, for fuzzing
//
// The third is why this is not just a replay file. A seeded random
// source explores a game's decision space without a human and without
// a model, and because it is seeded, an interesting run it finds can
// be replayed exactly.
//
// A MISMATCH IS AN ABORT. When a tape no longer fits the code, replay
// stops and says which site it expected and which it was asked for. It
// never falls back to asking live, because a replay that quietly
// becomes a live run is a test that reports success for a build it
// never exercised.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace logosphere::replay {

// One decision, described well enough to record, match and answer.
struct Ask {
    // Stable name for this decision point, owned by the game:
    // "chargen.career", "adjudicator". Replay matches on this, so it
    // must not drift casually.
    std::string site;

    // What a human would have been shown. Recorded so a tape reads
    // like a transcript. Never matched on: rewording a prompt must not
    // invalidate a tape.
    std::string prompt;

    // The keys that are acceptable. Empty means free-form text. A
    // taped or random answer outside this set is refused, which is how
    // a tape recorded against different rules is caught.
    std::vector<std::string> offered;
};

class InputSource {
public:
    virtual ~InputSource() = default;

    // Fills `answer`. Returns false with `error` set when it cannot,
    // which is always fatal to the run: there is no default answer to
    // a question the rules asked.
    virtual bool answer(const Ask& ask, std::string& answer,
                        std::string& error) = 0;

    // A seed for a named stream. Live sources invent one, taped
    // sources return what was recorded, so dice replay exactly.
    virtual uint64_t seed(const std::string& stream, uint64_t fallback) = 0;
};

// Asks whoever is really there. The callback is the game's own way of
// getting an answer: a prompt on screen, a model, a line of stdin.
class LiveInput : public InputSource {
public:
    using Asker = std::function<bool(const Ask&, std::string&, std::string&)>;
    explicit LiveInput(Asker asker) : asker_(std::move(asker)) {}

    bool answer(const Ask& ask, std::string& out, std::string& error) override;
    uint64_t seed(const std::string& stream, uint64_t fallback) override;

private:
    Asker asker_;
};

// Invents answers from a seed. Picks uniformly from `offered`; for a
// free-form ask it returns an empty string, because inventing prose is
// not something a generic engine can do, and a game that needs it
// should stub that site itself.
//
// Deterministic: the same seed answers the same way, so a fuzz run
// that finds something interesting can be replayed by seed alone.
class RandomInput : public InputSource {
public:
    explicit RandomInput(uint64_t seed) : state_(seed ? seed : 1) {}

    bool answer(const Ask& ask, std::string& out, std::string& error) override;
    uint64_t seed(const std::string& stream, uint64_t fallback) override;

private:
    uint64_t next();
    uint64_t state_;
};

// Replays a recorded run, in order. Refuses anything that does not fit
// rather than guessing.
class TapedInput : public InputSource {
public:
    // Reads a tape written by RunTape. `error` is set and the source
    // is left unusable when the file cannot be read.
    static std::unique_ptr<TapedInput> open(const std::string& path,
                                            std::string& error);

    bool answer(const Ask& ask, std::string& out, std::string& error) override;
    uint64_t seed(const std::string& stream, uint64_t fallback) override;

    // How many answers were consumed, and whether the tape ran out
    // early or was left with more. Both are divergence.
    size_t consumed() const { return at_; }
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string kind;     // "ask" or "seed"
        std::string site;     // or stream name
        std::string answer;   // or the seed as text
    };
    std::vector<Entry> entries_;
    size_t at_ = 0;
};

// Wraps a source and writes down every answer it gave, so any run can
// become a tape. Recording a taped run reproduces the tape, which is
// the cheapest possible check that replay is faithful.
class RunTape {
public:
    RunTape(InputSource& source, std::string path);
    ~RunTape();

    // Ask, record, return. The game calls this and nothing else.
    bool ask(const Ask& ask, std::string& answer, std::string& error);
    uint64_t seed(const std::string& stream, uint64_t fallback);

    // Writes the tape. Called by the destructor; safe twice.
    void flush();

    size_t entries() const { return lines_.size(); }
    const std::string& path() const { return path_; }

private:
    InputSource& source_;
    std::string path_;
    std::vector<std::string> lines_;
    bool flushed_ = false;
};

}  // namespace logosphere::replay

#endif  // LOGOSPHERE_REPLAY_RUN_TAPE_H
