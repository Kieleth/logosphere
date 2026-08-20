// Typed rulebook outcomes execute through planned, atomic KG operations.

#include "logosphere/core/dice_service.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/outcome_executor.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_env_portable.h"

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
    // A group needs more than one member to be a group, and one
    // property outside it to prove the rule stays inside its own.
    game.addProperty("TestCharacter", "dexterity",
                     kg::PropertyValueKind::Integer, false);
    game.addProperty("TestCharacter", "endurance",
                     kg::PropertyValueKind::Integer, false);
    game.addProperty("TestCharacter", "intelligence",
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
        world.setProperty(character, "dexterity", "8");
        world.setProperty(character, "endurance", "8");
        world.setProperty(character, "intelligence", "8");
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

// Aging prints "reduce three physical characteristics by 2" over a
// group of exactly three, so there is nothing to choose and the rule
// applies on its own. A game that installs no selector must still be
// able to run these.
void a_group_change_covering_everything_needs_no_selector() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto group = f.world.createEntity("AttributeGroup");
    f.world.setProperty(group, "attribute_refs",
                        "strength; dexterity; endurance");
    const auto out = outcome(f, "ModifyAttributesInGroup");
    f.world.setProperty(out, "attribute_group", std::to_string(group));
    f.world.setProperty(out, "affected_count", "3");
    f.world.setProperty(out, "attribute_delta", "-2");

    const auto result = executor.apply(out, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED,
            "the whole group applies without a selector: " + result.error);
    REQUIRE(f.world.getProperty(f.character, "strength") == "6" &&
                f.world.getProperty(f.character, "dexterity") == "6" &&
                f.world.getProperty(f.character, "endurance") == "6",
            "every attribute in the group took the delta");
}

// "Reduce two physical characteristics by 2" over a group of three is
// a real choice, and the engine refuses to invent it.
void a_partial_group_change_requires_an_installed_selector() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto group = f.world.createEntity("AttributeGroup");
    f.world.setProperty(group, "attribute_refs",
                        "strength; dexterity; endurance");
    const auto out = outcome(f, "ModifyAttributesInGroup");
    f.world.setProperty(out, "attribute_group", std::to_string(group));
    f.world.setProperty(out, "affected_count", "2");
    f.world.setProperty(out, "attribute_delta", "-2");

    const auto refused = executor.apply(out, f.context());
    REQUIRE(refused.status == rules::OutcomeStatus::FAILED &&
                refused.error.find("selector") != std::string::npos,
            "a partial change without a selector must fail loudly");
    REQUIRE(f.world.getProperty(f.character, "strength") == "8",
            "nothing moved on the refusal");

    rules::AttributeSelectionRequest seen;
    executor.set_attribute_selector(
        [&](const rules::AttributeSelectionRequest& request,
            std::vector<std::string>& chosen, std::string&) {
            seen = request;
            chosen = {"strength", "endurance"};
            return true;
        });
    const auto applied = executor.apply(out, f.context());
    REQUIRE(applied.status == rules::OutcomeStatus::APPLIED,
            "the selector's answer applies: " + applied.error);
    REQUIRE(seen.count == 2 && seen.delta == -2 &&
                seen.eligible.size() == 3 && seen.target == f.character,
            "the selector is told the count, the delta and the group");
    REQUIRE(f.world.getProperty(f.character, "strength") == "6" &&
                f.world.getProperty(f.character, "endurance") == "6" &&
                f.world.getProperty(f.character, "dexterity") == "8",
            "exactly the chosen attributes moved");
}

