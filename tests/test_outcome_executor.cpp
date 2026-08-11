// Typed rulebook outcomes execute through planned, atomic KG operations.

#include "logosphere/core/dice_service.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/outcome_executor.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());

    kg::OntologyRegistry game("schema://outcome-executor-test");
    game.addEntityType("TestCharacter", "Entity", false);
    game.addAncestors("TestCharacter", {"Entity", "Describable",
                                          "Identifiable", "Temporal"});
    game.addProperty("TestCharacter", "strength",
                     kg::PropertyValueKind::Integer, false);
    game.addEntityType("TestSkill", "Entity", false);
    game.addAncestors("TestSkill", {"Entity", "Describable",
                                     "Identifiable", "Temporal"});
    game.addEntityType("TestCurrency", "Entity", false);
    game.addAncestors("TestCurrency", {"Entity", "Describable",
                                        "Identifiable", "Temporal"});
    game.addEntityType("EndCareer", "Outcome", false);
    game.addAncestors("EndCareer", {"Outcome", "Cited", "Entity",
                                     "Describable", "Identifiable",
                                     "Temporal"});
    out.extend(game);
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    logosphere::EventBus events;
    logosphere::dice::DiceService dice;
    kg::EntityID character = kg::INVALID_ENTITY;
    kg::EntityID skill = kg::INVALID_ENTITY;
    kg::EntityID currency = kg::INVALID_ENTITY;
    int state_events = 0;
    int relation_events = 0;
    int dice_events = 0;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        world.set_event_bus(&events);
        dice.initialize(&events);
        character = world.createEntity("TestCharacter");
        skill = world.createEntity("TestSkill");
        currency = world.createEntity("TestCurrency");
        world.setProperty(character, "strength", "8");
        events.state_changes().subscribe(
            [&](const logosphere::ontology::WorldEvent&) { ++state_events; });
        events.relations().subscribe(
            [&](const logosphere::ontology::RelationEvent&) {
                ++relation_events;
            });
        events.dice_rolls().subscribe(
            [&](const logosphere::ontology::DiceRollEvent&) { ++dice_events; });
        state_events = 0;
    }

    rules::OutcomeContext context() const {
        return {character, "outcome-test", "typed outcome"};
    }
};

kg::EntityID outcome(Fixture& f, const std::string& type) {
    const auto id = f.world.createEntity(type);
    REQUIRE(id != kg::INVALID_ENTITY, "could not create outcome " + type);
    return id;
}

kg::EntityID modify(Fixture& f, const std::string& property, int delta) {
    const auto id = outcome(f, "ModifyAttribute");
    f.world.setProperty(id, "attribute_ref", property);
    f.world.setProperty(id, "attribute_delta", std::to_string(delta));
    return id;
}

kg::EntityID fixed_money(Fixture& f, int amount) {
    const auto id = outcome(f, "GainFixedMoney");
    f.world.setProperty(id, "currency", std::to_string(f.currency));
    f.world.setProperty(id, "amount", std::to_string(amount));
    return id;
}

kg::EntityID rolled_money(Fixture& f) {
    const auto dice = f.world.createEntity("DiceExpression");
    f.world.setProperty(dice, "dice_count", "1");
    f.world.setProperty(dice, "dice_sides", "6");
    f.world.setProperty(dice, "dice_multiplier", "10000");
    const auto id = outcome(f, "GainRolledMoney");
    f.world.setProperty(id, "currency", std::to_string(f.currency));
    f.world.setProperty(id, "amount_dice", std::to_string(dice));
    return id;
}

kg::EntityID sequence(Fixture& f,
                      const std::vector<kg::EntityID>& outcomes) {
    const auto root = outcome(f, "OutcomeSequence");
    for (size_t index = 0; index < outcomes.size(); ++index) {
        const auto step = f.world.createEntity("OutcomeStep");
        f.world.setProperty(step, "step_index", std::to_string(index));
        f.world.setProperty(step, "outcome", std::to_string(outcomes[index]));
        REQUIRE(f.world.createRelation(root, "HAS_PART", step) !=
                    kg::INVALID_RELATION,
                "could not attach outcome step");
    }
    return root;
}

kg::EntityID choice(Fixture& f, const std::string& authority,
                    const std::vector<std::pair<std::string, kg::EntityID>>&
                        alternatives) {
    const auto root = outcome(f, "OutcomeChoice");
    f.world.setProperty(root, "choice_authority", authority);
    for (size_t index = 0; index < alternatives.size(); ++index) {
        const auto option = f.world.createEntity("OutcomeOption");
        f.world.setProperty(option, "option_index", std::to_string(index));
        f.world.setProperty(option, "option_label", alternatives[index].first);
        f.world.setProperty(option, "outcome",
                            std::to_string(alternatives[index].second));
        REQUIRE(f.world.createRelation(root, "HAS_PART", option) !=
                    kg::INVALID_RELATION,
                "could not attach choice option");
    }
    return root;
}

