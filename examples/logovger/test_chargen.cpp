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
// Basic on purpose: qualification, survival, service skills, ageing,
// commission and advancement, benefits, and the aging crisis. No
// mishaps yet.
//
// Usage:
//   ./build/test_chargen

#undef NDEBUG

#include "chargen/chargen.h"
#include "chargen/rule_seeds.h"
#include "chargen/procedure_catalog.h"

#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "logosphere/rules/lookup_table_selector.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
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

// Every skill the character holds, as name -> level, read from the
// SkillRatings in the graph. What basic training did is a fact about
// the character, not about how the grant was made, so a test written
// this way survives the grant changing shape.
std::map<std::string, int> skills_held(const kg::KGModule& world,
                                       kg::EntityID character) {
    std::map<std::string, int> out;
    for (const auto part : world.getRelated(character, "HAS_PART")) {
        const std::string ref = world.getProperty(part, "skill");
        if (ref.empty()) continue;
        const auto skill = static_cast<kg::EntityID>(std::stoul(ref));
        const std::string level = world.getProperty(part, "skill_level");
        out[world.getProperty(skill, "name")] =
            level.empty() ? 0 : std::stoi(level);
    }
    return out;
}

// The skills a career's service table can grant, in table order.
std::vector<std::string> service_skill_names(const kg::KGModule& world,
                                             const std::string& career) {
    std::vector<std::string> out;
    for (const auto id : world.findByType("RollableTable")) {
        if (world.getProperty(id, "name") != career + " Service Skills") {
            continue;
        }
        for (const auto row : world.getRelated(id, "HAS_PART")) {
            const std::string outcome = world.getProperty(row, "outcome");
            if (outcome.empty()) continue;
            const auto grant = static_cast<kg::EntityID>(
                std::stoul(outcome));
            const std::string skill = world.getProperty(grant, "skill");
            if (skill.empty()) continue;
            out.push_back(world.getProperty(
                static_cast<kg::EntityID>(std::stoul(skill)), "name"));
        }
    }
    return out;
}

void test_a_life_is_generated() {
    kg::KGModule kg(game_registry());
    std::string why;
    CHECK(build_world(kg, why), "the Agent career seed verifies and loads: "
                                    + why);
    if (!why.empty()) return;

    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    // Seed 28, not 1. Re-enlistment is a throw now rather than a
    // decision, so most lives end when the book ends them and seed 1
    // is refused after a single term. This one runs the full path:
    // four terms, four gains, and one skill reached twice, which the
    // repeat assertion below needs.
    req.seed        = 28;
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
    // Ratings are no longer bounded by gains: basic training grants
    // every service skill at level 0 on entering a first career, and
    // those are held without ever having been rolled for. What must
    // hold is that a skill is held ONCE, whatever raised it.
    size_t ratings = 0;
    bool refs_resolve = true;
    bool one_rating_each = true;
    std::vector<std::string> held;
    for (auto part : kg.getRelated(sheet.id, "HAS_PART")) {
        const auto ref = kg.getProperty(part, "skill");
        if (ref.empty()) continue;
        ++ratings;
        if (std::find(held.begin(), held.end(), ref) != held.end()) {
            one_rating_each = false;
        }
        held.push_back(ref);
        const auto skill = static_cast<kg::EntityID>(std::stoul(ref));
        if (kg.getProperty(skill, "name").empty()) refs_resolve = false;
    }
    CHECK(refs_resolve && one_rating_each && ratings > 0,
          "each held skill is exactly one SkillRating pointing at a real "
          "Skill");

    // The book: gaining a skill you already have raises it instead of
    // granting a second copy. This life gained a skill it already held,
    // so some rating must stand above level 1 while each skill is still
    // held once, which the check above proves.
    int max_level = 0;
    for (auto part : kg.getRelated(sheet.id, "HAS_PART")) {
        const auto lv = kg.getProperty(part, "skill_level");
        if (!lv.empty()) max_level = std::max(max_level, std::stoi(lv));
    }
    CHECK(one_rating_each && max_level >= 2,
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
// "With the Referee's approval, you can keep the character that fails
// a survival roll and roll on the Survival Mishaps table instead."
//
// The whole chain in one life: a missed survival throw, the referee's
// call, a roll on the mishap table, and for two of its six rows a
// further roll on the Injury table, which the executor asks for and
// deliberately refuses to make itself.
void test_a_mishap_is_taken_instead_of_dying() {
    std::string why;
    kg::KGModule world(game_registry());
    // Once. Loading the same seeds into one world twice is refused, so
    // calling it again just to test the result silently skipped this
    // entire case: it reported one passing check and returned.
    const bool loaded = build_world(world, why);
    CHECK(loaded, "the mishap world loads: " + why);
    if (!loaded) return;

    const auto first_eligible =
        [](const logosphere::rules::AttributeSelectionRequest& request,
           std::vector<std::string>& chosen, std::string&) {
            chosen.assign(request.eligible.begin(),
                          request.eligible.begin() + request.count);
            return true;
        };

    int lives = 0, mishaps = 0, injuries = 0, refused = 0;
    uint64_t took_it = 0;
    std::string first_refusal;
    logovger::CharacterSheet marked;
    for (uint64_t seed = 1; seed <= 200 && took_it == 0; ++seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        req.attribute_selector = first_eligible;
        logovger::CharacterSheet sheet;
        std::string error;
        // A life that cannot be generated is a FINDING, not a seed to
        // skip. This loop used to `continue` here, and 8% of every
        // sweep was dying on mishap 1 and injury 3 - rows the book
        // prints and the executor was handing back unanswered - while
        // the measure line reported only the survivors.
        if (!logovger::run_chargen(req, world, dice, sheet, error)) {
            // The auto-player declares exactly one thing it will not
            // decide: Draft versus Drifter, which is an authority
            // choice. Any OTHER refusal is a rule the generator cannot
            // execute, and that is what this counts.
            if (error.find("Draft or Drifter") == std::string::npos) {
                ++refused;
                if (first_refusal.empty()) {
                    first_refusal =
                        "seed " + std::to_string(seed) + ": " + error;
                }
            }
            continue;
        }
        ++lives;
        bool had_mishap = false, had_injury = false;
        for (const auto& event : sheet.life) {
            if (event.what.rfind("mishap: ", 0) == 0) had_mishap = true;
            if (event.what.rfind("Injury: ", 0) == 0) had_injury = true;
        }
        if (had_mishap) ++mishaps;
        if (had_injury) { ++injuries; took_it = seed; marked = sheet; }
    }
    std::cout << "  [measure] " << lives << " lives, " << mishaps
              << " took a mishap, " << injuries
              << " reached the Injury table, " << refused
              << " stopped on a rule the generator could not execute\n";
    if (!first_refusal.empty()) {
        std::cout << "  [measure] first refusal: " << first_refusal << "\n";
    }

    CHECK(refused == 0,
          "no seed stops on a rule the generator cannot execute; " +
              std::to_string(refused) + " did, first was " + first_refusal);
    CHECK(mishaps > 0,
          "some life survives a failed throw by taking the mishap");
    CHECK(took_it != 0,
          "some mishap chains to the Injury table; none did, so the "
          "granted roll is unproven");
    if (took_it == 0) return;

    std::cout << "  [measure] seed " << took_it << ":\n"
              << logovger::format_life(marked);

    bool left = false, injured = false;
    for (const auto& event : marked.life) {
        if (event.what == "left the service" &&
            event.detail.rfind("two years into the term", 0) == 0) {
            left = true;
        }
        if (event.what.rfind("Injury: ", 0) == 0) injured = true;
    }
    CHECK(left, "the mishap ends the service two years into the term, not "
                "at the end of it");
    CHECK(injured, "and the Injury table was rolled and applied");

    // Every link in the chain cites its roll, so a reader can follow
    // the throw to the mishap to the injury.
    bool citable = true;
    size_t chain = 0;
    for (const auto& event : marked.life) {
        if (event.what.rfind("mishap: ", 0) != 0 &&
            event.what.rfind("Injury: ", 0) != 0) {
            continue;
        }
        ++chain;
        if (event.roll_id == 0) citable = false;
    }
    CHECK(citable && chain >= 2,
          "the mishap and the injury it caused each cite a roll (" +
              std::to_string(chain) + " links)");

    // The control: declining is still death, which is the book's
    // default and the thing the optional rule is an exception to.
    int declined = 0;
    for (uint64_t seed = 1; seed <= 200 && declined == 0; ++seed) {
        kg::KGModule solo(game_registry());
        if (!build_world(solo, why)) continue;
        logosphere::dice::DiceService dice;
        logovger::ChargenSession session(solo, dice);
        session.set_attribute_selector(first_eligible);
        std::string error;
        if (!session.begin(seed, error)) continue;
        bool refused = false;
        for (int guard = 0; guard < 300 && !session.finished(); ++guard) {
            if (session.choices().empty()) break;
            if (session.prompt().find("should have killed them") !=
                std::string::npos) {
                refused = true;
                if (!session.choose("2", error)) break;
                continue;
            }
            if (!session.choose(session.choices().front().key, error)) break;
        }
        if (refused && session.finished()) ++declined;
    }
    CHECK(declined > 0,
          "declining the mishap still ends the character, which is what "
          "the book does by default");
    std::cout << "  [measure] the declined life ended, as the book has it\n";
}

// Aging is the first rule the book leaves half-open: it fixes how many
// characteristics go and not which. The engine refuses to choose, so a
// life that rolls a damaging row cannot finish unless someone answers.
// This drives that path end to end.
//
// The seed is not hardcoded. A damaging row needs the modified 2D6 to
// land at 0 or below, which most lives never do, so the test sweeps
// until it finds one and asserts against THAT life. Finding none is
// itself a failure: it would mean aging had quietly stopped biting.
void test_aging_takes_what_the_referee_says_it_takes() {
    std::string why;

    // The stand-in referee: takes the first eligible, in order. A test
    // supplies its own so the production path never grows a fallback.
    std::vector<std::string> asked_for;
    int consulted = 0;
    const auto stub =
        [&](const logosphere::rules::AttributeSelectionRequest& request,
            std::vector<std::string>& chosen, std::string&) {
            ++consulted;
            asked_for = request.eligible;
            chosen.assign(request.eligible.begin(),
                          request.eligible.begin() + request.count);
            return true;
        };

    uint64_t bitten_seed = 0;
    logovger::CharacterSheet bitten;
    int bitten_consulted = 0;
    int ran_lives = 0, failed_lives = 0, aged_lives = 0, longest = 0;
    int marked_lives = 0;
    uint64_t whole_group_seed = 0;
    std::string last_failure;
    // One world, many lives. Rebuilding it per seed means verifying and
    // loading every rule seed again, which is the expensive part; the
    // characters are fresh entities either way. Most lives never reach
    // the aging table at all (re-enlistment is a throw, and auto-play
    // cannot answer the Draft), and the one that does has a 1-in-6
    // chance of being marked, so the sweep has to be wide.
    kg::KGModule sweep_world(game_registry());
    CHECK(build_world(sweep_world, why), "the sweep world loads: " + why);
    // Two separate proofs are wanted, and neither implies the other, so
    // the sweep runs until it has BOTH. Stopping at the first life that
    // consulted the referee left the whole-group case unproven whenever
    // the harder one happened to come first.
    for (uint64_t seed = 1;
         seed <= 400 && (bitten_seed == 0 || whole_group_seed == 0); ++seed) {
        kg::KGModule& world = sweep_world;
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        req.attribute_selector = stub;
        logovger::CharacterSheet sheet;
        std::string error;
        consulted = 0;
        if (!logovger::run_chargen(req, world, dice, sheet, error)) {
            ++failed_lives;
            if (last_failure.empty()) last_failure = error;
            continue;
        }
        ++ran_lives;
        if (sheet.terms_served >= 4) ++aged_lives;
        longest = std::max(longest, sheet.terms_served);
        for (const auto& event : sheet.life) {
            if (event.what.rfind("the years take", 0) != 0) continue;
            ++marked_lives;
            // A row that takes the WHOLE group leaves nothing to
            // decide, and those are the common ones. Remember the
            // first as its own proof, but keep hunting for a row that
            // actually asks.
            if (consulted == 0 && whole_group_seed == 0) {
                whole_group_seed = seed;
            }
            if (consulted > 0) {
                bitten_seed = seed;
                bitten = sheet;
                bitten_consulted = consulted;
            }
            break;
        }
    }

    std::cout << "  [measure] " << ran_lives << " lives ran, "
              << failed_lives << " failed, " << aged_lives
              << " reached 4+ terms, longest " << longest << " terms, "
              << marked_lives << " marked by aging\n";
    if (!last_failure.empty()) {
        std::cout << "  [measure] first failure: " << last_failure << "\n";
    }
    // A row that takes the whole group applies with nobody asked.
    // That is most of the aging table, and it must keep working in a
    // build that has no referee at all.
    CHECK(whole_group_seed != 0,
          "some life is marked by a row that takes the whole group, "
          "with no referee consulted");
    CHECK(bitten_seed != 0,
          "some life hits an aging row that leaves the choice open; "
          "none did, so either aging stopped firing or the rows that "
          "ask are unreachable");
    if (bitten_seed == 0) return;

    std::cout << "  [measure] seed " << bitten_seed
              << " is the first life aging marks\n";
    for (const auto& event : bitten.life) {
        if (event.what.rfind("the years", 0) == 0) {
            std::cout << "  [measure] " << event.what << "  ("
                      << event.detail << ")\n";
        }
    }

    CHECK(bitten_consulted > 0,
          "the referee was actually consulted for that life");
    CHECK(asked_for.size() == 3 &&
              std::find(asked_for.begin(), asked_for.end(), "strength") !=
                  asked_for.end(),
          "the choice offered was the physical group, not something else");

    // The control, and the point of the whole design: run the SAME
    // life with nobody to answer. It must fail loudly rather than pick
    // a characteristic for itself.
    kg::KGModule world(game_registry());
    CHECK(build_world(world, why), "the control world loads: " + why);
    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.seed = bitten_seed;
    req.max_terms = 7;
    // req.attribute_selector deliberately left empty.
    logovger::CharacterSheet sheet;
    std::string error;
    const bool ran = logovger::run_chargen(req, world, dice, sheet, error);
    CHECK(!ran && error.find("selector") != std::string::npos,
          "with nobody to decide, aging refuses instead of choosing: ran=" +
              std::to_string(ran) + " error=" + error);
}

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
    // WHICH missing table is named is op ordering, not a guarantee.
    // The procedure now also names the tables its Draft, mishap and
    // aging steps roll on, so the first unresolved reference moved when
    // those stopped being found by their English names. What must hold
    // is that it refuses, and that it says which table it wanted.
    const bool named_a_table =
        procedure_load.error.find("SubjectLookupTable/training_tables") !=
            std::string::npos ||
        procedure_load.error.find("RollableTable/") != std::string::npos;
    CHECK(!loaded_alone && named_a_table,
          "the procedure refuses to load without the career data it "
          "consults, and identifies the table it wanted: " +
              procedure_load.error);
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

// Leaving a career is not leaving the trade. Mustering out routed by
// falling through to the next step index, which was finish_character,
// so a character with five terms behind them and two still available
// was retired the moment they left their first career. The step routes
// explicitly now, and this is the case that says so.
// "A character gets one Benefit Roll for every full term served in
// THAT career", and rank is the rank you reached in it. Both were
// being counted across the whole life, so a third career paid for
// years spent in the first two and a Drifter kept a commission earned
// in the Navy.
void test_a_career_pays_only_for_its_own_years() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the benefits world loads: " + why);
    if (!why.empty()) return;

    logosphere::dice::DiceService dice;
    logovger::ChargenSession session(world, dice);
    std::string error;
    CHECK(session.begin(28, error), "a life begins: " + error);

    int careers_entered = 0;
    int terms_at_last_entry = 0;
    bool rank_carried_over = false;
    int benefit_rolls_offered = 0;
    int terms_in_that_career = 0;

    for (int step = 0; step < 500 && !session.finished(); ++step) {
        const auto& choices = session.choices();
        if (choices.empty()) break;
        const auto& sheet = session.sheet();

        // Entering a career: rank must be back to nothing.
        if (static_cast<int>(sheet.careers_served.size()) > careers_entered) {
            careers_entered = static_cast<int>(sheet.careers_served.size());
            terms_at_last_entry = sheet.terms_served;
            if (careers_entered > 1 && sheet.rank != 0) {
                rank_carried_over = true;
            }
        }
        // Benefits: count the offers, and the terms this career ran.
        if (session.prompt().find("benefit roll(s) left") !=
            std::string::npos) {
            if (benefit_rolls_offered == 0) {
                terms_in_that_career =
                    sheet.terms_served - terms_at_last_entry;
            }
            ++benefit_rolls_offered;
        }

        std::string take = choices.front().key;
        for (const auto& choice : choices) {
            if (choice.label == "Muster out") take = choice.key;
        }
        if (!session.choose(take, error)) break;
        if (benefit_rolls_offered > 0 &&
            session.prompt().find("benefit roll(s) left") ==
                std::string::npos) {
            break;                      // that payout is done
        }
    }

    CHECK(!rank_carried_over,
          "rank starts again at 0 in a new career rather than carrying "
          "the last one's commission over");
    CHECK(benefit_rolls_offered > 0,
          "a career that ended paid something: " +
              std::to_string(benefit_rolls_offered) + " roll(s)");
    // Rank bonuses can only ADD, so the payout is never fewer rolls
    // than the terms served in that career, and never the whole life's
    // terms when the career was shorter than the life.
    CHECK(benefit_rolls_offered >= terms_in_that_career,
          "at least one roll per term served in that career (" +
              std::to_string(benefit_rolls_offered) + " for " +
              std::to_string(terms_in_that_career) + " term(s))");
    std::cout << "  [measure] paid " << benefit_rolls_offered
              << " benefit roll(s) for " << terms_in_that_career
              << " term(s) in that career\n";
}

void test_leaving_a_career_offers_another_one() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the career-change world loads: " + why);
    if (!why.empty()) return;

    logosphere::dice::DiceService dice;
    logovger::ChargenSession session(world, dice);
    std::string error;
    CHECK(session.begin(28, error), "a life begins: " + error);

    // Serve, then LEAVE. That is the case in question: a character
    // with terms still available who is finished with one career.
    // Taking the first option every time would serve to the cap and
    // never exercise it.
    bool offered_another_career = false;
    for (int step = 0; step < 400 && !session.finished(); ++step) {
        const auto& choices = session.choices();
        if (choices.empty()) break;
        // Careers are offered by name, and Drifter is always there.
        for (const auto& choice : choices) {
            if (choice.label == "Drifter" && session.sheet().terms_served > 0) {
                offered_another_career = true;
            }
        }
        if (offered_another_career) break;

        std::string take = choices.front().key;
        for (const auto& choice : choices) {
            if (choice.label == "Muster out") take = choice.key;
        }
        if (!session.choose(take, error)) break;
    }
    CHECK(offered_another_career,
          "after a career ends, another one is offered rather than the "
          "character being retired: " +
              std::string(session.finished() ? "session finished" : error));
}

