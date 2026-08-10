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
};

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
    TEST(the_complete_table_validates_before_any_roll_is_consumed);
    TEST(malformed_references_and_context_fail_loudly_without_a_roll);
    TEST(outcome_failure_reuses_the_same_selection_without_rerolling);
    TEST(a_pending_outcome_keeps_the_original_table_selection);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
