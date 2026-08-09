#include "logosphere/core/dice_service.h"

#include "logosphere/events/event_bus.h"

#include <cctype>
#include <cstdlib>

namespace logosphere::dice {

bool DiceExpression::parse(const std::string& text, DiceExpression& out) {
    // Grammar: [count] D sides [(+|-) modifier] [x multiplier],
    // case-insensitive D and x. Strict: no spaces, no trailing junk,
    // all numbers positive.
    const char* p = text.c_str();
    char* end = nullptr;
    long count = 1;
    if (std::isdigit(static_cast<unsigned char>(*p))) {
        count = std::strtol(p, &end, 10);
        p = end;
    }
    if (*p != 'd' && *p != 'D') return false;
    ++p;
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    const long sides = std::strtol(p, &end, 10);
    p = end;
    long modifier = 0;
    if (*p == '+' || *p == '-') {
        const char sign = *p++;
        if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
        modifier = std::strtol(p, &end, 10);
        p = end;
        // Bound before use: strtol saturates silently on huge digit
        // strings (no errno check here by design), and an unbounded
        // modifier would overflow the worst-case guard below.
        if (modifier > 1000000) return false;
        if (sign == '-') modifier = -modifier;
    }
    long multiplier = 1;
    if (*p == 'x' || *p == 'X') {
        ++p;
        if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
        multiplier = std::strtol(p, &end, 10);
        p = end;
    }
    if (*p != '\0') return false;
    if (count < 1 || count > 100 || sides < 2 || sides > 1000) return false;
    if (multiplier < 1 || multiplier > 1000000) return false;
    // The extreme corners of the grammar (100D1000x1000000) would
    // overflow the int totals rolls travel in; refuse any expression
    // whose largest possible total does not fit. Every operand is
    // bounded above, so this arithmetic itself cannot overflow.
    const long long worst =
        (static_cast<long long>(count) * sides + std::llabs(modifier)) *
        multiplier;
    if (worst > 2147483647LL) return false;
    out.count = static_cast<int>(count);
    out.sides = static_cast<int>(sides);
    out.modifier = static_cast<int>(modifier);
    out.multiplier = static_cast<int>(multiplier);
    return true;
}

std::string DiceExpression::to_string() const {
    std::string s = std::to_string(count) + "D" + std::to_string(sides);
    if (modifier > 0) s += "+" + std::to_string(modifier);
    if (modifier < 0) s += std::to_string(modifier);
    if (multiplier != 1) s += "x" + std::to_string(multiplier);
    return s;
}

void DiceService::seed_stream(const std::string& stream, uint64_t seed) {
    // xorshift64* must not sit at state 0; the splitmix step below also
    // decorrelates adjacent seeds, so seed 1 and seed 2 share nothing.
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    streams_[stream] = z ? z : 0x9E3779B97F4A7C15ull;
}

uint64_t DiceService::next_u64(const std::string& stream) {
    auto it = streams_.find(stream);
    if (it == streams_.end()) {
        seed_stream(stream, 0);      // deterministic, never entropy
        it = streams_.find(stream);
    }
    uint64_t x = it->second;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    it->second = x;
    return x * 0x2545F4914F6CDD1Dull;
}

DiceRoll DiceService::roll(const DiceExpression& expr,
                           const std::string& stream,
                           const std::string& purpose) {
    DiceRoll r;
    r.id = next_id_++;
    r.expression = expr;
    r.stream = stream;
    r.purpose = purpose;
    r.total = expr.modifier;
    for (int i = 0; i < expr.count; ++i) {
        // Rejection-free modulo is fine here: sides are tiny against
        // 2^64, the bias is unmeasurable at any table a game rolls.
        const int v = static_cast<int>(next_u64(stream) %
                                       static_cast<uint64_t>(expr.sides)) + 1;
        r.values.push_back(v);
        r.total += v;
    }
    r.total *= expr.multiplier;
    journal_.push_back(r);

    if (bus_) {
        logosphere::ontology::DiceRollEvent e;
        e.event_type = "DICE_ROLL";
        e.roll_id = static_cast<int32_t>(r.id);
        e.dice_expression = expr.to_string();
        std::string vals;
        for (size_t i = 0; i < r.values.size(); ++i) {
            if (i) vals += ",";
            vals += std::to_string(r.values[i]);
        }
        e.roll_values = vals;
        e.roll_total = r.total;
        e.roll_stream = stream;
        e.roll_purpose = purpose;
        bus_->dice_rolls().emit(std::move(e));
    }
    return r;
}

DiceRoll DiceService::roll(const std::string& expr_text,
                           const std::string& stream,
                           const std::string& purpose) {
    DiceExpression expr;
    if (!DiceExpression::parse(expr_text, expr)) {
        return DiceRoll{};           // id 0: never a real roll
    }
    return roll(expr, stream, purpose);
}

const DiceRoll* DiceService::find(uint64_t id) const {
    if (id == 0) return nullptr;
    for (const auto& r : journal_)
        if (r.id == id) return &r;
    return nullptr;
}

} // namespace logosphere::dice
