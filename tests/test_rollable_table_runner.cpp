// Two-phase rollable-table execution: selection is a recorded fact and
// outcome application is an explicit, separate operation.

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/outcome_executor.h"
#include "logosphere/rules/rollable_table_runner.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++tests_passed; std::cout << "PASS\n"; }               \
    catch (const std::exception& error) {                                 \
        ++tests_failed; std::cout << "FAIL: " << error.what() << '\n';   \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

namespace {

namespace rules = logosphere::rules;

static_assert(std::is_copy_assignable_v<rules::RollableTableSelection>);
static_assert(!std::is_constructible_v<
              rules::RollableTableSelection, kg::EntityID, kg::EntityID,
              kg::EntityID, logosphere::dice::DiceRoll>);

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    logosphere::EventBus events;
    logosphere::dice::DiceService dice;
    kg::EntityID table = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        dice.initialize(&events);
        dice.seed_stream("table", 17);
        table = world.createEntity("RollableTable");
        const auto expression = world.createEntity("DiceExpression");
        world.setProperty(expression, "dice_count", "1");
        world.setProperty(expression, "dice_sides", "6");
        world.setProperty(table, "dice", std::to_string(expression));
    }

    kg::EntityID outcome(const std::string& type = "NoEffect") {
        return world.createEntity(type);
    }

    kg::EntityID row(int low, int high, kg::EntityID outcome) {
        const auto id = world.createEntity("TableEntry");
        world.setProperty(id, "roll_min", std::to_string(low));
        world.setProperty(id, "roll_max", std::to_string(high));
        world.setProperty(id, "outcome", std::to_string(outcome));
        world.createRelation(table, "HAS_PART", id);
        return id;
    }

    // A band the book prints open-ended ("1+"): a roll_min, no
    // roll_max at all, and roll_max_unbounded saying that is deliberate.
    kg::EntityID open_row(int low, kg::EntityID outcome) {
        const auto id = world.createEntity("TableEntry");
        world.setProperty(id, "roll_min", std::to_string(low));
        world.setProperty(id, "roll_max_unbounded", "true");
        world.setProperty(id, "outcome", std::to_string(outcome));
        world.createRelation(table, "HAS_PART", id);
        return id;
    }

    // The other end: a band written "N or less". The row still states
    // the figure the book prints, and roll_min_unbounded says it also
    // catches everything under it.
    kg::EntityID open_bottom_row(int low, int high, kg::EntityID outcome) {
        const auto id = world.createEntity("TableEntry");
        world.setProperty(id, "roll_min", std::to_string(low));
        world.setProperty(id, "roll_max", std::to_string(high));
        world.setProperty(id, "roll_min_unbounded", "true");
        world.setProperty(id, "outcome", std::to_string(outcome));
        world.createRelation(table, "HAS_PART", id);
        return id;
    }
};

// The mirror of the open top, and the reason it exists: a negative DM
// can undershoot the lowest figure a table prints. Cepheus's aging
// table does not write its bottom row that way, so nothing in the
// shipped seeds uses this; a book that does write "N or less" is now
// expressible without a procedure knowing which table it is.
void an_open_ended_bottom_band_catches_every_total_below_it() {
    Fixture f;
    const auto floor_outcome = f.outcome();
    const auto rest_outcome = f.outcome();
    const auto floor_row = f.open_bottom_row(-2, -2, floor_outcome);
    const auto rest = f.row(-1, 8, rest_outcome);

    // 2D6 shifted down by four reaches -2..8, so the floor row is what
    // stands between the table and an uncovered total.
    rules::RollableTableRunner runner(f.world, f.dice);
    const auto result = runner.select(f.table, "table", "fixture", -4);
    REQUIRE(result.ok(),
            "an open-ended bottom band must be rollable: " + result.error);
    const auto& selected = *result.selection;
    const bool low = selected.roll().total <= -2;
    REQUIRE(selected.row() == (low ? floor_row : rest) &&
                selected.outcome() == (low ? floor_outcome : rest_outcome),
            "every total at or below the floor band's figure selects it");

    // The control: without the flag the same table has a hole, and the
    // runner refuses it rather than rolling into nothing.
    Fixture bare;
    bare.row(-2, -2, bare.outcome());
    bare.row(-1, 8, bare.outcome());
    rules::RollableTableRunner bare_runner(bare.world, bare.dice);
    const auto refused =
        bare_runner.select(bare.table, "table", "fixture", -5);
    REQUIRE(!refused.ok() &&
                refused.error.find("no row for reachable total") !=
                    std::string::npos,
            "a closed bottom the DM can undershoot is refused: " +
                refused.error);
    REQUIRE(bare.dice.journal().empty(),
            "and refused before any roll is spent");
}

