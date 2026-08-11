// Atomic validated KG-op batches. Outcome handlers plan operations; this
// boundary is the only place a complete plan may touch world state.

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
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

kg::OntologyRegistry registry() {
    kg::OntologyRegistry out;
    out.addEntityType("Parent", "", false);
    out.addEntityType("Child", "", false);
    out.addProperty("Parent", "value", "integer", false);
    out.addProperty("Child", "label", "string", true);
    out.addRelationType("HAS_PART", {"Parent"}, {"Child"});
    // A reference to something a different seed created.
    out.addRefProperty("Child", "points_at", false, "Parent");
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    logosphere::EventBus events;
    kg::EntityID parent = kg::INVALID_ENTITY;
    int property_events = 0;
    int relation_events = 0;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        parent = world.createEntity("Parent");
        world.setProperty(parent, "value", "1");
        world.set_event_bus(&events);
        events.state_changes().subscribe(
            [&](const logosphere::ontology::WorldEvent&) {
                ++property_events;
            });
        events.relations().subscribe(
            [&](const logosphere::ontology::RelationEvent&) {
                ++relation_events;
            });
    }
};

std::vector<kg::KGOp> valid_plan(kg::EntityID parent) {
    return {
        kg::KGOpSetProperty{{parent, ""}, "value", "2"},
        kg::KGOpCreateEntity{"Child", {{"label", "planned"}}, "child"},
        kg::KGOpSetRelation{{parent, ""}, "HAS_PART",
                            {kg::INVALID_ENTITY, "child"}},
    };
}

void a_late_failure_restores_everything_and_emits_nothing() {
    Fixture f;
    auto ops = valid_plan(f.parent);
    ops.emplace_back(
        kg::KGOpSetProperty{{f.parent, ""}, "missing_property", "3"});

    kg::KGOpBatchReport report;
    REQUIRE(!kg::apply_kg_ops_atomically(ops, f.world, report),
            "the invalid batch must fail");
    REQUIRE(report.failed_op == 3 &&
                report.error.find("missing_property") != std::string::npos,
            "the report must identify the late invalid operation");
    REQUIRE(f.world.getProperty(f.parent, "value") == "1",
            "the earlier property write must roll back");
    REQUIRE(f.world.findByType("Child").empty(),
            "the created entity must roll back");
    REQUIRE(f.world.getRelated(f.parent, "HAS_PART").empty(),
            "the created relation must roll back");
    REQUIRE(f.property_events == 0 && f.relation_events == 0,
            "a failed batch must publish no mutation events");
    REQUIRE(report.bindings.empty() && report.created_ids.empty() &&
                report.ops_applied == 0,
            "a failed report must expose no partial state");
}

void a_success_commits_once_and_publishes_afterward() {
    Fixture f;
    auto ops = valid_plan(f.parent);
    kg::KGOpBatchReport report;
    REQUIRE(kg::apply_kg_ops_atomically(ops, f.world, report),
            "the valid batch must commit: " + report.error);
    REQUIRE(f.world.getProperty(f.parent, "value") == "2",
            "the property write must commit");
    REQUIRE(report.bindings.count("child") == 1 &&
                f.world.exists(report.bindings.at("child")),
            "the create alias must resolve to the committed child");
    REQUIRE(f.world.getRelated(f.parent, "HAS_PART") ==
                std::vector<kg::EntityID>{report.bindings.at("child")},
            "the symbolic relation must target the child");
    REQUIRE(report.ops_applied == 3 && report.created_ids.size() == 3,
            "the report covers the complete batch");
    REQUIRE(f.property_events == 2 && f.relation_events == 1,
            "successful mutation events publish after the commit");
}

void unsupported_destructive_ops_fail_before_mutation() {
    Fixture f;
    auto ops = valid_plan(f.parent);
    ops.insert(ops.begin(), kg::KGOpDestroyEntity{{f.parent, ""}});
    kg::KGOpBatchReport report;
    REQUIRE(!kg::apply_kg_ops_atomically(ops, f.world, report),
            "destroy must be rejected by the additive transaction");
    REQUIRE(report.failed_op == 0 && f.world.exists(f.parent),
            "the destructive operation must touch nothing");
}

