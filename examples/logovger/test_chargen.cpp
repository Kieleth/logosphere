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
#include "chargen/rule_seeds.h"
#include "chargen/procedure_catalog.h"

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
#include <type_traits>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

static_assert(!std::is_copy_constructible_v<logovger::ChargenSession>);
static_assert(!std::is_move_constructible_v<logovger::ChargenSession>);

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
    kg.setMode(kg::KGMode::MINIMAL);
    const auto procedures = logovger::make_chargen_procedure_registry();
    // The same list the game loads. See chargen/rule_seeds.h.
    const auto& seeds = logovger::kRuleSeeds;
    // A seed may reference what an earlier one owns, so verification
    // sees the same growing world the game does.
    std::vector<kg::SeedEnvelope> kept;
    kept.reserve(logovger::kRuleSeedCount);
    std::vector<const kg::SeedEnvelope*> loaded_before;
    for (const char* seed : seeds) {
        const std::string json = slurp(game_path(seed));
        if (json.empty()) { why = std::string(seed) + " unreadable"; return false; }

        auto parsed = kg::parse_seed_envelope(json);
        if (!parsed.ok()) { why = "envelope: " + parsed.error; return false; }

        const auto v = kg::verify_seed(parsed.seed,
                                       game_path("srd/cepheus"),
                                       game_registry(), &procedures,
                                       loaded_before);
        if (!v.ok()) {
            std::ostringstream o;
            for (const auto& viol : v.violations)
                o << "[" << viol.check << "] " << viol.alias << ": "
                  << viol.reason << "; ";
            why = "verify: " + o.str();
            return false;
        }

        kg::SeedLoadReport report;
        if (!kg::load_seed(parsed.seed, kg, report)) {
            why = "load: " + report.error;
            return false;
        }
        kept.push_back(std::move(parsed.seed));
        loaded_before.push_back(&kept.back());
    }
    return true;
}

kg::EntityID agent_training_table(kg::KGModule& world) {
    kg::EntityID career = kg::INVALID_ENTITY;
    for (const auto id : world.findByType("Career")) {
        if (world.getProperty(id, "name") == "Agent") career = id;
    }
    if (career == kg::INVALID_ENTITY) return kg::INVALID_ENTITY;
    for (const auto part : world.getRelated(career, "HAS_PART")) {
        if (world.getRegistry().isSubtypeOf(world.getType(part),
                                            "RollableTable")) {
            return part;
        }
    }
    return kg::INVALID_ENTITY;
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
    // Seed 3, not 1. Re-enlistment is a throw now, not a decision, so
    // most lives end when the book says they end rather than when the
    // player is finished; seed 1 is refused after one term. This one
    // still runs the full path, which is what the case is for.
    req.seed        = 3;
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
    CHECK(!ok && error.find("no Procedure") != std::string::npos &&
              empty.findByType("Character").empty(),
          "a world with no procedure refuses before creating state: "
              + error);

    // The procedure can no longer be loaded on its own: its
    // re-enlistment step names the table that maps each career to its
    // throw, and that table lives with the career data. So the
    // "procedure but no careers" world is not merely useless now, it
    // is unbuildable, and the refusal arrives at LOAD time naming
    // exactly what is missing rather than at play time. That is a
    // stronger guarantee than the one this case used to make.
    const std::string procedure_json = slurp(
        game_path("seeds/cepheus_basic_chargen_procedure.json"));
    auto procedure_seed = kg::parse_seed_envelope(procedure_json);
    kg::SeedLoadReport procedure_load;
    const bool loaded_alone =
        procedure_seed.ok() &&
        kg::load_seed(procedure_seed.seed, empty, procedure_load);
    CHECK(!loaded_alone &&
              procedure_load.error.find("reenlistment throw by career") !=
                  std::string::npos,
          "the procedure refuses to load without the career data it "
          "consults, and names it: " + procedure_load.error);
    CHECK(empty.findByType("Character").empty(),
          "and the refused load leaves nothing behind");

    kg::KGModule world(game_registry());
    std::string why;
    build_world(world, why);
    req.career_name = "Xenolinguist";
    const bool ok2 = logovger::run_chargen(req, world, dice, sheet, error);
    CHECK(!ok2 && error.find("Xenolinguist") != std::string::npos,
          "and a career that is not in the book at all is refused by "
          "name, not silently substituted");
}

