#include "logosphere/core/dice_service.h"

#include "logosphere/events/event_bus.h"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace logosphere::dice {

DiceTransaction::DiceTransaction(DiceTransaction&& other) noexcept
    : service_(other.service_) {
    other.service_ = nullptr;
}

DiceTransaction::~DiceTransaction() {
    if (service_) service_->rollback_transaction();
}

void DiceTransaction::commit() {
    if (!service_) {
        throw std::logic_error("DiceTransaction is no longer active");
    }
    service_->commit_transaction();
    service_ = nullptr;
}

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
    const auto int_max = std::numeric_limits<int>::max();
    const auto int_min = std::numeric_limits<int>::min();
    if (count > int_max || sides > int_max || modifier < int_min ||
        modifier > int_max || multiplier > int_max) return false;

    DiceExpression parsed;
    parsed.count = static_cast<int>(count);
    parsed.sides = static_cast<int>(sides);
    parsed.modifier = static_cast<int>(modifier);
    parsed.multiplier = static_cast<int>(multiplier);
    if (!parsed.is_valid()) return false;
    out = parsed;
    return true;
}

bool DiceExpression::is_valid() const {
    if (count < 1 || count > 100 || sides < 2 || sides > 1000 ||
        modifier < -1000000 || modifier > 1000000 ||
        multiplier < 1 || multiplier > 1000000) return false;

    const int64_t modifier_magnitude = modifier < 0
        ? -static_cast<int64_t>(modifier)
        : static_cast<int64_t>(modifier);
    const int64_t worst =
        (static_cast<int64_t>(count) * sides + modifier_magnitude) *
        multiplier;
    return worst <= std::numeric_limits<int>::max();
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

DiceTransaction DiceService::begin_transaction() {
    if (transaction_) {
        throw std::logic_error("DiceService transactions cannot nest");
    }
    transaction_ = TransactionState{streams_, journal_.size(), next_id_};
    return DiceTransaction(*this);
}

void DiceService::emit_roll_event(const DiceRoll& roll) {
    if (!bus_) return;
    logosphere::ontology::DiceRollEvent event;
    event.event_type = "DICE_ROLL";
    event.roll_id = static_cast<int32_t>(roll.id);
    event.dice_expression = roll.expression.to_string();
    std::string values;
    for (size_t index = 0; index < roll.values.size(); ++index) {
        if (index) values += ",";
        values += std::to_string(roll.values[index]);
    }
    event.roll_values = values;
    event.roll_total = roll.total;
    event.roll_stream = roll.stream;
    event.roll_purpose = roll.purpose;
    bus_->dice_rolls().emit(std::move(event));
}

void DiceService::commit_transaction() {
    if (!transaction_) {
        throw std::logic_error("DiceService has no active transaction");
    }
    const size_t first_roll = transaction_->journal_size;
    transaction_.reset();
    for (size_t index = first_roll; index < journal_.size(); ++index) {
        emit_roll_event(journal_[index]);
    }
}

void DiceService::rollback_transaction() {
    if (!transaction_) return;
    streams_ = std::move(transaction_->streams);
    journal_.resize(transaction_->journal_size);
    next_id_ = transaction_->next_id;
    transaction_.reset();
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
    if (!expr.is_valid()) return DiceRoll{};

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

    if (!transaction_) emit_roll_event(r);
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
