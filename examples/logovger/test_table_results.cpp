// Option 3: a lookup row is the typed result.
//
// The engine owns only the abstract LookupEntry selection shape. Cepheus
// declares the columns printed by each of its tables. Rollable rows keep a
// required typed Outcome root, with NoEffect and OutcomeSequence making an
// intentionally empty result and several ordered consequences explicit.

#undef NDEBUG

#include "chargen/rule_seeds.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "logosphere/rules/lookup_table_selector.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
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

// The mishap and injury tables moved. They were three demonstration
// rows in the sampler above, and the completed absorption took
// ownership of them, which is invariant 8: a partial thing is
// finished by deciding who owns it, not by editing the owner's file.
// The sequence fixtures below follow them.
kg::SeedEnvelope parse_shared_seed() {
    const auto parsed = kg::parse_seed_envelope(
        slurp(game_path("seeds/cepheus_book1_shared_tables.json")));
    CHECK(parsed.ok(), "the shared-table seed parses for a semantic "
                       "mutation: " + parsed.error);
    return parsed.seed;
}

// Everything the careers seed depends on, taken FROM THE MANIFEST
// rather than listed here. It references the Skills the vocabulary
// owns and the dice the tables seed owns, and that list has grown
// twice already; a copy of it in this file would drift the same way
// test_chargen's copy of the seed list drifted.
const std::vector<const kg::SeedEnvelope*>& prerequisites_before(
    const std::string& subject) {
    static std::map<std::string, std::vector<kg::SeedEnvelope>> owned;
    static std::map<std::string, std::vector<const kg::SeedEnvelope*>> refs;
    auto found = refs.find(subject);
    if (found != refs.end()) return found->second;

    auto& seeds = owned[subject];
    {
        for (const char* seed : logovger::kRuleSeeds) {
            if (std::string(seed).find(subject) != std::string::npos) {
                break;              // everything BEFORE the one under test
            }
            auto parsed = kg::parse_seed_envelope(slurp(game_path(seed)));
            if (!parsed.ok()) {
                std::cout << "  [setup] " << seed << " does not parse: "
                          << parsed.error << std::endl;
                continue;
            }
            seeds.push_back(std::move(parsed.seed));
        }
    }
    auto& out = refs[subject];
    for (const auto& seed : seeds) out.push_back(&seed);
    return out;
}