void test_missing_rule_constant_never_falls_back() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why),
          "the missing-constant control world loads: " + why);

    bool removed = false;
    for (const auto id : world.findByType("RuleConstant")) {
        if (world.getProperty(id, "name") != "max_terms") continue;
        world.removeProperty(id, "constant_value");
        removed = true;
    }
    CHECK(removed, "the control removed max_terms from the graph");

    logosphere::dice::DiceService dice;
    logovger::ChargenSession session(world, dice);
    std::string error;
    const bool ok = session.begin(1, error);
    CHECK(!ok && error.find("RuleConstant 'max_terms'") !=
                     std::string::npos &&
              error.find("invalid constant_value") != std::string::npos,
          "missing max_terms data fails instead of assuming seven: " +
              error);
}

void test_character_facts_use_the_modifier_table() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why),
          "the character-facts control world loads: " + why);

    logovger::CharacterSheet sheet;
    sheet.strength = sheet.dexterity = sheet.endurance = 9;
    sheet.intelligence = sheet.education = sheet.social_standing = 9;
    sheet.upp = "999999";

    std::string facts;
    std::string error;
    CHECK(logovger::format_character_facts(sheet, world, facts, error) &&
              facts.find("Str 9 (DM +1)") != std::string::npos,
          "narration facts use the seeded characteristic modifier: " +
              error);

    for (const auto row : world.findByType("CharacteristicModifierEntry")) {
        if (world.getProperty(row, "key_min") == "9") {
            world.setProperty(row, "characteristic_modifier", "42");
        }
    }
    facts.clear();
    error.clear();
    CHECK(logovger::format_character_facts(sheet, world, facts, error) &&
              facts.find("Str 9 (DM +42)") != std::string::npos,
          "changing modifier data changes narration facts without code: " +
              error);

    for (const auto row : world.findByType("CharacteristicModifierEntry")) {
        if (world.getProperty(row, "key_min") == "9") {
            world.removeProperty(row, "characteristic_modifier");
        }
    }
    facts.clear();
    error.clear();
    CHECK(!logovger::format_character_facts(sheet, world, facts, error) &&
              error.find("characteristic_modifier") != std::string::npos,
          "missing modifier data blocks narration facts loudly: " + error);
}