// Every number the book prints is a RuleConstant, and moving it in the
// graph moves the life. This exists because a cited constant nothing
// reads looks exactly like a cited constant something reads:
// prior_career_dm sat in the seed unread, and only asking "does
// changing it change anything" would have caught that.
void test_the_books_numbers_are_all_data() {
    // crisis_restore_value is deliberately absent. Reaching it needs a
    // life that both suffers a crisis and can pay for it, and an
    // auto-played character holds Cr0 until it musters out, so no seed
    // here gets there. test_the_aging_crisis_is_paid_for_or_kills
    // drives a session to a payable crisis and asserts the restored
    // value; asserting it here would only prove this harness cannot
    // reach the rule.
    // term_years is gone from this list because it is no longer a
    // constant: "Increase your age by 4 years" is an outcome the
    // advance_term STEP declares, and the step-outcome test below
    // proves it the same way.
    const char* const constants[] = {
        "aging_start_age", "cash_benefit_roll_max",
        "reenlistment_forced_natural",
    };
    // Values chosen to be unmistakable in a finished life: aging that
    // never starts, no cash rolls at all, and re-enlistment forced on
    // a natural 2.
    const char* const changed_to[] = {"99", "0", "2"};

    const auto life_under = [](kg::KGModule& world, uint64_t seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        req.attribute_selector =
            [](const logosphere::rules::AttributeSelectionRequest& request,
               std::vector<std::string>& chosen, std::string&) {
                chosen.assign(request.eligible.begin(),
                              request.eligible.begin() + request.count);
                return true;
            };
        logovger::CharacterSheet sheet;
        std::string error;
        logovger::run_chargen(req, world, dice, sheet, error);
        return logovger::format_life(sheet);
    };

    kg::KGModule control(game_registry());
    std::string why;
    CHECK(build_world(control, why), "the constants control world: " + why);
    if (!why.empty()) return;

    // Several seeds, because a constant only shows itself in a life
    // that reaches the rule: seed 28 never has a crisis, so moving the
    // crisis restore value moves nothing about it. Proving the rule is
    // live needs a life that gets there, not a bigger claim about one
    // that does not.
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i) {
        kg::KGModule changed(game_registry());
        CHECK(build_world(changed, why), "the changed world: " + why);
        bool found = false;
        for (const auto id : changed.findByType("RuleConstant")) {
            if (changed.getProperty(id, "name") != constants[i]) continue;
            changed.setProperty(id, "constant_value", changed_to[i]);
            found = true;
        }
        CHECK(found, std::string("'") + constants[i] +
                         "' is a RuleConstant in the graph");

        uint64_t moved_at = 0;
        for (uint64_t seed = 1; seed <= 120 && moved_at == 0; ++seed) {
            if (life_under(control, seed) != life_under(changed, seed)) {
                moved_at = seed;
            }
        }
        std::cout << "  [measure] " << constants[i] << " -> "
                  << changed_to[i] << ": life changes at seed " << moved_at
                  << "\n";
        CHECK(moved_at != 0,
              std::string("moving '") + constants[i] + "' to " +
                  changed_to[i] + " changes some life in 120 seeds; none "
                  "changed, so nothing reads it");
    }
}

