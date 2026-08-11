#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/rules/rule_fork.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>

static int passed = 0;
static int failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++passed; std::cout << "PASS\n"; }                     \
    catch (const std::exception& error) {                                 \
        ++failed; std::cout << "FAIL: " << error.what() << '\n';         \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

namespace {

struct Fixture {
    kg::KGModule world{rulebook::ontology::registry()};
    kg::EntityID source = kg::INVALID_ENTITY;
    kg::EntityID related = kg::INVALID_ENTITY;
    kg::EntityID runtime = kg::INVALID_ENTITY;
    kg::EntityID source_context = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        const auto parsed = kg::parse_seed_envelope(R"({
          "source":{"file":"book.md","commit":"abc123"},
          "layer":"published",
          "ops":[
            {"op":"create_entity","type":"RuleConstant","as":"@rule",
             "properties":{"name":"age","constant_value":18,
                           "source_quote":"Age is 18."}},
            {"op":"create_entity","type":"RuleConstant","as":"@base",
             "properties":{"name":"base","constant_value":1,
                           "source_quote":"Base is 1."}},
            {"op":"set_relation","from":"@rule",
             "relation":"SPECIALIZES","to":"@base"}
          ]
        })");
        REQUIRE(parsed.ok(), "fixture seed parses: " + parsed.error);
        kg::SeedLoadReport loaded;
        REQUIRE(kg::load_seed(parsed.seed, world, loaded),
                "fixture seed loads: " + loaded.error);
        source = loaded.bindings.at("rule");
        related = loaded.bindings.at("base");
        source_context = loaded.source_document_context;

        kg::KGOpBatchReport context;
        REQUIRE(kg::apply_kg_ops_atomically(
                    {kg::KGOp{kg::KGOpCreateEntity{
                        "RuntimeContext",
                        {{"context_key", "session:one"},
                         {"context_kind", "session"}},
                        "runtime"}}},
                    world, context),
                "runtime context creates: " + context.error);
        runtime = context.bindings.at("runtime");
    }
};

void a_fork_copies_content_and_changes_only_requested_fields() {
    Fixture fixture;
    const auto result = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.runtime, "age-session-one",
         {{"constant_value", "21"}}});
    REQUIRE(result.ok, "fork succeeds: " + result.error);
    REQUIRE(result.entity != kg::INVALID_ENTITY &&
                result.entity != fixture.source,
            "fork has new identity");
    REQUIRE(fixture.world.getProperty(fixture.source, "constant_value") ==
                "18",
            "published source remains unchanged");
    REQUIRE(fixture.world.getProperty(result.entity, "constant_value") ==
                "21" &&
                fixture.world.getProperty(result.entity, "name") == "age" &&
                fixture.world.getProperty(result.entity, "source_quote") ==
                    "Age is 18.",
            "fork copies source content and applies the explicit override");
    REQUIRE(fixture.world.getProperty(result.entity, "origin_context") ==
                std::to_string(fixture.runtime) &&
                fixture.world.getProperty(result.entity,
                                          "identity_context") ==
                    std::to_string(fixture.runtime) &&
                fixture.world.getProperty(result.entity, "entity_key") ==
                    "age-session-one" &&
                fixture.world.getProperty(result.entity, "fork_of") ==
                    std::to_string(fixture.source),
            "fork records its runtime origin and immediate source lineage");
    REQUIRE(fixture.world.getRelated(result.entity, "SPECIALIZES") ==
                std::vector<kg::EntityID>{fixture.related},
            "fork preserves outgoing rule composition");
}

void a_runtime_fork_remains_mutable() {
    Fixture fixture;
    const auto forked = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.runtime, "mutable-age",
         {{"constant_value", "21"}}});
    REQUIRE(forked.ok, "fork succeeds");
    kg::KGOpBatchReport update;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpSetProperty{
                    {forked.entity, ""}, "constant_value", "22"}}},
                fixture.world, update),
            "runtime fork accepts later validated edits: " + update.error);
    REQUIRE(fixture.world.getProperty(forked.entity, "constant_value") ==
                "22",
            "validated edit commits on the fork");
}

void invalid_forks_fail_atomically() {
    Fixture fixture;
    const size_t before = fixture.world.findByType("RuleConstant").size();

    const auto bad_value = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.runtime, "bad-value",
         {{"constant_value", "banana"}}});
    REQUIRE(!bad_value.ok &&
                bad_value.error.find("expected integer") != std::string::npos,
            "invalid override is rejected with the schema reason");

    const auto forged_origin = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.runtime, "forged-origin",
         {{"origin_context", std::to_string(fixture.source_context)}}});
    REQUIRE(!forged_origin.ok &&
                forged_origin.error.find("cannot override") !=
                    std::string::npos,
            "caller cannot rewrite fork provenance fields");

    const auto source_destination = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.source_context, "source-destination",
         {{"constant_value", "21"}}});
    REQUIRE(!source_destination.ok &&
                source_destination.error.find("RuntimeContext") !=
                    std::string::npos,
            "a runtime fork cannot claim a published source context");

    const auto missing_key = logosphere::rules::fork_rule(
        fixture.world,
        {fixture.source, fixture.runtime, "", {{"constant_value", "21"}}});
    REQUIRE(!missing_key.ok &&
                missing_key.error.find("entity key") != std::string::npos,
            "a fork requires an explicit portable entity key");
    REQUIRE(fixture.world.findByType("RuleConstant").size() == before,
            "every rejected fork leaves the graph unchanged");
}

}  // namespace

int main() {
    std::cout << "Rule copy-on-write forks\n";
    TEST(a_fork_copies_content_and_changes_only_requested_fields);
    TEST(a_runtime_fork_remains_mutable);
    TEST(invalid_forks_fail_atomically);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