// An alias is file-local. "@@Type:Name" is the one way across that
// boundary, and it must be narrow enough to be trustworthy.
void a_world_reference_finds_an_entity_another_batch_created() {
    Fixture f;
    f.world.setProperty(f.parent, "name", "Admin");
    std::vector<kg::KGOp> ops;
    kg::KGOpCreateEntity child;
    child.type = "Child";
    child.as = "c";
    child.properties = {{"label", "x"}, {"points_at", "@@Parent:Admin"}};
    ops.push_back(child);
    kg::KGOpBatchReport report;
    REQUIRE(kg::apply_kg_ops_atomically(ops, f.world, report),
            "a world reference to a loaded entity must resolve");
    const auto made = report.bindings.at("c");
    REQUIRE(f.world.getProperty(made, "points_at") ==
                std::to_string(f.parent),
            "it must bind to the entity that already exists");
}

// The career tables print "Jack o' Trades"; the skills chapter defines
// "Jack-of-All-Trades (Jack o' Trades or JoT)". One skill, two names,
// both the book's own.
void a_world_reference_resolves_through_a_source_alias() {
    Fixture f;
    f.world.setProperty(f.parent, "name", "Jack-of-All-Trades");
    f.world.setProperty(f.parent, "source_aliases", "Jack o' Trades; JoT");
    std::vector<kg::KGOp> ops;
    kg::KGOpCreateEntity child;
    child.type = "Child";
    child.as = "c";
    child.properties = {{"label", "x"}, {"points_at", "@@Parent:JoT"}};
    ops.push_back(child);
    kg::KGOpBatchReport report;
    REQUIRE(kg::apply_kg_ops_atomically(ops, f.world, report),
            "an alias the source itself uses must resolve: " + report.error);
    REQUIRE(f.world.getProperty(report.bindings.at("c"), "points_at") ==
                std::to_string(f.parent),
            "and it must reach the entity that carries the alias");
}

// An alias must never beat a real name, or a seed could be silently
// redirected by someone else's nickname.
void an_exact_name_wins_over_someone_elses_alias() {
    Fixture f;
    f.world.setProperty(f.parent, "name", "Recon");
    const auto other = f.world.createEntity("Parent");
    f.world.setProperty(other, "name", "Perception");
    f.world.setProperty(other, "source_aliases", "Recon");
    std::vector<kg::KGOp> ops;
    kg::KGOpCreateEntity child;
    child.type = "Child";
    child.as = "c";
    child.properties = {{"label", "x"}, {"points_at", "@@Parent:Recon"}};
    ops.push_back(child);
    kg::KGOpBatchReport report;
    REQUIRE(kg::apply_kg_ops_atomically(ops, f.world, report),
            "the exact name must resolve: " + report.error);
    REQUIRE(f.world.getProperty(report.bindings.at("c"), "points_at") ==
                std::to_string(f.parent),
            "to the entity NAMED Recon, not the one aliased Recon");
}

void a_world_reference_to_nothing_fails_loudly() {
    Fixture f;
    f.world.setProperty(f.parent, "name", "Admin");
    std::vector<kg::KGOp> ops;
    kg::KGOpCreateEntity child;
    child.type = "Child";
    child.as = "c";
    child.properties = {{"label", "x"}, {"points_at", "@@Parent:Nope"}};
    ops.push_back(child);
    kg::KGOpBatchReport report;
    REQUIRE(!kg::apply_kg_ops_atomically(ops, f.world, report),
            "a reference to a name nobody loaded must fail");
    REQUIRE(report.error.find("Nope") != std::string::npos,
            "the error must name what could not be found: " + report.error);
}

void an_ambiguous_world_reference_fails_rather_than_picking() {
    Fixture f;
    f.world.setProperty(f.parent, "name", "Admin");
    const auto twin = f.world.createEntity("Parent");
    f.world.setProperty(twin, "name", "Admin");
    std::vector<kg::KGOp> ops;
    kg::KGOpCreateEntity child;
    child.type = "Child";
    child.as = "c";
    child.properties = {{"label", "x"}, {"points_at", "@@Parent:Admin"}};
    ops.push_back(child);
    kg::KGOpBatchReport report;
    REQUIRE(!kg::apply_kg_ops_atomically(ops, f.world, report),
            "two entities of that name must not silently bind to one");
    REQUIRE(report.error.find("ambiguous") != std::string::npos,
            "the error must say why: " + report.error);
}

}  // namespace

int main() {
    std::cout << "Atomic KG operation batches\n";
    TEST(a_late_failure_restores_everything_and_emits_nothing);
    TEST(a_success_commits_once_and_publishes_afterward);
    TEST(unsupported_destructive_ops_fail_before_mutation);
    TEST(a_world_reference_finds_an_entity_another_batch_created);
    TEST(a_world_reference_resolves_through_a_source_alias);
    TEST(an_exact_name_wins_over_someone_elses_alias);
    TEST(a_world_reference_to_nothing_fails_loudly);
    TEST(an_ambiguous_world_reference_fails_rather_than_picking);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
