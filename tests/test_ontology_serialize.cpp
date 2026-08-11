// Headless test — kg::serialize_ontology_slice produces valid,
// stable, compact JSON for a slice of the ontology. Catches
// regressions in:
//   * unknown types not silently corrupting JSON shape
//   * properties losing their value kind or required flag
//   * string escaping leaking quotes/backslashes through
//   * abstract / concrete flag round-trip

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_serialize.h"
#include "logosphere/kg/kg_query.h"

// serialize_kg_snapshot was deleted (2026-07-30) — snapshots are the
// query algebra now. These cases keep locking the same behavior
// (type filtering, empty selection, determinism) through it.
static std::string snapshot_via_query(const kg::KGModule& kg,
                                      const std::vector<std::string>& types) {
    kg::Query q;
    q.types = types;
    return kg::render_query_json(kg::run_query(kg, q));
}

#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// =========================================================================
// Test — basic slice carries type, parent, abstract flag, and properties.
// =========================================================================
void emits_basic_type_with_properties() {
    kg::OntologyRegistry r;
    r.addEntityType("Cycle", "PhysicsEntity", /*abstract=*/false);
    r.addProperty("Cycle", "max_speed", kg::PropertyValueKind::Float,
                  /*required=*/false);
    r.addProperty("Cycle", "x", kg::PropertyValueKind::Float,
                  /*required=*/true);
    r.addProperty("Cycle", "name", kg::PropertyValueKind::String,
                  /*required=*/false);

    std::string out = kg::serialize_ontology_slice(r, {"Cycle"});

    ASSERT_TRUE(contains(out, "\"Cycle\":"), "type name must appear");
    ASSERT_TRUE(contains(out, "\"parent\":\"PhysicsEntity\""),
        "parent must appear");
    ASSERT_TRUE(contains(out, "\"abstract\":false"),
        "abstract flag must appear");
    ASSERT_TRUE(contains(out, "\"max_speed\":{\"type\":\"float\"}"),
        "non-required float property must round-trip");
    ASSERT_TRUE(contains(out, "\"x\":{\"type\":\"float\",\"required\":true}"),
        "required flag must appear when set");
    ASSERT_TRUE(contains(out, "\"name\":{\"type\":\"string\"}"),
        "string property must round-trip");

    // Output must be a single line (no embedded \n).
    ASSERT_TRUE(out.find('\n') == std::string::npos,
        "output must be single-line JSON");
}

// =========================================================================
// Test — multiple types comma-separated; abstract types carry the flag.
// =========================================================================
void emits_multiple_types_and_abstract_flag() {
    kg::OntologyRegistry r;
    r.addEntityType("Vehicle",     "Entity",  /*abstract=*/true);
    r.addEntityType("Cycle",       "Vehicle", /*abstract=*/false);
    r.addEntityType("Motorcycle",  "Cycle",   /*abstract=*/false);

    std::string out = kg::serialize_ontology_slice(
        r, {"Vehicle", "Cycle", "Motorcycle"});

    ASSERT_TRUE(contains(out, "\"Vehicle\":"),    "Vehicle missing");
    ASSERT_TRUE(contains(out, "\"Cycle\":"),      "Cycle missing");
    ASSERT_TRUE(contains(out, "\"Motorcycle\":"), "Motorcycle missing");
    ASSERT_TRUE(contains(out, "\"abstract\":true"),
        "abstract types must report abstract:true");
    // Two commas between three types.
    int comma_count = 0;
    for (char c : out) if (c == ',') ++comma_count;
    ASSERT_TRUE(comma_count >= 2,
        std::string("expected >= 2 commas (between types); got ")
        + std::to_string(comma_count));
}

// =========================================================================
// Test — unknown types are silently skipped without breaking JSON shape.
// =========================================================================
void unknown_types_skipped_silently() {
    kg::OntologyRegistry r;
    r.addEntityType("Cycle", "", /*abstract=*/false);

    std::string out = kg::serialize_ontology_slice(
        r, {"Cycle", "DefinitelyNotAType", "AlsoUnknown"});
    ASSERT_TRUE(contains(out, "\"Cycle\":"), "known type must appear");
    ASSERT_TRUE(!contains(out, "DefinitelyNotAType"),
        "unknown type must not appear");
    // Should still be valid: starts with { ends with }.
    ASSERT_TRUE(!out.empty() && out.front() == '{' && out.back() == '}',
        "output must be a valid JSON object");
}

// =========================================================================
// Test — empty type list yields the empty JSON object "{}".
// =========================================================================
void empty_type_list_yields_empty_object() {
    kg::OntologyRegistry r;
    r.addEntityType("Cycle", "", false);
    std::string out = kg::serialize_ontology_slice(r, {});
    ASSERT_TRUE(out == "{}", std::string("expected '{}'; got '") + out + "'");
}

