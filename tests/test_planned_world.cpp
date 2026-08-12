// PlannedWorld: the world as the current plan will leave it.
//
// The engine applies a rule's KG ops atomically at the very end, and
// deliberately shows nobody the middle of that: the graph reads as it
// did before the rule started, and the event bus is detached. So
// anything reasoning mid-rule about what the same rule has already
// decided reads through this view.
//
// The interesting cases are the ones where a value read is NOT enough.

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/outcome_executor.h"
#include "logosphere/rules/planned_world.h"
#include "generated/logosphere_ontology_registry.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

namespace rules = logosphere::rules;

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) { ++passed; }                                     \
        else { ++failed; std::cout << "FAIL: " << (message) << "\n"; }   \
    } while (false)

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    kg::OntologyRegistry game("schema://planned-world-test");
    game.addEntityType("Thing", "Entity", false);
    game.addAncestors("Thing", {"Entity", "Describable", "Identifiable",
                                "Temporal"});
    game.addProperty("Thing", "strength", kg::PropertyValueKind::Integer,
                     false);
    game.addProperty("Thing", "label", kg::PropertyValueKind::String, false);
    out.extend(game);
    return out;
}

kg::EntityRef by_id(kg::EntityID id) { return kg::EntityRef{id, ""}; }
kg::EntityRef by_alias(const std::string& alias) {
    return kg::EntityRef{kg::INVALID_ENTITY, alias};
}

// All four ways a property read resolves.
void a_read_resolves_plan_then_graph_then_pending_create() {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    const auto thing = world.createEntity("Thing");
    world.setProperty(thing, "strength", "8");

    rules::OutcomePlan plan;
    const rules::PlannedWorld view(world, plan);

    CHECK(view.property(by_id(thing), "strength") == "8",
          "with an empty plan the committed value stands");

    plan.ops.emplace_back(kg::KGOpSetProperty{by_id(thing), "strength", "6"});
    CHECK(view.property(by_id(thing), "strength") == "6",
          "a planned write wins over the committed value");

    // Newest write wins, so a rule's second step sees its first.
    plan.ops.emplace_back(kg::KGOpSetProperty{by_id(thing), "strength", "5"});
    CHECK(view.property(by_id(thing), "strength") == "5",
          "the newest planned write wins, got " +
              view.property(by_id(thing), "strength"));

    // A symbolic ref falls back to the properties of its pending
    // create, because the entity does not exist to be asked yet.
    kg::KGOpCreateEntity create;
    create.type = "Thing";
    create.as = "@fresh";
    create.properties.emplace_back("label", "newborn");
    plan.ops.emplace_back(std::move(create));
    CHECK(view.property(by_alias("@fresh"), "label") == "newborn",
          "a pending create answers for the entity it will make");
    CHECK(view.type_of(by_alias("@fresh")) == "Thing" &&
              view.exists(by_alias("@fresh")),
          "and the view knows its type and that it is coming");

    CHECK(view.property(by_alias("@nobody"), "label").empty() &&
              !view.exists(by_alias("@nobody")),
          "an alias no op named resolves to nothing");
    CHECK(view.committed().getProperty(thing, "strength") == "8",
          "and the committed graph is untouched throughout");
}

// The trap. was_written is NOT derivable from a value comparison, and
// anyone who "optimises" it into one breaks this.
void a_write_of_the_same_value_is_still_a_write() {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    const auto thing = world.createEntity("Thing");
    world.setProperty(thing, "strength", "8");

    rules::OutcomePlan plan;
    plan.ops.emplace_back(kg::KGOpSetProperty{by_id(thing), "strength", "8"});
    const rules::PlannedWorld view(world, plan);

    CHECK(view.property(by_id(thing), "strength") == "8",
          "the value is unchanged, so a value read cannot tell");
    CHECK(view.was_written(by_id(thing), "strength"),
          "but the plan DID spend this attribute, and the view says so");
    CHECK(!view.was_written(by_id(thing), "label"),
          "while an untouched property reports honestly");
}

// Committed first, then pending in plan order. Callers filter on this,
// so it is a contract and not an accident.
void related_lists_committed_first_then_pending_in_order() {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    const auto owner = world.createEntity("Thing");
    const auto old_part = world.createEntity("Thing");
    const auto new_part = world.createEntity("Thing");
    world.createRelation(owner, "HAS_PART", old_part);

    rules::OutcomePlan plan;
    plan.ops.emplace_back(
        kg::KGOpSetRelation{by_id(owner), "HAS_PART", by_id(new_part)});
    plan.ops.emplace_back(
        kg::KGOpSetRelation{by_id(owner), "HAS_PART", by_alias("@later")});
    const rules::PlannedWorld view(world, plan);

    const auto parts = view.related(by_id(owner), "HAS_PART");
    CHECK(parts.size() == 3, "committed and pending edges both appear, got " +
                                 std::to_string(parts.size()));
    CHECK(parts.size() == 3 && parts[0].id == old_part &&
              parts[1].id == new_part && parts[2].symbolic == "@later",
          "committed first, then pending in the order the plan made them");
    CHECK(view.related(by_id(owner), "WIELDS").empty(),
          "and a relation nobody touched is empty");
}

// The invariant the whole view rests on: a plan cannot remove
// anything, so absence is never ambiguous and no tombstones are
// needed. This is a tripwire. If destroy ever becomes plannable, this
// goes red and whoever changed it has to read the PlannedWorld header.
void a_plan_cannot_destroy_so_the_view_needs_no_tombstones() {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    const auto thing = world.createEntity("Thing");

    std::vector<kg::KGOp> ops;
    ops.emplace_back(kg::KGOpDestroyEntity{by_id(thing)});
    kg::KGOpBatchReport report;
    const bool applied = kg::apply_kg_ops_atomically(ops, world, report);

    CHECK(!applied, "a batch containing destroy_entity must be refused");
    CHECK(world.exists(thing),
          "and the entity it named must still be there");
    CHECK(!report.error.empty(),
          "with a reason: " + report.error);
}

}  // namespace

int main() {
    std::cout << "=== PlannedWorld: the world as the plan will leave it ===\n";
    a_read_resolves_plan_then_graph_then_pending_create();
    a_write_of_the_same_value_is_still_a_write();
    related_lists_committed_first_then_pending_in_order();
    a_plan_cannot_destroy_so_the_view_needs_no_tombstones();
    std::cout << "\n[measure] " << passed << " passed, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}