// The rules are DATA: change what the book says and the life changes,
// with no code touched. This is the claim the whole module rests on.
void test_the_rules_are_data() {
    kg::KGModule kg(game_registry());
    std::string why;
    if (!build_world(kg, why)) std::cout << "  [build_world] " << why << "\n";

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

    // The target lives on the career's qualification TaskCheck now, so
    // this reaches through the reference the seed built.
    const auto check_ref = kg.getProperty(career, "qualification_check");
    CHECK(!check_ref.empty(), "the career points at a qualification check");
    kg.setProperty(static_cast<kg::EntityID>(std::stoul(check_ref)),
                   "target_number", "13");

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

void test_characteristic_modifier_table_drives_checks() {
    kg::KGModule control(game_registry()), changed(game_registry());
    std::string why;
    CHECK(build_world(control, why), "the modifier control world loads: " +
                                         why);
    why.clear();
    CHECK(build_world(changed, why), "the changed modifier world loads: " +
                                         why);

    for (const auto row : changed.findByType("CharacteristicModifierEntry")) {
        changed.setProperty(row, "characteristic_modifier", "-100");
    }

    const logovger::ChargenRequest request{"Agent", 1, 1};
    logosphere::dice::DiceService control_dice, changed_dice;
    logovger::CharacterSheet control_sheet, changed_sheet;
    std::string error;
    const bool control_ok = logovger::run_chargen(
        request, control, control_dice, control_sheet, error);
    error.clear();
    const bool changed_ok = logovger::run_chargen(
        request, changed, changed_dice, changed_sheet, error);
    bool cited_changed_modifier = false;
    for (const auto& event : changed_sheet.life) {
        if (event.detail.find("-100 DM") != std::string::npos) {
            cited_changed_modifier = true;
        }
    }
    CHECK(control_ok && control_sheet.qualified && !changed_ok &&
              !changed_sheet.qualified && cited_changed_modifier &&
              error.find("Draft or Drifter") != std::string::npos,
          "changing only characteristic_modifier data changes the same "
          "qualification throw and stops at the unresolved authority "
          "choice: " + error);

    kg::KGModule incomplete(game_registry());
    why.clear();
    CHECK(build_world(incomplete, why),
          "the incomplete modifier world loads before mutation: " + why);
    for (const auto row :
         incomplete.findByType("CharacteristicModifierEntry")) {
        incomplete.removeProperty(row, "characteristic_modifier");
    }
    logosphere::dice::DiceService incomplete_dice;
    logovger::CharacterSheet incomplete_sheet;
    error.clear();
    const bool incomplete_ok = logovger::run_chargen(
        request, incomplete, incomplete_dice, incomplete_sheet, error);
    bool qualification_rolled = false;
    for (const auto& roll : incomplete_dice.journal()) {
        if (roll.purpose == "qualification") qualification_rolled = true;
    }
    CHECK(!incomplete_ok &&
              error.find("characteristic_modifier") != std::string::npos &&
              !qualification_rolled,
          "missing selected modifier data stops before the check roll: " +
          error);

    kg::KGModule wrong_column(game_registry());
    why.clear();
    CHECK(build_world(wrong_column, why),
          "the wrong-column world loads before mutation: " + why);
    kg::EntityID agent = kg::INVALID_ENTITY;
    for (const auto id : wrong_column.findByType("Career")) {
        if (wrong_column.getProperty(id, "name") == "Agent") agent = id;
    }
    const auto qualification_ref =
        wrong_column.getProperty(agent, "qualification_check");
    const auto qualification_check = static_cast<kg::EntityID>(
        std::stoul(qualification_ref));
    wrong_column.setProperty(qualification_check, "modifier_property",
                             "pseudohex_min");
    logosphere::dice::DiceService wrong_column_dice;
    logovger::CharacterSheet wrong_column_sheet;
    error.clear();
    const bool wrong_column_ok = logovger::run_chargen(
        request, wrong_column, wrong_column_dice, wrong_column_sheet, error);
    qualification_rolled = false;
    for (const auto& roll : wrong_column_dice.journal()) {
        if (roll.purpose == "qualification") qualification_rolled = true;
    }
    CHECK(!wrong_column_ok && error.find("integer") != std::string::npos &&
              !qualification_rolled,
          "chargen executes the TaskCheck's declared modifier column and "
          "rejects a string column before rolling: " + error);
}

void test_skill_outcome_parameters_drive_the_executor() {
    kg::KGModule changed(game_registry());
    std::string why;
    CHECK(build_world(changed, why), "the changed-rule world loads: " + why);
    for (const auto id : changed.findByType("AdvanceSkill")) {
        changed.setProperty(id, "existing_skill_delta", "7");
    }

    logosphere::dice::DiceService changed_dice;
    // Seed 3: a life long enough to gain the same skill twice, which
    // is what this case measures. Re-enlistment is a throw now, and
    // seed 1 is refused after one term.
    logovger::ChargenRequest request{"Agent", 3, 4};
    logovger::CharacterSheet changed_sheet;
    std::string error;
    const bool changed_ok = logovger::run_chargen(
        request, changed, changed_dice, changed_sheet, error);
    int max_level = 0;
    for (const auto part : changed.getRelated(changed_sheet.id, "HAS_PART")) {
        const auto level = changed.getProperty(part, "skill_level");
        if (!level.empty()) max_level = std::max(max_level, std::stoi(level));
    }
    CHECK(changed_ok && max_level >= 8,
          "changing existing_skill_delta in the KG changes repeated gains: "
              + error);

    kg::KGModule incomplete(game_registry());
    why.clear();
    CHECK(build_world(incomplete, why),
          "the incomplete-rule control world loads: " + why);
    for (const auto id : incomplete.findByType("AdvanceSkill")) {
        incomplete.removeProperty(id, "existing_skill_delta");
    }
    logosphere::dice::DiceService incomplete_dice;
    logovger::CharacterSheet incomplete_sheet;
    error.clear();
    const bool incomplete_ok = logovger::run_chargen(
        request, incomplete, incomplete_dice, incomplete_sheet, error);
    CHECK(!incomplete_ok &&
              error.find("existing_skill_delta") != std::string::npos,
          "missing required outcome data stops chargen loudly: " + error);
}

void test_skill_table_dice_data_drives_selection() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why),
          "the changed-table world loads: " + why);
    const auto table = agent_training_table(world);
    CHECK(table != kg::INVALID_ENTITY,
          "Agent has a skills and training RollableTable");
    if (table == kg::INVALID_ENTITY) return;

    const auto dice_ref = world.getProperty(table, "dice");
    const auto dice_id = static_cast<kg::EntityID>(std::stoul(dice_ref));
    world.setProperty(dice_id, "dice_modifier", "6");
    for (const auto row : world.getRelated(table, "HAS_PART")) {
        world.setProperty(
            row, "roll_min",
            std::to_string(std::stoi(world.getProperty(row, "roll_min")) + 6));
        world.setProperty(
            row, "roll_max",
            std::to_string(std::stoi(world.getProperty(row, "roll_max")) + 6));
    }

    logosphere::dice::DiceService dice;
    logovger::ChargenRequest request{"Agent", 1, 1};
    logovger::CharacterSheet sheet;
    std::string error;
    const bool ok = logovger::run_chargen(
        request, world, dice, sheet, error);
    const logosphere::dice::DiceRoll* training = nullptr;
    for (const auto& roll : dice.journal()) {
        if (roll.purpose == "skills and training") training = &roll;
    }
    CHECK(ok && sheet.skills.size() == 1 && training &&
              training->expression.modifier == 6 && training->total >= 7 &&
              training->total <= 12,
          "changing the table's DiceExpression and bands changes selection "
          "without procedure code changes: " + error);
}