// "Increase your age by 4 years" is the whole of what its checklist
// step does, so the STEP declares it and the executor applies it. It
// used to be age_years += 4 inside the primitive, where no reader of
// the procedure could find it.
void test_a_step_can_declare_what_it_does() {
    const auto age_after_four_terms = [](kg::KGModule& world) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = 28;
        req.max_terms = 4;
        logovger::CharacterSheet sheet;
        std::string error;
        logovger::run_chargen(req, world, dice, sheet, error);
        return sheet.age_years;
    };

    kg::KGModule control(game_registry());
    std::string why;
    CHECK(build_world(control, why), "the step-outcome world: " + why);
    if (!why.empty()) return;
    const int normal = age_after_four_terms(control);
    CHECK(normal > 18,
          "a served life ages at all: " + std::to_string(normal));

    // Move the number the STEP declares, not a constant in the code.
    kg::KGModule changed(game_registry());
    CHECK(build_world(changed, why), "the changed-step world: " + why);
    // Reached through the STEP, not by hunting for a matching entity:
    // mishap 5's four years of imprisonment are also age_years +4, and
    // they are a different rule.
    int found = 0;
    for (const auto id : changed.findByType("ProcedureStep")) {
        if (changed.getProperty(id, "primitive_ref") != "advance_term") {
            continue;
        }
        const std::string declared = changed.getProperty(id, "outcome");
        CHECK(!declared.empty(),
              "the advance_term step declares an outcome");
        if (declared.empty()) continue;
        changed.setProperty(
            static_cast<kg::EntityID>(std::stoul(declared)),
            "attribute_delta", "10");
        ++found;
    }
    CHECK(found == 1,
          "one advance_term step, declaring its years: " +
              std::to_string(found));
    const int stretched = age_after_four_terms(changed);
    std::cout << "  [measure] four terms age a character " << normal
              << ", and " << stretched << " when the step says ten years\n";
    CHECK(stretched > normal,
          "the years a term costs come off the step, not out of the "
          "procedure: " + std::to_string(normal) + " vs " +
              std::to_string(stretched));
}

// A rule that treats one table differently reads the ROW that offers
// it, not the table's printed name. Cash rolls were capped by finding
// "Cash Benefits" or "Cost Benefits" as substrings, and basic training
// found its table by comparing the last fourteen characters against
// "Service Skills": rules about English, not about the book. Rename
// every table and the same lives must come out.
void test_renaming_every_table_changes_nothing() {
    const auto life = [](kg::KGModule& world, uint64_t seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        logovger::CharacterSheet sheet;
        std::string error;
        logovger::run_chargen(req, world, dice, sheet, error);
        // The skills and the age, not the prose: a table's name is
        // printed in the timeline, and renaming it is meant to change
        // exactly that and nothing else.
        std::string shape = std::to_string(sheet.age_years) + "/" +
                            std::to_string(sheet.terms_served) + "/" +
                            std::to_string(sheet.credits) + "/" + sheet.upp;
        for (const auto& skill : sheet.skills) shape += "|" + skill;
        return shape;
    };

    kg::KGModule control(game_registry());
    std::string why;
    CHECK(build_world(control, why), "the naming control world: " + why);
    if (!why.empty()) return;

    kg::KGModule renamed(game_registry());
    CHECK(build_world(renamed, why), "the renamed world: " + why);
    // EVERY table, the Draft, Survival Mishaps and Aging included: the
    // steps that roll on those now name them, so nothing in the run
    // reads a table's printed name for anything but display.
    int touched = 0;
    for (const auto table : renamed.findByType("RollableTable")) {
        renamed.setProperty(table, "name",
                            "table " + std::to_string(table) + " (renamed)");
        ++touched;
    }
    std::cout << "  [measure] renamed " << touched << " tables, all of them\n";
    CHECK(touched > 90,
          "the world holds the tables this is about: " +
              std::to_string(touched));

    int compared = 0, differed = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        ++compared;
        if (life(control, seed) != life(renamed, seed)) ++differed;
    }
    std::cout << "  [measure] " << compared
              << " lives against renamed tables, " << differed
              << " came out different\n";
    CHECK(differed == 0,
          "no rule depends on what a table is called: " +
              std::to_string(differed) + " of " + std::to_string(compared) +
              " lives differed");
}

// "This mishap is always enough to force you to leave the service
// after half a term, or two years of service" is an effect the book
// applies, so every mishap row carries it as an outcome rather than
// the procedure adding it. Mishap 5 states its own four years of
// imprisonment and carries no half-term two: the reading, recorded on
// that row, is that the four INCLUDE them. Four years, not six.
void test_a_mishap_costs_the_years_its_row_states() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the mishap-years world: " + why);
    if (!why.empty()) return;

    int two_years = 0, four_years = 0;
    bool recorded = false;
    for (const auto id : world.findByType("ModifyAttribute")) {
        if (world.getProperty(id, "attribute_ref") != "age_years") continue;
        const std::string delta = world.getProperty(id, "attribute_delta");
        if (delta == "2") ++two_years;
        if (delta != "4") continue;
        // Two entities carry four years: this row, and the step that
        // says a term lasts four. Only the mishap row is unsettled.
        ++four_years;
        if (world.getProperty(id, "unmodelled").find("imprisonment") !=
            std::string::npos) {
            recorded = true;
        }
    }
    std::cout << "  [measure] year-outcomes: " << two_years
              << " of two years, " << four_years << " of four\n";
    CHECK(two_years == 5,
          "five of the six mishap rows cost two years each: " +
              std::to_string(two_years));
    CHECK(four_years >= 1,
          "and the prison row states its own four: " +
              std::to_string(four_years));
    CHECK(recorded,
          "the prison row says on ITSELF that the book does not settle "
          "this, and how it was read");
}

// "An additional benefit is gained if the character held rank O4, and
// two for rank O5. A character with rank O6 gains three extra
// benefits." Four numbers the book prints, read from a table keyed by
// rank rather than an if-ladder in the procedure. The O4 row states
// its count with the indefinite article, so it is proved by those
// words; the ranks the book never mentions have no row at all.
void test_extra_benefits_by_rank_are_a_table() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the rank-bonus world: " + why);
    if (!why.empty()) return;

    kg::EntityID table = kg::INVALID_ENTITY;
    for (const auto id : world.findByType("LookupTable")) {
        if (world.getProperty(id, "name") == "extra_benefits_by_rank") {
            table = id;
        }
    }
    CHECK(table != kg::INVALID_ENTITY,
          "the ladder is a table in the graph, not a branch in the code");
    if (table == kg::INVALID_ENTITY) return;

    logosphere::rules::LookupTableSelector selector(world);
    const int expected[] = {0, 0, 0, 0, 1, 2, 3};
    std::string measured;
    for (int rank = 0; rank <= 6; ++rank) {
        const auto row = selector.select(table, rank);
        int got = 0;
        if (row.ok()) {
            got = std::stoi(world.getProperty(row.selection->row(),
                                              "extra_benefit_rolls"));
        }
        measured += (rank ? " " : "") + std::to_string(rank) + ":" +
                    (row.ok() ? std::to_string(got)
                              : (row.missed ? "-" : "ERR"));
        if (rank < 4) {
            CHECK(!row.ok() && row.missed && row.error.empty(),
                  "rank " + std::to_string(rank) + " is one the book says "
                  "nothing about, so it misses rather than finding a zero");
        } else {
            CHECK(row.ok() && got == expected[rank],
                  "rank " + std::to_string(rank) + " grants " +
                      std::to_string(expected[rank]) + ": got " +
                      std::to_string(got) + " " + row.error);
        }
    }
    std::cout << "  [measure] extra benefits by rank: " << measured
              << "  (- is a miss, where the book is silent)\n";

    // The count the book states without writing carries the words that
    // state it, so a reader can see the inference and argue with it.
    bool implied = false;
    for (const auto row : world.getRelated(table, "HAS_PART")) {
        if (world.getProperty(row, "extra_benefit_rolls") != "1") continue;
        implied = !world.getProperty(row, "implied_by").empty();
    }
    CHECK(implied,
          "the O4 row says which words state its count, having no digit "
          "and no number word to prove it");
}

// An arbiter's decision leaves a record, and the record says who
// decided in words you can read.
//
// This was the one input to a character with no provenance at all.
// Every other value traces to a rule and a roll; a decision traced to
// a line of console output that scrolled away, and the arbiter's
// identity existed only inside a one-way cache-key hash, so the graph
// could not answer "which model chose this" even in principle.
//
// The test writes the record the way the session writes it, which also
// proves the schema wiring: the KG property gate aborts on an
// undeclared property, so a wrong slot name would kill this process
// rather than quietly store nothing.
void test_a_judgment_says_who_decided_and_why() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the judgment world: " + why);
    if (!why.empty()) return;

    const auto record = world.createEntity("ArbiterDecision");
    world.setProperty(record, "decision_question",
                      "The years take their due. Which 2 give way, and why?");
    world.setProperty(record, "decision_options",
                      "Strength, Dexterity, Endurance");
    world.setProperty(record, "decision_taken", "Strength, Endurance");
    world.setProperty(record, "decision_reason",
                      "a life spent hauling cargo wears the back first");
    world.setProperty(record, "arbiter", "anthropic/claude-haiku-4-5");

    CHECK(world.getProperty(record, "decision_taken") == "Strength, Endurance",
          "the choice round-trips");
    CHECK(world.getProperty(record, "decision_options").find("Dexterity") !=
              std::string::npos,
          "the options it was chosen FROM are kept, not just the answer");
    CHECK(!world.getProperty(record, "decision_reason").empty(),
          "the reason survives instead of scrolling past");

    // The point of the whole record. A hash would satisfy "a value is
    // stored" and fail the thing this exists for.
    const std::string arbiter = world.getProperty(record, "arbiter");
    CHECK(arbiter.find("claude") != std::string::npos,
          "the arbiter is readable, not a digest: " + arbiter);
    CHECK(arbiter.find_first_of(" /") != std::string::npos,
          "the arbiter names a backend and a model: " + arbiter);

    // And it is findable without knowing its id, which is what makes
    // it queryable provenance rather than a note.
    CHECK(world.findByType("ArbiterDecision").size() == 1,
          "the decision is queryable by type");
}

// Every roll a rule made says WHICH rule made it, and that rule is in
// the graph. This is the link an oracle derived from the KG needs:
// with it, the journal plus the graph is enough to re-derive what a
// rule permitted and compare it to what happened, so the rules become
// their own acceptance test. `purpose` reads well in a timeline and
// resolves to nothing.
void test_a_roll_names_the_rule_that_made_it() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the provenance world: " + why);
    if (!why.empty()) return;

    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.seed = 28;
    req.max_terms = 4;
    req.attribute_selector =
        [](const logosphere::rules::AttributeSelectionRequest& request,
           std::vector<std::string>& chosen, std::string&) {
            chosen.assign(request.eligible.begin(),
                          request.eligible.begin() + request.count);
            return true;
        };
    logovger::CharacterSheet sheet;
    std::string error;
    logovger::run_chargen(req, world, dice, sheet, error);

    // The rules that go through a runner: throws and table rolls. Rolls
    // a procedure makes directly - the crisis price, for one - name no
    // rule yet, and are counted rather than asserted, so this says what
    // is true today instead of overclaiming.
    int with_rule = 0, without = 0, resolvable = 0;
    std::map<std::string, int> unlinked;
    for (const auto& roll : dice.journal()) {
        if (roll.rule == 0) {
            ++without;
            ++unlinked[roll.purpose];
            continue;
        }
        ++with_rule;
        const auto rule = static_cast<kg::EntityID>(roll.rule);
        if (!world.exists(rule)) continue;
        const std::string type = world.getType(rule);
        if (world.getRegistry().isSubtypeOf(type, "TaskCheck") ||
            world.getRegistry().isSubtypeOf(type, "RollableTable")) {
            ++resolvable;
        }
    }
    std::string still;
    for (const auto& [purpose, count] : unlinked) {
        still += (still.empty() ? "" : ", ") + purpose + "x" +
                 std::to_string(count);
    }
    std::cout << "  [measure] " << with_rule << " rolls name their rule, "
              << without << " do not (" << still << ")\n";

    CHECK(with_rule > 0, "some roll names the rule that made it");
    CHECK(resolvable == with_rule,
          "and every named rule is a TaskCheck or RollableTable actually "
          "in the graph: " + std::to_string(resolvable) + " of " +
              std::to_string(with_rule));
}

