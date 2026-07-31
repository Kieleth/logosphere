// Headless test — kg::parse_kg_ops parses the Director's KGOp
// JSON shape into typed variants. Permissive but loud: bad JSON
// is fatal, malformed entries are warnings (the entry is dropped).
//
// Catches regressions in:
//   - the four op kinds (create_entity, destroy_entity,
//     set_property, set_relation)
//   - EntityRef parsing (numeric / "@alias" / stringified id)
//   - bad-shape entries become warnings, not crashes
//   - bare array vs {"ops":[...]} both accepted

#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_ops_parse.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

void parses_create_entity_with_properties() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"create_entity","type":"Wormhole",
         "properties":{"x":"3.5","y":"-2","pair_id":"north_west"}}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error expected");
    ASSERT_TRUE(r.ops.size() == 1, "one op");
    auto* ce = std::get_if<kg::KGOpCreateEntity>(&r.ops[0]);
    ASSERT_TRUE(ce != nullptr, "must be CreateEntity variant");
    ASSERT_TRUE(ce->type == "Wormhole", "type round-trips");
    ASSERT_TRUE(ce->properties.size() == 3, "three properties");
}

void parses_destroy_entity_with_symbolic_target() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"destroy_entity","target":"@some_entity"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    auto* de = std::get_if<kg::KGOpDestroyEntity>(&r.ops[0]);
    ASSERT_TRUE(de != nullptr, "must be DestroyEntity variant");
    ASSERT_TRUE(de->target.is_symbolic(), "symbolic target");
    ASSERT_TRUE(de->target.symbolic == "some_entity",
        "@ stripped from symbolic name");
}

void parses_set_property_with_numeric_target() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"set_property","target":7,
         "property":"max_speed","value":"12"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    auto* sp = std::get_if<kg::KGOpSetProperty>(&r.ops[0]);
    ASSERT_TRUE(sp != nullptr, "must be SetProperty");
    ASSERT_TRUE(sp->target.is_numeric() && sp->target.id == 7,
        "numeric target id");
    ASSERT_TRUE(sp->property == "max_speed", "property name");
    ASSERT_TRUE(sp->value == "12", "value (string-encoded)");
}

void parses_set_property_with_stringified_id() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"set_property","target":"42",
         "property":"x","value":"3.5"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    auto* sp = std::get_if<kg::KGOpSetProperty>(&r.ops[0]);
    ASSERT_TRUE(sp != nullptr, "must be SetProperty");
    ASSERT_TRUE(sp->target.is_numeric() && sp->target.id == 42,
        "stringified id parses as numeric");
}

void parses_set_relation() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"set_relation","from":"@a","relation":"PARENT_OF","to":"@b"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    auto* sr = std::get_if<kg::KGOpSetRelation>(&r.ops[0]);
    ASSERT_TRUE(sr != nullptr, "must be SetRelation");
    ASSERT_TRUE(sr->from.symbolic == "a", "from alias");
    ASSERT_TRUE(sr->to.symbolic == "b",   "to alias");
    ASSERT_TRUE(sr->relation == "PARENT_OF", "relation name");
}

void unknown_op_kind_warns_and_continues() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"summon_dragon","params":{}},
        {"op":"set_property","target":7,"property":"x","value":"3"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no fatal error");
    ASSERT_TRUE(r.ops.size() == 1, "good op survives");
    ASSERT_TRUE(!r.warnings.empty(), "unknown op warned about");
}

void missing_required_fields_warn() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"create_entity"},
        {"op":"set_property","target":"@x"},
        {"op":"set_relation","from":"@a"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no fatal");
    ASSERT_TRUE(r.ops.empty(), "no op survives missing required fields");
    ASSERT_TRUE(r.warnings.size() == 3, "one warning per malformed op");
}

void bad_json_is_fatal() {
    auto r = kg::parse_kg_ops("not json at all");
    ASSERT_TRUE(!r.parse_error.empty(), "fatal parse error expected");
    ASSERT_TRUE(r.ops.empty(), "no ops on fatal error");
}

