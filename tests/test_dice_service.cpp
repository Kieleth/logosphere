// The dice service: the only place randomness becomes fact.
//
// Design under test: docs/RPG_MODULE.md. The properties that matter
// are not "random looks random"; they are the HONESTY properties: the
// same seed replays the same life, streams cannot contaminate each
// other, every roll is citable by id, a malformed expression cannot
// produce a roll, and the bus carries what the journal records.
//
// Headless-core safe.
//
// Usage:
//   ./build/test_dice_service

#undef NDEBUG

#include "logosphere/core/dice_service.h"
#include "logosphere/events/event_bus.h"

#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

using logosphere::dice::DiceExpression;
using logosphere::dice::DiceRoll;
using logosphere::dice::DiceService;

// -------------------------------------------------------------- parsing

void test_the_grammar_is_strict() {
    DiceExpression e;
    check(DiceExpression::parse("2D6", e) && e.count == 2 && e.sides == 6 &&
              e.modifier == 0, "2D6 parses");
    check(DiceExpression::parse("d20", e) && e.count == 1 && e.sides == 20,
          "d20 parses with implicit count 1");
    check(DiceExpression::parse("3d6-1", e) && e.modifier == -1,
          "3d6-1 parses with negative modifier");
    check(DiceExpression::parse("1D6+2", e) && e.modifier == 2,
          "1D6+2 parses");

    // The book writes "1D6 x 10,000 Credits"; the grammar spells it
    // 1D6x10000. Multiplier applies to the whole: (sum + mod) * mult.
    check(DiceExpression::parse("1D6x10000", e) && e.multiplier == 10000 &&
              e.count == 1 && e.sides == 6 && e.modifier == 0,
          "1D6x10000 parses with multiplier");
    check(DiceExpression::parse("2d6+1X10", e) && e.modifier == 1 &&
              e.multiplier == 10, "modifier and multiplier combine");
    check(DiceExpression::parse("2D6", e) && e.multiplier == 1,
          "multiplier defaults to 1");

    // The control half: everything malformed must be refused, because
    // a lenient dice parser is how a typo becomes a different game.
    // The saturation twins: strtol caps a 20-digit modifier at
    // LONG_MAX; unbounded, that wrapped the overflow guard and parsed
    // as a DIFFERENT expression (1D6-1). Regression for that bug.
    const char* bad[] = {"", "D", "2X6", "2D", "2D6+", "2D6 ", " 2D6",
                         "2D6+1x", "0D6", "2D1", "-1D6",
                         "2D6x0", "2D6x-5", "x10", "2D6x1000001",
                         "100D1000x1000000",
                         "1D6+99999999999999999999",
                         "1D6-99999999999999999999",
                         "2D6x", "2D6x5x2", "2D6x+5"};
    bool all_refused = true;
    for (const char* b : bad) {
        DiceExpression tmp;
        if (DiceExpression::parse(b, tmp)) {
            all_refused = false;
            std::cout << "  [measure] wrongly accepted: '" << b << "'"
                      << std::endl;
        }
    }
    check(all_refused, "all twenty-one malformed expressions are refused");

    e = DiceExpression{2, 6, 1};
    check(e.to_string() == "2D6+1", "canonical spelling round-trips");
    e = DiceExpression{1, 6, 0, 10000};
    check(e.to_string() == "1D6x10000",
          "multiplier round-trips, omitted when 1");
}

// -------------------------------------------------------- determinism

void test_same_seed_same_life() {
    DiceService a, b;
    a.seed_stream("chargen", 42);
    b.seed_stream("chargen", 42);
    bool identical = true;
    for (int i = 0; i < 100; ++i) {
        const auto ra = a.roll("2D6", "chargen", "term");
        const auto rb = b.roll("2D6", "chargen", "term");
        if (ra.total != rb.total || ra.values != rb.values) identical = false;
    }
    check(identical, "the same seed replays the same 100 rolls exactly");

    DiceService c;
    c.seed_stream("chargen", 43);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        if (c.roll("2D6", "chargen", "term").total !=
            a.journal()[i].total) { differs = true; break; }
    }
    check(differs, "and a neighbouring seed diverges (the control)");
}

void test_streams_do_not_contaminate() {
    // The property a session depends on: rolling on the world stream
    // must not change what chargen rolls next.
    DiceService a, b;
    a.seed_stream("chargen", 7);
    a.seed_stream("world", 99);
    b.seed_stream("chargen", 7);

    // a interleaves world rolls; b does not.
    std::vector<int> a_chargen, b_chargen;
    for (int i = 0; i < 50; ++i) {
        a_chargen.push_back(a.roll("2D6", "chargen", "x").total);
        a.roll("1D6", "world", "noise");
        b_chargen.push_back(b.roll("2D6", "chargen", "x").total);
    }
    check(a_chargen == b_chargen,
          "interleaving another stream leaves a stream's sequence intact");
}

