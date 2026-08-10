// Character generation, end to end: the first Logovger slice that
// actually plays.
//
// The claim under test is the engine thesis in miniature. A life is
// produced by reading the book's rules OUT OF THE KG (career,
// targets, characteristic, skill table, rows, outcomes), rolling
// them with the engine's seeded dice, and writing the result BACK
// into the KG. The rules arrive as a seed file the ingestion
// verifier passed. No rule value is a constant in the runner.
//
// Watchable artifact (rule 12): the life prints as a timeline, one
// line per event, every roll citable by id. Read it and you can see
// the character being made.
//
// Basic on purpose: qualification, survival, service skills, ageing.
// No commission, mishaps, benefits or ageing crisis yet.
//
// Usage:
//   ./build/test_chargen

#undef NDEBUG

#include "chargen/chargen.h"

#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

std::string game_path(const std::string& rel) {
    return std::string(LOGOSPHERE_SOURCE_DIR) + "/examples/logovger/" + rel;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

kg::OntologyRegistry game_registry() {
    auto r = logosphere::ontology::registry();
    r.extend(rulebook::ontology::registry());
    r.extend(cepheus_book1_skills::ontology::registry());
    r.extend(cepheus_book1_character_creation::ontology::registry());
    return r;
}

// A world with the Agent career loaded the way a game loads it.
bool build_world(kg::KGModule& kg, std::string& why) {
    const std::string json = slurp(game_path("seeds/cepheus_careers.json"));
    if (json.empty()) { why = "career seed unreadable"; return false; }

    auto parsed = kg::parse_seed_envelope(json);
    if (!parsed.ok()) { why = "envelope: " + parsed.error; return false; }

    const auto v = kg::verify_seed(parsed.seed,
                                   game_path("srd/cepheus"), game_registry());
    if (!v.ok()) {
        std::ostringstream o;
        for (const auto& viol : v.violations)
            o << "[" << viol.check << "] " << viol.alias << ": "
              << viol.reason << "; ";
        why = "verify: " + o.str();
        return false;
    }

    kg.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport report;
    if (!kg::load_seed(parsed.seed, kg, report)) {
        why = "load: " + report.error;
        return false;
    }
    return true;
}

// ------------------------------------------------------- the slice

void test_a_life_is_generated() {
    kg::KGModule kg(game_registry());
    std::string why;
    CHECK(build_world(kg, why), "the Agent career seed verifies and loads: "
                                    + why);
    if (!why.empty()) return;

    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.seed        = 1;     // a life that qualifies and serves out
    req.max_terms   = 4;

    logovger::CharacterSheet sheet;
    std::string error;
    const bool ok = logovger::run_chargen(req, kg, dice, sheet, error);
    CHECK(ok, "chargen runs: " + error);
    if (!ok) return;

    // The watchable artifact.
    std::cout << logovger::format_life(sheet);
    CHECK(sheet.qualified && sheet.terms_served == 4 &&
              sheet.skills.size() >= 3,
          "this life qualified and served four terms with skills - the "
          "full path, not just the early exit");

    // Every characteristic is a real 2D6 result, and the UPP agrees
    // with them (the book's own encoding, ehex).
    const int* c[] = {&sheet.strength, &sheet.dexterity, &sheet.endurance,
                      &sheet.intelligence, &sheet.education,
                      &sheet.social_standing};
    bool in_range = true;
    for (auto p : c) if (*p < 2 || *p > 12) in_range = false;
    CHECK(in_range, "six characteristics, each a 2D6 result");
    CHECK(sheet.upp.size() == 6, "the UPP encodes all six");

    // The life is a citable chain: every event that rolled has an id
    // that resolves in the dice journal, and ids are unique.
    std::set<uint64_t> seen;
    bool citable = true;
    size_t rolled = 0;
    for (const auto& e : sheet.life) {
        if (!e.roll_id) continue;
        ++rolled;
        if (!dice.find(e.roll_id) || !seen.insert(e.roll_id).second)
            citable = false;
    }
    CHECK(citable && rolled >= 7,
          "every rolled event cites a distinct journalled roll ("
              + std::to_string(rolled) + " rolls)");
    CHECK(dice.journal().size() >= rolled,
          "and the journal holds them all");

    // The result is in the graph, not just in the struct: a referee
    // reading the KG sees the same character.
    CHECK(kg.getProperty(sheet.id, "upp") == sheet.upp,
          "the UPP reached the KG");
    CHECK(std::stoi(kg.getProperty(sheet.id, "age_years")) == sheet.age_years,
          "and so did the age");

    // Skills gained are SkillRatings hanging off the character, and
    // each points at a Skill that came from the seed.
    size_t ratings = 0;
    bool refs_resolve = true;
    for (auto part : kg.getRelated(sheet.id, "HAS_PART")) {
        const auto ref = kg.getProperty(part, "skill");
        if (ref.empty()) continue;
        ++ratings;
        const auto skill = static_cast<kg::EntityID>(std::stoul(ref));
        if (kg.getProperty(skill, "name").empty()) refs_resolve = false;
    }
    CHECK(ratings <= sheet.skills.size() && refs_resolve && ratings > 0,
          "each held skill is one SkillRating pointing at a real Skill");

    // The book: gaining a skill you already have raises it instead of
    // granting a second copy. This life gained four skills across four
    // terms with repeats, so it must hold FEWER ratings than gains,
    // and one of them must be above level 1.
    int max_level = 0;
    for (auto part : kg.getRelated(sheet.id, "HAS_PART")) {
        const auto lv = kg.getProperty(part, "skill_level");
        if (!lv.empty()) max_level = std::max(max_level, std::stoi(lv));
    }
    CHECK(ratings < sheet.skills.size() && max_level >= 2,
          "a repeated skill was RAISED, not duplicated (highest level "
              + std::to_string(max_level) + " across " +
              std::to_string(ratings) + " ratings from " +
              std::to_string(sheet.skills.size()) + " gains)");
    std::cout << "  [measure] " << rolled << " rolls, " << ratings
              << " skill ratings, " << sheet.terms_served << " terms"
              << std::endl;
}

// The honesty property that makes a session replayable and a referee
// auditable: the same seed replays the same life, a different seed
// does not.
void test_the_same_seed_replays_the_same_life() {
    kg::KGModule a(game_registry()), b(game_registry()), c(game_registry());
    std::string why;
    build_world(a, why);
    build_world(b, why);
    build_world(c, why);

    logosphere::dice::DiceService da, db, dc;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.max_terms   = 4;

    logovger::CharacterSheet sa, sb, sc;
    std::string e;
    req.seed = 4242;  logovger::run_chargen(req, a, da, sa, e);
    req.seed = 4242;  logovger::run_chargen(req, b, db, sb, e);
    req.seed = 4243;  logovger::run_chargen(req, c, dc, sc, e);

    CHECK(sa.upp == sb.upp && sa.terms_served == sb.terms_served &&
              sa.skills == sb.skills,
          "the same seed replays the same life exactly");
    CHECK(logovger::format_life(sa) == logovger::format_life(sb),
          "down to the timeline, event for event");
    CHECK(!(sa.upp == sc.upp && sa.skills == sc.skills),
          "and a neighbouring seed lives a different one (the control)");
    std::cout << "  [measure] seed 4242: " << sa.upp << " vs seed 4243: "
              << sc.upp << std::endl;
}

// Rules missing from the world are a loud failure, never a default.
void test_missing_rules_fail_loudly() {
    kg::KGModule empty(game_registry());
    empty.setMode(kg::KGMode::MINIMAL);
    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";

    logovger::CharacterSheet sheet;
    std::string error;
    const bool ok = logovger::run_chargen(req, empty, dice, sheet, error);
    CHECK(!ok && error.find("no Career") != std::string::npos,
          "a world with no careers refuses to generate, and says so: "
              + error);

    kg::KGModule world(game_registry());
    std::string why;
    build_world(world, why);
    req.career_name = "Scout";
    const bool ok2 = logovger::run_chargen(req, world, dice, sheet, error);
    CHECK(!ok2 && error.find("Scout") != std::string::npos,
          "and a career the book defined but the seed did not is refused "
          "by name, not silently substituted");
}

// The rules are DATA: change what the book says and the life changes,
// with no code touched. This is the claim the whole module rests on.
void test_the_rules_are_data() {
    kg::KGModule kg(game_registry());
    std::string why;
    build_world(kg, why);

    // Find the Agent career and make qualification impossible.
    kg::EntityID career = kg::INVALID_ENTITY;
    for (auto id : kg.findByType("Career"))
        if (kg.getProperty(id, "name") == "Agent") career = id;
    CHECK(career != kg::INVALID_ENTITY, "the career is in the graph");

    // A career's instances are the book's, so a career must be able to
    // prove its numbers: the table row it was read from, verbatim in
    // the chapter. This is what the Cited mixin buys.
    const auto quote = kg.getProperty(career, "source_quote");
    const auto chapter = slurp(game_path("srd/cepheus/book1/"
                                         "character-creation.md"));
    CHECK(!quote.empty() && chapter.find(quote) != std::string::npos,
          "the career cites the career-table row it came from");

    kg.setProperty(career, "qualification_target", "13");

    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.seed        = 1;   // the seed that qualified a moment ago
    logovger::CharacterSheet sheet;
    std::string error;
    logovger::run_chargen(req, kg, dice, sheet, error);

    CHECK(!sheet.qualified && sheet.terms_served == 0,
          "the same seed that served four terms now never joins: raising "
          "the target in the KG alone ended the career, and the runner "
          "reads the rule rather than knowing it");
}

}  // namespace

int main() {
    std::cout << "Logovger chargen (a life, from the book, in the graph)"
              << std::endl;
    test_a_life_is_generated();
    test_the_same_seed_replays_the_same_life();
    test_missing_rules_fail_loudly();
    test_the_rules_are_data();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