// The selector may be a prompt, a roll or a narrator. None of them is
// trusted: a wrong answer cannot widen the rule the book set.
void a_selector_cannot_widen_or_wander_outside_the_rule() {
    struct Case {
        const char* name;
        std::vector<std::string> answer;
        const char* expect;
    };
    const std::vector<Case> cases = {
        {"too many", {"strength", "dexterity", "endurance"}, "not 2"},
        {"too few", {"strength"}, "not 2"},
        {"outside the group", {"strength", "intelligence"}, "not in the group"},
        {"the same one twice", {"strength", "strength"}, "twice"},
    };
    for (const auto& item : cases) {
        Fixture f;
        rules::OutcomeExecutor executor(f.world, f.dice);
        const auto group = f.world.createEntity("AttributeGroup");
        f.world.setProperty(group, "attribute_refs",
                            "strength; dexterity; endurance");
        const auto out = outcome(f, "ModifyAttributesInGroup");
        f.world.setProperty(out, "attribute_group", std::to_string(group));
        f.world.setProperty(out, "affected_count", "2");
        f.world.setProperty(out, "attribute_delta", "-2");
        executor.set_attribute_selector(
            [&](const rules::AttributeSelectionRequest&,
                std::vector<std::string>& chosen, std::string&) {
                chosen = item.answer;
                return true;
            });
        const auto result = executor.apply(out, f.context());
        REQUIRE(result.status == rules::OutcomeStatus::FAILED &&
                    result.error.find(item.expect) != std::string::npos,
                std::string("a selector answering with ") + item.name +
                    " must be refused, got: " + result.error);
        REQUIRE(f.world.getProperty(f.character, "strength") == "8" &&
                    f.world.getProperty(f.character, "dexterity") == "8",
                std::string("nothing moved when the selector answered ") +
                    item.name);
    }
}

// WHY the chooser is handed its history instead of subscribing for it.
//
// The event bus is the engine's answer to "react to what happened",
// and it is deliberately no help here: apply_kg_ops_atomically
// detaches the bus, buffers every event, and re-emits only after the
// whole batch commits, so nothing is ever announced for a change that
// might roll back. A chooser runs INSIDE that batch, before any of it
// is real.
//
// This pins both halves at once: mid-rule, the bus is silent and the
// graph still reads as it did before the rule started, while
// already_taken tells the truth. If someone later makes the executor
// emit during planning, this test goes red and the guarantee in
// docs/GAME_LAYER.md is what they broke.
void the_bus_is_silent_until_the_whole_rule_commits() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto group = f.world.createEntity("AttributeGroup");
    f.world.setProperty(group, "attribute_refs",
                        "strength; dexterity; endurance");
    const auto step_of = [&](int count, int delta) {
        const auto id = outcome(f, "ModifyAttributesInGroup");
        f.world.setProperty(id, "attribute_group", std::to_string(group));
        f.world.setProperty(id, "affected_count", std::to_string(count));
        f.world.setProperty(id, "attribute_delta", std::to_string(delta));
        return id;
    };
    const auto root = sequence(f, {step_of(2, -2), step_of(1, -1)});

    struct Look {
        int events_seen = 0;
        std::string strength_in_graph;
        size_t already_taken = 0;
    };
    std::vector<Look> looks;
    f.state_events = 0;
    executor.set_attribute_selector(
        [&](const rules::AttributeSelectionRequest& request,
            std::vector<std::string>& chosen, std::string&) {
            looks.push_back({f.state_events,
                             f.world.getProperty(f.character, "strength"),
                             request.already_taken.size()});
            for (const auto& name : request.eligible) {
                if (std::find(request.already_taken.begin(),
                              request.already_taken.end(),
                              name) != request.already_taken.end()) {
                    continue;
                }
                if (static_cast<int>(chosen.size()) < request.count) {
                    chosen.push_back(name);
                }
            }
            return true;
        });

    const auto result = executor.apply(root, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED,
            "the rule applies: " + result.error);
    REQUIRE(looks.size() == 2, "the chooser ran twice inside one batch");

    REQUIRE(looks[0].events_seen == 0 && looks[1].events_seen == 0,
            "no state-change event reaches anyone mid-rule, got " +
                std::to_string(looks[0].events_seen) + " and " +
                std::to_string(looks[1].events_seen));
    REQUIRE(looks[0].strength_in_graph == "8" &&
                looks[1].strength_in_graph == "8",
            "and the graph still reads as it did before the rule began, "
            "even on the second call: got " + looks[1].strength_in_graph);

    // The only truthful source at that moment.
    REQUIRE(looks[0].already_taken == 0 && looks[1].already_taken == 2,
            "while already_taken knows exactly what the rule has spent");

    // And once it commits, both the graph and the bus catch up at once.
    REQUIRE(f.world.getProperty(f.character, "strength") == "6" &&
                f.state_events > 0,
            "after the commit the change is real and announced");
}