// "You begin as a Rank 0 character." Twenty-three of the twenty-four
// careers print something in that row of their Ranks and Skills table
// - Aerospace gives Aircraft-1, Navy Zero-G-1, Physician Medicine-1 -
// and every one of those grants is in the graph, cited to the cell it
// came from, read by nothing. Every character finishes a skill level
// short of what the book gives them.
//
// Written before the fix, and asserting what the CHARACTER ends up
// holding rather than how the grant is delivered, so it survives the
// wiring that makes it pass.
void test_joining_a_career_grants_its_rank_zero_skill() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the rank-zero world: " + why);
    if (!why.empty()) return;

    // What the book says each career starts you on, read from the
    // graph: the rank-0 step of its track, and the skill its grant
    // names. Nothing here is a list of careers we chose to check.
    std::map<std::string, std::string> owed;
    for (const auto row : world.findByType("CareerTrackEntry")) {
        const std::string subject = world.getProperty(row, "subject");
        const std::string track = world.getProperty(row, "track");
        if (subject.empty() || track.empty()) continue;
        for (const auto step : world.getRelated(
                 static_cast<kg::EntityID>(std::stoul(track)), "HAS_PART")) {
            if (world.getProperty(step, "step_index") != "0") continue;
            const std::string grant = world.getProperty(step, "grants");
            if (grant.empty()) continue;
            const std::string skill = world.getProperty(
                static_cast<kg::EntityID>(std::stoul(grant)), "skill");
            if (skill.empty()) continue;
            owed[world.getProperty(
                static_cast<kg::EntityID>(std::stoul(subject)), "name")] =
                world.getProperty(
                    static_cast<kg::EntityID>(std::stoul(skill)), "name");
        }
    }
    CHECK(owed.size() >= 20,
          "the graph knows what most careers start you on: " +
              std::to_string(owed.size()) + " of 24");

    std::string missing;
    int checked = 0;
    for (const auto& pair : owed) {
        // Asking for a career is not joining one: the qualification
        // throw can turn you away and the Draft puts you somewhere
        // else. Walk seeds until the dice let this career happen.
        bool joined = false;
        for (unsigned seed = 1; seed <= 60 && !joined; ++seed) {
            logosphere::dice::DiceService dice;
            logovger::ChargenSession session(world, dice);
            std::string error;
            CHECK(session.begin(seed, error), "a life begins: " + error);
            if (!session.choose(pair.first, error)) continue;
            if (session.sheet().career != pair.first) continue;
            joined = true;
            ++checked;
            const auto held = skills_held(world, session.sheet().id);
            const auto found = held.find(pair.second);
            const int level = found == held.end() ? -1 : found->second;
            if (level < 1) {
                missing += (missing.empty() ? "" : "; ") + pair.first +
                           " owes " + pair.second + "-1, holds " +
                           (level < 0 ? "nothing"
                                      : "level " + std::to_string(level));
            }
        }
        if (!joined) {
            missing += (missing.empty() ? "" : "; ") + pair.first +
                       " was never joined in 60 seeds";
        }
    }
    std::cout << "  [measure] " << owed.size()
              << " careers print a rank 0 skill, " << checked
              << " were joined and checked\n";
    CHECK(missing.empty(),
          "joining a career grants the skill its rank 0 row prints: " +
              missing);
}

// The rank 0 grants were faithful, sound, counted, and reached by
// nothing. No gate here could see that: the citation check proves the
// data matches the book, the well-formedness checks prove the graph
// hangs together, and a reader census at the level of TYPES was clean
// because the type had readers, just not those instances.
//
// This is the only check that answers "does the game act on it". It
// plays lives and asks the executor which absorbed rules it actually
// applied, then names the ones no life ever received.
void test_every_absorbed_rule_reaches_a_character() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the coverage world: " + why);
    if (!why.empty()) return;

    // The population is the executed vocabulary, taken from the
    // ontology rather than a list kept by hand: every concrete type the
    // executor can apply. A hand-kept list would go stale the first
    // time a new outcome type is absorbed, which is exactly the class
    // of silence this test exists to break.
    // The world's own registry, not a fresh game_registry(): that one
    // returns BY VALUE, so ranging over a temporary's entityTypes()
    // reads a destroyed map. It segfaulted exactly that way once.
    const auto& registry = world.getRegistry();
    std::map<kg::EntityID, std::string> absorbed;
    for (const auto& entry : registry.entityTypes()) {
        if (!registry.isSubtypeOf(entry.first, "Outcome")) continue;
        for (const auto id : world.findByType(entry.first)) {
            absorbed[id] = entry.first + " " +
                           world.getProperty(id, "name");
        }
    }
    CHECK(absorbed.size() > 500,
          "the graph holds the book's outcomes: " +
              std::to_string(absorbed.size()));

    std::vector<std::string> careers;
    for (const auto id : world.findByType("Career")) {
        careers.push_back(world.getProperty(id, "name"));
    }
    std::sort(careers.begin(), careers.end());

    // Every career, several seeds each, so a rule only one profession
    // can reach still gets its chance. And where the book leaves the
    // choice open, the sweep rotates through the options instead of
    // always taking the same one: with fixed taste the number measures
    // the auto-player's habits, not the book.
    std::set<kg::EntityID> reached;
    int lives = 0;
    for (const auto& career : careers) {
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            logosphere::dice::DiceService dice;
            logovger::ChargenRequest request;
            request.career_name = career;
            request.seed = seed;
            request.max_terms = 7;
            // Rotating, not random: a given seed always plays the same
            // life, so a finding here replays exactly. Every key comes
            // from the offered set, so nothing invalid is ever chosen.
            size_t turn = seed;
            request.taste = [&turn](const std::string&,
                                    const std::vector<logovger::Choice>& cs) {
                if (cs.empty()) return std::string();
                return cs[turn++ % cs.size()].key;
            };
            // Somebody has to answer the aging table, and without a
            // selector the engine refuses and the life ends there. The
            // sweep had been walking into that every time a character
            // reached the fourth term, which is why aging outcomes sat
            // at 1 of 14 reached: not because the rules were unwired,
            // but because no life got past the question.
            request.attribute_selector =
                [&turn](const logosphere::rules::AttributeSelectionRequest&
                            ask,
                        std::vector<std::string>& chosen,
                        std::string& error) {
                    for (int i = 0; i < ask.count; ++i) {
                        std::string pick;
                        for (size_t n = 0; n < ask.eligible.size(); ++n) {
                            const auto& name =
                                ask.eligible[(turn + n) % ask.eligible.size()];
                            if (std::find(ask.already_taken.begin(),
                                          ask.already_taken.end(), name) !=
                                ask.already_taken.end()) continue;
                            if (std::find(chosen.begin(), chosen.end(),
                                          name) != chosen.end()) continue;
                            pick = name;
                            break;
                        }
                        if (pick.empty()) {
                            error = "nothing left to take";
                            return false;
                        }
                        ++turn;
                        chosen.push_back(pick);
                    }
                    return true;
                };
            logovger::CharacterSheet sheet;
            std::string error;
            logovger::run_chargen(request, world, dice, sheet, error,
                                  &reached);
            ++lives;
        }
    }

    std::vector<std::string> never;
    std::map<std::string, std::pair<int, int>> by_type;  // reached, total
    for (const auto& rule : absorbed) {
        const std::string type = world.getType(rule.first);
        ++by_type[type].second;
        if (reached.count(rule.first)) {
            ++by_type[type].first;
            continue;
        }
        never.push_back(rule.second);
    }
    std::cout << "  [measure] " << lives << " lives over " << careers.size()
              << " careers reached " << reached.size() << " of "
              << absorbed.size() << " absorbed outcomes, "
              << never.size() << " never\n";
    for (const auto& row : by_type) {
        std::cout << "  [measure]   " << row.first << ": "
                  << row.second.first << "/" << row.second.second << "\n";
    }

    // An outcome nobody rolled this run is sampling noise. A whole
    // TABLE with not one row reached is the rank 0 shape: absorbed,
    // sound, and wired to nothing.
    int dead_tables = 0, tables = 0;
    std::string dead;
    for (const auto& entry : registry.entityTypes()) {
        const bool container =
            registry.isSubtypeOf(entry.first, "RollableTable") ||
            registry.isSubtypeOf(entry.first, "ProgressionTrack");
        if (!container) continue;
        for (const auto table : world.findByType(entry.first)) {
            int rows = 0, hit = 0;
            for (const auto row : world.getRelated(table, "HAS_PART")) {
                const std::string outcome =
                    world.getProperty(row, "outcome").empty()
                        ? world.getProperty(row, "grants")
                        : world.getProperty(row, "outcome");
                if (outcome.empty()) continue;
                ++rows;
                if (reached.count(
                        static_cast<kg::EntityID>(std::stoul(outcome)))) {
                    ++hit;
                }
            }
            if (rows == 0) continue;
            ++tables;
            if (hit == 0) {
                ++dead_tables;
                dead += (dead.empty() ? "" : "; ") +
                        world.getProperty(table, "name") + " (" +
                        std::to_string(rows) + " rows)";
            }
        }
    }
    std::cout << "  [measure] " << dead_tables << " of " << tables
              << " tables reached nothing at all\n";

    // The gate. One row reached is enough to prove a table is wired,
    // so this survives sampling: which row the dice pick varies, that
    // ANY row can be picked does not. A table at zero is either
    // unreachable or unchosen, and both are worth a build failure.
    CHECK(dead_tables == 0,
          "every absorbed table is reached by some life: " + dead);
    // And the sweep must keep doing real work. Without this, narrowing
    // it to one career would turn the gate above green.
    CHECK(reached.size() * 2 > absorbed.size(),
          "the sweep exercises most of the book: " +
              std::to_string(reached.size()) + " of " +
              std::to_string(absorbed.size()));
}