void test_every_skill_table_row_is_validated_before_selection() {
    kg::KGModule control(game_registry());
    std::string why;
    CHECK(build_world(control, why), "the table control world loads: " + why);
    logosphere::dice::DiceService control_dice;
    logovger::ChargenRequest request{"Agent", 1, 1};
    logovger::CharacterSheet control_sheet;
    std::string error;
    CHECK(logovger::run_chargen(request, control, control_dice,
                                control_sheet, error),
          "the unmodified one-term control completes: " + error);
    int selected_total = 0;
    for (const auto& roll : control_dice.journal()) {
        if (roll.purpose == "skills and training") {
            selected_total = roll.total;
        }
    }
    CHECK(selected_total != 0,
          "the control identifies its skills and training roll");

    kg::KGModule malformed(game_registry());
    why.clear();
    CHECK(build_world(malformed, why),
          "the malformed-table world loads before mutation: " + why);
    const auto table = agent_training_table(malformed);
    kg::EntityID unselected = kg::INVALID_ENTITY;
    for (const auto row : malformed.getRelated(table, "HAS_PART")) {
        const int low = std::stoi(malformed.getProperty(row, "roll_min"));
        const int high = std::stoi(malformed.getProperty(row, "roll_max"));
        if (selected_total < low || selected_total > high) {
            unselected = row;
            break;
        }
    }
    CHECK(unselected != kg::INVALID_ENTITY,
          "the control has a row not selected by this seed");
    if (unselected == kg::INVALID_ENTITY) return;
    malformed.removeProperty(unselected, "outcome");

    logosphere::dice::DiceService malformed_dice;
    logovger::CharacterSheet malformed_sheet;
    error.clear();
    const bool ok = logovger::run_chargen(
        request, malformed, malformed_dice, malformed_sheet, error);
    bool training_roll = false;
    for (const auto& roll : malformed_dice.journal()) {
        if (roll.purpose == "skills and training") training_roll = true;
    }
    size_t ratings = 0;
    if (malformed_sheet.id != kg::INVALID_ENTITY) {
        for (const auto part : malformed.getRelated(malformed_sheet.id,
                                                    "HAS_PART")) {
            if (malformed.getRegistry().isSubtypeOf(
                    malformed.getType(part), "SkillRating")) {
                ++ratings;
            }
        }
    }
    CHECK(!ok && error.find("outcome") != std::string::npos &&
              !training_roll && ratings == 0,
          "a malformed unselected row blocks the table before a selection "
          "roll or outcome mutation: " + error);
}