void test_unseeded_streams_are_deterministic_not_entropic() {
    DiceService a, b;
    const auto ra = a.roll("2D6", "never_seeded", "x");
    const auto rb = b.roll("2D6", "never_seeded", "x");
    check(ra.values == rb.values,
          "an unseeded stream is seed-0 deterministic, never entropy");
}

// ------------------------------------------------------------ honesty

void test_rolls_are_citable_facts() {
    DiceService d;
    d.seed_stream("chargen", 1);
    const auto r1 = d.roll("2D6", "chargen", "survival");
    const auto r2 = d.roll("2D6", "chargen", "commission");
    check(r1.id == 1 && r2.id == 2, "ids are monotonic from 1");

    const DiceRoll* found = d.find(r1.id);
    check(found && found->purpose == "survival" && found->total == r1.total,
          "a roll is retrievable by id, with its purpose");
    check(d.find(0) == nullptr, "id 0 is the never-a-roll sentinel");
    check(d.find(999) == nullptr, "an unknown id is nullptr, not a guess");
    check(d.journal().size() == 2, "the journal holds every roll");
}

void test_malformed_expressions_cannot_roll() {
    DiceService d;
    const auto r = d.roll("2X6", "chargen", "nope");
    check(r.id == 0 && r.values.empty(),
          "a malformed expression produces the id-0 sentinel, not dice");
    check(d.journal().empty(), "and nothing enters the journal");
}

void test_bounds_and_totals() {
    DiceService d;
    d.seed_stream("s", 5);
    bool in_bounds = true, totals_ok = true;
    for (int i = 0; i < 1000; ++i) {
        const auto r = d.roll("3D6+2", "s", "x");
        int sum = 2;
        for (int v : r.values) {
            if (v < 1 || v > 6) in_bounds = false;
            sum += v;
        }
        if (sum != r.total || r.values.size() != 3) totals_ok = false;
    }
    check(in_bounds, "1000 rolls of 3D6: every die in 1..6");
    check(totals_ok, "and every total is the sum plus the modifier");

    // The medical-bill expression: totals are whole multiples in range.
    bool mult_ok = true;
    for (int i = 0; i < 100; ++i) {
        const auto r = d.roll("1D6x10000", "s", "medical care");
        if (r.total < 10000 || r.total > 60000 || r.total % 10000 != 0)
            mult_ok = false;
    }
    check(mult_ok, "100 rolls of 1D6x10000: totals are 10000..60000 in steps");
}

void test_the_distribution_is_dicelike() {
    // Not a statistics suite: a tripwire. 2D6 over 10k rolls has mean
    // 7.0; a broken generator (stuck state, modulo-of-zero) lands far
    // outside 6.8..7.2.
    DiceService d;
    d.seed_stream("s", 12345);
    double sum = 0;
    const int n = 10000;
    for (int i = 0; i < n; ++i) sum += d.roll("2D6", "s", "x").total;
    const double mean = sum / n;
    std::cout << "  [measure] 2D6 mean over " << n << " rolls: " << mean
              << std::endl;
    check(mean > 6.8 && mean < 7.2,
          "the 2D6 mean sits where dice put it (" + std::to_string(mean) + ")");
}

// ---------------------------------------------------------------- bus

void test_the_bus_carries_what_the_journal_records() {
    logosphere::EventBus bus;
    DiceService d;
    d.initialize(&bus);
    d.seed_stream("chargen", 9);

    int events = 0;
    std::string last_expr, last_values;
    int last_total = -1;
    bus.dice_rolls().subscribe(
        [&](const logosphere::ontology::DiceRollEvent& e) {
            ++events;
            last_expr = e.dice_expression.value_or("");
            last_values = e.roll_values.value_or("");
            last_total = e.roll_total.value_or(-1);
        });

    const auto r = d.roll("2D6+1", "chargen", "qualification");
    std::cout << "  [measure] event: expr=" << last_expr << " values="
              << last_values << " total=" << last_total << std::endl;
    check(events == 1, "one roll, one event");
    check(last_expr == "2D6+1", "the event carries the canonical expression");
    check(last_total == r.total, "and the same total the caller received");
    check(!last_values.empty(), "and the individual dice");

    d.roll("2X6", "chargen", "bad");
    check(events == 1, "a refused expression emits nothing");
}

}  // namespace

int main() {
    std::cout << "Dice service (randomness becomes citable fact)" << std::endl;
    test_the_grammar_is_strict();
    test_same_seed_same_life();
    test_streams_do_not_contaminate();
    test_unseeded_streams_are_deterministic_not_entropic();
    test_rolls_are_citable_facts();
    test_malformed_expressions_cannot_roll();
    test_bounds_and_totals();
    test_the_distribution_is_dicelike();
    test_the_bus_carries_what_the_journal_records();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