// Aging's "reduce two physical characteristics by 2, reduce one
// physical characteristic by 1" is TWO changes over ONE group, and the
// book means all three go, each once. The chooser therefore has to
// know what the previous step took.
//
// It cannot learn that from the graph. The whole plan commits at the
// end, so a KG query between the two steps reads the values as they
// stood before either. The request carries the pending state instead.
void a_second_change_sees_what_the_first_one_took() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto group = f.world.createEntity("AttributeGroup");
    f.world.setProperty(group, "attribute_refs",
                        "strength; dexterity; endurance");
    const auto step_of = [&](int count, int delta) {
        const auto id = outcome(f, "ModifyAttributesInGroup");
        f.world.setProperty(id, "attribute_group", std::to_string(group));
        f.world.setProperty(id, "affected_count", std::to_string(count));
        f.world.setProperty(id, "attribute_delta", std::to_string(delta));
        return id;
    };
    const auto root = sequence(f, {step_of(2, -2), step_of(1, -1)});

    std::vector<rules::AttributeSelectionRequest> seen;
    executor.set_attribute_selector(
        [&](const rules::AttributeSelectionRequest& request,
            std::vector<std::string>& chosen, std::string&) {
            seen.push_back(request);
            // Behave the way the book means: never take the same one
            // twice. This is policy, which is why it lives here and
            // not in the engine.
            for (const auto& name : request.eligible) {
                if (std::find(request.already_taken.begin(),
                              request.already_taken.end(),
                              name) != request.already_taken.end()) {
                    continue;
                }
                if (static_cast<int>(chosen.size()) < request.count) {
                    chosen.push_back(name);
                }
            }
            return true;
        });

    const auto result = executor.apply(root, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED,
            "the two-step rule applies: " + result.error);
    REQUIRE(seen.size() == 2, "the chooser was asked twice");

    REQUIRE(seen[0].already_taken.empty(),
            "the first step has nothing behind it");
    REQUIRE(seen[0].current.size() == 3 && seen[0].current[0] == 8,
            "the first step sees the untouched values");

    // The claim. Without this the second step would re-offer an
    // attribute the first already spent, and the graph could not have
    // told it so.
    REQUIRE(seen[1].already_taken.size() == 2,
            "the second step is told which two are already spent, got " +
                std::to_string(seen[1].already_taken.size()));
    REQUIRE(seen[1].current[0] == 6 && seen[1].current[1] == 6,
            "and sees their PLANNED values, not the graph's stale ones: "
            "got " + std::to_string(seen[1].current[0]) + "," +
                std::to_string(seen[1].current[1]));

    // The whole point: all three moved, each exactly once.
    REQUIRE(f.world.getProperty(f.character, "strength") == "6" &&
                f.world.getProperty(f.character, "dexterity") == "6" &&
                f.world.getProperty(f.character, "endurance") == "7",
            "two by 2 and one by 1, across three distinct attributes: " +
                f.world.getProperty(f.character, "strength") + "/" +
                f.world.getProperty(f.character, "dexterity") + "/" +
                f.world.getProperty(f.character, "endurance"));

    // The control: the engine does NOT enforce disjointness, because a
    // rule may legitimately hit one attribute twice. A chooser that
    // ignores already_taken is allowed to, and compounds.
    Fixture g;
    rules::OutcomeExecutor greedy(g.world, g.dice);
    const auto ggroup = g.world.createEntity("AttributeGroup");
    g.world.setProperty(ggroup, "attribute_refs",
                        "strength; dexterity; endurance");
    const auto gstep = [&](int count, int delta) {
        const auto id = outcome(g, "ModifyAttributesInGroup");
        g.world.setProperty(id, "attribute_group", std::to_string(ggroup));
        g.world.setProperty(id, "affected_count", std::to_string(count));
        g.world.setProperty(id, "attribute_delta", std::to_string(delta));
        return id;
    };
    const auto groot = sequence(g, {gstep(2, -2), gstep(1, -1)});
    greedy.set_attribute_selector(
        [](const rules::AttributeSelectionRequest& request,
           std::vector<std::string>& chosen, std::string&) {
            chosen.assign(request.eligible.begin(),
                          request.eligible.begin() + request.count);
            return true;
        });
    REQUIRE(greedy.apply(groot, g.context()).status ==
                rules::OutcomeStatus::APPLIED,
            "a chooser that ignores the history is still allowed");
    REQUIRE(g.world.getProperty(g.character, "strength") == "5",
            "and its overlap compounds, 8 -2 -1, proving the engine "
            "reports rather than enforces: got " +
                g.world.getProperty(g.character, "strength"));
}

