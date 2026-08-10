// Option 3: a lookup row is the typed result.
//
// The engine owns only the abstract LookupEntry selection shape. Cepheus
// declares the columns printed by each of its tables. Rollable rows keep a
// required typed Outcome root, with NoEffect and OutcomeSequence making an
// intentionally empty result and several ordered consequences explicit.

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

kg::OntologyRegistry game_registry() {
    auto reg = logosphere::ontology::registry();
    reg.extend(rulebook::ontology::registry());
    reg.extend(cepheus_book1_character_creation::ontology::registry());
    reg.extend(cepheus_book1_skills::ontology::registry());
    return reg;
}

std::string slurp(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string game_path(const std::string& relative) {
    return std::string(LOGOSPHERE_SOURCE_DIR) + "/examples/logovger/" +
           relative;
}

kg::SeedEnvelope parse_table_seed() {
    const auto parsed = kg::parse_seed_envelope(
        slurp(game_path("seeds/cepheus_book1_tables.json")));
    CHECK(parsed.ok(), "the table seed parses for a semantic mutation: " +
                           parsed.error);
    return parsed.seed;
}

kg::KGOpCreateEntity* find_create(kg::SeedEnvelope& seed,
                                  const std::string& alias) {
    for (auto& op : seed.ops) {
        auto* create = std::get_if<kg::KGOpCreateEntity>(&op);
        if (create && create->as == alias) return create;
    }
    return nullptr;
}

bool set_property(kg::SeedEnvelope& seed, const std::string& alias,
                  const std::string& property, const std::string& value) {
    auto* create = find_create(seed, alias);
    if (!create) return false;
    for (auto& [key, stored] : create->properties) {
        if (key == property) {
            stored = value;
            return true;
        }
    }
    return false;
}

bool remove_relations_from(kg::SeedEnvelope& seed,
                           const std::string& from_alias) {
    const size_t before = seed.ops.size();
    seed.ops.erase(
        std::remove_if(seed.ops.begin(), seed.ops.end(),
            [&](const kg::KGOp& op) {
                const auto* relation = std::get_if<kg::KGOpSetRelation>(&op);
                return relation && relation->from.symbolic == from_alias;
            }),
        seed.ops.end());
    return seed.ops.size() != before;
}

bool retarget_relation(kg::SeedEnvelope& seed,
                       const std::string& from_alias,
                       const std::string& old_target,
                       const std::string& new_target) {
    for (auto& op : seed.ops) {
        auto* relation = std::get_if<kg::KGOpSetRelation>(&op);
        if (relation && relation->from.symbolic == from_alias &&
            relation->to.symbolic == old_target) {
            relation->to.symbolic = new_target;
            return true;
        }
    }
    return false;
}

void append_create(
    kg::SeedEnvelope& seed, const std::string& type,
    const std::string& alias,
    std::vector<std::pair<std::string, std::string>> properties) {
    kg::KGOpCreateEntity create;
    create.type = type;
    create.as = alias;
    create.properties = std::move(properties);
    seed.ops.emplace_back(std::move(create));
}

void append_relation(kg::SeedEnvelope& seed, const std::string& from,
                     const std::string& to) {
    kg::KGOpSetRelation relation;
    relation.from.symbolic = from;
    relation.relation = "HAS_PART";
    relation.to.symbolic = to;
    seed.ops.emplace_back(std::move(relation));
}

kg::SeedEnvelope seed_with_complete_choice() {
    auto seed = parse_table_seed();
    const std::string quote =
        "| 3 | Missing eye or limb. Reduce Strength or Dexterity by 2. |";
    append_create(seed, "OutcomeChoice", "injury_choice",
                  {{"name", "choose_injured_attribute"},
                   {"choice_authority", "player"},
                   {"source_section", "Injuries"},
                   {"source_quote", quote}});
    append_create(seed, "OutcomeOption", "injury_choice_strength",
                  {{"name", "reduce_strength"},
                   {"option_index", "0"},
                   {"option_label", "Strength"},
                   {"outcome", "@no_permanent_effect"},
                   {"source_section", "Injuries"},
                   {"source_quote", quote}});
    append_create(seed, "OutcomeOption", "injury_choice_dexterity",
                  {{"name", "reduce_dexterity"},
                   {"option_index", "1"},
                   {"option_label", "Dexterity"},
                   {"outcome", "@no_permanent_effect"},
                   {"source_section", "Injuries"},
                   {"source_quote", quote}});
    append_relation(seed, "injury_choice", "injury_choice_strength");
    append_relation(seed, "injury_choice", "injury_choice_dexterity");
    return seed;
}

bool semantic_reason_contains(const kg::SeedVerifyReport& report,
                              const std::string& text) {
    for (const auto& violation : report.violations) {
        if (violation.check == "semantic" &&
            violation.reason.find(text) != std::string::npos) {
            std::cout << "  [measure] semantic: " << violation.reason
                      << std::endl;
            return true;
        }
    }
    return false;
}

bool is_required(const kg::OntologyRegistry& reg, const std::string& type,
                 const std::string& property,
                 const std::string& value_type) {
    const auto* def = reg.findProperty(type, property);
    return def && def->required && def->value_type == value_type;
}

void test_cepheus_declares_each_lookup_result_shape() {
    const auto reg = game_registry();

    CHECK(reg.isAbstract("LookupEntry"),
          "the engine LookupEntry remains abstract");
    CHECK(reg.hasEntityType("CharacteristicModifierEntry") &&
              reg.isSubtypeOf("CharacteristicModifierEntry", "LookupEntry") &&
              !reg.isAbstract("CharacteristicModifierEntry"),
          "Cepheus declares a concrete characteristic-modifier row");
    CHECK(is_required(reg, "CharacteristicModifierEntry", "pseudohex_min",
                      "string") &&
              is_required(reg, "CharacteristicModifierEntry", "pseudohex_max",
                          "string") &&
              is_required(reg, "CharacteristicModifierEntry",
                          "characteristic_modifier", "integer"),
          "the characteristic row requires all three printed result fields");

    CHECK(reg.hasEntityType("DifficultyEntry") &&
              reg.isSubtypeOf("DifficultyEntry", "LookupEntry") &&
              !reg.isAbstract("DifficultyEntry"),
          "Cepheus declares a concrete law-difficulty row");
    CHECK(is_required(reg, "DifficultyEntry", "difficulty_name", "string") &&
              is_required(reg, "DifficultyEntry", "difficulty_modifier",
                          "integer"),
          "the difficulty row requires both printed result fields");

    CHECK(reg.hasEntityType("EndCareer") &&
              reg.isSubtypeOf("EndCareer", "Outcome"),
          "ending a Cepheus career is a game outcome, not engine policy");
}

void test_cited_tables_load_with_typed_results() {
    const std::string json =
        slurp(game_path("seeds/cepheus_book1_tables.json"));
    CHECK(!json.empty(), "the production table seed is readable");
    if (json.empty()) return;

    const auto parsed = kg::parse_seed_envelope(json);
    CHECK(parsed.ok(), "the production table seed parses: " + parsed.error);
    if (!parsed.ok()) return;

    const auto reg = game_registry();
    const auto verified = kg::verify_seed(
        parsed.seed, game_path("srd/cepheus"), reg);
    if (!verified.ok()) {
        for (const auto& violation : verified.violations)
            std::cout << "  [measure] " << violation.check << ": "
                      << violation.reason << std::endl;
    }
    CHECK(verified.ok(), "the typed table seed passes source, schema, value, "
                         "and semantic verification");
    if (!verified.ok()) return;

    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport loaded;
    CHECK(kg::load_seed(parsed.seed, world, loaded),
          "the verified table seed loads: " + loaded.error);
    if (!loaded.error.empty()) return;

    const auto dm_table = loaded.bindings.at("dm_table");
    const auto dm_rows = world.getRelated(dm_table, "HAS_PART");
    CHECK(world.getProperty(dm_table, "entry_type") ==
              "CharacteristicModifierEntry" &&
              dm_rows.size() == 2,
          "the characteristic table declares and contains its typed rows");
    bool complete_dm_rows = true;
    for (const auto row : dm_rows) {
        complete_dm_rows = complete_dm_rows &&
            world.getType(row) == "CharacteristicModifierEntry" &&
            !world.getProperty(row, "pseudohex_min").empty() &&
            !world.getProperty(row, "pseudohex_max").empty() &&
            !world.getProperty(row, "characteristic_modifier").empty();
    }
    CHECK(complete_dm_rows,
          "every characteristic row carries every printed result column");

    const auto law_table = loaded.bindings.at("law_table");
    const auto law_rows = world.getRelated(law_table, "HAS_PART");
    bool complete_law_rows = law_rows.size() == 2;
    for (const auto row : law_rows) {
        complete_law_rows = complete_law_rows &&
            world.getType(row) == "DifficultyEntry" &&
            !world.getProperty(row, "difficulty_name").empty() &&
            !world.getProperty(row, "difficulty_modifier").empty();
    }
    CHECK(world.getProperty(law_table, "entry_type") == "DifficultyEntry" &&
              complete_law_rows,
          "the law table returns a typed difficulty name and modifier");

    const auto mishap_three = loaded.bindings.at("mishap_row_3");
    const auto sequence = static_cast<kg::EntityID>(
        std::stoul(world.getProperty(mishap_three, "outcome")));
    const auto steps = world.getRelated(sequence, "HAS_PART");
    CHECK(world.getType(sequence) == "OutcomeSequence" && steps.size() == 2,
          "mishap 3 has one sequence root containing both consequences");
    CHECK(world.getProperty(steps[0], "step_index") !=
              world.getProperty(steps[1], "step_index"),
          "the composite consequences carry explicit distinct order");

    const auto injury_six = loaded.bindings.at("injury_row_6");
    const auto no_effect = static_cast<kg::EntityID>(
        std::stoul(world.getProperty(injury_six, "outcome")));
    CHECK(world.getType(no_effect) == "NoEffect",
          "the book's no-effect row is explicit, not missing data");
}

void test_lookup_table_rejects_an_unknown_entry_type() {
    auto seed = parse_table_seed();
    CHECK(set_property(seed, "dm_table", "entry_type", "MissingEntry"),
          "the lookup type mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry());
    CHECK(!report.ok(), "an unknown lookup entry type fails verification");
    CHECK(semantic_reason_contains(report, "unknown entry_type"),
          "the semantic error names the unresolved ontology type");
}

void test_lookup_table_rejects_rows_of_another_declared_shape() {
    auto seed = parse_table_seed();
    CHECK(set_property(seed, "dm_table", "entry_type", "DifficultyEntry"),
          "the incompatible lookup type mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry());
    CHECK(!report.ok(), "rows that contradict their table type fail");
    CHECK(semantic_reason_contains(report,
                                   "CharacteristicModifierEntry") &&
              semantic_reason_contains(report, "DifficultyEntry"),
          "the semantic error names both actual and declared row types");
}

void test_lookup_table_requires_a_concrete_entry_subtype_and_rows() {
    auto abstract = parse_table_seed();
    CHECK(set_property(abstract, "dm_table", "entry_type", "LookupEntry"),
          "the abstract lookup type mutation was applied");
    const auto abstract_report = kg::verify_seed(
        abstract, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(abstract_report, "is abstract"),
          "a table cannot declare the abstract base as its result shape");

    auto unrelated = parse_table_seed();
    CHECK(set_property(unrelated, "dm_table", "entry_type", "EndCareer"),
          "the unrelated lookup type mutation was applied");
    const auto unrelated_report = kg::verify_seed(
        unrelated, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(unrelated_report,
                                   "is not a LookupEntry subtype"),
          "a table cannot declare an unrelated ontology type");

    auto empty = parse_table_seed();
    CHECK(remove_relations_from(empty, "dm_table"),
          "both characteristic row relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(empty_report, "has no HAS_PART rows"),
          "a lookup table without rows fails semantic verification");
}

void test_outcome_sequence_rejects_duplicate_order() {
    auto seed = parse_table_seed();
    CHECK(set_property(seed, "mishap_3_step_1", "step_index", "0"),
          "the duplicate step mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry());
    CHECK(!report.ok(), "duplicate outcome step order fails verification");
    CHECK(semantic_reason_contains(report, "duplicate step_index 0"),
          "the semantic error names the duplicate sequence index");
}

void test_outcome_sequence_requires_steps_and_contiguous_order() {
    auto empty = parse_table_seed();
    CHECK(remove_relations_from(empty, "mishap_3_sequence"),
          "both sequence-step relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(empty_report,
                                   "has no OutcomeStep parts"),
          "an empty composite outcome fails semantic verification");

    auto wrong_part = parse_table_seed();
    CHECK(retarget_relation(wrong_part, "mishap_3_sequence",
                            "mishap_3_step_1", "end_career"),
          "a sequence relation was retargeted to a non-step outcome");
    const auto wrong_part_report = kg::verify_seed(
        wrong_part, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(wrong_part_report,
                                   "non-OutcomeStep part type 'EndCareer'"),
          "a sequence rejects parts that are not OutcomeStep entities");

    auto gap = parse_table_seed();
    CHECK(set_property(gap, "mishap_3_step_1", "step_index", "2"),
          "the sequence gap mutation was applied");
    const auto gap_report = kg::verify_seed(
        gap, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(gap_report, "are not contiguous"),
          "a sequence rejects gaps in its explicit order");
}

void test_outcome_choice_requires_authority_options_and_order() {
    auto complete = seed_with_complete_choice();
    const auto complete_report = kg::verify_seed(
        complete, game_path("srd/cepheus"), game_registry());
    if (!complete_report.ok()) {
        for (const auto& violation : complete_report.violations) {
            std::cout << "  [measure] " << violation.check << ": "
                      << violation.reason << std::endl;
        }
    }
    CHECK(complete_report.ok(),
          "a complete ordered player choice passes semantic verification");

    auto authority = seed_with_complete_choice();
    CHECK(set_property(authority, "injury_choice", "choice_authority",
                       "nobody"),
          "the invalid choice authority mutation was applied");
    const auto authority_report = kg::verify_seed(
        authority, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(authority_report,
                                   "unknown choice_authority 'nobody'"),
          "a choice rejects authority outside player, referee, procedure");

    auto empty = seed_with_complete_choice();
    CHECK(remove_relations_from(empty, "injury_choice"),
          "both choice-option relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(empty_report,
                                   "has no OutcomeOption parts"),
          "a choice without alternatives fails semantic verification");

    auto wrong_part = seed_with_complete_choice();
    CHECK(retarget_relation(wrong_part, "injury_choice",
                            "injury_choice_strength", "end_career"),
          "a choice relation was retargeted to a non-option outcome");
    const auto wrong_part_report = kg::verify_seed(
        wrong_part, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(
              wrong_part_report,
              "non-OutcomeOption part type 'EndCareer'"),
          "a choice rejects parts that are not OutcomeOption entities");

    auto duplicate = seed_with_complete_choice();
    CHECK(set_property(duplicate, "injury_choice_dexterity",
                       "option_index", "0"),
          "the duplicate option index mutation was applied");
    const auto duplicate_report = kg::verify_seed(
        duplicate, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(duplicate_report,
                                   "duplicate option_index 0"),
          "a choice rejects duplicate option order");

    auto gap = seed_with_complete_choice();
    CHECK(set_property(gap, "injury_choice_dexterity", "option_index", "2"),
          "the option gap mutation was applied");
    const auto gap_report = kg::verify_seed(
        gap, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(gap_report, "are not contiguous"),
          "a choice rejects gaps in option order");

    auto empty_label = seed_with_complete_choice();
    CHECK(set_property(empty_label, "injury_choice_dexterity",
                       "option_label", ""),
          "the empty option label mutation was applied");
    const auto empty_label_report = kg::verify_seed(
        empty_label, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(empty_label_report,
                                   "has empty option_label"),
          "a choice rejects an unlabeled alternative");
}

void test_rollable_table_rejects_non_rows() {
    auto seed = parse_table_seed();
    CHECK(retarget_relation(seed, "mishap_table", "mishap_row_2",
                            "end_career"),
          "a rollable-table relation was retargeted to an Outcome");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry());
    CHECK(semantic_reason_contains(report,
                                   "non-TableEntry row type 'EndCareer'"),
          "a rollable table rejects attached entities that are not rows");
}

}  // namespace

int main() {
    std::cout << "Logovger typed table results" << std::endl;
    test_cepheus_declares_each_lookup_result_shape();
    test_cited_tables_load_with_typed_results();
    test_lookup_table_rejects_an_unknown_entry_type();
    test_lookup_table_rejects_rows_of_another_declared_shape();
    test_lookup_table_requires_a_concrete_entry_subtype_and_rows();
    test_outcome_sequence_rejects_duplicate_order();
    test_outcome_sequence_requires_steps_and_contiguous_order();
    test_outcome_choice_requires_authority_options_and_order();
    test_rollable_table_rejects_non_rows();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
