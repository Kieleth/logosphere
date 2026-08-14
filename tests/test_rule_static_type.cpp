#include "logosphere/events/event_bus.h"
#include "logosphere/kg/finite_float.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_meta_graph.h"
#include "logosphere/rules/rule_static_type.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <cfenv>
#include <stdexcept>
#include <string>

namespace {

int passed = 0;
int failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++passed; std::cout << "PASS\n"; }                     \
    catch (const std::exception& error) {                                 \
        ++failed; std::cout << "FAIL: " << error.what() << '\n';         \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

kg::OntologyRegistry expression_registry() {
    kg::OntologyRegistry out = rulebook::ontology::registry();
    kg::OntologyRegistry test("schema://rule-static-type-test");
    test.addEntityType("TestIntegerExpression", "IntegerExpression", false);
    test.addAncestors("TestIntegerExpression",
                      {"IntegerExpression", "NumericExpression",
                       "ScalarExpression", "Expression", "Entity"});
    test.addEntityType("TestUntypedExpression", "Expression", false);
    test.addAncestors("TestUntypedExpression", {"Expression", "Entity"});
    out.extend(test);
    return out;
}

void expression_families_are_abstract_and_closed() {
    const auto& ontology = rulebook::ontology::registry();
    const char* families[] = {
        "Expression", "ScalarExpression", "BooleanExpression",
        "NumericExpression", "IntegerExpression", "FloatExpression",
        "StringExpression", "EntityExpression", "CollectionExpression",
        "BooleanCollectionExpression", "IntegerCollectionExpression",
        "FloatCollectionExpression", "StringCollectionExpression",
        "EntityCollectionExpression", "FunctionExpression",
        "OutcomePlanExpression",
    };
    for (const char* family : families) {
        REQUIRE(ontology.hasEntityType(family) && ontology.isAbstract(family),
                std::string(family) + " is an abstract ontology family");
    }
    REQUIRE(ontology.isSubtypeOf("IntegerExpression", "NumericExpression") &&
                ontology.isSubtypeOf("FloatExpression", "NumericExpression") &&
                ontology.isSubtypeOf("NumericExpression", "ScalarExpression") &&
                ontology.isSubtypeOf("EntityCollectionExpression",
                                     "CollectionExpression") &&
                !ontology.isSubtypeOf("StringExpression",
                                      "NumericExpression"),
            "the family tree enforces broad operand compatibility");
    REQUIRE(!ontology.hasEntityType("EnumExpression") &&
                !ontology.hasEntityType("TemporalExpression"),
            "deferred enum and temporal families are absent, not Strings");
}

void concrete_expression_roots_receive_one_static_family() {
    kg::KGModule world(expression_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport meta;
    REQUIRE(kg::materialize_ontology_meta_graph(world, meta), meta.error);

    kg::KGOpBatchReport batch;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "TestIntegerExpression", {}, "integer"}},
                 kg::KGOp{kg::KGOpCreateEntity{
                    "TestUntypedExpression", {}, "untyped"}}},
                world, batch),
            "test expressions create: " + batch.error);

    const auto inferred = logosphere::rules::infer_expression_static_type(
        world, batch.bindings.at("integer"));
    REQUIRE(inferred.ok &&
                inferred.type.family ==
                    logosphere::rules::RuleValueFamily::Integer,
            "a concrete IntegerExpression has Integer static type: " +
                inferred.error);

    const auto valid = logosphere::rules::validate_expression_static_type(
        world, batch.bindings.at("integer"),
        logosphere::rules::RuleValueFamily::Integer);
    REQUIRE(valid.ok, "matching root type validates: " + valid.error);

    const auto mismatch = logosphere::rules::validate_expression_static_type(
        world, batch.bindings.at("integer"),
        logosphere::rules::RuleValueFamily::Boolean);
    REQUIRE(!mismatch.ok &&
                mismatch.error.find("expected Boolean") != std::string::npos &&
                mismatch.error.find("actual Integer") != std::string::npos &&
                mismatch.error.find("TestIntegerExpression") !=
                    std::string::npos,
            "type errors name node class, expected, and actual family: " +
                mismatch.error);

    const auto untyped = logosphere::rules::infer_expression_static_type(
        world, batch.bindings.at("untyped"));
    REQUIRE(!untyped.ok &&
                untyped.error.find("no concrete result family") !=
                    std::string::npos,
            "the generic Expression root is not an executable escape hatch");
}