// A reduction past the schema floor is expected play, not malformed
// data: Cepheus floors characteristics at 0 and then has a rule for
// what reaching 0 means. So the change lands at the floor and says it
// did, instead of refusing the whole rule and killing the run.
void a_change_past_the_floor_lands_on_it_and_says_so() {
    // The fixture's own character type has no bounds, so this needs a
    // type that declares them, the way a real game's does.
    kg::OntologyRegistry ontology = registry();
    kg::OntologyRegistry bounded("schema://floor-test");
    bounded.addEntityType("BoundedCharacter", "Entity", false);
    bounded.addAncestors("BoundedCharacter", {"Entity", "Describable",
                                              "Identifiable", "Temporal"});
    bounded.addProperty("BoundedCharacter", "strength",
                        kg::PropertyValueKind::Integer, false, true, 0.0,
                        true, 15.0);
    ontology.extend(bounded);

    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    logosphere::dice::DiceService dice;
    const auto who = world.createEntity("BoundedCharacter");
    world.setProperty(who, "strength", "1");

    rules::OutcomeExecutor executor(world, dice);
    const auto out = world.createEntity("ModifyAttribute");
    world.setProperty(out, "attribute_ref", "strength");
    world.setProperty(out, "attribute_delta", "-3");

    const auto result = executor.apply(out, {who, "floor-test", "aging"});
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED,
            "a reduction past the floor still applies: " + result.error);
    REQUIRE(world.getProperty(who, "strength") == "0",
            "it lands ON the floor, got " +
                world.getProperty(who, "strength"));
    REQUIRE(result.attributes_limited.size() == 1 &&
                result.attributes_limited[0].attribute == "strength" &&
                result.attributes_limited[0].requested == -2 &&
                result.attributes_limited[0].applied == 0,
            "and reports what was asked for versus what was allowed");

    // The control: a change that fits reports nothing, so a game
    // reacting to the report does not fire on ordinary damage.
    kg::KGModule fine{ontology};
    fine.setMode(kg::KGMode::MINIMAL);
    logosphere::dice::DiceService fine_dice;
    const auto healthy = fine.createEntity("BoundedCharacter");
    fine.setProperty(healthy, "strength", "8");
    rules::OutcomeExecutor fine_exec(fine, fine_dice);
    const auto ok = fine.createEntity("ModifyAttribute");
    fine.setProperty(ok, "attribute_ref", "strength");
    fine.setProperty(ok, "attribute_delta", "-3");
    const auto plain = fine_exec.apply(ok, {healthy, "floor-test", "aging"});
    REQUIRE(plain.status == rules::OutcomeStatus::APPLIED &&
                fine.getProperty(healthy, "strength") == "5" &&
                plain.attributes_limited.empty(),
            "an ordinary reduction is silent about limits");
}

