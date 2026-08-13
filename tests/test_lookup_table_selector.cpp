// Generic LookupTable selection. A lookup returns its typed row and never
// interprets the game-declared result columns.

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/lookup_table_selector.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

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

static_assert(std::is_copy_assignable_v<rules::LookupTableSelection>);
static_assert(!std::is_constructible_v<
              rules::LookupTableSelection, kg::EntityID, kg::EntityID,
              int64_t>);

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    kg::OntologyRegistry rows("test://lookup-table-selector");
    rows.addEntityType("TestLookupEntry", "LookupEntry", false);
    rows.addAncestors(
        "TestLookupEntry",
        {"LookupEntry", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    rows.addProperty("TestLookupEntry", "result_value",
                     kg::PropertyValueKind::Integer, true);
    rows.addEntityType("OtherLookupEntry", "LookupEntry", false);
    rows.addAncestors(
        "OtherLookupEntry",
        {"LookupEntry", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    out.extend(rows);
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    kg::EntityID table = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        table = world.createEntity("LookupTable");
        world.setProperty(table, "entry_type", "TestLookupEntry");
    }

    kg::EntityID closed(int64_t low, int64_t high, int result) {
        const auto row = world.createEntity("TestLookupEntry");
        world.setProperty(row, "key_min", std::to_string(low));
        world.setProperty(row, "key_max", std::to_string(high));
        world.setProperty(row, "result_value", std::to_string(result));
        world.createRelation(table, "HAS_PART", row);
        return row;
    }

    kg::EntityID upper_unbounded(int64_t low, int result) {
        const auto row = world.createEntity("TestLookupEntry");
        world.setProperty(row, "key_min", std::to_string(low));
        world.setProperty(row, "key_max_unbounded", "true");
        world.setProperty(row, "result_value", std::to_string(result));
        world.createRelation(table, "HAS_PART", row);
        return row;
    }

    kg::EntityID lower_unbounded(int64_t high, int result) {
        const auto row = world.createEntity("TestLookupEntry");
        world.setProperty(row, "key_min_unbounded", "true");
        world.setProperty(row, "key_max", std::to_string(high));
        world.setProperty(row, "result_value", std::to_string(result));
        world.createRelation(table, "HAS_PART", row);
        return row;
    }
};

void selection_returns_the_exact_typed_row() {
    Fixture f;
    f.closed(0, 2, -2);
    const auto expected = f.closed(3, 5, -1);
    rules::LookupTableSelector selector(f.world);

    const auto result = selector.select(f.table, 4);
    REQUIRE(result.ok() && result.selection.has_value(),
            "a covered key must select one row: " + result.error);
    REQUIRE(result.selection->table() == f.table &&
                result.selection->row() == expected &&
                result.selection->key() == 4 &&
                f.world.getType(result.selection->row()) ==
                    "TestLookupEntry" &&
                f.world.getProperty(result.selection->row(),
                                    "result_value") == "-1",
            "selection must return the game-declared typed row unchanged");
}

void an_explicit_upper_unbounded_row_covers_every_larger_key() {
    Fixture f;
    f.closed(0, 32, 8);
    const auto expected = f.upper_unbounded(33, 9);
    rules::LookupTableSelector selector(f.world);

    const auto at_boundary = selector.select(f.table, 33);
    const auto at_limit = selector.select(
        f.table, std::numeric_limits<int64_t>::max());
    REQUIRE(at_boundary.ok() && at_limit.ok() &&
                at_boundary.selection->row() == expected &&
                at_limit.selection->row() == expected,
            "an explicit upper-unbounded row must include 33 and all above");
}

void an_explicit_lower_unbounded_row_covers_every_smaller_key() {
    Fixture f;
    const auto expected = f.lower_unbounded(-1, -9);
    f.closed(0, 5, 1);
    rules::LookupTableSelector selector(f.world);

    const auto at_boundary = selector.select(f.table, -1);
    const auto at_limit = selector.select(
        f.table, std::numeric_limits<int64_t>::min());
    REQUIRE(at_boundary.ok() && at_limit.ok() &&
                at_boundary.selection->row() == expected &&
                at_limit.selection->row() == expected,
            "an explicit lower-unbounded row must include -1 and all below");
}

void an_omitted_or_ambiguous_bound_fails_loudly() {
    Fixture missing;
    const auto missing_row = missing.world.createEntity("TestLookupEntry");
    missing.world.setProperty(missing_row, "key_min", "0");
    missing.world.setProperty(missing_row, "result_value", "1");
    missing.world.createRelation(missing.table, "HAS_PART", missing_row);
    rules::LookupTableSelector missing_selector(missing.world);
    const auto no_max = missing_selector.select(missing.table, 0);
    REQUIRE(!no_max.ok() && no_max.error.find("key_max") !=
                std::string::npos,
            "a missing maximum without an explicit flag must fail");

    Fixture ambiguous;
    const auto ambiguous_row = ambiguous.closed(0, 5, 1);
    ambiguous.world.setProperty(ambiguous_row, "key_max_unbounded", "true");
    rules::LookupTableSelector ambiguous_selector(ambiguous.world);
    const auto both = ambiguous_selector.select(ambiguous.table, 0);
    REQUIRE(!both.ok() && both.error.find("both key_max") !=
                std::string::npos,
            "a numeric maximum plus unbounded flag must fail as ambiguous");
}

void the_complete_table_is_validated_before_returning_a_match() {
    Fixture wrong_type;
    wrong_type.closed(0, 5, 1);
    const auto other = wrong_type.world.createEntity("OtherLookupEntry");
    wrong_type.world.setProperty(other, "key_min", "6");
    wrong_type.world.setProperty(other, "key_max", "10");
    wrong_type.world.createRelation(wrong_type.table, "HAS_PART", other);
    rules::LookupTableSelector wrong_selector(wrong_type.world);
    const auto wrong = wrong_selector.select(wrong_type.table, 1);
    REQUIRE(!wrong.ok() && wrong.error.find("OtherLookupEntry") !=
                std::string::npos,
            "an unselected row of the wrong declared type must fail preflight");

    Fixture overlap;
    overlap.closed(0, 5, 1);
    overlap.closed(5, 10, 2);
    rules::LookupTableSelector overlap_selector(overlap.world);
    const auto ambiguous = overlap_selector.select(overlap.table, 1);
    REQUIRE(!ambiguous.ok() && ambiguous.error.find("overlap") !=
                std::string::npos,
            "overlapping lookup bands must fail before returning a row");
}

void uncovered_keys_and_invalid_table_contracts_fail_loudly() {
    Fixture uncovered;
    uncovered.closed(0, 5, 1);
    rules::LookupTableSelector selector(uncovered.world);
    const auto no_row = selector.select(uncovered.table, 6);
    REQUIRE(!no_row.ok() && no_row.error.find("no row") != std::string::npos,
            "a key outside the declared bands must not receive a default");

    Fixture unknown;
    unknown.world.setProperty(unknown.table, "entry_type", "MissingEntry");
    unknown.closed(0, 5, 1);
    rules::LookupTableSelector unknown_selector(unknown.world);
    const auto bad_type = unknown_selector.select(unknown.table, 1);
    REQUIRE(!bad_type.ok() && bad_type.error.find("unknown entry_type") !=
                std::string::npos,
            "an unresolved result-row type must fail loudly");
}

// Books state a bonus for some keys and say nothing about the rest,
// and the silence is the rule: Cepheus gives extra benefits at ranks
// O4, O5 and O6 of seven and says nothing of the other four. A table
// may declare that a miss means nothing, and then it is an answer.
void a_table_may_say_that_silence_is_an_answer() {
    Fixture speaks;
    speaks.closed(4, 6, 1);
    speaks.world.setProperty(speaks.table, "miss_is_nothing", "true");
    rules::LookupTableSelector selector(speaks.world);

    const auto hit = selector.select(speaks.table, 5);
    REQUIRE(hit.ok() && !hit.missed,
            "a key the table covers still selects its row: " + hit.error);

    const auto quiet = selector.select(speaks.table, 0);
    REQUIRE(!quiet.ok() && quiet.missed && quiet.error.empty(),
            "a key it does not cover is a miss, not a failure, and carries "
            "no error to be mistaken for one: " + quiet.error);
    REQUIRE(!quiet.selection.has_value(),
            "and a miss returns no row, so nothing can read a value off it");

    // Only when the table says so. A miss is otherwise still a hole,
    // because most tables must cover every key, and a quiet zero there
    // is the defect this check exists to catch.
    Fixture silent;
    silent.closed(4, 6, 1);
    rules::LookupTableSelector strict(silent.world);
    const auto broken = strict.select(silent.table, 0);
    REQUIRE(!broken.ok() && !broken.missed &&
                broken.error.find("no row") != std::string::npos,
            "a table that has NOT declared it still fails on a miss: " +
                broken.error);
}

}  // namespace

int main() {
    std::cout << "Lookup table selector\n";
    TEST(selection_returns_the_exact_typed_row);
    TEST(an_explicit_upper_unbounded_row_covers_every_larger_key);
    TEST(an_explicit_lower_unbounded_row_covers_every_smaller_key);
    TEST(an_omitted_or_ambiguous_bound_fails_loudly);
    TEST(the_complete_table_is_validated_before_returning_a_match);
    TEST(uncovered_keys_and_invalid_table_contracts_fail_loudly);
    TEST(a_table_may_say_that_silence_is_an_answer);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