kg::SeedEnvelope parse_career_seed() {
    const auto parsed = kg::parse_seed_envelope(
        slurp(game_path("seeds/cepheus_careers.json")));
    CHECK(parsed.ok(), "the careers seed parses for a semantic mutation: " +
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

bool add_property(kg::SeedEnvelope& seed, const std::string& alias,
                  const std::string& property, const std::string& value) {
    auto* create = find_create(seed, alias);
    if (!create) return false;
    for (const auto& [key, stored] : create->properties) {
        (void)stored;
        if (key == property) return false;
    }
    create->properties.emplace_back(property, value);
    return true;
}

bool remove_property(kg::SeedEnvelope& seed, const std::string& alias,
                     const std::string& property) {
    auto* create = find_create(seed, alias);
    if (!create) return false;
    const auto before = create->properties.size();
    create->properties.erase(
        std::remove_if(create->properties.begin(), create->properties.end(),
                       [&](const auto& item) {
                           return item.first == property;
                       }),
        create->properties.end());
    return create->properties.size() != before;
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
    auto seed = parse_shared_seed();
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
                   {"outcome", "@injury_6_none"},
                   {"source_section", "Injuries"},
                   {"source_quote", quote}});
    append_create(seed, "OutcomeOption", "injury_choice_dexterity",
                  {{"name", "reduce_dexterity"},
                   {"option_index", "1"},
                   {"option_label", "Dexterity"},
                   {"outcome", "@injury_6_none"},
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
                 kg::PropertyValueKind value_kind) {
    const auto* def = reg.findProperty(type, property);
    return def && def->required && def->value_kind == value_kind;
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
                      kg::PropertyValueKind::String) &&
              is_required(reg, "CharacteristicModifierEntry", "pseudohex_max",
                          kg::PropertyValueKind::String) &&
              is_required(reg, "CharacteristicModifierEntry",
                          "characteristic_modifier",
                          kg::PropertyValueKind::Integer),
          "the characteristic row requires all three printed result fields");

    CHECK(reg.hasEntityType("DifficultyEntry") &&
              reg.isSubtypeOf("DifficultyEntry", "LookupEntry") &&
              !reg.isAbstract("DifficultyEntry"),
          "Cepheus declares a concrete law-difficulty row");
    CHECK(is_required(reg, "DifficultyEntry", "difficulty_name",
                      kg::PropertyValueKind::String) &&
              is_required(reg, "DifficultyEntry", "difficulty_modifier",
                          kg::PropertyValueKind::Integer),
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

    const auto careers = kg::parse_seed_envelope(
        slurp(game_path("seeds/cepheus_careers.json")));
    CHECK(careers.ok(), "the careers seed containing the modifier table "
                        "parses: " + careers.error);
    if (!careers.ok()) return;
    const auto careers_verified = kg::verify_seed(
        careers.seed, game_path("srd/cepheus"), reg, nullptr,
        prerequisites_before("cepheus_careers.json"));
    if (!careers_verified.ok()) {
        for (const auto& violation : careers_verified.violations)
            std::cout << "  [measure] careers " << violation.check << ": "
                      << violation.reason << std::endl;
    }
    CHECK(careers_verified.ok(),
          "the careers seed and colocated modifier table verify");
    if (!careers_verified.ok()) return;
    // This test already loaded the tables seed above as its own
    // subject, so load only the prerequisites the world does not
    // have. Loading one twice trips the very guard that exists to
    // stop two seeds owning the same name.
    // The tables seed is already in this world: it was loaded above as
    // this test's own subject. Loading it again would trip the guard
    // that stops two seeds owning one name, which is the guard working,
    // so a refusal here is expected rather than a failure.
    for (const auto* prerequisite : prerequisites_before("cepheus_careers.json")) {
        kg::SeedLoadReport before;
        kg::load_seed(*prerequisite, world, before);
    }
    kg::SeedLoadReport careers_loaded;
    CHECK(kg::load_seed(careers.seed, world, careers_loaded),
          "the verified careers seed loads: " + careers_loaded.error);
    if (!careers_loaded.error.empty()) return;

    const auto dm_table = careers_loaded.bindings.at("dm_table");
    const auto dm_rows = world.getRelated(dm_table, "HAS_PART");
    CHECK(world.getProperty(dm_table, "entry_type") ==
              "CharacteristicModifierEntry" &&
              dm_rows.size() == 12,
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
    const logosphere::rules::LookupTableSelector selector(world);
    bool every_score_has_the_book_modifier = true;
    for (int score = 0; score <= 35; ++score) {
        const auto selected = selector.select(dm_table, score);
        const int expected = score >= 33 ? 9 : score / 3 - 2;
        every_score_has_the_book_modifier =
            every_score_has_the_book_modifier && selected.ok() &&
            std::stoi(world.getProperty(selected.selection->row(),
                                        "characteristic_modifier")) ==
                expected;
    }
    const auto at_integer_limit = selector.select(
        dm_table, std::numeric_limits<int64_t>::max());
    CHECK(every_score_has_the_book_modifier && at_integer_limit.ok() &&
              world.getProperty(at_integer_limit.selection->row(),
                                "characteristic_modifier") == "9" &&
              world.getProperty(at_integer_limit.selection->row(),
                                "key_max").empty() &&
              world.getProperty(at_integer_limit.selection->row(),
                                "key_max_unbounded") == "true",
          "all twelve cited bands select correctly and 33+ is explicitly "
          "unbounded");

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

    // The mishap and injury rows live in the seed that owns those
    // tables now, and this world already has it: the prerequisites
    // above load everything the manifest lists before careers. So
    // find the rows rather than binding them a second time, which
    // would trip the one-owner guard.
    const auto row_named = [&world](const std::string& name) {
        for (const auto id : world.findByType("TableEntry")) {
            if (world.getProperty(id, "name") == name) return id;
        }
        return kg::INVALID_ENTITY;
    };
    const auto mishap_three = row_named("mishap 3");
    CHECK(mishap_three != kg::INVALID_ENTITY,
          "the completed mishap table is in this world");
    if (mishap_three == kg::INVALID_ENTITY) return;
    const auto sequence = static_cast<kg::EntityID>(
        std::stoul(world.getProperty(mishap_three, "outcome")));
    const auto steps = world.getRelated(sequence, "HAS_PART");
    // Three now, not two. Leaving the service costs two years, and
    // that is an outcome every mishap row carries rather than an
    // addition the procedure makes: "Honorably discharged... Legal
    // issues create a debt of Cr10,000", plus the half-term years the
    // mishap rule states for all of them.
    CHECK(world.getType(sequence) == "OutcomeSequence" && steps.size() == 3,
          "mishap 3 has one sequence root holding all its consequences: " +
              std::to_string(steps.size()));
    CHECK(world.getProperty(steps[0], "step_index") !=
              world.getProperty(steps[1], "step_index"),
          "the composite consequences carry explicit distinct order");

    const auto injury_six = row_named("injury 6");
    CHECK(injury_six != kg::INVALID_ENTITY,
          "and so is the completed injury table");
    if (injury_six == kg::INVALID_ENTITY) return;
    const auto no_effect = static_cast<kg::EntityID>(
        std::stoul(world.getProperty(injury_six, "outcome")));
    CHECK(world.getType(no_effect) == "NoEffect",
          "the book's no-effect row is explicit, not missing data");
}

void test_lookup_table_rejects_an_unknown_entry_type() {
    auto seed = parse_career_seed();
    CHECK(set_property(seed, "dm_table", "entry_type", "MissingEntry"),
          "the lookup type mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(!report.ok(), "an unknown lookup entry type fails verification");
    CHECK(semantic_reason_contains(report, "unknown entry_type"),
          "the semantic error names the unresolved ontology type");
}

void test_lookup_table_rejects_rows_of_another_declared_shape() {
    auto seed = parse_career_seed();
    CHECK(set_property(seed, "dm_table", "entry_type", "DifficultyEntry"),
          "the incompatible lookup type mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(!report.ok(), "rows that contradict their table type fail");
    CHECK(semantic_reason_contains(report,
                                   "CharacteristicModifierEntry") &&
              semantic_reason_contains(report, "DifficultyEntry"),
          "the semantic error names both actual and declared row types");
}

void test_lookup_table_requires_a_concrete_entry_subtype_and_rows() {
    auto abstract = parse_career_seed();
    CHECK(set_property(abstract, "dm_table", "entry_type", "LookupEntry"),
          "the abstract lookup type mutation was applied");
    const auto abstract_report = kg::verify_seed(
        abstract, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(semantic_reason_contains(abstract_report, "is abstract"),
          "a table cannot declare the abstract base as its result shape");

    auto unrelated = parse_career_seed();
    CHECK(set_property(unrelated, "dm_table", "entry_type", "EndCareer"),
          "the unrelated lookup type mutation was applied");
    const auto unrelated_report = kg::verify_seed(
        unrelated, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(semantic_reason_contains(unrelated_report,
                                   "is not a LookupEntry subtype"),
          "a table cannot declare an unrelated ontology type");

    auto empty = parse_career_seed();
    CHECK(remove_relations_from(empty, "dm_table"),
          "both characteristic row relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(semantic_reason_contains(empty_report, "has no HAS_PART rows"),
          "a lookup table without rows fails semantic verification");
}

void test_lookup_rows_require_one_explicit_bound_mode_per_side() {
    auto missing = parse_career_seed();
    CHECK(remove_property(missing, "dm_row_0_2", "key_max"),
          "the finite upper lookup bound was removed");
    const auto missing_report = kg::verify_seed(
        missing, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(semantic_reason_contains(missing_report, "key_max"),
          "an omitted lookup maximum without an unbounded flag fails");

    auto ambiguous = parse_career_seed();
    CHECK(add_property(ambiguous, "dm_row_0_2", "key_max_unbounded",
                       "true"),
          "the contradictory upper-unbounded flag was added");
    const auto ambiguous_report = kg::verify_seed(
        ambiguous, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(semantic_reason_contains(ambiguous_report, "both key_max"),
          "a finite and unbounded maximum cannot coexist");
}

void test_task_check_requires_an_integer_modifier_result() {
    auto wrong_type = parse_career_seed();
    CHECK(set_property(wrong_type, "aerospace_defense_qual",
                       "modifier_property", "pseudohex_min"),
          "the non-integer TaskCheck modifier column mutation was applied");
    const auto wrong_type_report = kg::verify_seed(
        wrong_type, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(!wrong_type_report.ok(),
          "a TaskCheck modifier column with the wrong type fails verification");
    CHECK(semantic_reason_contains(wrong_type_report,
                                   "modifier_property 'pseudohex_min'") &&
              semantic_reason_contains(wrong_type_report, "not integer"),
          "the semantic error names the TaskCheck column and required type");

    auto missing = parse_career_seed();
    CHECK(set_property(missing, "aerospace_defense_qual",
                       "modifier_property", "missing_modifier"),
          "the missing TaskCheck modifier column mutation was applied");
    const auto missing_report = kg::verify_seed(
        missing, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before("cepheus_careers.json"));
    CHECK(!missing_report.ok(),
          "an unknown TaskCheck modifier column fails verification");
    CHECK(semantic_reason_contains(missing_report,
                                   "unknown modifier_property "
                                   "'missing_modifier'"),
          "the semantic error names the unknown TaskCheck column");
}

void test_outcome_sequence_rejects_duplicate_order() {
    auto seed = parse_shared_seed();
    CHECK(set_property(seed, "mishap_3_s1", "step_index", "0"),
          "the duplicate step mutation was applied");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(!report.ok(), "duplicate outcome step order fails verification");
    CHECK(semantic_reason_contains(report, "duplicate step_index 0"),
          "the semantic error names the duplicate sequence index");
}

void test_outcome_sequence_requires_steps_and_contiguous_order() {
    auto empty = parse_shared_seed();
    CHECK(remove_relations_from(empty, "mishap_3"),
          "both sequence-step relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(empty_report,
                                   "has no OutcomeStep parts"),
          "an empty composite outcome fails semantic verification");

    auto wrong_part = parse_shared_seed();
    CHECK(retarget_relation(wrong_part, "mishap_3",
                            "mishap_3_s1", "mishap_3_c0"),
          "a sequence relation was retargeted to a non-step outcome");
    const auto wrong_part_report = kg::verify_seed(
        wrong_part, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(wrong_part_report,
                                   "non-OutcomeStep part type 'EndCareer'"),
          "a sequence rejects parts that are not OutcomeStep entities");

    auto gap = parse_shared_seed();
    CHECK(set_property(gap, "mishap_3_s1", "step_index", "2"),
          "the sequence gap mutation was applied");
    const auto gap_report = kg::verify_seed(
        gap, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(gap_report, "are not contiguous"),
          "a sequence rejects gaps in its explicit order");
}

void test_outcome_choice_requires_authority_options_and_order() {
    auto complete = seed_with_complete_choice();
    const auto complete_report = kg::verify_seed(
        complete, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
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
        authority, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(authority_report,
                                   "unknown choice_authority 'nobody'"),
          "a choice rejects authority outside player, referee, procedure");

    auto empty = seed_with_complete_choice();
    CHECK(remove_relations_from(empty, "injury_choice"),
          "both choice-option relations were removed");
    const auto empty_report = kg::verify_seed(
        empty, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(empty_report,
                                   "has no OutcomeOption parts"),
          "a choice without alternatives fails semantic verification");

    auto wrong_part = seed_with_complete_choice();
    CHECK(retarget_relation(wrong_part, "injury_choice",
                            "injury_choice_strength", "mishap_3_c0"),
          "a choice relation was retargeted to a non-option outcome");
    const auto wrong_part_report = kg::verify_seed(
        wrong_part, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(
              wrong_part_report,
              "non-OutcomeOption part type 'EndCareer'"),
          "a choice rejects parts that are not OutcomeOption entities");

    auto duplicate = seed_with_complete_choice();
    CHECK(set_property(duplicate, "injury_choice_dexterity",
                       "option_index", "0"),
          "the duplicate option index mutation was applied");
    const auto duplicate_report = kg::verify_seed(
        duplicate, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(duplicate_report,
                                   "duplicate option_index 0"),
          "a choice rejects duplicate option order");

    auto gap = seed_with_complete_choice();
    CHECK(set_property(gap, "injury_choice_dexterity", "option_index", "2"),
          "the option gap mutation was applied");
    const auto gap_report = kg::verify_seed(
        gap, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(gap_report, "are not contiguous"),
          "a choice rejects gaps in option order");

    auto empty_label = seed_with_complete_choice();
    CHECK(set_property(empty_label, "injury_choice_dexterity",
                       "option_label", ""),
          "the empty option label mutation was applied");
    const auto empty_label_report = kg::verify_seed(
        empty_label, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
    CHECK(semantic_reason_contains(empty_label_report,
                                   "has empty option_label"),
          "a choice rejects an unlabeled alternative");
}

void test_rollable_table_rejects_non_rows() {
    auto seed = parse_shared_seed();
    CHECK(retarget_relation(seed, "mishap_table", "mishap_row_2",
                            "mishap_2_c0"),
          "a rollable-table relation was retargeted to an Outcome");
    const auto report = kg::verify_seed(
        seed, game_path("srd/cepheus"), game_registry(), nullptr,
        prerequisites_before(
            "cepheus_book1_shared_tables.json"));
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
    test_lookup_rows_require_one_explicit_bound_mode_per_side();
    test_task_check_requires_an_integer_modifier_result();
    test_outcome_sequence_rejects_duplicate_order();
    test_outcome_sequence_requires_steps_and_contiguous_order();
    test_outcome_choice_requires_authority_options_and_order();
    test_rollable_table_rejects_non_rows();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