// "Alternatively, roll twice on the Injury table and take the lower
// result." Which of several rolls counts belongs to the rule, not to
// whoever happens to run it, so it travels with the request.
void rolling_twice_says_which_roll_counts() {
    const auto request = [](Fixture& f, const char* count,
                            const char* selection) {
        const auto table = f.world.createEntity("RollableTable");
        const auto id = outcome(f, "GrantTableRoll");
        f.world.setProperty(id, "table", std::to_string(table));
        if (count) f.world.setProperty(id, "roll_count", count);
        if (selection) f.world.setProperty(id, "roll_selection", selection);
        return id;
    };

    Fixture lower;
    rules::OutcomeExecutor lower_exec(lower.world, lower.dice);
    const auto got = lower_exec.apply(request(lower, "2", "LOWEST"),
                                      lower.context());
    REQUIRE(got.status == rules::OutcomeStatus::APPLIED,
            "roll twice take lowest applies: " + got.error);
    REQUIRE(got.table_roll_requests.size() == 1 &&
                got.table_roll_requests[0].roll_count == 2 &&
                got.table_roll_requests[0].selection ==
                    rules::TableRollSelection::LOWEST,
            "the request carries both the count and which roll counts");

    // A bare instruction to roll says nothing about choosing, so every
    // roll counts. Absent is EACH, exactly as absent count is one.
    Fixture bare;
    rules::OutcomeExecutor bare_exec(bare.world, bare.dice);
    const auto plain = bare_exec.apply(request(bare, nullptr, nullptr),
                                       bare.context());
    REQUIRE(plain.status == rules::OutcomeStatus::APPLIED &&
                plain.table_roll_requests[0].roll_count == 1 &&
                plain.table_roll_requests[0].selection ==
                    rules::TableRollSelection::EACH,
            "a bare roll is one roll, and it counts");

    // Refusals, so a typo cannot become a silent EACH and a rule
    // cannot ask to choose between a single roll.
    Fixture typo;
    rules::OutcomeExecutor typo_exec(typo.world, typo.dice);
    const auto refused = typo_exec.apply(request(typo, "2", "LOWER"),
                                         typo.context());
    REQUIRE(refused.status == rules::OutcomeStatus::FAILED &&
                refused.error.find("roll_selection") != std::string::npos,
            "an unknown roll_selection is refused, got: " + refused.error);

    Fixture lonely;
    rules::OutcomeExecutor lonely_exec(lonely.world, lonely.dice);
    const auto nonsense = lonely_exec.apply(request(lonely, "1", "LOWEST"),
                                            lonely.context());
    REQUIRE(nonsense.status == rules::OutcomeStatus::FAILED &&
                nonsense.error.find("more than one roll") !=
                    std::string::npos,
            "choosing between one roll is refused, got: " + nonsense.error);
}

// Dice say how much, never which way. The book prints "reduce one
// physical characteristic by 1D6" with no minus sign, and a rule that
// rolled a GAIN would print none either, so the engine must not infer
// a direction from whichever table it happened to absorb first.
void a_rolled_delta_must_say_which_way_it_goes() {
    const auto build = [](Fixture& f, const char* direction) {
        const auto group = f.world.createEntity("AttributeGroup");
        f.world.setProperty(group, "attribute_refs",
                            "strength; dexterity; endurance");
        const auto dice = f.world.createEntity("DiceExpression");
        f.world.setProperty(dice, "dice_count", "1");
        f.world.setProperty(dice, "dice_sides", "6");
        const auto out = outcome(f, "ModifyAttributesInGroup");
        f.world.setProperty(out, "attribute_group", std::to_string(group));
        f.world.setProperty(out, "affected_count", "3");
        f.world.setProperty(out, "attribute_delta_dice",
                            std::to_string(dice));
        if (direction) {
            f.world.setProperty(out, "attribute_delta_reduces", direction);
        }
        return out;
    };

    // Says nothing: refused, and nothing moved.
    Fixture silent;
    rules::OutcomeExecutor silent_exec(silent.world, silent.dice);
    const auto refused = silent_exec.apply(build(silent, nullptr),
                                           silent.context());
    REQUIRE(refused.status == rules::OutcomeStatus::FAILED &&
                refused.error.find("attribute_delta_reduces") !=
                    std::string::npos,
            "a rolled delta with no stated direction must be refused, "
            "got: " + refused.error);
    REQUIRE(silent.world.getProperty(silent.character, "strength") == "8",
            "the refused roll moved nothing");

    // Says it reduces: the characteristic goes DOWN by the roll.
    Fixture down;
    rules::OutcomeExecutor down_exec(down.world, down.dice);
    const auto reduced = down_exec.apply(build(down, "true"),
                                         down.context());
    REQUIRE(reduced.status == rules::OutcomeStatus::APPLIED,
            "a reducing roll applies: " + reduced.error);
    const int after_down =
        std::stoi(down.world.getProperty(down.character, "strength"));
    REQUIRE(after_down < 8 && after_down >= 2,
            "1D6 came off a starting 8, got " + std::to_string(after_down));

    // The control: says it does not reduce, and the same machinery
    // moves the characteristic UP. Without this, "reduces" could be
    // ignored entirely and the test above would still pass.
    Fixture up;
    rules::OutcomeExecutor up_exec(up.world, up.dice);
    const auto gained = up_exec.apply(build(up, "false"), up.context());
    REQUIRE(gained.status == rules::OutcomeStatus::APPLIED,
            "a rolled gain applies: " + gained.error);
    const int after_up =
        std::stoi(up.world.getProperty(up.character, "strength"));
    REQUIRE(after_up > 8 && after_up <= 14,
            "1D6 went onto a starting 8, got " + std::to_string(after_up));
}