kg::EntityID balance_for(Fixture& f) {
    for (const auto part : f.world.getRelated(f.character, "HAS_PART")) {
        if (f.world.getType(part) == "CurrencyBalance" &&
            f.world.getProperty(part, "currency") ==
                std::to_string(f.currency)) {
            return part;
        }
    }
    return kg::INVALID_ENTITY;
}

kg::EntityID rating_for(Fixture& f) {
    for (const auto part : f.world.getRelated(f.character, "HAS_PART")) {
        if (f.world.getType(part) == "SkillRating" &&
            f.world.getProperty(part, "skill") == std::to_string(f.skill)) {
            return part;
        }
    }
    return kg::INVALID_ENTITY;
}

void exact_type_dispatch_fails_loudly_and_rejects_duplicates() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto end = outcome(f, "EndCareer");
    auto missing = executor.apply(end, f.context());
    REQUIRE(missing.status == rules::OutcomeStatus::FAILED &&
                missing.error.find("no handler for concrete outcome type "
                                   "'EndCareer'") != std::string::npos,
            "an unhandled game outcome must fail by exact type");

    std::string error;
    rules::OutcomeHandler handler =
        [](const rules::OutcomeHandlerContext& context,
           rules::OutcomePlan& plan, std::string&) {
            plan.procedure_signals.push_back(
                {context.outcome_type, context.outcome, context.target});
            return true;
        };
    REQUIRE(executor.register_handler("EndCareer", handler, error),
            "the game handler must register: " + error);
    REQUIRE(!executor.register_handler("EndCareer", handler, error) &&
                error.find("already registered") != std::string::npos,
            "a duplicate handler must not silently replace policy");
    const auto applied = executor.apply(end, f.context());
    REQUIRE(applied.status == rules::OutcomeStatus::APPLIED &&
                applied.procedure_signals.size() == 1 &&
                applied.procedure_signals[0].outcome_type == "EndCareer",
            "the exact game handler returns a typed procedure signal");
}

void attribute_changes_are_validated_and_atomic() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto valid = modify(f, "strength", -2);
    const auto applied = executor.apply(valid, f.context());
    REQUIRE(applied.status == rules::OutcomeStatus::APPLIED &&
                f.world.getProperty(f.character, "strength") == "6",
            "ModifyAttribute applies its data through the validated path");

    const auto invalid = modify(f, "missing", -2);
    const auto rejected = executor.apply(invalid, f.context());
    REQUIRE(rejected.status == rules::OutcomeStatus::FAILED &&
                rejected.error.find("missing") != std::string::npos &&
                f.world.getProperty(f.character, "strength") == "6",
            "an unknown target property fails without another mutation");
}

void skill_outcomes_keep_both_rule_branches_in_data() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);

    const auto ensure = outcome(f, "EnsureSkillLevel");
    f.world.setProperty(ensure, "skill", std::to_string(f.skill));
    f.world.setProperty(ensure, "skill_level", "0");
    REQUIRE(executor.apply(ensure, f.context()).status ==
                rules::OutcomeStatus::APPLIED,
            "EnsureSkillLevel must apply");
    auto rating = rating_for(f);
    REQUIRE(rating != kg::INVALID_ENTITY &&
                f.world.getProperty(rating, "skill_level") == "0",
            "an absent rating is created at the stated minimum");

    f.world.setProperty(rating, "skill_level", "3");
    REQUIRE(executor.apply(ensure, f.context()).status ==
                rules::OutcomeStatus::APPLIED &&
                f.world.getProperty(rating, "skill_level") == "3",
            "ensuring a lower minimum does not reduce an existing skill");

    const auto advance = outcome(f, "AdvanceSkill");
    f.world.setProperty(advance, "skill", std::to_string(f.skill));
    f.world.setProperty(advance, "initial_skill_level", "1");
    f.world.setProperty(advance, "existing_skill_delta", "2");
    REQUIRE(executor.apply(advance, f.context()).status ==
                rules::OutcomeStatus::APPLIED &&
                f.world.getProperty(rating, "skill_level") == "5",
            "an existing rating changes by the explicit data delta");
}

