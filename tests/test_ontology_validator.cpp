// Headless test — kg::validate_kg_op gates LLM-proposed mutations
// against the ontology. Catches regressions where:
//   - unknown types or properties leak through
//   - abstract types get instantiated
//   - relations bypass valid_source/valid_target type rules
//   - destroy_entity / set_property targets that don't exist sneak through
//   - unresolved symbolic refs are silently ignored

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_validator.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static kg::OntologyRegistry make_registry() {
    kg::OntologyRegistry r;
    r.addEntityType("Vehicle",     "",        /*abstract=*/true);
    r.addEntityType("Cycle",       "Vehicle", false);
    r.addAncestors("Cycle", {"Vehicle"});
    r.addEntityType("TrailSegment","",        false);
    r.addProperty("Cycle",        "max_speed", "float", false,
                  /*has_min=*/true, 0.1, /*has_max=*/true, 25.0);
    r.addProperty("Cycle",        "x",         "float", false);
    r.addProperty("Cycle",        "lap_count", "integer", false);
    r.addProperty("Cycle",        "is_user",   "boolean", false);
    r.addProperty("TrailSegment", "start_x",   "float", false);
    r.addRefProperty("Cycle",     "trail",     false, "TrailSegment");
    r.addRelationType("PARENT_OF",
        std::unordered_set<std::string>{"Cycle"},
        std::unordered_set<std::string>{"TrailSegment"});
    return r;
}

struct Fixture {
    kg::OntologyRegistry registry = make_registry();
    kg::KGModule kg{registry};
    Fixture() { kg.setMode(kg::KGMode::MINIMAL); }
};

void create_known_concrete_type_ok() {
    Fixture f;
    kg::KGOp op = kg::KGOpCreateEntity{"Cycle", {{"max_speed", "10"}}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

void create_unknown_type_rejected() {
    Fixture f;
    kg::KGOp op = kg::KGOpCreateEntity{"DefinitelyNotAType", {}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "must reject unknown type");
    ASSERT_TRUE(r.reason.find("not in the ontology") != std::string::npos,
        std::string("expected 'not in the ontology' in reason; got '")
        + r.reason + "'");
}

void create_abstract_type_rejected() {
    Fixture f;
    kg::KGOp op = kg::KGOpCreateEntity{"Vehicle", {}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "must reject abstract type instantiation");
    ASSERT_TRUE(r.reason.find("abstract") != std::string::npos,
        "reason mentions abstract");
}

void create_with_unknown_property_rejected() {
    Fixture f;
    kg::KGOp op = kg::KGOpCreateEntity{"Cycle", {{"color_of_helmet", "red"}}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "unknown property must reject");
    ASSERT_TRUE(r.reason.find("color_of_helmet") != std::string::npos,
        "reason names the bad property");
}

void destroy_existing_entity_ok() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpDestroyEntity{ kg::EntityRef{e, ""} };
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

void destroy_missing_entity_rejected() {
    Fixture f;
    kg::KGOp op = kg::KGOpDestroyEntity{ kg::EntityRef{99, ""} };
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "missing entity must reject");
    ASSERT_TRUE(r.reason.find("does not exist") != std::string::npos,
        "reason mentions does-not-exist");
}

void unresolved_symbolic_ref_rejected() {
    Fixture f;
    kg::KGOp op = kg::KGOpDestroyEntity{ kg::EntityRef{kg::INVALID_ENTITY, "ai_cycle"} };
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "unresolved alias must reject");
    ASSERT_TRUE(r.reason.find("@ai_cycle") != std::string::npos,
        "reason includes the alias name");
}

void set_property_on_known_property_ok() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "max_speed", "12"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

void set_property_on_unknown_property_rejected() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "favorite_color", "purple"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "must reject unknown property");
}

void set_relation_with_valid_types_ok() {
    Fixture f;
    auto cyc  = f.kg.createEntity("Cycle");
    auto seg  = f.kg.createEntity("TrailSegment");
    kg::KGOp op = kg::KGOpSetRelation{
        kg::EntityRef{cyc, ""}, "PARENT_OF", kg::EntityRef{seg, ""}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

void set_relation_with_wrong_target_type_rejected() {
    Fixture f;
    auto a = f.kg.createEntity("Cycle");
    auto b = f.kg.createEntity("Cycle");  // PARENT_OF requires TrailSegment target
    kg::KGOp op = kg::KGOpSetRelation{
        kg::EntityRef{a, ""}, "PARENT_OF", kg::EntityRef{b, ""}};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "wrong target type must reject");
    ASSERT_TRUE(r.reason.find("does not accept") != std::string::npos,
        "reason mentions accept");
}

// =========================================================================
// Phase C.3 — schema range annotations enforced.
// =========================================================================
void set_property_within_range_ok() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "max_speed", "12.5"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok at 12.5; got: ") + r.reason);
}