void bare_array_accepted() {
    auto r = kg::parse_kg_ops(R"([
        {"op":"destroy_entity","target":"@x"}
    ])");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    ASSERT_TRUE(r.ops.size() == 1, "bare array also produces ops");
}

void v0_9_shape_yields_warning_not_error() {
    // The legacy shape uses {"mutations":[...]}; we don't parse it
    // here but we must not crash, just warn.
    auto r = kg::parse_kg_ops(R"({"mutations":[{"type":"clear_trails"}]})");
    ASSERT_TRUE(r.parse_error.empty(), "soft fall-through");
    ASSERT_TRUE(r.ops.empty(), "no ops parsed from legacy shape");
    ASSERT_TRUE(!r.warnings.empty(), "warns about missing 'ops' array");
}

void op_kind_name_returns_correct_string() {
    kg::KGOp ce = kg::KGOpCreateEntity{"Cycle", {}};
    kg::KGOp de = kg::KGOpDestroyEntity{};
    kg::KGOp sp = kg::KGOpSetProperty{};
    kg::KGOp sr = kg::KGOpSetRelation{};
    kg::KGOp pc = kg::KGOpPlayCinematic{};
    ASSERT_TRUE(std::string(kg::kg_op_kind_name(ce)) == "create_entity",  "ce name");
    ASSERT_TRUE(std::string(kg::kg_op_kind_name(de)) == "destroy_entity", "de name");
    ASSERT_TRUE(std::string(kg::kg_op_kind_name(sp)) == "set_property",   "sp name");
    ASSERT_TRUE(std::string(kg::kg_op_kind_name(sr)) == "set_relation",   "sr name");
    ASSERT_TRUE(std::string(kg::kg_op_kind_name(pc)) == "play_cinematic", "pc name");
}

void parses_play_cinematic_with_target_and_params() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"play_cinematic","name":"disk_throw_at",
         "target":"@player_cycle",
         "params":{"speed":"4.0","arc":"high"}}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    ASSERT_TRUE(r.ops.size() == 1, "one op");
    auto* pc = std::get_if<kg::KGOpPlayCinematic>(&r.ops[0]);
    ASSERT_TRUE(pc != nullptr, "must be PlayCinematic variant");
    ASSERT_TRUE(pc->name == "disk_throw_at", "name round-trips");
    ASSERT_TRUE(pc->target.is_symbolic() && pc->target.symbolic == "player_cycle",
        "target alias parsed");
    ASSERT_TRUE(pc->params.size() == 2, "params parsed");
}

void parses_play_cinematic_no_target_no_params() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"play_cinematic","name":"freeze_zoom"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no parse error");
    auto* pc = std::get_if<kg::KGOpPlayCinematic>(&r.ops[0]);
    ASSERT_TRUE(pc != nullptr, "must be PlayCinematic variant");
    ASSERT_TRUE(pc->target.id == kg::INVALID_ENTITY && pc->target.symbolic.empty(),
        "no target → invalid ref");
    ASSERT_TRUE(pc->params.empty(), "no params");
}

void play_cinematic_missing_name_warns() {
    auto r = kg::parse_kg_ops(R"({"ops":[
        {"op":"play_cinematic","target":"@x"}
    ]})");
    ASSERT_TRUE(r.parse_error.empty(), "no fatal");
    ASSERT_TRUE(r.ops.empty(), "missing name → no op");
    ASSERT_TRUE(!r.warnings.empty(), "warning surfaced");
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== test_kg_ops_parse ===" << std::endl;
    TEST(parses_create_entity_with_properties);
    TEST(parses_destroy_entity_with_symbolic_target);
    TEST(parses_set_property_with_numeric_target);
    TEST(parses_set_property_with_stringified_id);
    TEST(parses_set_relation);
    TEST(unknown_op_kind_warns_and_continues);
    TEST(missing_required_fields_warn);
    TEST(bad_json_is_fatal);
    TEST(bare_array_accepted);
    TEST(v0_9_shape_yields_warning_not_error);
    TEST(op_kind_name_returns_correct_string);
    TEST(parses_play_cinematic_with_target_and_params);
    TEST(parses_play_cinematic_no_target_no_params);
    TEST(play_cinematic_missing_name_warns);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
