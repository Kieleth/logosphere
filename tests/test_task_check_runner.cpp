// Declarative TaskCheck execution. The check names the target attribute,
// modifier lookup table, integer result column, dice, and threshold.

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/task_check_runner.h"
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

static_assert(std::is_copy_assignable_v<rules::TaskCheckExecution>);
static_assert(!std::is_default_constructible_v<rules::TaskCheckExecution>);

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    kg::OntologyRegistry game("test://task-check-runner");
    game.addEntityType("TestCharacter", "Entity", false);
    game.addAncestors(
        "TestCharacter", {"Describable", "Entity", "Identifiable",
                          "Temporal"});
    game.addProperty("TestCharacter", "ability",
                     kg::PropertyValueKind::Integer, true);
    game.addEntityType("TestModifierEntry", "LookupEntry", false);
    game.addAncestors(
        "TestModifierEntry",
        {"Cited", "Describable", "Entity", "Identifiable", "LookupEntry",
         "Temporal"});
    game.addProperty("TestModifierEntry", "modifier_value",
                     kg::PropertyValueKind::Integer,
                     true);
    game.addProperty("TestModifierEntry", "label",
                     kg::PropertyValueKind::String, true);
    game.addEntityType("OtherModifierEntry", "LookupEntry", false);
    game.addAncestors(
        "OtherModifierEntry",
        {"Cited", "Describable", "Entity", "Identifiable", "LookupEntry",
         "Temporal"});
    out.extend(game);
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    logosphere::EventBus events;
    logosphere::dice::DiceService dice;
    kg::EntityID expression = kg::INVALID_ENTITY;
    kg::EntityID table = kg::INVALID_ENTITY;
    kg::EntityID row = kg::INVALID_ENTITY;
    kg::EntityID check = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        dice.initialize(&events);
        dice.seed_stream("check", 23);

        expression = world.createEntity("DiceExpression");
        world.setProperty(expression, "dice_count", "1");
        world.setProperty(expression, "dice_sides", "2");
        world.setProperty(expression, "dice_modifier", "1");
        world.setProperty(expression, "dice_multiplier", "3");

        table = world.createEntity("LookupTable");
        world.setProperty(table, "entry_type", "TestModifierEntry");
        row = world.createEntity("TestModifierEntry");
        world.setProperty(row, "key_min", "0");
        world.setProperty(row, "key_max", "20");
        world.setProperty(row, "modifier_value", "4");
        world.setProperty(row, "label", "ordinary");
        world.createRelation(table, "HAS_PART", row);

        check = world.createEntity("TaskCheck");
        world.setProperty(check, "attribute_ref", "ability");
        world.setProperty(check, "target_number", "10");
        world.setProperty(check, "dice", std::to_string(expression));
        world.setProperty(check, "modifier_table", std::to_string(table));
        world.setProperty(check, "modifier_property", "modifier_value");

        target = world.createEntity("TestCharacter");
        world.setProperty(target, "ability", "7");
    }
};

void execution_returns_every_fact_used_by_the_decision() {
    Fixture f;
    int events = 0;
    f.events.dice_rolls().subscribe(
        [&](const logosphere::ontology::DiceRollEvent&) { ++events; });
    rules::TaskCheckRunner runner(f.world, f.dice);

    const auto result = runner.run(f.check, f.target, "check", "fixture");
    REQUIRE(result.ok() && result.execution.has_value(),
            "a complete TaskCheck must run: " + result.error);
    const auto& execution = *result.execution;
    REQUIRE(execution.check() == f.check && execution.target() == f.target &&
                execution.attribute() == "ability" &&
                execution.attribute_value() == 7 &&
                execution.modified() &&
                execution.lookup()->table() == f.table &&
                execution.lookup()->row() == f.row &&
                execution.modifier_property() == "modifier_value" &&
                execution.modifier() == 4 && execution.target_number() == 10,
            "the result must retain the exact check inputs and selected row");
    REQUIRE(execution.roll().expression.count == 1 &&
                execution.roll().expression.sides == 2 &&
                execution.roll().expression.modifier == 1 &&
                execution.roll().expression.multiplier == 3 &&
                execution.total() == execution.roll().total + 4 &&
                execution.passed() == (execution.total() >= 10),
            "the stored DiceExpression, external modifier, and threshold "
            "must determine the result");
    REQUIRE(execution.roll().id == 1 && f.dice.journal().size() == 1 &&
                f.dice.find(execution.roll().id) && events == 1,
            "the execution must publish exactly one citable dice fact");
}