void fixed_and_rolled_money_share_generic_balance_state() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    REQUIRE(executor.apply(fixed_money(f, -10000), f.context()).status ==
                rules::OutcomeStatus::APPLIED,
            "fixed debt must apply");
    const auto balance = balance_for(f);
    REQUIRE(balance != kg::INVALID_ENTITY &&
                f.world.getProperty(balance, "balance_amount") == "-10000",
            "fixed money creates generic per-currency balance state");

    f.dice.seed_stream("outcome-test", 4);
    const auto rolled = executor.apply(rolled_money(f), f.context());
    REQUIRE(rolled.status == rules::OutcomeStatus::APPLIED &&
                rolled.roll_ids.size() == 1 && f.dice_events == 1,
            "rolled money commits one journaled dice fact");
    const int amount = std::stoi(f.world.getProperty(balance,
                                                     "balance_amount"));
    REQUIRE(amount >= 0 && amount <= 50000 && amount % 10000 == 0,
            "the rolled total was added to the existing debt");
}

void table_rolls_are_typed_requests_not_hidden_recursive_execution() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto table = f.world.createEntity("RollableTable");
    const auto grant = outcome(f, "GrantTableRoll");
    f.world.setProperty(grant, "table", std::to_string(table));
    f.world.setProperty(grant, "roll_count", "2");
    const auto result = executor.apply(grant, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED &&
                result.table_roll_requests.size() == 1 &&
                result.table_roll_requests[0].table == table &&
                result.table_roll_requests[0].roll_count == 2,
            "GrantTableRoll returns the exact pending request from data");
    REQUIRE(f.dice.journal().empty(),
            "granting a roll does not choose or roll a table implicitly");
}

void a_sequence_rolls_back_kg_dice_and_events_on_late_failure() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto root = sequence(
        f, {modify(f, "strength", -2), rolled_money(f),
            outcome(f, "EndCareer")});
    const int state_events_before = f.state_events;
    const int relation_events_before = f.relation_events;
    const int dice_events_before = f.dice_events;
    const auto result = executor.apply(root, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::FAILED,
            "the unhandled final outcome must fail the sequence");
    REQUIRE(f.world.getProperty(f.character, "strength") == "8" &&
                balance_for(f) == kg::INVALID_ENTITY,
            "every earlier KG effect must roll back");
    REQUIRE(f.dice.journal().empty() &&
                f.dice_events == dice_events_before &&
                f.state_events == state_events_before &&
                f.relation_events == relation_events_before,
            "dice state and every external event must also roll back");
}

void a_late_kg_validation_failure_rolls_back_the_dice_plan() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    std::string error;
    REQUIRE(executor.register_handler(
                "EndCareer",
                [](const rules::OutcomeHandlerContext& context,
                   rules::OutcomePlan& plan, std::string&) {
                    plan.ops.emplace_back(kg::KGOpSetProperty{
                        {context.target, ""}, "missing_property", "1"});
                    return true;
                },
                error),
            "the invalid-op control handler must register: " + error);
    const auto root = sequence(
        f, {modify(f, "strength", -2), rolled_money(f),
            outcome(f, "EndCareer")});
    const int state_events_before = f.state_events;
    const int relation_events_before = f.relation_events;
    const int dice_events_before = f.dice_events;

    const auto result = executor.apply(root, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::FAILED &&
                result.error.find("missing_property") != std::string::npos,
            "the invalid final KG operation must fail during commit");
    REQUIRE(f.world.getProperty(f.character, "strength") == "8" &&
                balance_for(f) == kg::INVALID_ENTITY,
            "the KG batch restores every earlier planned mutation");
    REQUIRE(f.dice.journal().empty() &&
                f.dice_events == dice_events_before &&
                f.state_events == state_events_before &&
                f.relation_events == relation_events_before,
            "KG commit failure also restores dice and publishes nothing");
}

void sequences_compose_against_the_planned_state() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto advance = outcome(f, "AdvanceSkill");
    f.world.setProperty(advance, "skill", std::to_string(f.skill));
    f.world.setProperty(advance, "initial_skill_level", "1");
    f.world.setProperty(advance, "existing_skill_delta", "2");
    const auto root = sequence(
        f, {modify(f, "strength", 1), modify(f, "strength", -2),
            advance, advance, fixed_money(f, 10), fixed_money(f, 20)});

    const auto applied = executor.apply(root, f.context());
    REQUIRE(applied.status == rules::OutcomeStatus::APPLIED,
            "the composed sequence must apply: " + applied.error);
    const auto rating = rating_for(f);
    const auto balance = balance_for(f);
    REQUIRE(f.world.getProperty(f.character, "strength") == "7",
            "later attribute outcomes see earlier planned changes");
    REQUIRE(rating != kg::INVALID_ENTITY &&
                f.world.getProperty(rating, "skill_level") == "3",
            "a repeated skill outcome advances the planned new rating");
    REQUIRE(balance != kg::INVALID_ENTITY &&
                f.world.getProperty(balance, "balance_amount") == "30",
            "money outcomes share one planned new currency balance");
}