// The gate above asks whether a rule was EXECUTED. This one asks the
// easier question it does not cover: was the type ever INSTANTIATED at
// all. Eleven types were declared in the rulebook pack and no seed
// ever created one, JudgmentPoint.prompt_text among them, which is
// documented as the brief handed to the referee and is read by nothing
// while the adjudicator builds its prompt from a hardcoded string.
//
// Same shape as the rank 0 grants: declared, sound, cited, reached by
// nothing. A reader census at the level of types does not catch it,
// because a census is satisfied by one reader touching one instance.
// This catches the strictly easier case of no instances at all, and it
// reads the vocabulary from the registry so a type added tomorrow is
// covered without anyone maintaining a list.
void test_every_declared_rule_type_has_an_instance() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the instantiation world: " + why);
    if (!why.empty()) return;

    // Scoped to the vocabulary the BOOK and its packs declare. The
    // engine's own types (Humanoid, Wall, LightSource) have no business
    // existing in a chargen world and their absence proves nothing.
    const std::set<std::string> book_sources = {
        "https://logosphere.dev/packs/rulebook",
        "https://logosphere.dev/logovger/cepheus/book1-character-creation",
        "https://logosphere.dev/logovger/cepheus/book1-skills",
    };

    // Lives are played FIRST, because the population splits in two and
    // a seed world alone confuses them. Book content (Career,
    // TableEntry, AdvanceSkill) is created by seeds and is present at
    // load. State (SkillRating, ProgressionStanding, CurrencyBalance)
    // exists only once a character has lived, and reporting those as
    // never-instantiated would be measuring an empty room and calling
    // it a finding.
    for (const std::string& career : {std::string("Navy"),
                                      std::string("Scout"),
                                      std::string("Noble")}) {
        for (uint64_t seed = 1; seed <= 4; ++seed) {
            logosphere::dice::DiceService dice;
            logovger::ChargenRequest request;
            request.career_name = career;
            request.seed = seed;
            request.max_terms = 7;
            // Somebody has to answer the aging table. Without a
            // selector the engine refuses, the life ends at the fourth
            // term, and every type that only exists once a decision is
            // made stays absent for a reason that has nothing to do
            // with the wiring this test checks.
            request.attribute_selector =
                [](const logosphere::rules::AttributeSelectionRequest& ask,
                   std::vector<std::string>& chosen, std::string& error) {
                    for (int i = 0; i < ask.count; ++i) {
                        std::string pick;
                        for (const auto& name : ask.eligible) {
                            if (std::find(ask.already_taken.begin(),
                                          ask.already_taken.end(), name) !=
                                ask.already_taken.end()) continue;
                            if (std::find(chosen.begin(), chosen.end(),
                                          name) != chosen.end()) continue;
                            pick = name;
                            break;
                        }
                        if (pick.empty()) {
                            error = "nothing left to take";
                            return false;
                        }
                        chosen.push_back(pick);
                    }
                    return true;
                };
            logovger::CharacterSheet sheet;
            std::string error;
            logovger::run_chargen(request, world, dice, sheet, error);
        }
    }

    const auto& registry = world.getRegistry();
    std::vector<std::string> empty;
    int checked = 0, declared = 0;
    for (const auto& entry : registry.entityTypes()) {
        const auto& def = entry.second;
        if (def.is_abstract) continue;
        if (!book_sources.count(def.source)) continue;
        ++checked;
        if (!world.findByType(entry.first).empty()) continue;
        // A type may declare that it has no instance yet, in the
        // schema, with the reason written beside it. Declared absence
        // is a countable backlog; undeclared absence is the silence
        // this test exists to break.
        if (def.facets.count("no-instance-declared")) {
            ++declared;
            continue;
        }
        empty.push_back(entry.first);
    }
    std::sort(empty.begin(), empty.end());

    std::string undeclared;
    for (const auto& name : empty) {
        undeclared += (undeclared.empty() ? "" : ", ") + name;
    }
    std::cout << "  [measure] " << checked
              << " concrete types declared by the book and its packs, "
              << declared << " declared to have no instance yet, "
              << empty.size() << " silently empty\n";

    CHECK(checked > 20,
          "the scope actually found the book's vocabulary: " +
              std::to_string(checked) + " types");
    // The gate. A type with no instance is allowed, and saying so in
    // the schema is the price. Silence is not.
    CHECK(empty.empty(),
          "every declared type is instantiated or says why not: " +
              undeclared);
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

// "Each career has a survival roll. If you fail this roll, your
// character is dead... A natural 2 is always a failure." The floor is
// the book's number, held in the graph, and a life that snake-eyes its
// survival dies however good its Endurance was.
void test_a_natural_two_kills_however_good_the_endurance() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the natural-failure world loads: " + why);

    // The coincidence the rule is FOR - a natural 2 that a DM had
    // already carried over the target - needs a 4+ career and a
    // characteristic of 12, and does not turn up in a sweep. So the
    // rule is proved where it is decidable: the floor is data, and
    // moving it moves who lives.
    const auto floor_constant = [](kg::KGModule& w) {
        for (const auto id : w.findByType("RuleConstant")) {
            if (w.getProperty(id, "name") == "survival_natural_failure") {
                return id;
            }
        }
        return kg::INVALID_ENTITY;
    };
    CHECK(floor_constant(world) != kg::INVALID_ENTITY,
          "the book's floor is in the graph, not in the procedure");

    // Raise the floor to 12 and every 2D6 survival throw is a natural
    // failure, whatever the DM. Nobody may serve a term.
    kg::KGModule raised(game_registry());
    CHECK(build_world(raised, why), "the raised-floor world loads: " + why);
    raised.setProperty(floor_constant(raised), "constant_value", "12");

    int lived = 0, died = 0, said_natural = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        logovger::CharacterSheet sheet;
        std::string error;
        logovger::run_chargen(req, raised, dice, sheet, error);
        if (sheet.terms_served > 0) ++lived;
        for (const auto& event : sheet.life) {
            if (event.detail.find("always a failure") != std::string::npos) {
                ++said_natural;
                ++died;
                break;
            }
        }
    }
    std::cout << "  [measure] floor at 12: " << died
              << "/40 lives lost a survival throw to the dice, " << lived
              << " served a term\n";
    CHECK(died > 0 && said_natural == died,
          "raising the floor kills on the dice alone, and every such death "
          "says so in the timeline");
    CHECK(lived == 0,
          "with every natural result at or under the floor, no character "
          "survives a term: " + std::to_string(lived) + " did");

    // The control: the book's own floor of 2 leaves most lives alone,
    // so the kill above is the constant's doing and not the harness's.
    int control_lived = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenRequest req;
        req.career_name = "Agent";
        req.seed = seed;
        req.max_terms = 7;
        logovger::CharacterSheet sheet;
        std::string error;
        logovger::run_chargen(req, world, dice, sheet, error);
        if (sheet.terms_served > 0) ++control_lived;
    }
    std::cout << "  [measure] floor at 2 (the book's): " << control_lived
              << "/40 served a term\n";
    CHECK(control_lived > 0,
          "the same seeds under the book's floor do serve terms, so the "
          "difference is the data and not the sweep");

    // And missing is a stop, not a default of zero.
    kg::KGModule stripped(game_registry());
    CHECK(build_world(stripped, why), "the stripped world loads: " + why);
    stripped.removeProperty(floor_constant(stripped), "constant_value");
    logosphere::dice::DiceService dice;
    logovger::ChargenRequest req;
    req.career_name = "Agent";
    req.seed = 1;
    req.max_terms = 1;
    logovger::CharacterSheet sheet;
    std::string error;
    const bool ran = logovger::run_chargen(req, stripped, dice, sheet, error);
    CHECK(!ran && error.find("survival_natural_failure") != std::string::npos,
          "a survival throw with no floor in the graph stops the run rather "
          "than quietly having none: " + error);
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

    // The claim is about QUALIFYING, not about the life ending. A
    // refused character goes to the Draft or the Drifter, and the
    // auto-player now takes one rather than giving up, so the life
    // continues past the refusal. Asserting terms_served == 0 was
    // asserting that the harness stopped.
    CHECK(!sheet.qualified,
          "the same seed that qualified a moment ago now does not: raising "
          "the target in the KG alone turned it away, and the runner "
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
    // The claim is that a row of the modifier table drives the throw,
    // and the throw cites the DM it used. It USED to also assert the
    // run stopped at Draft-or-Drifter, which was asserting a limit of
    // the harness rather than anything about the rule: the auto-player
    // answers that question now, so a refused character carries on.
    CHECK(control_ok && control_sheet.qualified &&
              !changed_sheet.qualified && cited_changed_modifier,
          "changing only characteristic_modifier data changes the same "
          "qualification throw, and the throw cites the DM that changed "
          "it: " + error);

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
    // Seed 28: four terms, four gains, three ratings, one of them at
    // level 2. That repeat is what this case measures, and it takes a
    // specific life to produce now that promotions buy extra training
    // rolls and each roll may land on a different skill.
    logovger::ChargenRequest request{"Agent", 28, 4};
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

    // Point this table at DIFFERENT dice rather than editing the ones
    // it shares. One seed owns "1D6" and every table in the book
    // references it, so mutating that entity changes the benefit
    // tables too and they stop covering their own rows. Swapping the
    // reference is also the truer test: it proves the table's dice
    // slot drives selection.
    const auto shifted = world.createEntity("DiceExpression");
    world.setProperty(shifted, "name", "1D6+6 (test)");
    world.setProperty(shifted, "dice_count", "1");
    world.setProperty(shifted, "dice_sides", "6");
    world.setProperty(shifted, "dice_modifier", "6");
    world.setProperty(table, "dice", std::to_string(shifted));
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
    // Every training roll of the term, not just the last: a character
    // who is promoted rolls twice, and both must come off the table
    // this test rewrote. Counting rolls rather than skills also keeps
    // the assertion about the TABLE, since two rolls can land on the
    // same skill and raise it instead of granting a second.
    size_t training_rolls = 0, off_the_changed_table = 0;
    std::string totals;
    for (const auto& roll : dice.journal()) {
        if (roll.purpose != "skills and training") continue;
        ++training_rolls;
        totals += (totals.empty() ? "" : ",") + std::to_string(roll.total);
        if (roll.expression.modifier == 6 && roll.total >= 7 &&
            roll.total <= 12) {
            ++off_the_changed_table;
        }
    }
    CHECK(ok && !sheet.skills.empty() && training_rolls > 0 &&
              off_the_changed_table == training_rolls,
          "changing the table's DiceExpression and bands changes selection "
          "without procedure code changes: ran=" + std::to_string(ok) +
              " skills=" + std::to_string(sheet.skills.size()) +
              " training rolls=" + std::to_string(training_rolls) +
              " on the changed table=" +
              std::to_string(off_the_changed_table) + " totals=" + totals +
              " " + error);
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

// The procedure's vocabulary cannot grow quietly.
//
// A primitive is the C++ behind one step; the registry declares its
// name and the exit labels a seed may route on. Name plus exits is the
// route contract, and it is what keeps the procedure data rather than
// code, which makes the set of names a design surface rather than an
// implementation detail. RPG_MODULE.md OPEN item 1 says new ones
// surface for the owner's approval.
//
// Measured on 2026-08-15: the set grew from 8 to 17 and not one of the
// nine additions was ever surfaced, #59 adding five in a single PR.
// Nothing was harmed and nothing noticed, which is the same shape as
// the rank 0 grants and the endpoints that validated nothing. A rule
// with no gate is an intention.
//
// This does not prevent an addition and does not pretend to. It makes
// one impossible to add QUIETLY: the author must edit
// APPROVED_PRIMITIVES in the same commit, and that diff says in one
// line that the procedure's vocabulary changed. A review trigger, not
// a permission system.
void test_no_primitive_enters_the_procedure_unapproved() {
    const std::string text =
        slurp(game_path("chargen/APPROVED_PRIMITIVES"));
    CHECK(!text.empty(), "the approved-primitive list is readable");
    if (text.empty()) return;

    std::map<std::string, std::string> approved;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string labels = line.substr(colon + 1);
        const auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(
                                     s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(
                                     s.back()))) s.pop_back();
        };
        trim(name);
        trim(labels);
        if (name.empty()) continue;
        approved[name] = labels;
    }

    const auto registry = logovger::make_chargen_procedure_registry();
    std::map<std::string, std::string> live;
    for (const auto& name : registry.declared_names()) {
        const auto* contract = registry.contract(name);
        std::vector<std::string> labels(contract->route_labels.begin(),
                                        contract->route_labels.end());
        std::sort(labels.begin(), labels.end());
        std::string joined;
        for (const auto& label : labels) {
            joined += (joined.empty() ? "" : ",") + label;
        }
        live[name] = joined;
    }

    std::string unapproved, missing, drifted;
    for (const auto& entry : live) {
        const auto found = approved.find(entry.first);
        if (found == approved.end()) {
            unapproved += (unapproved.empty() ? "" : ", ") + entry.first;
        } else if (found->second != entry.second) {
            drifted += (drifted.empty() ? "" : "; ") + entry.first +
                       " declares [" + entry.second + "], approved as [" +
                       found->second + "]";
        }
    }
    for (const auto& entry : approved) {
        if (!live.count(entry.first)) {
            missing += (missing.empty() ? "" : ", ") + entry.first;
        }
    }

    std::cout << "  [measure] " << live.size()
              << " primitives declared, " << approved.size()
              << " approved\n";
    CHECK(unapproved.empty(),
          "no primitive enters the procedure without approval. Bring it to "
          "the owner, then add it to chargen/APPROVED_PRIMITIVES in the "
          "same commit. Unapproved: " + unapproved);
    // Both directions. A route contract that loosens silently changes
    // what every seed may do, and a primitive removed from the registry
    // while the list still promises it is equally a lie.
    CHECK(drifted.empty(), "route contracts match what was approved: " +
                               drifted);
    CHECK(missing.empty(),
          "the approved list does not promise primitives the registry no "
          "longer declares: " + missing);
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
    // Seed 28: four terms, four gains, three ratings, one of them at
    // level 2. That repeat is what this case measures, and it takes a
    // specific life to produce now that promotions buy extra training
    // rolls and each roll may land on a different skill.
    logovger::ChargenRequest request{"Agent", 28, 4};
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
    // Seed 28: four terms, four gains, three ratings, one of them at
    // level 2. That repeat is what this case measures, and it takes a
    // specific life to produce now that promotions buy extra training
    // rolls and each roll may land on a different skill.
    logovger::ChargenRequest request{"Agent", 28, 4};
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