// =========================================================================
// Test — defensive escaping: a property name containing a quote
// (won't happen in practice but the schema generator could feed
// odd names) round-trips as escaped JSON.
// =========================================================================
void string_escaping_handles_quotes_and_backslashes() {
    kg::OntologyRegistry r;
    r.addEntityType("Weird", "", false);
    r.addProperty("Weird", "name\"with\\quote",
                  kg::PropertyValueKind::String, false);

    std::string out = kg::serialize_ontology_slice(r, {"Weird"});
    ASSERT_TRUE(contains(out, "name\\\"with\\\\quote"),
        "embedded quote/backslash must escape to \\\" / \\\\");
}

// =========================================================================
// Test — KG snapshot lists every entity of the requested types as
// one JSON object per line, with all properties + the entity id.
// =========================================================================
void snapshot_lists_entities_of_requested_types() {
    kg::OntologyRegistry r;
    r.addEntityType("Wall",  "", false);
    r.addEntityType("Light", "", false);
    r.addEntityType("Other", "", false);

    kg::KGModule kg(r);
    kg.setMode(kg::KGMode::MINIMAL);

    auto w1 = kg.createEntity("Wall");
    kg.setProperty(w1, "x", "3.5");
    kg.setProperty(w1, "y", "12.0");
    auto w2 = kg.createEntity("Wall");
    kg.setProperty(w2, "x", "0");
    auto l1 = kg.createEntity("Light");
    kg.setProperty(l1, "intensity", "8.0");
    kg.createEntity("Other");  // present in KG but excluded by filter

    std::string out = snapshot_via_query(kg, {"Wall", "Light"});

    // Three relevant entities → three lines.
    int line_count = 0;
    for (char c : out) if (c == '\n') ++line_count;
    ASSERT_TRUE(line_count == 3,
        std::string("expected 3 lines (3 in-filter entities); got ")
        + std::to_string(line_count));

    ASSERT_TRUE(contains(out, "\"id\":" + std::to_string(w1)),
        "wall #1 must appear");
    ASSERT_TRUE(contains(out, "\"id\":" + std::to_string(w2)),
        "wall #2 must appear");
    ASSERT_TRUE(contains(out, "\"id\":" + std::to_string(l1)),
        "light must appear");
    ASSERT_TRUE(contains(out, "\"type\":\"Wall\""),
        "type must appear");
    ASSERT_TRUE(contains(out, "\"type\":\"Light\""),
        "type must appear");
    ASSERT_TRUE(!contains(out, "\"type\":\"Other\""),
        "out-of-filter type must not appear");
    ASSERT_TRUE(contains(out, "\"x\":\"3.5\""),
        "string-encoded property must round-trip");
    ASSERT_TRUE(contains(out, "\"intensity\":\"8.0\""),
        "light intensity must appear");
}

// =========================================================================
// Test — empty type filter yields empty output (intentional; the
// caller picks the relevant cohort).
// =========================================================================
void snapshot_empty_filter_yields_empty() {
    kg::OntologyRegistry r;
    r.addEntityType("Wall", "", false);
    kg::KGModule kg(r);
    kg.setMode(kg::KGMode::MINIMAL);
    kg.createEntity("Wall");

    std::string out = snapshot_via_query(kg, {});
    ASSERT_TRUE(out.empty(),
        std::string("empty filter must yield empty output; got '")
        + out + "'");
}

// =========================================================================
// Test — output is deterministic (sorted ids, sorted properties),
// so prompts can be cached / diff'd cheaply.
// =========================================================================
void snapshot_is_deterministic() {
    kg::OntologyRegistry r;
    r.addEntityType("Wall", "", false);
    kg::KGModule kg(r);
    kg.setMode(kg::KGMode::MINIMAL);
    auto w = kg.createEntity("Wall");
    // Set props in non-alphabetic order; output should be sorted.
    kg.setProperty(w, "z", "9");
    kg.setProperty(w, "a", "1");
    kg.setProperty(w, "m", "5");

    std::string out = snapshot_via_query(kg, {"Wall"});
    auto pos_a = out.find("\"a\":");
    auto pos_m = out.find("\"m\":");
    auto pos_z = out.find("\"z\":");
    ASSERT_TRUE(pos_a != std::string::npos &&
                pos_m != std::string::npos &&
                pos_z != std::string::npos,
        "all three property keys must appear");
    ASSERT_TRUE(pos_a < pos_m && pos_m < pos_z,
        "properties must be sorted alphabetically (a, m, z order)");
}

int main() {
    std::cout << "=== test_ontology_serialize ===" << std::endl;
    TEST(emits_basic_type_with_properties);
    TEST(emits_multiple_types_and_abstract_flag);
    TEST(unknown_types_skipped_silently);
    TEST(empty_type_list_yields_empty_object);
    TEST(string_escaping_handles_quotes_and_backslashes);
    TEST(snapshot_lists_entities_of_requested_types);
    TEST(snapshot_empty_filter_yields_empty);
    TEST(snapshot_is_deterministic);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
