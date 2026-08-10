// DiceService: the only place randomness becomes fact.
//
// Design: docs/RPG_MODULE.md ("the referee cannot roll"). Every roll
// is seeded, journaled, and citable by monotonic id; consumers, LLM
// referees included, receive results as facts and can never assert
// one. Named streams are independently seeded so a session replays
// deterministically: the chargen stream replays the same life, the
// world stream the same subsector, regardless of interleaving.
//
// Generic engine mechanism, not RPG-module code: any game that wants
// provenance on its randomness uses this. Headless-core safe.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace logosphere { class EventBus; }

namespace logosphere::dice {

// "NdS+M" / "NdS-M" / "dS", with an optional "xK" multiplier suffix
// for book expressions like "1D6x10000" (Cepheus: 1D6 x 10,000 Cr).
// Canonical spelling is upper D, lower x, modifier and multiplier
// only when they matter ("2D6", "1D6+2", "1D6x10000").
// Total = (dice sum + modifier) * multiplier.
struct DiceExpression {
    int count = 1;
    int sides = 6;
    int modifier = 0;
    int multiplier = 1;

    // Strict parse: returns false and leaves out untouched on anything
    // malformed. "2D6", "d20", "3d6-1" are valid; "2X6", "D", "" are not.
    static bool parse(const std::string& text, DiceExpression& out);
    bool is_valid() const;
    std::string to_string() const;
};

struct DiceRoll {
    uint64_t id = 0;                 // monotonic, citable
    DiceExpression expression;
    std::vector<int> values;         // individual dice, roll order
    int total = 0;                   // (sum + modifier) * multiplier
    std::string stream;
    std::string purpose;             // the roller's words
};

class DiceService {
public:
    // Bus is optional: without one, rolls are still recorded and
    // returned, just not broadcast.
    void initialize(logosphere::EventBus* bus) { bus_ = bus; }

    // (Re)seed a named stream. A stream that is rolled before being
    // seeded gets seed 0 deterministically; reproducibility is the
    // point, so there is no entropy fallback anywhere in this class.
    void seed_stream(const std::string& stream, uint64_t seed);

    // Roll. Invalid expressions return the id-0 sentinel without
    // touching the stream, journal, id sequence, or event bus. Valid
    // rolls emit DiceRollEvent when a bus is wired and are recorded in
    // the in-memory journal either way.
    DiceRoll roll(const DiceExpression& expr, const std::string& stream,
                  const std::string& purpose);
    // Convenience: parse + roll. Returns a roll with id 0 and empty
    // values when the expression is malformed; a zero id is never a
    // real roll, so callers can test for it.
    DiceRoll roll(const std::string& expr, const std::string& stream,
                  const std::string& purpose);

    // The journal: every roll this session, oldest first.
    const std::vector<DiceRoll>& journal() const { return journal_; }
    // A specific roll by id, nullptr if unknown (or id 0).
    const DiceRoll* find(uint64_t id) const;

    size_t stream_count() const { return streams_.size(); }

private:
    uint64_t next_u64(const std::string& stream);

    logosphere::EventBus* bus_ = nullptr;
    std::unordered_map<std::string, uint64_t> streams_;   // xorshift64* state
    std::vector<DiceRoll> journal_;
    uint64_t next_id_ = 1;           // 0 is the never-a-roll sentinel
};

} // namespace logosphere::dice