void changing_only_modifier_data_changes_the_same_seeded_check() {
    Fixture control;
    Fixture changed;
    control.world.setProperty(control.check, "target_number", "100");
    changed.world.setProperty(changed.check, "target_number", "100");
    changed.world.setProperty(changed.row, "modifier_value", "1000");
    rules::TaskCheckRunner control_runner(control.world, control.dice);
    rules::TaskCheckRunner changed_runner(changed.world, changed.dice);

    const auto ordinary = control_runner.run(
        control.check, control.target, "check", "fixture");
    const auto altered = changed_runner.run(
        changed.check, changed.target, "check", "fixture");
    REQUIRE(ordinary.ok() && altered.ok() &&
                ordinary.execution->roll().values ==
                    altered.execution->roll().values &&
                !ordinary.execution->passed() && altered.execution->passed() &&
                altered.execution->total() - ordinary.execution->total() ==
                    996,
            "the modifier row, not runner code, must control the result");
}

void every_dependency_is_validated_before_randomness() {
    Fixture missing_modifier;
    missing_modifier.world.removeProperty(missing_modifier.row,
                                           "modifier_value");
    rules::TaskCheckRunner missing_runner(
        missing_modifier.world, missing_modifier.dice);
    const auto missing = missing_runner.run(
        missing_modifier.check, missing_modifier.target, "check", "fixture");
    REQUIRE(!missing.ok() &&
                missing.error.find("modifier_value") != std::string::npos &&
                missing_modifier.dice.journal().empty(),
            "a missing selected result value must fail before rolling");

    Fixture wrong_column;
    wrong_column.world.setProperty(wrong_column.check, "modifier_property",
                                   "label");
    rules::TaskCheckRunner wrong_column_runner(
        wrong_column.world, wrong_column.dice);
    const auto wrong = wrong_column_runner.run(
        wrong_column.check, wrong_column.target, "check", "fixture");
    REQUIRE(!wrong.ok() && wrong.error.find("integer") != std::string::npos &&
                wrong_column.dice.journal().empty(),
            "the declared result column must be an ontology integer");

    Fixture wrong_row;
    const auto other = wrong_row.world.createEntity("OtherModifierEntry");
    wrong_row.world.setProperty(other, "key_min", "100");
    wrong_row.world.setProperty(other, "key_max", "101");
    wrong_row.world.createRelation(wrong_row.table, "HAS_PART", other);
    rules::TaskCheckRunner wrong_row_runner(wrong_row.world, wrong_row.dice);
    const auto malformed = wrong_row_runner.run(
        wrong_row.check, wrong_row.target, "check", "fixture");
    REQUIRE(!malformed.ok() &&
                malformed.error.find("OtherModifierEntry") !=
                    std::string::npos &&
                wrong_row.dice.journal().empty(),
            "an invalid unselected lookup row must block the check");
}

void missing_context_and_target_attribute_never_receive_defaults() {
    Fixture f;
    rules::TaskCheckRunner runner(f.world, f.dice);
    const auto no_stream = runner.run(f.check, f.target, "", "fixture");
    REQUIRE(!no_stream.ok() && no_stream.error.find("stream") !=
                std::string::npos && f.dice.journal().empty(),
            "an unnamed dice stream must fail");

    f.world.setProperty(f.check, "attribute_ref", "missing_attribute");
    const auto unknown = runner.run(f.check, f.target, "check", "fixture");
    REQUIRE(!unknown.ok() && unknown.error.find("missing_attribute") !=
                std::string::npos && f.dice.journal().empty(),
            "an unknown target attribute must fail without a value default");

    f.world.setProperty(f.check, "attribute_ref", "ability");
    f.world.removeProperty(f.target, "ability");
    const auto missing = runner.run(f.check, f.target, "check", "fixture");
    REQUIRE(!missing.ok() && missing.error.find("ability") !=
                std::string::npos && f.dice.journal().empty(),
            "a missing required target value must fail without zero filling");
}

}  // namespace

int main() {
    std::cout << "Task check runner\n";
    TEST(execution_returns_every_fact_used_by_the_decision);
    TEST(changing_only_modifier_data_changes_the_same_seeded_check);
    TEST(every_dependency_is_validated_before_randomness);
    TEST(missing_context_and_target_attribute_never_receive_defaults);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