// ------------------------------------------------- the aging crisis
//
// "If any characteristic is reduced to 0 by aging, then the character
// suffers an aging crisis. The character dies unless he can pay
// 1D6x10,000 Credits for medical care, which will bring any
// characteristics back up to 1. The character automatically fails any
// Qualification checks from now on."
//   -- srd/cepheus/book1/character-creation.md, "Aging Crisis"
//
// Every clause of it is driven here: the bill is quoted before it is
// answered and is the 1D6x10,000 the engine rolled, paying costs
// exactly that and brings every ruined characteristic back to 1 and
// nothing else with it, the survivor is refused by the next career
// WITHOUT a die being spent on the refusal, refusing the care ends
// the character, and a character short of the price is never offered
// the bargain at all.
//
// run_chargen cannot get there. It answers the crisis, but it takes
// only the one career it was named and it refuses the Draft, so it
// can never bank the benefits of a first career and carry them into a
// second, which is the only way a character holds Credits when the
// aging table finally bites. This drives ChargenSession by hand
// instead: a player who serves every term the book offers, takes cash
// while cash is allowed, and never finishes early.
//
// The seed is not hardcoded. The test sweeps until a life arrives at
// the crisis holding enough to settle it AND lives long enough
// afterwards to be turned away by a career, then asserts against THAT
// life. Finding none is a failure in itself.

// The referee this test installs takes the WEAKEST eligible
// characteristic first. The book fixes how many aging takes and
// leaves which to whoever applies the rule, so this is a legal
// referee, and it is the one that walks a life towards the crisis
// instead of away from it: a rule that reduces one characteristic by
// 2 and two more by 1 lands all of it on whatever is already lowest.
logosphere::rules::AttributeSelector weakest_first_referee() {
    return [](const logosphere::rules::AttributeSelectionRequest& request,
              std::vector<std::string>& chosen, std::string& error) {
        std::vector<bool> taken(request.eligible.size(), false);
        for (int i = 0; i < request.count; ++i) {
            int weakest = -1;
            for (size_t k = 0; k < request.eligible.size(); ++k) {
                if (taken[k]) continue;
                if (weakest < 0 ||
                    request.current[k] < request.current[weakest]) {
                    weakest = static_cast<int>(k);
                }
            }
            if (weakest < 0) {
                error = "the group has fewer attributes than the rule takes";
                return false;
            }
            taken[weakest] = true;
            chosen.push_back(request.eligible[weakest]);
        }
        return true;
    };
}

// A player who wants the life to go on: serve every term, train on
// service skills, take the cash benefit while the book still allows
// one, take the next career rather than finishing. Deterministic, so
// the same seed replays the same life and the crisis can be answered
// twice, once each way.
class LongLife {
public:
    explicit LongLife(logovger::ChargenSession& session)
        : session_(session) {}

    // Answers questions until one whose prompt contains `stop_at`
    // arrives. False when the life ended, an answer was refused, or
    // the question never came.
    bool run_until(const std::string& stop_at, std::string& error) {
        for (int answered = 0; answered < 500; ++answered) {
            if (session_.finished() || session_.choices().empty()) {
                error = "the life ended at '" + session_.prompt() +
                        "' before '" + stop_at + "'";
                return false;
            }
            if (session_.prompt().find(stop_at) != std::string::npos) {
                return true;
            }
            if (!session_.choose(answer(), error)) return false;
        }
        error = "500 answers and no '" + stop_at + "'";
        return false;
    }

    // From here on, leave the career at the end of the term. Reaching
    // a fresh Qualification check means leaving the one you are in.
    void leave_at_the_end_of_this_term() { leaving_ = true; }

private:
    std::string answer() const {
        const auto& choices = session_.choices();
        const std::string& prompt = session_.prompt();
        if (prompt.find("which table do you train on") != std::string::npos ||
            prompt.find("training roll(s). Which table?") !=
                std::string::npos) {
            for (const auto& choice : choices) {
                if (choice.label.size() > 14 &&
                    choice.label.compare(choice.label.size() - 14, 14,
                                         "Service Skills") == 0) {
                    return choice.key;
                }
            }
        }
        if (prompt.find("benefit roll(s) left") != std::string::npos) {
            // Cash, because the crisis is priced in Credits and a
            // character with none is never offered the choice.
            for (const auto& choice : choices) {
                if (choice.label.find("Cash Benefits") != std::string::npos ||
                    choice.label.find("Cost Benefits") != std::string::npos) {
                    return choice.key;
                }
            }
        }
        if (prompt.find("is over. What now?") != std::string::npos) {
            return leaving_ ? "2" : "1";
        }
        // Careers first, "finish" last: never finish while the book
        // still offers a career.
        for (const auto& choice : choices) {
            if (choice.key != "finish") return choice.key;
        }
        return choices.front().key;
    }

    logovger::ChargenSession& session_;
    bool leaving_ = false;
};

// The price the crisis quoted, read out of the question the player was
// asked rather than out of the code that asked it.
long long quoted_price(const std::string& prompt) {
    const auto at = prompt.find("Care costs Cr");
    if (at == std::string::npos) return -1;
    return std::stoll(prompt.substr(at + std::strlen("Care costs Cr")));
}

const char* const kCharacteristics[] = {"strength", "dexterity", "endurance",
                                        "intelligence", "education",
                                        "social_standing"};

std::vector<int> characteristics_of(const kg::KGModule& world,
                                    kg::EntityID character) {
    std::vector<int> out;
    for (const char* slot : kCharacteristics) {
        const auto text = world.getProperty(character, slot);
        out.push_back(text.empty() ? -1 : std::stoi(text));
    }
    return out;
}

// A training answer names a TABLE. It used to be resolved by position
// into a list recomputed on resume, and the Advanced Education gate
// made that list change shape mid-term: roll "+1 Edu" from 7 to 8 and
// a fourth table appears, shifting every option after it.
void test_a_training_answer_names_a_table_not_a_position() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the training-choice world loads: " + why);
    if (!why.empty()) return;

    std::string error;
    int reached = 0, shifted = 0;
    for (uint64_t seed = 1; seed <= 400 && shifted == 0; ++seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenSession session(world, dice);
        session.set_attribute_selector(weakest_first_referee());
        if (!session.begin(seed, error)) continue;
        LongLife player(session);
        if (!player.run_until("which table do you train on", error)) continue;
        ++reached;

        // Take whatever the offer's LAST slot is, and remember which
        // table that key promised. If the list grows before the answer
        // is resolved, a position-based lookup lands elsewhere.
        const auto offered = session.choices();
        if (offered.empty()) continue;
        const auto& taken = offered.back();
        const kg::EntityID promised = taken.subject;
        const std::string promised_name =
            world.getProperty(promised, "name");

        // Put the character one point below the gate so the very next
        // grant can open a table and change the list.
        world.setProperty(session.sheet().id, "education", "7");
        if (!session.choose(taken.key, error)) continue;

        bool rolled_the_promised_table = false;
        for (const auto& event : session.sheet().life) {
            if (event.detail == promised_name) rolled_the_promised_table = true;
        }
        CHECK(rolled_the_promised_table,
              "the table that was rolled is the table the answer named: "
              "asked for '" + promised_name + "'");
        ++shifted;
        std::cout << "  [measure] seed " << seed << ": answered '"
                  << taken.key << "' = " << promised_name
                  << ", rolled on it\n";
    }
    std::cout << "  [measure] " << reached
              << " lives reached a training choice\n";
    CHECK(reached > 0 && shifted > 0,
          "some life reaches a training choice; none did, so this proves "
          "nothing");
}

// "You must either submit to the Draft or take the Drifter career for
// this term." Two answers, two different lives, and the game has to act
// on the one it was handed.
//
// On Windows it did not. The answer was a POINTER into the offer list,
// the list was cleared before the answer was finished with, and the
// read that decided Draft-versus-Drifter happened after the Choice it
// pointed at had been destroyed. libc++ and libstdc++ leave a cleared
// vector's bytes alone, so on macOS and Linux "2" was still "2" and
// nothing looked wrong for as long as those were the only compilers.
// MSVC's std::string destructor zeroes the small-string buffer, so "2"
// read as "" there: every character who submitted to the Draft became a
// Drifter instead, the six EnterCareer outcomes behind the Draft table
// were reached by nobody, and headless-windows failed on the one gate
// that asks whether an absorbed table is ever reached.
//
// This test asserts the claim that broke: the answer decides. It is
// checked BOTH ways, because "always Drifter" is exactly what the bug
// produced and a one-sided check would have passed through it.
void test_the_draft_answer_decides_which_career_takes_you() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the draft-answer world loads: " + why);
    if (!why.empty()) return;

    // Play the first seed that is turned away by its career, twice,
    // answering the same question each way. The seed is not hardcoded:
    // whether a life fails qualification is a throw, so the sweep finds
    // one and asserts against THAT life.
    const std::string kTurnedAway = "will not have you this term";
    const auto play = [&](uint64_t seed, const std::string& answer,
                          logovger::CharacterSheet& out,
                          std::string& error) -> bool {
        logosphere::dice::DiceService dice;
        logovger::ChargenSession session(world, dice);
        session.set_attribute_selector(weakest_first_referee());
        if (!session.begin(seed, error)) return false;
        LongLife player(session);
        if (!player.run_until(kTurnedAway, error)) return false;
        if (!session.choose(answer, error)) return false;
        out = session.sheet();
        return true;
    };

    uint64_t turned_away_seed = 0;
    logovger::CharacterSheet drafted, drifted;
    std::string error;
    for (uint64_t seed = 1; seed <= 400 && turned_away_seed == 0; ++seed) {
        logovger::CharacterSheet sheet;
        if (!play(seed, "2", sheet, error)) continue;
        turned_away_seed = seed;
        drafted = sheet;
    }
    CHECK(turned_away_seed != 0,
          "some life is turned away and asked Draft-or-Drifter; none was, "
          "so this proves nothing: " + error);
    if (turned_away_seed == 0) return;
    CHECK(play(turned_away_seed, "1", drifted, error),
          "the same life replays and takes the Drifter answer: " + error);

    const auto life_says = [](const logovger::CharacterSheet& sheet,
                              const std::string& prefix) {
        for (const auto& event : sheet.life) {
            if (event.what.rfind(prefix, 0) == 0) return event.what;
        }
        return std::string();
    };
    const std::string drafted_into = life_says(drafted, "drafted into the ");
    const std::string became = life_says(drifted, "became a Drifter");

    std::cout << "  [measure] seed " << turned_away_seed
              << " turned away; answering the Draft gives '"
              << drafted.career << "', answering Drifter gives '"
              << drifted.career << "'\n";

    // Submitting to the Draft rolls the book's 1D6 table and the row
    // names the service that takes you. Any of the six is right; being
    // a Drifter is not, because that is the OTHER answer.
    CHECK(!drafted_into.empty(),
          "submitting to the Draft is recorded as a draft: the timeline "
          "says nothing about being drafted");
    CHECK(drafted.career != "Drifter" && !drafted.career.empty(),
          "submitting to the Draft puts the character in a service, not "
          "the Drifter career: got '" + drafted.career + "'");
    CHECK(became == "became a Drifter",
          "and taking the Drifter answer is recorded as taking it: '" +
              became + "'");
    CHECK(drifted.career == "Drifter",
          "which puts the character in the Drifter career: got '" +
              drifted.career + "'");
    // The point of the pair: one question, two answers, two outcomes.
    // If the answer were being dropped, both runs would land in the
    // same place and every check above except this one could still
    // pass.
    CHECK(drafted.career != drifted.career,
          "the two answers produce two different careers; both gave '" +
              drafted.career + "', so the answer was not read");
}