// Cepheus writes "Roll on the Injury table" with no number, so the
// seed stores no roll_count and the executor supplies the one the
// sentence means. Both mishap rows that refer to the Injury table are
// shaped this way; before this was honoured they failed outright with
// "missing required integer 'roll_count'".
void an_absent_roll_count_means_one() {
    Fixture f;
    rules::OutcomeExecutor executor(f.world, f.dice);
    const auto table = f.world.createEntity("RollableTable");
    const auto grant = outcome(f, "GrantTableRoll");
    f.world.setProperty(grant, "table", std::to_string(table));
    // roll_count deliberately not set.
    const auto result = executor.apply(grant, f.context());
    REQUIRE(result.status == rules::OutcomeStatus::APPLIED &&
                result.table_roll_requests.size() == 1 &&
                result.table_roll_requests[0].roll_count == 1,
            "a bare table-roll instruction grants exactly one roll");

    // The control: absent is the only thing that defaults. A count
    // that IS written must still be a positive integer, so a zero and
    // a non-number are both refused rather than silently becoming one.
    // The setProperty ontology gate (Malleus H1) would abort on these
    // writes before the executor ever sees them (schema minimum 1);
    // KG_GATE_LENIENT is the documented lever for planting malformed
    // data, and the point here is the executor's OWN refusal — defense
    // in depth behind the gate.
    test_env::set("KG_GATE_LENIENT", "1");
    for (const char* bad : {"0", "-1", "many"}) {
        Fixture g;
        rules::OutcomeExecutor ex(g.world, g.dice);
        const auto t = g.world.createEntity("RollableTable");
        const auto o = outcome(g, "GrantTableRoll");
        g.world.setProperty(o, "table", std::to_string(t));
        g.world.setProperty(o, "roll_count", bad);
        REQUIRE(ex.apply(o, g.context()).status ==
                    rules::OutcomeStatus::FAILED,
                std::string("a written roll_count of '") + bad +
                    "' is refused, not defaulted");
    }
    test_env::unset("KG_GATE_LENIENT");
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
    TEST(a_group_change_covering_everything_needs_no_selector);
    TEST(a_partial_group_change_requires_an_installed_selector);
    TEST(a_selector_cannot_widen_or_wander_outside_the_rule);
    TEST(a_change_past_the_floor_lands_on_it_and_says_so);
    TEST(rolling_twice_says_which_roll_counts);
    TEST(the_bus_is_silent_until_the_whole_rule_commits);
    TEST(a_second_change_sees_what_the_first_one_took);
    TEST(a_rolled_delta_must_say_which_way_it_goes);
    TEST(an_absent_roll_count_means_one);
    TEST(a_sequence_rolls_back_kg_dice_and_events_on_late_failure);
    TEST(a_late_kg_validation_failure_rolls_back_the_dice_plan);
    TEST(sequences_compose_against_the_planned_state);
    TEST(choices_suspend_the_complete_root_before_mutation);
    TEST(malformed_graphs_and_selection_misuse_fail_loudly);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
