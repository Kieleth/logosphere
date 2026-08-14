// Knowledge-layer query algebra tests: selection (types, facets,
// property equals), projection, limit, count, deterministic JSON
// render, and byte-compatibility with the legacy snapshot wrapper.
//
// Usage:
//   ./build/test_kg_query

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_query.h"
#include "generated/logosphere_ontology_registry.h"

#include <iostream>
#include <memory>
#include <string>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// Base ontology + two facet-tagged test types (facets are declared in
// schema YAML in real games; tests extend at runtime, the documented
// pattern from tests/test_ontology_extension.cpp).
std::unique_ptr<kg::KGModule> make_kg() {
    kg::OntologyRegistry ext;
    ext.addEntityType("QTrail", "WorldEntity", false);
    ext.addEntityType("QMenu", "WorldEntity", false);
    ext.addEntityType("QCrop", "WorldEntity", false);
    ext.addFacets("QTrail", {"world"});
    ext.addFacets("QCrop", {"world", "harvestable"});
    ext.addFacets("QMenu", {"ui"});
    // A runtime extension declares its properties like any schema
    // does: setProperty is ontology-gated (Malleus H1).
    ext.addProperty("QTrail", "len", "integer", false);
    ext.addProperty("QTrail", "note", "string", false);
    ext.addProperty("QCrop", "state", "string", false);
    ext.addProperty("QCrop", "kind", "string", false);

    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->extendOntology(ext);
    kg->setMode(kg::KGMode::MINIMAL);
    return kg;
}

}  // namespace

void test_select_by_type_deterministic() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    auto b = kg.createEntity("QTrail");
    auto a = kg.createEntity("QTrail");
    kg.setProperty(a, "len", "5");
    kg.setProperty(b, "len", "3");

    kg::Query q;
    q.types = {"QTrail"};
    auto rows = kg::run_query(kg, q);
    ASSERT(rows.size() == 2, "two trails selected");
    ASSERT(rows[0].id < rows[1].id, "rows sorted by id");
    ASSERT(rows[0].type == "QTrail", "type carried");
    (void)a; (void)b;
}

void test_select_by_facet() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    kg.createEntity("QTrail");
    kg.createEntity("QCrop");
    kg.createEntity("QMenu");

    kg::Query q;
    q.facets = {"world"};
    auto rows = kg::run_query(kg, q);
    ASSERT(rows.size() == 2, "world facet selects trail+crop, not menu");
    for (const auto& r : rows)
        ASSERT(r.type != "QMenu", "ui type excluded by construction");

    kg::Query h;
    h.facets = {"harvestable"};
    ASSERT(kg::run_query(kg, h).size() == 1, "second facet independent");
}

void test_types_then_facets_dedupe() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    kg.createEntity("QTrail");
    kg.createEntity("QCrop");

    kg::Query q;
    q.types = {"QTrail"};
    q.facets = {"world"};   // expands to QCrop + QTrail; QTrail deduped
    auto rows = kg::run_query(kg, q);
    ASSERT(rows.size() == 2, "dedupe across types+facets");
    ASSERT(rows[0].type == "QTrail", "explicit types come first");
}

void test_where_and_projection() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    auto a = kg.createEntity("QCrop");
    auto b = kg.createEntity("QCrop");
    kg.setProperty(a, "state", "ripe");
    kg.setProperty(a, "kind", "corn");
    kg.setProperty(b, "state", "growing");
    kg.setProperty(b, "kind", "corn");

    kg::Query q;
    q.types = {"QCrop"};
    q.where = {{"state", "ripe"}, {"kind", "corn"}};   // ANDed
    q.props = {"state", "missing_prop"};
    auto rows = kg::run_query(kg, q);
    ASSERT(rows.size() == 1, "where predicates AND");
    ASSERT(rows[0].id == a, "the ripe one");
    ASSERT(rows[0].props.size() == 1, "projection keeps only present props");
    ASSERT(rows[0].props[0].first == "state" &&
           rows[0].props[0].second == "ripe", "projected value");
    (void)b;
}

void test_limit_and_count() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    for (int i = 0; i < 5; ++i) kg.createEntity("QTrail");

    kg::Query q;
    q.types = {"QTrail"};
    q.limit = 3;
    ASSERT(kg::run_query(kg, q).size() == 3, "limit caps rows");
    ASSERT(kg::count_query(kg, q) == 3, "count honors limit");
    q.limit = SIZE_MAX;
    ASSERT(kg::count_query(kg, q) == 5, "count without limit");
}

void test_render_json_format() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    auto a = kg.createEntity("QTrail");
    kg.setProperty(a, "len", "5");
    kg.setProperty(a, "note", "a \"quoted\"\nvalue");

    kg::Query q;
    q.types = {"QTrail"};
    auto out = kg::render_query_json(kg::run_query(kg, q));
    // The stable one-entity-per-line contract prompts depend on.
    auto expect_head = "{\"id\":" + std::to_string(a) +
                       ",\"type\":\"QTrail\",\"props\":{";
    ASSERT(out.rfind(expect_head, 0) == 0, "line shape stable");
    ASSERT(out.back() == '\n', "one entity per line");
    ASSERT(out.find("\\\"quoted\\\"") != std::string::npos,
           "JSON escaping intact");
    ASSERT(out.find("\\n") != std::string::npos, "newline escaped");
}

void test_unknown_selects_nothing() {
    auto kg_owner = make_kg();
    auto& kg = *kg_owner;
    kg::Query q;
    q.types = {"NoSuchType"};
    q.facets = {"no_such_facet"};
    ASSERT(kg::run_query(kg, q).empty(), "unknown type/facet -> empty");
    ASSERT(kg::count_query(kg, q) == 0, "count agrees");
}

int main() {
    std::cout << "=== KG Query (Knowledge layer) Tests ===" << std::endl;
    test_select_by_type_deterministic();
    test_select_by_facet();
    test_types_then_facets_dedupe();
    test_where_and_projection();
    test_limit_and_count();
    test_render_json_format();
    test_unknown_selects_nothing();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