// A mishap can ruin a characteristic, and the crisis it raises
// suspends the SAME step that asked "take the mishap, or die". The
// answer to the second question was read as an answer to the first: a
// second mishap rolled, two more years added, no money taken, and the
// characteristic left at 0.
// "For your first term in your first career, you get every skill in the
// service skills table at level 0. For your first term in subsequent
// careers, you may pick any one skill from the service skills table at
// level 0."
//
// Written BEFORE basic training's grant is converted from a hand-built
// SkillRating to EnsureSkillLevel outcome data, and asserting only what
// the book promises the CHARACTER, so it holds either way. A refactor
// that changes what a character ends up with is not a refactor.
void test_basic_training_grants_what_the_book_promises() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the basic-training world: " + why);
    if (!why.empty()) return;

    // ---- a first career gives every service skill, at level 0 ------
    logosphere::dice::DiceService dice;
    logovger::ChargenSession session(world, dice);
    std::string error;
    CHECK(session.begin(28, error), "the first-career life begins: " + error);
    CHECK(session.choose("Navy", error),
          "and takes the Navy: " + error);

    const auto& sheet = session.sheet();
    const auto expected = service_skill_names(world, "Navy");
    CHECK(expected.size() == 6,
          "the Navy service table grants six skills: " +
              std::to_string(expected.size()));
    const auto held = skills_held(world, sheet.id);
    std::string missing, wrong_level;
    for (const auto& name : expected) {
        const auto found = held.find(name);
        if (found == held.end()) {
            missing += (missing.empty() ? "" : ", ") + name;
        } else if (found->second != 0) {
            wrong_level += (wrong_level.empty() ? "" : ", ") + name + "@" +
                           std::to_string(found->second);
        }
    }
    std::cout << "  [measure] first career holds " << held.size()
              << " skills after basic training\n";
    CHECK(missing.empty(),
          "a first career grants EVERY skill on its service table; these "
          "are absent: " + missing);
    CHECK(wrong_level.empty(),
          "and every one of them at level 0; these are not: " + wrong_level);

    // ---- a later career gives exactly one ---------------------------
    // Driven to a second career, where the book grants a choice of one
    // rather than the lot.
    logosphere::dice::DiceService second_dice;
    logovger::ChargenSession second(world, second_dice);
    CHECK(second.begin(95, error), "the multi-career life begins: " + error);
    LongLife player(second);
    player.leave_at_the_end_of_this_term();
    if (!player.run_until("which service skill do they start you on",
                          error)) {
        std::cout << "  [measure] no life reached a second career's basic "
                     "training in this seed; the first-career half stands\n";
        return;
    }
    const auto before = skills_held(world, second.sheet().id);
    const auto offered = second.choices();
    CHECK(offered.size() == 6,
          "the second career offers its six service skills: " +
              std::to_string(offered.size()));
    CHECK(second.choose(offered.front().key, error),
          "one of them is taken: " + error);
    const auto after = skills_held(world, second.sheet().id);
    int gained = 0;
    for (const auto& [name, level] : after) {
        if (!before.count(name)) ++gained;
    }
    std::cout << "  [measure] second career granted " << gained
              << " new skill(s)\n";
    CHECK(gained <= 1,
          "a later career grants at most ONE new skill, not the table: " +
              std::to_string(gained));
}

void test_paying_for_an_injury_does_not_re_roll_the_mishap() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the injury-crisis world loads: " + why);
    if (!why.empty()) return;

    const auto mishaps_in = [](const logovger::CharacterSheet& sheet) {
        int count = 0;
        for (const auto& event : sheet.life) {
            if (event.what.rfind("mishap: ", 0) == 0) ++count;
        }
        return count;
    };

    uint64_t found = 0;
    int crises = 0;
    std::string error;
    for (uint64_t seed = 1; seed <= 600 && found == 0; ++seed) {
        logosphere::dice::DiceService dice;
        logovger::ChargenSession session(world, dice);
        session.set_attribute_selector(weakest_first_referee());
        if (!session.begin(seed, error)) continue;
        LongLife player(session);
        if (!player.run_until("injury has taken", error)) continue;
        ++crises;
        bool can_pay = false;
        for (const auto& choice : session.choices()) {
            if (choice.key == "1") can_pay = true;
        }
        if (!can_pay) continue;

        const auto& sheet = session.sheet();
        const long long quoted = quoted_price(session.prompt());
        const long long held = sheet.credits;
        const int mishaps_before = mishaps_in(sheet);
        const int age_before = sheet.age_years;
        std::cout << "  [measure] seed " << seed << ": " << session.prompt()
                  << "\n";

        CHECK(session.choose("1", error), "the care is paid for: " + error);
        std::cout << "  [measure] after paying: Cr" << sheet.credits
                  << ", age " << sheet.age_years << ", " << mishaps_in(sheet)
                  << " mishap(s)\n";

        CHECK(sheet.credits == held - quoted,
              "paying an injury crisis costs what it quoted: Cr" +
                  std::to_string(held) + " - Cr" + std::to_string(quoted) +
                  " = Cr" + std::to_string(sheet.credits));
        CHECK(mishaps_in(sheet) == mishaps_before,
              "and rolls no second mishap: " +
                  std::to_string(mishaps_before) + " before, " +
                  std::to_string(mishaps_in(sheet)) + " after");
        CHECK(sheet.age_years == age_before,
              "and adds no further years: age " +
                  std::to_string(age_before) + " -> " +
                  std::to_string(sheet.age_years));
        bool still_ruined = false;
        for (const auto* slot : kCharacteristics) {
            if (world.getProperty(sheet.id, slot) == "0") still_ruined = true;
        }
        CHECK(!still_ruined, "and restores what the injury took: " + sheet.upp);
        CHECK(world.getProperty(sheet.id, "qualification_barred") == "true",
              "and marks the survivor, as the aging crisis does");
        found = seed;
    }
    std::cout << "  [measure] " << crises
              << " injury crises swept, first payable was seed " << found
              << "\n";
    CHECK(found != 0,
          "some life reaches an injury crisis it can pay for; none did, so "
          "this proves nothing");
}

