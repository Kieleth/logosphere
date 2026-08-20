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
    // A site with no offered set wants text, and inventing text is not
    // something a generic engine can do: it has no idea what a
    // plausible answer looks like in your game. So a game that fuzzes
    // free-form sites (a model's reply, a typed command) supplies this
    // and owns what a synthetic answer means. The roll is handed over
    // so the stub can vary deterministically with the run.
    //
    // Without it, a free-form ask answers empty, which is honest and
    // usually useless.
    using FreeForm =
        std::function<std::string(const Ask& ask, uint64_t roll)>;

    explicit RandomInput(uint64_t seed, FreeForm free_form = {})
        : state_(seed ? seed : 1), free_form_(std::move(free_form)) {}

    bool answer(const Ask& ask, std::string& out, std::string& error) override;
    uint64_t seed(const std::string& stream, uint64_t fallback) override;

private:
    uint64_t next();
    uint64_t state_;
    FreeForm free_form_;
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

    // ---- the untaken branches -------------------------------------
    //
    // A recorded run is one path, and the interesting question about a
    // path is what else it could have done. Every ask records the keys
    // that were legal at that moment, so a tape carries its own forks:
    // at entry i the run answered X and could equally have answered
    // any other member of `offered`.
    //
    // This is what makes a set of tapes a graph rather than a pile of
    // transcripts. Two runs that answered identically for three terms
    // and parted at the fourth share a trunk; the fork below produces
    // the second branch from the first without replaying a thing by
    // hand.
    //
    // `offered` was NOT recorded before 2026-08-19, so a tape written
    // by an older build reports no alternatives rather than lying
    // about having none. `records_alternatives()` tells the two apart.
    struct Fork {
        size_t index = 0;                    // which entry
        std::string site;
        std::string prompt;
        std::string taken;                   // what the run answered
        std::vector<std::string> untaken;    // what it could have
    };

    // Every decision that had at least one other legal answer.
    std::vector<Fork> forks() const;

    // False for a tape from a build that did not record `offered`, so
    // "no forks" cannot be confused with "we cannot see the forks".
    bool records_alternatives() const { return records_offered_; }

private:
    friend class ForkedInput;
    struct Entry {
        std::string kind;     // "ask" or "seed"
        std::string site;     // or stream name
        std::string answer;   // or the seed as text
        std::string prompt;
        std::vector<std::string> offered;    // empty = free-form or old tape
    };
    std::vector<Entry> entries_;
    size_t at_ = 0;
    bool records_offered_ = false;
};

// The counterfactual. Replays a tape up to `at`, answers that one
// decision differently, and hands every question after it to another
// source.
//
// Everything downstream of a decision is DERIVED from the answers and
// the seed, and the seed is itself entry 0 of the tape, so a fork
// inherits it by replaying the prefix. That is what makes "what if she
// had taken the commission" a computable question rather than a
// rewrite: the trunk is not re-decided, it is re-run.
//
// `then` answers everything past the fork. Pass a RandomInput to let
// the rules play it out, a LiveInput to let a model or a person take
// it from there, or another TapedInput to splice two runs.
class ForkedInput : public InputSource {
public:
    // Fails when `at` is not an ask, or when `instead` was not one of
    // the answers that decision offered. A fork onto an answer the
    // rules never allowed is not a counterfactual, it is a fiction.
    static std::unique_ptr<ForkedInput> create(
        std::unique_ptr<TapedInput> trunk, size_t at,
        const std::string& instead, InputSource& then, std::string& error);

    bool answer(const Ask& ask, std::string& out, std::string& error) override;
    uint64_t seed(const std::string& stream, uint64_t fallback) override;

    // Where the branch left the trunk, and whether it has yet.
    size_t fork_at() const { return at_; }
    bool diverged() const { return past_; }

private:
    ForkedInput(std::unique_ptr<TapedInput> trunk, size_t at,
                std::string instead, InputSource& then)
        : trunk_(std::move(trunk)), at_(at), instead_(std::move(instead)),
          then_(then) {}

    std::unique_ptr<TapedInput> trunk_;
    size_t at_ = 0;
    std::string instead_;
    InputSource& then_;
    size_t seen_ = 0;      // asks answered so far
    bool past_ = false;    // taken the fork yet
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