void choices_suspend_the_complete_root_before_mutation() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto pick = choice(f, "player",
                             {{"Lose one", modify(f, "strength", -1)},
                              {"Lose two", modify(f, "strength", -2)}});
    const auto root = sequence(f, {modify(f, "strength", 1), pick});

    auto pending = executor.apply(root, f.context());
    REQUIRE(pending.status == rules::OutcomeStatus::PENDING_CHOICE &&
                pending.pending_choice.has_value() &&
                pending.pending_choice->choice == pick &&
                pending.pending_choice->authority == "player" &&
                pending.pending_choice->options.size() == 2,
            "the unresolved choice returns ordered structured options");
    REQUIRE(f.world.getProperty(f.character, "strength") == "8",
            "a choice suspends before earlier sequence effects mutate");

    const std::vector<rules::OutcomeSelection> selections{{pick, 1}};
    const auto applied = executor.apply(root, f.context(), selections);
    REQUIRE(applied.status == rules::OutcomeStatus::APPLIED &&
                f.world.getProperty(f.character, "strength") == "7",
            "rerunning the root with a selection commits the whole sequence");

    const std::vector<rules::OutcomeSelection> invalid{{pick, 9}};
    const auto rejected = executor.apply(root, f.context(), invalid);
    REQUIRE(rejected.status == rules::OutcomeStatus::FAILED &&
                f.world.getProperty(f.character, "strength") == "7",
            "an unavailable option fails without touching state");
}

void malformed_graphs_and_selection_misuse_fail_loudly() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);

    const auto cycle = outcome(f, "OutcomeSequence");
    const auto step = f.world.createEntity("OutcomeStep");
    f.world.setProperty(step, "step_index", "0");
    f.world.setProperty(step, "outcome", std::to_string(cycle));
    f.world.createRelation(cycle, "HAS_PART", step);
    const auto cyclic = executor.apply(cycle, f.context());
    REQUIRE(cyclic.status == rules::OutcomeStatus::FAILED &&
                cyclic.error.find("cyclic outcome graph") !=
                    std::string::npos,
            "cyclic structured outcomes are rejected");

    const auto invalid_choice = choice(
        f, "computer", {{"Only", modify(f, "strength", -1)}});
    const auto invalid_authority = executor.apply(invalid_choice, f.context());
    REQUIRE(invalid_authority.status == rules::OutcomeStatus::FAILED &&
                invalid_authority.error.find("choice_authority") !=
                    std::string::npos,
            "runtime execution does not trust an invalid choice authority");

    const auto valid_choice = choice(
        f, "player", {{"Only", modify(f, "strength", -1)}});
    const std::vector<rules::OutcomeSelection> duplicate{
        {valid_choice, 0}, {valid_choice, 0}};
    const auto duplicate_result =
        executor.apply(valid_choice, f.context(), duplicate);
    REQUIRE(duplicate_result.status == rules::OutcomeStatus::FAILED &&
                duplicate_result.error.find("duplicate selection") !=
                    std::string::npos,
            "duplicate choice answers do not silently override each other");

    const auto incomplete = outcome(f, "AdvanceSkill");
    f.world.setProperty(incomplete, "skill", std::to_string(f.skill));
    f.world.setProperty(incomplete, "initial_skill_level", "1");
    const auto missing = executor.apply(incomplete, f.context());
    REQUIRE(missing.status == rules::OutcomeStatus::FAILED &&
                missing.error.find("existing_skill_delta") !=
                    std::string::npos &&
                rating_for(f) == kg::INVALID_ENTITY,
            "missing required consequence data has no runtime default");

    std::string error;
    REQUIRE(!executor.register_handler(
                "OutcomeSequence",
                [](const rules::OutcomeHandlerContext&, rules::OutcomePlan&,
                   std::string&) { return true; },
                error) &&
                error.find("structural") != std::string::npos,
            "reserved structural outcomes cannot register ignored handlers");
}

}  // namespace

int main() {
    std::cout << "Typed outcome executor\n";
    TEST(exact_type_dispatch_fails_loudly_and_rejects_duplicates);
    TEST(attribute_changes_are_validated_and_atomic);
    TEST(skill_outcomes_keep_both_rule_branches_in_data);
    TEST(fixed_and_rolled_money_share_generic_balance_state);
    TEST(table_rolls_are_typed_requests_not_hidden_recursive_execution);
    TEST(a_sequence_rolls_back_kg_dice_and_events_on_late_failure);
    TEST(a_late_kg_validation_failure_rolls_back_the_dice_plan);
    TEST(sequences_compose_against_the_planned_state);
    TEST(choices_suspend_the_complete_root_before_mutation);
    TEST(malformed_graphs_and_selection_misuse_fail_loudly);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