void test_the_aging_crisis_is_paid_for_or_kills() {
    kg::KGModule world(game_registry());
    std::string why;
    CHECK(build_world(world, why), "the crisis world loads: " + why);
    if (!why.empty()) return;
    std::string error;

    // ---- find a life the aging table ruins ------------------------
    uint64_t found = 0;
    int swept = 0, crises = 0, payable = 0;
    // The other side of the same sentence: "the character dies UNLESS
    // he can pay". A character short of the price is not offered the
    // bargain at all, and the first such life proves it.
    uint64_t broke_seed = 0;
    std::string broke_prompt;
    std::vector<std::string> broke_keys;
    for (uint64_t seed = 1; seed <= 400 && found == 0; ++seed) {
        ++swept;
        logosphere::dice::DiceService dice;
        logovger::ChargenSession session(world, dice);
        session.set_attribute_selector(weakest_first_referee());
        if (!session.begin(seed, error)) continue;
        LongLife player(session);
        if (!player.run_until("to nothing. Care costs", error)) continue;
        ++crises;
        bool can_pay = false;
        for (const auto& choice : session.choices()) {
            if (choice.key == "1") can_pay = true;
        }
        if (!can_pay) {
            if (broke_seed == 0) {
                broke_seed = seed;
                broke_prompt = session.prompt();
                for (const auto& choice : session.choices()) {
                    broke_keys.push_back(choice.key);
                }
            }
            continue;
        }
        ++payable;
        // It must also outlive the crisis by enough to be offered a
        // career, or the "never qualifies again" half is unreachable.
        if (!session.choose("1", error)) continue;
        player.leave_at_the_end_of_this_term();
        if (!player.run_until("Another career, or finish?", error)) continue;
        found = seed;
    }
    std::cout << "  [measure] swept " << swept << " seeds, " << crises
              << " reached an aging crisis, " << payable
              << " of those could pay\n";
    CHECK(found != 0,
          "some life is ruined by aging, can pay for the care, and lives "
          "on to be refused a career; none did in " +
              std::to_string(swept) + " seeds");
    if (found == 0) return;
    std::cout << "  [measure] seed " << found
              << " is the first life the aging table ruins with money in "
                 "hand\n";

    // ---- the bill --------------------------------------------------
    logosphere::dice::DiceService dice;
    logovger::ChargenSession session(world, dice);
    session.set_attribute_selector(weakest_first_referee());
    CHECK(session.begin(found, error), "the ruined life begins: " + error);
    LongLife player(session);
    CHECK(player.run_until("to nothing. Care costs", error),
          "the replay reaches the same crisis: " + error);
    const auto& sheet = session.sheet();
    std::cout << logovger::format_life(sheet);

    const long long quoted = quoted_price(session.prompt());
    const logosphere::dice::DiceRoll* priced = nullptr;
    for (const auto& roll : dice.journal()) {
        if (roll.purpose == "medical care") priced = &roll;
    }
    CHECK(priced != nullptr && quoted == priced->total &&
              priced->expression.count == 1 && priced->expression.sides == 6 &&
              priced->expression.multiplier == 10000 && quoted >= 10000 &&
              quoted <= 60000,
          "the question names a price, and it is the 1D6x10,000 the engine "
          "rolled: quoted Cr" + std::to_string(quoted) + ", rolled Cr" +
              std::to_string(priced ? priced->total : 0));

    const auto before = characteristics_of(world, sheet.id);
    std::vector<size_t> ruined;
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] == 0) ruined.push_back(i);
    }
    CHECK(!ruined.empty(),
          "the crisis was raised because something reached 0, and the graph "
          "says so too");
    const long long held = sheet.credits;
    CHECK(held >= quoted,
          "the pay option is offered because the money is there: Cr" +
              std::to_string(held) + " against Cr" + std::to_string(quoted));
    std::string ruined_names;
    for (const auto i : ruined) {
        ruined_names += (ruined_names.empty() ? "" : ", ");
        ruined_names += kCharacteristics[i];
    }
    std::cout << "  [measure] term " << sheet.terms_served << ", UPP "
              << sheet.upp << ", " << ruined_names << " at 0, care Cr"
              << quoted << " against Cr" << held << " held\n";

    // ---- paying it --------------------------------------------------
    // The purse the sheet claims to hold, itemised, so a disagreement
    // between the cache and the parts names the parts.
    std::string purse_before;
    long long purse_sum_before = 0;
    for (const auto part : world.getRelated(sheet.id, "HAS_PART")) {
        if (world.getType(part) != "CurrencyBalance") continue;
        const auto amount = world.getProperty(part, "balance_amount");
        purse_before += (purse_before.empty() ? "" : "+") +
                        (amount.empty() ? std::string("<empty>") : amount);
        if (!amount.empty()) purse_sum_before += std::stoll(amount);
    }
    // The question the crisis asks is "can you pay", so the number it
    // quotes has to be the money that is there. It was not: a rule that
    // charged the character between mustering out and the crisis moved
    // the parts while the sheet's copy stood still.
    CHECK(purse_sum_before == held,
          "the Cr" + std::to_string(held) +
              " the crisis says the character holds is the money in the "
              "parts: " + purse_before + " = Cr" +
              std::to_string(purse_sum_before));
    CHECK(session.choose("1", error), "the care is paid for: " + error);
    const auto after = characteristics_of(world, sheet.id);
    CHECK(sheet.credits == held - quoted,
          "paying costs exactly what was quoted: Cr" + std::to_string(held) +
              " - Cr" + std::to_string(quoted) + " = Cr" +
              std::to_string(sheet.credits));
    CHECK(world.getProperty(sheet.id, "credits") ==
              std::to_string(held - quoted),
          "and the graph holds the same balance, not the old one: '" +
              world.getProperty(sheet.id, "credits") + "'");
    bool restored_to_one = true, untouched = true;
    for (size_t i = 0; i < after.size(); ++i) {
        const bool was_ruined =
            std::find(ruined.begin(), ruined.end(), i) != ruined.end();
        if (was_ruined && after[i] != 1) restored_to_one = false;
        if (!was_ruined && after[i] != before[i]) untouched = false;
    }
    CHECK(restored_to_one,
          "every characteristic that was 0 comes back at exactly 1, and no "
          "higher: " + sheet.upp);
    CHECK(untouched,
          "and the care touches nothing that was not at 0: " + sheet.upp);
    CHECK(world.getProperty(sheet.id, "qualification_barred") == "true",
          "the survivor is marked in the graph, where a referee reading it "
          "will see it: '" +
              world.getProperty(sheet.id, "qualification_barred") + "'");
    std::cout << "  [measure] paid Cr" << quoted << ", left Cr"
              << sheet.credits << ", UPP now " << sheet.upp << "\n";

    // NOT AN ASSERTION, AND NOT A CLEAN RESULT. The money the
    // character actually holds lives in CurrencyBalance parts, which
    // mustering out sums into sheet.credits. resolve_crisis debits the
    // sum and the Character's `credits` property, and leaves the parts
    // alone, so the purse still holds the full amount here. The next
    // muster-out re-derives the sheet from those parts and the payment
    // comes back. Measured and printed rather than asserted: fixing it
    // is a change to chargen.cpp, which is the owner's call, and
    // asserting either number would either go red or bless the wrong
    // one.
    long long purse = 0;
    for (const auto part : world.getRelated(sheet.id, "HAS_PART")) {
        if (world.getType(part) != "CurrencyBalance") continue;
        const auto amount = world.getProperty(part, "balance_amount");
        if (!amount.empty()) purse += std::stoll(amount);
    }
    CHECK(purse == sheet.credits,
          "the money itself was spent, not just the sheet's copy of it: "
          "purse Cr" + std::to_string(purse) + " vs sheet Cr" +
              std::to_string(sheet.credits));
    std::cout << "  [measure] purse Cr" << purse << ", sheet Cr"
              << sheet.credits << ", agreed\n";

    // ---- and never qualifying again ---------------------------------
    // "The character automatically fails any Qualification checks from
    // now on." Automatically: the check is not made at a penalty, so
    // no die is spent and none is cited.
    player.leave_at_the_end_of_this_term();
    CHECK(player.run_until("Another career, or finish?", error),
          "the survivor lives long enough to be offered another career: " +
              error);
    std::string next_career;
    for (const auto& choice : session.choices()) {
        if (choice.key != "finish") { next_career = choice.label; break; }
    }
    const size_t journal_before = dice.journal().size();
    const size_t life_before = sheet.life.size();
    CHECK(session.choose("1", error),
          "the survivor tries for the " + next_career + ": " + error);
    CHECK(dice.journal().size() == journal_before,
          "the refusal spends no die: journal " +
              std::to_string(journal_before) + " -> " +
              std::to_string(dice.journal().size()));
    CHECK(!sheet.qualified, "and the character did not qualify");
    bool turned_away = false;
    for (size_t i = life_before; i < sheet.life.size(); ++i) {
        if (sheet.life[i].what == "turned away by " + next_career &&
            sheet.life[i].roll_id == 0) {
            turned_away = true;
        }
    }
    CHECK(turned_away,
          "the life says the " + next_career + " turned them away, citing no "
          "roll");
    std::cout << "  [measure] barred qualification for the " << next_career
              << ": journal stayed at " << journal_before << " rolls\n";
    // A muster-out has happened since the payment, and it re-derives
    // the sheet from scratch out of the CurrencyBalance parts. It also
    // pays new benefits, so the total legitimately grows: what must
    // hold is that the two records still agree. Had the crisis debited
    // only the cache, the re-derive would have restored the spent
    // money and this would part company.
    long long purse_later = 0;
    for (const auto part : world.getRelated(sheet.id, "HAS_PART")) {
        if (world.getType(part) != "CurrencyBalance") continue;
        const auto amount = world.getProperty(part, "balance_amount");
        if (!amount.empty()) purse_later += std::stoll(amount);
    }
    CHECK(purse_later == sheet.credits,
          "purse and sheet still agree after a re-derive: purse Cr" +
              std::to_string(purse_later) + " vs sheet Cr" +
              std::to_string(sheet.credits));
    std::cout << "  [measure] after a muster-out re-derive: purse Cr"
              << purse_later << ", sheet Cr" << sheet.credits << "\n";

    // The control. The same question, asked of the same life before
    // any crisis, DOES spend a die: without this the check above
    // passes for a build where qualification never rolls at all.
    logosphere::dice::DiceService control_dice;
    logovger::ChargenSession control(world, control_dice);
    CHECK(control.begin(found, error), "the control life begins: " + error);
    CHECK(control.prompt().find("Which career do you try for?") !=
              std::string::npos,
          "the control is asked the same question: " + control.prompt());
    const size_t control_before = control_dice.journal().size();
    CHECK(control.choose("1", error), "the control tries for it: " + error);
    size_t qualification_rolls = 0;
    for (const auto& roll : control_dice.journal()) {
        if (roll.purpose == "qualification") ++qualification_rolls;
    }
    CHECK(control_dice.journal().size() > control_before &&
              qualification_rolls == 1,
          "an unbarred character spends exactly one qualification die on the "
          "same question (" +
              std::to_string(qualification_rolls) + " rolled, journal " +
              std::to_string(control_before) + " -> " +
              std::to_string(control_dice.journal().size()) + ")");

    // ---- refusing it -------------------------------------------------
    logosphere::dice::DiceService death_dice;
    logovger::ChargenSession death(world, death_dice);
    death.set_attribute_selector(weakest_first_referee());
    CHECK(death.begin(found, error), "the same life begins again: " + error);
    LongLife mortal(death);
    CHECK(mortal.run_until("to nothing. Care costs", error),
          "and reaches the same crisis: " + error);
    const auto& corpse = death.sheet();
    const long long unspent = corpse.credits;
    CHECK(quoted_price(death.prompt()) == quoted,
          "priced the same, because the seed is the same: Cr" +
              std::to_string(quoted_price(death.prompt())));
    CHECK(death.choose("2", error), "the care is refused: " + error);
    CHECK(death.finished(),
          "refusing the care ends the character there and then");
    CHECK(death.prompt().find("the care went unpaid") != std::string::npos,
          "and says why: " + death.prompt());
    CHECK(corpse.credits == unspent,
          "nothing was charged for care not taken: Cr" +
              std::to_string(corpse.credits));
    CHECK(world.getProperty(corpse.id, "qualification_barred").empty(),
          "the dead are not marked as barred, they are simply dead");
    bool still_ruined = true;
    for (const auto i : ruined) {
        if (world.getProperty(corpse.id, kCharacteristics[i]) != "0") {
            still_ruined = false;
        }
    }
    CHECK(still_ruined,
          "and nothing was restored: " + ruined_names + " are still 0");
    bool death_recorded = false;
    for (const auto& event : corpse.life) {
        if (event.what == "died of the aging" &&
            event.roll_id == priced->id) {
            death_recorded = true;
        }
    }
    CHECK(death_recorded,
          "the life records the death, citing the bill that went unpaid");
    std::cout << "  [measure] refusing ended the life at term "
              << corpse.terms_served << " with Cr" << corpse.credits
              << " still in hand\n";

    // ---- and the life that cannot pay at all -------------------------
    CHECK(broke_seed != 0,
          "some life reaches the crisis without the price; none did in " +
              std::to_string(swept) + " seeds");
    if (broke_seed != 0) {
        CHECK(broke_keys.size() == 1 && broke_keys.front() == "2",
              "a character who cannot pay is offered no bargain, only the "
              "end (" + std::to_string(broke_keys.size()) + " option(s))");
        CHECK(broke_prompt.find("which is not enough") != std::string::npos,
              "and is told why: " + broke_prompt);
        std::cout << "  [measure] seed " << broke_seed
                  << " reached the crisis broke: " << broke_prompt << "\n";
    }
}

}  // namespace

int main() {
    std::cout << "Logovger chargen (a life, from the book, in the graph)"
              << std::endl;
    test_a_life_is_generated();
    test_a_mishap_is_taken_instead_of_dying();
    test_aging_takes_what_the_referee_says_it_takes();
    test_a_training_answer_names_a_table_not_a_position();
    test_the_draft_answer_decides_which_career_takes_you();
    test_paying_for_an_injury_does_not_re_roll_the_mishap();
    test_the_aging_crisis_is_paid_for_or_kills();
    test_the_same_seed_replays_the_same_life();
    test_missing_rules_fail_loudly();
    test_a_career_pays_only_for_its_own_years();
    test_leaving_a_career_offers_another_one();
    test_basic_training_grants_what_the_book_promises();
    test_a_roll_names_the_rule_that_made_it();
    test_a_judgment_says_who_decided_and_why();
    test_joining_a_career_grants_its_rank_zero_skill();
    test_every_absorbed_rule_reaches_a_character();
    test_every_declared_rule_type_has_an_instance();
    test_missing_rule_constant_never_falls_back();
    test_extra_benefits_by_rank_are_a_table();
    test_the_books_numbers_are_all_data();
    test_a_step_can_declare_what_it_does();
    test_a_mishap_costs_the_years_its_row_states();
    test_renaming_every_table_changes_nothing();
    test_a_natural_two_kills_however_good_the_endurance();
    test_character_facts_use_the_modifier_table();
    test_the_rules_are_data();
    test_characteristic_modifier_table_drives_checks();
    test_skill_outcome_parameters_drive_the_executor();
    test_skill_table_dice_data_drives_selection();
    test_every_skill_table_row_is_validated_before_selection();
    test_no_primitive_enters_the_procedure_unapproved();
    test_procedure_data_drives_chargen_control_flow();
    test_unknown_runtime_primitive_fails_before_character_state();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