void test_procedure_data_drives_chargen_control_flow() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the procedure-control world loads: " + why);

    kg::EntityID procedure = kg::INVALID_ENTITY;
    kg::EntityID decision = kg::INVALID_ENTITY;
    kg::EntityID finish = kg::INVALID_ENTITY;
    for (const auto id : world.findByType("Procedure")) {
        if (world.getProperty(id, "name") == "basic_chargen") {
            procedure = id;
        }
    }
    if (procedure != kg::INVALID_ENTITY) {
        for (const auto step : world.getRelated(procedure, "HAS_PART")) {
            const auto primitive = world.getProperty(step, "primitive_ref");
            if (primitive == "choose_term_end") decision = step;
            if (primitive == "finish_character") finish = step;
        }
    }
    CHECK(procedure != kg::INVALID_ENTITY &&
              decision != kg::INVALID_ENTITY &&
              finish != kg::INVALID_ENTITY,
          "the current playable flow is a seeded Procedure");
    if (decision == kg::INVALID_ENTITY || finish == kg::INVALID_ENTITY) return;

    bool redirected = false;
    for (const auto route : world.getRelated(decision, "HAS_PART")) {
        if (world.getProperty(route, "route_label") == "continue") {
            world.setProperty(route, "next_step", std::to_string(finish));
            redirected = true;
        }
    }
    CHECK(redirected, "the term decision carries a continue route in data");

    logosphere::dice::DiceService dice;
    // Seed 3: a life long enough to gain the same skill twice, which
    // is what this case measures. Re-enlistment is a throw now, and
    // seed 1 is refused after one term.
    logovger::ChargenRequest request{"Agent", 3, 4};
    logovger::CharacterSheet sheet;
    std::string error;
    const bool ok = logovger::run_chargen(
        request, world, dice, sheet, error);
    CHECK(ok && sheet.terms_served == 1,
          "retargeting only the seeded continue route ends after one term: "
              + error);
}

void test_unknown_runtime_primitive_fails_before_character_state() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why),
          "the unknown-primitive world loads: " + why);
    kg::EntityID procedure = kg::INVALID_ENTITY;
    for (const auto id : world.findByType("Procedure")) {
        if (world.getProperty(id, "name") == "basic_chargen") procedure = id;
    }
    bool mutated = false;
    for (const auto step : world.getRelated(procedure, "HAS_PART")) {
        if (world.getProperty(step, "primitive_ref") ==
            "roll_qualification") {
            world.setProperty(step, "primitive_ref", "invented_primitive");
            mutated = true;
        }
    }
    CHECK(mutated, "the runtime primitive mutation was applied");
    const auto characters_before = world.findByType("Character").size();

    logosphere::dice::DiceService dice;
    // Seed 3: a life long enough to gain the same skill twice, which
    // is what this case measures. Re-enlistment is a throw now, and
    // seed 1 is refused after one term.
    logovger::ChargenRequest request{"Agent", 3, 4};
    logovger::CharacterSheet sheet;
    std::string error;
    const bool ok = logovger::run_chargen(
        request, world, dice, sheet, error);
    CHECK(!ok && error.find("invented_primitive") != std::string::npos &&
              world.findByType("Character").size() == characters_before &&
              dice.journal().empty(),
          "runtime contract drift fails before character state or rolls: " +
              error);
}

}  // namespace

int main() {
    std::cout << "Logovger chargen (a life, from the book, in the graph)"
              << std::endl;
    test_a_life_is_generated();
    test_the_same_seed_replays_the_same_life();
    test_missing_rules_fail_loudly();
    test_missing_rule_constant_never_falls_back();
    test_character_facts_use_the_modifier_table();
    test_the_rules_are_data();
    test_characteristic_modifier_table_drives_checks();
    test_skill_outcome_parameters_drive_the_executor();
    test_skill_table_dice_data_drives_selection();
    test_every_skill_table_row_is_validated_before_selection();
    test_procedure_data_drives_chargen_control_flow();
    test_unknown_runtime_primitive_fails_before_character_state();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