// Cepheus writes the top of the aging table as "1+", with no upper
// figure. Before roll_max_unbounded was honoured here the whole table
// was unrollable: the missing roll_max failed as a malformed row.
void an_open_ended_band_catches_every_total_above_it() {
    Fixture f;
    const auto low_outcome = f.outcome();
    const auto open_outcome = f.outcome();
    const auto low = f.row(1, 3, low_outcome);
    const auto open = f.open_row(4, open_outcome);

    rules::RollableTableRunner runner(f.world, f.dice);
    const auto result = runner.select(f.table, "table", "fixture");
    REQUIRE(result.ok(),
            "an open-ended top band must be rollable: " + result.error);
    const auto& selected = *result.selection;
    const bool high = selected.roll().total >= 4;
    REQUIRE(selected.row() == (high ? open : low) &&
                selected.outcome() == (high ? open_outcome : low_outcome),
            "every total at or above the open band's floor selects it");

    // The control: unbounded is the only thing that excuses a missing
    // roll_max. Without the flag the same row is still malformed, so a
    // typo cannot quietly become an open band.
    Fixture bare;
    bare.row(1, 3, bare.outcome());
    const auto broken = bare.world.createEntity("TableEntry");
    bare.world.setProperty(broken, "roll_min", "4");
    bare.world.setProperty(broken, "outcome",
                           std::to_string(bare.outcome()));
    bare.world.createRelation(bare.table, "HAS_PART", broken);
    rules::RollableTableRunner bare_runner(bare.world, bare.dice);
    const auto refused = bare_runner.select(bare.table, "table", "fixture");
    REQUIRE(!refused.ok() &&
                refused.error.find("roll_max") != std::string::npos,
            "a row with no roll_max and no unbounded flag stays malformed");
    REQUIRE(bare.dice.journal().empty(),
            "the malformed table must be refused before any roll");
}

// Aging is "2D6 with total terms as a negative DM". The DM belongs to
// the roll, not to the table, and coverage has to be validated against
// the totals the modified roll can actually reach.
void a_dice_modifier_shifts_both_the_roll_and_its_validation() {
    Fixture f;
    const auto below = f.outcome();
    const auto above = f.outcome();
    f.row(-2, 0, below);
    f.row(1, 3, above);

    rules::RollableTableRunner runner(f.world, f.dice);
    const auto result = runner.select(f.table, "table", "fixture", -3);
    REQUIRE(result.ok(),
            "a table covering the shifted band must roll: " + result.error);
    const auto total = result.selection->roll().total;
    REQUIRE(total >= -2 && total <= 3,
            "1D6-3 must land in -2..3, got " + std::to_string(total));
    REQUIRE(result.selection->outcome() == (total <= 0 ? below : above),
            "the modified total selects the row, not the bare dice");

    // The control: the same table that is complete for an unmodified
    // 1D6 is incomplete once a DM moves the band, and must be refused
    // rather than rolled into a hole.
    Fixture unshifted;
    unshifted.row(1, 6, unshifted.outcome());
    rules::RollableTableRunner unshifted_runner(unshifted.world,
                                                unshifted.dice);
    const auto refused =
        unshifted_runner.select(unshifted.table, "table", "fixture", -3);
    REQUIRE(!refused.ok() &&
                refused.error.find("reachable total") != std::string::npos,
            "a DM that can reach an uncovered total must be refused");
    REQUIRE(unshifted.dice.journal().empty(),
            "the incomplete table must be refused before any roll");

    // And with no modifier that same table is fine, so the refusal
    // above is the DM's doing and not a broken fixture.
    Fixture plain;
    plain.row(1, 6, plain.outcome());
    rules::RollableTableRunner plain_runner(plain.world, plain.dice);
    REQUIRE(plain_runner.select(plain.table, "table", "fixture").ok(),
            "the unmodified roll still covers the same table");
}

void selection_records_the_roll_and_returns_the_exact_row_and_outcome() {
    Fixture f;
    const auto low_outcome = f.outcome();
    const auto high_outcome = f.outcome();
    const auto low = f.row(1, 3, low_outcome);
    const auto high = f.row(4, 6, high_outcome);
    int events = 0;
    f.events.dice_rolls().subscribe(
        [&](const logosphere::ontology::DiceRollEvent&) { ++events; });

    rules::RollableTableRunner runner(f.world, f.dice);
    const auto result = runner.select(f.table, "table", "fixture");
    REQUIRE(result.ok() && result.selection.has_value(),
            "a complete table must produce one selection: " + result.error);
    const auto& selected = *result.selection;
    const bool lower = selected.roll().total <= 3;
    REQUIRE(selected.table() == f.table &&
                selected.row() == (lower ? low : high) &&
                selected.outcome() == (lower ? low_outcome : high_outcome),
            "the returned row and outcome must match the recorded total");
    REQUIRE(selected.roll().id == 1 && f.dice.journal().size() == 1 &&
                f.dice.find(selected.roll().id) != nullptr && events == 1,
            "table selection must publish exactly one citable dice fact");
}

