// The cepheus skills pack: the book's open skill vocabulary as data,
// loaded the way the game will actually load it.
//
// Design under test: docs/RPG_MODULE.md step 3. The book keeps the
// skill list open ("Referees may add other skills as needed"), so
// Skill is a CLASS and the skills are KG instances; cascades are
// SPECIALIZES relations forming a tree (the book nests them two
// deep); a held level is the engine's SkillRating, attached with
// HAS_PART and written through the validated path.
//
// The instances come from seeds/cepheus_book1_skills.json through
// parse_seed_envelope -> verify_seed -> load_seed: the same pipeline
// step 5's extraction will emit into and step 6 loads at game start.
// Nothing here writes a Skill by hand. That is the point: a test
// that builds its facts with trusted direct writes proves the test,
// not the pipeline.
//
// Usage:
//   ./build/test_skills_pack

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/ontology_validator.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

const char* kSkillsFile = "book1/skills.md";

std::string game_path(const std::string& rel) {
    return std::string(LOGOSPHERE_SOURCE_DIR) + "/examples/logovger/" + rel;
}
std::string source_root() { return game_path("srd/cepheus"); }
std::string srd_path(const std::string& rel) {
    return source_root() + "/" + rel;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The game's full vocabulary: engine core + the rulebook meta-pack +
// the cepheus chapter packs. A game assembles exactly this at start.
kg::OntologyRegistry game_registry() {
    auto r = logosphere::ontology::registry();
    r.extend(rulebook::ontology::registry());
    r.extend(cepheus_book1_skills::ontology::registry());
    r.extend(cepheus_book1_character_creation::ontology::registry());
    return r;
}

// Find a loaded skill by the alias the seed file bound it to.
kg::EntityID by_alias(const kg::SeedLoadReport& report,
                      const std::string& alias) {
    auto it = report.bindings.find(alias);
    return it == report.bindings.end() ? kg::INVALID_ENTITY : it->second;
}

// ----------------------------------------------------------- layering

void test_the_vocabulary_is_gated() {
    kg::KGModule core_only(logosphere::ontology::registry());
    core_only.setMode(kg::KGMode::MINIMAL);
    CHECK(core_only.createEntity("Wall") != kg::INVALID_ENTITY,
          "the core-only world is alive (control)");
    CHECK(core_only.createEntity("Skill") == kg::INVALID_ENTITY,
          "a core-only world has no Skill (expected [KG] REJECTED above)");

    kg::KGModule engine_only(rulebook::ontology::registry());
    engine_only.setMode(kg::KGMode::MINIMAL);
    CHECK(engine_only.createEntity("Skill") == kg::INVALID_ENTITY,
          "the engine rulebook pack has no Skill either - the "
          "vocabulary is the game's");
    CHECK(engine_only.createEntity("SkillRating") != kg::INVALID_ENTITY,
          "but SkillRating is engine: generic skill outcome handlers "
          "must know its shape");
}

void test_registry_preserves_schema_provenance() {
    const auto& reg = cepheus_book1_skills::ontology::registry();
    const auto skill = reg.entityTypes().find("Skill");
    const auto entity = reg.entityTypes().find("Entity");
    const auto* cascade = reg.findProperty("Skill", "is_cascade");

    CHECK(skill != reg.entityTypes().end() &&
              skill->second.source ==
                  "https://logosphere.dev/logovger/cepheus/book1-skills",
          "Skill records the Cepheus Skills schema as its source");
    CHECK(entity != reg.entityTypes().end() &&
              entity->second.source == "https://malleus.dev/schema",
          "an imported Entity retains the Malleus schema source");
    CHECK(cascade && cascade->source ==
              "https://logosphere.dev/logovger/cepheus/book1-skills",
          "is_cascade records the schema that declared the slot");
}

void test_character_slots_do_not_corrupt_imported_types() {
    using CharacterStrength = decltype(
        cepheus_book1_character_creation::ontology::Character{}.strength);
    using RelationStrength = decltype(
        cepheus_book1_character_creation::ontology::Relation{}.strength);
    using RelationType = decltype(
        cepheus_book1_character_creation::ontology::RelationEvent{}
            .relation_type);

    CHECK((std::is_same_v<CharacterStrength, std::optional<int32_t>>),
          "Character strength remains the book's bounded integer");
    CHECK((std::is_same_v<RelationStrength, std::optional<float>>),
          "Character strength does not replace Malleus relation strength");
    CHECK((std::is_same_v<RelationType, std::string>),
          "RelationEvent keeps Malleus's required relation type");
}

// ------------------------------------------- the seed, end to end

kg::KGModule* world = nullptr;
kg::SeedLoadReport load_report;

void test_the_skills_seed_verifies_and_loads() {
    const std::string json =
        slurp(game_path("seeds/cepheus_book1_skills.json"));
    CHECK(!json.empty(), "the skills seed file is readable");

    auto parsed = kg::parse_seed_envelope(json);
    CHECK(parsed.ok(), std::string("the envelope parses: ") + parsed.error);
    if (!parsed.ok()) return;
    CHECK(parsed.seed.layer == "cepheus" &&
              parsed.seed.source.file == kSkillsFile,
          "and declares its layer and source file");

    // The ingestion verifier first: citations verbatim in the pinned
    // source, schema valid, values consistent, invariants held. This
    // is the check that would catch an extraction inventing a skill.
    const auto v = kg::verify_seed(parsed.seed, source_root(),
                                   game_registry());
    for (const auto& viol : v.violations)
        std::cout << "  [measure] violation (" << viol.check << ") "
                  << viol.alias << ": " << viol.reason << std::endl;
    for (const auto& w : v.warnings)
        std::cout << "  [measure] warning: " << w << std::endl;
    CHECK(v.ok(), "the skills seed VERIFIES");
    CHECK(v.warnings.empty(),
          "with no source drift: the seed's pin matches SOURCE_COMMIT");
    std::cout << "  [measure] verified " << v.quotes_checked
              << " quotes, " << v.ops_loaded << " ops, "
              << v.invariants_checked << " invariants" << std::endl;

    // Then load it into the real world, the same call step 6 makes.
    static kg::KGModule kg(game_registry());
    kg.setMode(kg::KGMode::MINIMAL);
    world = &kg;
    const bool loaded = kg::load_seed(parsed.seed, kg, load_report);
    CHECK(loaded && load_report.ok,
          std::string("and LOADS into a live world: ") + load_report.error);
    CHECK(load_report.ops_applied == parsed.seed.ops.size(),
          "every op applied");
    CHECK(load_report.bindings.size() == 7,
          "seven aliases bound, one per skill");
}

// ------------------------------------------- the cascade tree, cited

void test_the_cascade_tree_is_in_the_graph() {
    auto& kg = *world;
    const auto gun_combat = by_alias(load_report, "gun_combat");
    const auto slug_rifle = by_alias(load_report, "slug_rifle");
    const auto athletics  = by_alias(load_report, "athletics");
    const auto vehicle    = by_alias(load_report, "vehicle");
    const auto aircraft   = by_alias(load_report, "aircraft");
    const auto winged     = by_alias(load_report, "winged_aircraft");
    CHECK(gun_combat != kg::INVALID_ENTITY &&
              slug_rifle != kg::INVALID_ENTITY &&
              winged != kg::INVALID_ENTITY,
          "the seed's aliases resolve to real entities");

    CHECK(kg.getProperty(gun_combat, "is_cascade") == "true" &&
              kg.getProperty(athletics, "is_cascade").empty(),
          "the book's cascade marking survived the load");
    CHECK(kg.getProperty(slug_rifle, "name") == "Slug Rifle",
          "and so did the names");

    auto gun_specs = kg.getRelatedReverse(gun_combat, "SPECIALIZES");
    CHECK(gun_specs.size() == 2,
          "Gun Combat has two specializations in the graph");
    auto step1 = kg.getRelated(winged, "SPECIALIZES");
    CHECK(step1.size() == 1 && step1[0] == aircraft,
          "Winged Aircraft specializes Aircraft...");
    auto step2 = kg.getRelated(aircraft, "SPECIALIZES");
    CHECK(step2.size() == 1 && step2[0] == vehicle,
          "...which specializes Vehicle: the tree nests two deep, "
          "built by the loader from aliases alone");
}

// --------------------------------------------------- ratings, validated

void test_a_character_holds_a_skill_through_the_validated_path() {
    auto& kg = *world;
    const auto reg_full = game_registry();
    const auto slug_rifle = by_alias(load_report, "slug_rifle");

    auto ok = kg::validate_kg_op(
        kg::KGOp{kg::KGOpCreateEntity{
            "SkillRating",
            {{"skill", std::to_string(slug_rifle)}, {"skill_level", "2"}}}},
        kg, reg_full);
    CHECK(ok.ok, std::string("a rating referencing a real skill validates: ")
                     + ok.reason);
    auto rating = kg.createEntity("SkillRating");
    kg.setProperty(rating, "skill", std::to_string(slug_rifle));
    kg.setProperty(rating, "skill_level", "2");

    auto junk = kg::validate_kg_op(
        kg::KGOp{kg::KGOpCreateEntity{"SkillRating", {{"skill", "banana"}}}},
        kg, reg_full);
    CHECK(!junk.ok, "a rating with a non-id skill ref is refused");
    auto neg = kg::validate_kg_op(
        kg::KGOp{kg::KGOpCreateEntity{"SkillRating",
                                      {{"skill_level", "-1"}}}},
        kg, reg_full);
    CHECK(!neg.ok, "a negative skill level is refused by the schema min");

    auto character = kg.createEntity("Character");
    CHECK(character != kg::INVALID_ENTITY, "the book's Character exists");
    CHECK(kg.createRelation(character, "HAS_PART", rating) !=
              kg::INVALID_RELATION,
          "the character holds the rating through HAS_PART");
    auto held = kg.getRelated(character, "HAS_PART");
    CHECK(held.size() == 1 && held[0] == rating &&
              kg.getProperty(rating, "skill_level") == "2",
          "Slug Rifle at 2, reachable from the character");

    const auto ref = kg.getProperty(rating, "skill");
    CHECK(ref == std::to_string(slug_rifle),
          "the rating's skill ref survived the write");
    CHECK(!ref.empty() &&
              kg.getProperty(static_cast<kg::EntityID>(std::stoul(ref)),
                             "name") == "Slug Rifle",
          "and following it lands on Slug Rifle itself");
}

// ----------------------------------------------------------- citations

void test_citations_are_in_the_kg_and_verbatim() {
    auto& kg = *world;
    const std::string text = slurp(srd_path(kSkillsFile));
    CHECK(!text.empty(), "the vendored skills chapter is readable");

    // The verifier already proved this against the seed; prove it
    // again from the LOADED graph, which is what the referee reads.
    size_t checked = 0;
    bool all = true;
    for (const auto& [alias, id] : load_report.bindings) {
        const std::string file  = kg.getProperty(id, "source_file");
        const std::string quote = kg.getProperty(id, "source_quote");
        if (file != kSkillsFile || quote.empty()) {
            all = false;
            std::cout << "  [measure] uncited in the KG: " << alias
                      << std::endl;
            continue;
        }
        ++checked;
        if (text.find(quote) == std::string::npos) {
            all = false;
            std::cout << "  [measure] NOT VERBATIM (" << alias << ")"
                      << std::endl;
        }
    }
    CHECK(all && checked == 7,
          "every loaded skill carries its citation in the KG, verbatim "
          "in the vendored SRD");

    // Re-vendor tripwires: SOURCE_COMMIT points at our fix branch
    // until upstream merges PR #37. All three fixes must be present,
    // or the pin drifted.
    CHECK(text.find("| [Veterinary Medicine](#veterinary-medicine) | "
                    "[Slug Rifle](#slug-rifle) | [Mole](#mole) |") !=
              std::string::npos,
          "the Slug Rifle table row is fixed in the vendored text");
    CHECK(text.find("The various specialties of this skill cover different "
                    "fields of scientific study.") != std::string::npos,
          "and the Sciences sentence is fixed too");
    const std::string chargen = slurp(srd_path("book1/character-creation.md"));
    CHECK(chargen.find("[Gun Combat-0](skills.md#gun-combat-cascade-skill)") !=
              std::string::npos &&
              chargen.find("[Animals-0](skills.md#animals)") ==
                  std::string::npos,
          "and the cascade anchors are fixed in the chargen chapter, with "
          "no short-anchor survivors");
    std::cout << "  [measure] " << checked
              << " KG citations + 3 re-vendor tripwires verified"
              << std::endl;
}

// ------------------------------------------------- the seed must bite

void test_a_lying_seed_is_refused() {
    // Every guard below runs against the REAL seed with one field
    // changed, so a check that stopped working shows up here rather
    // than in a hand-built toy.
    const std::string json =
        slurp(game_path("seeds/cepheus_book1_skills.json"));

    struct Case {
        const char* what;
        const char* from;
        const char* to;
        const char* expect;  // which check must fire
    };
    const Case cases[] = {
        {"a misquoted skill",
         "similar to that of a trained athlete.",
         "similar to that of a TRAINED athlete.", "verbatim"},
        {"a skill with no citation",
         "\"source_quote\": \"Gun Combat (Cascade Skill)\"",
         "\"source_quote\": \"\"", "verbatim"},
        {"a cascade link to a skill that was never created",
         "\"to\": \"@gun_combat\"", "\"to\": \"@laser_pistol\"", "schema"},
        {"a miscounted invariant",
         "\"Skill\": 7", "\"Skill\": 8", "invariant"},
    };

    for (const auto& c : cases) {
        std::string mutated = json;
        const auto at = mutated.find(c.from);
        if (at == std::string::npos) {
            tests_failed++;
            std::cout << "FAIL: mutation anchor missing for " << c.what
                      << std::endl;
            continue;
        }
        mutated.replace(at, std::strlen(c.from), c.to);

        auto parsed = kg::parse_seed_envelope(mutated);
        if (!parsed.ok()) {
            tests_failed++;
            std::cout << "FAIL: mutated seed did not parse (" << c.what
                      << "): " << parsed.error << std::endl;
            continue;
        }
        const auto v = kg::verify_seed(parsed.seed, source_root(),
                                       game_registry());
        CHECK(!v.ok() && v.count(c.expect) > 0,
              std::string(c.what) + " is refused by the " + c.expect +
                  " check");
    }

    // A duplicate name is the Slug Pistol class of error, the one that
    // sent us upstream in the first place.
    std::string dup = json;
    const auto at = dup.find("\"name\": \"Slug Rifle\"");
    dup.replace(at, std::strlen("\"name\": \"Slug Rifle\""),
                "\"name\": \"Slug Pistol\"");
    auto parsed_dup = kg::parse_seed_envelope(dup);
    const auto vd = kg::verify_seed(parsed_dup.seed, source_root(),
                                    game_registry());
    CHECK(!vd.ok() && vd.count("invariant") > 0,
          "and a duplicated skill name is refused - the very defect we "
          "reported upstream");
}

}  // namespace

int main() {
    std::cout << "Cepheus skills pack (the open vocabulary, as data)"
              << std::endl;
    std::cout << "note: [KG] REJECTED lines below are EXPECTED - they "
                 "are the contract." << std::endl;
    test_the_vocabulary_is_gated();
    test_registry_preserves_schema_provenance();
    test_character_slots_do_not_corrupt_imported_types();
    test_the_skills_seed_verifies_and_loads();
    if (world) {
        test_the_cascade_tree_is_in_the_graph();
        test_a_character_holds_a_skill_through_the_validated_path();
        test_citations_are_in_the_kg_and_verbatim();
    } else {
        tests_failed++;
        std::cout << "FAIL: the seed never loaded; downstream checks "
                     "skipped" << std::endl;
    }
    test_a_lying_seed_is_refused();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
