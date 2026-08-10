// The cepheus skills pack: the book's open skill vocabulary as data.
//
// Design under test: docs/RPG_MODULE.md step 3. The book keeps the
// skill list open ("Referees may add other skills as needed"), so
// Skill is a CLASS and the skills are KG instances; cascades are
// SPECIALIZES relations forming a tree (the book nests them two
// deep); a held level is the engine's SkillRating, attached with
// HAS_PART and written through the validated path. Every citation
// string-matches the vendored SRD, which on this branch is the
// fork's FIXED text: the Slug Rifle table row and the Sciences
// sentence are asserted as re-vendor tripwires.
//
// Usage:
//   ./build/test_skills_pack

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/ontology_validator.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"

#include <algorithm>
#include <fstream>
#include <iostream>
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

std::string srd_path(const std::string& rel) {
    return std::string(LOGOSPHERE_SOURCE_DIR) +
           "/examples/logovger/srd/cepheus/" + rel;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct CitedSkill {
    kg::EntityID id;
    std::string name;
    std::string quote;
};
std::vector<CitedSkill> skills;

kg::EntityID make_skill(kg::KGModule& kg, const std::string& name,
                        bool cascade, const std::string& quote) {
    auto id = kg.createEntity("Skill");
    kg.setProperty(id, "name", name);
    if (cascade) kg.setProperty(id, "is_cascade", "true");
    kg.setProperty(id, "source_file", kSkillsFile);
    kg.setProperty(id, "source_quote", quote);
    skills.push_back({id, name, quote});
    return id;
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
          "but SkillRating is engine: the generic GrantSkill handler "
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

// -------------------------------------------- the cascade tree, cited

kg::KGModule* world = nullptr;

void test_skills_and_cascades() {
    static kg::KGModule kg(logosphere::ontology::registry());
    kg.extendOntology(rulebook::ontology::registry());
    kg.extendOntology(cepheus_book1_skills::ontology::registry());
    kg.extendOntology(
        cepheus_book1_character_creation::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    world = &kg;

    auto gun_combat = make_skill(kg, "Gun Combat", true,
                                 "### Gun Combat (Cascade Skill)");
    auto slug_pistol = make_skill(
        kg, "Slug Pistol", false,
        "The character is skilled at using projectile-based pistols like "
        "the body pistol or snub pistol.");
    auto slug_rifle = make_skill(
        kg, "Slug Rifle", false,
        "The character is skilled at using projectile-based rifle weapons "
        "such as the autorifle or gauss rifle.");
    auto athletics = make_skill(
        kg, "Athletics", false,
        "This skill covers physical fitness and training, similar to that "
        "of a trained athlete.");
    auto vehicle = make_skill(kg, "Vehicle", true,
                              "### Vehicle (Cascade Skill)");
    auto aircraft = make_skill(kg, "Aircraft", true,
                               "### Aircraft (Cascade Skill)");
    auto winged = make_skill(
        kg, "Winged Aircraft", false,
        "This skill grants the ability to properly maneuver and perform "
        "basic, routine maintenance on jets and other airplanes using a "
        "lifting body.");

    bool all_created = true;
    for (const auto& s : skills)
        if (s.id == kg::INVALID_ENTITY) all_created = false;
    CHECK(all_created && skills.size() == 7, "seven cited skills landed");
    CHECK(kg.getProperty(gun_combat, "is_cascade") == "true" &&
              kg.getProperty(athletics, "is_cascade").empty(),
          "the book's cascade marking is explicit data");

    // The cascade tree: specialization points at its group, and the
    // book nests them (Winged Aircraft under Aircraft under Vehicle).
    CHECK(kg.createRelation(slug_pistol, "SPECIALIZES", gun_combat) !=
              kg::INVALID_RELATION,
          "Slug Pistol SPECIALIZES Gun Combat");
    kg.createRelation(slug_rifle, "SPECIALIZES", gun_combat);
    kg.createRelation(aircraft, "SPECIALIZES", vehicle);
    kg.createRelation(winged, "SPECIALIZES", aircraft);

    auto gun_specs = kg.getRelatedReverse(gun_combat, "SPECIALIZES");
    CHECK(gun_specs.size() == 2, "Gun Combat has two specializations here");
    auto step1 = kg.getRelated(winged, "SPECIALIZES");
    CHECK(step1.size() == 1 && step1[0] == aircraft,
          "Winged Aircraft specializes Aircraft...");
    auto step2 = kg.getRelated(aircraft, "SPECIALIZES");
    CHECK(step2.size() == 1 && step2[0] == vehicle,
          "...which specializes Vehicle: the tree nests two deep");
}

// --------------------------------------------------- ratings, validated

void test_a_character_holds_a_skill_through_the_validated_path() {
    auto& kg = *world;
    const auto reg_full = [] {
        auto r = logosphere::ontology::registry();
        r.extend(rulebook::ontology::registry());
        r.extend(cepheus_book1_skills::ontology::registry());
        r.extend(cepheus_book1_character_creation::ontology::registry());
        return r;
    }();

    const auto slug_rifle =
        std::find_if(skills.begin(), skills.end(),
                     [](const CitedSkill& s) { return s.name == "Slug Rifle"; })
            ->id;

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

    // Read the rating's skill back through the graph and confirm it
    // is the right skill by NAME: without this the ref could be
    // dropped entirely and everything above still passes.
    const auto ref = kg.getProperty(rating, "skill");
    CHECK(ref == std::to_string(slug_rifle),
          "the rating's skill ref survived the write");
    CHECK(!ref.empty() &&
              kg.getProperty(static_cast<kg::EntityID>(std::stoul(ref)),
                             "name") == "Slug Rifle",
          "and following it lands on Slug Rifle itself");
}

// ----------------------------------------------------------- citations

void test_quotes_are_verbatim_in_the_fixed_source() {
    auto& kg = *world;
    const std::string text = slurp(srd_path(kSkillsFile));
    CHECK(!text.empty(), "the vendored skills chapter is readable");

    // Read the citation back OUT OF THE KG, not out of the C++ struct
    // that wrote it: this is what proves the Cited mixin reached the
    // graph. A dropped source_quote slot fails here.
    bool all = true;
    size_t checked = 0;
    for (const auto& s : skills) {
        const std::string file = kg.getProperty(s.id, "source_file");
        const std::string quote = kg.getProperty(s.id, "source_quote");
        if (file != kSkillsFile || quote.empty()) {
            all = false;
            std::cout << "  [measure] uncited in the KG: " << s.name
                      << std::endl;
            continue;
        }
        ++checked;
        if (text.find(quote) == std::string::npos) {
            all = false;
            std::cout << "  [measure] NOT VERBATIM (" << s.name << ")"
                      << std::endl;
        }
    }
    CHECK(all && checked == skills.size(),
          "every skill carries its citation in the KG, verbatim in the "
          "vendored SRD");

    // Re-vendor tripwires: this branch vendors the FIXED fork text
    // (SOURCE_COMMIT points at our fix commit until upstream merges
    // PR #37). All three fixes must be present, or the pin drifted.
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

}  // namespace

int main() {
    std::cout << "Cepheus skills pack (the open vocabulary, as data)"
              << std::endl;
    std::cout << "note: [KG] REJECTED lines below are EXPECTED - they "
                 "are the contract." << std::endl;
    test_the_vocabulary_is_gated();
    test_registry_preserves_schema_provenance();
    test_character_slots_do_not_corrupt_imported_types();
    test_skills_and_cascades();
    test_a_character_holds_a_skill_through_the_validated_path();
    test_quotes_are_verbatim_in_the_fixed_source();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