void the_complete_table_validates_before_any_roll_is_consumed() {
    Fixture wrong_part;
    wrong_part.row(1, 6, wrong_part.outcome());
    wrong_part.world.createRelation(
        wrong_part.table, "HAS_PART",
        wrong_part.world.createEntity("Entity"));
    rules::RollableTableRunner wrong_part_runner(
        wrong_part.world, wrong_part.dice);
    const auto wrong = wrong_part_runner.select(
        wrong_part.table, "table", "fixture");
    REQUIRE(!wrong.ok() && wrong.error.find("not TableEntry") !=
                std::string::npos && wrong_part.dice.journal().empty(),
            "a late wrong part must fail before the table roll");

    Fixture overlap;
    overlap.row(1, 4, overlap.outcome());
    overlap.row(4, 6, overlap.outcome());
    rules::RollableTableRunner overlap_runner(overlap.world, overlap.dice);
    const auto ambiguous = overlap_runner.select(
        overlap.table, "table", "fixture");
    REQUIRE(!ambiguous.ok() && ambiguous.error.find("overlap") !=
                std::string::npos && overlap.dice.journal().empty(),
            "overlapping bands must fail before randomness becomes fact");

    Fixture gap;
    gap.row(1, 2, gap.outcome());
    gap.row(4, 6, gap.outcome());
    rules::RollableTableRunner gap_runner(gap.world, gap.dice);
    const auto incomplete = gap_runner.select(
        gap.table, "table", "fixture");
    REQUIRE(!incomplete.ok() && incomplete.error.find("no row for reachable") !=
                std::string::npos && gap.dice.journal().empty(),
            "every reachable dice total must have exactly one row");
}

void malformed_references_and_context_fail_loudly_without_a_roll() {
    Fixture f;
    f.row(1, 6, f.outcome());
    rules::RollableTableRunner runner(f.world, f.dice);
    const auto no_stream = runner.select(f.table, "", "fixture");
    REQUIRE(!no_stream.ok() && no_stream.error.find("stream") !=
                std::string::npos && f.dice.journal().empty(),
            "a selection without a dice stream is not citable");

    f.world.setProperty(f.table, "dice", "999999");
    const auto missing_dice = runner.select(f.table, "table", "fixture");
    REQUIRE(!missing_dice.ok() && missing_dice.error.find("DiceExpression") !=
                std::string::npos && f.dice.journal().empty(),
            "a dangling table dice reference must fail loudly");
}

void outcome_failure_reuses_the_same_selection_without_rerolling() {
    Fixture f;
    const auto malformed = f.outcome("AdvanceSkill");
    f.row(1, 6, malformed);
    const auto target = f.world.createEntity("Entity");
    rules::RollableTableRunner runner(f.world, f.dice);
    const auto selected = runner.select(f.table, "table", "fixture");
    REQUIRE(selected.ok(), "selection must not interpret its outcome");

    rules::OutcomeExecutor executor(f.world, f.dice);
    const rules::OutcomeContext context{target, "table", "fixture outcome"};
    const auto first = executor.apply(selected.selection->outcome(), context);
    const auto second = executor.apply(selected.selection->outcome(), context);
    REQUIRE(first.status == rules::OutcomeStatus::FAILED &&
                second.status == rules::OutcomeStatus::FAILED &&
                f.dice.journal().size() == 1 &&
                f.dice.journal().front().id == selected.selection->roll().id,
            "outcome failure and retry must not reroll table selection");
}

void a_pending_outcome_keeps_the_original_table_selection() {
    Fixture f;
    const auto child = f.outcome();
    const auto choice = f.outcome("OutcomeChoice");
    f.world.setProperty(choice, "choice_authority", "player");
    const auto option = f.world.createEntity("OutcomeOption");
    f.world.setProperty(option, "option_index", "0");
    f.world.setProperty(option, "option_label", "Accept");
    f.world.setProperty(option, "outcome", std::to_string(child));
    f.world.createRelation(choice, "HAS_PART", option);
    f.row(1, 6, choice);
    const auto target = f.world.createEntity("Entity");

    rules::RollableTableRunner runner(f.world, f.dice);
    const auto selected = runner.select(f.table, "table", "fixture");
    rules::OutcomeExecutor executor(f.world, f.dice);
    const rules::OutcomeContext context{target, "table", "fixture outcome"};
    const auto pending = executor.apply(selected.selection->outcome(), context);
    const auto applied = executor.apply(
        selected.selection->outcome(), context, {{choice, 0}});
    REQUIRE(pending.status == rules::OutcomeStatus::PENDING_CHOICE &&
                applied.status == rules::OutcomeStatus::APPLIED &&
                f.dice.journal().size() == 1 &&
                f.dice.journal().front().id == selected.selection->roll().id,
            "suspension and resume must retain the original table fact");
}

}  // namespace

int main() {
    std::cout << "Rollable table runner\n";
    TEST(selection_records_the_roll_and_returns_the_exact_row_and_outcome);
    TEST(an_open_ended_band_catches_every_total_above_it);
    TEST(an_open_ended_bottom_band_catches_every_total_below_it);
    TEST(a_dice_modifier_shifts_both_the_roll_and_its_validation);
    TEST(the_complete_table_validates_before_any_roll_is_consumed);
    TEST(malformed_references_and_context_fail_loudly_without_a_roll);
    TEST(outcome_failure_reuses_the_same_selection_without_rerolling);
    TEST(a_pending_outcome_keeps_the_original_table_selection);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