void property_types_come_from_canonical_meta_entities() {
    kg::KGModule world(expression_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport meta;
    REQUIRE(kg::materialize_ontology_meta_graph(world, meta), meta.error);

    const auto integer = logosphere::rules::infer_property_static_type(
        world, "@@meta/property/TaskCheck/target_number");
    REQUIRE(integer.ok &&
                integer.type.family ==
                    logosphere::rules::RuleValueFamily::Integer &&
                integer.type.refinement.empty(),
            "integer property metadata produces Integer, not a parsed string");

    const auto entity = logosphere::rules::infer_property_static_type(
        world, "@@meta/property/TaskCheck/dice");
    REQUIRE(entity.ok &&
                entity.type.family ==
                    logosphere::rules::RuleValueFamily::Entity &&
                entity.type.refinement == "@@meta/class/DiceExpression",
            "entity-reference properties preserve exact class refinement: " +
                entity.error);

    const auto enumeration = logosphere::rules::infer_property_static_type(
        world, "@@meta/property/HasMaterial/material_type");
    REQUIRE(!enumeration.ok &&
                enumeration.error.find("enum 'MaterialType'") !=
                    std::string::npos &&
                enumeration.error.find("unsupported executable family") !=
                    std::string::npos &&
                enumeration.error.find("String") == std::string::npos,
            "enum properties fail with their nominal type and no String "
            "fallback: " + enumeration.error);

    const auto datetime = logosphere::rules::infer_property_static_type(
        world, "@@meta/property/Temporal/created_at");
    REQUIRE(!datetime.ok &&
                datetime.error.find("legacy datetime") != std::string::npos &&
                datetime.error.find("unsupported executable family") !=
                    std::string::npos,
            "legacy datetime properties fail explicitly: " + datetime.error);
}

kg::OntologyRegistry float_registry() {
    kg::OntologyRegistry out("schema://finite-float-test");
    out.addEntityType("Metric", "", false);
    out.addProperty("Metric", "score", kg::PropertyValueKind::Float, true);
    return out;
}

void validated_float_writes_reject_nonfinite_and_canonicalize_zero() {
    kg::KGModule world(float_registry());
    world.setMode(kg::KGMode::MINIMAL);

    kg::KGOpBatchReport created;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "Metric", {{"score", "-0.0"}}, "metric"}}},
                world, created),
            "negative zero create succeeds: " + created.error);
    const auto metric = created.bindings.at("metric");
    REQUIRE(world.getProperty(metric, "score") == "0",
            "validated storage canonicalizes negative zero");

    for (const std::string value : {"nan", "inf", "-inf"}) {
        kg::KGOpBatchReport invalid;
        REQUIRE(!kg::apply_kg_ops_atomically(
                    {kg::KGOp{kg::KGOpSetProperty{
                        {metric, ""}, "score", value}}},
                    world, invalid) &&
                    invalid.error.find("finite float") != std::string::npos &&
                    world.getProperty(metric, "score") == "0",
                "non-finite float '" + value +
                    "' fails without changing stored state: " + invalid.error);
    }

    kg::KGOpBatchReport updated;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpSetProperty{
                    {metric, ""}, "score", "-0e100"}}},
                world, updated) &&
                world.getProperty(metric, "score") == "0",
            "set_property canonicalizes every negative-zero spelling");
}

void binary64_parsing_ignores_and_restores_caller_rounding_mode() {
    const int original_mode = std::fegetround();
    REQUIRE(original_mode != -1, "the platform exposes a floating-point mode");
    REQUIRE(std::fesetround(FE_UPWARD) == 0,
            "the test can install a hostile caller rounding mode");

    const auto parsed = kg::parse_finite_binary64(
        "1.00000000000000011102230246251565404236316680908203125");
    const int restored_mode = std::fegetround();
    std::fesetround(original_mode);

    REQUIRE(parsed.ok && parsed.value == 1.0,
            "halfway binary64 input uses round-to-nearest ties-to-even: " +
                parsed.error);
    REQUIRE(restored_mode == FE_UPWARD,
            "finite parsing restores the caller's rounding mode");
}

void static_validation_is_observably_read_only() {
    kg::KGModule world(expression_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport meta;
    REQUIRE(kg::materialize_ontology_meta_graph(world, meta), meta.error);
    kg::KGOpBatchReport batch;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "TestIntegerExpression", {}, "integer"}}},
                world, batch), batch.error);

    logosphere::EventBus events;
    int state_events = 0;
    int relation_events = 0;
    events.state_changes().subscribe(
        [&](const logosphere::ontology::WorldEvent&) { ++state_events; });
    events.relations().subscribe(
        [&](const logosphere::ontology::RelationEvent&) { ++relation_events; });
    world.set_event_bus(&events);
    const auto before = world.getStats();

    const auto result = logosphere::rules::validate_expression_static_type(
        world, batch.bindings.at("integer"),
        logosphere::rules::RuleValueFamily::Integer);
    const auto property = logosphere::rules::infer_property_static_type(
        world, "@@meta/property/TaskCheck/target_number");
    const auto after = world.getStats();
    REQUIRE(result.ok && property.ok &&
                before.entity_count == after.entity_count &&
                before.relation_count == after.relation_count &&
                state_events == 0 && relation_events == 0,
            "static validation reads one graph snapshot and emits nothing");
}

}  // namespace

int main() {
    std::cout << "Rule-language static value families\n";
    TEST(expression_families_are_abstract_and_closed);
    TEST(concrete_expression_roots_receive_one_static_family);
    TEST(property_types_come_from_canonical_meta_entities);
    TEST(validated_float_writes_reject_nonfinite_and_canonicalize_zero);
    TEST(binary64_parsing_ignores_and_restores_caller_rounding_mode);
    TEST(static_validation_is_observably_read_only);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