void set_property_above_max_rejected() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "max_speed", "999.0"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "value above max must reject");
    ASSERT_TRUE(r.reason.find("above schema maximum") != std::string::npos,
        std::string("reason mentions max; got: ") + r.reason);
}

void set_property_below_min_rejected() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "max_speed", "0.0"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "value below min must reject");
    ASSERT_TRUE(r.reason.find("below schema minimum") != std::string::npos,
        std::string("reason mentions min; got: ") + r.reason);
}

void set_property_wrong_value_type_rejected() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "lap_count", "three"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "non-integer for integer property must reject");
    ASSERT_TRUE(r.reason.find("expected integer") != std::string::npos,
        "reason mentions expected integer");
}

void set_property_boolean_value_ok() {
    Fixture f;
    auto e = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{e, ""}, "is_user", "true"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

// Entity-reference properties (class-ranged slots): the value must be
// the id of an existing entity of the declared class or a subtype.

void set_ref_property_right_class_ok() {
    Fixture f;
    auto cyc = f.kg.createEntity("Cycle");
    auto seg = f.kg.createEntity("TrailSegment");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{cyc, ""}, "trail", std::to_string(seg)};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(r.ok, std::string("expected ok; got: ") + r.reason);
}

void set_ref_property_wrong_class_rejected() {
    Fixture f;
    auto cyc   = f.kg.createEntity("Cycle");
    auto other = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{cyc, ""}, "trail", std::to_string(other)};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "a Cycle where a TrailSegment belongs must reject");
    ASSERT_TRUE(r.reason.find("not a TrailSegment") != std::string::npos,
        std::string("reason names the expected class; got '") + r.reason + "'");
}

void set_ref_property_junk_rejected() {
    Fixture f;
    auto cyc = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{cyc, ""}, "trail", "banana"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "a non-id value must reject");
}

void set_ref_property_missing_entity_rejected() {
    Fixture f;
    auto cyc = f.kg.createEntity("Cycle");
    kg::KGOp op = kg::KGOpSetProperty{
        kg::EntityRef{cyc, ""}, "trail", "999"};
    auto r = kg::validate_kg_op(op, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "a dangling id must reject");
}

void create_with_ref_property_validated() {
    Fixture f;
    kg::KGOp bad = kg::KGOpCreateEntity{"Cycle", {{"trail", "banana"}}};
    auto r = kg::validate_kg_op(bad, f.kg, f.registry);
    ASSERT_TRUE(!r.ok, "create_entity must not bypass the ref check");

    auto seg = f.kg.createEntity("TrailSegment");
    kg::KGOp ok = kg::KGOpCreateEntity{"Cycle",
                                       {{"trail", std::to_string(seg)}}};
    auto r2 = kg::validate_kg_op(ok, f.kg, f.registry);
    ASSERT_TRUE(r2.ok, std::string("expected ok; got: ") + r2.reason);
}

int main() {
    std::cout << "=== test_ontology_validator ===" << std::endl;
    TEST(create_known_concrete_type_ok);
    TEST(create_unknown_type_rejected);
    TEST(create_abstract_type_rejected);
    TEST(create_with_unknown_property_rejected);
    TEST(destroy_existing_entity_ok);
    TEST(destroy_missing_entity_rejected);
    TEST(unresolved_symbolic_ref_rejected);
    TEST(set_property_on_known_property_ok);
    TEST(set_property_on_unknown_property_rejected);
    TEST(set_relation_with_valid_types_ok);
    TEST(set_relation_with_wrong_target_type_rejected);
    TEST(set_property_within_range_ok);
    TEST(set_property_above_max_rejected);
    TEST(set_property_below_min_rejected);
    TEST(set_property_wrong_value_type_rejected);
    TEST(set_property_boolean_value_ok);
    TEST(set_ref_property_right_class_ok);
    TEST(set_ref_property_wrong_class_rejected);
    TEST(set_ref_property_junk_rejected);
    TEST(set_ref_property_missing_entity_rejected);
    TEST(create_with_ref_property_validated);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
